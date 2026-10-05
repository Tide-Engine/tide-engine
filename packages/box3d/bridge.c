// The C side of box3d.tide: Box3D's worlds, and what the package's systems ask
// of them.
//
// A match's Box3D state is in the match, as an image (Physics.image), the
// geometry the image names (Physics.geometry) and a stamp that names it. C's
// worlds here are only where that state is stepped. Each is at a stamp, a tick
// finds the one at its match's stamp or has one made from the image, and the
// step leaves it at the next. So a rollback, a join, a reload or another match
// in the same program each just find no world at their stamp, and get one. Two
// matches at the same stamp are at the same state, and either's world serves
// both.
//
// A stamp is the stamp before it and everything that was done to the world
// since, hashed: Box3D's next state is its last and what's done to it, and
// nothing else. So it costs what was done, and the image is never read to
// name it.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/page.h"
#include "tide/time.h"

// Box3D's headers are its own, as its sources are: what they'd be warned for
// (a compiler's own spelling of "inline this") isn't this file's to fix.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "box3d/src/box3d/box3d.h"
#pragma clang diagnostic pop

// box3d.tide's types, field for field
typedef struct body {
    int64_t id;
    int32_t type;
    tide_float3 position;
    tide_quaternion rotation;
    tide_float3 velocity;
    tide_float3 angular_velocity;
    tide_float3 impulse;
    tide_float3 angular_impulse;
    float gravity_scale;
    float linear_damping;
    float angular_damping;
    tide_float3 step_position;
    tide_quaternion step_rotation;
    tide_float3 step_velocity;
    tide_float3 step_angular_velocity;
    bool bullet;
} body;

typedef struct box {
    tide_float3 size;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} box;

typedef struct sphere {
    float radius;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} sphere;

typedef struct capsule {
    float radius;
    float height;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} capsule;

typedef struct touch {
    int64_t a;
    int64_t b;
    bool began;
} touch;

_Static_assert(sizeof(body) == 160 && sizeof(box) == 40 && sizeof(sphere) == 32 && sizeof(capsule) == 36 && sizeof(touch) == 24,
               "box3d.tide's types changed: these are theirs");

// A step with fewer bodies awake than this runs on its own thread: sharing it
// out would cost more than it saves. The step itself is quicker shared from a
// couple of hundred bodies on, but a tick whose C starts jobs runs its systems
// on threads too (tide/jobs.h), and that's only made up for from about here.
#define SHARED_BODIES 512

#define WORLDS 4

typedef struct world {
    b3WorldId id;
    bool made;
    int workers;
    int64_t stamp;  // The state it's at: 0 for an empty one nothing stepped
    uint64_t chain; // The stamp, and what the running tick did to the world since
    uint64_t used;  // When a tick last had it
    int bases[B3_SNAPSHOT_SECTIONS]; // Where each section of its image goes: part of its state, as the image says them
    int image_size;                  // Its last image's, which is about the next one's: how much room to ask for
    uint8_t *before; // Measuring: the last image, to count what changed of it
    int before_size;
} world;

static world worlds[WORLDS];
static uint64_t uses;
static world *active; // The running tick's
static bool shared;   // The running step's tasks go to other threads
static bool narrow;   // Worlds solve 4 contacts at a time, though the CPU can 8
static int32_t shared_steps;
static int32_t restores;
static bool measuring; // No clock is read otherwise
static uint64_t step_ns;
static uint64_t image_ns;
static uint64_t blocks_changed; // Of the images taken while measuring: their 16 KiB blocks that weren't as the image before had them
static uint64_t blocks_in_all;
static uint64_t counting_ns; // What that counting took, which no match pays

// ---------------------------------------------------------------------------
// Box3D's tasks are the tick's (tide/jobs.h)

static void *enqueue(b3TaskCallback *task, void *context, void *user, const char *name)
{
    (void)user;
    (void)name;
    if (shared) {
        const tide_job job = tide_job_start(task, context);
        return job ? (void *)(uintptr_t)job : NULL; // NULL: it ran already
    }
    task(context);
    return NULL;
}

static void finish(void *task, void *user)
{
    (void)user;
    tide_job_wait((tide_job)(uintptr_t)task);
}

#ifdef __wasm__
// On the web, memory another thread grew can't be used until this one hears of
// it, which the engine's allocator sees to (tide/page.h).
static void *allocate(const size_t size, const int32_t alignment)
{
    const size_t align = alignment < 16 ? 16u : (size_t)alignment;
    uint8_t *raw = tide_alloc(size + align + sizeof raw);
    uint8_t *p = (uint8_t *)(((uintptr_t)raw + sizeof raw + align - 1u) & ~(uintptr_t)(align - 1u));
    memcpy(p - sizeof raw, &raw, sizeof raw);
    return p;
}

