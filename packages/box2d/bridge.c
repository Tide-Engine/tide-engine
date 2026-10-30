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
//
// The image goes into the match's list as what changed of the list's last
// one, where Box2D can say (b2t_image): the match's list keeps the rest, and
// what the package adds to a tick goes by what's awake, where an image
// written whole goes by how big the world is.

#include <stdatomic.h>
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
#include "tide/shard.h"
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
    float sleep_speed;
    tide_float2 step_position;
    float step_angle;
    tide_float2 step_velocity;
    float step_spin;
    bool fixed_rotation;
    bool bullet;
    bool asleep;
    bool step_asleep; // What the last step left `asleep` as
    bool rewind;
    uint8_t unused[3];
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

// Two shapes that started touching or stopped: each one's body, and the shape
// itself. A shape that's gone is 0, and so is its body when that's gone too.
typedef struct touch {
    int64_t a;
    int64_t b;
    int64_t shape_a;
    int64_t shape_b;
    bool began;
} touch;

// Two shapes that met faster than the world's hit speed: where, which way
// (from a's towards b's) and how fast.
typedef struct blow {
    int64_t a;
    int64_t b;
    int64_t shape_a;
    int64_t shape_b;
    tide_float2 point;
    tide_float2 normal;
    float speed;
} blow;

// What a ray or a shape that's cast met first, if anything.
typedef struct met {
    int64_t body;
    int64_t shape;
    tide_float2 point;
    tide_float2 normal;
    float fraction;
    bool any;
} met;

// Physics, with the padding it writes out. Its lists and its maps are the
// match's to read and write: C leaves them as they are.
typedef struct physics {
    tide_float2 gravity;
    int32_t substeps;
    float contact_stiffness;
    float contact_damping;
    float contact_speed;
    float bounce_speed;
    float hit_speed;
    bool sleeping;
    uint8_t unused0[3];
    int32_t bodies;
    int32_t shapes;
    uint8_t unused1[4];
    int64_t stamp;
    int32_t size;
    bool moved;
    uint8_t unused2[3];
    int32_t dropped;
    uint8_t unused3[4];
    tide_list image;
    tide_list gone;
    tide_list shed;
    tide_map owners;
    tide_map parts;
    tide_map sensors;
    float history_dt;
    float history_limit;
    int32_t history_tick;
    uint8_t history_padding[4];
    tide_map selected;
    tide_list pending;
    tide_list history;
    tide_map joints;
    tide_map fixtures;
    tide_list topology;
    bool topology_changed;
    uint8_t topology_padding[7];
    int64_t shard_moves; // Shards.moves as the last tick had it
    tide_map copies;     // Other shards' bodies near this one's border, by entity...
    tide_map copy_of;    // ...and whose each copy is
    tide_map copied_parts; // ...and the parts whose shapes are on one
} physics;

_Static_assert(sizeof(body) == 96 && offsetof(body, sleep_speed) == 60 && offsetof(body, fixed_rotation) == 88 && sizeof(box) == 32
                   && sizeof(circle) == 28 && sizeof(touch) == 40 && sizeof(blow) == 56 && sizeof(met) == 40
                   && sizeof(physics) == 440 && offsetof(physics, moved) == 60 && offsetof(physics, shard_moves) == 336,
               "box2d.tide's types changed: these are theirs");

_Static_assert(offsetof(physics, gravity) == 0 && offsetof(physics, substeps) == 8
                   && offsetof(physics, contact_stiffness) == 12 && offsetof(physics, contact_damping) == 16
                   && offsetof(physics, contact_speed) == 20 && offsetof(physics, bounce_speed) == 24
                   && offsetof(physics, hit_speed) == 28 && offsetof(physics, sleeping) == 32
                   && offsetof(physics, bodies) == 36 && offsetof(physics, shapes) == 40
                   && offsetof(physics, stamp) == 48 && offsetof(physics, size) == 56
                   && offsetof(physics, moved) == 60 && offsetof(physics, dropped) == 64
                   && offsetof(physics, image) == 72 && offsetof(physics, gone) == 80 && offsetof(physics, shed) == 88
                   && offsetof(physics, owners) == 96 && offsetof(physics, parts) == 128 && offsetof(physics, sensors) == 160,
               "Physics's fields aren't where box2d.tide lays them out any more");

// A body's four bools are side by side in its component: one field of four
// bytes to the systems written in C (tide/rows.h), which read whether it's
// asleep, and what the last step left that as, out of it.
#define FLAGS offsetof(body, fixed_rotation)

_Static_assert(FLAGS % 4 == 0 && offsetof(body, asleep) == FLAGS + 2 && offsetof(body, step_asleep) == FLAGS + 3
                   && offsetof(body, rewind) == FLAGS + 4,
               "a Body's flags aren't where the systems written in C look for them any more");

// Whether a body is asleep, as the four bytes its flags are in read, and what
// the last step left that as. The first byte of the four is the number's low
// one on every platform Tide builds for.
static inline bool flags_asleep(const int32_t flags)
{
    return ((uint32_t)flags >> 16 & 0xffu) != 0;
}

static inline bool flags_step_asleep(const int32_t flags)
{
    return ((uint32_t)flags >> 24 & 0xffu) != 0;
}

// The same four bytes with both of those saying `asleep`.
static inline int32_t flags_left(const int32_t flags, const bool asleep)
{
    return (int32_t)(((uint32_t)flags & 0xffffu) | (asleep ? 0x01010000u : 0u));
}

// A step with fewer bodies awake than this runs on its own thread: sharing it
// out would cost more than it saves. The step itself is quicker shared from a
// few hundred bodies, but a tick whose C starts jobs runs its systems on
// threads too (tide/jobs.h), and what that costs the rest of the tick is only
// made up for from about this many. A page's threads cost more to reach, and
// WebAssembly's SIMD leaves less of a step to share out, so there it is higher.
#ifdef __wasm__
#define SHARED_BODIES 4096
#else
#define SHARED_BODIES 2048
#endif

#define WORLDS 4

typedef struct body_controls {
    int64_t id;
    float gravity_scale, linear_damping, angular_damping, sleep_speed;
    bool fixed_rotation, bullet;
    uint8_t padding[6];
} body_controls;

typedef struct shape_source {
    int64_t body;
    uint64_t epoch;
    uint32_t words[15]; // Box/Circle, optional filter, and the presence flag
} shape_source;

typedef struct world {
    b2WorldId id;
    bool made;
    int workers;
    int64_t stamp;  // The state it's at: 0 for an empty one nothing stepped
    uint64_t chain; // The stamp, and what the running tick did to the world since
    uint64_t used;  // When a tick last had it
    int bases[B2_SNAPSHOT_SECTIONS]; // Where each section of its image goes: part of its state, as the image says them
    int image_size;                  // Its last image's, which is about the next one's: how much room to ask for
    // Measuring and checking only: a copy of the last image, to count what changed of it and to see that
    // what Box2D said changed is all that did. A match keeps none: its image is in its list.
    uint8_t *before;
    int before_size;
    bool before_good; // It's the world's last image
    uint8_t *whole;   // Checking: the image written whole, to compare
    int whole_size;
    // What each body is, by its index, as Box2D was last asked or told: one more than its b2BodyType, or 0 for
    // one not asked about yet. It's Box2D's own answer remembered, so that looking at every body every tick
    // for a `type` that code wrote is a comparison and not a call (see retyped)
    uint8_t *kinds;
    uint32_t kinds_room;
    body_controls *controls;
    uint32_t controls_room;
    shape_source *shape_sources;
    uint32_t shape_sources_room;
    uint64_t shape_epoch;
} world;

static world worlds[WORLDS];
static uint64_t uses;
static world *active; // The running tick's
// The one the queries that follow are of (b2t_seek, b2t_recall). Systems that ask run on several threads at
// once, each with its own question, so what a question keeps from one call to the next is each thread's own
static TIDE_THREAD_LOCAL world *asked;
static bool shared;   // The running step's tasks go to other threads
static int32_t share_from = SHARED_BODIES; // A step with this many bodies awake is shared out

// A list of handles that grows as it needs, and is kept from call to call:
// what a query found, and what it's told to pass over.
typedef struct handles {
    int64_t *items;
    int32_t count;
    int32_t room;
} handles;

static void handles_add(handles *h, const int64_t item)
{
    if (h->count == h->room) {
        const int32_t room = h->room ? h->room * 2 : 64;
        h->items = tide_realloc(h->items, (size_t)h->room * sizeof *h->items, (size_t)room * sizeof *h->items);
        h->room = room;
    }
    h->items[h->count++] = item;
}

static bool handles_have(const handles *h, const int64_t item)
{
    for (int32_t i = 0; i < h->count; i++) {
        if (h->items[i] == item) return true;
    }
    return false;
}

// The parts' shapes the running tick took off their bodies (b2t_shed), each
// with the body it was on: what it was touching parts from it after the step,
// when the shape itself is no more to ask whose it was.
typedef struct shed_shape {
    int64_t shape;
    int64_t body;
} shed_shape;

static shed_shape *sheds;
static int32_t shed_count;
static int32_t shed_room;

// The bodies and shapes a query passes over: the ones whose entities were
// destroyed since the last step, which are in C's world until the next one.
static TIDE_THREAD_LOCAL handles hidden_bodies;
static TIDE_THREAD_LOCAL handles hidden_shapes;
static TIDE_THREAD_LOCAL handles found; // What the last query that gives a list found

// Which of C's worlds is at a stamp is looked up, and one is made from an
// image, by one thread at a time: systems that ask on several threads all ask
// of the same match's world, and where there's none, the first makes it for
// all. Asking itself needs no turn: it changes nothing of a world, and the
// tick doesn't change one while a system that asks runs, as that system reads
// the Physics the systems that change the world write.
static atomic_flag finding = ATOMIC_FLAG_INIT;
static atomic_int asking_threads;

static void find_begin(void)
{
    while (atomic_flag_test_and_set_explicit(&finding, memory_order_acquire)) {
    }
}

static void find_end(void)
{
    atomic_flag_clear_explicit(&finding, memory_order_release);
}

// The bodies the running tick's Enter made, in the order it made them, each
// with its entity, for the system after it to take (b2t_made): the match keeps
// whose each body is in a map, which is Tide's to write. And each with the
// impulses its component was spawned with, which the step gives it (push_new):
// a body has no weight to push until its shapes are on it, later in the tick.
typedef struct new_body {
    int64_t body;
    tide_entity entity;
    tide_float2 impulse;
    float turn;
} new_body;

static new_body *made;
static int32_t made_count;
static int32_t made_room;
static int32_t made_taken;

// The bodies code put to sleep this tick (sync_body), in the order Enter came
// to them. The step puts each to sleep right before it steps (put_to_sleep),
// after everything else the tick wrote has gone to Box2D: a body's own move
// or push, and those of the bodies it touches, would wake it again.
static int64_t *lulls;
static int32_t lull_count;
static int32_t lull_room;
// The running tick's step put bodies to sleep that way. Box2D takes the bodies
// that touch one with it, and says nothing of which those were: b2t_leave then
// asks, of each body that was awake and that the step says nothing of.
static bool slept;

// Where the running tick's step left a body it moved: what b2t_leave writes
// into the body's component, found by the body's index. The step says which
// bodies those are (its move events), so Leave asks Box2D nothing for them,
// and nothing for the ones it left alone.
typedef struct left {
    int64_t id;    // The body, with its generation
    uint32_t step; // The step that left it there: `stepped`'s, or it's an older one's
    bool awake;    // Still awake after it; one that fell asleep is asked for, the rare time it's wanted
    float x;
    float y;
    float angle;
    float vx;
    float vy;
    float spin;
} left;

