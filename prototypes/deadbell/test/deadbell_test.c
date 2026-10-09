#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)
#define CROWD_REPLAY_HASH UINT64_C(0xbeea342b979203e0)

static tide_world world;

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

static void ticks(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

// The generated game's validator checks every packet before it is submitted.
// The four commands have either no payload or one signed integer.
static bool command(const uint32_t id, const int sender, const tide_entity target, const int value)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, target.index, 32);
    tide_bits_put(&bits, target.generation, 32);
    if (id == TIDE_COMMAND_ID_EquipWeapon || id == TIDE_COMMAND_ID_ChooseRelic)
        tide_bits_put_varint(&bits, tide_zigzag(value));
    const uint32_t size = tide_bits_end(&bits);
    const uint32_t sequence = tide_game_api.command_sequence(&world, player(sender), 0) + 1;
    return tide_game_api.check_command(bytes, size, NULL)
        && tide_game_api.command(&world, player(sender), 0, sequence, bytes, size);
}

static void setup(const int players)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    ticks(2);
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    ticks(2);
}

static void begin(void)
{
    setup(2);
    TIDE_CHECK(command(TIDE_COMMAND_ID_StartRun, 0, (tide_entity){0}, 0));
    ticks(2);
    TIDE_CHECK(world.Expedition.phase == RunPhase_March);
}

static tide_entity hero_entity(const int seat)
{
    return seat == 0 ? world.Expedition.hero0 : world.Expedition.hero1;
}

static void place(const int seat, const tide_float2 at)
{
    const tide_entity entity = hero_entity(seat);
    Hero hero = tide_get_Hero(&world, entity);
    hero.pos = at;
    hero.dashTime = 0;
    TIDE_CHECK(tide_set_Hero(&world, entity, hero));
}

static void protect(void)
{
    for (int seat = 0; seat < 2; seat++) {
        const tide_entity entity = hero_entity(seat);
        Hero hero = tide_get_Hero(&world, entity);
        hero.invulnerable = 10000;
        TIDE_CHECK(tide_set_Hero(&world, entity, hero));
    }
}

static int enemies(void)
{
    int count = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        if (tide_has_Enemy(&world, tide_entity_in_slot(&world.entities, i))) count++;
    }
    return count;
}

static tide_entity find_enemy(const bool keeper)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Enemy(&world, entity)) continue;
        if ((tide_get_Enemy(&world, entity).kind == EnemyKind_Warden) == keeper) return entity;
    }
    return (tide_entity){0};
}

static void input(const int seat, const Controls controls)
{
    tide_world_set_input(&world, player(seat), controls);
}

static void quiet(void)
{
    input(0, (Controls){0});
    input(1, (Controls){0});
}

TIDE_TEST(deadbell_needs_two_players_and_authorizes_each_command)
{
    // A join can arrive immediately, before the initial spawn queue settles.
    tide_world_init(&world, DT);
    tide_world_player_joined(&world, player(0));
    ticks(3);
    TIDE_CHECK(world.Expedition.connected == 1);
    TIDE_CHECK(tide_get_Hero(&world, world.Expedition.hero0).active);
    TIDE_CHECK(tide_get_Owner(&world, world.Expedition.hero0).player.id == player(0).id);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_StartRun, 0, (tide_entity){0}, 0));
    ticks(2);
    TIDE_CHECK(world.Expedition.phase == RunPhase_Waiting);

    // Test-mode companions satisfy the player count, but grant no outsider
    // permission to start the expedition.
    world.Expedition.testBots = true;
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_StartRun, 1, (tide_entity){0}, 0));
    ticks(1);
    TIDE_CHECK(world.Expedition.phase == RunPhase_Waiting);
    world.Expedition.testBots = false;

    tide_world_player_joined(&world, player(1));
    ticks(2);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_EquipWeapon, 0, hero_entity(1), 2));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).weapon == 0);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_EquipWeapon, 1, hero_entity(1), 1));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).weapon == 1);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_StartRun, 1, (tide_entity){0}, 0));
    ticks(2);
    TIDE_CHECK(world.Expedition.phase == RunPhase_March);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_EquipWeapon, 1, hero_entity(1), 2));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).weapon == 1);

    const tide_entity partner = hero_entity(1);
    tide_world_player_left(&world, player(1));
    ticks(2);
    const float clock = world.Expedition.clock;
    const tide_float2 position = tide_get_Hero(&world, hero_entity(0)).pos;
    input(0, (Controls){.move = {1, 0}, .fire = true});
    ticks(90);
    TIDE_CHECK(world.Expedition.clock == clock);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).pos.x == position.x);
    tide_world_player_joined(&world, player(1));
    ticks(2);
    TIDE_CHECK(tide_entity_equal(partner, hero_entity(1)));
    TIDE_CHECK(world.Expedition.phase == RunPhase_March && world.Expedition.connected == 2);
    tide_world_free(&world);
}

