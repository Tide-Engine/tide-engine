// The real generated match, with commands encoded as the network sends them.
// These tests exercise both seats, resource choices and a complete escape.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/net.h"
#include "tide_test.h"

#define DT (1.0f / 30.0f)
#define CHARGE 720
#define MIN_VIGIL 990
#define FINALE 240

static tide_world world;

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

static void start(const int players, const uint32_t seed)
{
    tide_world_free(&world);
    tide_world_start_seeded(&world, DT, NULL, seed);
    for (int seat = 0; seat < players; seat++) tide_world_player_joined(&world, player(seat));
    tide_world_tick(&world);
    tide_world_tick(&world);
}

static void tick(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static void hands(const int seat, const bool channel, const int ward, const int serial)
{
    tide_world_set_input(&world, player(seat), (Hands){.channel = channel, .ward = ward, .serial = serial});
}

static uint32_t encode(uint8_t *bytes, const uint32_t id, const int serial, const bool shortcut)
{
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id == TIDE_COMMAND_ID_Plan) tide_bits_put_bool(&bits, shortcut);
    return tide_bits_end(&bits);
}

static bool command(const uint32_t id, const int sender, const int serial, const bool shortcut)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = encode(bytes, id, serial, shortcut);
    return tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
}

static bool act(const uint32_t id, const int sender, const bool shortcut)
{
    const bool sent = command(id, sender, world.Rite.serial, shortcut);
    tide_world_tick(&world);
    return sent;
}

// A careful pilgrim channels in quiet and obeys the room's rule on the tell.
// Their keeper hears the symbol and holds it through each strike. A field is
// sampled afresh each tick, like actual held input; no rule helper is called.
static void careful_tick(tide_world *w, const bool seal)
{
    const Rite *r = &w->Rite;
    bool channel = true;
    if (r->threat == 1) channel = r->safeHold;
    for (int seat = 0; seat < 2; seat++) {
        const bool active = seat == r->active;
        tide_world_set_input(w, player(seat), (Hands){.channel = active && channel,
            .ward = !active && seal ? r->sigil : -1, .serial = r->serial});
    }
    tide_world_tick(w);
}

static int vigil(const bool seal)
{
    int ticks = 0;
    while (world.Rite.phase == Phase_Vigil && ticks < 1500) {
        careful_tick(&world, seal);
        ticks++;
    }
    return ticks;
}

static void force_strike(const bool hold, const bool safe_hold, const int active_ward, const int keeper_ward)
{
    world.Rite.phase = Phase_Vigil;
    world.Rite.threat = 1;
    world.Rite.threatClock = 1;
    world.Rite.sigil = 1;
    world.Rite.safeHold = safe_hold;
    hands(world.Rite.active, hold, active_ward, world.Rite.serial);
    hands(1 - world.Rite.active, false, keeper_ward, world.Rite.serial);
    tide_world_tick(&world);
}

TIDE_TEST(last_light_requires_two_players_and_preserves_a_disconnected_seat)
{
    start(1, 1);
    tick(1800);
    TIDE_CHECK(world.Rite.phase == Phase_Waiting && world.Rite.paused);
    TIDE_CHECK(world.Rite.candle == 100 && world.Rite.turn == 0);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Waiting);

    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_REQUIRE(world.Rite.phase == Phase_Planning);
    TIDE_CHECK(world.Rite.turn == 1 && world.Rite.active == 0);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    for (int i = 0; i < 100; i++) careful_tick(&world, true);
    tide_world_player_left(&world, player(0));
    tide_world_tick(&world);
    const int elapsed = world.Rite.elapsed;
    const int charge = world.Rite.charge;
    const int candle = world.Rite.candle;
    const int serial = world.Rite.serial;
    tick(300);
    TIDE_CHECK(world.Rite.paused && world.Rite.phase == Phase_Vigil);
    TIDE_CHECK(world.Rite.elapsed == elapsed && world.Rite.charge == charge && world.Rite.candle == candle);
    world.Rite.threat = 1;
    world.Rite.threatClock = 1;
    tide_world_player_joined(&world, player(0));
    tick(2);
    TIDE_CHECK(!world.Rite.paused && world.Rite.present.at[0] && world.Rite.active == 0);
    TIDE_CHECK(world.Rite.serial == serial && world.Rite.nerve.at[0] == 3);
    TIDE_CHECK(world.Rite.threat == 0 && world.Rite.threatClock > 60);
    // A duplicated join never creates another participant or moves a seat.
    const uint32_t count = tide_world_entity_count(&world);
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_entity_count(&world) == count);
    tide_world_free(&world);
}

