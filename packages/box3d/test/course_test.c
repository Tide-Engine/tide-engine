// What a game does with the Box3D package (course.tide): systems ordered
// against the step, what a view sees of a body, an impulse given where a body
// is spawned, bodies at rest that code moves, gravity that changes, and
// sensors.
//
// Every image that's written as what changed of the last one is compared with
// the image written whole (b3t_check), as in the package's other tests: what
// these do to a world, they do to bodies that are asleep or never move.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "course.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/time.h"
#include "tide/resident.h"
#include "tide_test.h"

// What the match holds of Box3D's state: the pages its resident keeps (tide/resident.h), in bytes
static int physics_bytes(const tide_world *w)
{
    return w->Box3D_Physics.state.pages ? (int)w->Box3D_Physics.state.pages->count * TIDE_ARENA_PAGE : 0;
}


// bridge.c
void b3t_forget(void);
void b3t_narrow(bool on);
void b3t_check(bool on, bool whenever);
void b3t_images(int32_t *whole, int32_t *in_parts);
uint64_t b3t_image_hash(void);
int32_t b3t_restores(void);
int32_t b3t_rebuilds(void);

#define DT (1.0f / 60.0f)

static tide_world a;

static void start(tide_world *w, const Case which)
{
    b3t_forget();
    b3t_check(true, true);
    tide_world_init(w, DT);
    w->Course.which = which;
}

static void tick(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
}

typedef bool has_fn(const tide_world *, tide_entity);

// The one entity with this component
static tide_entity the(const tide_world *w, has_fn *has)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (has(w, e)) return e;
    }
    return (tide_entity){0};
}

static Box3D_Body body_of(const tide_world *w, has_fn *has)
{
    return tide_get_Box3D_Body(w, the(w, has));
}

static bool near(const float x, const float y)
{
    return tide_abs_f(x - y) < 0.001f;
}

// ---------------------------------------------------------------------------
// Systems ordered against the step

TIDE_TEST(box3d_orders_a_games_systems)
{
    start(&a, Case_Order);
    tick(&a, 2); // The bodies are spawned in the first tick, and made in the second
    Box3D_Body early = body_of(&a, tide_has_Early);
    Box3D_Body late = body_of(&a, tide_has_Late);
    TIDE_CHECK(early.velocity.x == 1.0f && near(early.position.x, DT));
    TIDE_CHECK(late.velocity.x == 1.0f && late.position.x == 0.0f);
    tick(&a, 1);
    early = body_of(&a, tide_has_Early);
    late = body_of(&a, tide_has_Late);
    TIDE_CHECK(early.velocity.x == 2.0f && near(early.position.x, 3.0f * DT));
    TIDE_CHECK(late.velocity.x == 2.0f && near(late.position.x, DT));
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// What a view sees

static tide_world b;
static tide_world previous;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void put(tide_world *w, has_fn *has, const Box3D_Body body)
{
    tide_set_Box3D_Body(w, the(w, has), body);
}

// How alike two rotations are: 1 for the same one, less for others
static float alike(const tide_quaternion p, const tide_quaternion q)
{
    return tide_abs_f(tide_dot_q(tide_normalize_q(p), tide_normalize_q(q)));
}

TIDE_TEST(box3d_views_see_a_body_between_two_ticks)
{
    start(&a, Case_Turning);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    tick(&a, 40);
    tide_world_copy(&previous, &a);
    tide_world_tick(&a);
    const tide_quaternion was = body_of(&previous, tide_has_Spinner).rotation;
    const tide_quaternion is = body_of(&a, tide_has_Spinner).rotation;
    tide_draw_reset(&draw);
    tide_frame(&a, &previous, 0.5f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(alike(was, is) < 0.99999f);
    TIDE_CHECK(near(alike(local.Seen.rotation, was), alike(local.Seen.rotation, is)));
    TIDE_CHECK(alike(local.Seen.rotation, was) > alike(was, is));
    TIDE_CHECK(near(local.Seen.position.y, 5.0f));
    tide_local_free(&local);
    tide_world_free(&a);
    tide_world_free(&previous);
}

// The match says a body is somewhere else than this machine drew it, as the
// server does of what a client predicted: what a view sees doesn't jump there.
// It's where it was drawn, and gets to where the match says over a few frames.
TIDE_TEST(box3d_views_glide_through_a_correction)
{
    start(&a, Case_Turning);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    tick(&a, 30);
    tide_world_copy(&previous, &a);
    tide_world_tick(&a);
    const Box3D_Body drawn = body_of(&a, tide_has_Spinner);
    tide_draw_reset(&draw);
    tide_frame(&a, &previous, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.position.x == drawn.position.x && alike(local.Seen.rotation, drawn.rotation) > 0.99999f);

    tide_world_copy(&b, &a);
    Box3D_Body said = drawn;
    said.position.x += 1.0f;
    said.rotation = tide_mul_q(tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), 1.0f), drawn.rotation);
    put(&b, tide_has_Spinner, said);
    const uint64_t hash = tide_world_hash(&b);
    const tide_view_correction correction = {&a, &previous, &b, &previous, 0, 1.0f, false, false};
    tide_local_correct(&local, &correction);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    printf("    drawn at %g, and said to be at %g and turned a radian more: a view sees it at %g, turned %g of the way", (double)drawn.position.x,
           (double)said.position.x, (double)local.Seen.position.x,
           (double)(2.0f * tide_acos_f(tide_min_f(1.0f, alike(local.Seen.rotation, drawn.rotation)))));
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x) && alike(local.Seen.rotation, drawn.rotation) > 0.9999f);
    tide_local_frame_time(&local, 0.075f);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    const float turned = 2.0f * tide_acos_f(tide_min_f(1.0f, alike(local.Seen.rotation, drawn.rotation)));
    printf(", then at %g and %g\n", (double)local.Seen.position.x, (double)turned);
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x + 0.5f) && tide_abs_f(turned - 0.5f) < 0.01f);
    TIDE_CHECK(near(alike(local.Seen.rotation, drawn.rotation), alike(local.Seen.rotation, said.rotation)));
    TIDE_CHECK(tide_world_hash(&b) == hash && body_of(&b, tide_has_Spinner).position.x == said.position.x);
    tide_local_free(&local);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&previous);
}

// ---------------------------------------------------------------------------
// An impulse where a body is spawned