TIDE_TEST(deadbell_heavy_primes_and_a_partner_ruptures_from_the_first_district)
{
    begin();
    world.Expedition.spawnClock = 1;
    ticks(2);
    const tide_entity target = find_enemy(false);
    TIDE_REQUIRE(tide_has_Enemy(&world, target));
    Enemy enemy = tide_get_Enemy(&world, target);
    enemy.pos = tide_f2(0, 9);
    enemy.attackCooldown = 10;
    TIDE_REQUIRE(tide_set_Enemy(&world, target, enemy));
    place(0, tide_f2(-0.25f, 0));
    place(1, tide_f2(0.25f, 0));
    input(0, (Controls){.aim = {0, 100}, .heavy = true});
    ticks(1);
    TIDE_REQUIRE(tide_has_Enemy(&world, target));
    enemy = tide_get_Enemy(&world, target);
    TIDE_CHECK(enemy.health < enemy.maxHealth && enemy.prime > 0);
    input(0, (Controls){0});
    input(1, (Controls){.aim = enemy.pos, .fire = true});
    const int killed = world.Expedition.kills;
    ticks(2);
    TIDE_CHECK(!tide_has_Enemy(&world, target));
    TIDE_CHECK(world.Expedition.kills > killed && world.Expedition.ruptures > 0);
    tide_world_free(&world);
}

TIDE_TEST(deadbell_warned_area_attacks_hurt_both_players_and_spare_the_outside)
{
    begin();
    world.Expedition.spawnClock = 1;
    ticks(2);
    const tide_entity target = find_enemy(false);
    TIDE_REQUIRE(tide_has_Enemy(&world, target));
    quiet();
    for (int kind = 0; kind < 2; kind++) {
        for (int outside = 0; outside < 2; outside++) {
            for (int seat = 0; seat < 2; seat++) {
                Hero hero = tide_get_Hero(&world, hero_entity(seat));
                hero.pos = tide_f2(seat == 0 ? -1.0f : outside ? 9.0f : 1.0f, 0);
                hero.health = 100;
                hero.invulnerable = 0;
                TIDE_REQUIRE(tide_set_Hero(&world, hero_entity(seat), hero));
            }
            Enemy attacker = tide_get_Enemy(&world, target);
            attacker.kind = kind == 0 ? EnemyKind_Cantor : EnemyKind_Warden;
            attacker.pos = tide_f2(0, 20);
            attacker.attackAt = tide_f2(0, 0);
            attacker.target = outside ? 1 : 0;
            attacker.attackCooldown = 10;
            attacker.windup = DT * 0.5f;
            TIDE_REQUIRE(tide_set_Enemy(&world, target, attacker));
            ticks(2);
            TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).health < 100);
            const float partner_health = tide_get_Hero(&world, hero_entity(1)).health;
            TIDE_CHECK(outside ? partner_health == 100 : partner_health < 100);
        }
    }
    tide_world_free(&world);
}

TIDE_TEST(deadbell_overlapping_lures_follow_the_nearer_player)
{
    begin();
    world.Expedition.spawnClock = 1;
    ticks(2);
    const tide_entity target = find_enemy(false);
    TIDE_REQUIRE(tide_has_Enemy(&world, target));
    Enemy enemy = tide_get_Enemy(&world, target);
    enemy.kind = EnemyKind_Husk;
    enemy.serial = 0;
    enemy.pos = tide_f2(14, 18);
    enemy.windup = 0;
    enemy.attackCooldown = 10;
    TIDE_REQUIRE(tide_set_Enemy(&world, target, enemy));
    place(0, tide_f2(-15, 0));
    place(1, tide_f2(15, 0));
    input(0, (Controls){.aim = {-15, 100}, .lure = true});
    input(1, (Controls){.aim = {15, 100}, .lure = true});
    ticks(1);
    enemy = tide_get_Enemy(&world, target);
    TIDE_CHECK(enemy.pos.x > 14 && enemy.heading.x > 0);
    tide_world_free(&world);
}

