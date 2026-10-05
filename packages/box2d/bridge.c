// The C side of box2d.tide: Box2D's worlds, and what the package's systems ask
// of them.
//
// A match's Box2D state is in the match, as an image (Physics.image) and a
// stamp that names it. C's worlds here are only where that state is stepped.
// Each is at a stamp, a tick finds the one at its match's stamp or has one
// made from the image, and the step leaves it at the next. So a rollback, a
// join, a reload or another match in the same program each just find no world
// at their stamp, and get one. Two matches at the same stamp are at the same
// state, and either's world serves both.
//
// A stamp is the stamp before it and everything that was done to the world
// since, hashed: Box2D's next state is its last and what's done to it, and
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

#include "box2d/src/box2d/box2d.h"

// box2d.tide's types, field for field
typedef struct body {
    int64_t id;
    int32_t type;
    tide_float2 position;
    float angle;
    tide_float2 velocity;
    float spin;
    tide_float2 impulse;
    float turn;
    float gravity_scale;
    float linear_damping;
    float angular_damping;
    tide_float2 step_position;
    float step_angle;
    tide_float2 step_velocity;
    float step_spin;
    bool fixed_rotation;
    bool bullet;
} body;

typedef struct box {
    tide_float2 size;
    tide_float2 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} box;

typedef struct circle {
    float radius;
    tide_float2 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} circle;

typedef struct touch {
    int64_t a;
    int64_t b;
    bool began;
} touch;

_Static_assert(sizeof(body) == 88 && sizeof(box) == 32 && sizeof(circle) == 28 && sizeof(touch) == 24,
               "box2d.tide's types changed: these are theirs");

// A step with fewer bodies awake than this runs on its own thread: sharing it
// out would cost more than it saves.
#define SHARED_BODIES 128

#define WORLDS 4

typedef struct world {
    b2WorldId id;
    bool made;
    int workers;
    int64_t stamp;  // The state it's at: 0 for an empty one nothing stepped
    uint64_t chain; // The stamp, and what the running tick did to the world since
    uint64_t used;  // When a tick last had it
    int bases[B2_SNAPSHOT_SECTIONS]; // Where each section of its image goes: part of its state, as the image says them
    int image_size;                  // Its last image's, which is about the next one's: how much room to ask for
    uint8_t *before; // Measuring: the last image, to count what changed of it
    int before_size;
} world;

static world worlds[WORLDS];
static uint64_t uses;
static world *active; // The running tick's
static bool shared;   // The running step's tasks go to other threads
static int32_t shared_steps;
static int32_t restores;
static bool measuring; // No clock is read otherwise
static uint64_t step_ns;
static uint64_t image_ns;
static uint64_t blocks_changed; // Of the images taken while measuring: their 16 KiB blocks that weren't as the image before had them
static uint64_t blocks_in_all;

// ---------------------------------------------------------------------------
// Box2D's tasks are the tick's (tide/jobs.h)

static void *enqueue(b2TaskCallback *task, void *context, void *user)
{
    (void)user;
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
// Box2D's workers wait for each stage of a step in a loop, which on the web
// holds a core for as long as it goes on. One that has waited long sleeps
// until the next stage is given (solver.c, where they're called from), on the
// threads that can (tide/jobs.h).
bool b2t_stage_sleep(const uint32_t *bits, const uint32_t seen)
{
    return tide_job_sleep(bits, seen);
}

void b2t_stage_wake(const uint32_t *bits)
{
    tide_job_wake(bits);
}

// On the web, memory another thread grew can't be used until this one hears of
// it, which the engine's allocator sees to (tide/page.h).
static void *allocate(const size_t size, const int alignment)
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
    return threads > B2_MAX_WORKERS ? B2_MAX_WORKERS : (int)threads;
}

static void make(world *w, const int workers)
{
#ifdef __wasm__
    static bool set;
    if (!set) b2SetAllocator(allocate, release);
    set = true;
#endif
    b2WorldDef def = b2DefaultWorldDef();
    def.workerCount = workers;
    def.enqueueTask = enqueue;
    def.finishTask = finish;
    w->id = b2CreateWorld(&def);
    w->made = true;
    w->workers = workers;
    w->stamp = 0;
    w->chain = 0;
    memset(w->bases, 0, sizeof w->bases);
    w->image_size = 0;
}

static void unmake(world *w)
{
    if (w->made) b2DestroyWorld(w->id);
    w->made = false;
}