static left *lefts;
static uint32_t left_room;
static uint32_t stepped; // Steps taken, by any world: the last one's number
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

// Sensors never report another sensor, including when custom categories or
// positive collision groups would otherwise admit the pair.
static bool sensor_filter(b2ShapeId a, b2ShapeId b, void *context)
{
    (void)context;
    return !(b2Shape_IsSensor(a) && b2Shape_IsSensor(b));
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
    if (always) b2World_SetSnapshotAwakeShare(w->id, 1);
    w->made = true;
    w->workers = workers;
    w->stamp = 0;
    w->chain = 0;
    memset(w->bases, 0, sizeof w->bases);
    w->image_size = 0;
    w->before_good = false;
    b2World_SetCustomFilterCallback(w->id, sensor_filter, NULL);
    w->shape_epoch += 1;
    if (w->shape_epoch == 0) {
        if (w->shape_sources) memset(w->shape_sources, 0, (size_t)w->shape_sources_room * sizeof *w->shape_sources);
        w->shape_epoch = 1;
    }
    if (w->controls) memset(w->controls, 0, (size_t)w->controls_room * sizeof *w->controls);
    if (w->kinds) memset(w->kinds, 0, w->kinds_room);
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

static b2ShapeId shape_of(const world *w, const int64_t id)
{
    return (b2ShapeId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

// A body's or a shape's handle without its world, which is whichever of C's
// worlds the match's state is in.
static int64_t handle(const int32_t index1, const uint16_t generation)
{
    return (int64_t)((uint64_t)(uint32_t)index1 | (uint64_t)generation << 32);
}

static int64_t body_handle(const b2BodyId id)
{
    return handle(id.index1, id.generation);
}

static int64_t shape_handle(const b2ShapeId id)
{
    return handle(id.index1, id.generation);
}

// What a body is, remembered (world.kinds): `kind` is one more than its type, or 0 to forget it.
static void know(world *w, const int32_t index, const uint8_t kind)
{
    const uint32_t at = (uint32_t)index;
    if (at >= w->kinds_room) {
        if (kind == 0) return;
        uint32_t room = w->kinds_room ? w->kinds_room : 1024u;
        while (room <= at) room *= 2u;
        w->kinds = tide_realloc(w->kinds, w->kinds_room, room);
        memset(w->kinds + w->kinds_room, 0, room - w->kinds_room);
        w->kinds_room = room;
    }
    w->kinds[at] = kind;
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
    // A tick starts here. The last one's new bodies each have their entity: Own goes through the bodies as Enter did
    if (made_taken != made_count) {
        fprintf(stderr, "Box2D: %d bodies were made last tick that no entity was given\n", (int)(made_count - made_taken));
        abort();
    }
    made_count = made_taken = 0;
    lull_count = 0;
    shed_count = 0;
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

// One of C's worlds, made from a match's image and at its stamp.
static world *restore(const uint8_t *image, const int32_t size, const int64_t stamp)
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
    w->shape_epoch += 1;
    if (w->shape_epoch == 0) {
        if (w->shape_sources) memset(w->shape_sources, 0, (size_t)w->shape_sources_room * sizeof *w->shape_sources);
        w->shape_epoch = 1;
    }
    if (w->controls) memset(w->controls, 0, (size_t)w->controls_room * sizeof *w->controls);
    if (w->kinds) memset(w->kinds, 0, w->kinds_room); // Its bodies are the image's now: none is known
    w->image_size = size;
    w->before_good = false; // Its next image is written whole
    b2World_SetCustomFilterCallback(w->id, sensor_filter, NULL);
    w->stamp = stamp;
    w->chain = (uint64_t)stamp;
    w->used = ++uses;
    return w;
}

void b2t_restore(const uint8_t *image, const int32_t size, const int64_t stamp)
{
    active = restore(image, size, stamp);
}

void b2t_gone(const int64_t *bodies, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b2BodyId id = body_of(active, bodies[i]);
        if (!b2Body_IsValid(id)) continue;
        b2DestroyBody(id);
        know(active, id.index1, 0);
        note('g', &bodies[i], sizeof bodies[i]);
    }
}

// The shapes of parts whose entities were destroyed come off their bodies,
// which weigh what's left. One whose body went with it is gone already.
void b2t_shed(const int64_t *shapes, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b2ShapeId id = shape_of(active, shapes[i]);
        if (!b2Shape_IsValid(id)) continue;
        if (shed_count == shed_room) {
            const int32_t room = shed_room ? shed_room * 2 : 16;
            sheds = tide_realloc(sheds, (size_t)shed_room * sizeof *sheds, (size_t)room * sizeof *sheds);
            shed_room = room;
        }
        sheds[shed_count++] = (shed_shape){shapes[i], body_handle(b2Shape_GetBody(id))};
        b2DestroyShape(id, true);
        note('h', &shapes[i], sizeof shapes[i]);
    }
}

// For a host, and for tests: C's worlds go, as when a program starts. Matches
// lose nothing, as their state is their own.
void b2t_forget(void)
{
    for (int i = 0; i < WORLDS; i++) {
        unmake(&worlds[i]);
        free(worlds[i].before);
        free(worlds[i].whole);
        free(worlds[i].kinds);
        free(worlds[i].controls);
        free(worlds[i].shape_sources);
        worlds[i] = (world){0};
    }
    active = NULL;
    asked = NULL;
    made_count = made_taken = 0;
    lull_count = 0;
    slept = false;
    shed_count = 0;
}

// For tests: a step is shared out over the tick's threads from this many
// bodies awake, where it takes thousands for that to pay. None is the usual.
void b2t_share_from(const int32_t bodies)
{
    share_from = bodies > 0 ? bodies : SHARED_BODIES;
}

// For tests: every image that's written as what changed of the last one is
// also taken whole, and the two compared. One byte apart ends the program,
// saying where: the image is in the match's hash, and a player who joins makes
// their world from it. With `whenever`, the worlds made from here on write
// their images in parts whenever they can, where they'd write them whole
// while most of their bodies are awake: more for the check to see.
void b2t_check(const bool on, const bool whenever)
{
    checking = on;
    always = on && whenever;
}

// How many images were written whole, and how many as what changed of the
// last one, since the program started: tests read it to see they test both.
void b2t_images(int32_t *whole, int32_t *in_parts)
{
    *whole = images_whole;
    *in_parts = images_in_parts;
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
// snapshots can't share, and what's hashed again. Counting keeps a copy of
// each image and compares the next with it, inside the tick, which only a
// measured run does: `took` is the nanoseconds of that, to take off the
// tick's.
void b2t_measure_blocks(uint64_t *changed, uint64_t *in_all, uint64_t *took)
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

// What a body is, as its component says: a number that's none of the three is
// a body that moves, as one that says nothing is.
static b2BodyType type_of(const int32_t type)
{
    return type == b2_staticBody || type == b2_kinematicBody ? (b2BodyType)type : b2_dynamicBody;
}

// A body's component into Box2D: made, if it's new, or else what code wrote
// into it since the last step. Whether it was made.
static bool sync_body(body *b)
{
    world *w = active;
    const body was = *b;
    uint8_t did = 0; // What was done, a bit each
    b2BodyId id;
    if (b->id == 0) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = type_of(b->type);
        def.position = b2ToPos((b2Vec2){b->position.x, b->position.y});
        def.rotation = b2MakeRot(b->angle);
        def.linearVelocity = (b2Vec2){b->velocity.x, b->velocity.y};
        def.angularVelocity = b->spin;
        def.linearDamping = b->linear_damping;
        def.angularDamping = b->angular_damping;
        def.gravityScale = b->gravity_scale;
        def.sleepThreshold = b->sleep_speed >= 0 ? b->sleep_speed : 0; // And 0 for what's no number
        def.motionLocks.angularZ = b->fixed_rotation;
        def.isBullet = b->bullet;
        // Spawned asleep: it's made so, by itself, and is there for the others to rest on or run into. A
        // static body is neither asleep nor awake
        if (def.type == b2_staticBody) b->asleep = false;
        def.isAwake = !b->asleep;
        id = b2CreateBody(w->id, &def);
        b->id = handle(id.index1, id.generation);
        if (b->id != 0) know(w, id.index1, (uint8_t)(def.type + 1));
        did |= 1u;
    } else {
        // What code wrote since the last step, and only that: a body nothing
        // touched stays as Box2D has it, asleep if it was
        id = body_of(w, b->id);
        if (!b2Body_IsValid(id)) return 0;
        if (b2Body_GetType(id) != type_of(b->type)) {
            // What it is, before where it is and how fast, which are then the new kind's to take. Box2D lets go
            // of everything the body touched and finds it again at the step, and wakes the body with what it's
            // joined to by touching. What rested on a body that's now one that never moves, or leaned on one
            // that's now pushed around, is woken here, while Box2D still knows what that is
            b2Body_WakeTouching(id);
            b2Body_SetType(id, type_of(b->type));
            know(w, id.index1, (uint8_t)(type_of(b->type) + 1));
            did |= 64u;
        }
        const b2Pos at = b2Body_GetPosition(id);
        if ((float)at.x != b->position.x || (float)at.y != b->position.y || b2Rot_GetAngle(b2Body_GetRotation(id)) != b->angle) {
            b2Body_SetTransform(id, b2ToPos((b2Vec2){b->position.x, b->position.y}), b2MakeRot(b->angle));
            // Box2D moves a sleeping body and leaves it asleep, hanging where it was put, and leaves asleep
            // what rested on a body that's moved from under it. Code has no other way to say a body should
            // wake than to move it, so a body that's moved is awake, and so is what it touched. It follows
            // from the move, which is what's noted.
            b2Body_SetAwake(id, true);
            b2Body_WakeTouching(id);
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
        // Asleep or awake, where code wrote another than the last step left. Waking is done here, and
        // Box2D wakes all of what the body fell asleep with. Sleeping waits for the step (put_to_sleep).
        // A static body is neither, and reads so: one that was asleep and was just made static, too
        if (b2Body_GetType(id) == b2_staticBody) {
            b->asleep = false;
        } else if (b->asleep != b->step_asleep) {
            if (!b->asleep) b2Body_SetAwake(id, true);
            did |= 128u;
        }
    }
    const bool is_new = did & 1u;
    // A new sleeping body must go last too: changing the world's sleeping setting or gravity below
    // wakes it after creation, just as those changes wake a body code put to sleep this tick.
    if (b->asleep && (is_new || b->asleep != b->step_asleep)) {
        if (lull_count == lull_room) {
            const int32_t room = lull_room ? lull_room * 2 : 64;
            lulls = tide_realloc(lulls, (size_t)lull_room * sizeof *lulls, (size_t)room * sizeof *lulls);
            lull_room = room;
        }
        lulls[lull_count++] = b->id;
    }
    b->step_asleep = b->asleep; // What Leave makes of both, once the step has had its say
    // A body that was just made has no shape yet, so no weight, and an impulse would do nothing to it: the
    // ones it was spawned with wait for the step, which gives them once its shapes are on it (push_new).
    // Either way they're given once, and noted here
    if (b->impulse.x != 0 || b->impulse.y != 0) {
        if (!is_new) b2Body_ApplyLinearImpulseToCenter(id, (b2Vec2){b->impulse.x, b->impulse.y}, true);
        b->impulse = (tide_float2){0, 0};
        did |= 16u;
    }
    if (b->turn != 0) {
        if (!is_new) b2Body_ApplyAngularImpulse(id, b->turn, true);
        b->turn = 0;
        did |= 32u;
    }
    if (did) {
        note('b', &was, sizeof was);
        note('d', &did, sizeof did);
    }
    return did & 1u;
}

// A run of bodies' fields, as the match keeps them: an array a field, a value
// a body (tide/rows.h). Asked for again once a body was written, as what's
// written may be other memory than what was read.
typedef struct body_fields {
    const int32_t *id; // A long is two fields: its low half, then its high
    const int32_t *id_high;
    const int32_t *type;
    const float *x;
    const float *y;
    const float *angle;
    const float *vx;
    const float *vy;
    const float *spin;
    const float *impulse_x;
    const float *impulse_y;
    const float *turn;
    const float *step_x;
    const float *step_y;
    const float *step_angle;
    const float *step_vx;
    const float *step_vy;
    const float *step_spin;
    const int32_t *flags; // Its four bools, of which two say whether it's asleep (flags_asleep)
} body_fields;

static body_fields fields_of(const tide_rows *rows)
{
    return (body_fields){
        .id = tide_rows_ints(rows, 0, offsetof(body, id)),
        .id_high = tide_rows_ints(rows, 0, offsetof(body, id) + 4),
        .type = tide_rows_ints(rows, 0, offsetof(body, type)),
        .x = tide_rows_floats(rows, 0, offsetof(body, position.x)),
        .y = tide_rows_floats(rows, 0, offsetof(body, position.y)),
        .angle = tide_rows_floats(rows, 0, offsetof(body, angle)),
        .vx = tide_rows_floats(rows, 0, offsetof(body, velocity.x)),
        .vy = tide_rows_floats(rows, 0, offsetof(body, velocity.y)),
        .spin = tide_rows_floats(rows, 0, offsetof(body, spin)),
        .impulse_x = tide_rows_floats(rows, 0, offsetof(body, impulse.x)),
        .impulse_y = tide_rows_floats(rows, 0, offsetof(body, impulse.y)),
        .turn = tide_rows_floats(rows, 0, offsetof(body, turn)),
        .step_x = tide_rows_floats(rows, 0, offsetof(body, step_position.x)),
        .step_y = tide_rows_floats(rows, 0, offsetof(body, step_position.y)),
        .step_angle = tide_rows_floats(rows, 0, offsetof(body, step_angle)),
        .step_vx = tide_rows_floats(rows, 0, offsetof(body, step_velocity.x)),
        .step_vy = tide_rows_floats(rows, 0, offsetof(body, step_velocity.y)),
        .step_spin = tide_rows_floats(rows, 0, offsetof(body, step_spin)),
        .flags = tide_rows_ints(rows, 0, FLAGS),
    };
}

// Whether a body's component says it's another kind than Box2D has it as:
// code wrote its `type`. There's no field for what it was: Box2D is asked. It's
// asked once for each body, and its answer kept (world.kinds) until the body
// goes or the world is made from an image, as every body is looked at every
// tick and a call into Box2D for each would cost more than all the rest of
// what Enter looks at. What's kept is only ever Box2D's own answer, or what it
// was just told.
static bool retyped(world *w, const int32_t id, const int32_t id_high, const int32_t type)
{
    const uint32_t at = (uint32_t)id;
    uint8_t kind = at < w->kinds_room ? w->kinds[at] : 0;
    if (kind == 0) {
        const b2BodyId asked_of = {id, (uint16_t)(w->id.index1 - 1), (uint16_t)(uint32_t)id_high};
        if (!b2Body_IsValid(asked_of)) return false;
        kind = (uint8_t)(b2Body_GetType(asked_of) + 1);
        know(w, id, kind);
    }
#ifndef NDEBUG
    // A build that checks itself asks every time, and holds what's kept to the answer
    const b2BodyId checked = {id, (uint16_t)(w->id.index1 - 1), (uint16_t)(uint32_t)id_high};
    if (b2Body_IsValid(checked) && kind != (uint8_t)(b2Body_GetType(checked) + 1)) {
        fprintf(stderr, "Box2D: body %d is kept as kind %d, and is %d\n", (int)id, (int)kind - 1, (int)b2Body_GetType(checked));
        abort();
    }
#endif
    return kind != (uint8_t)(type_of(type) + 1);
}

// The system Enter: new bodies are made, and what code wrote into a body since
// the last step goes to Box2D. A body nothing wrote to costs these
// comparisons, and nothing else. It runs on one thread, its rows in order
// (it takes Physics to write), as what's done to the world is noted in order.
void b2t_enter(tide_rows *rows, physics *p)
{
    (void)p;
    world *w = active;
    const uint32_t count = tide_rows_count(rows);
    const tide_entity *entities = tide_rows_entities(rows);
    body_fields f = fields_of(rows);
    for (uint32_t i = 0; i < count; i++) {
        const bool moved = f.x[i] != f.step_x[i] || f.y[i] != f.step_y[i] || f.angle[i] != f.step_angle[i];
        const bool pushed = f.vx[i] != f.step_vx[i] || f.vy[i] != f.step_vy[i] || f.spin[i] != f.step_spin[i];
        const bool nudged = f.impulse_x[i] != 0 || f.impulse_y[i] != 0 || f.turn[i] != 0;
        const bool is_new = f.id[i] == 0 && f.id_high[i] == 0;
        const bool lulled = flags_asleep(f.flags[i]) != flags_step_asleep(f.flags[i]); // Put to sleep, or woken
        if (!is_new && !moved && !pushed && !nudged && !lulled && !retyped(w, f.id[i], f.id_high[i], f.type[i])) continue;
        body b;
        tide_rows_get(rows, 0, i, &b);
        const tide_float2 impulse = b.impulse;
        const float turn = b.turn;
        if (sync_body(&b)) {
            if (made_count == made_room) {
                const int32_t room = made_room ? made_room * 2 : 64;
                made = tide_realloc(made, (size_t)made_room * sizeof *made, (size_t)room * sizeof *made);
                made_room = room;
            }
            // One that's made asleep stays so: the impulses it was spawned with would wake it
            made[made_count++] = b.asleep ? (new_body){b.id, entities[i], {0, 0}, 0} : (new_body){b.id, entities[i], impulse, turn};
        }
        tide_rows_set(rows, 0, i, &b); // Its id, and the impulses it was given, which are gone
        f = fields_of(rows);
    }
}

// The body the tick's Enter made for `entity`, once, or 0 for an entity whose
// body it didn't make. Asked for each body in the order Enter went through
// them, which is the order it made them in, so the next one made is the only
// one to look at.
int64_t b2t_made(const tide_entity entity)
{
    if (made_taken == made_count || !tide_entity_equal(made[made_taken].entity, entity)) return 0;
    return made[made_taken++].body;
}

static b2ShapeDef shape_def(const float density, const float friction, const float bounce, const bool sensor)
{
    b2ShapeDef def = b2DefaultShapeDef();
    def.density = density;
    def.material.friction = friction;
    def.material.restitution = bounce;
    def.isSensor = sensor;
    def.enableContactEvents = true; // Touched and Parted
    // ...which a sensor sends too, for the shapes in it. Box2D only reports a shape to a sensor when both
    // say so, and neither does unless it's told to: every shape does, as any may come into a sensor
    def.enableSensorEvents = true;
    // Hit, for two shapes that meet faster than the world's hit speed. Box2D only looks at how fast a contact's
    // points close for the contacts of shapes that ask, and keeps what it found in the contact: one shape of a
    // pair asking is enough, and every shape does
    def.enableHitEvents = true;
    // A sensor doesn't report another: each would report the other, and their entities hear of it twice.
    // Shapes of one group below zero are never set against each other
    if (sensor) def.filter.groupIndex = -1;
    return def;
}

// A box as its component says it, around its body's place.
static b2Polygon box_shape(const box *s)
{
    return b2MakeOffsetBox(0.5f * s->size.x, 0.5f * s->size.y, (b2Vec2){s->offset.x, s->offset.y}, b2Rot_identity);
}

// ---------------------------------------------------------------------------
// The step

// Where the step that just ran left the bodies it moved, each found by its
// index: Box2D says which those are, the ones that were awake for it, with
// where each is now. One that's still awake has its speeds read too. One that
// fell asleep is only marked: b2t_leave asks Box2D about those, if it has to.
static void note_left(const world *w)
{
    if (++stepped == 0) { // Wrapped: no older step's is this one's
        if (lefts) memset(lefts, 0, (size_t)left_room * sizeof *lefts);
        stepped = 1;
    }
    const b2BodyEvents events = b2World_GetBodyEvents(w->id);
    for (int i = 0; i < events.moveCount; i++) {
        const b2BodyMoveEvent *event = &events.moveEvents[i];
        const uint32_t index = (uint32_t)event->bodyId.index1;
        if (index >= left_room) {
            uint32_t room = left_room ? left_room : 1024u;
            while (room <= index) room *= 2u;
            lefts = tide_realloc(lefts, (size_t)left_room * sizeof *lefts, (size_t)room * sizeof *lefts);
            memset(&lefts[left_room], 0, (size_t)(room - left_room) * sizeof *lefts);
            left_room = room;
        }
        left *l = &lefts[index];
        l->id = handle(event->bodyId.index1, event->bodyId.generation);
        l->step = stepped;
        l->awake = !event->fellAsleep;
        if (!l->awake) continue;
        const b2Vec2 velocity = b2Body_GetLinearVelocity(event->bodyId);
        l->x = (float)event->transform.p.x;
        l->y = (float)event->transform.p.y;
        l->angle = b2Rot_GetAngle(event->transform.q);
        l->vx = velocity.x;
        l->vy = velocity.y;
        l->spin = b2Body_GetAngularVelocity(event->bodyId);
    }
}

// The impulses the bodies made this tick were spawned with, given now that
// their shapes are on them and they weigh something: as an impulse written
// into a body that's there is, a tick sooner. In the order the bodies were
// made, and noted with them (sync_body), so nothing is noted here.
static void push_new(const world *w)
{
    for (int32_t i = 0; i < made_count; i++) {
        const new_body *n = &made[i];
        const bool pushed = n->impulse.x != 0 || n->impulse.y != 0;
        if (!pushed && n->turn == 0) continue;
        const b2BodyId id = body_of(w, n->body);
        if (!b2Body_IsValid(id)) continue;
        if (pushed) b2Body_ApplyLinearImpulseToCenter(id, (b2Vec2){n->impulse.x, n->impulse.y}, true);
        if (n->turn != 0) b2Body_ApplyAngularImpulse(id, n->turn, true);
    }
}

// The bodies code put to sleep this tick go to sleep, now that everything else
// the tick wrote is Box2D's: last, so that each is asleep as the step starts.
// Box2D puts a body's island to sleep with it, the bodies that touch it and
// the ones those touch, wherever they are and however fast: what they have of
// speed is gone, and each stays where it is until something wakes it. In the
// order Enter came to them, and noted with them (sync_body), so nothing is
// noted here. Whether any was.
static bool put_to_sleep(const world *w)
{
    const bool any = lull_count > 0;
    for (int32_t i = 0; i < lull_count; i++) {
        const b2BodyId id = body_of(w, lulls[i]);
        // One that's asleep already went with a body before it in the list
        if (b2Body_IsValid(id) && b2Body_GetType(id) != b2_staticBody && b2Body_IsAwake(id)) b2Body_SetAwake(id, false);
    }
    lull_count = 0;
    return any;
}

// A number the world is tuned by, as Box2D takes it: none under 0, and none that isn't one.
static float tuned(const float value)
{
    return !(value >= 0) ? 0 : value > 1e9f ? 1e9f : value;
}

// How the world is tuned, as the match says, where that's not how the world is: its own tuning is in its
// image, so it's the last step's wherever the world came from, as its gravity is. Each change is noted, so
// the step that follows isn't one the world can do without, though nothing in it be awake: its image is
// what says how it's tuned.
static void tune(const world *w, const float stiffness, const float damping, const float push_speed, const float bounce_speed,
                 const float hit_speed, const bool sleeping)
{
    const float contacts[3] = {tuned(stiffness), tuned(damping), tuned(push_speed)};
    float has[3];
    b2World_GetContactTuning(w->id, &has[0], &has[1], &has[2]);
    if (has[0] != contacts[0] || has[1] != contacts[1] || has[2] != contacts[2]) {
        b2World_SetContactTuning(w->id, contacts[0], contacts[1], contacts[2]);
        note('k', contacts, sizeof contacts);
    }
    const float bounce = tuned(bounce_speed);
    if (b2World_GetRestitutionThreshold(w->id) != bounce) {
        b2World_SetRestitutionThreshold(w->id, bounce);
        note('r', &bounce, sizeof bounce);
    }
    const float hit = tuned(hit_speed);
    if (b2World_GetHitEventThreshold(w->id) != hit) {
        b2World_SetHitEventThreshold(w->id, hit);
        note('i', &hit, sizeof hit);
    }
    if (b2World_IsSleepingEnabled(w->id) != sleeping) {
        b2World_EnableSleeping(w->id, sleeping); // Off, it wakes every body
        const uint8_t sleeps = sleeping;
        note('z', &sleeps, sizeof sleeps);
    }
}

int64_t b2t_step(const float dt, const int32_t substeps, const tide_float2 gravity, const float stiffness, const float damping,
                 const float push_speed, const float bounce_speed, const float hit_speed, const bool sleeping)
{
    world *w = active;
    tune(w, stiffness, damping, push_speed, bounce_speed, hit_speed, sleeping);
    // Gravity that isn't what the world's last step had: the world's own is in its image, so it's the
    // last step's wherever the world came from
    const b2Vec2 pulled = b2World_GetGravity(w->id);
    const bool turned = pulled.x != gravity.x || pulled.y != gravity.y;
    // Nothing awake, nothing done to the world since its last step, and what pulls on it as it was: a
    // step would leave it as it is. The stamp stays, which says so.
    if (!turned && w->stamp != 0 && w->chain == (uint64_t)w->stamp && b2World_GetAwakeBodyCount(w->id) == 0) return w->stamp;
    note('s', &dt, sizeof dt);
    note('s', &substeps, sizeof substeps);
    note('s', &gravity, sizeof gravity);
    push_new(w);
    b2World_SetGravity(w->id, (b2Vec2){gravity.x, gravity.y});
    if (turned) {
        // Everything is awake to feel it: a body at rest would stay where the old gravity left it. Box2D
        // wakes every body when sleeping changes to off, and has no other call for it. Code can put a
        // body to sleep with automatic sleeping off too, so turn it on first in that case, then off again.
        // Either way leave the setting as the match says. It follows from the gravity, which every step
        // notes, and bodies code put to sleep this tick still go last (put_to_sleep).
        b2World_EnableSleeping(w->id, !sleeping);
        b2World_EnableSleeping(w->id, sleeping);
    }
    slept = put_to_sleep(w);
    shared = w->workers > 1 && b2World_GetAwakeBodyCount(w->id) >= share_from;
    shared_steps += shared;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    b2World_Step(w->id, dt, substeps);
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

enum { BLOCK = 16384 }; // What a measured run counts the image's changes by

// Which part of an image a byte is in, for what a failed check says: Box2D's
// sections, in its order (snapshot.c).
static const char *const sections[B2_SNAPSHOT_SECTIONS] = {
    "settings and ids", "static and disabled sets", "awake set", "sleeping sets", "bodies",      "shapes",
    "fat bounds",       "contacts",                 "joints",    "chains",        "sensors",     "islands",
    "static tree",      "kinematic tree",           "dynamic tree", "pairs",      "graph",
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

// The parts Box2D says changed, over the copy of the last image: what the
// match's list becomes. While measuring, the image's blocks that a part made
// otherwise are counted.
static void remember_parts(world *w, const b2SnapshotPart *parts, const int count, const int size)
{
    w->before = tide_realloc(w->before, (size_t)w->before_size, (size_t)size);
    if (size > w->before_size) memset(w->before + w->before_size, 0, (size_t)(size - w->before_size)); // As a list grows
    const int blocks = (size + BLOCK - 1) / BLOCK;
    uint8_t *changed = measuring ? tide_alloc_zeroed((size_t)blocks, 1) : NULL;
    if (changed) {
        for (int at = w->before_size / BLOCK; at < blocks; at++) changed[at] = at * BLOCK + BLOCK > w->before_size;
    }
    for (int i = 0; i < count; i++) {
        const b2SnapshotPart *part = &parts[i];
        if (part->offset < 0 || part->offset + part->size > size) {
            fprintf(stderr, "Box2D gave a part of its image that's outside it: %d bytes at %d of %d\n", part->size, part->offset, size);
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
    int sizes[B2_SNAPSHOT_SECTIONS];
    const int whole = b2World_CheckSnapshotAt(w->id, w->whole, size, w->bases, sizes);
    if (whole == size && memcmp(w->whole, w->before, (size_t)size) == 0) return;
    if (whole != size) {
        fprintf(stderr, "Box2D's image in parts is %d bytes, and its image written whole %d\n", size, whole);
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
    for (int i = 0; i < B2_SNAPSHOT_SECTIONS; i++) {
        if (w->bases[i] <= first) section = i;
    }
    const int in = section < 0 ? first : first - w->bases[section];
    fprintf(stderr, "Box2D's image in parts isn't its image written whole: %d bytes apart, the first at %d, which is byte %d of %s", apart,
            first, in, section < 0 ? "its header" : sections[section]);
    // The sections of things that each take the same room start with how many there are
    int32_t count = 0;
    if (section >= 0) memcpy(&count, w->whole + w->bases[section], sizeof count);
    const int record = section < 0 ? 0 : b2GetSnapshotRecordSize(section);
    if (record && in >= 4 && in < 4 + count * record) fprintf(stderr, ": byte %d of number %d", (in - 4) % record, (in - 4) / record);
    fprintf(stderr, " (%d of them). Written whole it's %02x, and in parts %02x\n", (int)count, w->whole[first], w->before[first]);
    abort();
}

// The world's state into the match's list, and how many bytes that is.
//
// Where Box2D can say what changed of its last image, which the list holds,
// the list keeps the rest: only what changed is written, and nothing is
// compared but that (b2World_GetSnapshotParts, tide_list_out_keep). The image
// is, byte for byte, the one written whole.
//
// Otherwise Box2D writes all of it into room for a little more than the last
// one took, and the list is then exactly it; a second time when it's more
// than that, or when a section has to move.
int32_t b2t_image(tide_list_out *out)
{
    world *w = active;
    int sizes[B2_SNAPSHOT_SECTIONS] = {0};
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    const bool remembers = measuring || checking;
    // The copy a measured or checked run goes by has to be the last image
    if (w->image_size > 0 && (!remembers || w->before_good)) {
        const b2SnapshotPart *parts;
        int count;
        const int size = b2World_GetSnapshotParts(w->id, w->bases, sizes, &parts, &count);
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
    int size = b2World_GetSnapshotAt(w->id, image, room, w->bases, sizes);
    bool moved;
    const int after = place(w, sizes, &moved);
    if (moved || size > room) { // A section outgrew its place or left too much of it, or the image its room
        if (moved) size = after;
        if (size > room) {
            room = size;
            image = tide_list_out_room(out, (uint32_t)room);
        }
        size = b2World_GetSnapshotAt(w->id, image, room, w->bases, sizes);
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

// The fields the step leaves a body's place and speeds in, to write: the ones
// code reads and writes, and the package's own, which say where the last step
// left it.
typedef struct body_places {
    float *x;
    float *y;
    float *angle;
    float *vx;
    float *vy;
    float *spin;
    float *step_x;
    float *step_y;
    float *step_angle;
    float *step_vx;
    float *step_vy;
    float *step_spin;
} body_places;

static body_places places_of(tide_rows *rows)
{
    return (body_places){
        .x = tide_rows_floats_mut(rows, 0, offsetof(body, position.x)),
        .y = tide_rows_floats_mut(rows, 0, offsetof(body, position.y)),
        .angle = tide_rows_floats_mut(rows, 0, offsetof(body, angle)),
        .vx = tide_rows_floats_mut(rows, 0, offsetof(body, velocity.x)),
        .vy = tide_rows_floats_mut(rows, 0, offsetof(body, velocity.y)),
        .spin = tide_rows_floats_mut(rows, 0, offsetof(body, spin)),
        .step_x = tide_rows_floats_mut(rows, 0, offsetof(body, step_position.x)),
        .step_y = tide_rows_floats_mut(rows, 0, offsetof(body, step_position.y)),
        .step_angle = tide_rows_floats_mut(rows, 0, offsetof(body, step_angle)),
        .step_vx = tide_rows_floats_mut(rows, 0, offsetof(body, step_velocity.x)),
        .step_vy = tide_rows_floats_mut(rows, 0, offsetof(body, step_velocity.y)),
        .step_spin = tide_rows_floats_mut(rows, 0, offsetof(body, step_spin)),
    };
}

// The system Leave: where the step left each body, into its component, for the
// bodies whose components don't say so already.
//
// A body the step moved is in what the step left (note_left), and is written
// from there. Any other wasn't awake for it: asleep, static, or just fallen
// asleep. Where its component says it's where the last step left it, and
// still, there's nothing to write, and nothing is asked. Otherwise code moved
// it and the step left it there, or it just came to rest: its component gets
// what Box2D has, so that the next tick's Enter finds nothing written.
//
// Whether it's asleep is written the same way, where its component doesn't say
// so already: awake for a body the step moved, asleep for one that fell asleep
// in it. Any other is as its component says, as nothing else puts a body to
// sleep or wakes one without the step moving it, but in a tick whose step put
// bodies to sleep for code (`slept`): Box2D took the bodies that touched them
// along, so each body that was awake and that the step says nothing of is
// asked whether it still is.
//
// The fields are only asked for to write once a body has something to write,
// so a run of bodies at rest leaves every one of them shared with the match's
// snapshots. Runs go to several threads at once: each reads what the step
// left, and writes its own rows.
void b2t_leave(tide_rows *rows, const physics *p)
{
    if (!p->moved) return; // Nothing stepped: every body is where its component says
    const uint32_t count = tide_rows_count(rows);
    body_fields f = fields_of(rows);
    body_places to = {0};
    int32_t *flags_to = NULL;
    bool writing = false;
    for (uint32_t i = 0; i < count; i++) {
        const int64_t id = (int64_t)((uint64_t)(uint32_t)f.id[i] | (uint64_t)(uint32_t)f.id_high[i] << 32);
        if (id == 0) continue;
        const uint32_t index = (uint32_t)f.id[i];
        const left *l = index < left_room && lefts[index].step == stepped && lefts[index].id == id ? &lefts[index] : NULL;
        // Whether it's asleep, where that can be other than its component says: a body the step says
        // something of, and any body in a tick whose step put bodies to sleep for code
        const bool looked = l || slept;
        const int32_t flags = looked ? f.flags[i] : 0;
        bool asleep = l ? !l->awake : flags_step_asleep(flags);
        bool placed = true; // Its place and speeds are to be written
        float x = 0;
        float y = 0;
        float angle = 0;
        float vx = 0;
        float vy = 0;
        float spin = 0;
        if (l && l->awake) {
            x = l->x;
            y = l->y;
            angle = l->angle;
            vx = l->vx;
            vy = l->vy;
            spin = l->spin;
        } else {
            const b2BodyId asked = body_of(active, id);
            // Put to sleep by code this tick, with a body it touched
            if (!l && slept && !asleep && type_of(f.type[i]) != b2_staticBody && b2Body_IsValid(asked)) asleep = !b2Body_IsAwake(asked);
            const bool still = f.vx[i] == 0 && f.vy[i] == 0 && f.spin[i] == 0 && f.step_vx[i] == 0 && f.step_vy[i] == 0
                               && f.step_spin[i] == 0;
            const bool there = f.x[i] == f.step_x[i] && f.y[i] == f.step_y[i] && f.angle[i] == f.step_angle[i];
            placed = !(still && there) && b2Body_IsValid(asked);
            if (placed) {
                const b2Pos at = b2Body_GetPosition(asked);
                const b2Vec2 velocity = b2Body_GetLinearVelocity(asked);
                x = (float)at.x;
                y = (float)at.y;
                angle = b2Rot_GetAngle(b2Body_GetRotation(asked));
                vx = velocity.x;
                vy = velocity.y;
                spin = b2Body_GetAngularVelocity(asked);
            }
        }
        const int32_t left_as = flags_left(flags, asleep);
        if (looked && left_as != flags) {
            if (!flags_to) { // What's written is what's read from here on
                flags_to = tide_rows_ints_mut(rows, 0, FLAGS);
                f = fields_of(rows);
            }
            flags_to[i] = left_as;
        }
        if (!placed) continue;
        if (!writing) {
            to = places_of(rows);
            f = fields_of(rows);
            writing = true;
        }
        to.x[i] = to.step_x[i] = x;
        to.y[i] = to.step_y[i] = y;
        to.angle[i] = to.step_angle[i] = angle;
        to.vx[i] = to.step_vx[i] = vx;
        to.vy[i] = to.step_vy[i] = vy;
        to.spin[i] = to.step_spin[i] = spin;
    }
}

// What the tick's step says started touching and stopped, as Box2D has it:
// the contacts between solid shapes that began and ended, and then the same
// of its sensors, each with a shape that came into it or left it. They're the
// step's own, read in the tick that stepped, and a world that's made from its
// image has the same ones to say after its next step: what each sensor has in
// it is in the image.
int32_t b2t_touches(const int64_t stamp)
{
    (void)stamp;
    const b2ContactEvents contacts = b2World_GetContactEvents(active->id);
    const b2SensorEvents sensors = b2World_GetSensorEvents(active->id);
    return contacts.beginCount + contacts.endCount + sensors.beginCount + sensors.endCount;
}

// A shape, and whose body it's on. One that's gone is 0, and its body with
// it, when its entity was destroyed: what it touched parts from no entity. A
// part's shape that was taken off its body this tick (b2t_shed) is gone too,
// but its body is the one it was on, which is there to hear of the parting.
static void whose(const b2ShapeId shape, int64_t *body, int64_t *id)
{
    *body = 0;
    *id = 0;
    if (b2Shape_IsValid(shape)) {
        *id = shape_handle(shape);
        *body = body_handle(b2Shape_GetBody(shape));
        return;
    }
    const int64_t was = shape_handle(shape);
    for (int32_t i = 0; i < shed_count; i++) {
        if (sheds[i].shape != was) continue;
        if (b2Body_IsValid(body_of(active, sheds[i].body))) *body = sheds[i].body;
        return;
    }
}

static touch touching(const b2ShapeId a, const b2ShapeId b, const bool began)
{
    touch out = {0};
    whose(a, &out.a, &out.shape_a);
    whose(b, &out.b, &out.shape_b);
    out.began = began;
    return out;
}

touch b2t_touch(const int32_t index)
{
    const b2ContactEvents contacts = b2World_GetContactEvents(active->id);
    const b2SensorEvents sensors = b2World_GetSensorEvents(active->id);
    int32_t at = index;
    if (at < contacts.beginCount) return touching(contacts.beginEvents[at].shapeIdA, contacts.beginEvents[at].shapeIdB, true);
    at -= contacts.beginCount;
    if (at < contacts.endCount) return touching(contacts.endEvents[at].shapeIdA, contacts.endEvents[at].shapeIdB, false);
    at -= contacts.endCount;
    if (at < sensors.beginCount) return touching(sensors.beginEvents[at].sensorShapeId, sensors.beginEvents[at].visitorShapeId, true);
    at -= sensors.beginCount;
    if (at < sensors.endCount) return touching(sensors.endEvents[at].sensorShapeId, sensors.endEvents[at].visitorShapeId, false);
    return (touch){0};
}

// The shapes the tick's step says met faster than the world's hit speed, the
// step's own as the touches are: how many, and each. Box2D finds them from
// the contacts of the shapes that ask for it, which every shape does.
int32_t b2t_hits(void)
{
    return b2World_GetContactEvents(active->id).hitCount;
}

blow b2t_hit(const int32_t index)
{
    const b2ContactEvents contacts = b2World_GetContactEvents(active->id);
    blow out = {0};
    if (index < 0 || index >= contacts.hitCount) return out;
    const b2ContactHitEvent *event = &contacts.hitEvents[index];
    whose(event->shapeIdA, &out.a, &out.shape_a);
    whose(event->shapeIdB, &out.b, &out.shape_b);
    out.point = (tide_float2){(float)event->point.x, (float)event->point.y};
    out.normal = (tide_float2){event->normal.x, event->normal.y};
    out.speed = event->approachSpeed;
    return out;
}

// ---------------------------------------------------------------------------
// Queries: what's asked of a match's world, which changes nothing of it
//
// A query is of the world a match's Physics names, by its stamp: C's world at
// that stamp, as the match's last step left it, or one made from the match's
// image, which is the same world. So it's asked from anywhere: a system
// wherever it is in a tick, a handler, a view, with the Physics it was handed,
// and from several threads at once, by a system that runs on them. Nothing
// here is noted, as nothing here changes a world.
//
// An answer is the same on every machine because the world is: every body,
// shape and tree a query reads is in the image, which is the same bytes
// everywhere. It also doesn't rest on the order Box2D's trees are gone
// through: the nearest of what a ray or a shape meets is the one with the
// least fraction, and of two as near, the one that's first among Box2D's
// shapes; and what's found in a shape or a sensor is given in the order of
// Box2D's bodies, each once.

// C's world at a match's stamp, for the queries that follow: whether there is
// one. What the last query was told to pass over is forgotten.
static world *world_at(const int64_t stamp)
{
    for (int i = 0; i < WORLDS; i++) {
        world *w = &worlds[i];
        if (!w->made || w->stamp != stamp) continue;
        w->used = ++uses;
        return w;
    }
    return NULL;
}

void b2t_query_filter(int64_t category, int64_t mask);

bool b2t_seek(const int64_t stamp)
{
    b2t_query_filter(1, -1);
    static TIDE_THREAD_LOCAL bool counted;
    if (!counted) {
        counted = true;
        atomic_fetch_add(&asking_threads, 1);
    }
    hidden_bodies.count = 0;
    hidden_shapes.count = 0;
    find_begin();
    asked = world_at(stamp);
    find_end();
    return asked != NULL;
}

// For tests: how many threads have asked something, ever.
int32_t b2t_asking_threads(void)
{
    return atomic_load(&asking_threads);
}

// ...and where there's none, one made from the match's image. It's one of
// C's worlds like any other from here on: the tick that comes to this stamp
// next finds it.
void b2t_recall(const uint8_t *image, const int32_t size, const int64_t stamp)
{
    find_begin();
    asked = world_at(stamp); // Another thread may have made it since this one looked
    if (!asked) asked = restore(image, size, stamp);
    find_end();
}

// The bodies and the parts' shapes whose entities were destroyed since the
// match's last step: they're in C's world until its next one, and no query
// meets them.
void b2t_hide(const int64_t *bodies, const int32_t body_count, const int64_t *shapes, const int32_t shape_count)
{
    for (int32_t i = 0; i < body_count; i++) handles_add(&hidden_bodies, bodies[i]);
    for (int32_t i = 0; i < shape_count; i++) handles_add(&hidden_shapes, shapes[i]);
}

// Query scratch is thread-local, like the selected read-only world.
static TIDE_THREAD_LOCAL b2QueryFilter query_filter;
void b2t_query_filter(int64_t category, int64_t mask)
{
    query_filter = (b2QueryFilter){(uint64_t)category, (uint64_t)mask};
}

// What a ray or a shape is looking for: the nearest thing in its way.
typedef struct search {
    int64_t ignore; // A body it doesn't meet: its own, usually
    met best;
} search;

static bool hidden(const int64_t body, const int64_t shape)
{
    return (hidden_bodies.count != 0 && handles_have(&hidden_bodies, body)) || (hidden_shapes.count != 0 && handles_have(&hidden_shapes, shape));
}

// Where a body or a shape is among Box2D's: no two that are there share one.
static uint32_t slot(const int64_t id)
{
    return (uint32_t)id;
}

// The least number over a fraction of the way, and no more than all of it.
static float just_past(const float fraction)
{
    if (fraction >= 1.0f) return 1.0f;
    if (!(fraction > 0.0f)) return 1e-30f;
    uint32_t bits;
    memcpy(&bits, &fraction, sizeof bits);
    bits += 1u;
    float next;
    memcpy(&next, &bits, sizeof next);
    return next;
}

// Box2D found a shape in the way. Sensors stop nothing, so they aren't met,
// and neither is the body to ignore, nor what's on its way out of the world.
// The nearest so far is kept, and of two as near, the one that's first among
// Box2D's shapes. The way is then cut to just past the nearest, where Box2D
// would cut it to the nearest: a shape exactly as near is still looked at, so
// which of the two is kept isn't which the tree came to first.
static float nearer(const b2ShapeId shape, const b2Pos point, const b2Vec2 normal, const float fraction, void *context)
{
    search *s = context;
    if (b2Shape_IsSensor(shape)) return -1.0f;
    const int64_t id = shape_handle(shape);
    const int64_t body = body_handle(b2Shape_GetBody(shape));
    if (body == s->ignore || hidden(body, id)) return -1.0f;
    if (!s->best.any || fraction < s->best.fraction || (fraction == s->best.fraction && slot(id) < slot(s->best.shape))) {
        s->best = (met){body, id, {(float)point.x, (float)point.y}, {normal.x, normal.y}, fraction, true};
    }
    return just_past(s->best.fraction);
}

static bool real(const float value)
{
    return value - value == 0.0f;
}

static bool real2(const tide_float2 value)
{
    return real(value.x) && real(value.y);
}

// The first thing a ray from one place to another meets. One that starts
// inside a shape meets it there: at no fraction of its way, with no normal.
met b2t_ray(const tide_float2 from, const tide_float2 to, const int64_t ignore)
{
    search s = {.ignore = ignore};
    if (!asked || !real2(from) || !real2(to)) return s.best;
    const b2Vec2 along = {to.x - from.x, to.y - from.y};
    if (!real2((tide_float2){along.x, along.y})) return s.best;
    b2World_CastRay(asked->id, b2ToPos((b2Vec2){from.x, from.y}), along, query_filter, nearer,
                    &s);
    return s.best;
}

// A shape moved from one place to another: where it first touches something.
// The shape's points are around `from`, as a body's shapes are around its
// place. One that touches something where it starts meets it there, as a ray
// does.
static met cast(const b2ShapeProxy *shape, const tide_float2 from, const tide_float2 to, const int64_t ignore)
{
    search s = {.ignore = ignore};
    const b2Vec2 along = {to.x - from.x, to.y - from.y};
    if (!real2((tide_float2){along.x, along.y})) return s.best;
    b2World_CastShape(asked->id, b2ToPos((b2Vec2){from.x, from.y}), shape, along, query_filter,
                      nearer, &s);
    return s.best;
}

// A box or a circle as it is on a body that's turned by `angle`, around the
// body's place: what's cast, and what's looked into. False for a shape of no
// size, which Box2D makes nothing of.
static bool box_proxy(const box *shape, const float angle, b2ShapeProxy *proxy)
{
    if (!real2(shape->size) || !real2(shape->offset) || !real(angle) || !(shape->size.x > 0) || !(shape->size.y > 0)) return false;
    const b2Polygon polygon = box_shape(shape);
    *proxy = b2MakeOffsetProxy(polygon.vertices, polygon.count, polygon.radius, b2Vec2_zero, b2MakeRot(angle));
    return true;
}

static bool circle_proxy(const circle *shape, const float angle, b2ShapeProxy *proxy)
{
    if (!real(shape->radius) || !real2(shape->offset) || !real(angle) || !(shape->radius > 0)) return false;
    const b2Vec2 center = {shape->offset.x, shape->offset.y};
    *proxy = b2MakeOffsetProxy(&center, 1, shape->radius, b2Vec2_zero, b2MakeRot(angle));
    return true;
}

met b2t_cast_box(const box *shape, const tide_float2 from, const float angle, const tide_float2 to, const int64_t ignore)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(from) || !real2(to) || !box_proxy(shape, angle, &proxy)) return (met){0};
    return cast(&proxy, from, to, ignore);
}

met b2t_cast_circle(const circle *shape, const tide_float2 from, const float angle, const tide_float2 to, const int64_t ignore)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(from) || !real2(to) || !circle_proxy(shape, angle, &proxy)) return (met){0};
    return cast(&proxy, from, to, ignore);
}

// All-hit queries keep a small result on the stack and grow only for actual
// hits. The native query reports the first hit of each collider.
typedef struct cast_hits {
    int64_t ignore;
    bool include_sensors;
    met *items;
    int32_t count, room;
    met few[32];
} cast_hits;

static float collect_cast_hit(const b2ShapeId shape, const b2Pos point, const b2Vec2 normal,
                              const float fraction, void *context)
{
    cast_hits *hits = context;
    if (!hits->include_sensors && b2Shape_IsSensor(shape)) return -1.0f;
    const int64_t id = shape_handle(shape);
    const int64_t body = body_handle(b2Shape_GetBody(shape));
    if (body == hits->ignore || hidden(body, id)) return -1.0f;
    const int32_t most = (int32_t)(UINT32_C(0x40000000) / sizeof *hits->items);
    if (hits->count == most) tide_out_of_memory();
    if (hits->count == hits->room) {
        const int32_t more = hits->room > most / 2 ? most : hits->room * 2;
        met *items = tide_alloc((size_t)more * sizeof *items);
        memcpy(items, hits->items, (size_t)hits->count * sizeof *items);
        if (hits->items != hits->few) free(hits->items);
        hits->items = items;
        hits->room = more;
    }
    met *hit = &hits->items[hits->count++];
    memset(hit, 0, sizeof *hit); // Output list padding is match state too.
    hit->body = body;
    hit->shape = id;
    hit->point = (tide_float2){(float)point.x, (float)point.y};
    hit->normal = (tide_float2){normal.x, normal.y};
    hit->fraction = fraction;
    hit->any = true;
    return 1.0f; // Keep the entire sweep: a nearer hit never clips the rest.
}

static int cast_hit_order(const void *a, const void *b)
{
    const met *x = a, *y = b;
    if (x->fraction != y->fraction) return x->fraction < y->fraction ? -1 : 1;
    if (slot(x->shape) != slot(y->shape)) return slot(x->shape) < slot(y->shape) ? -1 : 1;
    return x->shape < y->shape ? -1 : x->shape > y->shape;
}

static void give_cast_hits(cast_hits *hits, tide_list_out *out)
{
    if (hits->count > 1) qsort(hits->items, (size_t)hits->count, sizeof *hits->items, cast_hit_order);
    if (hits->count) {
        void *room = tide_list_out_room(out, (uint32_t)hits->count);
        if (!room) tide_out_of_memory();
        memcpy(room, hits->items, (size_t)hits->count * sizeof *hits->items);
    }
    tide_list_out_count(out, (uint32_t)hits->count);
    if (hits->items != hits->few) free(hits->items);
}

void b2t_ray_all(tide_list_out *out, const tide_float2 from, const tide_float2 to, const int64_t ignore, const bool include_sensors)
{
    tide_list_out_count(out, 0);
    const b2Vec2 along = {to.x - from.x, to.y - from.y};
    if (!asked || !real2(from) || !real2(to) || !real2((tide_float2){along.x, along.y})) return;
    cast_hits hits;
    hits.ignore = ignore;
    hits.include_sensors = include_sensors;
    hits.count = 0;
    hits.room = 32;
    hits.items = hits.few;
    b2World_CastRay(asked->id, b2ToPos((b2Vec2){from.x, from.y}), along, query_filter, collect_cast_hit, &hits);
    give_cast_hits(&hits, out);
}

static void cast_all(tide_list_out *out, const b2ShapeProxy *shape, const tide_float2 from, const tide_float2 to,
                     const int64_t ignore, const bool include_sensors)
{
    tide_list_out_count(out, 0);
    const b2Vec2 along = {to.x - from.x, to.y - from.y};
    if (!asked || !real2(from) || !real2(to) || !real2((tide_float2){along.x, along.y})) return;
    cast_hits hits;
    hits.ignore = ignore;
    hits.include_sensors = include_sensors;
    hits.count = 0;
    hits.room = 32;
    hits.items = hits.few;
    b2World_CastShape(asked->id, b2ToPos((b2Vec2){from.x, from.y}), shape, along, query_filter, collect_cast_hit, &hits);
    give_cast_hits(&hits, out);
}

void b2t_cast_box_all(tide_list_out *out, const box *shape, const tide_float2 from, const float angle,
                         const tide_float2 to, const int64_t ignore, const bool include_sensors)
{
    tide_list_out_count(out, 0);
    b2ShapeProxy proxy;
    if (!box_proxy(shape, angle, &proxy)) return;
    cast_all(out, &proxy, from, to, ignore, include_sensors);
}

void b2t_cast_circle_all(tide_list_out *out, const circle *shape, const tide_float2 from, const float angle,
                         const tide_float2 to, const int64_t ignore, const bool include_sensors)
{
    tide_list_out_count(out, 0);
    b2ShapeProxy proxy;
    if (!circle_proxy(shape, angle, &proxy)) return;
    cast_all(out, &proxy, from, to, ignore, include_sensors);
}

static int before(const void *a, const void *b)
{
    const int64_t x = *(const int64_t *)a;
    const int64_t y = *(const int64_t *)b;
    if (slot(x) != slot(y)) return slot(x) < slot(y) ? -1 : 1;
    return x < y ? -1 : x > y;
}

// The bodies a query found, each once, in the order they are among Box2D's,
// which is the same on every machine and whichever order they were come to
// in: into the caller's list, and how many.
static int32_t give(tide_list_out *out)
{
    if (found.count == 0) return 0;
    qsort(found.items, (size_t)found.count, sizeof *found.items, before);
    int32_t count = 1;
    for (int32_t i = 1; i < found.count; i++) {
        if (found.items[i] != found.items[count - 1]) found.items[count++] = found.items[i];
    }
    memcpy(tide_list_out_room(out, (uint32_t)count), found.items, (size_t)count * sizeof *found.items);
    tide_list_out_count(out, (uint32_t)count);
    return count;
}

static bool within(const b2ShapeId shape, void *context)
{
    (void)context;
    if (b2Shape_IsSensor(shape)) return true;
    const int64_t body = body_handle(b2Shape_GetBody(shape));
    if (!hidden(body, shape_handle(shape))) handles_add(&found, body);
    return true;
}

// The bodies with a shape that's in a shape at a place, or touches it.
static int32_t overlap(const b2ShapeProxy *shape, const tide_float2 at, tide_list_out *out)
{
    found.count = 0;
    b2World_OverlapShape(asked->id, b2ToPos((b2Vec2){at.x, at.y}), shape, query_filter, within, NULL);
    return give(out);
}

int32_t b2t_overlap_box(const box *shape, const tide_float2 at, const float angle, tide_list_out *out)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(at) || !box_proxy(shape, angle, &proxy)) return 0;
    return overlap(&proxy, at, out);
}

int32_t b2t_overlap_circle(const circle *shape, const tide_float2 at, const float angle, tide_list_out *out)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(at) || !circle_proxy(shape, angle, &proxy)) return 0;
    return overlap(&proxy, at, out);
}

// What a sensor has in it, as the last step found: the bodies of its visitors.
// A shape that's no sensor has none.
static void visitors(const b2ShapeId sensor)
{
    static TIDE_THREAD_LOCAL b2ShapeId *shapes;
    static TIDE_THREAD_LOCAL int room;
    const int count = b2Shape_GetSensorCapacity(sensor);
    if (count == 0) return;
    if (count > room) {
        shapes = tide_realloc(shapes, (size_t)room * sizeof *shapes, (size_t)count * sizeof *shapes);
        room = count;
    }
    const int there = b2Shape_GetSensorData(sensor, shapes, count);
    for (int i = 0; i < there; i++) {
        if (!b2Shape_IsValid(shapes[i])) continue; // It went since
        const int64_t body = body_handle(b2Shape_GetBody(shapes[i]));
        if (!hidden(body, shape_handle(shapes[i]))) handles_add(&found, body);
    }
}

// The bodies in an entity's sensors: in each of the two shapes given that's
// there, its box and its circle.
int32_t b2t_inside(const int64_t *given, const int32_t count, tide_list_out *out)
{
    found.count = 0;
    if (!asked) return 0;
    for (int i = 0; i < count; i++) {
        if (given[i] == 0) continue;
        const b2ShapeId shape = shape_of(asked, given[i]);
        if (b2Shape_IsValid(shape)) visitors(shape);
    }
    return give(out);
}

// Historical scenes contain plain geometry, never native world pointers.
typedef struct collider {
    int64_t body, shape;
    tide_entity entity, part;
    int32_t snaps;
    tide_float2 position;
    float angle;
    tide_float2 points[8];
    int32_t count;
    float radius;
    bool sensor;
    uint8_t padding[3];
    int32_t kind;
    int64_t category, mask;
    int32_t group;
    uint8_t filter_padding[4];
} collider;
_Static_assert(sizeof(collider) == 152 && offsetof(physics, pending) == 240, "historical scene ABI changed");

static b2Vec2 scene_vec(const tide_float2 p) { return (b2Vec2){p.x, p.y}; }
static tide_float2 scene_float(const b2Vec2 p) { return (tide_float2){p.x, p.y}; }

static bool scene_real(const collider *c)
{
    if (c->count < 1 || c->count > 8 || !real2(c->position) || !real(c->radius) || c->radius < 0 || !real(c->angle)) return false;
    for (int i = 0; i < c->count; i++) if (!real2(c->points[i])) return false;
    return true;
}

// The caller supplies a validated native body; geometry remains in its local frame.
static int scene_read(const world *w, const int64_t handle, collider **out)
{
    *out = NULL;
    if (!w) return 0;
    const b2BodyId on = body_of(w, handle);
    if (!b2Body_IsValid(on)) return 0;
    const int count = b2Body_GetShapeCount(on);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(b2ShapeId) || (size_t)count > SIZE_MAX / sizeof(collider)) return 0;
    b2ShapeId *shapes = malloc((size_t)count * sizeof *shapes);
    collider *records = calloc((size_t)count, sizeof *records);
    if (!shapes || !records) { free(shapes); free(records); return 0; }
    const int got = b2Body_GetShapes(on, shapes, count);
    const b2WorldTransform pose = b2Body_GetTransform(on);
    int used = 0;
    for (int i = 0; i < got; i++) {
        const b2ShapeId id = shapes[i];
        collider c = {0};
        c.body = handle;
        c.shape = shape_handle(id);
        c.position = (tide_float2){(float)pose.p.x, (float)pose.p.y};
        c.angle = b2Rot_GetAngle(pose.q);
        c.sensor = b2Shape_IsSensor(id);
        c.kind = b2Shape_GetType(id);
        const b2Filter filter = b2Shape_GetFilter(id);
        c.category = (int64_t)filter.categoryBits; c.mask = (int64_t)filter.maskBits;
        c.group = filter.groupIndex;
        switch (b2Shape_GetType(id)) {
        case b2_circleShape: {
            const b2Circle shape = b2Shape_GetCircle(id);
            c.points[0] = scene_float(shape.center); c.count = 1; c.radius = shape.radius;
            break;
        }
        case b2_capsuleShape: {
            const b2Capsule shape = b2Shape_GetCapsule(id);
            c.points[0] = scene_float(shape.center1); c.points[1] = scene_float(shape.center2);
            c.count = 2; c.radius = shape.radius;
            break;
        }
        case b2_segmentShape: {
            const b2Segment shape = b2Shape_GetSegment(id);
            c.points[0] = scene_float(shape.point1); c.points[1] = scene_float(shape.point2); c.count = 2;
            break;
        }
        case b2_chainSegmentShape: {
            const b2ChainSegment shape = b2Shape_GetChainSegment(id);
            c.points[0] = scene_float(shape.segment.point1); c.points[1] = scene_float(shape.segment.point2); c.count = 2;
            break;
        }
        case b2_polygonShape: {
            const b2Polygon shape = b2Shape_GetPolygon(id);
            if (shape.count < 1 || shape.count > 8) continue;
            c.count = shape.count; c.radius = shape.radius;
            for (int k = 0; k < c.count; k++) c.points[k] = scene_float(shape.vertices[k]);
            break;
        }
        default: continue;
        }
        if (scene_real(&c)) records[used++] = c;
    }
    free(shapes);
    *out = records;
    return used;
}

void b2t_read_colliders(const int64_t handle, tide_list_out *out)
{
    collider *records;
    const int count = scene_read(asked, handle, &records);
    if (count) memcpy(tide_list_out_room(out, (uint32_t)count), records, (size_t)count * sizeof *records);
    tide_list_out_count(out, (uint32_t)count);
    free(records);
}

void b2t_capture_history(tide_rows *rows, physics *p)
{
    if (!(p->history_limit > 0)) return;
    const world *w = world_at(p->stamp);
    const tide_entity *entities = tide_rows_entities(rows);
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) {
        body b;
        tide_rows_get(rows, 0, i, &b);
        if (!b.rewind) continue;
        collider *records;
        const int count = scene_read(w, b.id, &records);
        for (int k = 0; k < count; k++) {
            records[k].entity = entities[i];
            records[k].snaps = rows->entities ? (int32_t)tide_entity_snaps(rows->entities, entities[i]) : 0;
            void *record = tide_list_add(&p->pending, sizeof(collider), TIDE_IN_MATCH);
            if (!record) { p->history_limit = 0; free(records); return; }
            memcpy(record, &records[k], sizeof(collider));
        }
        free(records);
    }
}

