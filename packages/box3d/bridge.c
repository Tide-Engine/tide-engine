// The C side of box3d.tide: Box3D's worlds, and what the package's systems ask
// of them.
//
// A match keeps Box3D's state as resident pages (Physics.state). C steps it
// in a fixed-address arena, commits touched pages after each step, and brings
// another world's pages back by identity before stepping or querying it.
// Network joins and layout migrations use a canonical image and geometry;
// ordinary ticks and rollbacks copy pages without serializing the world.
//
// A stamp is the stamp before it and everything that was done to the world
// since, hashed: Box3D's next state is its last and what's done to it, and
// nothing else. So it costs what was done, and the arena pages are never read
// to name it.
//
// The operation stamp and the library's deterministic digest are hashed.
// Arena bytes contain pointers and allocator order and are never hashed or
// sent.

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
#include "tide/resident.h"
#include "tide/rows.h"
#include "tide/shard.h"
#include "tide/time.h"

// Box3D's headers are its own, as its sources are: what they'd be warned for
// (a compiler's own spelling of "inline this") isn't this file's to fix.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "box3d/src/box3d/box3d.h"
#include "box3d/src/shape.h"
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
    bool rewind;
    bool settings_applied;
    uint8_t unused_too[2];
    tide_entity held_by; // The joint it goes with, in a sharded world
} body;

typedef struct box {
    tide_float3 size;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
    uint8_t cache_padding[3];
    int32_t cache;
} box;

typedef struct sphere {
    float radius;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
    uint8_t cache_padding[3];
    int32_t cache;
} sphere;

typedef struct capsule {
    float radius;
    float height;
    tide_float3 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
    uint8_t cache_padding[3];
    int32_t cache;
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
    tide_resident state; // Box3D's state, as pages of the world's (tide/resident.h)
    bool moved;
    uint8_t unused_too[3];
    tide_list gone;
    tide_map owners;
    tide_list gone_shapes;
    uint8_t unused_maps[4];
    tide_map parts;
    tide_map sensors;
    tide_map joints;
    float history_dt;
    float history_limit;
    int32_t history_tick;
    uint8_t history_padding[4];
    tide_map selected;
    tide_list pending;
    tide_list history;
    tide_list colliders;
    uint8_t collider_padding[4];
    tide_map collider_index;
    tide_list free_colliders;
    tide_list pending_hulls;
    tide_list topology;
    bool topology_changed;
    uint8_t topology_padding[3];
    int64_t shard_moves; // Shards.moves as the last tick had it
    tide_map copies;     // Other shards' bodies near this one's border, by entity...
    tide_map copy_of;    // ...and whose each copy is
    tide_map copied_parts; // ...and the parts whose shapes are on one
} physics;

_Static_assert(sizeof(body) == 184 && sizeof(box) == 44 && sizeof(sphere) == 36 && sizeof(capsule) == 40 && sizeof(touch) == 40
                   && sizeof(strike) == 48 && sizeof(found) == 48 && sizeof(physics) == 376 && offsetof(physics, moved) == 88
                   && offsetof(physics, shard_moves) == 296 && offsetof(physics, copies) == 304,
               "box3d.tide's types changed: these are theirs");

_Static_assert(offsetof(physics, gravity) == 0 && offsetof(physics, substeps) == 12
                   && offsetof(physics, contact_stiffness) == 16 && offsetof(physics, contact_damping) == 20
                   && offsetof(physics, contact_speed) == 24 && offsetof(physics, bounce_speed) == 28
                   && offsetof(physics, hit_speed) == 32 && offsetof(physics, sleeping) == 36
                   && offsetof(physics, bodies) == 40 && offsetof(physics, shapes) == 44 && offsetof(physics, stamp) == 48
                   && offsetof(physics, state) == 56 && offsetof(physics, moved) == 88 && offsetof(physics, gone) == 92
                   && offsetof(physics, owners) == 96 && offsetof(physics, gone_shapes) == 120
                   && offsetof(physics, parts) == 128 && offsetof(physics, sensors) == 152,
               "Physics's fields aren't where box3d.tide lays them out any more");

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

_Static_assert(offsetof(body, step_position) - SET == SETTINGS * sizeof(float) && offsetof(body, held_by) - SET_STEP == (SETTINGS + 1) * sizeof(float)
                   && offsetof(body, step_bullet) == FLAGS + 1 && offsetof(body, asleep) == FLAGS + 2
                   && offsetof(body, step_asleep) == FLAGS + 3 && FLAGS % 4 == 0 && offsetof(body, step_type) == FLAGS + 4,
               "a Body's settings, and what Box3D has of them, aren't where the systems written in C look for them any more");

// A step with fewer bodies awake than this runs on its own thread: sharing it
// out would cost more than it saves. The step itself is quicker shared from a
// couple of hundred bodies on, but a tick whose C starts jobs runs its systems
// on threads too (tide/jobs.h), and that's only made up for from about here.
#define SHARED_BODIES 512

// C's world: one, whose state lives in the engine's arena for this kind of
// resident state (tide/resident.h). A tick's world is the state the match's
// Physics.state names, restored into the arena when the arena holds another
// (b3t_open); after the step, the pages the step touched are committed into
// it (b3t_step). A question of another state restores that state the same way
// (b3t_rebuild), and the next tick restores its own. The arena outlives a
// build of the game's library: a build that takes it over finds the world's
// struct in it (user bytes) and links it to its own scratch and callbacks
// (relink).
typedef struct world {
    b3WorldId id;
    bool made;
    int workers;
    int64_t stamp;  // The state it's at: 0 for an empty one nothing stepped
    uint64_t chain; // The stamp, and what the running tick did to the world since
} world;

// What the arena's user bytes keep for a build that takes it over
typedef struct arena_user {
    uint32_t magic;
    int32_t world_id;      // Which slot of Box3D's the world's struct is in
    int32_t workers;
    uint32_t struct_offset; // The struct's copy, from the user bytes
} arena_user;

#define ARENA_MAGIC 0x33444242u // "BB3D" hmm: BOX3D's own
#define RESIDENT_KIND "Box3D"

static world live;
static world *active; // The running tick's: &live, or NULL before anything opened it
static tide_arena *arena;
static uint64_t kind;
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
static bool measuring; // No clock is read otherwise
static bool checking;  // Every committed resident page is compared with the live arena
static bool always;    // ...and sparse commits are tried whatever is awake
static bool wholly;    // Every arena page is marked, though the commit could be sparse
static int32_t images_whole;
static uint64_t digests; // A running hash of every digest committed (b3t_image_hash)
static int32_t images_in_parts;
static uint64_t step_ns;
static uint64_t image_ns; // Resident restore and commit time; the measurement API keeps its old name
static uint64_t blocks_changed; // Arena pages marked as touched in measured commits, even if their bytes stayed the same
static uint64_t blocks_in_all; // Arena pages in use at those commits
static uint64_t counting_ns; // What the optional full-arena check took, which no match pays

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

// Everything Box3D keeps between steps comes from the arena (tide/resident.h);
// its scratch is the system's (b3AllocScratch). The alignment Box3D asks for
// is 32, which the arena's least, 64, has.
static void *allocate(const size_t size, const int32_t alignment)
{
    (void)alignment;
    return tide_arena_alloc(arena, size);
}

static void release(void *memory, const size_t size)
{
    // The array may have been changed before it grew or was destroyed. The
    // touch pass only sees its replacement; snapshots still need these bytes
    // restored when their pages are brought back.
    tide_arena_touch(arena, memory, size);
    tide_arena_free(arena, memory, size);
}

static void touched(void *context, const void *memory, const size_t size)
{
    (void)context;
    tide_arena_touch(arena, memory, size);
}

static void touched_all(void *context)
{
    (void)context;
    tide_arena_touch_all(arena);
}

static void pack_state(void *context, const tide_resident *r, tide_writer *w);
static bool unpack_state(void *context, tide_resident *r, tide_reader *rd, uint32_t size);
static void relink_arena(void *context, tide_arena *a);
static void commit(tide_resident *state);

// A fresh joining process unpacks a world before it runs any tick. Its
// reader must know the kind then, as must a newly loaded game's library.
__attribute__((constructor)) static void register_state(void)
{
    const tide_resident_kind k = {.name = RESIDENT_KIND, .pack = pack_state, .unpack = unpack_state, .relink = relink_arena};
    kind = tide_resident_register(&k);
}

// The arena is taken the first time anything needs it.
static void ready(void)
{
    if (arena) return;
    if (!kind) register_state();
    b3SetAllocator(allocate, release);
    b3SetTouchHooks(touched, touched_all, NULL);
    arena = tide_resident_arena(kind); // Which relinks a world another build left in it
}

// As many workers as the tick has threads: a world steps the same with any.
static int workers_now(void)
{
    const uint32_t threads = tide_job_threads();
    return threads > B3_MAX_WORKERS ? B3_MAX_WORKERS : (int)threads;
}

