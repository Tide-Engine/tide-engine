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
//
// After a step, the image goes into the match as what changed of the last
// one, where Box3D can say (b3t_image): the match's list keeps the rest, and
// no copy of the image is kept here. What a world says changed is of the last
// image it wrote, which was of the state its stamp names. The match that has
// the world for a tick is at that stamp, so its list holds that image, byte
// for byte, whichever match's list it was written into. A world made from an
// image writes its next one whole.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide/list.h"
#include "tide/map.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/page.h"
#include "tide/rows.h"
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

// Physics, with the padding it writes out. Its lists and its map are the
// match's to read and write: C leaves them as they are.
typedef struct physics {
    tide_float3 gravity;
    int32_t substeps;
    int32_t bodies;
    int32_t shapes;
    int64_t stamp;
    int32_t size;
    bool moved;
    uint8_t unused[3];
    tide_list image;
    tide_list geometry;
    tide_list gone;
    tide_map owners;
} physics;

_Static_assert(sizeof(body) == 160 && sizeof(box) == 40 && sizeof(sphere) == 32 && sizeof(capsule) == 36 && sizeof(touch) == 24
                   && sizeof(physics) == 64 && offsetof(physics, moved) == 36,
               "box3d.tide's types changed: these are theirs");

// A body's place and speeds are 13 floats side by side in its component: its
// position, its rotation, its velocity and its angular velocity. Where the
// last step left it is the same 13 again, in the same order, and the impulses
// it's to be given are 6 between them. The systems written in C go through
// them as that many arrays (tide/rows.h), each named by where it starts.
enum { PLACE = 13, STILL = 7, PUSH = 6 }; // ...of which the 6 from STILL on are the speeds
#define NOW offsetof(body, position)
#define STEP offsetof(body, step_position)
#define PUSHES offsetof(body, impulse)

_Static_assert(offsetof(body, impulse) - NOW == PLACE * sizeof(float) && offsetof(body, bullet) - STEP == PLACE * sizeof(float)
                   && offsetof(body, gravity_scale) - PUSHES == PUSH * sizeof(float)
                   && offsetof(body, velocity) - NOW == STILL * sizeof(float),
               "a Body's place and speeds, and where the last step left them, aren't the same floats in the same order any more");

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
    // Measuring and checking only: a copy of the last image, to count what changed of it and to see that
    // what Box3D said changed is all that did. A match keeps none: its image is in its list.
    uint8_t *before;
    int before_size;
    bool before_good; // It's the world's last image
    uint8_t *whole;   // Checking: the image written whole, to compare
    int whole_size;
} world;

static world worlds[WORLDS];
static uint64_t uses;
static world *active; // The running tick's
static bool shared;   // The running step's tasks go to other threads

// The bodies the running tick's Enter made, in the order it made them, each
// with its entity, for the system after it to take (b3t_made): the match keeps
// whose each body is in a map, which is Tide's to write. And each with the
// impulses its component was spawned with, which the step gives it (push_new):
// a body has no weight to push until its shapes are on it, later in the tick.
typedef struct new_body {
    int64_t body;
    tide_entity entity;
    tide_float3 impulse;
    tide_float3 angular_impulse;
} new_body;

static new_body *made;
static int32_t made_count;
static int32_t made_room;
static int32_t made_taken;

// The bodies the running tick's Enter told Box3D something of: the step notes
// the ones it then left alone (note_left).
static int64_t *synced;
static int32_t synced_count;
static int32_t synced_room;

// What the running tick's step says of a body, found by the body's index: the
// bodies b3t_leave has something to write for, and no others.
//
// A body the step moved, and left awake, has where it left it: Leave writes
// that into the body's component and asks Box3D nothing. One that fell asleep
// in the step, or that Enter told Box3D something of and the step then left
// alone (a static body that code moved), is only marked: Leave asks Box3D
// where it is, if its component doesn't say so already.
//
// Any other body wasn't awake for the step, and nothing was done to it this
// tick: it's as the last step that noted it left it, which its component says
// since that tick's Leave. Every body that's awake for a step is in the step's
// move events, the ones that fall asleep in it too, and nothing code writes
// reaches Box3D but through Enter.
typedef struct left {
    int64_t id;         // The body, with its generation
    float place[PLACE]; // Its place and speeds, as its component keeps them, if it's awake
    bool awake;         // Still awake after the step
} left;

