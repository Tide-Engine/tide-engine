// Box3D's image written as what changed of the last one, against the image
// written whole: the same bytes, whatever is done to the world. The image is
// in a match's hash and is what a player who joins makes their world from, so
// one byte apart is a match that comes apart.
//
// Twice over: a match (yard.tide) that does to sleeping and static bodies what
// the step doesn't, through the package as a game uses it; and Box3D by
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

#ifdef BOX3D_TEST_JOBS
#include "tide/platform.h"
#endif

// Box3D's headers are its own, as its sources are
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "../box3d/src/box3d/box3d.h"
#pragma clang diagnostic pop

// bridge.c
void b3t_forget(void);
void b3t_check(bool on, bool whenever);
void b3t_images(int32_t *whole, int32_t *in_parts);
void b3t_share_from(int32_t bodies);
int32_t b3t_shared_steps(void);
int32_t b3t_restores(void);

#define DT (1.0f / 60.0f)

enum { TICKS = 760 }; // The script's, and then until everything is asleep

static tide_world a;
static tide_world b;
static tide_world kept;
static uint64_t went[TICKS];

// The yard for `ticks` ticks, with every tick's hash into `hashes`: the script
// stops once it's through, and what's left comes to rest.
static void play(tide_world *w, uint64_t *hashes, const int from, const int ticks)
{
    for (int i = from; i < ticks; i++) {
        if (i == 680) w->Script.quiet = true;
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w); // The image is in it
    }
}

static void images_since(int32_t *whole, int32_t *in_parts)
{
    int32_t now_whole;
    int32_t now_in_parts;
    b3t_images(&now_whole, &now_in_parts);
    const int32_t were_whole = *whole;
    const int32_t were_in_parts = *in_parts;
    *whole = now_whole - were_whole;
    *in_parts = now_in_parts - were_in_parts;
}

