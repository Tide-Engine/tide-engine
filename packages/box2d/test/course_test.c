// What a game does with the Box2D package (course.tide): systems ordered
// against the step, what a view sees of a body, an impulse given where a body
// is spawned, bodies at rest that code moves, gravity that changes, and
// sensors.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "course.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide_test.h"

// bridge.c
void b2t_forget(void);
int32_t b2t_restores(void);

#define DT (1.0f / 60.0f)

static tide_world a;

static void start(tide_world *w, const Case which)
{
    b2t_forget();
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

static Box2D_Body body_of(const tide_world *w, has_fn *has)
{
    return tide_get_Box2D_Body(w, the(w, has));
}

static bool near(const float x, const float y)
{
    return tide_abs_f(x - y) < 0.001f;
}

// ---------------------------------------------------------------------------
// Systems ordered against the step

TIDE_TEST(box2d_orders_a_games_systems)
{
    start(&a, Case_Order);
    tick(&a, 2); // The bodies are spawned in the first tick, and made in the second
    Box2D_Body early = body_of(&a, tide_has_Early);
    Box2D_Body late = body_of(&a, tide_has_Late);
    // Before the step: the speed it wrote is the speed that tick stepped with
    TIDE_CHECK(early.velocity.x == 1.0f && near(early.position.x, DT));
    // After it: written, and not stepped with yet
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

static void put(tide_world *w, has_fn *has, const Box2D_Body body)
{
    tide_set_Box2D_Body(w, the(w, has), body);
}

TIDE_TEST(box2d_views_see_a_body_turn_the_short_way)
{
    start(&a, Case_Turning);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    bool wrapped = false;
    for (int i = 0; i < 400 && !wrapped; i++) {
        tide_world_copy(&previous, &a);
        tide_world_tick(&a);
        const float was = body_of(&previous, tide_has_Spinner).angle;
        const float is = body_of(&a, tide_has_Spinner).angle;
        if (was < 3.0f || is > -3.0f) continue;
        // The tick its angle went from just under half a turn to just over it, which Box2D says as -pi: a
        // view halfway between the two ticks sees it half a turn round, not facing the other way
        wrapped = true;
        tide_draw_reset(&draw);
        tide_frame(&a, &previous, 0.5f, NULL, &local, &draw, &gui);
        printf("    between a tick at %g and the next at %g, a view sees the body at %g\n", (double)was, (double)is,
               (double)local.Seen.angle);
        TIDE_CHECK(tide_abs_f(local.Seen.angle) > 3.0f);
        TIDE_CHECK(near(local.Seen.position.y, 5.0f));
    }
    TIDE_CHECK(wrapped);
    tide_local_free(&local);
    tide_world_free(&a);
    tide_world_free(&previous);
}

// The match says a body is somewhere else than this machine drew it, as the
// server does of what a client predicted: what a view sees doesn't jump there.
// It's where it was drawn, and gets to where the match says over a few frames.
TIDE_TEST(box2d_views_glide_through_a_correction)
{
    start(&a, Case_Turning);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    tick(&a, 30);
    tide_world_copy(&previous, &a);
    tide_world_tick(&a);
    const Box2D_Body drawn = body_of(&a, tide_has_Spinner);
    tide_draw_reset(&draw);
    tide_frame(&a, &previous, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.position.x == drawn.position.x && local.Seen.angle == drawn.angle);

    // The same tick as the server had it: a meter to the side, and turned half a radian more
    tide_world_copy(&b, &a);
    Box2D_Body said = drawn;
    said.position.x += 1.0f;
    said.angle += 0.5f;
    put(&b, tide_has_Spinner, said);
    const uint64_t hash = tide_world_hash(&b);
    const tide_view_correction correction = {&a, &previous, &b, &previous, 0, 1.0f, false};
    tide_local_correct(&local, &correction);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, &local, &draw, &gui);
    printf("    drawn at %g and turned %g, and said to be at %g and %g: a view sees it at %g and %g", (double)drawn.position.x,
           (double)drawn.angle, (double)said.position.x, (double)said.angle, (double)local.Seen.position.x, (double)local.Seen.angle);
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x) && near(local.Seen.angle, drawn.angle));
    // ...and halfway there 75 ms later
    tide_local_frame_time(&local, 0.075f);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, &local, &draw, &gui);
    printf(", then at %g and %g\n", (double)local.Seen.position.x, (double)local.Seen.angle);
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x + 0.5f) && near(local.Seen.angle, drawn.angle + 0.25f));
    // None of it is the match's: the world is as the server said
    TIDE_CHECK(tide_world_hash(&b) == hash && body_of(&b, tide_has_Spinner).position.x == said.position.x);
    tide_local_free(&local);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&previous);
}

// ---------------------------------------------------------------------------
// An impulse where a body is spawned

