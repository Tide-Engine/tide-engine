// Bodies that code puts to sleep and wakes, and what `asleep` reads
// (sleep.tide): a crate at rest and one on its way, what goes to sleep with a
// body, bodies spawned asleep, and all of it through what a match does to a
// world.
//
// Every image that's written as what changed of the last one is compared with
// the image written whole (b3t_check), as in the package's other tests:
// putting a body to sleep and waking one are calls an image in parts follows.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sleeping.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide_test.h"

// bridge.c
void b3t_forget(void);
void b3t_check(bool on, bool whenever);
void b3t_images(int32_t *whole, int32_t *in_parts);
int32_t b3t_restores(void);

#define DT (1.0f / 60.0f)

static tide_world a;
static tide_world b;
static tide_world kept;

static void start(tide_world *w, const Case which)
{
    b3t_forget();
    b3t_check(true, true);
    tide_world_init(w, DT);
    w->Nap.which = which;
}

static void tick(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
}

// The entity a case marked with this number
static tide_entity marked(const tide_world *w, const int index)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Mark(w, e) && tide_get_Mark(w, e).index == index) return e;
    }
    return (tide_entity){0};
}

static Box3D_Body body(const tide_world *w, const int index)
{
    return tide_get_Box3D_Body(w, marked(w, index));
}

// What a system of a game's writes into a body after the step: the next
// tick's step has it
static void put(tide_world *w, const int index, const Box3D_Body value)
{
    tide_set_Box3D_Body(w, marked(w, index), value);
}

static void lull(tide_world *w, const int index, const bool asleep)
{
    Box3D_Body value = body(w, index);
    value.asleep = asleep;
    put(w, index, value);
}

static bool same(const tide_float3 at, const tide_float3 as)
{
    return at.x == as.x && at.y == as.y && at.z == as.z;
}

static bool still(const Box3D_Body value)
{
    return same(value.velocity, tide_f3(0.0f, 0.0f, 0.0f)) && same(value.angularVelocity, tide_f3(0.0f, 0.0f, 0.0f));
}

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
    bool as_it_was = true;
    for (int i = 0; i < 60; i++) {
        tide_world_tick(w);
        as_it_was = as_it_was && !w->Box3D_Physics.moved && w->Box3D_Physics.stamp == stamp;
    }
    return as_it_was;
}

static void throw_ball(tide_world *w, const float x, const float y, const float dx, const float dy)
{
    w->Nap.throws = true;
    w->Nap.from = tide_f3(x, y, 0.0f);
    w->Nap.speed = tide_f3(dx, dy, 0.0f);
}

// Ticks until the body is awake, and whether it was within `most`
static bool wakes(tide_world *w, const int index, const int most)
{
    for (int i = 0; i < most; i++) {
        tide_world_tick(w);
        if (!body(w, index).asleep) return true;
    }
    return false;
}

// How many images were written in parts since `since` was taken
static int32_t in_parts_since(const int32_t since)
{
    int32_t whole;
    int32_t in_parts;
    b3t_images(&whole, &in_parts);
    return in_parts - since;
}

// ---------------------------------------------------------------------------
// A crate at rest

TIDE_TEST(box3d_a_resting_crate_put_to_sleep_costs_nothing)
{
    start(&a, Case_Rest);
    tick(&a, 12); // On the floor, and not yet asleep: Box3D waits half a second for that
    Box3D_Body crate = body(&a, 0);
    TIDE_CHECK(!crate.asleep && !body(&a, 1).asleep);
    const tide_float3 at = crate.position;
    lull(&a, 0, true);
    tide_world_tick(&a);
    crate = body(&a, 0);
    TIDE_CHECK(crate.asleep && same(crate.position, at) && still(crate));
    // The other one isn't touching it: a floor that never moves ties nothing together
    TIDE_CHECK(!body(&a, 1).asleep);

    for (int i = 0; i < 6; i++) {
        lull(&a, 0, i % 2 == 1);
        tide_world_tick(&a);
        TIDE_CHECK(body(&a, 0).asleep == (i % 2 == 1));
    }
    const tide_float3 slept_at = body(&a, 0).position;

    TIDE_REQUIRE(rests(&a, 120));
    TIDE_CHECK(body(&a, 0).asleep && body(&a, 1).asleep && still(body(&a, 1)));
    TIDE_CHECK(stays(&a));
    TIDE_CHECK(same(body(&a, 0).position, slept_at));

    const int64_t stamp = a.Box3D_Physics.stamp;
    lull(&a, 0, false);
    tide_world_tick(&a);
    TIDE_CHECK(a.Box3D_Physics.moved && a.Box3D_Physics.stamp != stamp);
    TIDE_CHECK(!body(&a, 0).asleep && body(&a, 1).asleep);
    TIDE_REQUIRE(rests(&a, 120));
    TIDE_CHECK(body(&a, 0).asleep);

    throw_ball(&a, 5.0f, 6.0f, 0.0f, -12.0f);
    TIDE_REQUIRE(wakes(&a, 1, 120));
    TIDE_CHECK(body(&a, 0).asleep); // And not the first, which it never touched

    crate = body(&a, 0);
    crate.type = Box3D_BodyType_Static;
    put(&a, 0, crate);
    tide_world_tick(&a);
    TIDE_CHECK(!body(&a, 0).asleep);
    tide_world_free(&a);
}