TIDE_TEST(deadbell_dash_rescue_progress_and_return_protection)
{
    begin();
    place(0, tide_f2(0, 0));
    input(0, (Controls){.move = {1, 0}, .dash = true});
    ticks(1);
    Hero hero = tide_get_Hero(&world, hero_entity(0));
    TIDE_CHECK(hero.pos.x > 0.3f && hero.invulnerable > 0 && hero.stamina < 100);

    place(0, tide_f2(0, 0));
    hero = tide_get_Hero(&world, hero_entity(1));
    hero.pos = tide_f2(2, 0);
    hero.health = 0;
    hero.down = true;
    hero.bleed = 35;
    TIDE_REQUIRE(tide_set_Hero(&world, hero_entity(1), hero));
    input(0, (Controls){.interact = true});
    input(1, (Controls){.move = {0, 1}, .fire = true, .heavy = true});
    ticks(30);
    hero = tide_get_Hero(&world, hero_entity(1));
    TIDE_CHECK(hero.down && hero.pos.y > 0.4f && hero.fireFlash == 0 && hero.heavyFlash == 0);
    input(1, (Controls){0});
    ticks(60);
    hero = tide_get_Hero(&world, hero_entity(1));
    TIDE_CHECK(hero.down && hero.revive > 0.8f);
    quiet();
    ticks(30);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).revive > 0.5f);
    input(0, (Controls){.interact = true});
    ticks(140);
    hero = tide_get_Hero(&world, hero_entity(1));
    TIDE_CHECK(!hero.down && hero.health > 0 && hero.invulnerable > 2);

    const float recovered = hero.health;
    world.Expedition.spawnClock = 1;
    ticks(2);
    const tide_entity target = find_enemy(false);
    TIDE_REQUIRE(tide_has_Enemy(&world, target));
    Enemy enemy = tide_get_Enemy(&world, target);
    enemy.pos = hero.pos;
    enemy.attackAt = hero.pos;
    enemy.target = 1;
    enemy.windup = DT * 0.5f;
    TIDE_REQUIRE(tide_set_Enemy(&world, target, enemy));
    ticks(2);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).health == recovered);

    for (int seat = 0; seat < 2; seat++) {
        hero = tide_get_Hero(&world, hero_entity(seat));
        hero.down = true;
        hero.health = 0;
        hero.bleed = 30;
        TIDE_REQUIRE(tide_set_Hero(&world, hero_entity(seat), hero));
    }
    ticks(1);
    TIDE_CHECK(world.Expedition.phase == RunPhase_Defeat);
    tide_world_free(&world);
}

TIDE_TEST(deadbell_cache_is_a_team_choice_and_each_draft_awards_once)
{
    begin();
    place(0, world.Expedition.cache);
    place(1, world.Expedition.cache);
    input(0, (Controls){.interact = true});
    ticks(130);
    TIDE_CHECK(!world.Expedition.cacheTaken);
    input(1, (Controls){.interact = true});
    ticks(125);
    TIDE_CHECK(world.Expedition.cacheTaken && world.Expedition.riskCount == 1 && world.Expedition.risk);
    const int coins = world.Expedition.coins;
    ticks(30);
    TIDE_CHECK(world.Expedition.riskCount == 1 && world.Expedition.coins == coins);

    quiet();
    world.Expedition.phase = RunPhase_Sanctuary;
    world.Expedition.zone = 1;
    world.Expedition.seal = 2;
    world.Expedition.phaseTime = 0;
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 0, hero_entity(1), 1));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(1)).level == 0);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 1, hero_entity(1), 1));
    ticks(1);
    Hero hero = tide_get_Hero(&world, hero_entity(1));
    TIDE_CHECK(hero.ready && hero.level == 1 && hero.relics.at[0] == 2);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 1, hero_entity(1), 2));
    ticks(1);
    hero = tide_get_Hero(&world, hero_entity(1));
    TIDE_CHECK(hero.level == 1 && hero.relics.at[0] == 2);
    tide_world_free(&world);
}

