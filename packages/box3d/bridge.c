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
// image goes on from that image, which is the one its match's list holds.

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
    bool step_bullet; // What Box3D has of the settings above, from here on, but for the two bytes after it
    bool asleep;
    bool step_asleep; // What the last step left `asleep` as
    int32_t step_type;
    float step_gravity_scale;
    float step_linear_damping;
    float step_angular_damping;
    uint8_t unused_too[4];
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
    int64_t a; // The two bodies, or 0 for one that's gone
    int64_t b;
    int64_t shape_a; // ...and the shape of each that met, or 0 for one that's gone
    int64_t shape_b;
    bool began;
} touch;

typedef struct strike {
    int64_t a;
    int64_t b;
    tide_float3 point;
    tide_float3 normal; // From a's body towards b's
    float speed;
} strike;

// What a query found: a body and its shape, or 0 for nothing
typedef struct found {
    int64_t body;
    int64_t shape;
    tide_float3 point;
    tide_float3 normal;
    float fraction;
} found;

// Physics, with the padding it writes out. Its lists and its map are the
// match's to read and write: C leaves them as they are.
typedef struct physics {
    tide_float3 gravity;
    int32_t substeps;
    float contact_stiffness;
    float contact_damping;
    float contact_speed;
    float bounce_speed;
    float hit_speed;
    bool sleeping;
    uint8_t unused[3];
    int32_t bodies;
    int32_t shapes;
    int64_t stamp;
    int32_t size;
    bool moved;
    uint8_t unused_too[3];
    tide_list image;
    tide_list geometry;
    tide_list gone;
    tide_map owners;
    tide_list gone_shapes;
    tide_map parts;
    tide_map sensors;
    uint8_t unused_last[4];
} physics;