// A world whose bodies don't fall asleep by themselves: one that code puts to
// sleep sleeps all the same.
TIDE_TEST(box3d_sleeps_a_body_where_none_falls_asleep)
{
    start(&a, Case_Rest);
    a.Box3D_Physics.sleeping = false;
    tick(&a, 150);
    TIDE_CHECK(!body(&a, 0).asleep && !body(&a, 1).asleep && a.Box3D_Physics.moved); // At rest for two seconds, and awake
    const tide_float3 at = body(&a, 0).position;
    lull(&a, 0, true);
    tick(&a, 90);
    TIDE_CHECK(body(&a, 0).asleep && same(body(&a, 0).position, at) && !body(&a, 1).asleep);
    throw_ball(&a, 0.0f, 6.0f, 0.0f, -12.0f);
    TIDE_REQUIRE(wakes(&a, 0, 120));
    tick(&a, 120);
    TIDE_CHECK(!body(&a, 0).asleep); // ...and doesn't fall asleep again
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// A crate on its way

TIDE_TEST(box3d_a_moving_body_put_to_sleep_stops_where_it_is)
{
    start(&a, Case_Flight);
    tick(&a, 12);
    Box3D_Body crate = body(&a, 0);
    TIDE_CHECK(!crate.asleep && crate.velocity.x == 2.0f && crate.velocity.y < -1.0f && crate.position.y > 4.0f);
    const tide_float3 at = crate.position;
    const float falling = crate.velocity.y;
    lull(&a, 0, true);
    tide_world_tick(&a);
    crate = body(&a, 0);
    printf("    a crate falling at %g, put to sleep at a height of %g: a tick later it's at %g, going at %g\n", (double)falling, (double)at.y,
           (double)crate.position.y, (double)crate.velocity.y);
    TIDE_CHECK(crate.asleep && same(crate.position, at) && still(crate));
    TIDE_CHECK(stays(&a)); // It's all there was to step
    TIDE_CHECK(same(body(&a, 0).position, at));

    throw_ball(&a, at.x - 6.0f, at.y, 12.0f, 0.0f);
    TIDE_REQUIRE(wakes(&a, 0, 120));
    tick(&a, 20);
    crate = body(&a, 0);
    TIDE_CHECK(!crate.asleep && crate.position.y < at.y - 0.3f && crate.velocity.y < -1.0f);

    crate.position = tide_f3(7.0f, 7.0f, 0.0f);
    crate.asleep = true;
    put(&a, 0, crate);
    tide_world_tick(&a);
    crate = body(&a, 0);
    TIDE_CHECK(crate.asleep && same(crate.position, tide_f3(7.0f, 7.0f, 0.0f)) && still(crate));
    TIDE_REQUIRE(rests(&a, 1200)); // Once the ball has gone
    TIDE_CHECK(stays(&a));

    lull(&a, 0, false);
    tick(&a, 20);
    crate = body(&a, 0);
    TIDE_CHECK(!crate.asleep && crate.position.x == 7.0f && crate.position.y < 6.6f);

    TIDE_REQUIRE(rests(&a, 600));
    crate = body(&a, 0);
    TIDE_CHECK(crate.asleep && crate.position.y < 0.6f);
    crate.position.y = 3.0f;
    put(&a, 0, crate);
    tide_world_tick(&a);
    TIDE_CHECK(!body(&a, 0).asleep);
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// What goes to sleep with a body, and wakes with it

TIDE_TEST(box3d_sleep_takes_what_a_body_touches)
{
    start(&a, Case_Plank);
    tick(&a, 8);
    Box3D_Body slider = body(&a, 1);
    TIDE_CHECK(slider.velocity.x > 2.0f && !slider.asleep && !body(&a, 0).asleep);
    const tide_float3 at = slider.position;
    const float sliding = slider.velocity.x;
    lull(&a, 0, true);
    tide_world_tick(&a);
    slider = body(&a, 1);
    printf("    a crate sliding at %g along a plank that's put to sleep: a tick later it's going at %g, at %g where it was at %g\n",
           (double)sliding, (double)slider.velocity.x, (double)slider.position.x, (double)at.x);
    TIDE_CHECK(body(&a, 0).asleep && slider.asleep && same(slider.position, at) && still(slider));
    TIDE_CHECK(!body(&a, 2).asleep && !body(&a, 3).asleep);

    const Box3D_Body lift = body(&a, 2);
    TIDE_CHECK(still(lift));
    lull(&a, 3, true);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 3).asleep && body(&a, 2).asleep && same(body(&a, 2).position, lift.position));
    TIDE_CHECK(stays(&a)); // All four are asleep

    lull(&a, 2, false);
    tide_world_tick(&a);
    TIDE_CHECK(!body(&a, 2).asleep && !body(&a, 3).asleep && body(&a, 0).asleep && body(&a, 1).asleep);

    lull(&a, 0, false);
    tick(&a, 3);
    TIDE_CHECK(!body(&a, 0).asleep && !body(&a, 1).asleep);
    Box3D_Body plank = body(&a, 0);
    slider = body(&a, 1);
    plank.asleep = true;
    slider.impulse = tide_f3(1000.0f, 2000.0f, 0.0f);
    put(&a, 0, plank);
    put(&a, 1, slider);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 0).asleep && body(&a, 1).asleep && still(body(&a, 1)) && body(&a, 1).impulse.y == 0.0f);
    lull(&a, 1, false);
    tick(&a, 3);
    TIDE_CHECK(!body(&a, 0).asleep && !body(&a, 1).asleep);
    plank = body(&a, 0);
    slider = body(&a, 1);
    slider.asleep = true;
    plank.impulse = tide_f3(1000.0f, 2000.0f, 0.0f);
    put(&a, 0, plank);
    put(&a, 1, slider);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 0).asleep && body(&a, 1).asleep && still(body(&a, 0)) && body(&a, 0).impulse.y == 0.0f);

    slider = body(&a, 1);
    throw_ball(&a, slider.position.x, slider.position.y + 6.0f, 0.0f, -12.0f);
    TIDE_REQUIRE(wakes(&a, 1, 120));
    TIDE_CHECK(!body(&a, 0).asleep);
    tide_world_free(&a);
}

