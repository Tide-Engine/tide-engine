// Box2D's image written as what changed of the last one, against the image
// written whole: the same bytes, whatever is done to the world. The image is
// in a match's hash and is what a player who joins makes their world from, so
// one byte apart is a match that comes apart.
//
// Twice over: a match (yard.tide) that does to sleeping and static bodies what
// the step doesn't, through the package as a game uses it; and Box2D by
// itself, with whatever a game might call, at random.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide/net.h"
#include "tide_test.h"
#include "yard.h"

#ifdef BOX2D_TEST_JOBS
#include "tide/platform.h"
#endif

// Box2D's headers are its own, as its sources are
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "../box2d/src/box2d/box2d.h"
#pragma clang diagnostic pop

// bridge.c
void b2t_forget(void);
void b2t_check(bool on, bool whenever);
void b2t_images(int32_t *whole, int32_t *in_parts);
void b2t_share_from(int32_t bodies);
int32_t b2t_shared_steps(void);
int32_t b2t_restores(void);

#define DT (1.0f / 60.0f)

enum { TICKS = 900, QUIET = 680 }; // The script's, and then until everything is asleep

static tide_world a;
static tide_world b;
static tide_world kept;
static uint64_t went[TICKS];

// The yard for `ticks` ticks, with every tick's hash into `hashes`: the script
// stops once it's through, and what's left comes to rest.
static void play(tide_world *w, uint64_t *hashes, const int from, const int ticks)
{
    for (int i = from; i < ticks; i++) {
        if (i == QUIET) w->Script.quiet = true;
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w); // The image is in it
    }
}

static void images_since(int32_t *whole, int32_t *in_parts)
{
    int32_t now_whole;
    int32_t now_in_parts;
    b2t_images(&now_whole, &now_in_parts);
    const int32_t were_whole = *whole;
    const int32_t were_in_parts = *in_parts;
    *whole = now_whole - were_whole;
    *in_parts = now_in_parts - were_in_parts;
}