TIDE_TEST(box3d_gives_a_new_body_its_impulse)
{
    start(&a, Case_Impulse);
    tick(&a, 2);
    const Box3D_Body thrown = body_of(&a, tide_has_Thrown);
    printf("    spawned with an impulse of 3000 and an angular one of 500, a crate goes at %g and turns at %g\n", (double)thrown.velocity.x,
           (double)thrown.angularVelocity.z);
    TIDE_CHECK(near(thrown.velocity.x, 3.0f) && thrown.velocity.y == 0.0f && near(thrown.angularVelocity.z, 3.0f));
    TIDE_CHECK(thrown.impulse.x == 0.0f && thrown.angularImpulse.z == 0.0f); // Given, once
    tick(&a, 1);
    const Box3D_Body nudged = body_of(&a, tide_has_Nudged);
    TIDE_CHECK(nudged.velocity.x == thrown.velocity.x && nudged.velocity.y == thrown.velocity.y
               && nudged.angularVelocity.z == thrown.angularVelocity.z);
    tick(&a, 30);
    TIDE_CHECK(body_of(&a, tide_has_Thrown).velocity.x == thrown.velocity.x); // And not again
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Bodies at rest

// Ticks until a tick steps nothing, and whether one came within `most`
static bool rests(tide_world *w, const int most)
{
    for (int i = 0; i < most; i++) {
        tide_world_tick(w);
        if (!w->Box3D_Physics.moved) return true;
    }
    return false;
}

// A world at rest that nothing is done to: no tick steps it, so its state is
// the one it was at
static bool stays(tide_world *w)
{
    const int64_t stamp = w->Box3D_Physics.stamp;
    bool still = true;
    for (int i = 0; i < 60; i++) {
        tide_world_tick(w);
        still = still && !w->Box3D_Physics.moved && w->Box3D_Physics.stamp == stamp;
    }
    return still;
}

TIDE_TEST(box3d_wakes_a_body_that_code_moves)
{
    start(&a, Case_Rest);
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(stays(&a));

    Box3D_Body sleeper = body_of(&a, tide_has_Sleeper);
    TIDE_CHECK(near(sleeper.position.y, 0.5f));
    sleeper.position.y = 5.0f;
    put(&a, tide_has_Sleeper, sleeper);
    tick(&a, 20);
    TIDE_CHECK(a.Box3D_Physics.moved);
    const float falling = body_of(&a, tide_has_Sleeper).position.y;
    printf("    a sleeping crate put at 5 is at %g a third of a second later\n", (double)falling);
    TIDE_CHECK(falling < 4.9f && body_of(&a, tide_has_Sleeper).velocity.y < -1.0f);
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(near(body_of(&a, tide_has_Sleeper).position.y, 0.5f));
    TIDE_CHECK(stays(&a));

    Box3D_Body tilted = body_of(&a, tide_has_Tilted);
    const tide_quaternion turned = tide_axisangle_q(tide_f3(0.0f, 0.0f, 1.0f), 0.5f);
    tilted.rotation = turned;
    put(&a, tide_has_Tilted, tilted);
    tick(&a, 5);
    TIDE_CHECK(a.Box3D_Physics.moved);
    TIDE_REQUIRE(rests(&a, 600));
    const float lies = alike(body_of(&a, tide_has_Tilted).rotation, turned);
    printf("    a sleeping crate turned by 0.5 is %g from how it was turned once it's at rest\n",
           (double)(2.0f * tide_acos_f(tide_min_f(1.0f, lies))));
    TIDE_CHECK(lies < 0.99f);

    // A body that never moves, moved from under a crate asleep on it: the crate falls to the floor
    TIDE_CHECK(body_of(&a, tide_has_Rider).position.y > 3.0f);
    Box3D_Body ledge = body_of(&a, tide_has_Ledge);
    ledge.position.x = 14.0f;
    put(&a, tide_has_Ledge, ledge);
    TIDE_REQUIRE(rests(&a, 600));
    const Box3D_Body rider = body_of(&a, tide_has_Rider);
    printf("    a crate asleep on a ledge that's moved away comes to rest at %g\n", (double)rider.position.y);
    TIDE_CHECK(near(rider.position.y, 0.5f));
    TIDE_CHECK(body_of(&a, tide_has_Ledge).position.x == 14.0f);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

TIDE_TEST(box3d_gravity_that_changes_wakes_the_world)
{
    start(&a, Case_Rest);
    TIDE_REQUIRE(rests(&a, 600));
    const int64_t stamp = a.Box3D_Physics.stamp;
    TIDE_CHECK(stays(&a));

    a.Box3D_Physics.gravity = tide_f3(0.0f, 10.0f, 0.0f);
    tide_world_tick(&a);
    TIDE_CHECK(a.Box3D_Physics.moved && a.Box3D_Physics.stamp != stamp); // Stepped, though nothing was awake
    tick(&a, 30);
    const Box3D_Body sleeper = body_of(&a, tide_has_Sleeper);
    printf("    with gravity turned upwards, a crate that slept on the floor is at %g half a second later\n", (double)sleeper.position.y);
    TIDE_CHECK(sleeper.position.y > 1.0f && sleeper.velocity.y > 1.0f);
    TIDE_CHECK(body_of(&a, tide_has_Rider).position.y > 4.2f);

    a.Box3D_Physics.gravity = tide_f3(0.0f, -10.0f, 0.0f);
    TIDE_REQUIRE(rests(&a, 900));
    TIDE_CHECK(body_of(&a, tide_has_Sleeper).position.y < 2.0f);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Sensors

// The match of sensors after its 330 ticks, on every platform and in every
// configuration: what a sensor holds of its visitors is in Box3D's image.
#define SENSORS_HASH UINT64_C(0x51fe299be0ae7d50)
#define SENSORS_IMAGES_HASH UINT64_C(0xc1a1c39623de65a6)

enum { SENSOR_TICKS = 330, LOOK = 150, SPRING = 250, RELEASE = 280 };

static Heard heard_by(const tide_world *w, has_fn *has)
{
    return tide_get_Heard(w, the(w, has));
}

// The match of sensors, with every tick's hash: the bait goes from its trap at
// one tick and the snare from around its prey at another. With `again`, C's
// world is made from the match's image before every tick. `at_look` is the
// world as it was while the probe was still falling.
static void play_sensors(tide_world *w, uint64_t *hashes, const bool again, tide_world *at_look)
{
    start(w, Case_Sensors);
    for (int i = 0; i < SENSOR_TICKS; i++) {
        w->Course.spring = i == SPRING;
        w->Course.release = i == RELEASE;
        if (again) b3t_forget();
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w);
        if (i == LOOK && at_look) tide_world_copy(at_look, w);
    }
}

TIDE_TEST(box3d_sensors_report)
{
    static uint64_t went[SENSOR_TICKS];
    int32_t whole;
    int32_t in_parts;
    b3t_images(&whole, &in_parts);
    const int32_t parts_before = in_parts;
    play_sensors(&a, went, false, &previous);
    b3t_images(&whole, &in_parts);

    const tide_world *w = &previous;
    const Heard gate = heard_by(w, tide_has_Gate);
    const Heard fence = heard_by(w, tide_has_Fence);
    const Heard marble = heard_by(w, tide_has_Marble);
    const Heard shelf = heard_by(w, tide_has_Shelf);
    const Heard probe = heard_by(w, tide_has_Probe);
    const Heard floor = heard_by(w, tide_has_Floor);
    const Heard trap = heard_by(w, tide_has_Trap);
    const Heard bait = heard_by(w, tide_has_Bait);
    printf("    the gate heard %d come and %d go, the marble %d and %d, the probe %d and %d, the shelf %d and %d, the floor %d and %d\n",
           (int)gate.touched, (int)gate.parted, (int)marble.touched, (int)marble.parted, (int)probe.touched, (int)probe.parted,
           (int)shelf.touched, (int)shelf.parted, (int)floor.touched, (int)floor.parted);
    TIDE_CHECK(gate.touched == 1 && gate.parted == 1 && gate.gone == 0);
    TIDE_CHECK(tide_entity_equal(gate.last, the(w, tide_has_Marble)));
    TIDE_CHECK(marble.touched == 2 && marble.parted == 1);
    TIDE_CHECK(tide_entity_equal(marble.last, the(w, tide_has_Floor)));
    TIDE_CHECK(body_of(w, tide_has_Marble).position.y < 1.0f);
    TIDE_CHECK(fence.touched == 0 && fence.parted == 0);
    TIDE_CHECK(probe.touched == 2 && probe.parted == 2 && shelf.touched == 1 && shelf.parted == 1);
    TIDE_CHECK(tide_entity_equal(shelf.last, the(w, tide_has_Probe)) && tide_entity_equal(probe.last, the(w, tide_has_Floor)));
    TIDE_CHECK(body_of(w, tide_has_Probe).position.y < -2.0f); // Nothing stopped it
    TIDE_CHECK(trap.touched == 1 && trap.parted == 0 && tide_entity_equal(trap.last, the(w, tide_has_Bait)));
    TIDE_CHECK(bait.touched == 2 && bait.parted == 0);
    TIDE_CHECK(floor.touched == 4 && floor.parted == 1);

    const Heard trap_after = heard_by(&a, tide_has_Trap);
    const Heard prey = heard_by(&a, tide_has_Prey);
    TIDE_CHECK(trap_after.touched == 1 && trap_after.parted == 1 && trap_after.gone == 1);
    TIDE_CHECK(tide_entity_equal(trap_after.last, (tide_entity){0}));
    TIDE_CHECK(prey.touched == 2 && prey.parted == 1 && prey.gone == 1);
    const Heard floor_after = heard_by(&a, tide_has_Floor);
    TIDE_CHECK(floor_after.touched == 4 && floor_after.parted == 2 && floor_after.gone == 1);
    TIDE_CHECK(a.Box3D_Physics.bodies == 10 && a.Box3D_Physics.shapes == 10);
    TIDE_CHECK(!a.Box3D_Physics.moved); // And it all came to rest
    TIDE_CHECK(stays(&a));

    uint64_t images = 0;
    for (int i = 0; i < SENSOR_TICKS; i++) images = tide_hash_more(images, &went[i], sizeof went[i]);
    printf("    the world's hash is %016llx, and its %d images' %016llx; %d of them were written in parts\n",
           (unsigned long long)went[SENSOR_TICKS - 1], (int)SENSOR_TICKS, (unsigned long long)tide_hash_end(images),
           (int)(in_parts - parts_before));
    TIDE_CHECK(went[SENSOR_TICKS - 1] == SENSORS_HASH);
    TIDE_CHECK(tide_hash_end(images) == SENSORS_IMAGES_HASH);
    TIDE_CHECK(in_parts - parts_before > 100); // With sensors in it, an image in parts is still the image written whole

    static uint64_t again[SENSOR_TICKS];
    const int32_t before = b3t_restores();
    b3t_narrow(true);
    play_sensors(&b, again, true, NULL);
    b3t_narrow(false);
    TIDE_CHECK(b3t_restores() - before == SENSOR_TICKS - 1); // All but the first, which has no image to make it from
    for (int i = 0; i < SENSOR_TICKS; i++) {
        if (again[i] != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(again[i] == went[i]);
    }
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&previous);
}

// ---------------------------------------------------------------------------
// A match played for its hashes, as a host scripts it

typedef void script_fn(tide_world *w, int tick);

// A case for `ticks` ticks, with what a host does before each, and every
// tick's hash. With `again`, C's world is made from the match's image before
// every tick.
static void play(tide_world *w, const Case which, const int ticks, script_fn *script, uint64_t *hashes, const bool again)
{
    start(w, which);
    for (int i = 0; i < ticks; i++) {
        if (script) script(w, i);
        if (again) b3t_forget();
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w);
    }
}

static uint64_t all_of(const uint64_t *hashes, const int count)
{
    uint64_t hash = 0;
    for (int i = 0; i < count; i++) hash = tide_hash_more(hash, &hashes[i], sizeof hashes[i]);
    return tide_hash_end(hash);
}

// The same match with C's world made from the image before every tick, and
// solved 4 contacts at a time: tick for tick the match that never was.
static bool again_the_same(const Case which, const int ticks, script_fn *script, const uint64_t *went)
{
    static uint64_t again[1024];
    b3t_narrow(true);
    play(&b, which, ticks, script, again, true);
    b3t_narrow(false);
    bool same = true;
    for (int i = 0; i < ticks && same; i++) {
        if (again[i] != went[i]) printf("    tick %d came out different\n", i);
        same = again[i] == went[i];
    }
    tide_world_free(&b);
    return same;
}

static int count_of(const tide_world *w, has_fn *has)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) count += has(w, tide_entity_in_slot(&w->entities, i));
    return count;
}