TIDE_TEST(deadbell_split_seal_accepts_either_partner_but_requires_both_bells)
{
    begin();
    protect();
    quiet();
    world.Expedition.phase = RunPhase_Hold;
    world.Expedition.seal = 2;
    world.Expedition.anchor = tide_f2(0, 0);
    world.Expedition.goal = tide_f2(-12, 0);
    world.Expedition.goalB = tide_f2(12, 0);
    world.Expedition.objective = 0;

    place(0, world.Expedition.goal);
    place(1, world.Expedition.goal);
    ticks(10);
    TIDE_CHECK(world.Expedition.objective == 0);

    place(0, world.Expedition.goalB);
    place(1, world.Expedition.goal);
    ticks(10);
    TIDE_CHECK(world.Expedition.objective > 0.1f);

    const float captured = world.Expedition.objective;
    place(0, world.Expedition.goalB);
    place(1, world.Expedition.goalB);
    ticks(10);
    TIDE_CHECK(world.Expedition.objective == captured);

    place(0, world.Expedition.goal);
    place(1, world.Expedition.goalB);
    ticks(10);
    TIDE_CHECK(world.Expedition.objective > captured);
    tide_world_free(&world);
}

static void kill_keeper(void)
{
    const tide_entity target = find_enemy(true);
    TIDE_CHECK(tide_has_Enemy(&world, target));
    if (!tide_has_Enemy(&world, target)) return;
    Enemy keeper = tide_get_Enemy(&world, target);
    keeper.health = 1;
    keeper.pos = tide_f2(0, 8);
    keeper.attackCooldown = 100;
    TIDE_CHECK(tide_set_Enemy(&world, target, keeper));
    place(0, tide_f2(0, 0));
    Hero hero = tide_get_Hero(&world, hero_entity(0));
    hero.fireCooldown = 0;
    TIDE_CHECK(tide_set_Hero(&world, hero_entity(0), hero));
    input(0, (Controls){.aim = {0, 100}, .fire = true});
    ticks(2);
    quiet();
    TIDE_CHECK(!tide_has_Enemy(&world, target));
}

TIDE_TEST(deadbell_all_nine_seals_require_play_and_the_boss_requires_a_kill)
{
    begin();
    protect();
    for (int seal = 0; seal < 9; seal++) {
        TIDE_REQUIRE(world.Expedition.phase == RunPhase_March);
        TIDE_CHECK(world.Expedition.zone * 3 + world.Expedition.seal == seal);
        place(0, world.Expedition.goal);
        place(1, world.Expedition.goalB);
        world.Expedition.phaseTime = 16;
        quiet();
        ticks(3);
        TIDE_CHECK(world.Expedition.phase == RunPhase_March);
        input(0, (Controls){.interact = true});
        input(1, (Controls){.interact = true});
        ticks(62);
        TIDE_REQUIRE(world.Expedition.phase == RunPhase_Hold);
        quiet();
        if (world.Expedition.seal == 2) {
            world.Expedition.objective = world.Expedition.objectiveNeed;
            place(0, world.Expedition.goal);
            place(1, world.Expedition.goalB);
            ticks(1);
            TIDE_CHECK(world.Expedition.phase == RunPhase_Hold);
            kill_keeper();
            TIDE_CHECK(world.Expedition.keeperDead);
        }
        place(0, world.Expedition.goal);
        place(1, world.Expedition.goalB);
        world.Expedition.objective = world.Expedition.objectiveNeed - DT * 0.5f;
        ticks(2);
        TIDE_REQUIRE(world.Expedition.phase == RunPhase_Sanctuary);
        TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 0, hero_entity(0), 0));
        TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 1, hero_entity(1), 1));
        ticks(1);
        TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).level == seal + 1);
        world.Expedition.phaseTime = 11;
        ticks(1);
    }
    TIDE_REQUIRE(world.Expedition.phase == RunPhase_Boss);
    world.Expedition.phaseTime = 1000;
    ticks(2);
    TIDE_CHECK(world.Expedition.phase == RunPhase_Boss);
    TIDE_CHECK(world.Expedition.bossHealth > 0);
    kill_keeper();
    TIDE_CHECK(world.Expedition.phase == RunPhase_Victory);
    const int previous_seed = world.Expedition.seed;
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_RestartRun, 1, (tide_entity){0}, 0));
    ticks(2);
    TIDE_CHECK(world.Expedition.phase == RunPhase_Waiting);
    TIDE_CHECK(world.Expedition.zone == 0 && world.Expedition.seal == 0 && world.Expedition.kills == 0);
    TIDE_CHECK(world.Expedition.seed != previous_seed);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).level == 0 && !tide_get_Hero(&world, hero_entity(0)).down);
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_EquipWeapon, 0, hero_entity(0), 2));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).weapon == 2);
    tide_world_free(&world);
}