// The match comes out the same whether its images are written in parts
// whenever they can be, or as a match has them, whole while most of the world
// is awake: and every image in parts is the image written whole (b2t_check).
TIDE_TEST(box2d_parts_are_the_whole_image)
{
    static uint64_t as_a_match[TICKS];
    for (int usual = 0; usual < 2; usual++) {
        int32_t whole = 0;
        int32_t in_parts = 0;
        images_since(&whole, &in_parts);
        b2t_forget();
        b2t_check(true, !usual);
        tide_world_init(&a, DT);
        play(&a, usual ? as_a_match : went, 0, TICKS);
        images_since(&whole, &in_parts);
        printf("    %s: %d images written whole and %d in parts; %d bodies and %d shapes made; %d touches began and %d ended; the "
               "image is %d bytes\n",
               usual ? "as a match has them" : "in parts whenever", (int)whole, (int)in_parts, (int)a.Box2D_Physics.bodies,
               (int)a.Box2D_Physics.shapes, (int)a.Tally.touched, (int)a.Tally.parted, (int)a.Box2D_Physics.size);
        // What the script is for happened: bodies came and went, and things touched
        TIDE_CHECK(a.Box2D_Physics.bodies == 40 && a.Box2D_Physics.shapes == 42 && a.Tally.touched > 30 && a.Tally.parted > 5);
        TIDE_CHECK(in_parts > (usual ? 300 : 500) && whole > 0);
        // ...and it all came to rest: the last ticks stepped nothing
        TIDE_CHECK(!a.Box2D_Physics.moved && went[TICKS - 1] != 0);
        tide_world_free(&a);
    }
    for (int i = 0; i < TICKS; i++) {
        if (went[i] != as_a_match[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(went[i] == as_a_match[i]);
    }
}

// A world whose C side is made from its image again before every tick writes
// every image whole, and one that never is writes them in parts: the same
// match, tick for tick, so the same bytes in the match's list.
TIDE_TEST(box2d_parts_leave_the_list_as_a_whole_image_does)
{
    b2t_forget();
    b2t_check(true, true);
    tide_world_init(&a, DT);
    play(&a, went, 0, TICKS);
    tide_world_init(&b, DT);
    int32_t whole = 0;
    int32_t in_parts = 0;
    images_since(&whole, &in_parts);
    for (int i = 0; i < TICKS; i++) {
        if (i == QUIET) b.Script.quiet = true;
        b2t_forget();
        tide_world_tick(&b);
        if (tide_world_hash(&b) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
    }
    images_since(&whole, &in_parts);
    TIDE_CHECK(in_parts == 0 && whole > 600);
    tide_world_free(&a);
    tide_world_free(&b);
}

// A snapshot goes on as the world it was taken of did: the first image after
// C's world is made from the match's again is whole, and the ones after it
// are parts of that one.
TIDE_TEST(box2d_parts_roll_back)
{
    b2t_forget();
    b2t_check(true, true);
    tide_world_init(&a, DT);
    play(&a, went, 0, TICKS);
    tide_world_free(&a);
    tide_world_init(&a, DT);
    static uint64_t again[TICKS];
    // Every 37 ticks the world is kept, goes on for 20 ticks, and goes back to
    // go on once more, as a client that predicted does
    for (int from = 0; from < TICKS; from += 37) {
        const int to = from + 37 < TICKS ? from + 37 : TICKS;
        tide_world_copy(&kept, &a);
        const bool quiet = a.Script.quiet;
        play(&a, again, from, from + 20 < TICKS ? from + 20 : TICKS);
        tide_world_copy(&a, &kept);
        a.Script.quiet = quiet;
        play(&a, again, from, to);
        for (int i = from; i < to; i++) {
            if (again[i] != went[i]) printf("    tick %d came out different\n", i);
            TIDE_REQUIRE(again[i] == went[i]);
        }
    }
    TIDE_CHECK(b2t_restores() > 15);
    tide_world_free(&a);
    tide_world_free(&kept);
}

#ifdef BOX2D_TEST_JOBS
// With the step shared out over the tick's threads while images are written
// in parts: what's said to have changed is said on one thread, and the step's
// workers say nothing.
TIDE_TEST(box2d_parts_on_threads)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    b2t_forget();
    b2t_check(true, true);
    tide_world_init(&a, DT);
    play(&a, went, 0, TICKS);
    tide_world_free(&a);

    b2t_forget();
    b2t_share_from(4); // A step with four bodies awake is shared out
    const int32_t before = b2t_shared_steps();
    int32_t whole = 0;
    int32_t in_parts = 0;
    images_since(&whole, &in_parts);
    tide_world_init(&a, DT);
    for (int i = 0; i < TICKS; i++) {
        if (i == QUIET) a.Script.quiet = true;
        tide_world_tick_on(&a, jobs);
        if (tide_world_hash(&a) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&a) == went[i]);
    }
    images_since(&whole, &in_parts);
    printf("    %d steps were shared out over %u threads, with %d images in parts\n", (int)(b2t_shared_steps() - before),
           (unsigned)jobs->threads, (int)in_parts);
    TIDE_CHECK(b2t_shared_steps() - before > 300 && in_parts > 500);
    b2t_share_from(0);
    tide_world_free(&a);
}
#endif

// ---------------------------------------------------------------------------
// Box2D by itself

enum { BODIES = 120, JOINTS = 24, SHELF = 41, SHELVES = 8 };

typedef struct lab {
    b2WorldId world;
    uint32_t random;
    b2BodyId bodies[BODIES];
    b2JointId joints[JOINTS];
    int bases[B2_SNAPSHOT_SECTIONS];
    uint8_t *image; // What a match's list would hold: the world's last image
    int size;
    int room;
    uint8_t *whole; // The image written whole, to compare
    int whole_room;
    // The world as it was a while ago, to go back to: its image and whose its bodies were
    uint8_t *old_image;
    int old_size;
    int old_room;
    int old_bases[B2_SNAPSHOT_SECTIONS];
    b2BodyId old_bodies[BODIES];
    b2JointId old_joints[JOINTS];
    bool sleeping; // b2World_EnableSleeping
    bool recorded; // Box2D is recording what's done: the world isn't made again from an image meanwhile
    bool restored_now; // ...and it was, since the last step
    int images_whole;
    int images_in_parts;
    int restored;
    int step;
    char last[64]; // What was done last, for what a failure says
} lab;

static const lab *running;

// What Box2D says when it finds itself wrong, with what was done to it last
static int stopped(const char *condition, const char *file, const int line)
{
    fprintf(stderr, "Box2D stopped: %s, %s line %d", condition, file, line);
    if (running) fprintf(stderr, ", at step %d after %s", running->step, running->last);
    fprintf(stderr, "\n");
    fflush(stderr);
    return 1;
}

static uint32_t roll(lab *l, const uint32_t under)
{
    l->random ^= l->random << 13;
    l->random ^= l->random >> 17;
    l->random ^= l->random << 5;
    return l->random % under;
}

static float between(lab *l, const float low, const float high)
{
    return low + (high - low) * (float)roll(l, 10001) / 10000.0f;
}

// As the bridge places an image's sections (place, in bridge.c)
static int place(int *bases, const int *sizes, bool *moved)
{
    int end = b2GetSnapshotHead();
    *moved = false;
    for (int i = 0; i < B2_SNAPSHOT_SECTIONS; i++) {
        const int before = i ? sizes[i - 1] : 0;
        if (bases[i] < end || bases[i] > end + before / 4 + 256) {
            bases[i] = i ? end + before / 8 + 64 : end;
            *moved = true;
        }
        end = bases[i] + sizes[i];
    }
    return end;
}

static void room_for(uint8_t **bytes, int *room, const int size)
{
    if (size <= *room) return;
    *bytes = realloc(*bytes, (size_t)size);
    memset(*bytes + *room, 0, (size_t)(size - *room));
    *room = size;
}

// The world's image, as a caller takes it: what changed of the last one where
// Box2D can say, written over the last one, and all of it otherwise. Then all
// of it once more, to compare. False when the two aren't the same bytes.
static bool take_image(lab *l)
{
    int sizes[B2_SNAPSHOT_SECTIONS] = {0};
    const b2SnapshotPart *parts = NULL;
    int count = 0;
    int size = l->size ? b2World_GetSnapshotParts(l->world, l->bases, sizes, &parts, &count) : 0;
    bool moved = false;
    if (size != 0) place(l->bases, sizes, &moved);
    if (size > 0 && !moved) {
        room_for(&l->image, &l->room, size);
        if (size > l->size) memset(l->image + l->size, 0, (size_t)(size - l->size)); // As a list that grows
        for (int i = 0; i < count; i++) {
            if (parts[i].offset < 0 || parts[i].offset + parts[i].size > size) {
                printf("    a part is outside its image: %d bytes at %d of %d\n", parts[i].size, parts[i].offset, size);
                return false;
            }
            memcpy(l->image + parts[i].offset, parts[i].bytes, (size_t)parts[i].size);
        }
        l->images_in_parts++;
    } else {
        size = b2World_GetSnapshotAt(l->world, l->room ? l->image : NULL, l->room, l->bases, sizes);
        const int after = place(l->bases, sizes, &moved);
        if (moved || size > l->room) {
            if (moved) size = after;
            room_for(&l->image, &l->room, size);
            size = b2World_GetSnapshotAt(l->world, l->image, l->room, l->bases, sizes);
        }
        if (size <= 0) {
            printf("    the world has no image\n");
            return false;
        }
        l->images_whole++;
    }
    l->size = size;

    room_for(&l->whole, &l->whole_room, size);
    const int whole = b2World_CheckSnapshotAt(l->world, l->whole, l->whole_room, l->bases, sizes);
    if (whole != size) {
        printf("    step %d, after %s: the image in parts is %d bytes, and written whole %d\n", l->step, l->last, size, whole);
        return false;
    }
    if (memcmp(l->whole, l->image, (size_t)size) == 0) return true;
    int first = 0;
    int apart = 0;
    for (int at = size - 1; at >= 0; at--) {
        if (l->whole[at] == l->image[at]) continue;
        first = at;
        apart++;
    }
    int section = -1;
    for (int i = 0; i < B2_SNAPSHOT_SECTIONS; i++) {
        if (l->bases[i] <= first) section = i;
    }
    const int in = section < 0 ? first : first - l->bases[section];
    const int record = section < 0 ? 0 : b2GetSnapshotRecordSize(section);
    printf("    step %d, after %s: the image in parts isn't the image written whole. %d bytes apart, the first at %d: byte %d of section "
           "%d, %02x whole and %02x in parts",
           l->step, l->last, apart, first, in, section, l->whole[first], l->image[first]);
    if (record && in >= 4) printf(" (byte %d of number %d)", (in - 4) % record, (in - 4) / record);
    printf("\n");
    return false;
}

static void did(lab *l, const char *what, const int on)
{
    snprintf(l->last, sizeof l->last, "%s %d", what, on);
}

// One of the bodies there are, or -1
static int pick(lab *l)
{
    const int from = (int)roll(l, BODIES);
    for (int i = 0; i < BODIES; i++) {
        const int at = (from + i) % BODIES;
        if (b2Body_IsValid(l->bodies[at])) return at;
    }
    return -1;
}

static int spare(const lab *l)
{
    for (int i = 0; i < BODIES; i++) {
        if (!b2Body_IsValid(l->bodies[i])) return i;
    }
    return -1;
}

static void add_shape(lab *l, const b2BodyId body, const bool may_sense)
{
    b2ShapeDef def = b2DefaultShapeDef();
    // A static body has no mass to work out again, so its shapes can leave that for later, which is another
    // way through Box2D
    def.updateBodyMass = b2Body_GetType(body) != b2_staticBody || roll(l, 2) == 0;
    def.density = between(l, 0.2f, 2.0f);
    def.material.friction = between(l, 0.2f, 0.9f);
    def.material.restitution = roll(l, 4) == 0 ? 0.4f : 0.0f;
    def.enableContactEvents = roll(l, 2) == 0;
    def.enableSensorEvents = roll(l, 2) == 0;
    def.enableHitEvents = roll(l, 4) == 0;
    def.isSensor = may_sense && roll(l, 6) == 0;
    const float x = between(l, -0.3f, 0.3f);
    const float y = between(l, -0.3f, 0.3f);
    const float r = between(l, 0.2f, 0.6f);
    switch (roll(l, 4)) {
    case 0: {
        const b2Polygon box = b2MakeOffsetBox(r, 0.5f, (b2Vec2){x, y}, b2Rot_identity);
        b2CreatePolygonShape(body, &def, &box);
        break;
    }
    case 1: {
        const b2Circle ball = {{x, y}, r};
        b2CreateCircleShape(body, &def, &ball);
        break;
    }
    case 2: {
        const b2Capsule pill = {{x, y - 0.4f}, {x, y + 0.4f}, r};
        b2CreateCapsuleShape(body, &def, &pill);
        break;
    }
    default: {
        const b2Segment line = {{x - 0.6f, y}, {x + 0.6f, y + 0.1f}};
        b2CreateSegmentShape(body, &def, &line);
        break;
    }
    }
}

static void spawn(lab *l, const b2BodyType type, const bool awake)
{
    const int at = spare(l);
    if (at < 0) return;
    b2BodyDef def = b2DefaultBodyDef();
    def.type = type;
    const float x = between(l, -5.0f, 5.0f);
    const float y = type == b2_staticBody ? between(l, 0.0f, 3.0f) : between(l, 0.6f, 6.0f);
    def.position = b2ToPos((b2Vec2){x, y});
    def.isAwake = awake;
    def.isBullet = type == b2_dynamicBody && roll(l, 12) == 0;
    def.linearDamping = between(l, 0.0f, 0.5f);
    def.angularDamping = between(l, 0.2f, 0.8f);
    if (type == b2_kinematicBody) {
        const float speed = between(l, -2.0f, 2.0f);
        def.linearVelocity = (b2Vec2){speed, 0.0f};
    }
    l->bodies[at] = b2CreateBody(l->world, &def);
    // Some with no shape at all: nothing then says the body is new but its own making
    if (roll(l, 8) != 0) add_shape(l, l->bodies[at], type == b2_staticBody);
    did(l, awake ? "a body made" : "a body made asleep", at);
}

// Whether a body is where the calls for an awake or sleeping one go: disabled
// bodies have a few of their own
static bool enabled(const lab *l, const int at)
{
    return at >= 0 && b2Body_IsEnabled(l->bodies[at]);
}

// Something a game might do to the world between two steps.
static void act(lab *l)
{
    const int at = pick(l);
    const b2BodyId body = at >= 0 ? l->bodies[at] : (b2BodyId){0};
    const uint32_t what = roll(l, 100);
    const float x = between(l, -5.0f, 5.0f);
    const float y = between(l, 0.6f, 6.0f);
    const float turn = between(l, -1.0f, 1.0f);
    const bool wake = roll(l, 2) == 0;
    if (what < 10) {
        spawn(l, b2_dynamicBody, roll(l, 5) != 0);
    } else if (what < 13) {
        spawn(l, b2_staticBody, true);
    } else if (what < 15) {
        spawn(l, b2_kinematicBody, true);
    } else if (what < 23) {
        if (at < 1) return; // Not the floor
        b2DestroyBody(body);
        did(l, "a body destroyed", at);
    } else if (what < 32) {
        if (at < 1) return;
        b2Body_SetTransform(body, b2ToPos((b2Vec2){x, y}), roll(l, 2) ? b2Rot_identity : b2MakeRot(turn));
        did(l, "a body moved", at);
    } else if (what < 38) {
        if (!enabled(l, at)) return;
        b2Body_SetLinearVelocity(body, roll(l, 2) ? (b2Vec2){0.0f, 0.0f} : (b2Vec2){x, y});
        did(l, "a body's speed set", at);
    } else if (what < 41) {
        if (!enabled(l, at)) return;
        b2Body_SetAngularVelocity(body, roll(l, 2) ? 0.0f : 3.0f * turn);
        did(l, "a body's spin set", at);
    } else if (what < 48) {
        if (!enabled(l, at)) return;
        b2Body_ApplyLinearImpulseToCenter(body, (b2Vec2){0.5f * x, 0.5f * y}, wake);
        did(l, wake ? "a body pushed" : "a body pushed if it's awake", at);
    } else if (what < 51) {
        if (!enabled(l, at)) return;
        switch (roll(l, 4)) {
        case 0: b2Body_ApplyAngularImpulse(body, 0.2f * turn, wake); break;
        case 1: b2Body_ApplyLinearImpulse(body, (b2Vec2){0.4f * x, 0.0f}, b2Body_GetPosition(body), wake); break;
        case 2: b2Body_ApplyForceToCenter(body, (b2Vec2){9.0f * x, 9.0f * y}, wake); break;
        default: b2Body_ApplyTorque(body, 2.0f * turn, wake); break;
        }
        did(l, "a body turned or pulled", at);
    } else if (what < 57) {
        if (!enabled(l, at) || b2Body_GetShapeCount(body) >= 4) return;
        add_shape(l, body, true);
        did(l, "a shape added", at);
    } else if (what < 60) {
        if (!enabled(l, at) || b2Body_GetType(body) != b2_kinematicBody) return;
        const b2WorldTransform target = {b2ToPos((b2Vec2){x, y}), b2Rot_identity};
        b2Body_SetTargetTransform(body, target, 0.5f, wake);
        did(l, "a body sent somewhere", at);
    } else if (what < 63) {
        if (at < 1 || !enabled(l, at) || b2Body_GetJointCount(body) > 0) return;
        b2Body_SetType(body, (b2BodyType)roll(l, 3));
        did(l, "a body's type set", at);
    } else if (what < 66) {
        if (at < 1 || b2Body_GetJointCount(body) > 0) return;
        if (b2Body_IsEnabled(body)) b2Body_Disable(body);
        else b2Body_Enable(body);
        did(l, "a body disabled or enabled", at);
    } else if (what < 69) {
        if (!enabled(l, at) || b2Body_GetType(body) == b2_staticBody) return;
        // Not to sleep right after a restore: Box2D then looks the body up among the last step's events,
        // which a restore clears (b2TrySleepIsland)
        if (!wake && l->restored_now) return;
        if (roll(l, 3) == 0) b2Body_WakeTouching(body);
        else b2Body_SetAwake(body, wake);
        did(l, wake ? "a body woken" : "a body put to sleep, or what it touches woken", at);
    } else if (what < 72) {
        if (at < 1 || !enabled(l, at) || b2Body_GetShapeCount(body) < 2) return;
        b2ShapeId shapes[8];
        const int count = b2Body_GetShapes(body, shapes, 8);
        // With its body weighed again or not: one that isn't says nothing of the body but what the
        // shape's going says itself
        const b2ShapeId gone = shapes[roll(l, (uint32_t)count)];
        const bool weigh = roll(l, 2) == 0;
        if (b2Shape_GetType(gone) == b2_chainSegmentShape) return; // A chain's shapes go with their chain
        b2DestroyShape(gone, weigh);
        did(l, weigh ? "a shape destroyed" : "a shape destroyed, its body not weighed", at);
    } else if (what < 74) {
        if (!enabled(l, at) || b2Body_GetShapeCount(body) < 1) return;
        b2ShapeId shapes[8];
        const int count = b2Body_GetShapes(body, shapes, 8);
        const b2ShapeId shape = shapes[roll(l, (uint32_t)count)];
        if (roll(l, 2)) b2Shape_SetFriction(shape, between(l, 0.1f, 1.0f));
        else b2Shape_SetDensity(shape, between(l, 0.1f, 3.0f), true);
        did(l, "a shape's friction or density set", at);
    } else if (what < 77) {
        const int other = pick(l);
        int slot = -1;
        for (int i = 0; i < JOINTS && slot < 0; i++) {
            if (!b2Joint_IsValid(l->joints[i])) slot = i;
        }
        if (slot < 0 || other < 0 || other == at || !enabled(l, at) || !enabled(l, other)) return;
        if (b2Body_GetType(body) != b2_dynamicBody && b2Body_GetType(l->bodies[other]) != b2_dynamicBody) return;
        b2WeldJointDef def = b2DefaultWeldJointDef();
        def.base.bodyIdA = body;
        def.base.bodyIdB = l->bodies[other];
        def.base.collideConnected = roll(l, 2) == 0;
        def.linearHertz = 2.0f;
        def.angularHertz = 2.0f;
        l->joints[slot] = b2CreateWeldJoint(l->world, &def);
        did(l, "two bodies joined", at);
    } else if (what < 79) {
        const int slot = (int)roll(l, JOINTS);
        if (!b2Joint_IsValid(l->joints[slot])) return;
        b2DestroyJoint(l->joints[slot]);
        did(l, "a joint destroyed", slot);
    } else if (what < 80) {
        l->sleeping = !l->sleeping;
        b2World_EnableSleeping(l->world, l->sleeping);
        did(l, "sleeping turned on or off", l->sleeping);
    } else if (what < 82) {
        b2World_SetGravity(l->world, (b2Vec2){0.0f, -10.0f + 2.0f * turn});
        did(l, "gravity set", 0);
    } else if (what < 84) {
        // A chain: a row of shapes on a body that never moves, which an image has a section of its own for
        const int slot = spare(l);
        if (slot < 0) return;
        b2BodyDef def = b2DefaultBodyDef();
        def.position = b2ToPos((b2Vec2){x, 0.2f});
        l->bodies[slot] = b2CreateBody(l->world, &def);
        const b2Vec2 points[5] = {{-2.0f, 0.0f}, {-1.0f, 0.3f}, {0.0f, 0.1f}, {1.0f, 0.4f}, {2.0f, 0.0f}};
        b2ChainDef chain = b2DefaultChainDef();
        chain.points = points;
        chain.pointCount = 5;
        chain.ghost1 = (b2Vec2){-3.0f, 0.0f};
        chain.ghost2 = (b2Vec2){3.0f, 0.0f};
        b2CreateChain(l->bodies[slot], &chain);
        did(l, "a chain made", slot);
    } else if (what < 86 && l->size > 0 && !l->recorded) {
        // The world made again from its last image, in place: its next image is whole
        if (!b2World_Restore(l->world, l->image, l->size)) {
            printf("    the world's own image doesn't restore\n");
            abort();
        }
        l->restored++;
        l->restored_now = true;
        did(l, "the world restored", 0);
    } else if (what < 88 && l->old_size > 0 && !l->recorded) {
        // ...or from an older one, as a rollback does: the image a match holds is then that one
        if (!b2World_Restore(l->world, l->old_image, l->old_size)) {
            printf("    the world's older image doesn't restore\n");
            abort();
        }
        room_for(&l->image, &l->room, l->old_size);
        memcpy(l->image, l->old_image, (size_t)l->old_size);
        l->size = l->old_size;
        memcpy(l->bases, l->old_bases, sizeof l->bases);
        memcpy(l->bodies, l->old_bodies, sizeof l->bodies);
        memcpy(l->joints, l->old_joints, sizeof l->joints);
        l->restored++;
        l->restored_now = true;
        did(l, "the world rolled back", 0);
    } else if (what < 91) {
        // A shape's geometry set, or what it collides with: calls an image in parts doesn't follow
        if (at < 1 || !enabled(l, at) || b2Body_GetShapeCount(body) < 1) return;
        b2ShapeId shapes[8];
        const int count = b2Body_GetShapes(body, shapes, 8);
        const b2ShapeId shape = shapes[roll(l, (uint32_t)count)];
        if (b2Shape_GetType(shape) == b2_chainSegmentShape) return;
        const float r = between(l, 0.2f, 0.6f);
        switch (roll(l, 3)) {
        case 0: {
            const b2Polygon box = b2MakeBox(r, 0.4f);
            b2Shape_SetPolygon(shape, &box);
            break;
        }
        case 1: {
            const b2Circle ball = {{0.0f, 0.0f}, r};
            b2Shape_SetCircle(shape, &ball);
            break;
        }
        default: {
            b2Filter filter = b2Shape_GetFilter(shape);
            filter.maskBits = roll(l, 2) ? 1u : ~(uint64_t)0;
            b2Shape_SetFilter(shape, filter);
            break;
        }
        }
        did(l, "a shape's geometry or filter set", at);
    } else if (what < 96) {
        // How a body is to be, which is in the body or in what its set has of it: asleep or static as it may be
        if (at < 1) return;
        switch (roll(l, 5)) {
        case 0: b2Body_SetGravityScale(body, between(l, -0.5f, 1.5f)); break;
        case 1: b2Body_SetLinearDamping(body, between(l, 0.0f, 1.0f)); break;
        case 2: b2Body_SetAngularDamping(body, between(l, 0.0f, 1.0f)); break;
        case 3: b2Body_SetBullet(body, roll(l, 2) == 0); break;
        default: b2Body_EnableSleep(body, roll(l, 4) != 0); break;
        }
        did(l, "a body's setting set", at);
    } else if (what < 99) {
        // How the world steps: numbers of its own, which every image writes
        const float one = between(l, 10.0f, 30.0f);
        const float two = between(l, 2.0f, 10.0f);
        const float three = between(l, 1.0f, 3.0f);
        switch (roll(l, 5)) {
        case 0: b2World_SetContactTuning(l->world, one, two, three); break;
        case 1: b2World_SetRestitutionThreshold(l->world, three); break;
        case 2: b2World_SetHitEventThreshold(l->world, three); break;
        case 3: b2World_SetMaximumLinearSpeed(l->world, 100.0f * one); break;
        default: b2World_EnableContinuous(l->world, roll(l, 4) != 0); break;
        }
        did(l, "the world's setting set", 0);
    } else {
        // A blast, which pushes whatever is near it
        b2ExplosionDef blast = b2DefaultExplosionDef();
        blast.position = b2ToPos((b2Vec2){x, y});
        blast.radius = 2.0f;
        blast.falloff = 1.0f;
        blast.impulsePerLength = 1.0f;
        b2World_Explode(l->world, &blast);
        did(l, "a blast", 0);
    }
}

// The world as it is now, to go back to later: the image a match would hold of it
static void keep(lab *l)
{
    room_for(&l->old_image, &l->old_room, l->size);
    memcpy(l->old_image, l->image, (size_t)l->size);
    l->old_size = l->size;
    memcpy(l->old_bases, l->bases, sizeof l->bases);
    memcpy(l->old_bodies, l->bodies, sizeof l->bodies);
    memcpy(l->old_joints, l->joints, sizeof l->joints);
}

// A world of stacks on a floor.
static void begin(lab *l, const uint32_t seed, const int share)
{
    *l = (lab){.random = seed, .sleeping = true};
    running = l;
    b2WorldDef world = b2DefaultWorldDef();
    world.gravity = (b2Vec2){0.0f, -10.0f};
    l->world = b2CreateWorld(&world);
    if (share) b2World_SetSnapshotAwakeShare(l->world, share);

    b2BodyDef floor = b2DefaultBodyDef();
    floor.position = b2ToPos((b2Vec2){0.0f, -0.5f});
    l->bodies[0] = b2CreateBody(l->world, &floor);
    const b2ShapeDef shape = b2DefaultShapeDef();
    const b2Polygon slab = b2MakeBox(100.0f, 0.5f);
    b2CreatePolygonShape(l->bodies[0], &shape, &slab);
    for (int i = 0; i < 40; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_dynamicBody;
        def.position = b2ToPos((b2Vec2){-9.0f + 2.0f * (float)(i / 4), 0.5f + (float)(i % 4)});
        l->bodies[1 + i] = b2CreateBody(l->world, &def);
        const b2Polygon crate = b2MakeBox(0.5f, 0.5f);
        b2CreatePolygonShape(l->bodies[1 + i], &shape, &crate);
    }
    // Shelves away from the rest: with a few static bodies, one that comes or goes doesn't move a section
    // of the image, which would have it written whole
    for (int i = 0; i < SHELVES; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.position = b2ToPos((b2Vec2){30.0f + 4.0f * (float)i, 1.0f});
        l->bodies[SHELF + i] = b2CreateBody(l->world, &def);
        const b2Polygon shelf = b2MakeBox(1.0f, 0.25f);
        b2CreatePolygonShape(l->bodies[SHELF + i], &shape, &shelf);
    }
    did(l, "the world made", 0);
}

// Steps up to `steps`, with something done between them at times: in bursts,
// and then left alone long enough to fall asleep, so most of what's done is
// done to sleeping bodies. An image after every step.
static bool go_on(lab *l, const int steps)
{
    bool right = true;
    for (; l->step < steps && right; l->step++) {
        // 90 steps of every 240 are busy, and the rest nearly quiet
        const bool busy = l->step % 240 >= 150;
        const uint32_t acts = busy ? roll(l, 3) : roll(l, 40) == 0;
        for (uint32_t i = 0; i < acts; i++) {
            act(l);
            // Sleeping stays on but for a few steps, or nothing would be asleep
            if (!l->sleeping && roll(l, 4) == 0) {
                l->sleeping = true;
                b2World_EnableSleeping(l->world, true);
            }
        }
        b2World_Step(l->world, DT, 4);
        l->restored_now = false;
        right = take_image(l);
        // And once more with nothing done: no part of it is another
        if (right && l->step % 97 == 0) right = take_image(l);
        if (right && l->step % 131 == 60) keep(l);
    }
    return right;
}

static void end(lab *l)
{
    running = NULL;
    b2DestroyWorld(l->world);
    free(l->image);
    free(l->whole);
    free(l->old_image);
}

static bool run(lab *l, const uint32_t seed, const int steps, const int share)
{
    begin(l, seed, share);
    const bool right = go_on(l, steps);
    end(l);
    return right;
}

TIDE_TEST(box2d_parts_whatever_is_done_to_the_world)
{
    // The bridge's allocator is Box2D's for the whole program, where it has one
    // (a web build): it's set before any world is made here
    b2t_forget();
    b2t_check(false, false);
    tide_world_init(&a, DT);
    tide_world_tick(&a);
    tide_world_free(&a);
    b2t_forget();

    b2SetAssertFcn(stopped);
    static lab l;
    int whole = 0;
    int in_parts = 0;
    int restored = 0;
    // In parts whenever there can be, and as a match has them, each from a few seeds
    for (uint32_t seed = 1; seed <= 6; seed++) {
        const bool right = run(&l, 2654435761u * seed, 2400, seed % 2);
        whole += l.images_whole;
        in_parts += l.images_in_parts;
        restored += l.restored;
        if (!right) printf("    seed %u\n", (unsigned)seed);
        TIDE_REQUIRE(right);
    }
    printf("    %d images written whole and %d in parts, with the world restored %d times\n", whole, in_parts, restored);
    TIDE_CHECK(in_parts > 4000 && whole > 500 && restored > 20);
}

// A world that's asleep, where what's done takes the places of things that
// went and fits in the room its image has, so that nothing but one line in
// Box2D says what changed: not how many bodies, shapes or contacts there are,
// and not a section of the image that has to move.
TIDE_TEST(box2d_parts_where_only_one_line_says_so)
{
    b2SetAssertFcn(stopped);
    static lab l;
    enum { RESTLESS = 10, LONER = 60, LONERS = 8, NEAR = 70, NEARS = 4 };
    begin(&l, 7u, 1);
    // Bodies that never move, with nothing on them, far off: with these the static set has room in the image
    // for one more or one less, where a shelf that goes would otherwise move the section after it, and the
    // image is then written whole whatever else is so
    for (int i = 0; i < 40; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.position = b2ToPos((b2Vec2){300.0f + (float)i, 0.0f});
        b2CreateBody(l.world, &def);
    }
    // Crates by themselves on the floor, away from the stacks: one that goes, or is moved, wakes no other.
    // Box2D wakes what a body touched when the body goes, and a stack that wakes moves a section of the image
    for (int i = 0; i < LONERS; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_dynamicBody;
        def.position = b2ToPos((b2Vec2){-60.0f - 3.0f * (float)i, 0.5f});
        l.bodies[LONER + i] = b2CreateBody(l.world, &def);
        const b2ShapeDef shape = b2DefaultShapeDef();
        const b2Polygon crate = b2MakeBox(0.5f, 0.5f);
        b2CreatePolygonShape(l.bodies[LONER + i], &shape, &crate);
    }
    // A sensor on each of three shelves
    b2ShapeDef sensing = b2DefaultShapeDef();
    sensing.isSensor = true;
    sensing.enableSensorEvents = true;
    const b2Circle ball = {{0.0f, 1.0f}, 0.5f};
    b2ShapeId gates[3];
    for (int i = 0; i < 3; i++) gates[i] = b2CreateCircleShape(l.bodies[SHELF + i], &sensing, &ball);
    // Crates side by side, a little apart: near enough for Box2D to keep a contact between each two, which
    // touch nothing. Once they sleep those contacts are the disabled set's, with no island and no set of
    // either crate's: nothing wakes when one goes
    for (int i = 0; i < NEARS; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_dynamicBody;
        def.position = b2ToPos((b2Vec2){-92.0f + 1.08f * (float)i, 0.5f});
        l.bodies[NEAR + i] = b2CreateBody(l.world, &def);
        const b2ShapeDef shape = b2DefaultShapeDef();
        const b2Polygon crate = b2MakeBox(0.5f, 0.5f);
        b2CreatePolygonShape(l.bodies[NEAR + i], &shape, &crate);
    }
    // Two more shapes on another shelf, with nothing on them: they go one at a time, while everything
    // sleeps. A body's shapes are a list from the last one made, so the first to go is the one in the middle
    const b2ShapeDef plain = b2DefaultShapeDef();
    const b2ShapeId middle = b2CreateCircleShape(l.bodies[SHELF + 5], &plain, &ball);
    const b2ShapeId head = b2CreateCircleShape(l.bodies[SHELF + 5], &plain, &ball);
    // A few bodies that never sleep, away from the rest: with them the awake set has room for one more
    // body in the image, where one that wakes in a world all asleep moves a section, and the image is
    // then written whole whatever else is so
    for (int i = 0; i < RESTLESS; i++) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_dynamicBody;
        def.enableSleep = false;
        def.position = b2ToPos((b2Vec2){-30.0f - 2.0f * (float)i, 0.5f});
        const b2BodyId body = b2CreateBody(l.world, &def);
        const b2ShapeDef shape = b2DefaultShapeDef();
        const b2Circle round = {{0.0f, 0.0f}, 0.5f};
        b2CreateCircleShape(body, &shape, &round);
    }

    bool right = true;
    for (l.step = 0; l.step < 300 && right; l.step++) {
        // Crates and a shelf go, while everything is asleep: their places are there to take
        if (l.step == 120) {
            b2DestroyBody(l.bodies[LONER]);
            b2DestroyBody(l.bodies[LONER + 1]);
            b2DestroyBody(l.bodies[SHELF + 7]);
            did(&l, "two crates and a shelf destroyed", 0);
        }
        // The first of the crates side by side goes, asleep: its contact with the next one goes with it, the
        // last contact of the disabled set takes its place there, and the next crate, which stays asleep,
        // has one contact less
        if (l.step == 135) {
            TIDE_CHECK(b2Body_GetContactCapacity(l.bodies[NEAR + 1]) == 3 && !b2Body_IsAwake(l.bodies[NEAR + 1]));
            b2DestroyBody(l.bodies[NEAR]);
            TIDE_CHECK(b2Body_GetContactCapacity(l.bodies[NEAR + 1]) == 2 && !b2Body_IsAwake(l.bodies[NEAR + 1]));
            did(&l, "a sleeping crate beside another destroyed", 0);
        }
        // A crate that's asleep is put somewhere else, where nothing is: nothing wakes, and nothing says so
        // but the move itself
        if (l.step == 150) {
            b2Body_SetTransform(l.bodies[LONER + 2], b2ToPos((b2Vec2){-100.0f, 0.5f}), b2Rot_identity);
            did(&l, "a sleeping crate put elsewhere", 0);
        }
        // ...and how a sleeping crate and a shelf are to be
        if (l.step == 160) {
            b2Body_SetLinearDamping(l.bodies[LONER + 3], 0.3f);
            did(&l, "a sleeping crate's damping set", 0);
        }
        if (l.step == 165) {
            b2Body_SetGravityScale(l.bodies[SHELF + 6], 0.5f);
            did(&l, "a shelf's gravity scale set", 0);
        }
        // A shape goes from the middle of a body's list, on a body that never moves and isn't weighed again:
        // the shape, the one before it and the one after it, whose links change, and the body, which counts
        // them. Then the one that heads the list, which the body names
        if (l.step == 180) {
            b2DestroyShape(middle, false);
            did(&l, "a shape destroyed from the middle of its body's", 0);
        }
        if (l.step == 190) {
            b2DestroyShape(head, false);
            did(&l, "the shape that heads its body's destroyed", 0);
        }
        // A shape onto a shelf, and onto a crate that's asleep, which is weighed again and stays asleep
        if (l.step == 200) {
            b2CreateCircleShape(l.bodies[SHELF + 4], &plain, &ball);
            did(&l, "a shape made on a shelf", 0);
        }
        if (l.step == 210) {
            const b2Circle bump = {{0.0f, 0.3f}, 0.2f};
            b2CreateCircleShape(l.bodies[LONER + 4], &plain, &bump);
            did(&l, "a shape made on a sleeping crate", 0);
        }
        // The shelf with the first sensor goes: the last sensor takes its place in Box2D's list, and
        // its shape, on a static body, is told where it is now
        if (l.step == 250) {
            b2DestroyBody(l.bodies[SHELF]);
            did(&l, "a sensor's shelf destroyed", 0);
        }
        // ...and the sensor that's first in the list now goes by itself, its shelf staying: the other's shape
        // is told
        if (l.step == 255) {
            b2DestroyShape(gates[2], false);
            did(&l, "a sensor destroyed", 0);
        }
        // A static body where there was one, under nothing
        if (l.step == 270) {
            b2BodyDef def = b2DefaultBodyDef();
            def.position = b2ToPos((b2Vec2){60.0f, 1.0f});
            l.bodies[SHELF + 7] = b2CreateBody(l.world, &def);
            const b2Polygon shelf = b2MakeBox(1.0f, 0.25f);
            b2CreatePolygonShape(l.bodies[SHELF + 7], &plain, &shelf);
            did(&l, "a shelf made", 0);
        }
        b2World_Step(l.world, DT, 4);
        const int parts = l.images_in_parts;
        right = take_image(&l);
        // Each of those steps had its image in parts: the world was asleep and followed
        if (l.step == 120 || l.step == 135 || l.step == 150 || l.step == 160 || l.step == 165 || l.step == 180 || l.step == 190
            || l.step == 200 || l.step == 210 || l.step == 250 || l.step == 255 || l.step == 270) {
            if (l.images_in_parts == parts) printf("    step %d's image was written whole\n", l.step);
            TIDE_CHECK(l.images_in_parts > parts);
        }
    }
    TIDE_REQUIRE(right);
    TIDE_CHECK(b2World_GetAwakeBodyCount(l.world) == RESTLESS);
    end(&l);
}