static const tide_entity nobody = {0};

// ---------------------------------------------------------------------------
// Shapes on entities of their own

#define PARTS_HASH UINT64_C(0x4d249a9c6df305fd)

enum { PART_TICKS = 460, SAW = 200, CLEAR = 340 };

static void script_parts(tide_world *w, const int tick)
{
    w->Course.saw = tick == SAW;
    w->Course.clear = tick == CLEAR;
}

TIDE_TEST(box3d_parts_are_shapes_on_entities_of_their_own)
{
    static uint64_t went[PART_TICKS];
    const tide_quaternion upright = {{0.0f, 0.0f, 0.0f, 1.0f}};
    tide_entity table = nobody;
    tide_entity floor = nobody;
    start(&a, Case_Parts);
    for (int i = 0; i < PART_TICKS; i++) {
        script_parts(&a, i);
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
        const tide_world *w = &a;
        if (i == 1) {
            table = the(w, tide_has_Table);
            floor = the(w, tide_has_Floor);
            TIDE_CHECK(w->Box3D_Physics.bodies == 4 && w->Box3D_Physics.shapes == 9);
            TIDE_CHECK(w->Box3D_Physics.parts.live == 5 && count_of(w, tide_has_Leg) == 4);
            const Box3D_Part leg = tide_get_Box3D_Part(w, the(w, tide_has_Leg));
            TIDE_CHECK(leg.box != 0 && leg.sphere == 0 && tide_entity_equal(leg.of, table));
            TIDE_CHECK(count_of(w, tide_has_Stray) == 0 && count_of(w, tide_has_Nobody) == 1);
        }
        if (i == SAW - 1) {
            const Box3D_Body body = tide_get_Box3D_Body(w, table);
            printf("    a table of a top and four legs, each a part, rests with its body at %g\n", (double)body.position.y);
            TIDE_CHECK(!w->Box3D_Physics.moved && near(body.position.y, 0.1f) && alike(body.rotation, upright) > 0.99999f);
            const Heard at_table = tide_get_Heard(w, table);
            const Heard at_floor = tide_get_Heard(w, floor);
            TIDE_CHECK(at_table.touched == 4 && at_table.ownParts == 4 && at_table.otherParts == 0 && at_table.parted == 0);
            TIDE_CHECK(tide_has_Leg(w, at_table.lastPart) && tide_entity_equal(at_table.last, floor)
                       && tide_entity_equal(at_table.lastOtherPart, floor));
            TIDE_CHECK(at_floor.touched == 4 && at_floor.otherParts == 4 && at_floor.ownParts == 0);
            TIDE_CHECK(tide_has_Leg(w, at_floor.lastOtherPart) && tide_entity_equal(at_floor.lastPart, floor));
            const tide_entity stand = the(w, tide_has_Stand);
            const tide_entity cup = the(w, tide_has_Cup);
            const tide_entity pea = the(w, tide_has_Pea);
            const Heard at_stand = tide_get_Heard(w, stand);
            const Heard at_pea = tide_get_Heard(w, pea);
            printf("    the stand heard %d come and %d go, %d of them by its cup; the cup had %d in it at most\n", (int)at_stand.touched,
                   (int)at_stand.parted, (int)at_stand.ownParts, (int)w->Peeked.most);
            TIDE_CHECK(at_stand.touched == 2 && at_stand.parted == 1 && at_stand.ownParts == 2);
            TIDE_CHECK(at_pea.touched == 2 && at_pea.parted == 1 && at_pea.otherParts == 2 && tide_entity_equal(at_pea.last, stand));
            TIDE_CHECK(tide_get_Heard(w, cup).touched == 0);
            TIDE_CHECK(w->Peeked.most == 1 && tide_entity_equal(w->Peeked.who, pea) && w->Peeked.now == 0 && !w->Peeked.stand);
            TIDE_CHECK(near(tide_get_Box3D_Body(w, pea).position.y, 1.2f));
        }
        if (i == SAW + 1) {
            TIDE_CHECK(count_of(w, tide_has_Leg) == 2 && w->Box3D_Physics.parts.live == 3 && tide_has_Table(w, table));
            TIDE_CHECK(w->Box3D_Physics.moved && tide_list_count(w->Box3D_Physics.goneShapes) == 0);
            const Heard at_table = tide_get_Heard(w, table);
            const Heard at_floor = tide_get_Heard(w, floor);
            TIDE_CHECK(at_floor.parted == 2 && at_floor.gone == 0 && tide_entity_equal(at_floor.last, table)
                       && tide_entity_equal(at_floor.lastOtherPart, nobody));
            TIDE_CHECK(at_table.parted == 2 && tide_entity_equal(at_table.last, floor) && tide_entity_equal(at_table.lastPart, nobody));
        }
        if (i == CLEAR - 1) {
            const Box3D_Body body = tide_get_Box3D_Body(w, table);
            printf("    with two legs gone it comes to rest tipped by %g\n", (double)(2.0f * tide_acos_f(tide_min_f(1.0f, alike(body.rotation, upright)))));
            TIDE_CHECK(!w->Box3D_Physics.moved && alike(body.rotation, upright) < 0.999f);
        }
        if (i == CLEAR + 2) {
            TIDE_CHECK(count_of(w, tide_has_Table) == 0 && count_of(w, tide_has_Leg) == 0);
            TIDE_CHECK(w->Box3D_Physics.parts.live == 1 && tide_list_count(w->Box3D_Physics.goneShapes) == 0);
            TIDE_CHECK(tide_get_Heard(w, floor).gone > 0); // What it lay on heard it part, from no entity
        }
    }
    TIDE_CHECK(!a.Box3D_Physics.moved && stays(&a));
    printf("    the match's %d ticks hash to %016llx\n", (int)PART_TICKS, (unsigned long long)all_of(went, PART_TICKS));
    TIDE_CHECK(all_of(went, PART_TICKS) == PARTS_HASH);
    tide_world_free(&a);
    TIDE_CHECK(again_the_same(Case_Parts, PART_TICKS, script_parts, went));
}