// Geometry was captured by the physics step; identity/Snap metadata is final
// only after the tick's queued changes have applied.
typedef struct history_frame { int32_t tick; uint8_t padding[4]; tide_list colliders; } history_frame;
_Static_assert(sizeof(history_frame) == 16, "history frame ABI changed");
static void seal_history(void *context, const tide_entities *entities)
{
    physics *p = context;
    const int32_t last = tide_list_count(p->history) - 1;
    const history_frame *stored = tide_list_at(p->history, last, sizeof(history_frame));
    if (!stored) return;
    history_frame frame = *stored;
    for (int32_t i = tide_list_count(frame.colliders); i-- > 0;) {
        const collider *stored_c = tide_list_at(frame.colliders, i, sizeof(collider));
        if (!stored_c) continue;
        collider c = *stored_c;
        if (!tide_entity_alive(entities, c.entity) || !tide_entity_alive(entities, c.part)) {
            tide_list_remove_at(&frame.colliders, i, sizeof(collider));
            continue;
        }
        const int32_t snaps = (int32_t)tide_entity_snaps(entities, c.entity);
        if (c.snaps != snaps) {
            c.snaps = snaps;
            memcpy(tide_list_at_mut(frame.colliders, i, sizeof(collider)), &c, sizeof(collider));
        }
    }
    memcpy(tide_list_at_mut(p->history, last, sizeof(history_frame)), &frame, sizeof(frame));
}
void b2t_seal_history(tide_rows *rows, physics *p)
{
    if (p->history_limit > 0) tide_rows_after_tick(rows, seal_history, p);
}

