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