static void release(void *memory, const size_t size)
{
    (void)size;
    if (!memory) return;
    uint8_t *raw;
    memcpy(&raw, (uint8_t *)memory - sizeof raw, sizeof raw);
    free(raw);
}
#endif

// As many workers as the tick has threads: a world steps the same with any.
static int workers_now(void)
{
    const uint32_t threads = tide_job_threads();
    return threads > B3_MAX_WORKERS ? B3_MAX_WORKERS : (int)threads;
}

static void make(world *w, const int workers)
{
#ifdef __wasm__
    static bool set;
    if (!set) b3SetAllocator(allocate, release);
    set = true;
#endif
    b3WorldDef def = b3DefaultWorldDef();
    def.workerCount = (uint32_t)workers;
    def.enqueueTask = enqueue;
    def.finishTask = finish;
    w->id = b3CreateWorld(&def);
    if (narrow) b3World_EnableSSE2Fallback(w->id, true);
    w->made = true;
    w->workers = workers;
    w->stamp = 0;
    w->chain = 0;
    memset(w->bases, 0, sizeof w->bases);
    w->image_size = 0;
}

static void unmake(world *w)
{
    if (w->made) b3DestroyWorld(w->id);
    w->made = false;
}

static b3BodyId body_of(const world *w, const int64_t id)
{
    return (b3BodyId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

// A body's or a shape's handle without its world, which is whichever of C's
// worlds the match's state is in.
static int64_t handle(const int32_t index1, const uint16_t generation)
{
    return (int64_t)((uint64_t)(uint32_t)index1 | (uint64_t)generation << 32);
}

// Something done to the tick's world, into its stamp: what, and with what.
// Everything that changes the world says so here, or two worlds that differ
// would go by one stamp.
static void note(const char what, const void *with, const size_t size)
{
    active->chain = tide_hash_more(tide_hash_more(active->chain, &what, 1), with, size);
}

static b3Vec3 vec(const tide_float3 v)
{
    return (b3Vec3){v.x, v.y, v.z};
}

static tide_float3 float3(const b3Vec3 v)
{
    return (tide_float3){v.x, v.y, v.z};
}

static bool same(const tide_float3 a, const tide_float3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool same_turn(const tide_quaternion a, const tide_quaternion b)
{
    return a.value.x == b.value.x && a.value.y == b.value.y && a.value.z == b.value.z && a.value.w == b.value.w;
}

static tide_quaternion turn(const b3Quat q)
{
    return (tide_quaternion){{q.v.x, q.v.y, q.v.z, q.s}};
}

// A rotation Box3D takes: what code wrote, of length 1. One that's that
// already goes as it is, so a body's rotation reads back as it was written.
static b3Quat quat(const tide_quaternion q)
{
    const b3Quat as_written = {{q.value.x, q.value.y, q.value.z}, q.value.w};
    return b3IsValidQuat(as_written) ? as_written : b3NormalizeQuat(as_written);
}

// ---------------------------------------------------------------------------
// The tick's world

bool b3t_open(const int64_t stamp)
{
    const int workers = workers_now();
    for (int i = 0; i < WORLDS; i++) {
        world *w = &worlds[i];
        if (!w->made || w->stamp != stamp || w->workers != workers) continue;
        w->used = ++uses;
        w->chain = (uint64_t)stamp;
        active = w;
        return true;
    }
    return false;
}

void b3t_restore(const uint8_t *image, const int32_t size, const uint8_t *geometry, const int32_t geometry_size, const int64_t stamp)
{
    const int workers = workers_now();
    world *w = NULL;
    for (int i = 0; i < WORLDS && !w; i++) {
        if (!worlds[i].made) w = &worlds[i];
    }
    for (int i = 0; i < WORLDS && !w; i++) { // None to spare: the one a tick had longest ago
        bool oldest = true;
        for (int k = 0; k < WORLDS; k++) oldest = oldest && worlds[i].used <= worlds[k].used;
        if (oldest) w = &worlds[i];
    }
    // An empty one for a match that starts, and for other workers than it has
    if (w->made && (size == 0 || w->workers != workers)) unmake(w);
    if (!w->made) make(w, workers);
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    restores += size > 0;
    if (size > 0 && !b3World_Restore(w->id, image, size, geometry, geometry_size)) {
        fprintf(stderr,
                "Box3D can't read the match's image of its world (%d bytes, and %d of geometry): it was made by another build of the game\n",
                (int)size, (int)geometry_size);
        abort();
    }
    if (measuring) image_ns += tide_time_now_ns() - began;
    if (size == 0 || !b3GetSnapshotBases(image, size, w->bases)) memset(w->bases, 0, sizeof w->bases);
    w->image_size = size;
    w->stamp = stamp;
    w->chain = (uint64_t)stamp;
    w->used = ++uses;
    active = w;
}

void b3t_gone(const int64_t *bodies, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b3BodyId id = body_of(active, bodies[i]);
        if (!b3Body_IsValid(id)) continue;
        b3DestroyBody(id);
        note('g', &bodies[i], sizeof bodies[i]);
    }
}

// For a host, and for tests: C's worlds go, as when a program starts. Matches
// lose nothing, as their state is their own.
void b3t_forget(void)
{
    for (int i = 0; i < WORLDS; i++) {
        unmake(&worlds[i]);
        free(worlds[i].before);
        worlds[i] = (world){0};
    }
    active = NULL;
}

// For tests: the worlds made from here on solve 4 contacts at a time, as a CPU
// without AVX2 does, where this one would solve 8. They step the same world.
void b3t_narrow(const bool on)
{
    narrow = on;
}

// How many steps were shared out over threads, and how many times a world was
// made from an image: what tests and benchmarks read.
int32_t b3t_shared_steps(void)
{
    return shared_steps;
}

int32_t b3t_restores(void)
{
    return restores;
}

// Where a tick's time goes, for a benchmark that asks: Box3D's own step, and
// its state into an image or back out of one. Nanoseconds since the last call;
// the first starts the measuring.
void b3t_measure(uint64_t *step, uint64_t *image)
{
    measuring = true;
    *step = step_ns;
    *image = image_ns;
    step_ns = 0;
    image_ns = 0;
}

// Of the images since the last call, how many of their 16 KiB blocks changed
// from one image to the next, and how many there were: what a match's
// snapshots can't share, and what's hashed again. Counting keeps a copy of
// each image and compares the next with it, inside the tick, which only a
// measured run does: `took` is the nanoseconds of that, to take off the
// tick's.
void b3t_measure_blocks(uint64_t *changed, uint64_t *in_all, uint64_t *took)
{
    *took = counting_ns;
    counting_ns = 0;
    *changed = blocks_changed;
    *in_all = blocks_in_all;
    blocks_changed = 0;
    blocks_in_all = 0;
}

// ---------------------------------------------------------------------------
// Bodies and shapes

int32_t b3t_body(body *b)
{
    const world *w = active;
    const body was = *b;
    uint8_t did = 0; // What was done, a bit each
    b3BodyId id;
    if (b->id == 0) {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = (b3BodyType)b->type;
        def.position = b3ToPos(vec(b->position));
        def.rotation = quat(b->rotation);
        def.linearVelocity = vec(b->velocity);
        def.angularVelocity = vec(b->angular_velocity);
        def.linearDamping = b->linear_damping;
        def.angularDamping = b->angular_damping;
        def.gravityScale = b->gravity_scale;
        def.isBullet = b->bullet;
        id = b3CreateBody(w->id, &def);
        b->id = handle(id.index1, id.generation);
        did |= 1u;
    } else {
        // What code wrote since the last step, and only that: a body nothing
        // touched stays as Box3D has it, asleep if it was
        id = body_of(w, b->id);
        if (!b3Body_IsValid(id)) return 0;
        if (!same(float3(b3ToVec3(b3Body_GetPosition(id))), b->position) || !same_turn(turn(b3Body_GetRotation(id)), b->rotation)) {
            b3Body_SetTransform(id, b3ToPos(vec(b->position)), quat(b->rotation));
            did |= 2u;
        }
        if (!same(float3(b3Body_GetLinearVelocity(id)), b->velocity)) {
            b3Body_SetLinearVelocity(id, vec(b->velocity));
            did |= 4u;
        }
        if (!same(float3(b3Body_GetAngularVelocity(id)), b->angular_velocity)) {
            b3Body_SetAngularVelocity(id, vec(b->angular_velocity));
            did |= 8u;
        }
    }
    if (b->impulse.x != 0 || b->impulse.y != 0 || b->impulse.z != 0) {
        b3Body_ApplyLinearImpulseToCenter(id, vec(b->impulse), true);
        b->impulse = (tide_float3){0, 0, 0};
        did |= 16u;
    }
    if (b->angular_impulse.x != 0 || b->angular_impulse.y != 0 || b->angular_impulse.z != 0) {
        b3Body_ApplyAngularImpulse(id, vec(b->angular_impulse), true);
        b->angular_impulse = (tide_float3){0, 0, 0};
        did |= 32u;
    }
    if (did) {
        note('b', &was, sizeof was);
        note('d', &did, sizeof did);
    }
    return did & 1u;
}

static b3ShapeDef shape_def(const float density, const float friction, const float bounce, const bool sensor)
{
    b3ShapeDef def = b3DefaultShapeDef();
    def.density = density;
    def.baseMaterial.friction = friction;
    def.baseMaterial.restitution = bounce;
    def.isSensor = sensor;
    def.enableContactEvents = true; // Touched and Parted
    return def;
}

int32_t b3t_box(const int64_t on, const box *s)
{
    if (on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner)) return 0;
    note('x', &on, sizeof on);
    note('x', s, sizeof *s);
    const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b3BoxHull hull = b3MakeOffsetBoxHull(0.5f * s->size.x, 0.5f * s->size.y, 0.5f * s->size.z, vec(s->offset));
    b3CreateHullShape(owner, &def, &hull.base);
    return 1;
}

int32_t b3t_sphere(const int64_t on, const sphere *s)
{
    if (on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner)) return 0;
    note('o', &on, sizeof on);
    note('o', s, sizeof *s);
    const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b3Sphere round = {vec(s->offset), s->radius};
    b3CreateSphereShape(owner, &def, &round);
    return 1;
}