// ---------------------------------------------------------------------------
// A body's type, and how it's to be, written after it's made

#define CHANGES_HASH UINT64_C(0xdb0bb102677be2f7)

enum { CHANGE_TICKS = 900 };

static void retype(tide_world *w, has_fn *has, const Box3D_BodyType type, const tide_float3 velocity)
{
    Box3D_Body body = body_of(w, has);
    body.type = type;
    body.velocity = velocity;
    put(w, has, body);
}

static void script_changes(tide_world *w, const int tick)
{
    if (tick == 10) retype(w, tide_has_Shifter, Box3D_BodyType_Static, body_of(w, tide_has_Shifter).velocity);
    if (tick == 20) retype(w, tide_has_Shifter, Box3D_BodyType_Dynamic, tide_f3(0.0f, 0.0f, 0.0f));
    if (tick == 30) retype(w, tide_has_Shifter, Box3D_BodyType_Kinematic, tide_f3(1.0f, 0.0f, 0.0f));
    if (tick == 40) retype(w, tide_has_Shifter, Box3D_BodyType_Static, tide_f3(1.0f, 0.0f, 0.0f));
    if (tick == 50) retype(w, tide_has_Shifter, Box3D_BodyType_Kinematic, tide_f3(0.0f, 0.0f, 2.0f));
    if (tick == 60) retype(w, tide_has_Shifter, Box3D_BodyType_Dynamic, tide_f3(0.0f, 0.0f, 2.0f));
    Box3D_Body floater = body_of(w, tide_has_Floater);
    if (tick == 10) floater.gravityScale = 0.0f;
    if (tick == 20) floater.linearDamping = 5.0f;
    if (tick == 45) {
        floater.bullet = true;
        floater.angularDamping = 2.0f;
    }
    if (tick == 600) { // Asleep in the air by now: it wakes to fall, with nothing to slow it
        floater.gravityScale = 1.0f;
        floater.linearDamping = 0.0f;
    }
    if (tick == 10 || tick == 20 || tick == 45 || tick == 600) put(w, tide_has_Floater, floater);
    // Two crates asleep, one on the other. The one on top is moved by code and let go again, and the one
    // under it made never to move and let go again
    if (tick == 400) retype(w, tide_has_Perched, Box3D_BodyType_Kinematic, tide_f3(0.0f, 0.0f, 0.0f));
    if (tick == 420) retype(w, tide_has_Perched, Box3D_BodyType_Dynamic, tide_f3(0.0f, 0.0f, 0.0f));
    if (tick == 440) retype(w, tide_has_Sitter, Box3D_BodyType_Static, tide_f3(0.0f, 0.0f, 0.0f));
    if (tick == 460) retype(w, tide_has_Sitter, Box3D_BodyType_Dynamic, tide_f3(0.0f, 0.0f, 0.0f));
}