// Match the view layer's short-arc interpolation, including its exact fast path.
static float scene_wrap_angle(const float angle)
{
    if (angle >= -TIDE_PI_F && angle <= TIDE_PI_F) return angle;
    if (angle > TIDE_PI_F && angle <= 3.0f * TIDE_PI_F) return angle - 2.0f * TIDE_PI_F;
    if (angle < -TIDE_PI_F && angle >= -3.0f * TIDE_PI_F) return angle + 2.0f * TIDE_PI_F;
    return tide_atan2_f(tide_sin_f(angle), tide_cos_f(angle));
}

collider b2t_blend_collider(const collider *before, const collider *current, const float alpha)
{
    collider c = *current;
    if (!scene_real(before) || !scene_real(current) || !(alpha >= 0 && alpha <= 1)) return c;
    c.position.x = before->position.x + (current->position.x - before->position.x) * alpha;
    c.position.y = before->position.y + (current->position.y - before->position.y) * alpha;
    const float delta = scene_wrap_angle(scene_wrap_angle(current->angle) - scene_wrap_angle(before->angle));
    c.angle = scene_wrap_angle(before->angle) + delta * alpha;
    return c;
}

static b2ShapeProxy scene_proxy(const collider *c, b2Vec2 points[8])
{
    for (int i = 0; i < c->count; i++) points[i] = b2RotateVector(b2MakeRot(c->angle), scene_vec(c->points[i]));
    return b2MakeProxy(points, c->count, c->radius);
}

