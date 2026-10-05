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
#include "tide_test.h"

// bridge.c
void b3t_forget(void);
void b3t_check(bool on, bool whenever);
void b3t_images(int32_t *whole, int32_t *in_parts);
int32_t b3t_restores(void);

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
    tide_frame(&a, &previous, 0.5f, NULL, &local, &draw, &gui);
    // Halfway between the two ticks, it's turned half of what it turned between them
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
    tide_frame(&a, &previous, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.position.x == drawn.position.x && alike(local.Seen.rotation, drawn.rotation) > 0.99999f);

    // The same tick as the server had it: a meter to the side, and turned a radian more, around another axis
    tide_world_copy(&b, &a);
    Box3D_Body said = drawn;
    said.position.x += 1.0f;
    said.rotation = tide_mul_q(tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), 1.0f), drawn.rotation);
    put(&b, tide_has_Spinner, said);
    const uint64_t hash = tide_world_hash(&b);
    const tide_view_correction correction = {&a, &previous, &b, &previous, 0, 1.0f, false};
    tide_local_correct(&local, &correction);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, &local, &draw, &gui);
    printf("    drawn at %g, and said to be at %g and turned a radian more: a view sees it at %g, turned %g of the way", (double)drawn.position.x,
           (double)said.position.x, (double)local.Seen.position.x,
           (double)(2.0f * tide_acos_f(tide_min_f(1.0f, alike(local.Seen.rotation, drawn.rotation)))));
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x) && alike(local.Seen.rotation, drawn.rotation) > 0.9999f);
    // ...and halfway there 75 ms later
    tide_local_frame_time(&local, 0.075f);
    tide_draw_reset(&draw);
    tide_frame(&b, &previous, 1.0f, NULL, &local, &draw, &gui);
    const float turned = 2.0f * tide_acos_f(tide_min_f(1.0f, alike(local.Seen.rotation, drawn.rotation)));
    printf(", then at %g and %g\n", (double)local.Seen.position.x, (double)turned);
    TIDE_CHECK(near(local.Seen.position.x, drawn.position.x + 0.5f) && tide_abs_f(turned - 0.5f) < 0.01f);
    TIDE_CHECK(near(alike(local.Seen.rotation, drawn.rotation), alike(local.Seen.rotation, said.rotation)));
    // None of it is the match's: the world is as the server said
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
    // A crate a meter wide weighs a ton, and turns as a sixth of that
    TIDE_CHECK(near(thrown.velocity.x, 3.0f) && thrown.velocity.y == 0.0f && near(thrown.angularVelocity.z, 3.0f));
    TIDE_CHECK(thrown.impulse.x == 0.0f && thrown.angularImpulse.z == 0.0f); // Given, once
    tick(&a, 1);
    // ...as the same impulses do that are written into a body that's there
    const Box3D_Body nudged = body_of(&a, tide_has_Nudged);
    TIDE_CHECK(nudged.velocity.x == thrown.velocity.x && nudged.velocity.y == thrown.velocity.y
               && nudged.angularVelocity.z == thrown.angularVelocity.z);
    tick(&a, 30);
    TIDE_CHECK(body_of(&a, tide_has_Thrown).velocity.x == thrown.velocity.x); // And not again
    tide_world_free(&a);
}