// The world's struct is Box3D's, outside the arena: a commit copies it into
// the arena's block for it, and a restore copies it back (b3World_StructSave,
// b3World_StructLoad) and links it to this build's scratch and callbacks.
static void save_struct(void)
{
    arena_user *u = tide_arena_user(arena);
    if (!u->struct_offset) {
        void *block = tide_arena_alloc(arena, (size_t)b3World_StructSize());
        u->struct_offset = (uint32_t)((uint8_t *)block - (uint8_t *)u);
    }
    u->magic = ARENA_MAGIC;
    u->world_id = live.id.index1 - 1;
    u->workers = live.workers;
    void *block = (uint8_t *)u + u->struct_offset;
    b3World_StructSave(live.id, block);
    tide_arena_touch(arena, block, (size_t)b3World_StructSize());
}

static void load_struct(void)
{
    const arena_user *u = tide_arena_user_read(arena);
    if (u->magic != ARENA_MAGIC || !u->struct_offset) {
        fprintf(stderr, "Box3D: the arena holds no world to take over\n");
        abort();
    }
    live.id = b3World_StructLoad(u->world_id, (const uint8_t *)u + u->struct_offset);
    live.made = true;
    live.workers = u->workers;
    b3World_Relink(live.id, enqueue, finish, NULL);
    if (narrow) b3World_EnableSSE2Fallback(live.id, true);
    if (always) b3World_SetSnapshotAwakeShare(live.id, 1);
}

// A fresh world in an empty arena
static void make(const int workers)
{
    ready();
    b3WorldDef def = b3DefaultWorldDef();
    def.workerCount = (uint32_t)workers;
    def.enqueueTask = enqueue;
    def.finishTask = finish;
    live.id = b3CreateWorld(&def);
    if (narrow) b3World_EnableSSE2Fallback(live.id, true);
    if (always) b3World_SetSnapshotAwakeShare(live.id, 1);
    live.made = true;
    live.workers = workers;
    live.stamp = 0;
    live.chain = 0;
}

static void unmake(void)
{
    if (live.made) b3DestroyWorld(live.id);
    live.made = false;
    live.stamp = 0;
    live.chain = 0;
}

// The arena holds the state `r` names, with the world's struct and scratch to
// go with it. An `r` with no pages is a match that starts: an empty world.
static void restore_state(const tide_resident *r)
{
    ready();
    // By the pages' identity, never by the stamp: two runs of one state have other bytes in the arena
    if (live.made && r->pages && tide_arena_is(arena, r)) return;
    if (live.made && !r->pages && r->stamp == 0 && live.stamp == 0 && tide_arena_pages(arena) > 1) {
        // An empty match's world with nothing committed yet is the one there is: a match's first tick
        return;
    }
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    if (!r->pages) {
        if (live.made) unmake();
        tide_resident none = {0};
        tide_arena_restore(arena, &none); // Empty: nothing the world handed out is anyone's
        make(workers_now());
    } else {
        tide_arena_restore(arena, r);
        restores++;
        load_struct();
        live.stamp = (int64_t)tide_arena_stamp(arena);
        live.chain = (uint64_t)live.stamp;
        if (live.workers != workers_now()) { // Another count of threads than the state was stepped with: it steps the same
            b3World_SetWorkerCount(live.id, workers_now());
            live.workers = workers_now();
        }
    }
    if (measuring) image_ns += tide_time_now_ns() - began;
}