TIDE_TEST(box3d_a_bodys_type_and_settings_change)
{
    static uint64_t went[CHANGE_TICKS];
    Box3D_Body was = {0};
    Box3D_Body ball = {0};
    Heard top = {0};
    Heard under = {0};
    Heard ground = {0};
    int32_t whole;
    int32_t in_parts;
    start(&a, Case_Changes);
    for (int i = 0; i < CHANGE_TICKS; i++) {
        script_changes(&a, i);
        b3t_images(&whole, &in_parts);
        const int32_t parts_before = in_parts;
        tide_world_tick(&a);
        b3t_images(&whole, &in_parts);
        went[i] = tide_world_hash(&a);
        const tide_world *w = &a;
        const Box3D_Body body = body_of(w, tide_has_Shifter);
        const Box3D_Body floater = body_of(w, tide_has_Floater);
        if (i == 9) {
            was = body;
            ball = floater;
            TIDE_CHECK(body.velocity.y < -1.0f && floater.velocity.y < -1.0f);
        }
        if (i == 10 || i == 19) {
            TIDE_CHECK(body.position.y == was.position.y && body.velocity.y == 0.0f && body.stepType == Box3D_BodyType_Static);
        }
        if (i == 29) TIDE_CHECK(body.position.y < was.position.y - 0.05f && body.velocity.y < -1.0f);
        if (i == 30) was = body;
        if (i == 39) {
            printf("    made kinematic with a speed of 1 along x, a falling crate goes %g in 9 ticks and falls %g\n",
                   (double)(body.position.x - was.position.x), (double)(was.position.y - body.position.y));
            TIDE_CHECK(body.velocity.x == 1.0f && body.velocity.y == 0.0f && body.position.y == was.position.y);
            TIDE_CHECK(near(body.position.x - was.position.x, 9.0f * DT));
            was = body;
        }
        if (i == 49) TIDE_CHECK(body.position.x == was.position.x && body.velocity.x == 0.0f);
        if (i == 59) TIDE_CHECK(body.position.z > 0.25f && body.velocity.z == 2.0f && body.velocity.y == 0.0f);
        if (i == 69) TIDE_CHECK(body.velocity.y < -1.0f && body.velocity.z > 1.9f);

        if (i == 10 || i == 19) TIDE_CHECK(floater.velocity.y == ball.velocity.y && floater.stepGravityScale == 0.0f);
        if (i == 44) {
            printf("    a ball falling at %g with no gravity and a damping of 5 falls at %g after 25 ticks\n", (double)ball.velocity.y,
                   (double)floater.velocity.y);
            TIDE_CHECK(floater.velocity.y < 0.0f && floater.velocity.y > 0.3f * ball.velocity.y);
        }
        if (i == 45) TIDE_CHECK(floater.stepBullet && floater.stepAngularDamping == 2.0f && floater.stepLinearDamping == 5.0f);
        if (i == 599) {
            ball = floater;
            TIDE_CHECK(!w->Box3D_Physics.moved && floater.velocity.y == 0.0f && floater.position.y > 5.0f);
        }
        if (i == 620) TIDE_CHECK(floater.velocity.y < -1.0f && floater.position.y < ball.position.y);

        if (i == 399) {
            top = heard_by(w, tide_has_Perched);
            under = heard_by(w, tide_has_Sitter);
            ground = heard_by(w, tide_has_Floor);
            TIDE_CHECK(!w->Box3D_Physics.moved && top.touched == 1 && under.touched == 2);
        }
        if (i == 400 || i == 420 || i == 440 || i == 460) TIDE_CHECK(w->Box3D_Physics.moved && in_parts == parts_before);
        if (i == 400 || i == 419 || i == 420 || i == 439) {
            const Heard now = heard_by(w, tide_has_Perched);
            const Heard below = heard_by(w, tide_has_Sitter);
            TIDE_CHECK(now.touched == top.touched && now.parted == top.parted);
            TIDE_CHECK(below.touched == under.touched && below.parted == under.parted);
        }
        // A body that never moves doesn't touch the floor, which doesn't either: those two parted. The crate
        // on it still touches it
        if (i == 440 || i == 459) {
            const Heard below = heard_by(w, tide_has_Sitter);
            TIDE_CHECK(below.parted == under.parted + 1 && below.touched == under.touched);
            TIDE_CHECK(tide_entity_equal(below.last, the(w, tide_has_Floor)));
            TIDE_CHECK(heard_by(w, tide_has_Floor).parted == ground.parted + 1);
            TIDE_CHECK(heard_by(w, tide_has_Perched).parted == top.parted && heard_by(w, tide_has_Perched).touched == top.touched);
        }
        if (i == 460) {
            const Heard below = heard_by(w, tide_has_Sitter);
            TIDE_CHECK(below.touched == under.touched + 1 && below.parted == under.parted + 1);
            TIDE_CHECK(heard_by(w, tide_has_Perched).touched == top.touched);
        }
    }
    TIDE_CHECK(!a.Box3D_Physics.moved && stays(&a));
    TIDE_CHECK(near(body_of(&a, tide_has_Perched).position.y, 1.5f) && near(body_of(&a, tide_has_Sitter).position.y, 0.5f));
    printf("    the match's %d ticks hash to %016llx\n", (int)CHANGE_TICKS, (unsigned long long)all_of(went, CHANGE_TICKS));
    TIDE_CHECK(all_of(went, CHANGE_TICKS) == CHANGES_HASH);
    tide_world_free(&a);
    TIDE_CHECK(again_the_same(Case_Changes, CHANGE_TICKS, script_changes, went));
}