TIDE_TEST(last_light_commands_require_the_active_seat_and_current_phase)
{
    start(2, 2);
    const int serial = world.Rite.serial;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 1, false));
    TIDE_CHECK(world.Rite.phase == Phase_Planning && world.Rite.node == 0);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Plan, 0, serial - 1, false));
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.phase == Phase_Planning && world.Rite.candle == 100);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Return, 0, false));
    TIDE_CHECK(act(TIDE_COMMAND_ID_Linger, 0, false));
    TIDE_CHECK(act(TIDE_COMMAND_ID_Restart, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Planning && world.Rite.turn == 1);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Vigil && world.Rite.serial != serial);
    const int node = world.Rite.node;
    TIDE_CHECK(command(TIDE_COMMAND_ID_Plan, 0, serial, true));
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.node == node && world.Rite.candle == 97);
    tide_world_free(&world);
}

TIDE_TEST(last_light_routes_change_destination_and_resource_risk)
{
    start(2, 3);
    world.Rite.die = 2;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    TIDE_CHECK(world.Rite.node == 2 && world.Rite.safeHold && !world.Rite.shortcut);
    TIDE_CHECK(world.Rite.candle == 97 && world.Rite.dread == 0);
    start(2, 3);
    world.Rite.die = 2;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, true));
    TIDE_CHECK(world.Rite.node == 4 && !world.Rite.safeHold && world.Rite.shortcut);
    TIDE_CHECK(world.Rite.candle == 97 && world.Rite.dread == 10 && world.Rite.wax == 46);
    tide_world_free(&world);
}

TIDE_TEST(last_light_only_the_pilgrim_is_exposed_and_keeper_has_the_ward)
{
    start(2, 4);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    const int wax = world.Rite.wax;
    // An active player's ward cannot seal; their correct release is still safe.
    force_strike(false, false, 1, -1);
    TIDE_CHECK(world.Rite.seals == 0 && world.Rite.wax == wax && world.Rite.nerve.at[0] == 3);
    // A keeper's channel does not earn progress or expose that keeper.
    world.Rite.threat = 0;
    world.Rite.threatClock = 100;
    hands(0, false, -1, world.Rite.serial);
    hands(1, true, -1, world.Rite.serial);
    const int charge = world.Rite.charge;
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.charge == charge);
    force_strike(true, false, 1, -1);
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.note == Note_Hurt);
    TIDE_CHECK(world.Rite.nerve.at[0] == 2 && world.Rite.nerve.at[1] == 3);
    TIDE_CHECK(world.Rite.candle == 91 && world.Rite.carried == 0);
    tide_world_free(&world);
}

TIDE_TEST(last_light_well_requires_holding_and_old_inputs_do_nothing)
{
    start(2, 5);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    force_strike(true, true, -1, -1);
    TIDE_CHECK(world.Rite.phase == Phase_Vigil && world.Rite.nerve.at[0] == 3);
    force_strike(false, true, -1, -1);
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.nerve.at[0] == 2);
    start(2, 5);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    world.Rite.threat = 0;
    world.Rite.threatClock = 100;
    hands(0, true, 1, world.Rite.serial - 1);
    hands(1, true, 1, world.Rite.serial - 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.charge == 0 && world.Rite.ward == -1);
    tide_world_free(&world);
}