// A capsule stands along its body's y, `height` from end to end, as Unity's
// does: its two centers are a radius in from each. One no higher than it's
// wide is a sphere.
int32_t b3t_capsule(const int64_t on, const capsule *s)
{
    if (on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner)) return 0;
    note('p', &on, sizeof on);
    note('p', s, sizeof *s);
    const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const float half = 0.5f * s->height - s->radius;
    const float reach = half > 0 ? half : 0;
    const b3Capsule pill = {{s->offset.x, s->offset.y - reach, s->offset.z}, {s->offset.x, s->offset.y + reach, s->offset.z}, s->radius};
    b3CreateCapsuleShape(owner, &def, &pill);
    return 1;
}

// ---------------------------------------------------------------------------
// The step

int64_t b3t_step(const float dt, const int32_t substeps, const tide_float3 gravity)
{
    world *w = active;
    // Nothing awake, and nothing done to the world since its last step: a step
    // would leave it as it is. The stamp stays, which says so.
    if (w->stamp != 0 && w->chain == (uint64_t)w->stamp && b3World_GetAwakeBodyCount(w->id) == 0) return w->stamp;
    note('s', &dt, sizeof dt);
    note('s', &substeps, sizeof substeps);
    note('s', &gravity, sizeof gravity);
    b3World_SetGravity(w->id, vec(gravity));
    shared = w->workers > 1 && b3World_GetAwakeBodyCount(w->id) >= SHARED_BODIES;
    shared_steps += shared;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    b3World_Step(w->id, dt, substeps);
    shared = false;
    if (measuring) step_ns += tide_time_now_ns() - began;

    const uint64_t stamp = tide_hash_end(w->chain);
    w->stamp = stamp ? (int64_t)stamp : 1; // 0 is a match nothing stepped
    w->chain = (uint64_t)w->stamp;
    return w->stamp;
}

