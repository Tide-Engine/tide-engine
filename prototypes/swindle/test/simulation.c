#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "tide/net.h"
#include "tide_test.h"

static tide_world world, saved, restored;
#define heist world.Heist
#define thief(s) heist.thieves.at[s]
static tide_player_id player(int seat) { return tide_player_from_index(seat); }
static void ticks(int count) { for (int i = 0; i < count; i++) tide_world_tick(&world); }
static void begin(int humans)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    for (int s = 0; s < humans; s++) tide_world_player_joined(&world, player(s));
    ticks(3);
}
static bool deal(int sender, int mode, bool couch, bool bots)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Deal, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(mode));
    tide_bits_put(&bits, couch, 1);
    tide_bits_put(&bits, bots, 1);
    const uint32_t size = tide_bits_end(&bits);
    const bool accepted = tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
    ticks(1);
    return accepted;
}
static void start(int humans, int mode, bool bots)
{
    begin(humans);
    (void)deal(0, mode, false, bots);
    ticks(181);
}
static void input(int seat, float x, float y, bool dash, bool bluff)
{
    tide_world_set_input(&world, player(seat), (Controls){.move = {x, y}, .dash = dash, .bluff = bluff});
}
TIDE_TEST(swindle_authorization_roster_couch_countdown)
{
    begin(2);
    TIDE_CHECK(heist.hostSeat == 0);
    TIDE_CHECK(deal(1, 1, false, true));
    TIDE_CHECK(heist.phase == RoundPhase_Ready);
    TIDE_CHECK(deal(0, 999, false, true));
    TIDE_CHECK(heist.phase == RoundPhase_Ready);
    TIDE_CHECK(deal(0, 0, true, true));
    TIDE_CHECK(heist.phase == RoundPhase_Ready);
    TIDE_CHECK(deal(0, 0, false, false));
    TIDE_CHECK(heist.phase == RoundPhase_Countdown);
    const float x = thief(0).position.x;
    input(0, 1, 0, true, true);
    ticks(100);
    TIDE_CHECK(thief(0).position.x == x);
    TIDE_CHECK(heist.carrier == -1);
    tide_world_player_left(&world, player(0));
    ticks(2);
    TIDE_CHECK(heist.hostSeat == 1);
    TIDE_CHECK(!heist.present.at[0]);
    begin(1);
    TIDE_CHECK(deal(0, 0, true, false));
    ticks(181);
    const float second_x = thief(1).position.x;
    tide_world_set_input(&world, player(0), (Controls){.second = {-1, 0}, .secondDash = true});
    ticks(1);
    TIDE_CHECK(thief(1).position.x < second_x);
    TIDE_CHECK(thief(1).dashCooldown > 1);
    begin(4);
    TIDE_CHECK(deal(0, 0, false, true));
    TIDE_CHECK(heist.phase == RoundPhase_Ready);
    TIDE_CHECK(deal(0, 2, false, true));
    TIDE_CHECK(heist.mode == MatchMode_Teams);
}
TIDE_TEST(swindle_pickup_steal_value_cash_out)
{
    start(2, 0, false);
    thief(0).position = (tide_float2){0, 0};
    ticks(1);
    TIDE_CHECK(heist.carrier == 0);
    heist.charge = 9.5f;
    thief(0).safe = 0;
    thief(1).position = (tide_float2){-40, 0};
    input(1, 1, 0, true, false);
    ticks(1);
    TIDE_CHECK(heist.carrier == 1);
    TIDE_CHECK(thief(1).steals == 1);
    TIDE_CHECK(thief(0).stun > 0);
    TIDE_CHECK(heist.charge > 9.5f);
    thief(1).position = (tide_float2){-390, -190};
    thief(1).dashTime = 0;
    thief(1).push = (tide_float2){0, 0};
    input(1, 0, 0, false, false);
    ticks(42);
    TIDE_CHECK(thief(1).score == 5);
    TIDE_CHECK(heist.carrier == -1);
    TIDE_CHECK(heist.charge == 0);
    TIDE_CHECK(heist.jewelWait > 0);
    TIDE_CHECK(heist.jewel.x == 0 && heist.jewel.y == 0);
}
TIDE_TEST(swindle_bluff_replica_stun_cooldowns)
{
    start(2, 0, false);
    heist.carrier = 0;
    thief(0).position = (tide_float2){0, 0};
    input(0, 0, -1, false, true);
    ticks(1);
    TIDE_CHECK(heist.replicas.at[0].life > 2);
    TIDE_CHECK(heist.replicas.at[0].jewel);
    TIDE_CHECK(thief(0).cloak > 0.9f);
    TIDE_CHECK(thief(0).bluffCooldown > 5);
    thief(0).position = (tide_float2){150, 0};
    thief(1).position = (tide_float2){-40, 0};
    input(0, 0, 0, false, false);
    input(1, 1, 0, true, false);
    ticks(1);
    TIDE_CHECK(heist.replicas.at[0].life == 0);
    TIDE_CHECK(thief(1).stun > 0.6f);
    TIDE_CHECK(thief(0).fooled == 1);
    TIDE_CHECK(heist.carrier == 0);
    const float cooldown = thief(0).bluffCooldown;
    input(0, 0, 0, false, true);
    ticks(1);
    TIDE_CHECK(thief(0).bluffCooldown < cooldown);
    TIDE_CHECK(heist.replicas.at[0].life == 0);
    ticks(160);
    TIDE_CHECK(thief(0).cloak == 0);
    TIDE_CHECK(thief(1).dashCooldown == 0);
}
TIDE_TEST(swindle_teams_exit_overtime)
{
    start(4, 2, false);
    heist.carrier = 0;
    thief(0).position = (tide_float2){0, 0};
    thief(0).safe = 0;
    thief(2).position = (tide_float2){-40, 0};
    input(2, 1, 0, true, false);
    ticks(1);
    TIDE_CHECK(heist.carrier == 0);
    TIDE_CHECK(thief(0).stun == 0);
    heist.exitClock = 0.001f;
    ticks(1);
    TIDE_CHECK(heist.exit == 1);
    TIDE_CHECK(heist.exitClock > 11);
    heist.clock = 0.001f;
    ticks(1);
    TIDE_CHECK(heist.overtime);
    TIDE_CHECK(heist.phase == RoundPhase_Playing);
    thief(0).position = (tide_float2){390, -190};
    ticks(42);
    TIDE_CHECK(heist.phase == RoundPhase_Finished);
    TIDE_CHECK(heist.winner == 0);
    TIDE_CHECK(thief(0).score >= 2);
}
TIDE_TEST(swindle_bounds_walls_invalid_input)
{
    start(2, 0, false);
    thief(0).position = (tide_float2){-245, -105};
    input(0, 1, 0, true, false);
    ticks(12);
    TIDE_CHECK(thief(0).position.x <= -243);
    thief(0).position = (tide_float2){433, 232};
    input(0, 1, 1, false, false);
    ticks(60);
    TIDE_CHECK(thief(0).position.x <= 435);
    TIDE_CHECK(thief(0).position.y <= 235);
    input(0, NAN, INFINITY, false, false);
    ticks(10);
    TIDE_CHECK(isfinite(thief(0).position.x) && isfinite(thief(0).position.y));
}
TIDE_TEST(swindle_bot_rounds_all_modes_maps)
{
    const uint64_t expected[9] = {
        UINT64_C(0x65db7a968f3983be), UINT64_C(0xb9fe20a13c471fb5), UINT64_C(0x8ce5c31d7e592d68),
        UINT64_C(0xffef9290a61ccedc), UINT64_C(0xdbbcd7313efbc9e2), UINT64_C(0xc1015e81658ea7af),
        UINT64_C(0xa8a700fc30926305), UINT64_C(0x3bca0e597f344484), UINT64_C(0x2ce0ec8cead71696)
    };
    int banks = 0, steals = 0, fooled = 0;
    for (int mode = 0; mode < 3; mode++) {
        for (int map = 0; map < 3; map++) {
            begin(1);
            TIDE_REQUIRE(deal(0, mode, false, true));
            heist.map = map;
            heist.present.at[0] = false;
            ticks(12000);
            TIDE_CHECK(heist.phase == RoundPhase_Finished);
            TIDE_CHECK(heist.winner >= 0);
            int total = 0;
            for (int s = 0; s < (mode == 0 ? 2 : 4); s++) {
                total += thief(s).score;
                banks += thief(s).score;
                steals += thief(s).steals;
                fooled += thief(s).fooled;
                TIDE_CHECK(isfinite(thief(s).position.x) && isfinite(thief(s).position.y));
            }
            TIDE_CHECK(total >= 8);
            TIDE_CHECK(tide_world_hash(&world) == expected[mode * 3 + map]);
            printf("SWINDLE mode=%d map=%d total=%d hash=%016llx\n", mode, map, total, (unsigned long long)tide_world_hash(&world));
        }
    }
    TIDE_CHECK(banks >= 80);
    TIDE_CHECK(steals >= 15);
    TIDE_CHECK(fooled > 0);
}
TIDE_TEST(swindle_snapshot_pack_replay)
{
    start(1, 1, true);
    ticks(500);
    tide_world_free(&saved);
    tide_world_copy(&saved, &world);
    const uint32_t capacity = 1024 * 1024;
    uint8_t *bytes = malloc(capacity);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, capacity);
    TIDE_REQUIRE(size > 0);
    tide_world_free(&restored);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    ticks(600);
    for (int i = 0; i < 600; i++) tide_world_tick(&saved);
    TIDE_CHECK(tide_world_hash(&saved) == tide_world_hash(&world));
    tide_world_free(&saved);
    tide_world_free(&restored);
    tide_world_free(&world);
}