// Box2D's own recordings go through the writer the images do, and a world
// that's recorded has its images taken in parts meanwhile: the recording
// replays to the world it was taken of, step for step, and back from the
// images it keeps of its own.
TIDE_TEST(box2d_parts_leave_recordings_as_they_were)
{
    b2SetAssertFcn(stopped);
    static lab l;
    begin(&l, 2654435761u * 3u, 1);
    TIDE_REQUIRE(go_on(&l, 200));
    b2Recording *recording = b2CreateRecording(0);
    b2World_StartRecording(l.world, recording);
    l.recorded = true;
    const bool right = go_on(&l, 900);
    b2World_StopRecording(l.world);
    TIDE_REQUIRE(right);
    TIDE_CHECK(l.images_in_parts > 300);

    const uint8_t *bytes = b2Recording_GetData(recording);
    const int size = b2Recording_GetSize(recording);
    TIDE_CHECK(b2ValidateReplay(bytes, size, 1));
    b2Replay *player = b2CreateReplay(bytes, size, 1);
    TIDE_REQUIRE(player != NULL);
    const int frames = b2Replay_GetInfo(player).frameCount;
    TIDE_CHECK(frames == 700);
    while (b2Replay_StepFrame(player)) {
    }
    TIDE_CHECK(b2Replay_GetFrame(player) == frames && !b2Replay_HasDiverged(player));
    b2Replay_SeekFrame(player, 350); // Back, from an image the player kept
    while (b2Replay_StepFrame(player)) {
    }
    TIDE_CHECK(b2Replay_GetFrame(player) == frames && !b2Replay_HasDiverged(player));
    printf("    a recording of %d steps is %d bytes, and replays\n", frames, size);
    b2DestroyReplay(player);
    end(&l);
    b2DestroyRecording(recording);
}