// The match comes out the same whether its images are written in parts
// whenever they can be, or as a match has them, whole while most of the world
// is awake: and every image in parts is the image written whole (b3t_check).
TIDE_TEST(box3d_parts_are_the_whole_image)
{
    static uint64_t as_a_match[TICKS];
    for (int usual = 0; usual < 2; usual++) {
        int32_t whole = 0;
        int32_t in_parts = 0;
        images_since(&whole, &in_parts);
        b3t_forget();
        b3t_check(true, !usual);
        tide_world_init(&a, DT);
        play(&a, usual ? as_a_match : went, 0, TICKS);
        images_since(&whole, &in_parts);
        printf("    %s: %d images written whole and %d in parts; %d touches began and %d ended; the image is %d bytes\n",
               usual ? "as a match has them" : "in parts whenever", (int)whole, (int)in_parts, (int)a.Tally.touched, (int)a.Tally.parted,
               (int)a.Box3D_Physics.size);
        // What the script is for happened: bodies came and went, and things touched
        TIDE_CHECK(a.Box3D_Physics.bodies == 38 && a.Tally.touched > 30 && a.Tally.parted > 5);
        TIDE_CHECK(in_parts > (usual ? 300 : 500) && whole > 0);
        // ...and it all came to rest: the last ticks stepped nothing
        TIDE_CHECK(!a.Box3D_Physics.moved && went[TICKS - 1] != 0);
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
TIDE_TEST(box3d_parts_leave_the_list_as_a_whole_image_does)
{
    b3t_forget();
    b3t_check(true, true);
    tide_world_init(&a, DT);
    play(&a, went, 0, TICKS);
    tide_world_init(&b, DT);
    int32_t whole = 0;
    int32_t in_parts = 0;
    images_since(&whole, &in_parts);
    for (int i = 0; i < TICKS; i++) {
        if (i == 680) b.Script.quiet = true;
        b3t_forget();
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
TIDE_TEST(box3d_parts_roll_back)
{
    b3t_forget();
    b3t_check(true, true);
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
    TIDE_CHECK(b3t_restores() > 15);
    tide_world_free(&a);
    tide_world_free(&kept);
}

#ifdef BOX3D_TEST_JOBS
// With the step shared out over the tick's threads while images are written
// in parts: what's said to have changed is said on one thread, and the step's
// workers say nothing.
TIDE_TEST(box3d_parts_on_threads)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    b3t_forget();
    b3t_check(true, true);
    tide_world_init(&a, DT);
    play(&a, went, 0, TICKS);
    tide_world_free(&a);

    b3t_forget();
    b3t_share_from(4); // A step with four bodies awake is shared out
    const int32_t before = b3t_shared_steps();
    int32_t whole = 0;
    int32_t in_parts = 0;
    images_since(&whole, &in_parts);
    tide_world_init(&a, DT);
    for (int i = 0; i < TICKS; i++) {
        if (i == 680) a.Script.quiet = true;
        tide_world_tick_on(&a, jobs);
        if (tide_world_hash(&a) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&a) == went[i]);
    }
    images_since(&whole, &in_parts);
    printf("    %d steps were shared out over %u threads, with %d images in parts\n", (int)(b3t_shared_steps() - before),
           (unsigned)jobs->threads, (int)in_parts);
    TIDE_CHECK(b3t_shared_steps() - before > 300 && in_parts > 500);
    b3t_share_from(0);
    tide_world_free(&a);
}
#endif

// ---------------------------------------------------------------------------
// Box3D by itself

enum { BODIES = 120, JOINTS = 24, SHELF = 41, SHELVES = 8 };

typedef struct lab {
    b3WorldId world;
    uint32_t random;
    b3BodyId bodies[BODIES];
    bool terrain[BODIES]; // It has a mesh or a height field
    b3JointId joints[JOINTS];
    int bases[B3_SNAPSHOT_SECTIONS];
    uint8_t *image; // What a match's list would hold: the world's last image
    int size;
    int room;
    uint8_t *whole; // The image written whole, to compare
    int whole_room;
    uint8_t *geometry;
    int geometry_size;
    // The world as it was a while ago, to go back to: its image, its geometry and whose its bodies were
    uint8_t *old_image;
    int old_size;
    int old_room;
    uint8_t *old_geometry;
    int old_geometry_size;
    int old_geometry_room;
    int old_bases[B3_SNAPSHOT_SECTIONS];
    b3BodyId old_bodies[BODIES];
    bool old_terrain[BODIES];
    b3JointId old_joints[JOINTS];
    bool sleeping; // b3World_EnableSleeping
    bool recorded; // Box3D is recording what's done: the world isn't made again from an image meanwhile
    bool restored_now; // ...and it was, since the last step
    int images_whole;
    int images_in_parts;
    int restored;
    int step;
    char last[64]; // What was done last, for what a failure says
} lab;

static b3MeshData *mesh;
static b3HeightFieldData *field;
static const lab *running;

// What Box3D says when it finds itself wrong, with what was done to it last
static int stopped(const char *condition, const char *file, const int line)
{
    fprintf(stderr, "Box3D stopped: %s, %s line %d", condition, file, line);
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
    int end = b3GetSnapshotHead();
    *moved = false;
    for (int i = 0; i < B3_SNAPSHOT_SECTIONS; i++) {
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
// Box3D can say, written over the last one, and all of it otherwise. Then all
// of it once more, to compare. False when the two aren't the same bytes.
static bool take_image(lab *l)
{
    int sizes[B3_SNAPSHOT_SECTIONS] = {0};
    const b3SnapshotPart *parts = NULL;
    int count = 0;
    int size = l->size ? b3World_GetSnapshotParts(l->world, l->bases, sizes, &parts, &count) : 0;
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
        size = b3World_GetSnapshotAt(l->world, l->room ? l->image : NULL, l->room, l->bases, sizes);
        const int after = place(l->bases, sizes, &moved);
        if (moved || size > l->room) {
            if (moved) size = after;
            room_for(&l->image, &l->room, size);
            size = b3World_GetSnapshotAt(l->world, l->image, l->room, l->bases, sizes);
        }
        if (size <= 0) {
            printf("    the world has no image\n");
            return false;
        }
        l->images_whole++;
    }
    l->size = size;

    room_for(&l->whole, &l->whole_room, size);
    const int whole = b3World_CheckSnapshotAt(l->world, l->whole, l->whole_room, l->bases, sizes);
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
    for (int i = 0; i < B3_SNAPSHOT_SECTIONS; i++) {
        if (l->bases[i] <= first) section = i;
    }
    printf("    step %d, after %s: the image in parts isn't the image written whole. %d bytes apart, the first at %d: byte %d of section "
           "%d, %02x whole and %02x in parts\n",
           l->step, l->last, apart, first, section < 0 ? first : first - l->bases[section], section, l->whole[first], l->image[first]);
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
        if (b3Body_IsValid(l->bodies[at])) return at;
    }
    return -1;
}

static int spare(const lab *l)
{
    for (int i = 0; i < BODIES; i++) {
        if (!b3Body_IsValid(l->bodies[i])) return i;
    }
    return -1;
}

static void add_shape(lab *l, const b3BodyId body, const bool may_sense)
{
    b3ShapeDef def = b3DefaultShapeDef();
    // A static body has no mass to work out again, so its shapes can leave that for later, which is another
    // way through Box3D
    def.updateBodyMass = b3Body_GetType(body) != b3_staticBody || roll(l, 2) == 0;
    def.density = between(l, 200.0f, 2000.0f);
    def.baseMaterial.friction = between(l, 0.2f, 0.9f);
    def.enableContactEvents = roll(l, 2) == 0;
    def.enableSensorEvents = roll(l, 2) == 0;
    def.isSensor = may_sense && roll(l, 6) == 0;
    // A material array of its own, which a shape of any kind can have, and which an image has after the shapes
    b3SurfaceMaterial two[2] = {def.baseMaterial, def.baseMaterial};
    two[1].friction = 0.1f;
    if (roll(l, 12) == 0) {
        def.materials = two;
        def.materialCount = 2;
    }
    const float x = between(l, -0.3f, 0.3f);
    const float y = between(l, -0.3f, 0.3f);
    const float z = between(l, -0.3f, 0.3f);
    const float r = between(l, 0.2f, 0.6f);
    switch (roll(l, 3)) {
    case 0: {
        const b3BoxHull hull = b3MakeOffsetBoxHull(r, 0.5f, 0.5f, (b3Vec3){x, y, z});
        b3CreateHullShape(body, &def, &hull.base);
        break;
    }
    case 1: {
        const b3Sphere ball = {{x, y, z}, r};
        b3CreateSphereShape(body, &def, &ball);
        break;
    }
    default: {
        const b3Capsule pill = {{x, y - 0.4f, z}, {x, y + 0.4f, z}, r};
        b3CreateCapsuleShape(body, &def, &pill);
        break;
    }
    }
}

static void spawn(lab *l, const b3BodyType type, const bool awake)
{
    const int at = spare(l);
    if (at < 0) return;
    b3BodyDef def = b3DefaultBodyDef();
    def.type = type;
    const float x = between(l, -5.0f, 5.0f);
    const float y = type == b3_staticBody ? between(l, 0.0f, 3.0f) : between(l, 0.6f, 6.0f);
    const float z = between(l, -5.0f, 5.0f);
    def.position = b3ToPos((b3Vec3){x, y, z});
    def.isAwake = awake;
    def.isBullet = type == b3_dynamicBody && roll(l, 12) == 0;
    def.linearDamping = between(l, 0.0f, 0.5f);
    def.angularDamping = between(l, 0.2f, 0.8f);
    if (type == b3_kinematicBody) {
        const float speed = between(l, -2.0f, 2.0f);
        def.linearVelocity = (b3Vec3){speed, 0.0f, 0.0f};
    }
    l->bodies[at] = b3CreateBody(l->world, &def);
    l->terrain[at] = false;
    // Some with no shape at all: nothing then says the body is new but its own making
    if (roll(l, 8) != 0) add_shape(l, l->bodies[at], type == b3_staticBody);
    did(l, awake ? "a body made" : "a body made asleep", at);
}

// Whether a body is where the calls for an awake or sleeping one go: disabled
// bodies have a few of their own
static bool enabled(const lab *l, const int at)
{
    return at >= 0 && b3Body_IsEnabled(l->bodies[at]);
}

// Something a game might do to the world between two steps.
static void act(lab *l)
{
    const int at = pick(l);
    const b3BodyId body = at >= 0 ? l->bodies[at] : (b3BodyId){0};
    const uint32_t what = roll(l, 100);
    const float x = between(l, -5.0f, 5.0f);
    const float y = between(l, 0.6f, 6.0f);
    const float z = between(l, -5.0f, 5.0f);
    const bool wake = roll(l, 2) == 0;
    if (what < 10) {
        spawn(l, b3_dynamicBody, roll(l, 5) != 0);
    } else if (what < 13) {
        spawn(l, b3_staticBody, true);
    } else if (what < 15) {
        spawn(l, b3_kinematicBody, true);
    } else if (what < 23) {
        if (at < 1) return; // Not the floor
        b3DestroyBody(body);
        did(l, "a body destroyed", at);
    } else if (what < 32) {
        if (at < 1) return;
        const b3Quat turn = roll(l, 2) ? b3Quat_identity : b3MakeQuatFromAxisAngle((b3Vec3){0.0f, 1.0f, 0.0f}, between(l, -1.0f, 1.0f));
        b3Body_SetTransform(body, b3ToPos((b3Vec3){x, y, z}), turn);
        did(l, "a body moved", at);
    } else if (what < 38) {
        if (!enabled(l, at)) return;
        b3Body_SetLinearVelocity(body, roll(l, 2) ? (b3Vec3){0.0f, 0.0f, 0.0f} : (b3Vec3){x, y, z});
        did(l, "a body's speed set", at);
    } else if (what < 41) {
        if (!enabled(l, at)) return;
        b3Body_SetAngularVelocity(body, roll(l, 2) ? (b3Vec3){0.0f, 0.0f, 0.0f} : (b3Vec3){x, 0.0f, z});
        did(l, "a body's spin set", at);
    } else if (what < 48) {
        if (!enabled(l, at)) return;
        b3Body_ApplyLinearImpulseToCenter(body, (b3Vec3){300.0f * x, 300.0f * y, 300.0f * z}, wake);
        did(l, wake ? "a body pushed" : "a body pushed if it's awake", at);
    } else if (what < 51) {
        if (!enabled(l, at)) return;
        switch (roll(l, 4)) {
        case 0: b3Body_ApplyAngularImpulse(body, (b3Vec3){20.0f * x, 20.0f * y, 20.0f * z}, wake); break;
        case 1: b3Body_ApplyLinearImpulse(body, (b3Vec3){200.0f * x, 0.0f, 200.0f * z}, b3Body_GetPosition(body), wake); break;
        case 2: b3Body_ApplyForceToCenter(body, (b3Vec3){900.0f * x, 900.0f * y, 0.0f}, wake); break;
        default: b3Body_ApplyTorque(body, (b3Vec3){0.0f, 90.0f * y, 0.0f}, wake); break;
        }
        did(l, "a body turned or pulled", at);
    } else if (what < 57) {
        if (!enabled(l, at) || b3Body_GetShapeCount(body) >= 4) return;
        add_shape(l, body, true);
        did(l, "a shape added", at);
    } else if (what < 60) {
        if (!enabled(l, at) || b3Body_GetType(body) != b3_kinematicBody) return;
        const b3WorldTransform target = {b3ToPos((b3Vec3){x, y, z}), b3Quat_identity};
        b3Body_SetTargetTransform(body, target, 0.5f, wake);
        did(l, "a body sent somewhere", at);
    } else if (what < 63) {
        // Not a body with a height field: Box3D refuses, and leaves its world locked (b3Body_SetType)
        if (at < 1 || !enabled(l, at) || b3Body_GetJointCount(body) > 0 || l->terrain[at]) return;
        b3Body_SetType(body, (b3BodyType)roll(l, 3));
        did(l, "a body's type set", at);
    } else if (what < 66) {
        if (at < 1 || b3Body_GetJointCount(body) > 0) return;
        if (b3Body_IsEnabled(body)) b3Body_Disable(body);
        else b3Body_Enable(body);
        did(l, "a body disabled or enabled", at);
    } else if (what < 69) {
        if (!enabled(l, at) || b3Body_GetType(body) == b3_staticBody) return;
        // Not to sleep right after a restore: Box3D then looks the body up among the last step's events,
        // which a restore clears (b3TrySleepIsland)
        if (!wake && l->restored_now) return;
        b3Body_SetAwake(body, wake);
        did(l, wake ? "a body woken" : "a body put to sleep", at);
    } else if (what < 72) {
        if (at < 1 || !enabled(l, at) || b3Body_GetShapeCount(body) < 2) return;
        b3ShapeId shapes[8];
        const int count = b3Body_GetShapes(body, shapes, 8);
        b3DestroyShape(shapes[roll(l, (uint32_t)count)], true);
        did(l, "a shape destroyed", at);
    } else if (what < 74) {
        if (!enabled(l, at) || b3Body_GetShapeCount(body) < 1) return;
        b3ShapeId shapes[8];
        const int count = b3Body_GetShapes(body, shapes, 8);
        const b3ShapeId shape = shapes[roll(l, (uint32_t)count)];
        if (roll(l, 2)) b3Shape_SetFriction(shape, between(l, 0.1f, 1.0f));
        else b3Shape_SetDensity(shape, between(l, 100.0f, 3000.0f), true);
        did(l, "a shape's friction or density set", at);
    } else if (what < 77) {
        const int other = pick(l);
        int slot = -1;
        for (int i = 0; i < JOINTS && slot < 0; i++) {
            if (!b3Joint_IsValid(l->joints[i])) slot = i;
        }
        if (slot < 0 || other < 0 || other == at || !enabled(l, at) || !enabled(l, other)) return;
        if (b3Body_GetType(body) != b3_dynamicBody && b3Body_GetType(l->bodies[other]) != b3_dynamicBody) return;
        b3WeldJointDef def = b3DefaultWeldJointDef();
        def.base.bodyIdA = body;
        def.base.bodyIdB = l->bodies[other];
        def.base.collideConnected = roll(l, 2) == 0;
        def.linearHertz = 2.0f;
        def.angularHertz = 2.0f;
        l->joints[slot] = b3CreateWeldJoint(l->world, &def);
        did(l, "two bodies joined", at);
    } else if (what < 79) {
        const int slot = (int)roll(l, JOINTS);
        if (!b3Joint_IsValid(l->joints[slot])) return;
        b3DestroyJoint(l->joints[slot], wake);
        did(l, "a joint destroyed", slot);
    } else if (what < 80) {
        l->sleeping = !l->sleeping;
        b3World_EnableSleeping(l->world, l->sleeping);
        did(l, "sleeping turned on or off", l->sleeping);
    } else if (what < 82) {
        b3World_SetGravity(l->world, (b3Vec3){0.0f, -10.0f + between(l, -2.0f, 2.0f), 0.0f});
        did(l, "gravity set", 0);
    } else if (what < 84) {
        // A mesh or a height field, which have more in an image than other shapes
        const int slot = spare(l);
        if (slot < 0) return;
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_staticBody;
        def.position = b3ToPos((b3Vec3){x, 0.2f, z});
        l->bodies[slot] = b3CreateBody(l->world, &def);
        l->terrain[slot] = true;
        const b3ShapeDef shape = b3DefaultShapeDef();
        if (roll(l, 2)) b3CreateMeshShape(l->bodies[slot], &shape, mesh, (b3Vec3){1.0f, 1.0f, 1.0f});
        else b3CreateHeightFieldShape(l->bodies[slot], &shape, field);
        did(l, "a mesh or a height field made", slot);
    } else if (what < 86 && l->size > 0 && !l->recorded) {
        // The world made again from its last image, in place: its next image is whole
        const int needed = b3World_GetSnapshotGeometry(l->world, NULL, 0);
        room_for(&l->geometry, &l->geometry_size, needed);
        b3World_GetSnapshotGeometry(l->world, l->geometry, needed);
        if (!b3World_Restore(l->world, l->image, l->size, l->geometry, needed)) {
            printf("    the world's own image doesn't restore\n");
            abort();
        }
        l->restored++;
        l->restored_now = true;
        did(l, "the world restored", 0);
    } else if (what < 88 && l->old_size > 0 && !l->recorded) {
        // ...or from an older one, as a rollback does: the image a match holds is then that one
        if (!b3World_Restore(l->world, l->old_image, l->old_size, l->old_geometry, l->old_geometry_size)) {
            printf("    the world's older image doesn't restore\n");
            abort();
        }
        room_for(&l->image, &l->room, l->old_size);
        memcpy(l->image, l->old_image, (size_t)l->old_size);
        l->size = l->old_size;
        memcpy(l->bases, l->old_bases, sizeof l->bases);
        memcpy(l->bodies, l->old_bodies, sizeof l->bodies);
        memcpy(l->terrain, l->old_terrain, sizeof l->terrain);
        memcpy(l->joints, l->old_joints, sizeof l->joints);
        l->restored++;
        l->restored_now = true;
        did(l, "the world rolled back", 0);
    } else if (what < 91) {
        // A shape's geometry set: a call Box3D records by hand, or another shape altogether
        if (at < 1 || !enabled(l, at) || l->terrain[at] || b3Body_GetShapeCount(body) < 1) return;
        b3ShapeId shapes[8];
        const int count = b3Body_GetShapes(body, shapes, 8);
        const b3ShapeId shape = shapes[roll(l, (uint32_t)count)];
        const float r = between(l, 0.2f, 0.6f);
        switch (roll(l, b3Body_GetType(body) == b3_staticBody ? 4 : 3)) {
        case 0: {
            const b3BoxHull hull = b3MakeBoxHull(r, 0.4f, 0.5f);
            b3Shape_SetHull(shape, &hull.base);
            break;
        }
        case 1: {
            const b3Sphere ball = {{0.0f, 0.0f, 0.0f}, r};
            b3Shape_SetSphere(shape, &ball);
            break;
        }
        case 2: {
            const b3Capsule pill = {{0.0f, -0.3f, 0.0f}, {0.0f, 0.3f, 0.0f}, r};
            b3Shape_SetCapsule(shape, &pill);
            break;
        }
        default:
            b3Shape_SetMesh(shape, mesh, (b3Vec3){1.0f, 1.0f, 1.0f});
            l->terrain[at] = true;
            break;
        }
        did(l, "a shape's geometry set", at);
    }
}

// The world as it is now, to go back to later: the image a match would hold of it, and its geometry
static void keep(lab *l)
{
    room_for(&l->old_image, &l->old_room, l->size);
    memcpy(l->old_image, l->image, (size_t)l->size);
    l->old_size = l->size;
    l->old_geometry_size = b3World_GetSnapshotGeometry(l->world, NULL, 0);
    room_for(&l->old_geometry, &l->old_geometry_room, l->old_geometry_size);
    b3World_GetSnapshotGeometry(l->world, l->old_geometry, l->old_geometry_size);
    memcpy(l->old_bases, l->bases, sizeof l->bases);
    memcpy(l->old_bodies, l->bodies, sizeof l->bodies);
    memcpy(l->old_terrain, l->terrain, sizeof l->terrain);
    memcpy(l->old_joints, l->joints, sizeof l->joints);
}

// A world of stacks on a floor.
static void begin(lab *l, const uint32_t seed, const int share)
{
    *l = (lab){.random = seed, .sleeping = true};
    running = l;
    b3WorldDef world = b3DefaultWorldDef();
    world.gravity = (b3Vec3){0.0f, -10.0f, 0.0f};
    l->world = b3CreateWorld(&world);
    if (share) b3World_SetSnapshotAwakeShare(l->world, share);

    b3BodyDef floor = b3DefaultBodyDef();
    floor.position = b3ToPos((b3Vec3){0.0f, -0.5f, 0.0f});
    l->bodies[0] = b3CreateBody(l->world, &floor);
    const b3ShapeDef shape = b3DefaultShapeDef();
    const b3BoxHull slab = b3MakeBoxHull(100.0f, 0.5f, 100.0f);
    b3CreateHullShape(l->bodies[0], &shape, &slab.base);
    for (int i = 0; i < 40; i++) {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_dynamicBody;
        def.position = b3ToPos((b3Vec3){-4.0f + 2.0f * (float)(i / 4 % 5), 0.5f + (float)(i % 4), -2.0f + 4.0f * (float)(i / 20)});
        l->bodies[1 + i] = b3CreateBody(l->world, &def);
        const b3BoxHull crate = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
        b3CreateHullShape(l->bodies[1 + i], &shape, &crate.base);
    }
    // Shelves away from the rest: with a few static bodies, one that comes or goes doesn't move a section
    // of the image, which would have it written whole
    for (int i = 0; i < SHELVES; i++) {
        b3BodyDef def = b3DefaultBodyDef();
        def.position = b3ToPos((b3Vec3){30.0f + 4.0f * (float)i, 1.0f, 0.0f});
        l->bodies[SHELF + i] = b3CreateBody(l->world, &def);
        const b3BoxHull shelf = b3MakeBoxHull(1.0f, 0.25f, 1.0f);
        b3CreateHullShape(l->bodies[SHELF + i], &shape, &shelf.base);
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
                b3World_EnableSleeping(l->world, true);
            }
        }
        b3World_Step(l->world, DT, 4);
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
    b3DestroyWorld(l->world);
    free(l->image);
    free(l->whole);
    free(l->geometry);
    free(l->old_image);
    free(l->old_geometry);
}

static bool run(lab *l, const uint32_t seed, const int steps, const int share)
{
    begin(l, seed, share);
    const bool right = go_on(l, steps);
    end(l);
    return right;
}

TIDE_TEST(box3d_parts_whatever_is_done_to_the_world)
{
    // The bridge's allocator is Box3D's for the whole program, where it has one
    // (a web build): it's set before any world is made here
    b3t_forget();
    b3t_check(false, false);
    tide_world_init(&a, DT);
    tide_world_tick(&a);
    tide_world_free(&a);
    b3t_forget();

    b3SetAssertFcn(stopped);
    mesh = b3CreateBoxMesh((b3Vec3){0.0f, 0.0f, 0.0f}, (b3Vec3){1.5f, 0.3f, 1.5f}, false);
    field = b3CreateGrid(6, 6, (b3Vec3){0.6f, 0.3f, 0.6f}, false);
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
    b3DestroyMesh(mesh);
    b3DestroyHeightField(field);
}

// A world that's asleep, where what's done takes the places of things that
// went and fits in the room its image has, so that nothing but one line in
// Box3D says what changed: not how many bodies, shapes or contacts there are,
// and not a section of the image that has to move. With a shape that has a
// material array of its own, or with a mesh: each in a world that had neither,
// as either has the image written whole once, which finds the other.
static void where_one_line_says_so(const bool with_mesh)
{
    static lab l;
    enum { RESTLESS = 10 };
    begin(&l, 7u, 1);
    // A sensor on each of three shelves
    b3ShapeDef sensing = b3DefaultShapeDef();
    sensing.isSensor = true;
    sensing.enableSensorEvents = true;
    const b3Sphere ball = {{0.0f, 1.0f, 0.0f}, 0.5f};
    for (int i = 0; i < 3; i++) b3CreateSphereShape(l.bodies[SHELF + i], &sensing, &ball);
    // A few bodies that never sleep, away from the rest: with them the awake set has room for one more
    // body in the image, where one that wakes in a world all asleep moves a section, and the image is
    // then written whole whatever else is so
    for (int i = 0; i < RESTLESS; i++) {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_dynamicBody;
        def.enableSleep = false;
        def.position = b3ToPos((b3Vec3){-20.0f - 2.0f * (float)i, 0.5f, 0.0f});
        const b3BodyId body = b3CreateBody(l.world, &def);
        const b3ShapeDef shape = b3DefaultShapeDef();
        b3CreateSphereShape(body, &shape, &(b3Sphere){{0.0f, 0.0f, 0.0f}, 0.5f});
    }

    bool right = true;
    for (l.step = 0; l.step < 300 && right; l.step++) {
        // Crates and a shelf go, while everything is asleep: their places are there to take
        if (l.step == 120) {
            for (int i = 0; i < 4; i++) b3DestroyBody(l.bodies[4 + 8 * i]); // The tops of four stacks
            b3DestroyBody(l.bodies[SHELF + 7]);
            did(&l, "four crates and a shelf destroyed", 0);
        }
        // The shelf with the first sensor goes: the last sensor takes its place in Box3D's list, and
        // its shape, on a static body, is told where it is now
        if (l.step == 250) {
            b3DestroyBody(l.bodies[SHELF]);
            did(&l, "a sensor's shelf destroyed", 0);
        }
        // A shape with a material array of its own, on a shelf that's there
        if (l.step == 270 && !with_mesh) {
            b3ShapeDef def = b3DefaultShapeDef();
            b3SurfaceMaterial two[2] = {def.baseMaterial, def.baseMaterial};
            two[1].friction = 0.1f;
            def.materials = two;
            def.materialCount = 2;
            b3CreateSphereShape(l.bodies[SHELF + 4], &def, &ball);
            did(&l, "a shape with two materials made", 0);
        }
        b3World_Step(l.world, DT, 4);
        const int parts = l.images_in_parts;
        right = take_image(&l);
        // The steps before each of those had their images in parts: the world was asleep and followed
        if (l.step == 119 || l.step == 249 || l.step == 269) TIDE_CHECK(l.images_in_parts > parts);
    }
    TIDE_REQUIRE(right);
    TIDE_CHECK(b3World_GetAwakeBodyCount(l.world) == RESTLESS);
    if (!with_mesh) {
        end(&l);
        return;
    }

    // A mesh, in a world that had none, and a crate onto it: the contact between them has more in an
    // image than the room every contact takes
    b3BodyDef ground = b3DefaultBodyDef();
    ground.position = b3ToPos((b3Vec3){12.0f, 0.3f, 0.0f});
    const b3BodyId slab = b3CreateBody(l.world, &ground);
    const b3ShapeDef shape = b3DefaultShapeDef();
    b3CreateMeshShape(slab, &shape, mesh, (b3Vec3){1.0f, 1.0f, 1.0f});
    b3BodyDef falling = b3DefaultBodyDef();
    falling.type = b3_dynamicBody;
    falling.position = b3ToPos((b3Vec3){12.0f, 1.6f, 0.0f});
    const b3BodyId crate = b3CreateBody(l.world, &falling);
    const b3BoxHull hull = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
    b3CreateHullShape(crate, &shape, &hull.base);
    did(&l, "a mesh made late", 0);

    int touching = 0; // Images in parts taken while the crate was on the mesh
    for (; l.step < 440 && right; l.step++) {
        b3World_Step(l.world, DT, 4);
        const int parts = l.images_in_parts;
        right = take_image(&l);
        touching += l.images_in_parts > parts && b3Body_GetContactCapacity(slab) > 0;
    }
    TIDE_REQUIRE(right);
    printf("    %d images in parts while a crate was on the mesh\n", touching);
    TIDE_CHECK(touching > 20);
    end(&l);
}

TIDE_TEST(box3d_parts_where_only_one_line_says_so)
{
    b3SetAssertFcn(stopped);
    mesh = b3CreateBoxMesh((b3Vec3){0.0f, 0.0f, 0.0f}, (b3Vec3){1.5f, 0.3f, 1.5f}, false);
    where_one_line_says_so(false);
    where_one_line_says_so(true);
    b3DestroyMesh(mesh);
}

// Box3D's own recordings go through the writer the images do, and a world
// that's recorded has its images taken in parts meanwhile: the recording
// replays to the world it was taken of, step for step, and back from the
// images it keeps of its own.
TIDE_TEST(box3d_parts_leave_recordings_as_they_were)
{
    b3SetAssertFcn(stopped);
    mesh = b3CreateBoxMesh((b3Vec3){0.0f, 0.0f, 0.0f}, (b3Vec3){1.5f, 0.3f, 1.5f}, false);
    field = b3CreateGrid(6, 6, (b3Vec3){0.6f, 0.3f, 0.6f}, false);
    static lab l;
    begin(&l, 2654435761u * 3u, 1);
    TIDE_REQUIRE(go_on(&l, 200));
    b3Recording *recording = b3CreateRecording(0);
    b3World_StartRecording(l.world, recording);
    l.recorded = true;
    const bool right = go_on(&l, 900);
    b3World_StopRecording(l.world);
    TIDE_REQUIRE(right);
    TIDE_CHECK(l.images_in_parts > 300);

    const uint8_t *bytes = b3Recording_GetData(recording);
    const int size = b3Recording_GetSize(recording);
    TIDE_CHECK(b3ValidateReplay(bytes, size, 1));
    b3RecPlayer *player = b3CreatePlayer(bytes, size, 1);
    TIDE_REQUIRE(player != NULL);
    TIDE_CHECK(b3RecPlayer_GetFrameCount(player) == 700);
    while (b3RecPlayer_StepFrame(player)) {
    }
    TIDE_CHECK(b3RecPlayer_GetFrame(player) == b3RecPlayer_GetFrameCount(player) && !b3RecPlayer_HasDiverged(player));
    b3RecPlayer_SeekFrame(player, 350); // Back, from an image the player kept
    while (b3RecPlayer_StepFrame(player)) {
    }
    TIDE_CHECK(b3RecPlayer_GetFrame(player) == b3RecPlayer_GetFrameCount(player) && !b3RecPlayer_HasDiverged(player));
    printf("    a recording of %d steps is %d bytes, and replays\n", b3RecPlayer_GetFrameCount(player), size);
    b3DestroyPlayer(player);
    end(&l);
    b3DestroyRecording(recording);
    b3DestroyMesh(mesh);
    b3DestroyHeightField(field);
}