static bool scene_overlap(const collider *c, b2ShapeProxy query, const tide_float2 position)
{
    if (!scene_real(c) || !real2(position)) return false;
    b2Vec2 points[8];
    const b2ShapeProxy target = scene_proxy(c, points);
    const b2Vec2 relative = b2Sub(scene_vec(position), scene_vec(c->position));
    if (!real2(scene_float(relative))) return false;
    const b2DistanceInput input = {.proxyA = target, .proxyB = query, .transform = {relative, b2Rot_identity}, .useRadii = true};
    b2SimplexCache cache = {0};
    return b2ShapeDistance(&input, &cache, NULL, 0).distance <= 0.0f;
}

static met scene_cast(const collider *c, b2ShapeProxy query, const tide_float2 from, const tide_float2 to)
{
    met result = {0};
    if (!scene_real(c) || !real2(from) || !real2(to)) return result;
    const b2Vec2 translation = b2Sub(scene_vec(to), scene_vec(from));
    const b2Vec2 relative = b2Sub(scene_vec(from), scene_vec(c->position));
    if (!real2(scene_float(translation)) || !real2(scene_float(relative))) return result;
    if (scene_overlap(c, query, from)) {
        result.body = c->body; result.shape = c->shape; result.point = from; result.any = true;
        return result;
    }
    b2Vec2 points[8];
    const b2ShapeCastPairInput input = {.proxyA = scene_proxy(c, points), .proxyB = query,
        .transform = {relative, b2Rot_identity}, .translationB = translation, .maxFraction = 1.0f};
    const b2CastOutput hit = b2ShapeCast(&input);
    if (!hit.hit) return result;
    result.body = c->body; result.shape = c->shape;
    result.point = scene_float(b2Add(scene_vec(c->position), hit.point));
    result.normal = scene_float(hit.normal); result.fraction = hit.fraction; result.any = true;
    return result;
}