TIDE_TEST(box3d_a_stack_goes_to_sleep_from_its_bottom)
{
    start(&a, Case_Stack);
    tick(&a, 15);
    for (int i = 0; i < 7; i++) TIDE_CHECK(!body(&a, i).asleep);
    lull(&a, 0, true);
    tide_world_tick(&a);
    for (int i = 0; i < 5; i++) TIDE_CHECK(body(&a, i).asleep && still(body(&a, i)));
    TIDE_CHECK(!body(&a, 5).asleep && body(&a, 5).velocity.y < -1.0f && !body(&a, 6).asleep);
    const float top = body(&a, 4).position.y;

    int landed = 0;
    for (int i = 1; i < 200 && !landed; i++) {
        tide_world_tick(&a);
        if (!body(&a, 4).asleep) landed = i;
    }
    printf("    a stack put to sleep from its bottom crate wakes %d ticks later, when a crate lands on it\n", landed);
    TIDE_REQUIRE(landed);
    tide_world_tick(&a);
    for (int i = 0; i < 5; i++) TIDE_CHECK(!body(&a, i).asleep);
    TIDE_CHECK(tide_abs_f(body(&a, 4).position.y - top) < 0.2f); // It stood where it slept

    TIDE_REQUIRE(rests(&a, 900));
    for (int i = 0; i < 7; i++) TIDE_CHECK(body(&a, i).asleep);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Spawned asleep

TIDE_TEST(box3d_spawns_a_body_asleep)
{
    start(&a, Case_Spawned);
    tick(&a, 3);
    const Box3D_Body hung = body(&a, 0);
    const Box3D_Body pushed = body(&a, 1);
    TIDE_CHECK(hung.asleep && same(hung.position, tide_f3(0.0f, 5.0f, 0.0f)) && still(hung));
    TIDE_CHECK(pushed.asleep && same(pushed.position, tide_f3(3.0f, 5.0f, 0.0f)) && still(pushed));
    TIDE_CHECK(same(pushed.impulse, tide_f3(0.0f, 0.0f, 0.0f)));
    TIDE_CHECK(!body(&a, 2).asleep); // A static body is neither
    Box3D_Body lift = body(&a, 3);
    TIDE_CHECK(!lift.asleep && lift.velocity.x == 1.0f);
    tick(&a, 40);
    TIDE_CHECK(body(&a, 0).position.y == 5.0f && body(&a, 1).position.y == 5.0f);
    TIDE_CHECK(a.Box3D_Physics.moved); // The lift is going

    lift = body(&a, 3);
    lull(&a, 3, true);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 3).asleep && still(body(&a, 3)) && same(body(&a, 3).position, lift.position));
    TIDE_CHECK(stays(&a));

    const int64_t stamp = a.Box3D_Physics.stamp;
    lull(&a, 2, true);
    tide_world_tick(&a);
    TIDE_CHECK(!body(&a, 2).asleep && !a.Box3D_Physics.moved && a.Box3D_Physics.stamp == stamp);

    lift = body(&a, 3);
    lift.velocity = tide_f3(1.0f, 0.0f, 0.0f);
    put(&a, 3, lift);
    tick(&a, 10);
    TIDE_CHECK(!body(&a, 3).asleep && body(&a, 3).position.x > lift.position.x + 0.1f);
    TIDE_CHECK(body(&a, 0).asleep && body(&a, 1).asleep);

    throw_ball(&a, -6.0f, 5.0f, 12.0f, 0.0f);
    TIDE_REQUIRE(wakes(&a, 0, 120));
    tick(&a, 20);
    TIDE_CHECK(body(&a, 0).position.y < 4.7f && body(&a, 1).asleep && body(&a, 1).position.y == 5.0f);

    // Three that were spawned asleep on each other: each is asleep by itself, as Box3D never saw them
    // touch. A ball into the bottom one wakes it, and the ones on it a step or two after each other, as
    // each is found to rest on one that's awake
    TIDE_CHECK(body(&a, 4).asleep && body(&a, 5).asleep && body(&a, 6).asleep && body(&a, 6).position.y == 2.5f);
    throw_ball(&a, -18.0f, 0.5f, 12.0f, 0.0f);
    int woke[3] = {0, 0, 0};
    for (int i = 1; i < 120 && !woke[2]; i++) {
        tide_world_tick(&a);
        for (int k = 0; k < 3; k++) {
            if (!woke[k] && !body(&a, 4 + k).asleep) woke[k] = i;
        }
    }
    printf("    of three crates spawned asleep on each other, the bottom one wakes when a ball hits it, the next %d ticks later, the top one %d\n",
           woke[1] - woke[0], woke[2] - woke[0]);
    TIDE_CHECK(woke[0] && woke[1] >= woke[0] && woke[2] >= woke[1] && woke[2] - woke[0] <= 6);
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Gravity

