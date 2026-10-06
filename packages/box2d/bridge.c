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
    bool asleep;
    bool step_asleep; // What the last step left `asleep` as
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

// Physics, with the padding it writes out. Its lists and its map are the
// match's to read and write: C leaves them as they are.
typedef struct physics {
    tide_float2 gravity;
    int32_t substeps;
    int32_t bodies;
    int32_t shapes;
    uint8_t unused0[4];
    int64_t stamp;
    int32_t size;
    bool moved;
    uint8_t unused1[3];
    tide_list image;
    tide_list gone;
    tide_map owners;
    uint8_t unused2[4];
} physics;

_Static_assert(sizeof(body) == 88 && sizeof(box) == 32 && sizeof(circle) == 28 && sizeof(touch) == 24
                   && sizeof(physics) == 64 && offsetof(physics, moved) == 36,
               "box2d.tide's types changed: these are theirs");

// A body's four bools are side by side, the last four bytes of its component:
// one field of four bytes to the systems written in C (tide/rows.h), which
// read whether it's asleep, and what the last step left that as, out of it.
#define FLAGS offsetof(body, fixed_rotation)

_Static_assert(FLAGS % 4 == 0 && offsetof(body, asleep) == FLAGS + 2 && offsetof(body, step_asleep) == FLAGS + 3
                   && sizeof(body) == FLAGS + 4,
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
// made up for from about here: measured, a tick with 1,800 crates awake was
// slower shared and one with 2,400 quicker. A page's threads cost more to
// reach, and its step has no SIMD to share out: there, 2,400 was slower
// shared and 4,800 quicker.
#ifdef __wasm__
#define SHARED_BODIES 4096
#else
#define SHARED_BODIES 2048
#endif

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
static int32_t share_from = SHARED_BODIES; // A step with this many bodies awake is shared out

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
static int64_t *sleeps;
static int32_t sleep_count;
static int32_t sleep_room;
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
    // A tick starts here. The last one's new bodies each have their entity: Own goes through the bodies as Enter did
    if (made_taken != made_count) {
        fprintf(stderr, "Box2D: %d bodies were made last tick that no entity was given\n", (int)(made_count - made_taken));
        abort();
    }
    made_count = made_taken = 0;
    sleep_count = 0;
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
    made_count = made_taken = 0;
    sleep_count = 0;
    slept = false;
}

// For tests: a step is shared out over the tick's threads from this many
// bodies awake, where it takes thousands for that to pay. None is the usual.
void b2t_share_from(const int32_t bodies)
{
    share_from = bodies > 0 ? bodies : SHARED_BODIES;
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

// A body's component into Box2D: made, if it's new, or else what code wrote
// into it since the last step. Whether it was made.
static bool sync_body(body *b)
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
        // Spawned asleep: it's made so, by itself, and is there for the others to rest on or run into. A
        // static body is neither asleep nor awake
        if (b->type == b2_staticBody) b->asleep = false;
        def.isAwake = !b->asleep;
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
        // Box2D wakes all of what the body fell asleep with. Sleeping waits for the step (put_to_sleep)
        if (b->asleep != b->step_asleep) {
            if (b2Body_GetType(id) == b2_staticBody) {
                b->asleep = false; // It's neither, and reads so again
            } else {
                if (b->asleep) {
                    if (sleep_count == sleep_room) {
                        const int32_t room = sleep_room ? sleep_room * 2 : 64;
                        sleeps = tide_realloc(sleeps, (size_t)sleep_room * sizeof *sleeps, (size_t)room * sizeof *sleeps);
                        sleep_room = room;
                    }
                    sleeps[sleep_count++] = b->id;
                } else {
                    b2Body_SetAwake(id, true);
                }
                did |= 64u;
            }
        }
    }
    b->step_asleep = b->asleep; // What Leave makes of both, once the step has had its say
    // A body that was just made has no shape yet, so no weight, and an impulse would do nothing to it: the
    // ones it was spawned with wait for the step, which gives them once its shapes are on it (push_new).
    // Either way they're given once, and noted here
    const bool is_new = did & 1u;
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
    const int32_t *type;
    const int32_t *flags; // Its four bools, of which two say whether it's asleep (flags_asleep)
} body_fields;

static body_fields fields_of(const tide_rows *rows)
{
    return (body_fields){
        .id = tide_rows_ints(rows, 0, offsetof(body, id)),
        .id_high = tide_rows_ints(rows, 0, offsetof(body, id) + 4),
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
        .type = tide_rows_ints(rows, 0, offsetof(body, type)),
        .flags = tide_rows_ints(rows, 0, FLAGS),
    };
}

// The system Enter: new bodies are made, and what code wrote into a body since
// the last step goes to Box2D. A body nothing wrote to costs these
// comparisons, and nothing else. It runs on one thread, its rows in order
// (it takes Physics to write), as what's done to the world is noted in order.
void b2t_enter(tide_rows *rows, physics *p)
{
    (void)p;
    const uint32_t count = tide_rows_count(rows);
    const tide_entity *entities = tide_rows_entities(rows);
    body_fields f = fields_of(rows);
    for (uint32_t i = 0; i < count; i++) {
        const bool moved = f.x[i] != f.step_x[i] || f.y[i] != f.step_y[i] || f.angle[i] != f.step_angle[i];
        const bool pushed = f.vx[i] != f.step_vx[i] || f.vy[i] != f.step_vy[i] || f.spin[i] != f.step_spin[i];
        const bool nudged = f.impulse_x[i] != 0 || f.impulse_y[i] != 0 || f.turn[i] != 0;
        const bool is_new = f.id[i] == 0 && f.id_high[i] == 0;
        const bool lulled = flags_asleep(f.flags[i]) != flags_step_asleep(f.flags[i]); // Put to sleep, or woken
        if (!is_new && !moved && !pushed && !nudged && !lulled) continue;
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
    // A sensor doesn't report another: each would report the other, and their entities hear of it twice.
    // Shapes of one group below zero are never set against each other
    if (sensor) def.filter.groupIndex = -1;
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
    const bool any = sleep_count > 0;
    for (int32_t i = 0; i < sleep_count; i++) {
        const b2BodyId id = body_of(w, sleeps[i]);
        // One that's asleep already went with a body before it in the list
        if (b2Body_IsValid(id) && b2Body_GetType(id) != b2_staticBody && b2Body_IsAwake(id)) b2Body_SetAwake(id, false);
    }
    sleep_count = 0;
    return any;
}

int64_t b2t_step(const float dt, const int32_t substeps, const tide_float2 gravity)
{
    world *w = active;
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
        // wakes every body when sleeping is turned off, and has no other call for it. It follows from the
        // gravity, which every step notes.
        b2World_EnableSleeping(w->id, false);
        b2World_EnableSleeping(w->id, true);
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
            if (!l && slept && !asleep && f.type[i] != b2_staticBody && b2Body_IsValid(asked)) asleep = !b2Body_IsAwake(asked);
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

// Whose body a shape is on, or 0 for a shape that's gone, with its body: its
// entity was destroyed, and what it touched parts from no entity.
static int64_t owner_of(const b2ShapeId shape)
{
    if (!b2Shape_IsValid(shape)) return 0;
    const b2BodyId id = b2Shape_GetBody(shape);
    return handle(id.index1, id.generation);
}

touch b2t_touch(const int32_t index)
{
    const b2ContactEvents contacts = b2World_GetContactEvents(active->id);
    const b2SensorEvents sensors = b2World_GetSensorEvents(active->id);
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
