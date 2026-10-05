// The C side of box2d.tide: Box2D's worlds, and what the package's systems ask
// of them.
//
// A match's Box2D state is in the match, as an image (Physics.image) and a
// stamp that names it: a hash of the image. C's worlds here are only where
// that state is stepped. Each is at a stamp, a tick finds the one at its
// match's stamp or has one made from the image, and the step leaves it at the
// next. So a rollback, a join, a reload or another match in the same program
// each just find no world at their stamp, and get one. Two matches at the same
// stamp are at the same state, and either's world serves both.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
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
    bool fixed_rotation;
    bool bullet;
} body;

typedef struct box {
    int64_t id;
    tide_float2 size;
    tide_float2 offset;
    float density;
    float friction;
    float bounce;
    bool sensor;
} box;

typedef struct circle {
    int64_t id;
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

_Static_assert(sizeof(body) == 64 && sizeof(box) == 40 && sizeof(circle) == 40 && sizeof(touch) == 24,
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
    uint64_t used;  // When a tick last had it
    uint8_t *image; // Its state after its last step
    int size;
    int room;
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
    w->size = 0;
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

// ---------------------------------------------------------------------------
// The tick's world

bool b2t_open(const int64_t stamp)
{
    const int workers = workers_now();
    for (int i = 0; i < WORLDS; i++) {
        world *w = &worlds[i];
        if (!w->made || w->stamp != stamp || w->workers != workers) continue;
        w->used = ++uses;
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
    restores += size > 0;
    if (size > 0 && !b2World_Restore(w->id, image, size)) {
        fprintf(stderr, "Box2D can't read the match's image of its world (%d bytes): it was made by another build of the game\n",
                (int)size);
        abort();
    }
    w->stamp = stamp;
    w->used = ++uses;
    active = w;
}

void b2t_gone(const int64_t *bodies, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const b2BodyId id = body_of(active, bodies[i]);
        if (b2Body_IsValid(id)) b2DestroyBody(id);
    }
}

// For a host, and for tests: C's worlds go, as when a program starts. Matches
// lose nothing, as their state is their own.
void b2t_forget(void)
{
    for (int i = 0; i < WORLDS; i++) {
        unmake(&worlds[i]);
        free(worlds[i].image);
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

// Where a step's time goes, for a benchmark that asks: Box2D's own step, and
// its state into an image (with the image's hash). Nanoseconds since the last
// call; the first starts the measuring.
void b2t_measure(uint64_t *step, uint64_t *image)
{
    measuring = true;
    *step = step_ns;
    *image = image_ns;
    step_ns = 0;
    image_ns = 0;
}

// ---------------------------------------------------------------------------
// Bodies and shapes

int32_t b2t_body(body *b)
{
    const world *w = active;
    int32_t made = 0;
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
        made = 1;
    } else {
        // What code wrote since the last step, and only that: a body nothing
        // touched stays as Box2D has it, asleep if it was
        id = body_of(w, b->id);
        if (!b2Body_IsValid(id)) return 0;
        const b2Pos at = b2Body_GetPosition(id);
        if ((float)at.x != b->position.x || (float)at.y != b->position.y || b2Rot_GetAngle(b2Body_GetRotation(id)) != b->angle) {
            b2Body_SetTransform(id, b2ToPos((b2Vec2){b->position.x, b->position.y}), b2MakeRot(b->angle));
        }
        const b2Vec2 velocity = b2Body_GetLinearVelocity(id);
        if (velocity.x != b->velocity.x || velocity.y != b->velocity.y) {
            b2Body_SetLinearVelocity(id, (b2Vec2){b->velocity.x, b->velocity.y});
        }
        if (b2Body_GetAngularVelocity(id) != b->spin) b2Body_SetAngularVelocity(id, b->spin);
    }
    if (b->impulse.x != 0 || b->impulse.y != 0) {
        b2Body_ApplyLinearImpulseToCenter(id, (b2Vec2){b->impulse.x, b->impulse.y}, true);
        b->impulse = (tide_float2){0, 0};
    }
    if (b->turn != 0) {
        b2Body_ApplyAngularImpulse(id, b->turn, true);
        b->turn = 0;
    }
    return made;
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

int32_t b2t_box(const int64_t on, box *s)
{
    if (s->id != 0 || on == 0) return 0;
    const b2BodyId owner = body_of(active, on);
    if (!b2Body_IsValid(owner)) return 0;
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Polygon polygon = b2MakeOffsetBox(0.5f * s->size.x, 0.5f * s->size.y, (b2Vec2){s->offset.x, s->offset.y}, b2Rot_identity);
    const b2ShapeId id = b2CreatePolygonShape(owner, &def, &polygon);
    s->id = handle(id.index1, id.generation);
    return 1;
}

int32_t b2t_circle(const int64_t on, circle *s)
{
    if (s->id != 0 || on == 0) return 0;
    const b2BodyId owner = body_of(active, on);
    if (!b2Body_IsValid(owner)) return 0;
    const b2ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
    const b2Circle round = {{s->offset.x, s->offset.y}, s->radius};
    const b2ShapeId id = b2CreateCircleShape(owner, &def, &round);
    s->id = handle(id.index1, id.generation);
    return 1;
}

// ---------------------------------------------------------------------------
// The step

// An image's stamp: megabytes, every tick, so four streams of 8 bytes at a
// time, which a CPU runs side by side. The same on every machine: whole
// numbers, read as bytes in order.
static uint64_t word_at(const uint8_t *p)
{
    uint64_t word;
    memcpy(&word, p, sizeof word);
    return word;
}

static uint64_t mix(const uint64_t h, const uint64_t word)
{
    const uint64_t x = (h ^ word) * UINT64_C(0x9e3779b97f4a7c15);
    return x << 29 | x >> 35;
}

static int64_t stamp_of(const uint8_t *image, const size_t size)
{
    uint64_t a = UINT64_C(0x243f6a8885a308d3);
    uint64_t b = UINT64_C(0x13198a2e03707344);
    uint64_t c = UINT64_C(0xa4093822299f31d0);
    uint64_t d = UINT64_C(0x082efa98ec4e6c89);
    size_t at = 0;
    for (; at + 32 <= size; at += 32) {
        a = mix(a, word_at(image + at));
        b = mix(b, word_at(image + at + 8));
        c = mix(c, word_at(image + at + 16));
        d = mix(d, word_at(image + at + 24));
    }
    const uint64_t bytes = size; // As wide on every machine
    uint64_t h = tide_hash_more(tide_hash_more(tide_hash_more(a, &b, sizeof b), &c, sizeof c), &d, sizeof d);
    h = tide_hash_end(tide_hash_more(tide_hash_more(h, image + at, size - at), &bytes, sizeof bytes));
    return h ? (int64_t)h : 1; // 0 is a match nothing stepped
}

int64_t b2t_step(const float dt, const int32_t substeps, const tide_float2 gravity)
{
    world *w = active;
    b2World_SetGravity(w->id, (b2Vec2){gravity.x, gravity.y});
    shared = w->workers > 1 && b2World_GetAwakeBodyCount(w->id) >= SHARED_BODIES;
    shared_steps += shared;
    const uint64_t began = measuring ? tide_time_now_ns() : 0;
    b2World_Step(w->id, dt, substeps);
    shared = false;
    const uint64_t stepped = measuring ? tide_time_now_ns() : 0;

    // Into the room there is: asking for the size first would go through the
    // world once more, and it only fails to fit when the world grew a lot
    w->size = w->room ? b2World_GetSnapshot(w->id, w->image, w->room) : b2World_GetSnapshot(w->id, NULL, 0);
    if (w->size > w->room) {
        free(w->image);
        w->room = w->size + w->size / 2;
        w->image = tide_alloc((size_t)w->room);
        w->size = b2World_GetSnapshot(w->id, w->image, w->room);
    }
    w->stamp = stamp_of(w->image, (size_t)w->size);
    if (measuring) {
        step_ns += stepped - began;
        image_ns += tide_time_now_ns() - stepped;
    }
    return w->stamp;
}

int32_t b2t_size(void)
{
    return active->size;
}

void b2t_image(uint8_t *image, const int32_t count)
{
    const world *w = active;
    memcpy(image, w->image, (size_t)w->size);
    memset(image + w->size, 0, (size_t)(count - w->size));
}

void b2t_pull(body *b, const int64_t stamp)
{
    (void)stamp;
    if (b->id == 0) return;
    const b2BodyId id = body_of(active, b->id);
    if (!b2Body_IsValid(id)) return;
    const b2Pos at = b2Body_GetPosition(id);
    const b2Vec2 velocity = b2Body_GetLinearVelocity(id);
    b->position = (tide_float2){(float)at.x, (float)at.y};
    b->angle = b2Rot_GetAngle(b2Body_GetRotation(id));
    b->velocity = (tide_float2){velocity.x, velocity.y};
    b->spin = b2Body_GetAngularVelocity(id);
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