bool b2t_scene_touch(const collider *a, const collider *b)
{
    if (!scene_real(b)) return false;
    b2Vec2 points[8];
    return scene_overlap(a, scene_proxy(b, points), b->position);
}

met b2t_scene_ray(const collider *c, const tide_float2 from, const tide_float2 to)
{
    met result = {0};
    if (!scene_real(c) || !real2(from) || !real2(to)) return result;
    const b2Vec2 start = b2InvRotateVector(b2MakeRot(c->angle), b2Sub(scene_vec(from), scene_vec(c->position)));
    const b2Vec2 finish = b2InvRotateVector(b2MakeRot(c->angle), b2Sub(scene_vec(to), scene_vec(c->position)));
    const b2Vec2 direction = b2Sub(finish, start);
    if (!real2(scene_float(start)) || !real2(scene_float(direction))) return result;
    const b2RayCastInput input = {.origin = start, .translation = direction, .maxFraction = 1.0f};
    b2CastOutput hit = {0};
    if (c->kind == b2_circleShape) {
        const b2Circle shape = {scene_vec(c->points[0]), c->radius};
        hit = b2RayCastCircle(&shape, &input);
    } else if (c->kind == b2_capsuleShape) {
        const b2Capsule shape = {scene_vec(c->points[0]), scene_vec(c->points[1]), c->radius};
        hit = b2RayCastCapsule(&shape, &input);
    } else if (c->kind == b2_segmentShape || c->kind == b2_chainSegmentShape) {
        const b2Segment shape = {scene_vec(c->points[0]), scene_vec(c->points[1])};
        hit = b2RayCastSegment(&shape, &input, c->kind == b2_chainSegmentShape);
        if (!hit.hit) return result; // One-sided back-face misses remain misses.
    } else {
        b2Hull hull = {0}; hull.count = c->count;
        if (hull.count < 3) return result;
        for (int i = 0; i < hull.count; i++) hull.points[i] = scene_vec(c->points[i]);
        if (!b2ValidateHull(&hull)) return result;
        const b2Polygon shape = b2MakePolygon(&hull, c->radius);
        hit = b2RayCastPolygon(&shape, &input);
    }
    if (!hit.hit) {
        // Box2D's world rays count their starting overlap; low-level rays don't.
        const b2Vec2 point = {0}; const b2ShapeProxy query = b2MakeProxy(&point, 1, 0); return scene_cast(c, query, from, to);
    }
    result.body = c->body; result.shape = c->shape;
    result.point = scene_float(b2Add(scene_vec(c->position), b2RotateVector(b2MakeRot(c->angle), hit.point)));
    result.normal = scene_float(b2RotateVector(b2MakeRot(c->angle), hit.normal)); result.fraction = hit.fraction; result.any = true;
    return result;
}
met b2t_scene_cast_box(const collider *c, const box *shape, const tide_float2 from, const float angle, const tide_float2 to)
{
    b2ShapeProxy query; if (!box_proxy(shape, angle, &query)) return (met){0};
    return scene_cast(c, query, from, to);
}
bool b2t_scene_overlap_box(const collider *c, const box *shape, const tide_float2 from, const float angle)
{
    b2ShapeProxy query; if (!box_proxy(shape, angle, &query)) return false;
    return scene_overlap(c, query, from);
}
met b2t_scene_cast_circle(const collider *c, const circle *shape, const tide_float2 from, const float angle, const tide_float2 to)
{
    b2ShapeProxy query; if (!circle_proxy(shape, angle, &query)) return (met){0};
    return scene_cast(c, query, from, to);
}
bool b2t_scene_overlap_circle(const collider *c, const circle *shape, const tide_float2 from, const float angle)
{
    b2ShapeProxy query; if (!circle_proxy(shape, angle, &query)) return false;
    return scene_overlap(c, query, from);
}