static b3BodyId body_of(const world *w, const int64_t id)
{
    return (b3BodyId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

static b3ShapeId shape_of(const world *w, const int64_t id)
{
    return (b3ShapeId){(int32_t)(uint32_t)id, (uint16_t)(w->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

// A body's or a shape's handle without its world, which is C's live world
// after the match's resident state has been restored into it.
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

bool b3t_open(tide_resident *state)
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
    restore_state(state);
    live.chain = (uint64_t)live.stamp;
    active = &live;
    return true;
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

// A copy of a body another shard owns near this one's border (a ghost's, in
// a sharded world): kinematic, or static for a static one, where the body's
// owner last had it and as fast. Made when `id` is 0, and moved when it
// isn't; its id, or 0 when it's gone. A copy at rest is left as it is, so
// what rests against it can sleep.
int64_t b3t_copy(const int64_t id, const body *b)
{
    world *w = active;
    const b3BodyType type = b->type == b3_staticBody ? b3_staticBody : b3_kinematicBody;
    if (id == 0) {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = type;
        def.position = b3ToPos(vec(b->position));
        def.rotation = quat(b->rotation);
        if (type == b3_kinematicBody) {
            def.linearVelocity = vec(b->velocity);
            def.angularVelocity = vec(b->angular_velocity);
        }
        const b3BodyId made = b3CreateBody(w->id, &def);
        const int64_t copy = handle(made.index1, made.generation);
        note('c', &copy, sizeof copy);
        note('c', b, sizeof *b);
        return copy;
    }
    const b3BodyId at = body_of(w, id);
    if (!b3Body_IsValid(at)) return 0;
    const b3Pos was = b3Body_GetPosition(at);
    const b3Pos now = b3ToPos(vec(b->position));
    const b3Quat turned = b3Body_GetRotation(at), turn = quat(b->rotation);
    if (memcmp(&was, &now, sizeof now) != 0 || memcmp(&turned, &turn, sizeof turn) != 0) b3Body_SetTransform(at, now, turn);
    if (type == b3_kinematicBody) {
        const b3Vec3 v = b3Body_GetLinearVelocity(at), want = vec(b->velocity);
        if (memcmp(&v, &want, sizeof v) != 0) b3Body_SetLinearVelocity(at, want);
        b3Body_SetAngularVelocity(at, vec(b->angular_velocity));
    }
    note('m', &id, sizeof id);
    note('m', b, sizeof *b);
    return id;
}

void b3t_drop_copy(const int64_t id)
{
    const b3BodyId at = body_of(active, id);
    if (!b3Body_IsValid(at)) return;
    b3DestroyBody(at);
    note('d', &id, sizeof id);
}

// For a host, and for tests: C's worlds go, as when a program starts. Matches
// lose nothing, as their state is their own.
void b3t_forget(void)
{
    if (arena) {
        unmake();
        tide_resident none = {0};
        tide_arena_restore(arena, &none);
    }
    active = NULL;
    made_count = made_taken = 0;
    sleep_count = 0;
    slept = false;
    synced_count = 0;
    parted_count = 0;
    touch_count = 0;
    retyped = false;
    digests = 0;
}

// For tests: the worlds made from here on solve 4 contacts at a time, as a CPU
// without AVX2 does, where this one would solve 8. They step the same world.
void b3t_narrow(const bool on)
{
    narrow = on;
}

// For tests: every committed resident page is compared with the live arena.
// One byte apart ends the program, saying where Box3D wrote without a mark.
// With `whenever`, worlds made from here on try sparse marks even when most
// bodies are awake, where they'd normally mark the whole arena: more for the
// check to see.
void b3t_check(const bool on, const bool whenever)
{
    checking = on;
    always = on && whenever;
}

// For tests: the arena is marked whole where its commits could be sparse.
// The match's resident state has to be the same either way, which a match
// played both ways says.
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

// How many resident commits marked the whole arena, and how many used sparse
// marks, since the program started: tests read it to see they test both.
void b3t_images(int32_t *whole, int32_t *in_parts)
{
    *whole = images_whole;
    *in_parts = images_in_parts;
}

// For tests: a hash of every digest the ticks' worlds committed, in order,
// since the program started or b3t_forget: what Box3D's state was at each
// tick, apart from everything else a match holds. 0 before any.
uint64_t b3t_image_hash(void)
{
    return digests ? tide_hash_end(digests) : 0;
}

// How many steps were shared out over threads, and how many times another
// resident state was restored or received: what tests and benchmarks read.
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
// resident restores and commits (including marks and digest). Nanoseconds
// since the last call; the first starts measuring. The second output keeps
// its old image name.
void b3t_measure(uint64_t *step, uint64_t *image)
{
    measuring = true;
    *step = step_ns;
    *image = image_ns;
    step_ns = 0;
    image_ns = 0;
}

// Arena pages touched by commits since the last call, and the pages in use
// at those commits. A touched page is copied even if its bytes stayed the
// same. `took` is the optional full-arena check's nanoseconds, to take off a
// measured tick; no match pays it.
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
// with. The snapshot tracking follows it (box3d/TIDE.md): what wakes is
// marked, and untouched resident pages stay shared.
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
        // moved takes none. It's the one call here the sparse tracking doesn't follow (box3d/TIDE.md), so the
        // next resident commit marks the whole arena. Box3D wakes the body, lets go of everything it touches, and finds
        // what still does in the step
        if (b->type != b->step_type) {
            // Mesh contacts require a static body. Keep the component and native
            // body in agreement if game code tries to make one move by type.
            if (b->type != b3_staticBody && b->step_type == b3_staticBody) {
                const int count = b3t_body_shapes(b->id);
                for (int i = 0; i < count; i++) {
                    if ((b3Shape_GetType(listed[i]) == b3_meshShape || b3Shape_GetType(listed[i]) == b3_heightShape)) { b->type = b3_staticBody; break; }
                }
            }
        }
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

static bool finite3(tide_float3 v);
static bool finite1(float x);

// The native row helper flattens the lists. Bounds and arithmetic are checked
// before Box3D sees arbitrary game data. Cook once; the world owns the result.
static int64_t attach_mesh(const int64_t on, const tide_float3 *vertices, const int32_t vertex_count,
                 const int32_t *indices, const int32_t index_count, const tide_float3 scale,
                 const tide_float3 offset, const float friction, const float bounce)
{
    if (!active || on == 0) return 0;
    const b3BodyId owner = body_of(active, on);
    if (!b3Body_IsValid(owner) || b3Body_GetType(owner) != b3_staticBody) return 0;
    // Bounds keep the cooker's signed counts/byte offsets and temporary arrays
    // comfortably within range, including on wasm32 and the resident arena.
    if (!vertices || !indices || vertex_count < 3 || vertex_count > 65536 ||
        index_count < 3 || index_count > 3 * 65536 || index_count % 3 != 0 ||
        !finite3(scale) || !finite3(offset) || scale.x == 0 || scale.y == 0 || scale.z == 0 ||
        !finite1(friction) || friction < 0 || !finite1(bounce) || bounce < 0 || bounce > 1) return 0;
    for (int i = 0; i < index_count; i++) if (indices[i] < 0 || indices[i] >= vertex_count) return 0;
    b3Vec3 *points = malloc((size_t)vertex_count * sizeof *points);
    if (!points) return 0;
    for (int i = 0; i < vertex_count; i++) {
        points[i] = (b3Vec3){vertices[i].x * scale.x + offset.x, vertices[i].y * scale.y + offset.y,
                             vertices[i].z * scale.z + offset.z};
        const b3Vec3 p = points[i];
        // Finite bounded coordinates also prevent overflowing the cooker's
        // cross products, squared areas and BVH cost calculations.
        if (!finite3(float3(p)) || p.x < -1e6f || p.x > 1e6f || p.y < -1e6f || p.y > 1e6f || p.z < -1e6f || p.z > 1e6f) {
            free(points); return 0;
        }
    }
    const b3MeshDef mesh_def = {.vertices = points, .indices = (int32_t *)(uintptr_t)indices,
        .vertexCount = vertex_count, .triangleCount = index_count / 3, .identifyEdges = true,
        .clockWiseWinding = (scale.x < 0) ^ (scale.y < 0) ^ (scale.z < 0)};
    b3MeshData *cooked = b3CreateMesh(&mesh_def, NULL, 0);
    free(points);
    if (!cooked) return 0;
    const b3MeshData *kept = b3World_InternMesh(active->id, cooked);
    b3DestroyMesh(cooked);
    const b3ShapeDef def = shape_def(0, friction, bounce, false);
    const b3ShapeId made = b3CreateMeshShape(owner, &def, kept, b3Vec3_one);
    if (made.index1 == 0) return 0;
    note('m', &on, sizeof on);
    note('m', &kept->byteCount, sizeof kept->byteCount);
    note('m', kept, (size_t)kept->byteCount);
    note('m', &friction, sizeof friction);
    note('m', &bounce, sizeof bounce);
    return handle(made.index1, made.generation);
}

// These C systems read real list handles and derive their lengths here, so
// Tide code never supplies a raw buffer length to the trusted mesh cooker.
typedef struct mesh_collider {
    tide_list vertices, indices;
    tide_float3 scale, offset;
    float friction, bounce;
    int32_t cache;
} mesh_collider;
typedef struct part { tide_entity of; int64_t box, sphere, capsule, mesh, hull, height_field, body; } part;
_Static_assert(sizeof(mesh_collider) == 44 && sizeof(part) == 64, "mesh component ABI changed");

static int64_t attach_mesh_component(const int64_t on, const mesh_collider *m)
{
    const int32_t vertices = tide_list_count(m->vertices), indices = tide_list_count(m->indices);
    if (vertices < 3 || vertices > 65536 || indices < 3 || indices > 3 * 65536) return 0;
    bool copied_vertices = false, copied_indices = false;
    tide_float3 *points = tide_list_flatten(m->vertices, sizeof(tide_float3), false, &copied_vertices);
    int32_t *triangles = tide_list_flatten(m->indices, sizeof(int32_t), false, &copied_indices);
    const int64_t shape = attach_mesh(on, points, vertices, triangles, indices, m->scale, m->offset, m->friction, m->bounce);
    tide_list_unflatten(m->vertices, points, sizeof(tide_float3), false, copied_vertices);
    tide_list_unflatten(m->indices, triangles, sizeof(int32_t), false, copied_indices);
    return shape;
}


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
// calls the snapshot tracking follows, as the ones Enter makes are.
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
// noted here. It's a call the snapshot tracking follows. Whether any was.
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

int64_t b3t_step(tide_resident *state, const float dt, const int32_t substeps, const tide_float3 gravity, const float contact_stiffness,
                 const float contact_damping, const float contact_speed, const float bounce_speed, const float hit_speed,
                 const bool sleeping)
{
    world *w = active;
    // Gravity that isn't what the world's last step had: the world's own is in its resident state, so it's the
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
    // The world's settings are the match's, every step: each is saved with the world's struct,
    // and the snapshot tracking follows these calls (box3d/TIDE.md)
    b3World_SetGravity(w->id, vec(gravity));
    b3World_SetContactTuning(w->id, contact_stiffness, contact_damping, contact_speed);
    b3World_SetRestitutionThreshold(w->id, bounce_speed);
    b3World_SetHitEventThreshold(w->id, hit_speed);
    // Everything is awake to feel a gravity that changed: a body at rest would stay where the old one
    // left it. Box3D wakes every body when sleeping is turned off, and has no other call for it. It
    // follows from the gravity, which every step notes. The sparse tracking doesn't follow turning sleeping
    // off or on, so the next resident commit marks the whole arena, as with everything awake; one
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
    commit(state);
    return w->stamp;
}

// The state after the step into the match: every page the step touched, by
// Box3D's marks (b3World_TouchChanged) and the world's struct, copied into the
// match's resident (tide_arena_commit), with the stamp and a digest of what
// the match agrees on. A world that had something done to it that isn't
// followed, or most of whose bodies are awake, has every page copied.
static void commit(tide_resident *state)
{
    world *w = active;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    if (wholly) tide_arena_touch_all(arena);
    const bool everything = b3World_TouchChanged(w->id) || wholly;
    save_struct();
    const uint64_t digest = b3World_Digest(w->id);
    const uint32_t before = tide_arena_pages(arena);
    const uint32_t changed = tide_arena_touched(arena);
    tide_arena_commit(arena, state, (uint64_t)w->stamp, digest);
    digests = tide_hash_more(digests ? digests : TIDE_HASH_START, &digest, sizeof digest);
    if (everything) images_whole++; // The arena was marked whole; the counters keep their old image names
    else images_in_parts++;
    if (measuring) {
        blocks_changed += changed;
        blocks_in_all += before;
        image_ns += tide_time_now_ns() - began;
    }
    if (checking) {
        const uint64_t counting = measuring ? tide_time_now_ns() : 0;
        const int64_t page = tide_arena_differs(arena, state);
        if (page >= 0) {
            char what[128];
            const uint8_t *now = tide_arena_page_at(arena, (uint32_t)page);
            const uint8_t *was = tide_page_data(state->pages->page[page]);
            uint32_t offset = 0;
            while (offset < TIDE_ARENA_PAGE && now[offset] == was[offset]) offset++;
            b3World_Describe(w->id, now + offset, what, sizeof what);
            fprintf(stderr, "Box3D wrote page %lld of its arena without saying so: byte %u is in %s\n", (long long)page, offset, what);
            abort();
        }
        if (measuring) counting_ns += tide_time_now_ns() - counting;
    }
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
// step's own, read in the tick that stepped, and a world restored from its
// resident has the same ones to say after its next step: each sensor's state
// is kept with it. Asking changes nothing of the world. How many there
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
// Each is asked of the state a stamp names, when C's live world is at it
// (at), and none of them does anything to that world: Box3D's own queries
// write nothing, so its resident state stays as it was.
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
// here keeps anything between calls but the live world, and another resident
// state is restored (b3t_rebuild) one thread at a time.

static uint32_t rebuilding; // A lock: 1 while a thread restores a world for a question

// C's world at this state, or NULL.
static world *at(const int64_t stamp)
{
    return live.made && live.stamp == stamp ? &live : NULL;
}

// Whether C has a world at this state to ask. A match nothing stepped has no
// state yet, and nothing to find in it.
bool b3t_find(const int64_t stamp)
{
    return stamp != 0 && at(stamp) != NULL;
}

// The world at this state, restored from the match's pages of it, for a
// question asked where C's world is at another: a view of a tick this machine
// ran a while ago, or a system that runs ahead of the step in a tick that goes
// back to an older state. The next tick restores its own. One thread at a
// time; a question that finds the world there asks it as it is.
void b3t_rebuild(const tide_resident *state)
{
    if (state->stamp == 0 || !state->pages) return;
    while (__atomic_exchange_n(&rebuilding, 1u, __ATOMIC_ACQUIRE)) {
    }
    if (!at(state->stamp)) { // No other thread restored it meanwhile
        restore_state(state);
        rebuilds++;
    }
    __atomic_store_n(&rebuilding, 0u, __ATOMIC_RELEASE);
}

// A question, and its answer so far.
typedef struct asked {
    int64_t ignore;
    bool include_sensors;
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
    if (!a->include_sensors && b3Shape_IsSensor(shape)) return true;
    if (a->gone_shape_count > 0) {
        const int64_t named = handle(shape.index1, shape.generation);
        for (int32_t i = 0; i < a->gone_shape_count; i++) {
            if (a->gone_shapes[i] == named) return true;
        }
    }
    if (a->ignore || a->gone_count > 0) {
        const b3BodyId on = b3Shape_GetBody(shape);
        const int64_t named = handle(on.index1, on.generation);
        if (named == a->ignore) return true;
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
              const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t ignore, const int64_t query_category, const int64_t query_mask)
{
    asked a = {.ignore = ignore, .gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes,
               .gone_shape_count = gone_shape_count, .ray = true};
    const world *w = at(stamp);
    if (!w || !finite3(from) || !finite3(to)) return answer(w, &a);
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return answer(w, &a);
    b3World_CastRay(w->id, b3ToPos(vec(from)), along, (b3QueryFilter){.categoryBits=(uint64_t)query_category, .maskBits=(uint64_t)query_mask}, nearest, &a);
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
                  const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t ignore, const int64_t query_category, const int64_t query_mask)
{
    asked a = {.ignore = ignore, .gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    const world *w = at(stamp);
    if (!w || !p.good || !finite3(from) || !finite3(to)) return answer(w, &a);
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return answer(w, &a);
    p.proxy.points = p.points; // Where they are now: the probe came by value
    b3World_CastShape(w->id, b3ToPos(vec(from)), &p.proxy, along, (b3QueryFilter){.categoryBits=(uint64_t)query_category, .maskBits=(uint64_t)query_mask}, nearest, &a);
    return answer(w, &a);
}

found b3t_cast_box(const int64_t stamp, const box *s, const tide_float3 from, const tide_quaternion rotation, const tide_float3 to,
                   const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count,
                   const int64_t ignore, const int64_t query_category, const int64_t query_mask)
{
    probe p = box_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, query_category, query_mask);
}

found b3t_cast_sphere(const int64_t stamp, const sphere *s, const tide_float3 from, const tide_quaternion rotation,
                      const tide_float3 to, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                      const int32_t gone_shape_count, const int64_t ignore, const int64_t query_category, const int64_t query_mask)
{
    probe p = sphere_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, query_category, query_mask);
}

found b3t_cast_capsule(const int64_t stamp, const capsule *s, const tide_float3 from, const tide_quaternion rotation,
                       const tide_float3 to, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                       const int32_t gone_shape_count, const int64_t ignore, const int64_t query_category, const int64_t query_mask)
{
    probe p = capsule_probe(s, rotation);
    p.good = p.good && sound(rotation);
    return cast(stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, query_category, query_mask);
}

// All-hit queries keep a small result on the stack and grow only for actual
// hits. The native query reports the first hit of each collider (a mesh too).
typedef struct cast_hits {
    asked filter;
    found *items;
    int32_t count, room;
    found few[32];
} cast_hits;

static float collect_cast_hit(const b3ShapeId shape, const b3Pos point, const b3Vec3 normal,
                              const float fraction, const uint64_t material, const int triangle, const int child, void *context)
{
    (void)material;
    (void)triangle;
    (void)child;
    cast_hits *hits = context;
    if ((hits->filter.ray && fraction == 0.0f) || passed(&hits->filter, shape)) return -1.0f;
    const b3BodyId on = b3Shape_GetBody(shape);
    const int64_t body = handle(on.index1, on.generation);
    const int64_t id = handle(shape.index1, shape.generation);
    const int32_t most = (int32_t)(UINT32_C(0x40000000) / sizeof *hits->items);
    if (hits->count == most) tide_out_of_memory();
    if (hits->count == hits->room) {
        const int32_t more = hits->room > most / 2 ? most : hits->room * 2;
        found *items = tide_alloc((size_t)more * sizeof *items);
        memcpy(items, hits->items, (size_t)hits->count * sizeof *items);
        if (hits->items != hits->few) free(hits->items);
        hits->items = items;
        hits->room = more;
    }
    found *hit = &hits->items[hits->count++];
    memset(hit, 0, sizeof *hit); // Output list padding is match state too.
    hit->body = body;
    hit->shape = id;
    hit->point = float3(b3ToVec3(point));
    hit->normal = float3(normal);
    hit->fraction = fraction;
    return 1.0f; // Keep the entire sweep: a nearer hit never clips the rest.
}

static int cast_hit_order(const void *a, const void *b)
{
    const found *x = a, *y = b;
    if (x->fraction != y->fraction) return x->fraction < y->fraction ? -1 : 1;
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

void b3t_ray_all(tide_list_out *out, const int64_t stamp, const tide_float3 from, const tide_float3 to,
                  const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count,
                  const int64_t ignore, const bool include_sensors, const int64_t query_category, const int64_t query_mask)
{
    tide_list_out_count(out, 0);
    const world *w = at(stamp);
    if (!w || !finite3(from) || !finite3(to)) return;
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return;
    cast_hits hits;
    hits.filter = (asked){.ignore = ignore, .include_sensors = include_sensors, .gone = gone, .gone_count = gone_count,
                          .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count, .ray = true};
    hits.count = 0;
    hits.room = 32;
    hits.items = hits.few;
    b3World_CastRay(w->id, b3ToPos(vec(from)), along, (b3QueryFilter){.categoryBits=(uint64_t)query_category, .maskBits=(uint64_t)query_mask}, collect_cast_hit, &hits);
    give_cast_hits(&hits, out);
}

static void cast_all(tide_list_out *out, const int64_t stamp, probe p, const tide_float3 from, const tide_float3 to,
                     const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count,
                     const int64_t ignore, const bool include_sensors, const int64_t query_category, const int64_t query_mask)
{
    tide_list_out_count(out, 0);
    const world *w = at(stamp);
    if (!w || !p.good || !finite3(from) || !finite3(to)) return;
    const b3Vec3 along = b3Sub(vec(to), vec(from));
    if (!finite3(float3(along))) return;
    cast_hits hits;
    hits.filter = (asked){.ignore = ignore, .include_sensors = include_sensors, .gone = gone, .gone_count = gone_count,
                          .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    hits.count = 0;
    hits.room = 32;
    hits.items = hits.few;
    p.proxy.points = p.points;
    b3World_CastShape(w->id, b3ToPos(vec(from)), &p.proxy, along, (b3QueryFilter){.categoryBits=(uint64_t)query_category, .maskBits=(uint64_t)query_mask}, collect_cast_hit, &hits);
    give_cast_hits(&hits, out);
}

void b3t_cast_box_all(tide_list_out *out, const int64_t stamp, const box *shape, const tide_float3 from,
                         const tide_quaternion rotation, const tide_float3 to, const int64_t *gone, const int32_t gone_count,
                         const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t ignore, const bool include_sensors, const int64_t query_category, const int64_t query_mask)
{
    probe p = box_probe(shape, rotation);
    p.good = p.good && sound(rotation);
    cast_all(out, stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, include_sensors, query_category, query_mask);
}

void b3t_cast_sphere_all(tide_list_out *out, const int64_t stamp, const sphere *shape, const tide_float3 from,
                         const tide_quaternion rotation, const tide_float3 to, const int64_t *gone, const int32_t gone_count,
                         const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t ignore, const bool include_sensors, const int64_t query_category, const int64_t query_mask)
{
    probe p = sphere_probe(shape, rotation);
    p.good = p.good && sound(rotation);
    cast_all(out, stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, include_sensors, query_category, query_mask);
}

void b3t_cast_capsule_all(tide_list_out *out, const int64_t stamp, const capsule *shape, const tide_float3 from,
                         const tide_quaternion rotation, const tide_float3 to, const int64_t *gone, const int32_t gone_count,
                         const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t ignore, const bool include_sensors, const int64_t query_category, const int64_t query_mask)
{
    probe p = capsule_probe(shape, rotation);
    p.good = p.good && sound(rotation);
    cast_all(out, stamp, p, from, to, gone, gone_count, gone_shapes, gone_shape_count, ignore, include_sensors, query_category, query_mask);
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
                    const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t query_category, const int64_t query_mask)
{
    gathered g;
    g.a = (asked){.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    g.bodies = g.few;
    g.count = 0;
    g.room = (int32_t)(sizeof g.few / sizeof g.few[0]);
    const world *w = at(stamp);
    if (w && p.good && finite3(at_place)) {
        p.proxy.points = p.points;
        b3World_OverlapShape(w->id, b3ToPos(vec(at_place)), &p.proxy, (b3QueryFilter){.categoryBits=(uint64_t)query_category, .maskBits=(uint64_t)query_mask}, overlapped, &g);
    }
    give(&g, out);
}

void b3t_overlap_box(tide_list_out *out, const int64_t stamp, const box *s, const tide_float3 at_place, const tide_quaternion rotation,
                     const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count, const int64_t query_category, const int64_t query_mask)
{
    probe p = box_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count, query_category, query_mask);
}

void b3t_overlap_sphere(tide_list_out *out, const int64_t stamp, const sphere *s, const tide_float3 at_place,
                        const tide_quaternion rotation, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                        const int32_t gone_shape_count, const int64_t query_category, const int64_t query_mask)
{
    probe p = sphere_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count, query_category, query_mask);
}

void b3t_overlap_capsule(tide_list_out *out, const int64_t stamp, const capsule *s, const tide_float3 at_place,
                         const tide_quaternion rotation, const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes,
                         const int32_t gone_shape_count, const int64_t query_category, const int64_t query_mask)
{
    probe p = capsule_probe(s, rotation);
    p.good = p.good && sound(rotation);
    overlap(out, stamp, p, at_place, gone, gone_count, gone_shapes, gone_shape_count, query_category, query_mask);
}

// The bodies with a shape in a sensor, as the last step found them: Box3D
// keeps each sensor's, and they're in the world's resident state, so a world
// restored from it says the same. Of up to three sensors, an entity's.
void b3t_inside(tide_list_out *out, const int64_t stamp, const int64_t first, const int64_t second, const int64_t third, const int64_t fourth,
                const int64_t *gone, const int32_t gone_count, const int64_t *gone_shapes, const int32_t gone_shape_count)
{
    gathered g;
    g.a = (asked){.gone = gone, .gone_count = gone_count, .gone_shapes = gone_shapes, .gone_shape_count = gone_shape_count};
    g.bodies = g.few;
    g.count = 0;
    g.room = (int32_t)(sizeof g.few / sizeof g.few[0]);
    const world *w = at(stamp);
    const int64_t sensors[4] = {first, second, third, fourth};
    for (int k = 0; w && k < 4; k++) {
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

// ---------------------------------------------------------------------------
// The resident's own functions (tide/resident.h)

// The canonical bytes of the state `r` names, for a machine that has none of
// it: the stamp, then Box3D's image of the world and the geometry it names, as
// a world was written into the match before its state was resident.
static void pack_state(void *context, const tide_resident *r, tide_writer *w)
{
    (void)context;
    restore_state(r);
    // Canonical interning also mutates existing geometry directories. All
    // temporary writes must be discarded when packing restores this resident.
    tide_arena_touch_all(arena);
    int sizes[B3_SNAPSHOT_SECTIONS] = {0};
    int bases[B3_SNAPSHOT_SECTIONS] = {0};
    int size = b3World_GetSnapshotAt(live.id, NULL, 0, bases, sizes);
    int at = b3GetSnapshotHead();
    for (int i = 0; i < B3_SNAPSHOT_SECTIONS; i++) { // Each section right after the one before: nothing to keep in place
        bases[i] = at;
        at += sizes[i];
    }
    size = at;
    tide_write_u64(w, (uint64_t)r->stamp);
    tide_write_u32(w, (uint32_t)size);
    uint8_t *image = tide_write_space(w, (uint32_t)size);
    if (image) {
        const int written = b3World_GetSnapshotAt(live.id, image, size, bases, sizes);
        if (written != size) {
            fprintf(stderr, "Box3D's image of its world came out %d bytes where %d were counted\n", written, size);
            abort();
        }
    }
    const int geometry = b3World_GetSnapshotGeometry(live.id, NULL, 0);
    tide_write_u32(w, (uint32_t)geometry);
    uint8_t *bytes = tide_write_space(w, (uint32_t)geometry);
    if (bytes && geometry > 0) b3World_GetSnapshotGeometry(live.id, bytes, geometry);
    // Serialization interns geometry and changes the snapshot tracking in
    // the native world. Packing is a read of r: discard that temporary work,
    // including dirty pages whose last committed identity did not change.
    tide_arena_restore(arena, r);
    load_struct();
    live.stamp = (int64_t)r->stamp;
    live.chain = r->stamp;
}

// The arena's state from canonical bytes, committed into `r`: a world made
// again, as a joining player's or a build that carries the match over makes it.
static bool unpack_state(void *context, tide_resident *r, tide_reader *rd, const uint32_t size)
{
    (void)context;
    (void)size;
    ready();
    const int64_t stamp = (int64_t)tide_read_u64(rd);
    const uint32_t image_size = tide_read_u32(rd);
    const uint8_t *image = tide_read_bytes(rd, image_size);
    const uint32_t geometry_size = tide_read_u32(rd);
    const uint8_t *geometry = tide_read_bytes(rd, geometry_size);
    if (rd->failed || !image || (geometry_size && !geometry)) return false;
    tide_resident none = {0};
    if (live.made) unmake();
    tide_arena_restore(arena, &none);
    make(workers_now());
    if (image_size > 0 && !b3World_Restore(live.id, image, (int)image_size, geometry, (int)geometry_size)) {
        fprintf(stderr, "Box3D can't read the match's image of its world (%d bytes, and %d of geometry): it was made by another build of the game\n",
                (int)image_size, (int)geometry_size);
        return false;
    }
    restores++;
    live.stamp = stamp;
    live.chain = (uint64_t)stamp;
    tide_arena_touch_all(arena);
    save_struct();
    tide_arena_commit(arena, r, (uint64_t)stamp, b3World_Digest(live.id));
    return true;
}

// Another build of the game's library left the world in the arena: it's this
// build's now, with its scratch and its callbacks.
static void relink_arena(void *context, tide_arena *a)
{
    (void)context;
    arena = a;
    const arena_user *u = tide_arena_user_read(a);
    if (u->magic != ARENA_MAGIC || tide_arena_stamp(a) == 0) return; // Nothing in it yet
    load_struct();
    live.stamp = (int64_t)tide_arena_stamp(a);
    live.chain = (uint64_t)live.stamp;
}

// Historical scenes contain plain geometry, never native world pointers.
typedef struct collider {
    int64_t body, shape;
    tide_entity entity, part;
    int32_t snaps;
    tide_float3 position;
    tide_quaternion rotation;
    tide_float3 points[8];
    int32_t count;
    float radius;
    bool sensor;
    uint8_t padding[3];
    int32_t geometry;
    int64_t category, mask;
} collider;
typedef struct hull_geometry { tide_float3 points[128]; tide_float4 planes[128]; int32_t count, faces; } hull_geometry;
_Static_assert(sizeof(collider) == 192 && offsetof(physics, pending) == 240, "historical scene ABI changed");

static b3Vec3 scene_vec(const tide_float3 p) { return vec(p); }
static tide_float3 scene_float(const b3Vec3 p) { return float3(p); }

static bool scene_real(const collider *c)
{
    if (c->count < 1 || c->count > 8 || !finite3(c->position) || !finite1(c->radius) || c->radius < 0 || !sound(c->rotation)) return false;
    for (int i = 0; i < c->count; i++) if (!finite3(c->points[i])) return false;
    return true;
}

// The caller supplies a validated native body; geometry remains in its local frame.
static int scene_read(const world *w, const int64_t native_id, collider **out, hull_geometry **hulls, int *hull_count)
{
    *out = NULL; *hulls = NULL; *hull_count = 0;
    if (!w) return 0;
    const b3BodyId on = body_of(w, native_id);
    if (!b3Body_IsValid(on) || !b3Body_IsEnabled(on)) return 0;
    const int count = b3Body_GetShapeCount(on);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(b3ShapeId) || (size_t)count > SIZE_MAX / sizeof(collider)) return 0;
    b3ShapeId *shapes = malloc((size_t)count * sizeof *shapes);
    if (!shapes) return 0;
    const int got = b3Body_GetShapes(on, shapes, count);
    size_t room = (size_t)got;
    for (int i = 0; i < got; i++) {
        if (b3Shape_GetType(shapes[i]) == b3_heightShape) {
            const b3HeightFieldData *height = b3Shape_GetHeightField(shapes[i]);
            const size_t extra = (size_t)2 * (size_t)(height->columnCount-1) * (size_t)(height->rowCount-1) - 1;
            if (extra > SIZE_MAX - room) { free(shapes); return 0; }
            room += extra;
            continue;
        }
        if (b3Shape_GetType(shapes[i]) != b3_meshShape) continue;
        const b3Mesh mesh = b3Shape_GetMesh(shapes[i]);
        if (!mesh.data || mesh.data->triangleCount < 1) { free(shapes); return 0; }
        const size_t extra = (size_t)mesh.data->triangleCount - 1;
        if (extra > SIZE_MAX - room) { free(shapes); return 0; }
        room += extra;
    }
    if (room > (1u << 30) / sizeof(collider) || room > SIZE_MAX / sizeof(collider)) { free(shapes); return 0; }
    collider *records = calloc(room, sizeof *records);
    if (!records) { free(shapes); return 0; }
    const b3WorldTransform pose = b3Body_GetTransform(on);
    int used = 0;
    for (int i = 0; i < got; i++) {
        const b3ShapeId id = shapes[i];
        collider c = {0};
        c.body = native_id;
        c.shape = handle(id.index1, id.generation);
        c.position = (tide_float3){(float)pose.p.x, (float)pose.p.y, (float)pose.p.z};
        c.rotation = turn(pose.q);
        c.sensor = b3Shape_IsSensor(id);
        const b3Filter filter = b3Shape_GetFilter(id);
        c.category = (int64_t)filter.categoryBits; c.mask = (int64_t)filter.maskBits;
        switch (b3Shape_GetType(id)) {
        case b3_sphereShape: {
            const b3Sphere shape = b3Shape_GetSphere(id);
            c.points[0] = scene_float(shape.center); c.count = 1; c.radius = shape.radius;
            break;
        }
        case b3_capsuleShape: {
            const b3Capsule shape = b3Shape_GetCapsule(id);
            c.points[0] = scene_float(shape.center1); c.points[1] = scene_float(shape.center2);
            c.count = 2; c.radius = shape.radius;
            break;
        }
        case b3_hullShape: {
            const b3HullData *shape = b3Shape_GetHull(id);
            if (!shape || shape->vertexCount < 1 || shape->vertexCount > 128 || shape->faceCount > 128) continue;
            const b3Plane *planes = b3GetHullPlanes(shape);
            bool is_box = shape->vertexCount == 8 && shape->faceCount == 6;
            for (int k = 0; is_box && k < 6; k++) {
                const b3Vec3 n = planes[k].normal;
                is_box = ((n.x == 1 || n.x == -1) && n.y == 0 && n.z == 0) ||
                    ((n.y == 1 || n.y == -1) && n.x == 0 && n.z == 0) ||
                    ((n.z == 1 || n.z == -1) && n.x == 0 && n.y == 0);
            }
            if (!is_box) {
                const size_t size = (size_t)(*hull_count + 1) * sizeof(hull_geometry);
                *hulls = tide_realloc(*hulls, (size_t)*hull_count * sizeof(hull_geometry), size);
                hull_geometry *g = *hulls + *hull_count;
                memset(g, 0, sizeof *g);
                g->count = shape->vertexCount; g->faces = shape->faceCount;
                const b3Vec3 *points = b3GetHullPoints(shape);
                for (int k=0;k<g->count;k++) g->points[k] = scene_float(points[k]);
                for (int k=0;k<g->faces;k++) g->planes[k] = (tide_float4){planes[k].normal.x,planes[k].normal.y,planes[k].normal.z,planes[k].offset};
                c.geometry = ++*hull_count;
                c.count = 8;
                for (int k=0;k<8;k++) c.points[k] = scene_float((b3Vec3){
                    k&1 ? shape->aabb.upperBound.x : shape->aabb.lowerBound.x,
                    k&2 ? shape->aabb.upperBound.y : shape->aabb.lowerBound.y,
                    k&4 ? shape->aabb.upperBound.z : shape->aabb.lowerBound.z});
                break;
            }
            c.count = shape->vertexCount; c.radius = 0;
            const b3Vec3 *points = b3GetHullPoints(shape);
            if (!points) continue;
            for (int k = 0; k < c.count; k++) c.points[k] = scene_float(points[k]);
            break;
        }
        case b3_heightShape: {
            const b3HeightFieldData *height = b3Shape_GetHeightField(id);
            const uint8_t *materials = b3GetHeightFieldMaterialIndices(height);
            const int cells = (height->columnCount-1)*(height->rowCount-1);
            c.count = 3;
            for (int k=0;k<2*cells;k++) {
                if (materials[k/2] == B3_HEIGHT_FIELD_HOLE) continue;
                const b3Triangle triangle = b3GetHeightFieldTriangle(height,k);
                for (int j=0;j<3;j++) c.points[j] = scene_float(triangle.vertices[j]);
                if (scene_real(&c)) records[used++] = c;
            }
            continue;
        }
        case b3_meshShape: {
            // Detached scenes retain triangle values, never arena pointers.
            // Their existing BVH indexes triangles, including concave gaps.
            const b3Mesh mesh = b3Shape_GetMesh(id);
            const b3Vec3 *vertices = b3GetMeshVertices(mesh.data);
            const b3MeshTriangle *triangles = b3GetMeshTriangles(mesh.data);
            if (!vertices || !triangles) continue;
            c.count = 3;
            const bool mirrored = (mesh.scale.x < 0) ^ (mesh.scale.y < 0) ^ (mesh.scale.z < 0);
            for (int k = 0; k < mesh.data->triangleCount; k++) {
                const b3MeshTriangle t = triangles[k];
                c.points[0] = scene_float(b3Mul(mesh.scale, vertices[t.index1]));
                c.points[1] = scene_float(b3Mul(mesh.scale, vertices[mirrored ? t.index3 : t.index2]));
                c.points[2] = scene_float(b3Mul(mesh.scale, vertices[mirrored ? t.index2 : t.index3]));
                if (scene_real(&c)) records[used++] = c;
            }
            continue;
        }
        default: continue;
        }
        if (scene_real(&c)) records[used++] = c;
    }
    free(shapes);
    *out = records;
    return used;
}

void b3t_read_colliders(const int64_t handle, tide_list_out *out, tide_list_out *hull_out)
{
    collider *records; hull_geometry *hulls; int hull_count;
    const int count = scene_read(active, handle, &records, &hulls, &hull_count);
    void *hull_room = hull_count ? tide_list_out_room(hull_out, (uint32_t)hull_count) : NULL;
    if (hull_room) memcpy(hull_room,hulls,(size_t)hull_count*sizeof *hulls);
    tide_list_out_count(hull_out,hull_room ? (uint32_t)hull_count : 0);
    free(hulls);
    void *room = count ? tide_list_out_room(out, (uint32_t)count) : NULL;
    if (room) memcpy(room, records, (size_t)count * sizeof *records);
    tide_list_out_count(out, room ? (uint32_t)count : 0);
    free(records);
}

// Opt-in belongs to the body, even when it is disabled or has no shapes.
// Otherwise removing its final collider would also hide its older history.
typedef struct selected_entry { int64_t order, key; bool value; uint8_t padding[7]; } selected_entry;
_Static_assert(sizeof(selected_entry)==24,"history selection map ABI changed");
static uint32_t selected_hash(const void *key) { return tide_map_hash_bytes(key,sizeof(int64_t)); }
static bool selected_equal(const void *a,const void *b) { return memcmp(a,b,sizeof(int64_t))==0; }
static const tide_map_kind selected_kind={sizeof(selected_entry),offsetof(selected_entry,key),selected_hash,selected_hash,selected_equal};

void b3t_capture_history(tide_rows *rows, physics *p)
{
    if (!(p->history_limit > 0)) return;
    const world *w = at(p->stamp);
    const tide_entity *entities = tide_rows_entities(rows);
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) {
        body b;
        tide_rows_get(rows, 0, i, &b);
        if (!b.rewind) continue;
        bool added;
        selected_entry *selection=tide_map_insert(&p->selected,&selected_kind,&b.id,TIDE_IN_MATCH,&added);
        if (!selection) tide_out_of_memory();
        if (added) selection->key=b.id;
        selection->value=true;
        collider *records; hull_geometry *hulls; int hull_count;
        const int count = scene_read(w, b.id, &records, &hulls, &hull_count);
        const int32_t hull_base = tide_list_count(p->pending_hulls);
        for (int k=0;k<hull_count;k++) {
            void *to = tide_list_add(&p->pending_hulls,sizeof(hull_geometry),TIDE_IN_MATCH);
            if (!to) tide_out_of_memory();
            memcpy(to,hulls+k,sizeof(hull_geometry));
        }
        free(hulls);
        for (int k = 0; k < count; k++) {
            if (records[k].geometry > 0) records[k].geometry += hull_base;
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
typedef struct history_frame { int32_t tick; tide_list colliders, hulls; } history_frame;
_Static_assert(sizeof(history_frame) == 12, "history frame ABI changed");
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
void b3t_seal_history(tide_rows *rows, physics *p)
{
    if (p->history_limit > 0) tide_rows_after_tick(rows, seal_history, p);
}

collider b3t_blend_collider(const collider *before, const collider *current, const float alpha)
{
    collider c = *current;
    if (!scene_real(before) || !scene_real(current) || !(alpha >= 0 && alpha <= 1)) return c;
    c.position.x = before->position.x + (current->position.x - before->position.x) * alpha;
    c.position.y = before->position.y + (current->position.y - before->position.y) * alpha;
    c.position.z = before->position.z + (current->position.z - before->position.z) * alpha;
    c.rotation = tide_slerp_q(before->rotation, current->rotation, alpha);
    return c;
}

static b3ShapeProxy scene_proxy(const collider *c, b3Vec3 points[8])
{
    for (int i = 0; i < c->count; i++) points[i] = b3RotateVector(quat(c->rotation), scene_vec(c->points[i]));
    return (b3ShapeProxy){points, c->count, c->radius};
}

// A one-leaf mesh over a detached triangle lets historical queries use the
// same front-face tests and narrow phase as native mesh queries, with no cook.
typedef struct scene_triangle {
    b3MeshData data;
    b3MeshNode node;
    b3Vec3 vertices[3];
    b3MeshTriangle triangle;
    uint8_t material;
} scene_triangle;

static b3Mesh scene_triangle_mesh(scene_triangle *t, const b3Vec3 *points)
{
    memset(t, 0, sizeof *t);
    t->data.nodeOffset = offsetof(scene_triangle, node);
    t->data.vertexOffset = offsetof(scene_triangle, vertices);
    t->data.triangleOffset = offsetof(scene_triangle, triangle);
    t->data.materialOffset = offsetof(scene_triangle, material);
    t->data.nodeCount = 1;
    t->data.vertexCount = 3;
    t->data.triangleCount = 1;
    t->data.materialCount = 1;
    memcpy(t->vertices, points, sizeof t->vertices);
    t->triangle = (b3MeshTriangle){0, 1, 2};
    t->node.data.asLeaf.type = 3;
    t->node.data.asLeaf.triangleCount = 1;
    t->node.lowerBound = b3Min(points[0], b3Min(points[1], points[2]));
    t->node.upperBound = b3Max(points[0], b3Max(points[1], points[2]));
    return (b3Mesh){&t->data, b3Vec3_one};
}

static bool scene_overlap(const collider *c, b3ShapeProxy query, const tide_float3 position)
{
    if (!scene_real(c) || !finite3(position)) return false;
    b3Vec3 points[8];
    const b3ShapeProxy target = scene_proxy(c, points);
    const b3Vec3 relative = b3Sub(scene_vec(position), scene_vec(c->position));
    if (!finite3(scene_float(relative))) return false;
    const b3DistanceInput input = {.proxyA = target, .proxyB = query, .transform = {relative, b3Quat_identity}, .useRadii = true};
    b3SimplexCache cache = {0};
    return b3ShapeDistance(&input, &cache, NULL, 0).distance <= 0.0f;
}

static found scene_cast(const collider *c, b3ShapeProxy query, const tide_float3 from, const tide_float3 to, const bool ray)
{
    found result = {0};
    if (!scene_real(c) || !finite3(from) || !finite3(to)) return result;
    const b3Vec3 translation = b3Sub(scene_vec(to), scene_vec(from));
    const b3Vec3 relative = b3Sub(scene_vec(from), scene_vec(c->position));
    if (!finite3(scene_float(translation)) || !finite3(scene_float(relative))) return result;
    if (scene_overlap(c, query, from)) {
        if (ray) return result;
        result.body = c->body; result.shape = c->shape; result.point = from;
        return result;
    }
    b3Vec3 points[8];
    const b3ShapeCastPairInput input = {.proxyA = scene_proxy(c, points), .proxyB = query,
        .transform = {relative, b3Quat_identity}, .translationB = translation, .maxFraction = 1.0f};
    b3CastOutput hit;
    if (c->count == 3) {
        scene_triangle triangle;
        const b3Mesh mesh = scene_triangle_mesh(&triangle, points);
        b3Vec3 shifted[8];
        if (query.count < 1 || query.count > 8) return result;
        for (int i = 0; i < query.count; i++) shifted[i] = b3Add(relative, query.points[i]);
        const b3ShapeCastInput cast = {.proxy = {shifted, query.count, query.radius}, .translation = translation, .maxFraction = 1};
        hit = b3ShapeCastMesh(&mesh, &cast);
    } else hit = b3ShapeCast(&input);
    if (!hit.hit) return result;
    result.body = c->body; result.shape = c->shape;
    result.point = scene_float(b3Add(scene_vec(c->position), hit.point));
    result.normal = scene_float(hit.normal); result.fraction = hit.fraction;
    return result;
}

bool b3t_scene_touch(const collider *a, const collider *b)
{
    if (!scene_real(b)) return false;
    b3Vec3 points[8];
    return scene_overlap(a, scene_proxy(b, points), b->position);
}

found b3t_scene_ray(const collider *c, const tide_float3 from, const tide_float3 to)
{
    found result = {0};
    if (!scene_real(c) || !finite3(from) || !finite3(to)) return result;
    const b3Vec3 start = b3InvRotateVector(quat(c->rotation), b3Sub(scene_vec(from), scene_vec(c->position)));
    const b3Vec3 finish = b3InvRotateVector(quat(c->rotation), b3Sub(scene_vec(to), scene_vec(c->position)));
    const b3Vec3 direction = b3Sub(finish, start);
    if (!finite3(scene_float(start)) || !finite3(scene_float(direction))) return result;
    const b3Vec3 point = {0};
    if (scene_overlap(c, (b3ShapeProxy){&point, 1, 0}, from)) return result;
    const b3RayCastInput input = {.origin = start, .translation = direction, .maxFraction = 1.0f};
    b3CastOutput hit = {0};
    if (c->count == 1) {
        const b3Sphere shape = {scene_vec(c->points[0]), c->radius};
        hit = b3RayCastSphere(&shape, &input);
    }
    else if (c->count == 2) {
        const b3Capsule shape = {scene_vec(c->points[0]), scene_vec(c->points[1]), c->radius};
        hit = b3RayCastCapsule(&shape, &input);
    }
    else if (c->count == 3) {
        scene_triangle triangle;
        b3Vec3 points[3];
        for (int i = 0; i < 3; i++) points[i] = scene_vec(c->points[i]);
        const b3Mesh mesh = scene_triangle_mesh(&triangle, points);
        hit = b3RayCastMesh(&mesh, &input);
    }
    else {
        tide_float3 lo = c->points[0], hi = lo;
        for (int i = 1; i < c->count; i++) { if (c->points[i].x < lo.x) lo.x = c->points[i].x; if (c->points[i].x > hi.x) hi.x = c->points[i].x; }
        for (int i = 1; i < c->count; i++) { if (c->points[i].y < lo.y) lo.y = c->points[i].y; if (c->points[i].y > hi.y) hi.y = c->points[i].y; }
        for (int i = 1; i < c->count; i++) { if (c->points[i].z < lo.z) lo.z = c->points[i].z; if (c->points[i].z > hi.z) hi.z = c->points[i].z; }
        float enter = 0.0f, leave = 1.0f;
        bool inside = true;
        if (start.x < lo.x || start.x > hi.x) inside = false;
        if (direction.x == 0) { if (start.x < lo.x || start.x > hi.x) return result; }
        else {
            float near = (lo.x - start.x) / direction.x, far = (hi.x - start.x) / direction.x;
            float sign = -1.0f;
            if (near > far) { const float temp = near; near = far; far = temp; sign = 1.0f; }
            if (near > enter) { enter = near; hit.normal = (b3Vec3){0}; hit.normal.x = sign; }
            if (far < leave) leave = far;
            if (enter > leave) return result;
        }
        if (start.y < lo.y || start.y > hi.y) inside = false;
        if (direction.y == 0) { if (start.y < lo.y || start.y > hi.y) return result; }
        else {
            float near = (lo.y - start.y) / direction.y, far = (hi.y - start.y) / direction.y;
            float sign = -1.0f;
            if (near > far) { const float temp = near; near = far; far = temp; sign = 1.0f; }
            if (near > enter) { enter = near; hit.normal = (b3Vec3){0}; hit.normal.y = sign; }
            if (far < leave) leave = far;
            if (enter > leave) return result;
        }
        if (start.z < lo.z || start.z > hi.z) inside = false;
        if (direction.z == 0) { if (start.z < lo.z || start.z > hi.z) return result; }
        else {
            float near = (lo.z - start.z) / direction.z, far = (hi.z - start.z) / direction.z;
            float sign = -1.0f;
            if (near > far) { const float temp = near; near = far; far = temp; sign = 1.0f; }
            if (near > enter) { enter = near; hit.normal = (b3Vec3){0}; hit.normal.z = sign; }
            if (far < leave) leave = far;
            if (enter > leave) return result;
        }
        if (inside) return result;
        hit.hit = true; hit.fraction = enter; hit.point = b3Add(start, b3MulSV(enter, direction));
    }
    if (!hit.hit) {
        return result;
    }
    result.body = c->body; result.shape = c->shape;
    result.point = scene_float(b3Add(scene_vec(c->position), b3RotateVector(quat(c->rotation), hit.point)));
    result.normal = scene_float(b3RotateVector(quat(c->rotation), hit.normal)); result.fraction = hit.fraction;
    return result;
}
found b3t_scene_cast_box(const collider *c, const box *shape, const tide_float3 from, const tide_quaternion rotation, const tide_float3 to)
{
    probe p = box_probe(shape, rotation); if (!sound(rotation) || !p.good) return (found){0};
    p.proxy.points = p.points;
    return scene_cast(c, p.proxy, from, to, false);
}
bool b3t_scene_overlap_box(const collider *c, const box *shape, const tide_float3 from, const tide_quaternion rotation)
{
    probe p = box_probe(shape, rotation); if (!sound(rotation) || !p.good) return false;
    p.proxy.points = p.points;
    return scene_overlap(c, p.proxy, from);
}
found b3t_scene_cast_sphere(const collider *c, const sphere *shape, const tide_float3 from, const tide_quaternion rotation, const tide_float3 to)
{
    probe p = sphere_probe(shape, rotation); if (!sound(rotation) || !p.good) return (found){0};
    p.proxy.points = p.points;
    return scene_cast(c, p.proxy, from, to, false);
}
bool b3t_scene_overlap_sphere(const collider *c, const sphere *shape, const tide_float3 from, const tide_quaternion rotation)
{
    probe p = sphere_probe(shape, rotation); if (!sound(rotation) || !p.good) return false;
    p.proxy.points = p.points;
    return scene_overlap(c, p.proxy, from);
}
found b3t_scene_cast_capsule(const collider *c, const capsule *shape, const tide_float3 from, const tide_quaternion rotation, const tide_float3 to)
{
    probe p = capsule_probe(shape, rotation); if (!sound(rotation) || !p.good) return (found){0};
    p.proxy.points = p.points;
    return scene_cast(c, p.proxy, from, to, false);
}
bool b3t_scene_overlap_capsule(const collider *c, const capsule *shape, const tide_float3 from, const tide_quaternion rotation)
{
    probe p = capsule_probe(shape, rotation); if (!sound(rotation) || !p.good) return false;
    p.proxy.points = p.points;
    return scene_overlap(c, p.proxy, from);
}

// Immutable preorder BVH. A missed branch jumps to its end; queries allocate
// only their short candidate list and preserve the original native handle order.
typedef struct scene_bounds { tide_float3 min, max; bool valid; uint8_t padding[3]; } scene_bounds;
typedef struct scene_node { tide_float3 min, max; int32_t collider, next; } scene_node;
_Static_assert(sizeof(scene_bounds) == 28 && sizeof(scene_node) == 32, "scene index ABI changed");
static scene_bounds proxy_bounds(const b3Vec3 *points, const int count, const float radius,
                                 const tide_float3 from, const tide_float3 to)
{
    scene_bounds bounds = {0};
    if (count < 1 || count > 8 || !finite3(from) || !finite3(to) || !finite1(radius) || radius < 0) return bounds;
    bounds.min = bounds.max = scene_float(points[0]);
    for (int i = 1; i < count; i++) { if (points[i].x < bounds.min.x) bounds.min.x = points[i].x; if (points[i].x > bounds.max.x) bounds.max.x = points[i].x; }
    bounds.min.x += (from.x < to.x ? from.x : to.x) - radius;
    bounds.max.x += (from.x > to.x ? from.x : to.x) + radius;
    for (int i = 1; i < count; i++) { if (points[i].y < bounds.min.y) bounds.min.y = points[i].y; if (points[i].y > bounds.max.y) bounds.max.y = points[i].y; }
    bounds.min.y += (from.y < to.y ? from.y : to.y) - radius;
    bounds.max.y += (from.y > to.y ? from.y : to.y) + radius;
    for (int i = 1; i < count; i++) { if (points[i].z < bounds.min.z) bounds.min.z = points[i].z; if (points[i].z > bounds.max.z) bounds.max.z = points[i].z; }
    bounds.min.z += (from.z < to.z ? from.z : to.z) - radius;
    bounds.max.z += (from.z > to.z ? from.z : to.z) + radius;
    bounds.valid = finite3(bounds.min) && finite3(bounds.max);
    return bounds;
}
scene_bounds b3t_collider_bounds(const collider *c)
{
    if (!scene_real(c)) return (scene_bounds){0};
    b3Vec3 points[8];
    const b3ShapeProxy proxy = scene_proxy(c, points);
    return proxy_bounds(points, proxy.count, proxy.radius, c->position, c->position);
}
scene_bounds b3t_box_bounds(const box *shape, const tide_quaternion rotation, const tide_float3 from, const tide_float3 to)
{
    const probe p = box_probe(shape, rotation);
    if (!p.good || !sound(rotation)) return (scene_bounds){0};
    return proxy_bounds(p.points, p.proxy.count, p.proxy.radius, from, to);
}

scene_bounds b3t_sphere_bounds(const sphere *shape, const tide_quaternion rotation, const tide_float3 from, const tide_float3 to)
{
    const probe p = sphere_probe(shape, rotation);
    if (!p.good || !sound(rotation)) return (scene_bounds){0};
    return proxy_bounds(p.points, p.proxy.count, p.proxy.radius, from, to);
}

scene_bounds b3t_capsule_bounds(const capsule *shape, const tide_quaternion rotation, const tide_float3 from, const tide_float3 to)
{
    const probe p = capsule_probe(shape, rotation);
    if (!p.good || !sound(rotation)) return (scene_bounds){0};
    return proxy_bounds(p.points, p.proxy.count, p.proxy.radius, from, to);
}

#include "joints.inc"

#include "controls.inc"

#include "shapes.inc"

#include "hull_queries.inc"

// Shapes on a copy of another shard's body (CopyBodies, CopyParts in a
// sharded world): what the entity it copies has, made as the systems that
// keep a body's shapes make them
int64_t b3t_copy_mesh(const int64_t on, const tide_float3 *vertices, const int32_t vertex_count, const int32_t *indices,
                      const int32_t index_count, const tide_float3 scale, const tide_float3 offset, const float friction,
                      const float bounce)
{
    note('M', &on, sizeof on);
    if (vertex_count < 3 || vertex_count > 65536 || index_count < 3 || index_count > 3 * 65536) return 0;
    return attach_mesh(on, vertices, vertex_count, indices, index_count, scale, offset, friction, bounce);
}

int64_t b3t_copy_hull(const int64_t on, const tide_float3 *vertices, const int32_t count, const tide_float3 scale,
                      const tide_float3 offset, const float density, const float friction, const float bounce, const bool sensor)
{
    note('U', &on, sizeof on);
    const uint32_t mark = tide_scratch_mark();
    const hull_collider s = {.vertices = tide_list_from(vertices, count, sizeof *vertices), .scale = scale, .offset = offset,
                             .density = density, .friction = friction, .bounce = bounce, .sensor = sensor};
    const int64_t made = live_hull(on, 0, &s);
    tide_scratch_reset(mark);
    return made;
}

int64_t b3t_copy_height(const int64_t on, const float *heights, const int32_t height_count, const uint8_t *holes,
                        const int32_t hole_count, const int32_t columns, const int32_t rows, const tide_float3 scale,
                        const float minimum, const float maximum, const float friction, const float bounce)
{
    note('E', &on, sizeof on);
    const uint32_t mark = tide_scratch_mark();
    const height_collider s = {.heights = tide_list_from(heights, height_count, sizeof *heights),
                               .holes = tide_list_from(holes, hole_count, sizeof *holes), .columns = columns, .rows = rows,
                               .scale = scale, .minimum = minimum, .maximum = maximum, .friction = friction, .bounce = bounce};
    const int64_t made = live_height(on, 0, &s);
    tide_scratch_reset(mark);
    return made;
}