_Static_assert(sizeof(body) == 176 && sizeof(box) == 40 && sizeof(sphere) == 32 && sizeof(capsule) == 36 && sizeof(touch) == 40
                   && sizeof(strike) == 48 && sizeof(found) == 48 && sizeof(physics) == 120 && offsetof(physics, moved) == 60,
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

// A body's settings are what code wrote of how it's to be, and what Box3D has
// of each: its type, three floats side by side, and whether it's a bullet,
// which is a byte beside the byte that says what Box3D has. Whether it's
// asleep, and what the last step left that as, are the two bytes after those:
// the four are one field of four bytes to the systems written in C.
enum { SETTINGS = 3 };
#define SET offsetof(body, gravity_scale)
#define SET_STEP offsetof(body, step_gravity_scale)
#define FLAGS offsetof(body, bullet)

_Static_assert(offsetof(body, step_position) - SET == SETTINGS * sizeof(float) && sizeof(body) - SET_STEP == (SETTINGS + 1) * sizeof(float)
                   && offsetof(body, step_bullet) == FLAGS + 1 && offsetof(body, asleep) == FLAGS + 2
                   && offsetof(body, step_asleep) == FLAGS + 3 && FLAGS % 4 == 0 && offsetof(body, step_type) == FLAGS + 4,
               "a Body's settings, and what Box3D has of them, aren't where the systems written in C look for them any more");

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
// with its entity, for the system after it to take (b3t_made_count): the match
// keeps whose each body is in a map, which is Tide's to write. And each with
// the impulses its component was spawned with, which the step gives it
// (push_new): a body has no weight to push until its shapes are on it, later
// in the tick.
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

// The bodies code put to sleep this tick (sync_body), in the order Enter came
// to them. The step puts each to sleep right before it steps (put_to_sleep),
// after everything else the tick wrote has gone to Box3D: a body's own move
// or push, and those of the bodies it touches, would wake it again.
static int64_t *sleeps;
static int32_t sleep_count;
static int32_t sleep_room;
// The running tick's step put bodies to sleep that way. Box3D takes the bodies
// that touch one with it, and says nothing of which those were: b3t_leave then
// asks, of each body that was awake and that the step says nothing of.
static bool slept;

// The bodies the running tick's Enter told Box3D something of: the step notes
// the ones it then left alone (note_left).
static int64_t *synced;
static int32_t synced_count;
static int32_t synced_room;

// The shapes the running tick destroyed by themselves, a part's (b3t_gone_shapes),
// each with the body it was on: the step's events still name such a shape, for
// what it was touching, and Box3D no longer knows whose it was.
typedef struct parted {
    int64_t shape;
    int64_t body;
} parted;

static parted *parteds;
static int32_t parted_count;
static int32_t parted_room;

// The running tick changed a body's type. Box3D lets go of everything such a
// body touches and finds it again in the step, so what still touches is said
// to have parted and to have met: b3t_touches says neither.
static bool retyped;

// What the tick's step says started touching and stopped, for the system that
// reports it (b3t_touches, b3t_touch).
static touch *touches;
static int32_t touch_count;
static int32_t touch_room;

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
static int32_t rebuilds;      // ...of them, the ones a question asked for (b3t_rebuild)
static int last_workers = 1;  // What the last tick's world had: a world made for a question has as many, so a tick can take it
static bool measuring; // No clock is read otherwise
static bool checking;  // Every image written in parts is compared with the image written whole
static bool always;    // ...and images are written in parts whatever is awake
static bool wholly;    // Every image is written whole, though it could be written in parts
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

static b3ShapeId shape_of(const world *w, const int64_t id)
{
    return (b3ShapeId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

// A body's or a shape's handle without its world, which is whichever of C's
// worlds the match's state is in.
static int64_t handle(const int32_t index1, const uint16_t generation)
{
    return (int64_t)((uint64_t)(uint32_t)index1 | (uint64_t)generation << 32);
}

// More room in an array that doubles, for one more element.
static void *grown(void *array, const int32_t count, int32_t *room, const size_t size)
{
    if (count < *room) return array;
    const int32_t more = *room ? *room * 2 : 64;
    array = tide_realloc(array, (size_t)*room * size, (size_t)more * size);
    *room = more;
    return array;
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
    // A tick starts here. The last one's new bodies each have their entity: Own takes all that Enter made
    if (made_taken != made_count) {
        fprintf(stderr, "Box3D: %d bodies were made last tick that no entity was given\n", (int)(made_count - made_taken));
        abort();
    }
    made_count = made_taken = 0;
    sleep_count = 0;
    synced_count = 0;
    parted_count = 0;
    touch_count = 0;
    retyped = false;
    const int workers = workers_now();
    last_workers = workers;
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

// One of C's worlds to make a match's state in: one that's free, or else the
// one a tick had longest ago, which isn't `kept`.
static world *spare(const world *kept)
{
    for (int i = 0; i < WORLDS; i++) {
        if (!worlds[i].made) return &worlds[i];
    }
    world *w = NULL;
    for (int i = 0; i < WORLDS; i++) {
        if (&worlds[i] != kept && (!w || worlds[i].used < w->used)) w = &worlds[i];
    }
    return w;
}

// A copy of the image a match's list holds, or is about to, which measuring
// and checking go by.
static void remember(world *w, const uint8_t *image, const int size)
{
    w->before = tide_realloc(w->before, (size_t)w->before_size, (size_t)size);
    memcpy(w->before, image, (size_t)size);
    w->before_size = size;
    w->before_good = true;
}

// `w` made from an image: the state at `stamp`.
static void restore_into(world *w, const int workers, const uint8_t *image, const int32_t size, const uint8_t *geometry,
                         const int32_t geometry_size, const int64_t stamp)
{
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
    // Its next image goes on from the one it was made from, which the match's list holds (b3World_Restore).
    // A measured or checked run's copy of the last image is that one
    w->before_good = false;
    if (size > 0 && (measuring || checking)) {
        const uint64_t counting = measuring ? tide_time_now_ns() : 0;
        remember(w, image, size);
        if (measuring) counting_ns += tide_time_now_ns() - counting;
    }
    w->chain = (uint64_t)stamp;
    w->used = ++uses;
    w->stamp = stamp; // Last: it's what a question finds the world by (at)
}

void b3t_restore(const uint8_t *image, const int32_t size, const uint8_t *geometry, const int32_t geometry_size, const int64_t stamp)
{
    world *w = spare(NULL);
    restore_into(w, workers_now(), image, size, geometry, geometry_size, stamp);
    active = w;
}

// A part's shapes go, each by itself: its entity was destroyed, and its body's
// wasn't. What rested on one wakes, as Box3D does for a shape that goes.
void b3t_gone_shapes(const int64_t *shapes, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b3ShapeId id = shape_of(active, shapes[i]);
        if (!b3Shape_IsValid(id)) continue; // Gone already, with its body
        const b3BodyId on = b3Shape_GetBody(id);
        parteds = grown(parteds, parted_count, &parted_room, sizeof *parteds);
        parteds[parted_count++] = (parted){shapes[i], handle(on.index1, on.generation)};
        b3DestroyShape(id, true);
        note('h', &shapes[i], sizeof shapes[i]);
    }
}

// The shapes on a body, for the system that's about to remove it to ask of
// each whether it's a part's: how many, and then each (b3t_body_shape).
static b3ShapeId *listed;
static int32_t listed_count;
static int32_t listed_room;

int32_t b3t_body_shapes(const int64_t body)
{
    listed_count = 0;
    const b3BodyId id = body_of(active, body);
    if (!b3Body_IsValid(id)) return 0;
    const int count = b3Body_GetShapeCount(id);
    while (listed_room < count) listed = grown(listed, listed_room, &listed_room, sizeof *listed);
    listed_count = b3Body_GetShapes(id, listed, count);
    return listed_count;
}

int64_t b3t_body_shape(const int32_t index)
{
    if (index < 0 || index >= listed_count) return 0;
    return handle(listed[index].index1, listed[index].generation);
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
    sleep_count = 0;
    slept = false;
    synced_count = 0;
    parted_count = 0;
    touch_count = 0;
    retyped = false;
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

// For tests: the images from here on are written whole, where they'd be
// written in parts: what a match's list holds has to be the same bytes either
// way, which a match played both ways says.
void b3t_whole(const bool on)
{
    wholly = on;
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

// For tests: a hash of the image the last tick's world wrote, by itself, where
// every image is checked (b3t_check), which keeps a copy of it: what Box3D's
// state is, apart from everything else a match holds. 0 with no copy.
uint64_t b3t_image_hash(void)
{
    const world *w = active;
    if (!w || !w->before_good) return 0;
    return tide_hash_end(tide_hash_more(0, w->before, (size_t)w->before_size));
}

// ...and the image itself, for a test to write out when two builds disagree
// about it (box3d/TIDE.md, Updating it): its size, and its bytes where there's
// room for them.
int32_t b3t_image_bytes(uint8_t *bytes, const int32_t room)
{
    const world *w = active;
    if (!w || !w->before_good) return 0;
    if (bytes && room >= w->before_size) memcpy(bytes, w->before, (size_t)w->before_size);
    return w->before_size;
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

// ...and of those, how many were for a question asked of a state C had no
// world at (b3t_rebuild).
int32_t b3t_rebuilds(void)
{
    return rebuilds;
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

// A body that's asleep is awake from here on, with all of what it fell asleep
// with. It's a call an image in parts follows (box3d/TIDE.md): what wakes is
// written, and the rest of the image is the last one's.
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
    uint16_t did = 0; // What was done, a bit each
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
        // Spawned asleep: it's made so, by itself, and is there for the others to rest on or run into. A
        // static body is neither asleep nor awake
        if (b->type == b3_staticBody) b->asleep = false;
        def.isAwake = !b->asleep;
        id = b3CreateBody(w->id, &def);
        b->id = handle(id.index1, id.generation);
        did |= 1u;
    } else {
        // What code wrote since the last step, and only that: a body nothing
        // touched stays as Box3D has it, asleep if it was
        id = body_of(w, b->id);
        if (!b3Body_IsValid(id)) return 0;
        // Another type first, so that a speed written with it is the new type's to have: a body that never
        // moved takes none. It's the one call here an image in parts doesn't follow (box3d/TIDE.md), so the
        // next image is written whole. Box3D wakes the body, lets go of everything it touches, and finds
        // what still does in the step
        if (b->type != b->step_type) {
            b3Body_SetType(id, (b3BodyType)b->type);
            retyped = true;
            did |= 64u;
        }
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
        // How it's to be, where that isn't what Box3D has: the same fields a body is made from
        if (b->gravity_scale != b->step_gravity_scale) {
            b3Body_SetGravityScale(id, b->gravity_scale);
            did |= 128u;
        }
        if (b->linear_damping != b->step_linear_damping) {
            b3Body_SetLinearDamping(id, b->linear_damping);
            did |= 256u;
        }
        if (b->angular_damping != b->step_angular_damping) {
            b3Body_SetAngularDamping(id, b->angular_damping);
            did |= 512u;
        }
        if (b->bullet != b->step_bullet) {
            b3Body_SetBullet(id, b->bullet);
            did |= 1024u;
        }
        // A body at rest that's to be another way is awake to be it: one that weighs on things again falls
        if (did & (128u | 256u | 512u | 1024u)) wake(id);
        // Asleep or awake, where code wrote another than the last step left. Waking is done here, and
        // Box3D wakes all of what the body fell asleep with. Sleeping waits for the step (put_to_sleep).
        // A body that's static, which it may just have been made, is neither, and reads so again
        if (b->asleep != b->step_asleep) {
            if (b3Body_GetType(id) == b3_staticBody) {
                b->asleep = false;
            } else {
                if (b->asleep) {
                    sleeps = grown(sleeps, sleep_count, &sleep_room, sizeof *sleeps);
                    sleeps[sleep_count++] = b->id;
                } else {
                    wake(id);
                }
                did |= 2048u;
            }
        }
    }
    b->step_asleep = b->asleep; // What Leave makes of both, once the step has had its say
    b->step_type = b->type;
    b->step_gravity_scale = b->gravity_scale;
    b->step_linear_damping = b->linear_damping;
    b->step_angular_damping = b->angular_damping;
    b->step_bullet = b->bullet;
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
    const int32_t *id; // The low half of it, which a long is as two fields: the body's index, from 1
    const float *now[PLACE];  // Where its component says it is, and how fast
    const float *step[PLACE]; // ...and where the last step left it
    const float *push[PUSH];  // The impulses it's to be given
    const int32_t *type;      // How it's to be: its type,
    const int32_t *step_type; // ...which Box3D has as this,
    const float *set[SETTINGS];      // the three floats of its settings,
    const float *set_step[SETTINGS]; // ...which Box3D has as these,
    const int32_t *flags; // and whether it's a bullet, in the byte beside what Box3D has
} body_fields;

static body_fields fields_of(const tide_rows *rows)
{
    body_fields f;
    f.id = tide_rows_ints(rows, 0, offsetof(body, id));
    for (size_t k = 0; k < PLACE; k++) {
        f.now[k] = tide_rows_floats(rows, 0, NOW + k * sizeof(float));
        f.step[k] = tide_rows_floats(rows, 0, STEP + k * sizeof(float));
    }
    for (size_t k = 0; k < PUSH; k++) f.push[k] = tide_rows_floats(rows, 0, PUSHES + k * sizeof(float));
    f.type = tide_rows_ints(rows, 0, offsetof(body, type));
    f.step_type = tide_rows_ints(rows, 0, offsetof(body, step_type));
    for (size_t k = 0; k < SETTINGS; k++) {
        f.set[k] = tide_rows_floats(rows, 0, SET + k * sizeof(float));
        f.set_step[k] = tide_rows_floats(rows, 0, SET_STEP + k * sizeof(float));
    }
    f.flags = tide_rows_ints(rows, 0, FLAGS);
    return f;
}

// The byte that says whether a body is a bullet, and the one beside it that
// says what Box3D has, as the four bytes they're in read: apart, when the two
// aren't the same. And the same of the two bytes after them, which say whether
// it's asleep and what the last step left that as. The first byte of the four
// is the number's low one on every platform Tide builds for.
static inline uint32_t flags_apart(const int32_t flags)
{
    const uint32_t bits = (uint32_t)flags;
    return (bits ^ bits >> 8) & 0x00ff00ffu;
}

// What the last step left a body as, asleep or not, out of the same four
// bytes, and the four with both of the two that say so saying `asleep`.
static inline bool flags_step_asleep(const int32_t flags)
{
    return ((uint32_t)flags >> 24 & 0xffu) != 0;
}

static inline int32_t flags_left(const int32_t flags, const bool asleep)
{
    return (int32_t)(((uint32_t)flags & 0xffffu) | (asleep ? 0x01010000u : 0u));
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
    for (uint32_t i = 0; i < count; i++) wrote[i] = id[i] == 0; // New: a body that was made has an index, from 1
    for (int k = 0; k < PLACE; k++) { // Moved, turned, pushed or spun
        const float *restrict now = f->now[k] + first;
        const float *restrict step = f->step[k] + first;
        for (uint32_t i = 0; i < count; i++) wrote[i] |= now[i] != step[i];
    }
    for (int k = 0; k < PUSH; k++) { // Nudged
        const float *restrict push = f->push[k] + first;
        for (uint32_t i = 0; i < count; i++) wrote[i] |= push[i] != 0;
    }
    // To be another way than Box3D has it
    const int32_t *restrict type = f->type + first;
    const int32_t *restrict step_type = f->step_type + first;
    for (uint32_t i = 0; i < count; i++) wrote[i] |= type[i] != step_type[i];
    for (int k = 0; k < SETTINGS; k++) {
        const float *restrict set = f->set[k] + first;
        const float *restrict set_step = f->set_step[k] + first;
        for (uint32_t i = 0; i < count; i++) wrote[i] |= set[i] != set_step[i];
    }
    const int32_t *restrict flags = f->flags + first;
    for (uint32_t i = 0; i < count; i++) wrote[i] |= flags_apart(flags[i]) != 0;
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
            // One that's made asleep stays so: the impulses it was spawned with would wake it
            const tide_float3 no_push = {0, 0, 0};
            made[made_count++] = b.asleep ? (new_body){b.id, entities[i], no_push, no_push}
                                          : (new_body){b.id, entities[i], impulse, angular_impulse};
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

// The bodies the tick's Enter made, for the system that gives each its entity:
// how many, and then each with whose it is, in the order Enter made them. A
// tick that makes none costs the one call.
int32_t b3t_made_count(void)
{
    made_taken = made_count;
    return made_count;
}

int64_t b3t_made_body(const int32_t index)
{
    return index >= 0 && index < made_count ? made[index].body : 0;
}

tide_entity b3t_made_entity(const int32_t index)
{
    return index >= 0 && index < made_count ? made[index].entity : (tide_entity){0};
}

static b3ShapeDef shape_def(const float density, const float friction, const float bounce, const bool sensor)
{
    b3ShapeDef def = b3DefaultShapeDef();
    def.density = density;
    def.baseMaterial.friction = friction;
    def.baseMaterial.restitution = bounce;
    def.isSensor = sensor;
    def.enableContactEvents = true; // Touched and Parted
    // ...and Hit, for what meets it faster than the world's threshold: Box3D only keeps how fast two
    // shapes close on each other where one of them asks. It's a bit of each shape's flags in the image, a
    // bit of each contact's, and that speed in each point of a contact that touches (box3d/TIDE.md)
    def.enableHitEvents = true;
    // ...which a sensor sends too, for the shapes in it. Box3D only reports a shape to a sensor when both
    // say so, and neither does unless it's told to: every shape does, as any may come into a sensor
    def.enableSensorEvents = true;
    // A sensor doesn't report another: each would report the other, and their entities hear of it twice.
    // Shapes of one group below zero are never set against each other
    if (sensor) def.filter.groupIndex = -1;
    return def;
}

int64_t b3t_box(const int64_t on, const box *s)
{
    if (on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner)) return 0;
    note('x', &on, sizeof on);
    note('x', s, sizeof *s);
    const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b3BoxHull hull = b3MakeOffsetBoxHull(0.5f * s->size.x, 0.5f * s->size.y, 0.5f * s->size.z, vec(s->offset));
    const b3ShapeId made = b3CreateHullShape(owner, &def, &hull.base);
    return handle(made.index1, made.generation);
}

int64_t b3t_sphere(const int64_t on, const sphere *s)
{
    if (on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner)) return 0;
    note('o', &on, sizeof on);
    note('o', s, sizeof *s);
    const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b3Sphere round = {vec(s->offset), s->radius};
    const b3ShapeId made = b3CreateSphereShape(owner, &def, &round);
    return handle(made.index1, made.generation);
}

// A capsule stands along its body's y, `height` from end to end, as Unity's
// does: its two centers are a radius in from each. One no higher than it's
// wide is a sphere.
int64_t b3t_capsule(const int64_t on, const capsule *s)
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
    const b3ShapeId made = b3CreateCapsuleShape(owner, &def, &pill);
    return handle(made.index1, made.generation);
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

// The bodies code put to sleep this tick go to sleep, now that everything else
// the tick wrote is Box3D's: last, so that each is asleep as the step starts.
// Box3D puts a body's island to sleep with it, the bodies that touch it and
// the ones those touch, wherever they are and however fast: what they have of
// speed is gone, and each stays where it is until something wakes it. In the
// order Enter came to them, and noted with them (sync_body), so nothing is
// noted here. It's a call an image in parts follows. Whether any was.
static bool put_to_sleep(const world *w)
{
    const bool any = sleep_count > 0;
    for (int32_t i = 0; i < sleep_count; i++) {
        const b3BodyId id = body_of(w, sleeps[i]);
        // One that's asleep already went with a body before it in the list
        if (b3Body_IsValid(id) && b3Body_GetType(id) != b3_staticBody && b3Body_IsAwake(id)) b3Body_SetAwake(id, false);
    }
    sleep_count = 0;
    return any;
}

// How the world is to step: Physics' fields that say so, in their order.
typedef struct tuning {
    float contact_stiffness;
    float contact_damping;
    float contact_speed;
    float bounce_speed;
    float hit_speed;
    bool sleeping;
} tuning;

int64_t b3t_step(const float dt, const int32_t substeps, const tide_float3 gravity, const float contact_stiffness,
                 const float contact_damping, const float contact_speed, const float bounce_speed, const float hit_speed,
                 const bool sleeping)
{
    world *w = active;
    // Gravity that isn't what the world's last step had: the world's own is in its image, so it's the
    // last step's wherever the world came from
    const bool turned = !same(float3(b3World_GetGravity(w->id)), gravity);
    // ...and bodies that may sleep no more, where they might: the world's own says, the same way
    const bool roused = !sleeping && b3World_IsSleepingEnabled(w->id);
    // Nothing awake, nothing done to the world since its last step, and what pulls on it as it was: a
    // step would leave it as it is. The stamp stays, which says so. The rest of how it's to step waits
    // for a step there's something to step in.
    if (!turned && !roused && w->stamp != 0 && w->chain == (uint64_t)w->stamp && b3World_GetAwakeBodyCount(w->id) == 0) {
        return w->stamp;
    }
    tuning tuned;
    memset(&tuned, 0, sizeof tuned); // With the bytes between its fields, as it's noted whole
    tuned.contact_stiffness = contact_stiffness;
    tuned.contact_damping = contact_damping;
    tuned.contact_speed = contact_speed;
    tuned.bounce_speed = bounce_speed;
    tuned.hit_speed = hit_speed;
    tuned.sleeping = sleeping;
    note('s', &dt, sizeof dt);
    note('s', &substeps, sizeof substeps);
    note('s', &gravity, sizeof gravity);
    note('s', &tuned, sizeof tuned);
    push_new(w);
    // The world's settings are the match's, every step: each of these is a number of the world's that
    // every image writes, and a call an image in parts follows (box3d/TIDE.md)
    b3World_SetGravity(w->id, vec(gravity));
    b3World_SetContactTuning(w->id, contact_stiffness, contact_damping, contact_speed);
    b3World_SetRestitutionThreshold(w->id, bounce_speed);
    b3World_SetHitEventThreshold(w->id, hit_speed);
    // Everything is awake to feel a gravity that changed: a body at rest would stay where the old one
    // left it. Box3D wakes every body when sleeping is turned off, and has no other call for it. It
    // follows from the gravity, which every step notes. Turning sleeping off or on is no call an image
    // in parts follows, so the next image is written whole, as it would be with everything awake; one
    // that leaves it as it is does nothing.
    if (turned || roused) b3World_EnableSleeping(w->id, false);
    b3World_EnableSleeping(w->id, sleeping);
    // ...and then what code put to sleep this tick is: in a world whose bodies don't fall asleep by
    // themselves too, as that only says what the step does
    slept = put_to_sleep(w);
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
    if (!wholly && w->image_size > 0 && (!remembers || w->before_good)) {
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
// Whether it's asleep is written the same way, where its component doesn't say
// so already: awake for a body the step moved, and what Box3D says for one
// it's asked about. Any other is as its component says, as nothing else puts
// a body to sleep or wakes one without the step moving it, but in a tick whose
// step put bodies to sleep for code (`slept`): Box3D took the bodies that
// touched them along, so each body that was awake and that the step says
// nothing of is asked whether it still is. Only such a tick reads more of
// those bodies than which they are.
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
    // Whether each is asleep, by its component, and what Box3D has it as: read of the bodies below
    const int32_t *flags = tide_rows_ints(rows, 0, FLAGS);
    const int32_t *types = tide_rows_ints(rows, 0, offsetof(body, step_type));
    const uint32_t *steps = left_steps;
    const uint32_t room = left_room;
    const uint32_t step = stepped;
    const bool lulled = slept;
    body_places to = {0};
    int32_t *flags_to = NULL;
    bool writing = false;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t index = (uint32_t)ids[i];
        const bool said = index < room && steps[index] == step;
        // The step says nothing of it: it's as its component says, but where bodies were put to sleep for
        // code, and this one was awake, and isn't one that never moves
        if (!said && (!lulled || index == 0 || flags_step_asleep(flags[i]) || types[i] == b3_staticBody)) continue;
        const left *l = said ? &lefts[index] : NULL;
        const int64_t id = (int64_t)((uint64_t)index | (uint64_t)(uint32_t)ids_high[i] << 32);
        if (l && l->id != id) continue; // Another body's, which has the index now: this one is gone
        float asked[PLACE];
        const float *where = NULL;
        bool asleep = false;
        if (l && l->awake) {
            where = l->place;
        } else {
            const b3BodyId body_id = body_of(active, id);
            if (!b3Body_IsValid(body_id)) continue;
            asleep = b3Body_GetType(body_id) != b3_staticBody && !b3Body_IsAwake(body_id);
            if (!l && !asleep) continue; // Awake, and the step would have said so
            // Its fields as they are now, the ones this run wrote to among them: asked for here, as it's rare
            const body_fields f = fields_of(rows);
            bool rests = true; // Where the last step left it, and still, by what its component says
            for (int k = 0; k < PLACE; k++) rests &= f.now[k][i] == f.step[k][i];
            for (int k = STILL; k < PLACE; k++) rests &= f.now[k][i] == 0;
            if (!rests) {
                place_of(asked, b3Body_GetPosition(body_id), b3Body_GetRotation(body_id), b3Body_GetLinearVelocity(body_id),
                         b3Body_GetAngularVelocity(body_id));
                where = asked;
            }
        }
        const int32_t left_as = flags_left(flags[i], asleep);
        if (left_as != flags[i]) {
            if (!flags_to) { // What's written is what's read from here on
                flags_to = tide_rows_ints_mut(rows, 0, FLAGS);
                flags = flags_to;
            }
            flags_to[i] = left_as;
        }
        if (!where) continue;
        if (!writing) {
            to = places_of(rows);
            writing = true;
        }
        for (int k = 0; k < PLACE; k++) to.now[k][i] = to.step[k][i] = where[k];
    }
}

// Whose body a shape is on, with the shape itself, or 0 for both when it's
// gone with its body: its entity was destroyed, and what it touched parts from
// no entity. A shape that went by itself this tick, a part's, was on a body
// that's still there: the body is said, and no shape.
static void owner_of(const b3ShapeId shape, int64_t *body, int64_t *of_shape)
{
    *body = 0;
    *of_shape = 0;
    const int64_t named = handle(shape.index1, shape.generation);
    if (b3Shape_IsValid(shape)) {
        const b3BodyId id = b3Shape_GetBody(shape);
        *body = handle(id.index1, id.generation);
        *of_shape = named;
        return;
    }
    for (int32_t i = 0; i < parted_count; i++) {
        if (parteds[i].shape != named) continue;
        if (b3Body_IsValid(body_of(active, parteds[i].body))) *body = parteds[i].body;
        return;
    }
}

static void add_touch(const b3ShapeId a, const b3ShapeId b, const bool began)
{
    touches = grown(touches, touch_count, &touch_room, sizeof *touches);
    touch *t = &touches[touch_count++];
    memset(t, 0, sizeof *t);
    owner_of(a, &t->a, &t->shape_a);
    owner_of(b, &t->b, &t->shape_b);
    t->began = began;
}

static bool same_shapes(const b3ShapeId a, const b3ShapeId b, const b3ShapeId c, const b3ShapeId d)
{
    return (B3_ID_EQUALS(a, c) && B3_ID_EQUALS(b, d)) || (B3_ID_EQUALS(a, d) && B3_ID_EQUALS(b, c));
}

// What the tick's step says started touching and stopped, as Box3D has it:
// the contacts between solid shapes that began and ended, and then the same
// of its sensors, each with a shape that came into it or left it. They're the
// step's own, read in the tick that stepped, and a world that's made from its
// image has the same ones to say after its next step: what each sensor has in
// it is in the image. Asking changes nothing of the world. How many there
// are, for b3t_touch to give each.
//
// In a tick that changed a body's type, two shapes that parted and met are
// said to have done neither: they touched before it and they touch after, and
// it's Box3D that let go of them between (sync_body).
int32_t b3t_touches(const int64_t stamp)
{
    (void)stamp;
    const b3ContactEvents contacts = b3World_GetContactEvents(active->id);
    const b3SensorEvents sensors = b3World_GetSensorEvents(active->id);
    touch_count = 0;
    for (int i = 0; i < contacts.beginCount; i++) {
        const b3ContactBeginTouchEvent *event = &contacts.beginEvents[i];
        bool again = false;
        for (int k = 0; retyped && k < contacts.endCount && !again; k++) {
            again = same_shapes(event->shapeIdA, event->shapeIdB, contacts.endEvents[k].shapeIdA, contacts.endEvents[k].shapeIdB);
        }
        if (!again) add_touch(event->shapeIdA, event->shapeIdB, true);
    }
    for (int i = 0; i < contacts.endCount; i++) {
        const b3ContactEndTouchEvent *event = &contacts.endEvents[i];
        bool again = false;
        for (int k = 0; retyped && k < contacts.beginCount && !again; k++) {
            again = same_shapes(event->shapeIdA, event->shapeIdB, contacts.beginEvents[k].shapeIdA, contacts.beginEvents[k].shapeIdB);
        }
        if (!again) add_touch(event->shapeIdA, event->shapeIdB, false);
    }
    for (int i = 0; i < sensors.beginCount; i++) add_touch(sensors.beginEvents[i].sensorShapeId, sensors.beginEvents[i].visitorShapeId, true);
    for (int i = 0; i < sensors.endCount; i++) add_touch(sensors.endEvents[i].sensorShapeId, sensors.endEvents[i].visitorShapeId, false);
    return touch_count;
}

touch b3t_touch(const int32_t index)
{
    if (index < 0 || index >= touch_count) return (touch){0};
    return touches[index];
}

// The shapes that met in the tick's step faster than the world's threshold,
// each pair once, with where and how fast: Box3D's own, as its touches are,
// and in the order of its contacts.
int32_t b3t_hits(void)
{
    return b3World_GetContactEvents(active->id).hitCount;
}

strike b3t_hit(const int32_t index)
{
    const b3ContactEvents contacts = b3World_GetContactEvents(active->id);
    strike out;
    memset(&out, 0, sizeof out);
    if (index < 0 || index >= contacts.hitCount) return out;
    const b3ContactHitEvent *event = &contacts.hitEvents[index];
    int64_t shape;
    owner_of(event->shapeIdA, &out.a, &shape);
    owner_of(event->shapeIdB, &out.b, &shape);
    out.point = float3(b3ToVec3(event->point));
    out.normal = float3(event->normal);
    out.speed = event->approachSpeed;
    return out;
}

// ---------------------------------------------------------------------------
// Questions
//
// What code asks of the world without changing it: what a ray, or a shape
// that's moved, meets first; what a shape overlaps; and what's in a sensor.
// Each is asked of the state a stamp names, in whichever of C's worlds is at
// it (at), and none of them does anything to that world: Box3D's own queries
// write nothing, so the world's next image is what it would have been.
//
// An answer is the same on every machine for the same state, and doesn't go
// by how Box3D happens to have its shapes in its trees:
// - What each shape says of a ray or a shape that's cast at it is worked out
//   from the shape, where its body is and the cast alone, in plain floats.
// - The nearest of them is the one with the least fraction, and of several
//   at the same fraction the one whose shape was made first (its handle).
//   Box3D's trees are only asked for the shapes that could be it: the search
//   is cut short a little past the nearest so far, never at it, so a shape
//   as near is looked at whichever comes first (clip).
// - What a shape overlaps, and what's in a sensor, come back in the order of
//   their bodies' ids, each body once.
//
// A sensor is in no one's way, so none of these finds one: what's in it is
// what to ask of it (b3t_inside). And a body whose entity was destroyed is
// still in the world until the next step, as is a part's shape: the match
// lists both (Physics.gone, Physics.goneShapes), and they're passed by.
//
// Systems that ask run on the tick's threads, several at once, so nothing
// here keeps anything between calls but which worlds there are, and a world
// is only made (b3t_rebuild) one thread at a time.

static uint32_t rebuilding; // A lock: 1 while a thread makes a world for a question

// C's world at this state, or NULL: the tick's, if it's there, and else any.
static world *at(const int64_t stamp)
{
    world *tick = active;
    if (tick && tick->made && tick->stamp == stamp) return tick;
    for (int i = 0; i < WORLDS; i++) {
        if (worlds[i].made && worlds[i].stamp == stamp) return &worlds[i];
    }
    return NULL;
}

// Whether C has a world at this state to ask. A match nothing stepped has no
// state yet, and nothing to find in it.
bool b3t_find(const int64_t stamp)
{
    return stamp != 0 && at(stamp) != NULL;
}

// A world at this state, made from the match's image of it, for a question
// asked where C has none: a view of a tick this machine ran a while ago, or a
// system that runs ahead of the step in a tick that goes back to an older
// state. It takes the place of the world a tick had longest ago, and never of
// the running tick's. A tick at this state finds it, as it would any.
void b3t_rebuild(const uint8_t *image, const int32_t size, const uint8_t *geometry, const int32_t geometry_size, const int64_t stamp)
{
    if (stamp == 0 || size <= 0) return;
    while (__atomic_exchange_n(&rebuilding, 1u, __ATOMIC_ACQUIRE)) {
    }
    if (!at(stamp)) { // No other thread made it meanwhile
        world *w = spare(active);
        restore_into(w, w->made ? w->workers : last_workers, image, size, geometry, geometry_size, stamp);
        rebuilds++;
    }
    __atomic_store_n(&rebuilding, 0u, __ATOMIC_RELEASE);
}

// A question, and its answer so far.
typedef struct asked {
    const int64_t *gone; // The bodies whose entities are destroyed, which the next step removes
    int32_t gone_count;
    const int64_t *gone_shapes; // ...and the parts' shapes
    int32_t gone_shape_count;
    bool ray; // A ray says nothing of what it starts inside
    bool hit;
    int64_t shape; // The nearest so far
    b3Vec3 point;
    b3Vec3 normal;
    float fraction;
} asked;

static bool finite3(const tide_float3 v)
{
    return v.x - v.x == 0 && v.y - v.y == 0 && v.z - v.z == 0; // Neither NaN nor infinite
}

// Whether a question passes a shape by: a sensor, or one whose entity is no
// more.
static bool passed(const asked *a, const b3ShapeId shape)
{
    if (b3Shape_IsSensor(shape)) return true;
    if (a->gone_shape_count > 0) {
        const int64_t named = handle(shape.index1, shape.generation);
        for (int32_t i = 0; i < a->gone_shape_count; i++) {
            if (a->gone_shapes[i] == named) return true;
        }
    }
    if (a->gone_count > 0) {
        const b3BodyId on = b3Shape_GetBody(shape);
        const int64_t named = handle(on.index1, on.generation);
        for (int32_t i = 0; i < a->gone_count; i++) {
            if (a->gone[i] == named) return true;
        }
    }
    return false;
}

// How far Box3D goes on looking, once something was found at `fraction`: a
// little past it. What a shape says of a cast doesn't go by this (it's worked
// out before it's compared), but a shape exactly as near as the nearest has
// to be looked at whichever of the two Box3D comes to first, and one that's
// cut off right at the nearest might not be.
static float clip(const float fraction)
{
    const float past = fraction * 1.0001f + 0.000001f;
    return past < 1.0f ? past : 1.0f;
}

static float nearest(const b3ShapeId shape, const b3Pos point, const b3Vec3 normal, const float fraction, const uint64_t material,
                     const int triangle, const int child, void *context)
{
    (void)material;
    (void)triangle;
    (void)child;
    asked *a = context;
    if ((a->ray && fraction == 0.0f) || passed(a, shape)) return -1.0f; // Not this one: go on as before
    const int64_t named = handle(shape.index1, shape.generation);
    if (!a->hit || fraction < a->fraction || (fraction == a->fraction && named < a->shape)) {
        a->hit = true;
        a->shape = named;
        a->point = b3ToVec3(point);
        a->normal = normal;
        a->fraction = fraction;
    }
    return clip(a->fraction);
}

static found answer(const world *w, const asked *a)
{
    found out;
    memset(&out, 0, sizeof out);
    if (!a->hit) return out;
    const b3BodyId on = b3Shape_GetBody(shape_of(w, a->shape));
    out.body = handle(on.index1, on.generation);
    out.shape = a->shape;
    out.point = float3(a->point);
    out.normal = float3(a->normal);
    out.fraction = a->fraction;
    return out;
}

// The first thing a ray from one point to another meets: the body and its
// shape, where, the shape's surface there, and how far along. Nothing, for a
// ray that meets nothing, and of what it starts inside.
found b3t_ray(const int64_t stamp, const tide_float3 from, const tide_float3 to, const int64_t *gone, const int32_t gone_count,
              const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    asked a = {.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count, .ray = true};
    const world *w = at(stamp);
    if (!w || !finite3(from) || !finite3(to)) return answer(w, &a);
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return answer(w, &a);
    b3World_CastRay(w->id, b3ToPos(vec(from)), along, b3DefaultQueryFilter(), nearest, &a);
    return answer(w, &a);
}

// A shape as Box3D's queries take one: its corners, or the middle of its
// round parts, around where it's asked about, and how far it reaches past
// them.
typedef struct probe {
    b3Vec3 points[8];
    b3ShapeProxy proxy;
    bool good; // Its sizes are numbers
} probe;

static bool finite1(const float x)
{
    return x - x == 0;
}

static probe box_probe(const box *s, const tide_quaternion rotation)
{
    probe p;
    p.good = finite3(s->size) && finite3(s->offset);
    const b3Quat q = quat(rotation);
    const b3Vec3 half = {0.5f * b3AbsFloat(s->size.x), 0.5f * b3AbsFloat(s->size.y), 0.5f * b3AbsFloat(s->size.z)};
    for (int i = 0; i < 8; i++) {
        const b3Vec3 corner = {s->offset.x + (i & 1 ? half.x : -half.x), s->offset.y + (i & 2 ? half.y : -half.y),
                               s->offset.z + (i & 4 ? half.z : -half.z)};
        p.points[i] = b3RotateVector(q, corner);
    }
    p.proxy = (b3ShapeProxy){p.points, 8, 0.0f};
    return p;
}

static probe sphere_probe(const sphere *s, const tide_quaternion rotation)
{
    probe p;
    p.good = finite1(s->radius) && finite3(s->offset);
    p.points[0] = b3RotateVector(quat(rotation), vec(s->offset));
    p.proxy = (b3ShapeProxy){p.points, 1, b3AbsFloat(s->radius)};
    return p;
}

// As a capsule on a body is: along its y, `height` from end to end.
static probe capsule_probe(const capsule *s, const tide_quaternion rotation)
{
    probe p;
    p.good = finite1(s->radius) && finite1(s->height) && finite3(s->offset);
    const b3Quat q = quat(rotation);
    const float radius = b3AbsFloat(s->radius);
    const float half = 0.5f * s->height - radius;
    const float reach = half > 0 ? half : 0;
    p.points[0] = b3RotateVector(q, (b3Vec3){s->offset.x, s->offset.y - reach, s->offset.z});
    p.points[1] = b3RotateVector(q, (b3Vec3){s->offset.x, s->offset.y + reach, s->offset.z});
    p.proxy = (b3ShapeProxy){p.points, 2, radius};
    return p;
}

static bool sound(const tide_quaternion q)
{
    return finite1(q.value.x) && finite1(q.value.y) && finite1(q.value.z) && finite1(q.value.w);
}

// The first thing a shape meets that's moved from one place to another,
// turned as it's said and without turning on the way: as a ray's answer, with
// the point where the two touch. What the shape is in where it starts is met
// there, at no way along, and that has no surface to say.
static found cast(const int64_t stamp, probe p, const tide_float3 from, const tide_float3 to, const int64_t *gone,
                  const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    asked a = {.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    const world *w = at(stamp);
    if (!w || !p.good || !finite3(from) || !finite3(to)) return answer(w, &a);
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return answer(w, &a);
    p.proxy.points = p.points; // Where they are now: the probe came by value
    b3World_CastShape(w->id, b3ToPos(vec(from)), &p.proxy, along, b3DefaultQueryFilter(), nearest, &a);
    return answer(w, &a);
}

found b3t_cast_box(const int64_t stamp, const box *s, const tide_float3 from, const tide_quaternion rotation, const tide_float3 to,
                   const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    probe p = box_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count);
}

found b3t_cast_sphere(const int64_t stamp, const sphere *s, const tide_float3 from, const tide_quaternion rotation,
                      const tide_float3 to, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                      const int32_t gone_shape_count)
{
    probe p = sphere_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count);
}

found b3t_cast_capsule(const int64_t stamp, const capsule *s, const tide_float3 from, const tide_quaternion rotation,
                       const tide_float3 to, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                       const int32_t gone_shape_count)
{
    probe p = capsule_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count);
}

// The bodies a question found, to give back each once, in the order of their
// ids.
typedef struct gathered {
    asked a;
    int64_t *bodies;
    int32_t count;
    int32_t room;
    int64_t few[64]; // Where they are until there are more
} gathered;

static void gather(gathered *g, const b3ShapeId shape)
{
    const b3BodyId on = b3Shape_GetBody(shape);
    if (g->count == g->room) {
        const int32_t more = g->room * 2;
        int64_t *bodies = tide_alloc((size_t)more * sizeof *bodies);
        memcpy(bodies, g->bodies, (size_t)g->count * sizeof *bodies);
        if (g->bodies != g->few) free(g->bodies);
        g->bodies = bodies;
        g->room = more;
    }
    g->bodies[g->count++] = handle(on.index1, on.generation);
}

static bool overlapped(const b3ShapeId shape, void *context)
{
    gathered *g = context;
    if (!passed(&g->a, shape)) gather(g, shape);
    return true; // And the rest
}

static int by_id(const void *a, const void *b)
{
    const int64_t x = *(const int64_t *)a;
    const int64_t y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

// The bodies gathered into the match's list: each once, by its id.
static void give(gathered *g, tide_list_out *out)
{
    qsort(g->bodies, (size_t)g->count, sizeof *g->bodies, by_id);
    int32_t count = 0;
    for (int32_t i = 0; i < g->count; i++) {
        if (count == 0 || g->bodies[count - 1] != g->bodies[i]) g->bodies[count++] = g->bodies[i];
    }
    if (count > 0) memcpy(tide_list_out_room(out, (uint32_t)count), g->bodies, (size_t)count * sizeof *g->bodies);
    tide_list_out_count(out, (uint32_t)count);
    if (g->bodies != g->few) free(g->bodies);
}

// The bodies with a shape that a shape at a place overlaps, or touches.
static void overlap(tide_list_out *out, const int64_t stamp, probe p, const tide_float3 at_place, const int64_t *gone,
                    const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    gathered g;
    g.a = (asked){.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    g.bodies = g.few;
    g.count = 0;
    g.room = (int32_t)(sizeof g.few / sizeof g.few[0]);
    const world *w = at(stamp);
    if (w && p.good && finite3(at_place)) {
        p.proxy.points = p.points;
        b3World_OverlapShape(w->id, b3ToPos(vec(at_place)), &p.proxy, b3DefaultQueryFilter(), overlapped, &g);
    }
    give(&g, out);
}

void b3t_overlap_box(tide_list_out *out, const int64_t stamp, const box *s, const tide_float3 at_place, const tide_quaternion rotation,
                     const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    probe p = box_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count);
}

void b3t_overlap_sphere(tide_list_out *out, const int64_t stamp, const sphere *s, const tide_float3 at_place,
                        const tide_quaternion rotation, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                        const int32_t gone_shape_count)
{
    probe p = sphere_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count);
}

void b3t_overlap_capsule(tide_list_out *out, const int64_t stamp, const capsule *s, const tide_float3 at_place,
                         const tide_quaternion rotation, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                         const int32_t gone_shape_count)
{
    probe p = capsule_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count);
}

// The bodies with a shape in a sensor, as the last step found them: Box3D
// keeps each sensor's, and they're in the world's image, so a world made from
// one says the same. Of up to three sensors, an entity's.
void b3t_inside(tide_list_out *out, const int64_t stamp, const int64_t first, const int64_t second, const int64_t third,
                const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    gathered g;
    g.a = (asked){.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    g.bodies = g.few;
    g.count = 0;
    g.room = (int32_t)(sizeof g.few / sizeof g.few[0]);
    const world *w = at(stamp);
    const int64_t sensors[3] = {first, second, third};
    for (int k = 0; w && k < 3; k++) {
        const b3ShapeId sensor = shape_of(w, sensors[k]);
        if (sensors[k] == 0 || !b3Shape_IsValid(sensor)) continue;
        const int most = b3Shape_GetSensorCapacity(sensor);
        if (most == 0) continue;
        b3ShapeId *visitors = tide_alloc((size_t)most * sizeof *visitors);
        const int count = b3Shape_GetSensorData(sensor, visitors, most);
        for (int i = 0; i < count; i++) {
            // One that went since the step is in the sensor no more
            if (b3Shape_IsValid(visitors[i]) && !passed(&g.a, visitors[i])) gather(&g, visitors[i]);
        }
        free(visitors);
    }
    give(&g, out);
}