static left *lefts;
// Which step noted each body in `lefts`: `stepped`'s, or it's an older one's
// and the last step says nothing of the body. Apart from `lefts`, as it's all
// Leave reads of such a body, which is most of them.
static uint32_t *left_steps;
static uint32_t left_room;
static uint32_t stepped; // Steps taken, by any world: the last one's number
static int share_from = SHARED_BODIES; // A step with this many bodies awake is shared out
static bool narrow;   // Worlds solve 4 contacts at a time, though the CPU can 8
static int32_t shared_steps;
static int32_t restores;
static bool measuring; // No clock is read otherwise
static bool checking;  // Every image written in parts is compared with the image written whole
static bool always;    // ...and images are written in parts whatever is awake
static int32_t images_whole;
static int32_t images_in_parts;
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
// Box3D's workers wait for each stage of a step in a loop, which on the web
// holds a core for as long as it goes on. One that has waited long sleeps
// until the next stage is given (solver.c, where they're called from), on the
// threads that can (tide/jobs.h).
bool b3t_stage_sleep(const uint32_t *bits, const uint32_t seen)
{
    return tide_job_sleep(bits, seen);
}

void b3t_stage_wake(const uint32_t *bits)
{
    tide_job_wake(bits);
}

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
    if (always) b3World_SetSnapshotAwakeShare(w->id, 1);
    w->made = true;
    w->workers = workers;
    w->stamp = 0;
    w->chain = 0;
    memset(w->bases, 0, sizeof w->bases);
    w->image_size = 0;
    w->before_good = false;
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

static bool none(const tide_float3 v)
{
    return v.x == 0 && v.y == 0 && v.z == 0;
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
    // A tick starts here. The last one's new bodies each have their entity: Own goes through the bodies as Enter did
    if (made_taken != made_count) {
        fprintf(stderr, "Box3D: %d bodies were made last tick that no entity was given\n", (int)(made_count - made_taken));
        abort();
    }
    made_count = made_taken = 0;
    synced_count = 0;
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
    w->before_good = false; // Its next image is written whole
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
        free(worlds[i].whole);
        worlds[i] = (world){0};
    }
    active = NULL;
    made_count = made_taken = 0;
    synced_count = 0;
}

// For tests: the worlds made from here on solve 4 contacts at a time, as a CPU
// without AVX2 does, where this one would solve 8. They step the same world.
void b3t_narrow(const bool on)
{
    narrow = on;
}

// For tests: every image that's written as what changed of the last one is
// also taken whole, and the two compared. One byte apart ends the program,
// saying where: the image is in the match's hash, and a player who joins makes
// their world from it. With `whenever`, the worlds made from here on write
// their images in parts whenever they can, where they'd write them whole
// while most of their bodies are awake: more for the check to see.
void b3t_check(const bool on, const bool whenever)
{
    checking = on;
    always = on && whenever;
}

// For tests: a step is shared out over the tick's threads from this many
// bodies awake, where it takes hundreds for that to pay. None is the usual.
void b3t_share_from(const int32_t bodies)
{
    share_from = bodies > 0 ? bodies : SHARED_BODIES;
}