static b2BodyId body_of(const world *w, const int64_t id)
{
    return (b2BodyId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
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

// ---------------------------------------------------------------------------
// The tick's world

bool b2t_open(const int64_t stamp)
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

void b2t_restore(const uint8_t *image, const int32_t size, const int64_t stamp)
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
    if (size > 0 && !b2World_Restore(w->id, image, size)) {
        fprintf(stderr, "Box2D can't read the match's image of its world (%d bytes): it was made by another build of the game\n",
                (int)size);
        abort();
    }
    if (measuring) image_ns += tide_time_now_ns() - began;
    if (size == 0 || !b2GetSnapshotBases(image, size, w->bases)) memset(w->bases, 0, sizeof w->bases);
    w->image_size = size;
    w->stamp = stamp;
    w->chain = (uint64_t)stamp;
    w->used = ++uses;
    active = w;
}

void b2t_gone(const int64_t *bodies, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b2BodyId id = body_of(active, bodies[i]);
        if (!b2Body_IsValid(id)) continue;
        b2DestroyBody(id);
        note('g', &bodies[i], sizeof bodies[i]);
    }
}

// For a host, and for tests: C's worlds go, as when a program starts. Matches
// lose nothing, as their state is their own.
void b2t_forget(void)
{
    for (int i = 0; i < WORLDS; i++) {
        unmake(&worlds[i]);
        free(worlds[i].before);
        worlds[i] = (world){0};
    }
    active = NULL;
}

// How many steps were shared out over threads, and how many times a world was
// made from an image: what tests and benchmarks read.
int32_t b2t_shared_steps(void)
{
    return shared_steps;
}

int32_t b2t_restores(void)
{
    return restores;
}

// Where a tick's time goes, for a benchmark that asks: Box2D's own step, and
// its state into an image or back out of one. Nanoseconds since the last call;
// the first starts the measuring.
void b2t_measure(uint64_t *step, uint64_t *image)
{
    measuring = true;
    *step = step_ns;
    *image = image_ns;
    step_ns = 0;
    image_ns = 0;
}

// Of the images since the last call, how many of their 16 KiB blocks changed
// from one image to the next, and how many there were: what a match's
// snapshots can't share, and what's hashed again.
void b2t_measure_blocks(uint64_t *changed, uint64_t *in_all)
{
    *changed = blocks_changed;
    *in_all = blocks_in_all;
    blocks_changed = 0;
    blocks_in_all = 0;
}

// ---------------------------------------------------------------------------
// Bodies and shapes

int32_t b2t_body(body *b)
{
    const world *w = active;
    const body was = *b;
    uint8_t did = 0; // What was done, a bit each
    b2BodyId id;
    if (b->id == 0) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = (b2BodyType)b->type;
        def.position = b2ToPos((b2Vec2){b->position.x, b->position.y});
        def.rotation = b2MakeRot(b->angle);
        def.linearVelocity = (b2Vec2){b->velocity.x, b->velocity.y};
        def.angularVelocity = b->spin;
        def.linearDamping = b->linear_damping;
        def.angularDamping = b->angular_damping;
        def.gravityScale = b->gravity_scale;
        def.motionLocks.angularZ = b->fixed_rotation;
        def.isBullet = b->bullet;
        id = b2CreateBody(w->id, &def);
        b->id = handle(id.index1, id.generation);
        did |= 1u;
    } else {
        // What code wrote since the last step, and only that: a body nothing
        // touched stays as Box2D has it, asleep if it was
        id = body_of(w, b->id);
        if (!b2Body_IsValid(id)) return 0;
        const b2Pos at = b2Body_GetPosition(id);
        if ((float)at.x != b->position.x || (float)at.y != b->position.y || b2Rot_GetAngle(b2Body_GetRotation(id)) != b->angle) {
            b2Body_SetTransform(id, b2ToPos((b2Vec2){b->position.x, b->position.y}), b2MakeRot(b->angle));
            did |= 2u;
        }
        const b2Vec2 velocity = b2Body_GetLinearVelocity(id);
        if (velocity.x != b->velocity.x || velocity.y != b->velocity.y) {
            b2Body_SetLinearVelocity(id, (b2Vec2){b->velocity.x, b->velocity.y});
            did |= 4u;
        }
        if (b2Body_GetAngularVelocity(id) != b->spin) {
            b2Body_SetAngularVelocity(id, b->spin);
            did |= 8u;
        }
    }
    if (b->impulse.x != 0 || b->impulse.y != 0) {
        b2Body_ApplyLinearImpulseToCenter(id, (b2Vec2){b->impulse.x, b->impulse.y}, true);
        b->impulse = (tide_float2){0, 0};
        did |= 16u;
    }
    if (b->turn != 0) {
        b2Body_ApplyAngularImpulse(id, b->turn, true);
        b->turn = 0;
        did |= 32u;
    }
    if (did) {
        note('b', &was, sizeof was);
        note('d', &did, sizeof did);
    }
    return did & 1u;
}

static b2ShapeDef shape_def(const float density, const float friction, const float bounce, const bool sensor)
{
    b2ShapeDef def = b2DefaultShapeDef();
    def.density = density;
    def.material.friction = friction;
    def.material.restitution = bounce;
    def.isSensor = sensor;
    def.enableContactEvents = true; // Touched and Parted
    return def;
}