TIDE_TEST(box3d_gravity_wakes_what_sleeps_and_not_what_is_put_to_sleep_with_it)
{
    start(&a, Case_Rest);
    tick(&a, 12);
    lull(&a, 1, true);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 1).asleep);
    a.Box3D_Physics.gravity = tide_f3(0.0f, 10.0f, 0.0f);
    tick(&a, 10);
    TIDE_CHECK(!body(&a, 0).asleep && !body(&a, 1).asleep && body(&a, 1).velocity.y > 1.0f);
    const tide_float3 at = body(&a, 0).position;
    a.Box3D_Physics.gravity = tide_f3(0.0f, -10.0f, 0.0f);
    lull(&a, 0, true);
    tide_world_tick(&a);
    TIDE_CHECK(body(&a, 0).asleep && same(body(&a, 0).position, at) && still(body(&a, 0)));
    TIDE_CHECK(!body(&a, 1).asleep);
    tick(&a, 19);
    TIDE_CHECK(same(body(&a, 0).position, at) && body(&a, 1).velocity.y < -1.0f); // It hangs, and the other is on its way down
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// All of it is the match's

// The scripted match after its ticks, on every platform and in every
// configuration: whether a body is asleep is in its component and in Box3D's
// image.
#define SCRIPT_HASH UINT64_C(0xeecb5dc123b2ce6a)
#define SCRIPT_IMAGES_HASH UINT64_C(0x88355a413a34cd95)

enum { SCRIPT_TICKS = 320, KEPT_AT = 95 };