// ---------------------------------------------------------------------------
// How the world is tuned, and what meets hard

#define TUNING_HASH UINT64_C(0x79edccf2a7afb035)

enum { TUNE_TICKS = 150 };

// What a run of the tuning's match came to
typedef struct tuned {
    Heard crate;      // What the dropped crate heard
    Heard ground;     // ...and the floor
    Box3D_Body ended; // Where the crate is at the end
    float deepest;    // The lowest the crate got
    float rebound;    // The fastest it went up after it landed
    float bounced;    // The fastest the ball went up
    float sunk;       // Where the crate that started in the floor is after 12 ticks
} tuned;

typedef void tune_fn(tide_world *w, int tick);

static tuned tune(tide_world *w, tune_fn *how, uint64_t *hashes)
{
    tuned out = {.deepest = 100.0f};
    start(w, Case_Tuning);
    for (int i = 0; i < TUNE_TICKS; i++) {
        if (how) how(w, i);
        tide_world_tick(w);
        if (hashes) hashes[i] = tide_world_hash(w);
        if (i < 2) continue;
        const Box3D_Body crate = body_of(w, tide_has_Dropped);
        const Box3D_Body ball = body_of(w, tide_has_Bouncy);
        if (crate.position.y < out.deepest) out.deepest = crate.position.y;
        if (heard_by(w, tide_has_Dropped).touched > 0 && crate.velocity.y > out.rebound) out.rebound = crate.velocity.y;
        if (ball.velocity.y > out.bounced) out.bounced = ball.velocity.y;
        if (i == 13) out.sunk = body_of(w, tide_has_Sunk).position.y;
    }
    out.crate = heard_by(w, tide_has_Dropped);
    out.ground = heard_by(w, tide_has_Floor);
    out.ended = body_of(w, tide_has_Dropped);
    return out;
}

static void no_hits(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.hitSpeed = 50.0f;
}

static void late_hits(tide_world *w, const int tick)
{
    w->Box3D_Physics.hitSpeed = tick < 30 ? 50.0f : 1.0f; // Said while the crate is still falling
}

static void lively(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.bounceSpeed = 0.1f;
}

static void slow_push(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.contactSpeed = 0.2f;
}

static void soft(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.contactStiffness = 3.0f;
}

static void springy(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.contactStiffness = 8.0f;
    w->Box3D_Physics.contactDamping = 0.1f;
}

static void stiff_but_damped(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.contactStiffness = 8.0f;
}

static void sleepless(tide_world *w, const int tick)
{
    (void)tick;
    w->Box3D_Physics.sleeping = false;
}