static void seeded_run(const uint32_t seed)
{
    tide_world_free(&world);
    tide_world_start_seeded(&world, DT, NULL, seed);
    ticks(2);
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    ticks(2);
    TIDE_CHECK(command(TIDE_COMMAND_ID_StartRun, 0, (tide_entity){0}, 0));
    ticks(2);
    world.Expedition.spawnClock = 1;
    ticks(2);
}

TIDE_TEST(deadbell_seed_repeats_a_run_and_changes_the_next_crowd)
{
    seeded_run(123);
    const uint64_t first = tide_world_hash(&world);
    const Enemy previous = tide_get_Enemy(&world, find_enemy(false));
    world.Expedition.phase = RunPhase_Sanctuary;
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 0, hero_entity(0), 0));
    ticks(1);
    const int relic = tide_get_Hero(&world, hero_entity(0)).upgrade;
    seeded_run(123);
    TIDE_CHECK(tide_world_hash(&world) == first);
    world.Expedition.phase = RunPhase_Sanctuary;
    TIDE_REQUIRE(command(TIDE_COMMAND_ID_ChooseRelic, 0, hero_entity(0), 0));
    ticks(1);
    TIDE_CHECK(tide_get_Hero(&world, hero_entity(0)).upgrade == relic);
    seeded_run(321);
    const Enemy changed = tide_get_Enemy(&world, find_enemy(false));
    TIDE_CHECK(tide_world_hash(&world) != first);
    TIDE_CHECK(changed.pos.x != previous.pos.x || changed.pos.y != previous.pos.y);
    tide_world_free(&world);
}

static void replay_inputs(const int tick)
{
    input(0, (Controls){.move = {0.3f, 0.2f}, .aim = {100, 100}, .fire = tick % 13 < 3, .heavy = tick % 17 == 0});
    input(1, (Controls){.move = {-0.2f, 0.3f}, .aim = {100, -100}, .fire = tick % 9 < 2, .lure = tick == 5});
}

TIDE_TEST(deadbell_eight_thousand_real_enemies_survive_snapshot_and_replay)
{
    begin();
    protect();
    world.Expedition.phase = RunPhase_Boss;
    world.Expedition.zone = 2;
    quiet();
    ticks(1200);
    TIDE_REQUIRE(enemies() == 8000);
    TIDE_CHECK(world.Expedition.live == 8000 && world.Expedition.peak == 8000);
    const tide_entity target = find_enemy(false);
    const Enemy enemy = tide_get_Enemy(&world, target);
    place(0, tide_f2(enemy.pos.x - 2, enemy.pos.y - 8));
    place(1, tide_f2(enemy.pos.x + 2, enemy.pos.y - 8));
    tide_world kept = {0};
    tide_world_copy(&kept, &world);
    for (int i = 0; i < 30; i++) { replay_inputs(i); ticks(1); }
    const uint64_t hash = tide_world_hash(&world);
    TIDE_CHECK(hash == CROWD_REPLAY_HASH);
    tide_world_copy(&world, &kept);
    for (int i = 0; i < 30; i++) { replay_inputs(i); ticks(1); }
    TIDE_CHECK(tide_world_hash(&world) == hash);
    TIDE_CHECK(world.Expedition.kills > 0);

    const uint32_t capacity = 16u * 1024u * 1024u;
    uint8_t *bytes = malloc(capacity);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, capacity);
    tide_world unpacked = {0};
    TIDE_CHECK(size > 0 && tide_world_unpack(&unpacked, bytes, size));
    TIDE_CHECK(tide_world_hash(&unpacked) == hash);
    printf("    8000 simulated enemies; combat/rollback world hash 0x%016llx; packed %u bytes\n",
           (unsigned long long)hash, (unsigned)size);
    free(bytes);
    tide_world_free(&unpacked);
    tide_world_free(&kept);
    tide_world_free(&world);
}