// What the script has done by each of these ticks
static void look(const tide_world *w, const int ticks)
{
    if (ticks == 22) {
        for (int i = 0; i < 5; i++) TIDE_CHECK(body(w, i).asleep); // The stack, from its bottom
        TIDE_CHECK(!body(w, 5).asleep);
    }
    if (ticks == 56) TIDE_CHECK(body(w, 0).asleep && body(w, 4).asleep); // Again, after the crate landed on it
    if (ticks == 66) TIDE_CHECK(!body(w, 0).asleep && !body(w, 4).asleep); // Woken from its middle
    if (ticks == 82) TIDE_CHECK(body(w, 1).asleep && body(w, 4).asleep && still(body(w, 4))); // Asleep, though one was pushed
    if (ticks == 110) {
        const Box3D_Body aside = body(w, 6);
        TIDE_CHECK(aside.asleep && same(aside.position, tide_f3(12.0f, 6.0f, 0.0f)));
    }
    if (ticks == 128) TIDE_CHECK(body(w, 7).asleep && still(body(w, 7)) && body(w, 8).asleep);
    if (ticks == 160) {
        const Box3D_Body late = body(w, 9);
        TIDE_CHECK(late.asleep && same(late.position, tide_f3(-10.0f, 4.0f, 0.0f)));
    }
    if (ticks == 180) TIDE_CHECK(!body(w, 7).asleep && body(w, 7).velocity.x == -0.5f && !body(w, 8).asleep);
    if (ticks == 215) TIDE_CHECK(!body(w, 6).asleep && body(w, 6).position.y < 5.8f);
    if (ticks == 229) TIDE_CHECK(body(w, 3).asleep && !body(w, 6).asleep);
    if (ticks == 236) TIDE_CHECK(body(w, 6).asleep && still(body(w, 6)) && !body(w, 3).asleep && !body(w, 7).asleep);
    if (ticks == 300) TIDE_CHECK(!body(w, 6).asleep);
}

// The scripted match, with every tick's hash. With `again`, C's world is made
// from the match's image before every tick.
static void play(tide_world *w, uint64_t *hashes, const bool again, tide_world *at_kept)
{
    start(w, Case_Script);
    for (int i = 0; i < SCRIPT_TICKS; i++) {
        if (again) b3t_forget();
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w);
        if (!again) look(w, i + 1);
        if (i + 1 == KEPT_AT && at_kept) tide_world_copy(at_kept, w);
    }
}

TIDE_TEST(box3d_sleep_is_the_matchs)
{
    static uint64_t went[SCRIPT_TICKS];
    const int32_t parts_before = in_parts_since(0);
    play(&a, went, false, &kept);
    uint64_t images = 0;
    for (int i = 0; i < SCRIPT_TICKS; i++) images = tide_hash_more(images, &went[i], sizeof went[i]);
    printf("    the world's hash is %016llx, and its %d images' %016llx; %d of them were written in parts\n",
           (unsigned long long)went[SCRIPT_TICKS - 1], (int)SCRIPT_TICKS, (unsigned long long)tide_hash_end(images),
           (int)in_parts_since(parts_before));
    TIDE_CHECK(went[SCRIPT_TICKS - 1] == SCRIPT_HASH);
    TIDE_CHECK(tide_hash_end(images) == SCRIPT_IMAGES_HASH);
    TIDE_CHECK(in_parts_since(parts_before) > SCRIPT_TICKS / 2);

    const int32_t before = b3t_restores();
    for (int round = 0; round < 3; round++) {
        tide_world_copy(&b, &kept);
        for (int i = KEPT_AT; i < SCRIPT_TICKS; i++) {
            tide_world_tick(&b);
            if (tide_world_hash(&b) != went[i]) printf("    round %d: tick %d came out different\n", round, i);
            TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
        }
    }
    TIDE_CHECK(b3t_restores() - before >= 3);

    const uint32_t size = tide_world_pack(&kept, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&kept, bytes, size) == size);
    b3t_forget();
    TIDE_REQUIRE(tide_world_unpack(&b, bytes, size));
    free(bytes);
    for (int i = KEPT_AT; i < SCRIPT_TICKS; i++) {
        tide_world_tick(&b);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
    }

    // A world that's made from its image again before every tick is the world that never was: a body is
    // put to sleep, and woken, in a world that was just made
    static uint64_t again[SCRIPT_TICKS];
    const int32_t restores = b3t_restores();
    play(&b, again, true, NULL);
    TIDE_CHECK(b3t_restores() - restores == SCRIPT_TICKS - 1); // All but the first, which has no image to make it from
    for (int i = 0; i < SCRIPT_TICKS; i++) {
        if (again[i] != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(again[i] == went[i]);
    }
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
    b3t_check(false, false);
}