TIDE_TEST(box3d_the_world_is_tuned_by_its_fields)
{
    static uint64_t went[TUNE_TICKS];
    const tuned plain = tune(&a, NULL, went);
    printf("    a crate dropped 2.5 m hit the floor at %g: it heard %d hit%s, pushed towards (%g, %g, %g) at (%g, %g, %g)\n",
           (double)plain.crate.hitSpeed, (int)plain.crate.hits, plain.crate.hits == 1 ? "" : "s", (double)plain.crate.hitNormal.x,
           (double)plain.crate.hitNormal.y, (double)plain.crate.hitNormal.z, (double)plain.crate.hitPoint.x, (double)plain.crate.hitPoint.y,
           (double)plain.crate.hitPoint.z);
    TIDE_CHECK(plain.crate.hits >= 1 && plain.crate.hitSpeed > 6.0f && plain.crate.hitSpeed < 7.5f);
    TIDE_CHECK(tide_entity_equal(plain.crate.hitBy, the(&a, tide_has_Floor)) && tide_entity_equal(plain.ground.hitBy, the(&a, tide_has_Dropped)));
    TIDE_CHECK(plain.crate.hitNormal.y > 0.99f && plain.ground.hitNormal.y < -0.99f);
    TIDE_CHECK(plain.ground.hitSpeed == plain.crate.hitSpeed && tide_abs_f(plain.crate.hitPoint.y) < 0.1f
               && tide_abs_f(plain.crate.hitPoint.x) < 0.6f);
    TIDE_CHECK(plain.ground.hits == plain.crate.hits);
    printf("    the match's %d ticks hash to %016llx\n", (int)TUNE_TICKS, (unsigned long long)all_of(went, TUNE_TICKS));
    TIDE_CHECK(all_of(went, TUNE_TICKS) == TUNING_HASH);
    tide_world_free(&a);
    TIDE_CHECK(again_the_same(Case_Tuning, TUNE_TICKS, NULL, went));

    // A threshold nothing reaches: no hit, and the same world otherwise, to the bit
    const tuned quiet = tune(&a, no_hits, NULL);
    TIDE_CHECK(quiet.crate.hits == 0 && quiet.ground.hits == 0);
    TIDE_CHECK(memcmp(&quiet.ended.position, &plain.ended.position, sizeof plain.ended.position) == 0
               && memcmp(&quiet.ended.rotation, &plain.ended.rotation, sizeof plain.ended.rotation) == 0);
    tide_world_free(&a);
    const tuned told = tune(&a, late_hits, NULL);
    TIDE_CHECK(told.crate.hits == plain.crate.hits && told.crate.hitSpeed == plain.crate.hitSpeed);
    tide_world_free(&a);

    const tuned bouncy = tune(&a, lively, NULL);
    printf("    a ball that lands at 0.6 goes up at %g, and at %g with a bounce threshold of 0.1\n", (double)plain.bounced,
           (double)bouncy.bounced);
    TIDE_CHECK(plain.bounced < 0.05f && bouncy.bounced > 0.3f);
    tide_world_free(&a);

    const tuned pushed = tune(&a, slow_push, NULL);
    printf("    a crate that starts 0.3 in the floor is at %g after 12 ticks, and at %g pushed out at 0.2 at most\n", (double)plain.sunk,
           (double)pushed.sunk);
    TIDE_CHECK(pushed.sunk < plain.sunk - 0.1f && pushed.sunk < 0.3f && plain.sunk > 0.45f);
    tide_world_free(&a);

    const tuned sinking = tune(&a, soft, NULL);
    printf("    the crate gets down to %g as it lands, and to %g on contacts of 3 cycles a second\n", (double)plain.deepest,
           (double)sinking.deepest);
    TIDE_CHECK(sinking.deepest < plain.deepest - 0.005f);
    tide_world_free(&a);

    const tuned damped = tune(&a, stiff_but_damped, NULL);
    tide_world_free(&a);
    const tuned sprung = tune(&a, springy, NULL);
    printf("    on contacts of 8 cycles a second it comes back up at %g, and at %g with a damping of 0.1\n", (double)damped.rebound,
           (double)sprung.rebound);
    TIDE_CHECK(sprung.rebound > damped.rebound + 0.2f);
    tide_world_free(&a);

    (void)tune(&a, sleepless, NULL);
    bool stepped = true;
    for (int i = 0; i < 400; i++) {
        tide_world_tick(&a);
        stepped = stepped && a.Box3D_Physics.moved;
    }
    TIDE_CHECK(stepped);
    a.Box3D_Physics.sleeping = true;
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(stays(&a));
    a.Box3D_Physics.sleeping = false;
    tide_world_tick(&a);
    TIDE_CHECK(a.Box3D_Physics.moved);
    tick(&a, 200);
    TIDE_CHECK(a.Box3D_Physics.moved);
    a.Box3D_Physics.sleeping = true;
    TIDE_REQUIRE(rests(&a, 600));
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// What code asks of the world

#define ASKING_HASH UINT64_C(0x0c4b57c65a3619b0)
#define ANSWERS_HASH UINT64_C(0xdc5da4ff7c55ceb8)

enum { ASK_TICKS = 260, DOOM = 120 };

static void script_asking(tide_world *w, const int tick)
{
    w->Course.doom = tick == DOOM;
}

// Two sets of answers, but for what's said of the wall that goes
static bool same_answers(Answers x, Answers y)
{
    memset(&x.doomed, 0, sizeof x.doomed);
    memset(&y.doomed, 0, sizeof y.doomed);
    return memcmp(&x, &y, sizeof x) == 0;
}

static bool close3(const tide_float3 v, const float x, const float y, const float z)
{
    return tide_abs_f(v.x - x) < 0.01f && tide_abs_f(v.y - y) < 0.01f && tide_abs_f(v.z - z) < 0.01f;
}

TIDE_TEST(box3d_answers_what_code_asks)
{
    static uint64_t went[ASK_TICKS];
    static uint64_t images[ASK_TICKS];
    static Answers said[ASK_TICKS];
    int32_t whole;
    int32_t in_parts;
    b3t_images(&whole, &in_parts);
    const int32_t parts_before = in_parts;
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    start(&a, Case_Asking);
    for (int i = 0; i < ASK_TICKS; i++) {
        script_asking(&a, i);
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
        images[i] = b3t_image_hash();
        said[i] = a.Asked.late;
        const tide_world *w = &a;
        if (i >= 2) {
            if (!same_answers(a.Asked.early, said[i - 1])) printf("    tick %d: asked before the step, the answers aren't the last tick's\n", i);
            TIDE_REQUIRE(same_answers(a.Asked.early, said[i - 1]));
        }
        tide_draw_reset(&draw);
        tide_frame(&a, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        if (i >= 1) {
            if (!same_answers(local.Glimpse.seen, said[i])) printf("    tick %d: a view's answers aren't the match's\n", i);
            TIDE_REQUIRE(same_answers(local.Glimpse.seen, said[i]));
        }
        const Answers *now = &a.Asked.late;
        if (i == 0) TIDE_CHECK(!now->wall.any && now->over == 0 && a.Asked.zone == 0); // No step yet: nothing to find
        if (i == 5) {
            const tide_entity wall = the(w, tide_has_Wall);
            const tide_entity post = the(w, tide_has_Post);
            printf("    a ray along x meets the wall %g of the way, at (%g, %g, %g), facing (%g, %g, %g)\n", (double)now->wall.hit.fraction,
                   (double)now->wall.hit.point.x, (double)now->wall.hit.point.y, (double)now->wall.hit.point.z, (double)now->wall.hit.normal.x,
                   (double)now->wall.hit.normal.y, (double)now->wall.hit.normal.z);
            TIDE_CHECK(now->wall.any && tide_entity_equal(now->wall.hit.entity, wall) && tide_entity_equal(now->wall.hit.part, wall));
            TIDE_CHECK(near(now->wall.hit.fraction, 0.45f) && close3(now->wall.hit.point, 4.5f, 1.0f, 0.0f)
                       && close3(now->wall.hit.normal, -1.0f, 0.0f, 0.0f));
            TIDE_CHECK(now->twin.any && tide_entity_equal(now->twin.hit.entity, the(w, tide_has_Twin)) && near(now->twin.hit.fraction, 0.45f));
            TIDE_CHECK(now->knob.any && tide_entity_equal(now->knob.hit.entity, post)
                       && tide_entity_equal(now->knob.hit.part, the(w, tide_has_Knob)) && near(now->knob.hit.fraction, 0.4f)
                       && close3(now->knob.hit.normal, 0.0f, 1.0f, 0.0f));
            TIDE_CHECK(!now->nothing.any && tide_entity_equal(now->nothing.hit.entity, nobody) && now->nothing.hit.fraction == 0.0f);
            TIDE_CHECK(!now->within.any);
            TIDE_CHECK(now->turner.any && tide_entity_equal(now->turner.hit.entity, the(w, tide_has_Turner)));
            TIDE_CHECK(now->doomed.any && near(now->doomed.hit.fraction, 3.5f / 12.0f));
            printf("    a ball cast down 6 m onto a crate stops %g of the way, and a box cast along x at the wall %g\n",
                   (double)now->ball.hit.fraction, (double)now->block.hit.fraction);
            TIDE_CHECK(now->ball.any && tide_entity_equal(now->ball.hit.entity, the(w, tide_has_Resting))
                       && tide_abs_f(now->ball.hit.fraction - 0.25f) < 0.005f && close3(now->ball.hit.normal, 0.0f, 1.0f, 0.0f)
                       && close3(now->ball.hit.point, 0.0f, 1.0f, 5.0f));
            TIDE_CHECK(now->block.any && tide_entity_equal(now->block.hit.entity, wall) && now->block.hit.fraction > 0.37f
                       && now->block.hit.fraction < 0.39f && close3(now->block.hit.normal, -1.0f, 0.0f, 0.0f));
            TIDE_CHECK(now->pill.any && tide_entity_equal(now->pill.hit.entity, the(w, tide_has_Turner)));
            TIDE_CHECK(now->around == 1 && now->capsule == 2);
            TIDE_CHECK(a.Asked.zone == 1 && tide_entity_equal(a.Asked.zoneFirst, the(w, tide_has_Resting)));
        }
        if (i >= 1) TIDE_CHECK(a.Asked.sorted && a.Asked.listed == now->over && now->over >= 3);
        if (i == DOOM) TIDE_CHECK(now->doomed.any && !local.Glimpse.seen.doomed.any);
        if (i == DOOM + 1) TIDE_CHECK(!a.Asked.early.doomed.any && !now->doomed.any);
        if (i != DOOM && i >= 1) TIDE_CHECK(local.Glimpse.seen.doomed.any == now->doomed.any);
    }
    b3t_images(&whole, &in_parts);
    const Answers last = a.Asked.late;
    printf("    at the end the big box overlaps %d bodies; %d of the match's images were written in parts\n", (int)last.over,
           (int)(in_parts - parts_before));
    TIDE_CHECK(last.over >= 3 && last.over <= 6 && a.Asked.asked == ASK_TICKS);
    TIDE_CHECK(in_parts - parts_before > 100);

    uint64_t answers = 0;
    for (int i = 0; i < ASK_TICKS; i++) answers = tide_hash_more(answers, &said[i], sizeof said[i]);
    printf("    the match's %d ticks hash to %016llx, and what was answered in them to %016llx\n", (int)ASK_TICKS,
           (unsigned long long)all_of(went, ASK_TICKS), (unsigned long long)tide_hash_end(answers));
    TIDE_CHECK(all_of(went, ASK_TICKS) == ASKING_HASH);
    TIDE_CHECK(tide_hash_end(answers) == ANSWERS_HASH);

    b3t_forget();
    const int32_t rebuilt = b3t_rebuilds();
    memset(&local.Glimpse, 0, sizeof local.Glimpse);
    tide_draw_reset(&draw);
    tide_frame(&a, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(b3t_rebuilds() == rebuilt + 1 && same_answers(local.Glimpse.seen, last));
    const int32_t restored = b3t_restores();
    tide_draw_reset(&draw);
    tide_frame(&a, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_world_tick(&a);
    TIDE_CHECK(b3t_rebuilds() == rebuilt + 1 && b3t_restores() == restored);
    tide_local_free(&local);
    tide_world_free(&a);

    static Answers again[ASK_TICKS];
    const int32_t rebuilds = b3t_rebuilds();
    const int32_t restores = b3t_restores();
    b3t_narrow(true);
    start(&b, Case_Asking);
    for (int i = 0; i < ASK_TICKS; i++) {
        script_asking(&b, i);
        b3t_forget();
        tide_world_tick(&b);
        again[i] = b.Asked.late;
        if (tide_world_hash(&b) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
        TIDE_REQUIRE(memcmp(&again[i], &said[i], sizeof said[i]) == 0);
    }
    b3t_narrow(false);
    TIDE_CHECK(b3t_rebuilds() - rebuilds == ASK_TICKS - 1 && b3t_restores() - restores == ASK_TICKS - 1);
    tide_world_free(&b);

    start(&b, Case_Asking);
    b.Course.mute = true;
    for (int i = 0; i < ASK_TICKS; i++) {
        script_asking(&b, i);
        tide_world_tick(&b);
        if (b3t_image_hash() != images[i]) printf("    tick %d: Box3D's image isn't what it is when nothing is asked\n", i);
        TIDE_REQUIRE(b3t_image_hash() == images[i]);
    }
    TIDE_CHECK(b.Asked.asked == 0);
    tide_world_free(&b);
}

// What it takes to make C's world for a question it has none at, which a tick
// that goes back in time pays too: said, and not checked, as times are the
// machine's.
TIDE_TEST(box3d_asking_costs)
{
    start(&a, Case_Asking);
    tick(&a, 200);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    b3t_check(false, false);
    uint64_t there = UINT64_MAX;
    uint64_t made = UINT64_MAX;
    for (int round = 0; round < 20; round++) {
        tide_draw_reset(&draw);
        uint64_t began = tide_time_now_ns();
        tide_frame(&a, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        uint64_t took = tide_time_now_ns() - began;
        if (took < there) there = took;
        b3t_forget();
        tide_draw_reset(&draw);
        began = tide_time_now_ns();
        tide_frame(&a, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        took = tide_time_now_ns() - began;
        if (took < made) made = took;
    }
    printf("    a view that asks 13 questions of a world of %d bodies takes %.1f us, and %.1f when C's world is made from the image first "
           "(%d bytes)\n",
           (int)a.Box3D_Physics.bodies, (double)there / 1e3, (double)made / 1e3, physics_bytes(&a));
    tide_local_free(&local);
    tide_world_free(&a);
}