TIDE_TEST(last_light_keeper_pays_first_seal_and_optional_exposure_only)
{
    start(2, 6);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    const int wax = world.Rite.wax;
    force_strike(false, false, -1, 1);
    TIDE_CHECK(world.Rite.seals == 1 && world.Rite.wax == wax - 3 && world.Rite.charge == 24);
    force_strike(false, false, -1, 1);
    TIDE_CHECK(world.Rite.seals == 2 && world.Rite.wax == wax - 3 && world.Rite.charge == 24);
    force_strike(true, false, -1, 1);
    TIDE_CHECK(world.Rite.seals == 3 && world.Rite.wax == wax - 6 && world.Rite.charge == 48);
    TIDE_CHECK(world.Rite.nerve.at[0] == 3 && world.Rite.candle == 97);
    tide_world_free(&world);
}

TIDE_TEST(last_light_fragment_requires_time_channel_and_a_keeper_seal)
{
    start(2, 7);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    world.Rite.charge = CHARGE;
    world.Rite.seals = 1;
    world.Rite.elapsed = MIN_VIGIL - 2;
    world.Rite.threat = 0;
    world.Rite.threatClock = 100;
    tick(1);
    TIDE_CHECK(world.Rite.phase == Phase_Vigil && world.Rite.carried == 0);
    tick(1);
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.carried == 1);

    start(2, 7);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    TIDE_CHECK(vigil(false) >= MIN_VIGIL);
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.note == Note_Unsealed && world.Rite.carried == 0);
    TIDE_CHECK(world.Rite.nerve.at[0] == 3 && world.Rite.candle == 97);
    // Empty-handed banking is a deliberate recovery from a depleted wax pool.
    world.Rite.wax = 0;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Return, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Planning && world.Rite.wax == 4 && world.Rite.active == 1);
    tide_world_free(&world);
}

TIDE_TEST(last_light_linger_risks_carried_fragments_and_caps_three_attempts)
{
    start(2, 8);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    TIDE_CHECK(vigil(true) >= MIN_VIGIL);
    TIDE_REQUIRE(world.Rite.carried == 1);
    const int serial = world.Rite.serial;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Linger, 1, false));
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.attempts == 1);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Linger, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Vigil && world.Rite.carried == 1 && world.Rite.candle == 95);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Return, 0, serial, false));
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.fragments == 0 && world.Rite.phase == Phase_Vigil);
    TIDE_CHECK(vigil(true) >= MIN_VIGIL - 1);
    TIDE_CHECK(world.Rite.carried == 2);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Linger, 0, false));
    TIDE_CHECK(vigil(true) >= MIN_VIGIL);
    TIDE_CHECK(world.Rite.carried == 3 && world.Rite.attempts == 3);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Linger, 0, false));
    TIDE_CHECK(world.Rite.phase == Phase_Result && world.Rite.carried == 3);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Return, 0, false));
    TIDE_CHECK(world.Rite.fragments == 3 && world.Rite.carried == 0 && world.Rite.active == 1);
    tide_world_free(&world);
}

TIDE_TEST(last_light_an_injury_loses_unbanked_memory_and_restart_is_a_new_rite)
{
    start(2, 9);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    world.Rite.carried = 2;
    world.Rite.nerve.at[0] = 1;
    force_strike(true, false, -1, -1);
    TIDE_CHECK(world.Rite.phase == Phase_Lost && world.Rite.note == Note_Taken && world.Rite.carried == 0);
    const int serial = world.Rite.serial;
    TIDE_CHECK(command(TIDE_COMMAND_ID_Restart, 1, serial - 1, false));
    tide_world_tick(&world);
    TIDE_CHECK(world.Rite.phase == Phase_Lost);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Restart, 1, false));
    TIDE_CHECK(world.Rite.phase == Phase_Planning && world.Rite.active == 0 && world.Rite.turn == 1);
    TIDE_CHECK(world.Rite.fragments == 0 && world.Rite.candle == 100 && world.Rite.wax == 40);
    TIDE_CHECK(world.Rite.nerve.at[0] == 3 && world.Rite.nerve.at[1] == 3);
    TIDE_CHECK(world.Rite.present.at[0] && world.Rite.present.at[1] && world.Rite.serial > serial);
    tide_world_free(&world);
}

