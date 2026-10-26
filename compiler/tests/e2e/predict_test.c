#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world snapshot;

static Controls defaults(void)
{
    const tide_devices devices = {0};
    return tide_input_sample(&devices, NULL);
}

static Controls active(void)
{
    Controls input = defaults();
    input.move = 1.0f;
    input.jump = input.dash = input.details.fire = true;
    input.choice = input.details.level = 99;
    input.selection.ok = true;
    input.selection.value = 42;
    input.axes.at[0] = input.axes.at[1] = 9.0f;
    input.aim = 8.0f;
    input.fade = 5.0f;
    return input;
}

TIDE_TEST(predict_adjusts_guesses_and_preserves_samples_and_snapshots)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const Controls input = active();
    tide_world_set_input(&world, tide_player_from_index(0), input);
    tide_world_set_input(&world, tide_player_from_index(1), input);
    tide_world_set_server_input(&world, input);
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&snapshot);
    tide_player_set keep = {0};
    tide_players_add(&keep, 0);
    const uint32_t scratch = tide_scratch_mark();
    TIDE_REQUIRE(tide_game_api.predict_inputs != NULL);
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep));
    TIDE_CHECK(world.inputs[0].jump && world.inputs[0].dash && world.inputs[0].fade == 5.0f);
    const Controls guess = world.inputs[1];
    TIDE_CHECK(!guess.jump && !guess.dash && !guess.details.fire);
    TIDE_CHECK(guess.choice == 3 && guess.details.level == 7 && !guess.selection.ok && guess.selection.value == 0);
    TIDE_CHECK(guess.axes.at[0] == 1.0f && guess.axes.at[1] == 2.0f);
    TIDE_CHECK(guess.move == 1.0f && guess.aim == 8.0f && guess.fade == 4.0f && guess.computed == 2.0f);
    TIDE_CHECK(guess.deliberate); // Predict runs after attribute defaults.
    TIDE_CHECK(!world.inputs[TIDE_SERVER_INPUT].jump && world.inputs[TIDE_SERVER_INPUT].fade == 4.0f);
    TIDE_CHECK(memcmp(&guess, &world.previous_inputs[1], sizeof guess) == 0);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash && snapshot.inputs[1].jump);
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep) && world.inputs[1].fade == 3.0f);

    // Guesses pass through repair/bounds/Sanitize, and retain zeroed padding.
    world.inputs[1].fade = 50.0f;
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep) && world.inputs[1].fade == 10.0f);
    world.inputs[1].move = 0.0f;
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep) && world.inputs[1].computed == 0.0f);
    const uint32_t nan = 0x7FC00000u;
    memcpy(&world.inputs[1].aim, &nan, sizeof nan);
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep) && world.inputs[1].aim == 0.0f);
    TIDE_CHECK(tide_scratch_mark() == scratch);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

TIDE_TEST(predict_idle_guess_changes_nothing)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const uint64_t hash = tide_world_hash(&world);
    const tide_player_set keep = {0};
    TIDE_CHECK(!tide_game_api.predict_inputs(&world, &keep));
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_world_free(&world);
}

static void sample(void *user, uint32_t tick, void *out)
{
    (void)user;
    Controls input = defaults();
    input.aim = 8.0f;
    if (tick >= 300u && tick < 420u) input = active();
    *(Controls *)out = input;
}

static bool body(const void *data, tide_player_id player, Body *out)
{
    const tide_world *w = data;
    for (uint32_t row = 0; w && row < w->arch0_Owner_Body.count; row++) {
        if (tide_player_index(TIDE_AT(w, arch0_Owner_Body, Owner, row).player) != tide_player_index(player)) continue;
        *out = TIDE_AT(w, arch0_Owner_Body, Body, row);
        return true;
    }
    return false;
}

static void network(tide_net_conditions conditions)
{
    tide_loopback *net = tide_loopback_create(41);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0.0);
    const tide_client_desc rd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1), .sample = sample, .lead = 2};
    tide_client *runner = tide_client_create(&rd, 0.0);
    const tide_client_desc wd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 3),
        .server = tide_loopback_address(1), .lead = 2};
    tide_client *watcher = tide_client_create(&wd, 0.0);
    int own_actions = 0;
    int remote_motion = 0;
    int compared = 0;
    uint32_t quiet_again = 0;
    for (uint32_t frame = 1; frame <= 900u; frame++) {
        const double now = (double)frame / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(runner, now);
        tide_client_update(watcher, now);
        tide_server_update(server, now);
        if (frame == 840u) quiet_again = tide_client_status_of(watcher).again_ticks;
        const tide_player_id player = tide_client_status_of(runner).player;
        Body shown, known, own;
        if (!body(tide_client_world(watcher), player, &shown)
            || !body(tide_client_verified_world(watcher), player, &known)) continue;
        compared++;
        TIDE_CHECK(shown.jumps == known.jumps && shown.dashes == known.dashes && shown.fires == known.fires);
        TIDE_CHECK(shown.downs == known.downs && shown.ups == known.ups && shown.fireUps == known.fireUps);
        TIDE_CHECK(shown.facing == known.facing);
        const int moved = (int)(shown.position - known.position);
        TIDE_CHECK(moved >= 0 && moved <= 9); // The existing 0.15-second cutoff still applies.
        if (moved > remote_motion) remote_motion = moved;
        if (body(tide_client_world(runner), player, &own) && own.jumps - known.jumps > own_actions)
            own_actions = own.jumps - known.jumps;
    }
    TIDE_CHECK(compared > 500 && own_actions > 9 && remote_motion == 9);
    TIDE_CHECK(tide_client_status_of(watcher).again_ticks == quiet_again);
    TIDE_CHECK(tide_client_status_of(runner).resyncs == 0 && tide_client_status_of(watcher).resyncs == 0);
    TIDE_CHECK(tide_client_status_of(runner).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(watcher).state == TIDE_SESSION_CONNECTED);
    Body truth, verified;
    const tide_player_id player = tide_client_status_of(runner).player;
    TIDE_REQUIRE(body(tide_server_world(server), player, &truth));
    TIDE_REQUIRE(body(tide_client_verified_world(watcher), player, &verified));
    TIDE_CHECK(truth.jumps > 90 && truth.jumps == truth.dashes && truth.jumps == truth.fires);
    TIDE_CHECK(memcmp(&truth, &verified, sizeof truth) == 0);
    tide_client_destroy(runner);
    tide_client_destroy(watcher);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(predict_actions_stay_real_over_latency) { network((tide_net_conditions){.latency = 0.25}); }
TIDE_TEST(predict_actions_stay_real_over_loss_and_jitter)
{
    network((tide_net_conditions){.latency = 0.12, .jitter = 0.06, .loss = 0.2});
}