int32_t b2t_box(const int64_t on, const box *s)
{
    if (on == 0) return 0;
    const b2BodyId owner = body_of(active, on);
    if (!b2Body_IsValid(owner)) return 0;
    note('x', &on, sizeof on);
    note('x', s, sizeof *s);
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Polygon polygon = b2MakeOffsetBox(0.5f * s->size.x, 0.5f * s->size.y, (b2Vec2){s->offset.x, s->offset.y}, b2Rot_identity);
    b2CreatePolygonShape(owner, &def, &polygon);
    return 1;
}

int32_t b2t_circle(const int64_t on, const circle *s)
{
    if (on == 0) return 0;
    const b2BodyId owner = body_of(active, on);
    if (!b2Body_IsValid(owner)) return 0;
    note('c', &on, sizeof on);
    note('c', s, sizeof *s);
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Circle round = {{s->offset.x, s->offset.y}, s->radius};
    b2CreateCircleShape(owner, &def, &round);
    return 1;
}

// ---------------------------------------------------------------------------
// The step

int64_t b2t_step(const float dt, const int32_t substeps, const tide_float2 gravity)
{
    world *w = active;
    // Nothing awake, and nothing done to the world since its last step: a step
    // would leave it as it is. The stamp stays, which says so.
    if (w->stamp != 0 && w->chain == (uint64_t)w->stamp && b2World_GetAwakeBodyCount(w->id) == 0) return w->stamp;
    note('s', &dt, sizeof dt);
    note('s', &substeps, sizeof substeps);
    note('s', &gravity, sizeof gravity);
    b2World_SetGravity(w->id, (b2Vec2){gravity.x, gravity.y});
    shared = w->workers > 1 && b2World_GetAwakeBodyCount(w->id) >= SHARED_BODIES;
    shared_steps += shared;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    b2World_Step(w->id, dt, substeps);
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
    int end = b2GetSnapshotHead();
    *moved = false;
    for (int i = 0; i < B2_SNAPSHOT_SECTIONS; i++) {
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
// many bytes that is. Box2D writes it once, into room for a little more than
// the last one took; a second time when it's more than that, or when a section
// has to move.
int32_t b2t_image(tide_list_out *out)
{
    world *placed = active;
    int sizes[B2_SNAPSHOT_SECTIONS] = {0};
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    int room = placed->image_size ? placed->image_size + placed->image_size / 16 + 4096 : 0;
    uint8_t *image = room ? tide_list_out_room(out, (uint32_t)room) : NULL;
    int size = b2World_GetSnapshotAt(placed->id, image, room, placed->bases, sizes);
    bool moved;
    const int after = place(placed, sizes, &moved);
    if (moved || size > room) { // A section outgrew its place or left too much of it, or the image its room
        if (moved) size = after;
        if (size > room) {
            room = size;
            image = tide_list_out_room(out, (uint32_t)room);
        }
        size = b2World_GetSnapshotAt(placed->id, image, room, placed->bases, sizes);
    }
    tide_list_out_count(out, (uint32_t)size);
    placed->image_size = size;
    if (measuring) image_ns += tide_time_now_ns() - began;
    if (measuring) {
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
    }
    return size;
}

// Where the step left the body, into its component. False when that's where
// the component has it already.
bool b2t_pull(body *b, const int64_t stamp)
{
    (void)stamp;
    if (b->id == 0) return false;
    const b2BodyId id = body_of(active, b->id);
    if (!b2Body_IsValid(id)) return false;
    // Asleep, and the component says so already: where it is, and that it's still
    const bool still = b->velocity.x == 0 && b->velocity.y == 0 && b->spin == 0 && b->step_velocity.x == 0 && b->step_velocity.y == 0
                       && b->step_spin == 0;
    const bool there = b->position.x == b->step_position.x && b->position.y == b->step_position.y && b->angle == b->step_angle;
    if (still && there && !b2Body_IsAwake(id)) return false;
    const body was = *b;
    const b2Pos at = b2Body_GetPosition(id);
    const b2Vec2 velocity = b2Body_GetLinearVelocity(id);
    b->position = (tide_float2){(float)at.x, (float)at.y};
    b->angle = b2Rot_GetAngle(b2Body_GetRotation(id));
    b->velocity = (tide_float2){velocity.x, velocity.y};
    b->spin = b2Body_GetAngularVelocity(id);
    b->step_position = b->position;
    b->step_angle = b->angle;
    b->step_velocity = b->velocity;
    b->step_spin = b->spin;
    return memcmp(&was, b, sizeof was) != 0;
}

int32_t b2t_touches(const int64_t stamp)
{
    (void)stamp;
    const b2ContactEvents events = b2World_GetContactEvents(active->id);
    return events.beginCount + events.endCount;
}

static int64_t owner_of(const b2ShapeId shape)
{
    if (!b2Shape_IsValid(shape)) return 0;
    const b2BodyId id = b2Shape_GetBody(shape);
    return handle(id.index1, id.generation);
}

touch b2t_touch(const int32_t index)
{
    const b2ContactEvents events = b2World_GetContactEvents(active->id);
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