// Where each section of the image goes: where it went before, so that what
// didn't change is the same bytes at the same place, while that's past the
// end of the section before it and not by more than a quarter of that one.
// Else it goes an eighth of that one past its end: room for it to grow and to
// shrink without anything moving, and never much of the image spent on room.
// The image's size, and whether any section moved.
static int place(world *w, const int *sizes, bool *moved)
{
    int end = b3GetSnapshotHead();
    *moved = false;
    for (int i = 0; i < B3_SNAPSHOT_SECTIONS; i++) {
        const int before = i ? sizes[i - 1] : 0;
        if (w->bases[i] < end || w->bases[i] > end + before / 4 + 256) {
            w->bases[i] = i ? end + before / 8 + 64 : end;
            *moved = true;
        }
        end = w->bases[i] + sizes[i];
    }
    return end;
}

// The world's state into the match's list, which is then exactly it, and how
// many bytes that is. Box3D writes it once, into room for a little more than
// the last one took; a second time when it's more than that, or when a section
// has to move.
int32_t b3t_image(tide_list_out *out)
{
    world *placed = active;
    int sizes[B3_SNAPSHOT_SECTIONS] = {0};
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    int room = placed->image_size ? placed->image_size + placed->image_size / 16 + 4096 : 0;
    uint8_t *image = room ? tide_list_out_room(out, (uint32_t)room) : NULL;
    int size = b3World_GetSnapshotAt(placed->id, image, room, placed->bases, sizes);
    bool moved;
    const int after = place(placed, sizes, &moved);
    if (moved || size > room) { // A section outgrew its place or left too much of it, or the image its room
        if (moved) size = after;
        if (size > room) {
            room = size;
            image = tide_list_out_room(out, (uint32_t)room);
        }
        size = b3World_GetSnapshotAt(placed->id, image, room, placed->bases, sizes);
    }
    if (size <= 0) { // A shape the bridge never makes: Box3D has no image of a world with a compound
        fprintf(stderr, "Box3D has no image of this world\n");
        abort();
    }
    tide_list_out_count(out, (uint32_t)size);
    placed->image_size = size;
    if (measuring) {
        const uint64_t counting = tide_time_now_ns();
        image_ns += counting - began;
        world *w = active;
        enum { BLOCK = 16384 };
        for (int at = 0; at < size; at += BLOCK) {
            const int n = size - at < BLOCK ? size - at : BLOCK;
            blocks_changed += at + n > w->before_size || memcmp(w->before + at, image + at, (size_t)n) != 0;
            blocks_in_all++;
        }
        w->before = tide_realloc(w->before, (size_t)w->before_size, (size_t)size);
        memcpy(w->before, image, (size_t)size);
        w->before_size = size;
        counting_ns += tide_time_now_ns() - counting;
    }
    return size;
}