// How many images were written whole, and how many as what changed of the
// last one, since the program started: tests read it to see they test both.
void b3t_images(int32_t *whole, int32_t *in_parts)
{
    *whole = images_whole;
    *in_parts = images_in_parts;
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

// A body that's asleep is awake from here on. It isn't a call an image in
// parts follows (box3d/TIDE.md), so the world's next image is written whole:
// it's only made for a body that's asleep, which code seldom does anything to.
static void wake(const b3BodyId id)
{
    if (b3Body_GetType(id) != b3_staticBody && !b3Body_IsAwake(id)) b3Body_SetAwake(id, true);
}

// Box3D moves a sleeping body and leaves it asleep, hanging where it was put,
// and leaves asleep what rested on a body that's moved from under it. Code has
// no other way to say a body should wake than to move it, so a body that's
// moved is awake, and so is what it touched. It follows from the move, which
// is what's noted.
static void wake_moved(const b3BodyId id)
{
    static b3ContactData *touching;
    static int room;
    wake(id);
    const int most = b3Body_GetContactCapacity(id);
    if (most == 0) return;
    if (most > room) {
        touching = tide_realloc(touching, (size_t)room * sizeof *touching, (size_t)most * sizeof *touching);
        room = most;
    }
    const int count = b3Body_GetContactData(id, touching, most);
    for (int i = 0; i < count; i++) {
        const b3BodyId one = b3Shape_GetBody(touching[i].shapeIdA);
        const bool own = one.index1 == id.index1 && one.generation == id.generation;
        wake(own ? b3Shape_GetBody(touching[i].shapeIdB) : one);
    }
}

// A body's component into Box3D: made, if it's new, or else what code wrote
// into it since the last step. Whether it was made.
static bool sync_body(body *b)
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
            wake_moved(id);
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
    // A body that was just made has no shape yet, so no weight, and an impulse would do nothing to it: the
    // ones it was spawned with wait for the step, which gives them once its shapes are on it (push_new).
    // Either way they're given once, and noted here
    const bool is_new = did & 1u;
    if (!none(b->impulse)) {
        if (!is_new) b3Body_ApplyLinearImpulseToCenter(id, vec(b->impulse), true);
        b->impulse = (tide_float3){0, 0, 0};
        did |= 16u;
    }
    if (!none(b->angular_impulse)) {
        if (!is_new) b3Body_ApplyAngularImpulse(id, vec(b->angular_impulse), true);
        b->angular_impulse = (tide_float3){0, 0, 0};
        did |= 32u;
    }
    if (did) {
        note('b', &was, sizeof was);
        note('d', &did, sizeof did);
    }
    return did & 1u;
}

// A run of bodies' fields, as the match keeps them: an array a field, a value
// a body (tide/rows.h).
typedef struct body_fields {
    const int32_t *id; // A long is two fields: its low half, then its high
    const int32_t *id_high;
    const float *now[PLACE];  // Where its component says it is, and how fast
    const float *step[PLACE]; // ...and where the last step left it
    const float *push[PUSH];  // The impulses it's to be given
} body_fields;

static body_fields fields_of(const tide_rows *rows)
{
    body_fields f;
    f.id = tide_rows_ints(rows, 0, offsetof(body, id));
    f.id_high = tide_rows_ints(rows, 0, offsetof(body, id) + 4);
    for (size_t k = 0; k < PLACE; k++) {
        f.now[k] = tide_rows_floats(rows, 0, NOW + k * sizeof(float));
        f.step[k] = tide_rows_floats(rows, 0, STEP + k * sizeof(float));
    }
    for (size_t k = 0; k < PUSH; k++) f.push[k] = tide_rows_floats(rows, 0, PUSHES + k * sizeof(float));
    return f;
}

// The bodies of a run are looked at this many at a time, a field after
// another, so that each comparison runs down one array and the C compiler does
// it for several bodies at once: a chunk's worth, which is what a run has.
enum { AT_ONCE = 2048 };

// Whether each of `count` bodies from `first` on is one code wrote to since
// the last step, or a new one: what Enter has something to do for.
static void written(const body_fields *f, const uint32_t first, const uint32_t count, uint8_t *restrict wrote)
{
    const int32_t *restrict id = f->id + first;
    const int32_t *restrict id_high = f->id_high + first;
    for (uint32_t i = 0; i < count; i++) wrote[i] = (id[i] | id_high[i]) == 0; // New
    for (int k = 0; k < PLACE; k++) { // Moved, turned, pushed or spun
        const float *restrict now = f->now[k] + first;
        const float *restrict step = f->step[k] + first;
        for (uint32_t i = 0; i < count; i++) wrote[i] |= now[i] != step[i];
    }
    for (int k = 0; k < PUSH; k++) { // Nudged
        const float *restrict push = f->push[k] + first;
        for (uint32_t i = 0; i < count; i++) wrote[i] |= push[i] != 0;
    }
}