// Immutable preorder BVH. A missed branch jumps to its end; queries allocate
// only their short candidate list and preserve the original native handle order.
typedef struct scene_bounds { tide_float2 min, max; bool valid; uint8_t padding[3]; } scene_bounds;
typedef struct scene_node { tide_float2 min, max; int32_t collider, next; } scene_node;
_Static_assert(sizeof(scene_bounds) == 20 && sizeof(scene_node) == 24, "scene index ABI changed");
static scene_bounds proxy_bounds(const b2Vec2 *points, const int count, const float radius,
                                 const tide_float2 from, const tide_float2 to)
{
    scene_bounds bounds = {0};
    if (count < 1 || count > 8 || !real2(from) || !real2(to) || !real(radius) || radius < 0) return bounds;
    bounds.min = bounds.max = scene_float(points[0]);
    for (int i = 1; i < count; i++) { if (points[i].x < bounds.min.x) bounds.min.x = points[i].x; if (points[i].x > bounds.max.x) bounds.max.x = points[i].x; }
    bounds.min.x += (from.x < to.x ? from.x : to.x) - radius;
    bounds.max.x += (from.x > to.x ? from.x : to.x) + radius;
    for (int i = 1; i < count; i++) { if (points[i].y < bounds.min.y) bounds.min.y = points[i].y; if (points[i].y > bounds.max.y) bounds.max.y = points[i].y; }
    bounds.min.y += (from.y < to.y ? from.y : to.y) - radius;
    bounds.max.y += (from.y > to.y ? from.y : to.y) + radius;
    bounds.valid = real2(bounds.min) && real2(bounds.max);
    return bounds;
}
scene_bounds b2t_collider_bounds(const collider *c)
{
    if (!scene_real(c)) return (scene_bounds){0};
    b2Vec2 points[8];
    const b2ShapeProxy proxy = scene_proxy(c, points);
    return proxy_bounds(points, proxy.count, proxy.radius, c->position, c->position);
}
scene_bounds b2t_box_bounds(const box *shape, const float rotation, const tide_float2 from, const tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!box_proxy(shape, rotation, &proxy)) return (scene_bounds){0};
    return proxy_bounds(proxy.points, proxy.count, proxy.radius, from, to);
}

