#include <string.h>

#include "predict_near.h"
#include "tide_test.h"

// predictionRange with a physics package (box2d/test/predict_near.tide,
// box3d/test/predict_near.tide), compiled against each package's game: the
// step holds what's far from a client's player where the server last
// confirmed it (HoldFar), so in every predicted world a far ball rolling the
// whole time is byte for byte its confirmed self, while a near ball rolls
// ahead. The near ball rolls into a wall just past the range, which a client's
// predicted ticks hold as a static body: the ball stops at it, as on the
// server. Every confirmed tick still comes out as the server's.

#if PREDICT_NEAR_3D
typedef Box3D_Body BODY;
#define BODY_GET tide_get_Box3D_Body
#define HOLDS b3t_holds
#define HELD b3t_held
#define CHECK_ARENA b3t_check
#define TEST_NAME box3d_predicted_ticks_hold_far_bodies_as_confirmed
#else
typedef Box2D_Body BODY;
#define BODY_GET tide_get_Box2D_Body
#define HOLDS b2t_holds
#define HELD b2t_held
#define CHECK_ARENA b2t_check
#define TEST_NAME box2d_predicted_ticks_hold_far_bodies_as_confirmed
#endif
#define NAMED_TEST(name) TIDE_TEST(name) // TEST_NAME expanded first
#define WALL_HALF 0.25f  // The wall's half width
#define BALL_RADIUS 0.5f

int32_t HOLDS(void); // The package's bridge: holds made, and bodies they put to sleep
int32_t HELD(void);
void CHECK_ARENA(bool on, bool whenever); // Every write the package makes is followed: a held world restores whole

// What the near system Look found of the far ball by a ray after the step
static float seen(const void *world)
{
    const tide_world *w = world;
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_entity_alive(&w->entities, e) && tide_has_Seen(w, e)) return tide_get_Seen(w, e).farX;
    }
    return -2;
}

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[2];
static double now;

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static tide_client *join(const uint32_t endpoint)
{
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, endpoint),
        .server = tide_loopback_address(1),
        .sample = sample,
        .lead = 2,
    };
    return tide_client_create(&desc, now);
}

// A ball's body in a world: false while it has none.
static bool ball(const void *world, const int32_t id, BODY *out)
{
    const tide_world *w = world;
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_entity_alive(&w->entities, e) || !tide_has_Ball(w, e) || tide_get_Ball(w, e).id != id) continue;
        *out = BODY_GET(w, e);
        return true;
    }
    return false;
}

NAMED_TEST(TEST_NAME)
{
    now = 0.0;
    net = tide_loopback_create(23);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.1});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    clients[0] = join(2);
    clients[1] = join(3);
    CHECK_ARENA(true, true);
    const int32_t holds_before = HOLDS();
    int frames = 0, far_seen = 0, far_same = 0, wall_same = 0, near_ahead = 0, stopped = 0;
    float nearest = 1e9f;
    bool odd = false;
    while (now < 9.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int c = 0; c < 2; c++) tide_client_update(clients[c], now);
        tide_server_update(server, now);
        for (int c = 0; c < 2; c++) {
            const tide_client_status status = tide_client_status_of(clients[c]);
            if (status.state != TIDE_SESSION_CONNECTED || status.predicted_tick <= status.verified_tick) continue;
            const void *shown = tide_client_world(clients[c]), *known = tide_client_verified_world(clients[c]);
            BODY near_shown, near_known, wall_shown, wall_known, far_shown, far_known;
            if (!ball(shown, 1, &near_shown) || !ball(known, 1, &near_known) || !ball(shown, 2, &wall_shown)
                || !ball(known, 2, &wall_known) || !ball(shown, 3, &far_shown) || !ball(known, 3, &far_known)) continue;
            frames++;
            far_same += memcmp(&far_shown, &far_known, sizeof far_shown) == 0;
            // ...and the physics world has it there too, where a ray finds its near side
            far_seen += tide_abs_f(seen(shown) - (far_shown.position.x - BALL_RADIUS)) < 1e-3f;
            wall_same += memcmp(&wall_shown, &wall_known, sizeof wall_shown) == 0;
            near_ahead += near_shown.position.x != near_known.position.x;
            // Where the near ball's centre gets to against the wall's face
            const float gap = (wall_shown.position.x - WALL_HALF) - (near_shown.position.x + BALL_RADIUS);
            if (gap < nearest) nearest = gap;
            stopped += now > 7.0 && near_shown.velocity.x < 0.5f;
        }
    }
    TIDE_CHECK(frames > 500);
    TIDE_CHECK(far_same == frames);   // Held in every predicted tick
    TIDE_CHECK(wall_same == frames);
    TIDE_CHECK(far_seen == frames);
    TIDE_CHECK(near_ahead > frames / 2); // Run ahead while it rolls
    TIDE_CHECK(nearest > -0.05f);     // Never through the wall
    TIDE_CHECK(stopped > 50);         // ...and stopped by it
    // Each run of predicted ticks held the far bodies once, putting the far ball to sleep
    TIDE_CHECK(HOLDS() - holds_before > 100);
    TIDE_CHECK(HELD() > 0);
    // Every confirmed tick is the server's: nothing resynced, and the far ball rolled on
    BODY truth, known;
    for (int c = 0; c < 2; c++) {
        TIDE_CHECK(tide_client_status_of(clients[c]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(clients[c]).resyncs == 0);
    }
    TIDE_CHECK(ball(tide_server_world(server), 3, &truth) && ball(tide_client_verified_world(clients[0]), 3, &known));
    TIDE_CHECK(truth.position.x > 70.0f && known.position.x > 70.0f && truth.position.x - known.position.x < 2.0f);
    for (int c = 0; c < 2; c++) tide_client_destroy(clients[c]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    CHECK_ARENA(false, false);
}