// The geometry the world's images name: the boxes' hulls, each once, in the
// order images first had them. It's only ever added to, so the match's list is
// written again only when this is another size than it.
int32_t b3t_geometry_size(void)
{
    return b3World_GetSnapshotGeometry(active->id, NULL, 0);
}

void b3t_geometry(tide_list_out *out)
{
    const int size = b3World_GetSnapshotGeometry(active->id, NULL, 0);
    if (size > 0) b3World_GetSnapshotGeometry(active->id, tide_list_out_room(out, (uint32_t)size), size);
    tide_list_out_count(out, (uint32_t)size);
}

// Where the step left the body, into its component. False when that's where
// the component has it already.
bool b3t_pull(body *b, const int64_t stamp)
{
    (void)stamp;
    if (b->id == 0) return false;
    const b3BodyId id = body_of(active, b->id);
    if (!b3Body_IsValid(id)) return false;
    // Asleep, and the component says so already: where it is, and that it's still
    const tide_float3 none = {0, 0, 0};
    const bool still = same(b->velocity, none) && same(b->angular_velocity, none) && same(b->step_velocity, none)
                       && same(b->step_angular_velocity, none);
    const bool there = same(b->position, b->step_position) && same_turn(b->rotation, b->step_rotation);
    if (still && there && !b3Body_IsAwake(id)) return false;
    const body was = *b;
    b->position = float3(b3ToVec3(b3Body_GetPosition(id)));
    b->rotation = turn(b3Body_GetRotation(id));
    b->velocity = float3(b3Body_GetLinearVelocity(id));
    b->angular_velocity = float3(b3Body_GetAngularVelocity(id));
    b->step_position = b->position;
    b->step_rotation = b->rotation;
    b->step_velocity = b->velocity;
    b->step_angular_velocity = b->angular_velocity;
    return memcmp(&was, b, sizeof was) != 0;
}

int32_t b3t_touches(const int64_t stamp)
{
    (void)stamp;
    const b3ContactEvents events = b3World_GetContactEvents(active->id);
    return events.beginCount + events.endCount;
}

static int64_t owner_of(const b3ShapeId shape)
{
    if (!b3Shape_IsValid(shape)) return 0;
    const b3BodyId id = b3Shape_GetBody(shape);
    return handle(id.index1, id.generation);
}

touch b3t_touch(const int32_t index)
{
    const b3ContactEvents events = b3World_GetContactEvents(active->id);
    touch out = {0};
    if (index < events.beginCount) {
        out.a = owner_of(events.beginEvents[index].shapeIdA);
        out.b = owner_of(events.beginEvents[index].shapeIdB);
        out.began = true;
    } else if (index - events.beginCount < events.endCount) {
        out.a = owner_of(events.endEvents[index - events.beginCount].shapeIdA);
        out.b = owner_of(events.endEvents[index - events.beginCount].shapeIdB);
    }
    return out;
}