scene_bounds b2t_circle_bounds(const circle *shape, const float rotation, const tide_float2 from, const tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!circle_proxy(shape, rotation, &proxy)) return (scene_bounds){0};
    return proxy_bounds(proxy.points, proxy.count, proxy.radius, from, to);
}

#include "joints.inc"

// A copy of a body another shard owns near this one's border (a ghost's, in
// a sharded world): kinematic, or static for a static one, where the body's
// owner last had it and as fast. Made when `id` is 0, and moved when it
// isn't; its id, or 0 when it's gone. A copy at rest is left as it is.
int64_t b2t_copy(const int64_t id, const body *b)
{
    world *w = active;
    const b2BodyType type = type_of(b->type); // A dynamic one as heavy as the body (b3t_copy says why)
    if (id == 0) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = type;
        def.position = b2ToPos((b2Vec2){b->position.x, b->position.y});
        def.rotation = b2MakeRot(b->angle);
        if (type != b2_staticBody) {
            def.linearVelocity = (b2Vec2){b->velocity.x, b->velocity.y};
            def.angularVelocity = b->spin;
        }
        if (type == b2_dynamicBody) {
            def.gravityScale = b->gravity_scale;
            def.linearDamping = b->linear_damping;
            def.angularDamping = b->angular_damping;
            def.motionLocks.angularZ = b->fixed_rotation;
        }
        const b2BodyId made = b2CreateBody(w->id, &def);
        const int64_t copy = handle(made.index1, made.generation);
        if (copy != 0) know(w, made.index1, (uint8_t)(type + 1));
        note('c', &copy, sizeof copy);
        note('c', b, sizeof *b);
        return copy;
    }
    const b2BodyId at = body_of(w, id);
    if (!b2Body_IsValid(at)) return 0;
    const b2Pos was = b2Body_GetPosition(at);
    if ((float)was.x != b->position.x || (float)was.y != b->position.y || b2Rot_GetAngle(b2Body_GetRotation(at)) != b->angle)
        b2Body_SetTransform(at, b2ToPos((b2Vec2){b->position.x, b->position.y}), b2MakeRot(b->angle));
    if (type != b2_staticBody) {
        const b2Vec2 v = b2Body_GetLinearVelocity(at);
        if (v.x != b->velocity.x || v.y != b->velocity.y) b2Body_SetLinearVelocity(at, (b2Vec2){b->velocity.x, b->velocity.y});
        if (b2Body_GetAngularVelocity(at) != b->spin) b2Body_SetAngularVelocity(at, b->spin);
    }
    note('m', &id, sizeof id);
    note('m', b, sizeof *b);
    return id;
}

void b2t_copy_box(const int64_t on, const box *s)
{
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !(s->size.x > 0) || !(s->size.y > 0)) return;
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Polygon shape = box_shape(s);
    b2CreatePolygonShape(body_id, &def, &shape);
    note('x', &on, sizeof on);
    note('x', s, sizeof *s);
}

void b2t_copy_circle(const int64_t on, const circle *s)
{
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !(s->radius > 0)) return;
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Circle shape = {{s->offset.x, s->offset.y}, s->radius};
    b2CreateCircleShape(body_id, &def, &shape);
    note('o', &on, sizeof on);
    note('o', s, sizeof *s);
}

// The box a body's shapes cover, in the world (Span): zeros for one that's gone
typedef struct span {
    tide_float2 lo;
    tide_float2 hi;
} span;

span b2t_span(const int64_t id)
{
    span out = {{0}, {0}};
    const b2BodyId at = body_of(active, id);
    if (!b2Body_IsValid(at)) return out;
    const b2AABB box = b2Body_ComputeAABB(at);
    out.lo = (tide_float2){box.lowerBound.x, box.lowerBound.y};
    out.hi = (tide_float2){box.upperBound.x, box.upperBound.y};
    return out;
}

void b2t_drop_copy(const int64_t id)
{
    const b2BodyId at = body_of(active, id);
    if (!b2Body_IsValid(at)) return;
    b2DestroyBody(at);
    know(active, at.index1, 0);
    note('d', &id, sizeof id);
}

#include "colliders.inc"

// Only cached copies of native settings live here. Every restore invalidates
// them, and the first comparison reads the restored native values.
void b2t_configure_bodies(tide_rows *rows, physics *p)
{
    (void)p;
    const uint32_t count = tide_rows_count(rows);
    const float *gravity = tide_rows_floats(rows, 0, offsetof(body, gravity_scale));
    const float *linear = tide_rows_floats(rows, 0, offsetof(body, linear_damping));
    const float *angular = tide_rows_floats(rows, 0, offsetof(body, angular_damping));
    const float *sleep = tide_rows_floats(rows, 0, offsetof(body, sleep_speed));
    body_fields f = fields_of(rows);
    for (uint32_t i = 0; i < count; i++) {
        body_controls desired = {0};
        desired.id = (int64_t)((uint64_t)(uint32_t)f.id[i] | (uint64_t)(uint32_t)f.id_high[i] << 32);
        desired.gravity_scale = geometry_float(gravity[i]) ? gravity[i] : 1;
        desired.linear_damping = geometry_float(linear[i]) && linear[i] >= 0 ? linear[i] : 0;
        desired.angular_damping = geometry_float(angular[i]) && angular[i] >= 0 ? angular[i] : 0;
        desired.sleep_speed = geometry_float(sleep[i]) && sleep[i] >= 0 ? sleep[i] : 0;
        desired.fixed_rotation = (f.flags[i] & 0xff) != 0;
        desired.bullet = ((uint32_t)f.flags[i] >> 8 & 0xff) != 0;
        const uint32_t at = (uint32_t)desired.id;
        if (at < active->controls_room && memcmp(&active->controls[at], &desired, sizeof desired) == 0) continue;
        const b2BodyId id = body_of(active, desired.id);
        if (!b2Body_IsValid(id)) continue;
        if (at >= active->controls_room) {
            uint32_t room = active->controls_room ? active->controls_room : 64;
            while (room <= at) {
                if (room > (1u << 30) / (2 * sizeof(body_controls))) tide_out_of_memory();
                room *= 2;
            }
            active->controls = tide_realloc(active->controls, (size_t)active->controls_room * sizeof *active->controls,
                                           (size_t)room * sizeof *active->controls);
            memset(active->controls + active->controls_room, 0, (size_t)(room - active->controls_room) * sizeof *active->controls);
            active->controls_room = room;
        }
        bool changed = false;
        if (b2Body_GetGravityScale(id) != desired.gravity_scale) { b2Body_SetGravityScale(id, desired.gravity_scale); changed = true; }
        if (b2Body_GetLinearDamping(id) != desired.linear_damping) { b2Body_SetLinearDamping(id, desired.linear_damping); changed = true; }
        if (b2Body_GetAngularDamping(id) != desired.angular_damping) { b2Body_SetAngularDamping(id, desired.angular_damping); changed = true; }
        if (b2Body_GetSleepThreshold(id) != desired.sleep_speed) { b2Body_SetSleepThreshold(id, desired.sleep_speed); changed = true; }
        if (b2Body_IsBullet(id) != desired.bullet) { b2Body_SetBullet(id, desired.bullet); changed = true; }
        b2MotionLocks locks = b2Body_GetMotionLocks(id);
        if (locks.angularZ != desired.fixed_rotation) { locks.angularZ = desired.fixed_rotation; b2Body_SetMotionLocks(id, locks); changed = true; }
        if (changed) {
            b2Body_SetAwake(id, true);
            note('U', &desired.id, sizeof desired.id);
            note('U', &desired.gravity_scale, 4 * sizeof(float));
            note('U', &desired.fixed_rotation, sizeof desired.fixed_rotation);
            note('U', &desired.bullet, sizeof desired.bullet);
        }
        active->controls[at] = desired;
    }
}

// More shapes on a copy of another shard's body (CopyBodies, CopyParts in a
// sharded world), made as the systems that keep a body's shapes make them
void b2t_copy_capsule(const int64_t on, const capsule *s)
{
    const fixture none = {0};
    (void)b2t_sync_capsule(&none, on, s, NULL, false);
}

void b2t_copy_polygon(const int64_t on, const polygon_shape *s)
{
    const fixture none = {0};
    (void)b2t_sync_polygon(&none, on, s, NULL, false);
}

void b2t_copy_segment(const int64_t on, const segment_shape *s)
{
    const fixture none = {0};
    (void)b2t_sync_segment(&none, on, s, NULL, false);
}

void b2t_copy_chain(const int64_t on, const tide_float2 *points, const int32_t count, const float friction, const float bounce,
                    const bool loop)
{
    const fixture none = {0};
    (void)b2t_sync_chain(&none, on, points, count, friction, bounce, loop, NULL, false);
}