// The system Enter: new bodies are made, and what code wrote into a body since
// the last step goes to Box3D. A body nothing wrote to costs these
// comparisons, and nothing else. It runs on one thread, its rows in order
// (it takes Physics to write), as what's done to the world is noted in order.
//
// The arrays are asked for once, before any body is written, though writing
// one may give the match other memory for a field than what was read: a body
// is looked at before it's written and never after, and the ones not yet
// written are the same in both.
void b3t_enter(tide_rows *rows, physics *p)
{
    (void)p;
    const uint32_t count = tide_rows_count(rows);
    const tide_entity *entities = tide_rows_entities(rows);
    const body_fields f = fields_of(rows);
    uint8_t wrote[AT_ONCE];
    for (uint32_t i = 0; i < count; i++) {
        if (i % AT_ONCE == 0) written(&f, i, count - i < AT_ONCE ? count - i : AT_ONCE, wrote);
        if (!wrote[i % AT_ONCE]) continue;
        body b;
        tide_rows_get(rows, 0, i, &b);
        const tide_float3 impulse = b.impulse;
        const tide_float3 angular_impulse = b.angular_impulse;
        if (sync_body(&b)) {
            if (made_count == made_room) {
                const int32_t room = made_room ? made_room * 2 : 64;
                made = tide_realloc(made, (size_t)made_room * sizeof *made, (size_t)room * sizeof *made);
                made_room = room;
            }
            made[made_count++] = (new_body){b.id, entities[i], impulse, angular_impulse};
        }
        if (synced_count == synced_room) {
            const int32_t room = synced_room ? synced_room * 2 : 64;
            synced = tide_realloc(synced, (size_t)synced_room * sizeof *synced, (size_t)room * sizeof *synced);
            synced_room = room;
        }
        synced[synced_count++] = b.id; // For the step to note, if it leaves the body alone
        tide_rows_set(rows, 0, i, &b); // Its id, and the impulses it was given, which are gone
    }
}

// The body the tick's Enter made for `entity`, once, or 0 for an entity whose
// body it didn't make. Asked for each body in the order Enter went through
// them, which is the order it made them in, so the next one made is the only
// one to look at.
int64_t b3t_made(const tide_entity entity)
{
    if (made_taken == made_count || !tide_entity_equal(made[made_taken].entity, entity)) return 0;
    return made[made_taken++].body;
}