TIDE_TEST(last_light_final_belfry_has_a_second_readable_call)
{
    start(2, 10);
    world.Rite.node = 2;
    world.Rite.die = 1;
    world.Rite.act = 2;
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, false));
    const int second = world.Rite.secondarySigil;
    force_strike(false, false, -1, 1);
    tick(12);
    TIDE_CHECK(world.Rite.threat == 1 && world.Rite.echo && world.Rite.sigil == second);
    TIDE_CHECK(world.Rite.threatClock == 60);
    tide_world_free(&world);
}

TIDE_TEST(last_light_finale_requires_both_hands_together)
{
    start(2, 11);
    world.Rite.phase = Phase_Finale;
    hands(0, true, -1, world.Rite.serial);
    hands(1, false, -1, world.Rite.serial);
    tick(300);
    TIDE_CHECK(world.Rite.phase == Phase_Finale && world.Rite.finaleCharge == 0);
    hands(1, true, -1, world.Rite.serial);
    tick(100);
    TIDE_CHECK(world.Rite.finaleCharge == 100);
    hands(0, false, -1, world.Rite.serial);
    tick(10);
    TIDE_CHECK(world.Rite.finaleCharge == 80);
    hands(0, true, -1, world.Rite.serial);
    tick(FINALE - 80);
    TIDE_CHECK(world.Rite.phase == Phase_Won && world.Rite.note == Note_Escaped);
    tide_world_free(&world);
}

static uint64_t complete_rite(const bool linger, int *vigil_ticks)
{
    start(2, 17);
    *vigil_ticks = 0;
    for (int guard = 0; guard < 80 && world.Rite.phase != Phase_Finale && world.Rite.phase != Phase_Lost; guard++) {
        if (world.Rite.phase == Phase_Interlude) { tick(240); continue; }
        if (world.Rite.phase == Phase_Planning) {
            TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, world.Rite.active, false));
        }
        if (world.Rite.phase == Phase_Vigil) *vigil_ticks += vigil(true);
        TIDE_CHECK(world.Rite.phase == Phase_Result);
        TIDE_CHECK(world.Rite.carried > 0);
        if (world.Rite.phase != Phase_Result || world.Rite.carried <= 0) { tide_world_free(&world); return 0; }
        const uint32_t id = linger && world.Rite.attempts < 3 && world.Rite.fragments + world.Rite.carried < 18
            ? TIDE_COMMAND_ID_Linger : TIDE_COMMAND_ID_Return;
        TIDE_CHECK(act(id, world.Rite.active, false));
    }
    TIDE_CHECK(world.Rite.phase == Phase_Finale && world.Rite.fragments == 18);
    TIDE_CHECK(world.Rite.candle == (linger ? 58 : 46));
    TIDE_CHECK(world.Rite.strikes == 0 && world.Rite.nerve.at[0] == 3 && world.Rite.nerve.at[1] == 3);
    hands(0, true, -1, world.Rite.serial);
    hands(1, true, -1, world.Rite.serial);
    tick(FINALE);
    TIDE_CHECK(world.Rite.phase == Phase_Won);
    const uint64_t hash = tide_world_hash(&world);
    tide_world_free(&world);
    return hash;
}

TIDE_TEST(last_light_eighteen_real_rituals_can_escape_with_both_economies)
{
    int bank_ticks = 0;
    int linger_ticks = 0;
    const uint64_t bank_hash = complete_rite(false, &bank_ticks);
    const uint64_t linger_hash = complete_rite(true, &linger_ticks);
    TIDE_CHECK(bank_ticks >= 18 * MIN_VIGIL && linger_ticks >= 18 * MIN_VIGIL);
    // WASI and native builds must produce these exact complete worlds.
    TIDE_CHECK(bank_hash == UINT64_C(4501482852783830943));
    TIDE_CHECK(linger_hash == UINT64_C(1440756107033208584));
    printf("last_light_complete bank=%llu linger=%llu ritual_ticks=%d/%d\n",
        (unsigned long long)bank_hash, (unsigned long long)linger_hash, bank_ticks, linger_ticks);
}