TIDE_TEST(box2d_gives_a_new_body_its_impulse)
{
    start(&a, Case_Impulse);
    tick(&a, 2);
    const Box2D_Body thrown = body_of(&a, tide_has_Thrown);
    printf("    spawned with an impulse of 3 and a turn of 0.5, a crate goes at %g and spins at %g\n", (double)thrown.velocity.x,
           (double)thrown.spin);
    // A crate of 1 by 1 weighs 1, and turns as a sixth of that
    TIDE_CHECK(near(thrown.velocity.x, 3.0f) && thrown.velocity.y == 0.0f && near(thrown.spin, 3.0f));
    TIDE_CHECK(thrown.impulse.x == 0.0f && thrown.impulse.y == 0.0f && thrown.turn == 0.0f); // Given, once
    tick(&a, 1);
    // ...as the same impulses do that are written into a body that's there
    const Box2D_Body nudged = body_of(&a, tide_has_Nudged);
    TIDE_CHECK(nudged.velocity.x == thrown.velocity.x && nudged.velocity.y == thrown.velocity.y && nudged.spin == thrown.spin);
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
        if (!w->Box2D_Physics.moved) return true;
    }
    return false;
}

// A world at rest that nothing is done to: no tick steps it, so its state is
// the one it was at
static bool stays(tide_world *w)
{
    const int64_t stamp = w->Box2D_Physics.stamp;
    bool still = true;
    for (int i = 0; i < 60; i++) {
        tide_world_tick(w);
        still = still && !w->Box2D_Physics.moved && w->Box2D_Physics.stamp == stamp;
    }
    return still;
}

TIDE_TEST(box2d_wakes_a_body_that_code_moves)
{
    start(&a, Case_Rest);
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(stays(&a));

    // A crate asleep on the floor, put in the air: it falls
    Box2D_Body sleeper = body_of(&a, tide_has_Sleeper);
    TIDE_CHECK(near(sleeper.position.y, 0.5f));
    sleeper.position.y = 5.0f;
    put(&a, tide_has_Sleeper, sleeper);
    tick(&a, 20);
    TIDE_CHECK(a.Box2D_Physics.moved);
    const float falling = body_of(&a, tide_has_Sleeper).position.y;
    printf("    a sleeping crate put at 5 is at %g a third of a second later\n", (double)falling);
    TIDE_CHECK(falling < 4.9f && body_of(&a, tide_has_Sleeper).velocity.y < -1.0f);
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(near(body_of(&a, tide_has_Sleeper).position.y, 0.5f));
    TIDE_CHECK(stays(&a));

    // ...and one that's turned where it lies: it comes to lie on a side again
    Box2D_Body tilted = body_of(&a, tide_has_Tilted);
    tilted.angle = 0.5f;
    put(&a, tide_has_Tilted, tilted);
    tick(&a, 5);
    TIDE_CHECK(a.Box2D_Physics.moved);
    TIDE_REQUIRE(rests(&a, 600));
    const float lies = body_of(&a, tide_has_Tilted).angle;
    printf("    a sleeping crate turned to 0.5 lies at %g once it's at rest\n", (double)lies);
    TIDE_CHECK(tide_abs_f(lies) < 0.05f || tide_abs_f(tide_abs_f(lies) - 0.5f * TIDE_PI_F) < 0.05f);

    // A body that never moves, moved from under a crate asleep on it: the crate falls to the floor
    TIDE_CHECK(body_of(&a, tide_has_Rider).position.y > 3.0f);
    Box2D_Body ledge = body_of(&a, tide_has_Ledge);
    ledge.position.x = 14.0f;
    put(&a, tide_has_Ledge, ledge);
    TIDE_REQUIRE(rests(&a, 600));
    const Box2D_Body rider = body_of(&a, tide_has_Rider);
    printf("    a crate asleep on a ledge that's moved away comes to rest at %g\n", (double)rider.position.y);
    TIDE_CHECK(near(rider.position.y, 0.5f));
    TIDE_CHECK(body_of(&a, tide_has_Ledge).position.x == 14.0f);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

TIDE_TEST(box2d_gravity_that_changes_wakes_the_world)
{
    start(&a, Case_Rest);
    TIDE_REQUIRE(rests(&a, 600));
    const int64_t stamp = a.Box2D_Physics.stamp;
    TIDE_CHECK(stays(&a));

    a.Box2D_Physics.gravity = tide_f2(0.0f, 10.0f);
    tide_world_tick(&a);
    TIDE_CHECK(a.Box2D_Physics.moved && a.Box2D_Physics.stamp != stamp); // Stepped, though nothing was awake
    tick(&a, 30);
    const Box2D_Body sleeper = body_of(&a, tide_has_Sleeper);
    printf("    with gravity turned upwards, a crate that slept on the floor is at %g half a second later\n", (double)sleeper.position.y);
    TIDE_CHECK(sleeper.position.y > 1.0f && sleeper.velocity.y > 1.0f);
    TIDE_CHECK(body_of(&a, tide_has_Rider).position.y > 4.2f);

    // ...and back: everything falls again, and comes to rest
    a.Box2D_Physics.gravity = tide_f2(0.0f, -10.0f);
    TIDE_REQUIRE(rests(&a, 900));
    TIDE_CHECK(body_of(&a, tide_has_Sleeper).position.y < 2.0f);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Sensors

// The match of sensors after its 330 ticks, on every platform and in every
// configuration: what a sensor holds of its visitors is in Box2D's image.
#define SENSORS_HASH UINT64_C(0xd8bc027345f18c9a)
#define SENSORS_IMAGES_HASH UINT64_C(0x14500025d237e07b)

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
        if (again) b2t_forget();
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w);
        if (i == LOOK && at_look) tide_world_copy(at_look, w);
    }
}