static b3ShapeDef shape_def(const float density, const float friction, const float bounce, const bool sensor)
{
    b3ShapeDef def = b3DefaultShapeDef();
    def.density = density;
    def.baseMaterial.friction = friction;
    def.baseMaterial.restitution = bounce;
    def.isSensor = sensor;
    def.enableContactEvents = true; // Touched and Parted
    // ...which a sensor sends too, for the shapes in it. Box3D only reports a shape to a sensor when both
    // say so, and neither does unless it's told to: every shape does, as any may come into a sensor
    def.enableSensorEvents = true;
    // A sensor doesn't report another: each would report the other, and their entities hear of it twice.
    // Shapes of one group below zero are never set against each other
    if (sensor) def.filter.groupIndex = -1;
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

// A body's place and speeds as its component keeps them, from Box3D's own.
static void place_of(float *place, const b3Pos at, const b3Quat rotation, const b3Vec3 velocity, const b3Vec3 angular)
{
    const tide_float3 position = float3(b3ToVec3(at));
    const tide_quaternion turned = turn(rotation);
    const float as_kept[PLACE] = {position.x,       position.y, position.z, turned.value.x, turned.value.y, turned.value.z, turned.value.w,
                                  velocity.x,       velocity.y, velocity.z, angular.x,      angular.y,      angular.z};
    memcpy(place, as_kept, sizeof as_kept);
}

// The body of this index, noted by the step that just ran.
static left *left_at(const uint32_t index)
{
    if (index >= left_room) {
        uint32_t room = left_room ? left_room : 1024u;
        while (room <= index) room *= 2u;
        lefts = tide_realloc(lefts, (size_t)left_room * sizeof *lefts, (size_t)room * sizeof *lefts);
        left_steps = tide_realloc(left_steps, (size_t)left_room * sizeof *left_steps, (size_t)room * sizeof *left_steps);
        memset(&left_steps[left_room], 0, (size_t)(room - left_room) * sizeof *left_steps);
        left_room = room;
    }
    left_steps[index] = stepped;
    return &lefts[index];
}

// What the step that just ran says of the bodies b3t_leave has something to
// write for, each found by its index. Box3D says which were awake for it (its
// move events), with where each is now and, here, how fast, and which of them
// fell asleep in it. And Enter says which it told Box3D something of: the ones
// of those the step left alone are noted as the ones that fell asleep are, for
// Leave to ask about. Nothing here changes the world: these are all Box3D's
// own answers.
static void note_left(const world *w)
{
    if (++stepped == 0) { // Wrapped: no older step's is this one's
        if (left_steps) memset(left_steps, 0, (size_t)left_room * sizeof *left_steps);
        stepped = 1;
    }
    const b3BodyEvents events = b3World_GetBodyEvents(w->id);
    for (int i = 0; i < events.moveCount; i++) {
        const b3BodyMoveEvent *event = &events.moveEvents[i];
        left *l = left_at((uint32_t)event->bodyId.index1);
        l->id = handle(event->bodyId.index1, event->bodyId.generation);
        l->awake = !event->fellAsleep;
        if (!l->awake) continue;
        place_of(l->place, event->transform.p, event->transform.q, b3Body_GetLinearVelocity(event->bodyId),
                 b3Body_GetAngularVelocity(event->bodyId));
    }
    for (int32_t i = 0; i < synced_count; i++) {
        const int64_t id = synced[i];
        const uint32_t index = (uint32_t)id;
        if (index < left_room && left_steps[index] == stepped && lefts[index].id == id) continue; // The step moved it
        if (!b3Body_IsValid(body_of(w, id))) continue;
        left *l = left_at(index);
        l->id = id;
        l->awake = false;
    }
    synced_count = 0;
}

// The impulses the bodies made this tick were spawned with, given now that
// their shapes are on them and they weigh something: as an impulse written
// into a body that's there is, a tick sooner. In the order the bodies were
// made, and noted with them (sync_body), so nothing is noted here. They're
// calls an image in parts follows, as the ones Enter makes are.
static void push_new(const world *w)
{
    for (int32_t i = 0; i < made_count; i++) {
        const new_body *n = &made[i];
        if (none(n->impulse) && none(n->angular_impulse)) continue;
        const b3BodyId id = body_of(w, n->body);
        if (!b3Body_IsValid(id)) continue;
        if (!none(n->impulse)) b3Body_ApplyLinearImpulseToCenter(id, vec(n->impulse), true);
        if (!none(n->angular_impulse)) b3Body_ApplyAngularImpulse(id, vec(n->angular_impulse), true);
    }
}

int64_t b3t_step(const float dt, const int32_t substeps, const tide_float3 gravity)
{
    world *w = active;
    // Gravity that isn't what the world's last step had: the world's own is in its image, so it's the
    // last step's wherever the world came from
    const bool turned = !same(float3(b3World_GetGravity(w->id)), gravity);
    // Nothing awake, nothing done to the world since its last step, and what pulls on it as it was: a
    // step would leave it as it is. The stamp stays, which says so.
    if (!turned && w->stamp != 0 && w->chain == (uint64_t)w->stamp && b3World_GetAwakeBodyCount(w->id) == 0) return w->stamp;
    note('s', &dt, sizeof dt);
    note('s', &substeps, sizeof substeps);
    note('s', &gravity, sizeof gravity);
    push_new(w);
    b3World_SetGravity(w->id, vec(gravity));
    if (turned) {
        // Everything is awake to feel it: a body at rest would stay where the old gravity left it. Box3D
        // wakes every body when sleeping is turned off, and has no other call for it. It follows from the
        // gravity, which every step notes. Neither call is one an image in parts follows, so the next
        // image is written whole, as it would be with everything awake.
        b3World_EnableSleeping(w->id, false);
        b3World_EnableSleeping(w->id, true);
    }
    shared = w->workers > 1 && b3World_GetAwakeBodyCount(w->id) >= share_from;
    shared_steps += shared;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    b3World_Step(w->id, dt, substeps);
    shared = false;
    if (measuring) step_ns += tide_time_now_ns() - began;
    note_left(w);

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

enum { BLOCK = 16384 }; // What a measured run counts the image's changes by

// Which part of an image a byte is in, for what a failed check says: Box3D's
// sections, in its order (world_snapshot.c).
static const char *const sections[B3_SNAPSHOT_SECTIONS] = {
    "settings and ids", "static and disabled sets", "awake set",   "sleeping sets", "bodies",       "shapes",
    "fat bounds",       "contacts",                 "joints",      "sensors",       "islands",      "static tree",
    "kinematic tree",   "dynamic tree",             "pairs",       "graph",         "names",
};

// A copy of the image a match's list is about to hold, which measuring and
// checking go by.
static void remember(world *w, const uint8_t *image, const int size)
{
    w->before = tide_realloc(w->before, (size_t)w->before_size, (size_t)size);
    memcpy(w->before, image, (size_t)size);
    w->before_size = size;
    w->before_good = true;
}

// The parts Box3D says changed, over the copy of the last image: what the
// match's list becomes. While measuring, the image's blocks that a part made
// otherwise are counted.
static void remember_parts(world *w, const b3SnapshotPart *parts, const int count, const int size)
{
    w->before = tide_realloc(w->before, (size_t)w->before_size, (size_t)size);
    if (size > w->before_size) memset(w->before + w->before_size, 0, (size_t)(size - w->before_size)); // As a list grows
    const int blocks = (size + BLOCK - 1) / BLOCK;
    uint8_t *changed = measuring ? tide_alloc_zeroed((size_t)blocks, 1) : NULL;
    if (changed) {
        for (int at = w->before_size / BLOCK; at < blocks; at++) changed[at] = at * BLOCK + BLOCK > w->before_size;
    }
    for (int i = 0; i < count; i++) {
        const b3SnapshotPart *part = &parts[i];
        if (part->offset < 0 || part->offset + part->size > size) {
            fprintf(stderr, "Box3D gave a part of its image that's outside it: %d bytes at %d of %d\n", part->size, part->offset, size);
            abort();
        }
        for (int at = part->offset; changed && at < part->offset + part->size;) {
            const int end = (at / BLOCK + 1) * BLOCK < part->offset + part->size ? (at / BLOCK + 1) * BLOCK : part->offset + part->size;
            if (memcmp(w->before + at, part->bytes + (at - part->offset), (size_t)(end - at)) != 0) changed[at / BLOCK] = 1;
            at = end;
        }
        memcpy(w->before + part->offset, part->bytes, (size_t)part->size);
    }
    for (int at = 0; changed && at < blocks; at++) blocks_changed += changed[at];
    if (changed) blocks_in_all += (uint64_t)blocks;
    free(changed);
    w->before_size = size;
}

// The image written whole, against the one its parts made: the same bytes, or
// the program ends, saying where the first one that isn't is.
static void check(world *w, const int size)
{
    w->whole = tide_realloc(w->whole, (size_t)w->whole_size, (size_t)size);
    w->whole_size = size;
    int sizes[B3_SNAPSHOT_SECTIONS];
    const int whole = b3World_CheckSnapshotAt(w->id, w->whole, size, w->bases, sizes);
    if (whole == size && memcmp(w->whole, w->before, (size_t)size) == 0) return;
    if (whole != size) {
        fprintf(stderr, "Box3D's image in parts is %d bytes, and its image written whole %d\n", size, whole);
        abort();
    }
    int first = 0;
    int apart = 0;
    for (int at = size - 1; at >= 0; at--) {
        if (w->whole[at] == w->before[at]) continue;
        first = at;
        apart++;
    }
    int section = -1;
    for (int i = 0; i < B3_SNAPSHOT_SECTIONS; i++) {
        if (w->bases[i] <= first) section = i;
    }
    const int in = section < 0 ? first : first - w->bases[section];
    fprintf(stderr, "Box3D's image in parts isn't its image written whole: %d bytes apart, the first at %d, which is byte %d of %s", apart,
            first, in, section < 0 ? "its header" : sections[section]);
    // The sections of things that each take the same room start with how many there are
    int32_t count = 0;
    if (section >= 0) memcpy(&count, w->whole + w->bases[section], sizeof count);
    const int record = section == 4 ? 136 : section == 5 ? 208 : section == 7 ? 216 : 0; // A body, a shape, a contact
    if (record && in >= 4 && in < 4 + count * record) {
        fprintf(stderr, ": byte %d of %s %d", (in - 4) % record, section == 4 ? "body" : section == 5 ? "shape" : "contact", (in - 4) / record);
    } else if (section == 7 && in >= 4 + count * 216 && in < 4 + count * (216 + 268)) {
        fprintf(stderr, ": byte %d of the manifold of contact %d", (in - 4 - count * 216) % 268, (in - 4 - count * 216) / 268);
    }
    fprintf(stderr, " (%d of them). Written whole it's %02x, and in parts %02x\n", (int)count, w->whole[first], w->before[first]);
    abort();
}

// The world's state into the match's list, and how many bytes that is.
//
// Where Box3D can say what changed of its last image, which the list holds,
// the list keeps the rest: only what changed is written, and nothing is
// compared but that (b3World_GetSnapshotParts, tide_list_out_keep). The image
// is, byte for byte, the one written whole.
//
// Otherwise Box3D writes all of it into room for a little more than the last
// one took, and the list is then exactly it; a second time when it's more
// than that, or when a section has to move.
int32_t b3t_image(tide_list_out *out)
{
    world *w = active;
    int sizes[B3_SNAPSHOT_SECTIONS] = {0};
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    const bool remembers = measuring || checking;
    // The copy a measured or checked run goes by has to be the last image
    if (w->image_size > 0 && (!remembers || w->before_good)) {
        const b3SnapshotPart *parts;
        int count;
        const int size = b3World_GetSnapshotParts(w->id, w->bases, sizes, &parts, &count);
        bool moved = false;
        if (size != 0) place(w, sizes, &moved); // A section outgrew its place, or left too much of it: all of it again
        if (size > 0 && !moved) {
            tide_list_out_keep(out, (uint32_t)size);
            for (int i = 0; i < count; i++) tide_list_out_part(out, (uint32_t)parts[i].offset, parts[i].bytes, (uint32_t)parts[i].size);
            w->image_size = size;
            images_in_parts++;
            const uint64_t counting = measuring ? tide_time_now_ns() : 0;
            if (measuring) image_ns += counting - began;
            if (remembers) remember_parts(w, parts, count, size);
            if (checking) check(w, size);
            if (measuring) counting_ns += tide_time_now_ns() - counting;
            return size;
        }
    }

    int room = w->image_size ? w->image_size + w->image_size / 16 + 4096 : 0;
    uint8_t *image = room ? tide_list_out_room(out, (uint32_t)room) : NULL;
    int size = b3World_GetSnapshotAt(w->id, image, room, w->bases, sizes);
    bool moved;
    const int after = place(w, sizes, &moved);
    if (moved || size > room) { // A section outgrew its place or left too much of it, or the image its room
        if (moved) size = after;
        if (size > room) {
            room = size;
            image = tide_list_out_room(out, (uint32_t)room);
        }
        size = b3World_GetSnapshotAt(w->id, image, room, w->bases, sizes);
    }
    if (size <= 0) { // A shape the bridge never makes: Box3D has no image of a world with a compound
        fprintf(stderr, "Box3D has no image of this world\n");
        abort();
    }
    tide_list_out_count(out, (uint32_t)size);
    w->image_size = size;
    images_whole++;
    if (measuring) {
        const uint64_t counting = tide_time_now_ns();
        image_ns += counting - began;
        for (int at = 0; at < size; at += BLOCK) {
            const int n = size - at < BLOCK ? size - at : BLOCK;
            blocks_changed += at + n > w->before_size || memcmp(w->before + at, image + at, (size_t)n) != 0;
            blocks_in_all++;
        }
        remember(w, image, size);
        counting_ns += tide_time_now_ns() - counting;
    } else if (checking) {
        remember(w, image, size);
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

// The fields the step leaves a body's place and speeds in, to write: the ones
// code reads and writes, and the package's own, which say where the last step
// left it.
typedef struct body_places {
    float *now[PLACE];
    float *step[PLACE];
} body_places;

static body_places places_of(tide_rows *rows)
{
    body_places to;
    for (size_t k = 0; k < PLACE; k++) {
        to.now[k] = tide_rows_floats_mut(rows, 0, NOW + k * sizeof(float));
        to.step[k] = tide_rows_floats_mut(rows, 0, STEP + k * sizeof(float));
    }
    return to;
}

// The system Leave: where the step left each body, into its component, for the
// bodies the step says something of (note_left). Of any other, a field is read
// to find that out, and nothing else: a world with little moving costs it
// little.
//
// A body the step moved, and left awake, is written from where the step left
// it. One that fell asleep in it, or that code wrote to and the step then left
// alone, is asked for, unless its component says it's where the last step left
// it, and still: its component gets what Box3D has, so that the next tick's
// Enter finds nothing written.
//
// The fields are only asked for to write once a body has something to write,
// so a run of bodies at rest leaves every one of them shared with the match's
// snapshots. Runs go to several threads at once: each reads what the step
// says, and writes its own rows.
void b3t_leave(tide_rows *rows, const physics *p)
{
    if (!p->moved) return; // Nothing stepped: every body is where its component says
    const uint32_t count = tide_rows_count(rows);
    // Which body each is: all that's read of most. Nothing here writes these, so they stay where they are
    const int32_t *ids = tide_rows_ints(rows, 0, offsetof(body, id));
    const int32_t *ids_high = tide_rows_ints(rows, 0, offsetof(body, id) + 4);
    const uint32_t *steps = left_steps;
    const uint32_t room = left_room;
    const uint32_t step = stepped;
    body_places to = {0};
    bool writing = false;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t index = (uint32_t)ids[i];
        if (index >= room || steps[index] != step) continue; // The step says nothing of it
        const left *l = &lefts[index];
        const int64_t id = (int64_t)((uint64_t)index | (uint64_t)(uint32_t)ids_high[i] << 32);
        if (l->id != id) continue; // Another body's, which has the index now: this one is gone
        float asked[PLACE];
        const float *where = l->place;
        if (!l->awake) {
            // Its fields as they are now, the ones this run wrote to among them: asked for here, as it's rare
            const body_fields f = fields_of(rows);
            bool rests = true; // Where the last step left it, and still, by what its component says
            for (int k = 0; k < PLACE; k++) rests &= f.now[k][i] == f.step[k][i];
            for (int k = STILL; k < PLACE; k++) rests &= f.now[k][i] == 0;
            if (rests) continue;
            const b3BodyId body_id = body_of(active, id);
            if (!b3Body_IsValid(body_id)) continue;
            place_of(asked, b3Body_GetPosition(body_id), b3Body_GetRotation(body_id), b3Body_GetLinearVelocity(body_id),
                     b3Body_GetAngularVelocity(body_id));
            where = asked;
        }
        if (!writing) {
            to = places_of(rows);
            writing = true;
        }
        for (int k = 0; k < PLACE; k++) to.now[k][i] = to.step[k][i] = where[k];
    }
}

// What the tick's step says started touching and stopped, as Box3D has it:
// the contacts between solid shapes that began and ended, and then the same
// of its sensors, each with a shape that came into it or left it. They're the
// step's own, read in the tick that stepped, and a world that's made from its
// image has the same ones to say after its next step: what each sensor has in
// it is in the image. Asking changes nothing of the world.
int32_t b3t_touches(const int64_t stamp)
{
    (void)stamp;
    const b3ContactEvents contacts = b3World_GetContactEvents(active->id);
    const b3SensorEvents sensors = b3World_GetSensorEvents(active->id);
    return contacts.beginCount + contacts.endCount + sensors.beginCount + sensors.endCount;
}

// Whose body a shape is on, or 0 for a shape that's gone, with its body: its
// entity was destroyed, and what it touched parts from no entity.
static int64_t owner_of(const b3ShapeId shape)
{
    if (!b3Shape_IsValid(shape)) return 0;
    const b3BodyId id = b3Shape_GetBody(shape);
    return handle(id.index1, id.generation);
}

touch b3t_touch(const int32_t index)
{
    const b3ContactEvents contacts = b3World_GetContactEvents(active->id);
    const b3SensorEvents sensors = b3World_GetSensorEvents(active->id);
    touch out = {0};
    int32_t at = index;
    if (at < contacts.beginCount) {
        out.a = owner_of(contacts.beginEvents[at].shapeIdA);
        out.b = owner_of(contacts.beginEvents[at].shapeIdB);
        out.began = true;
        return out;
    }
    at -= contacts.beginCount;
    if (at < contacts.endCount) {
        out.a = owner_of(contacts.endEvents[at].shapeIdA);
        out.b = owner_of(contacts.endEvents[at].shapeIdB);
        return out;
    }
    at -= contacts.endCount;
    if (at < sensors.beginCount) {
        out.a = owner_of(sensors.beginEvents[at].sensorShapeId);
        out.b = owner_of(sensors.beginEvents[at].visitorShapeId);
        out.began = true;
        return out;
    }
    at -= sensors.beginCount;
    if (at < sensors.endCount) {
        out.a = owner_of(sensors.endEvents[at].sensorShapeId);
        out.b = owner_of(sensors.endEvents[at].visitorShapeId);
    }
    return out;
}