TIDE_TEST(last_light_snapshots_replay_every_roll_and_risk_identically)
{
    start(2, 29);
    TIDE_CHECK(act(TIDE_COMMAND_ID_Plan, 0, true));
    for (int i = 0; i < 177; i++) careful_tick(&world, true);
    tide_world replay = {0};
    tide_world_copy(&replay, &world);
    for (int i = 0; i < 700 && world.Rite.phase == Phase_Vigil; i++) {
        careful_tick(&world, true);
        careful_tick(&replay, true);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&replay));
    }
    // A world packed for migration carries seats, clocks, wards and resources.
    uint8_t *bytes = malloc(1024 * 1024);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, 1024 * 1024);
    tide_world migrated = {0};
    TIDE_CHECK(size > 0 && tide_world_unpack(&migrated, bytes, size));
    TIDE_CHECK(tide_world_hash(&migrated) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&migrated);
    tide_world_free(&replay);
    tide_world_free(&world);
}

typedef struct network_hand { tide_client *client; } network_hand;

static void network_sample(void *user, const uint32_t tick_number, void *input)
{
    (void)tick_number;
    const network_hand *hand = user;
    const tide_world *view = tide_client_world(hand->client);
    Hands sampled = {.ward = -1, .serial = -1};
    if (view) {
        const Rite *r = &view->Rite;
        const tide_player_id me = tide_client_status_of(hand->client).player;
        const bool active = tide_player_equal(me, r->players.at[r->active]);
        sampled.serial = r->serial;
        sampled.channel = active && (r->threat != 1 || r->safeHold);
        sampled.ward = active ? -1 : r->sigil;
    }
    *(Hands *)input = sampled;
}

TIDE_TEST(last_light_real_two_client_ritual_agrees_over_loss_and_reordering)
{
    tide_loopback *net = tide_loopback_create(53);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.06, .jitter = 0.04, .loss = 0.05});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api,
        .tick_rate = 30, .seed = 31, .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    network_hand players[2] = {{0}, {0}};
    for (int seat = 0; seat < 2; seat++) {
        players[seat].client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)seat + 2u), .server = tide_loopback_address(1),
            .sample = network_sample, .user = &players[seat], .lead = 1}, 0);
    }
    uint64_t hashes[4096] = {0};
    bool same = true;
    bool sent = false;
    int finished = -1;
    int compared = 0;
    for (int frame = 1; frame < 3600; frame++) {
        const double now = (double)frame / 60.0;
        tide_loopback_set_time(net, now);
        for (int seat = 0; seat < 2; seat++) tide_client_update(players[seat].client, now);
        tide_server_update(server, now);
        const tide_world *authoritative = tide_server_world(server);
        const uint32_t server_tick = tide_server_tick(server);
        if (server_tick < 4096) hashes[server_tick] = tide_world_hash(authoritative);
        for (int seat = 0; seat < 2; seat++) {
            const tide_client_status status = tide_client_status_of(players[seat].client);
            const tide_world *verified = tide_client_verified_world(players[seat].client);
            if (verified && status.verified_tick < 4096 && hashes[status.verified_tick]) {
                same = same && hashes[status.verified_tick] == tide_world_hash(verified);
                compared++;
            }
        }
        if (!sent && frame > 300 && authoritative->Rite.phase == Phase_Planning) {
            uint8_t bytes[TIDE_COMMAND_BYTES];
            const uint32_t size = encode(bytes, TIDE_COMMAND_ID_Plan, authoritative->Rite.serial, false);
            for (int seat = 0; seat < 2; seat++) {
                const tide_player_id me = tide_client_status_of(players[seat].client).player;
                if (tide_player_equal(me, authoritative->Rite.players.at[authoritative->Rite.active]))
                    sent = tide_client_send_command(players[seat].client, bytes, size);
            }
        }
        if (authoritative->Rite.phase == Phase_Result && finished < 0) finished = frame;
        if (finished >= 0 && frame > finished + 120) break;
    }
    const tide_world *result = tide_server_world(server);
    TIDE_CHECK(sent && finished >= 0 && result->Rite.carried == 1);
    TIDE_CHECK(result->Rite.strikes == 0 && result->Rite.candle == 97);
    TIDE_CHECK(same && compared > 1000);
    for (int seat = 0; seat < 2; seat++) {
        TIDE_CHECK(tide_client_status_of(players[seat].client).resyncs == 0);
        tide_client_destroy(players[seat].client);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