TIDE_TEST(box2d_sensors_report)
{
    static uint64_t went[SENSOR_TICKS];
    play_sensors(&a, went, false, &previous);

    // While the probe was still falling: everything else had happened
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
    // The marble fell through the gate: each heard the other come and go, once
    TIDE_CHECK(gate.touched == 1 && gate.parted == 1 && gate.gone == 0);
    TIDE_CHECK(tide_entity_equal(gate.last, the(w, tide_has_Marble)));
    // ...and landed on the floor, which is the second thing it touched
    TIDE_CHECK(marble.touched == 2 && marble.parted == 1);
    TIDE_CHECK(tide_entity_equal(marble.last, the(w, tide_has_Floor)));
    TIDE_CHECK(body_of(w, tide_has_Marble).position.y < 1.0f);
    // A sensor doesn't report another: the fence crosses the gate, and heard nothing
    TIDE_CHECK(fence.touched == 0 && fence.parted == 0);
    // A sensor that moves reports the bodies it passes through, static ones too: the shelf, then the floor
    TIDE_CHECK(probe.touched == 2 && probe.parted == 2 && shelf.touched == 1 && shelf.parted == 1);
    TIDE_CHECK(tide_entity_equal(shelf.last, the(w, tide_has_Probe)) && tide_entity_equal(probe.last, the(w, tide_has_Floor)));
    TIDE_CHECK(body_of(w, tide_has_Probe).position.y < -2.0f); // Nothing stopped it
    // A body that's in a sensor from its first step is reported, and stays so while it rests there
    TIDE_CHECK(trap.touched == 1 && trap.parted == 0 && tide_entity_equal(trap.last, the(w, tide_has_Bait)));
    TIDE_CHECK(bait.touched == 2 && bait.parted == 0);
    // The floor heard the marble, the probe, the bait and the prey, and the probe leave
    TIDE_CHECK(floor.touched == 4 && floor.parted == 1);

    // After it all: the bait went from inside its trap, and the snare from around its prey. Each that stayed
    // heard a parting, from no entity
    const Heard trap_after = heard_by(&a, tide_has_Trap);
    const Heard prey = heard_by(&a, tide_has_Prey);
    TIDE_CHECK(trap_after.touched == 1 && trap_after.parted == 1 && trap_after.gone == 1);
    TIDE_CHECK(tide_entity_equal(trap_after.last, (tide_entity){0}));
    TIDE_CHECK(prey.touched == 2 && prey.parted == 1 && prey.gone == 1);
    // ...and so did the floor, which the bait stood on: a solid shape's parting comes the same way
    const Heard floor_after = heard_by(&a, tide_has_Floor);
    TIDE_CHECK(floor_after.touched == 4 && floor_after.parted == 2 && floor_after.gone == 1);
    TIDE_CHECK(a.Box2D_Physics.bodies == 10 && a.Box2D_Physics.shapes == 10);
    TIDE_CHECK(!a.Box2D_Physics.moved); // And it all came to rest
    TIDE_CHECK(stays(&a));

    uint64_t images = 0;
    for (int i = 0; i < SENSOR_TICKS; i++) images = tide_hash_more(images, &went[i], sizeof went[i]);
    printf("    the world's hash is %016llx, and its %d images' %016llx\n", (unsigned long long)went[SENSOR_TICKS - 1], (int)SENSOR_TICKS,
           (unsigned long long)tide_hash_end(images));
    TIDE_CHECK(went[SENSOR_TICKS - 1] == SENSORS_HASH);
    TIDE_CHECK(tide_hash_end(images) == SENSORS_IMAGES_HASH);

    // What a sensor holds is in the image: a world made from it again before every tick hears the same, at
    // the same ticks
    static uint64_t again[SENSOR_TICKS];
    const int32_t before = b2t_restores();
    play_sensors(&b, again, true, NULL);
    TIDE_CHECK(b2t_restores() - before == SENSOR_TICKS - 1); // All but the first, which has no image to make it from
    for (int i = 0; i < SENSOR_TICKS; i++) {
        if (again[i] != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(again[i] == went[i]);
    }
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&previous);
}
