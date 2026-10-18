// How long a GRAVEWAKE tick takes with a horde: two Gunners stand by the fire
// while the dead rise one a tick until there are `count` of them (150 unless
// the first argument says), then the ticks are timed while they crowd in and
// the Gunners fire into them, with a gun drone, an ember ring and Live Wire
// each, along with a copy of the world (what a rollback's snapshot costs) and
// its packed size. The dead don't die. Not a test: run it from a release build
// to measure.
//   gravewake_perf [count] [ticks]
#include "gravewake_perf.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static tide_world world;
static Hands held[2];

static double now(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void tick(void)
{
    for (int seat = 0; seat < 2; seat++) {
        tide_entity e = world.Party.survivors.at[seat];
        Survivor s = tide_get_Survivor(&world, e);
        if (s.life == Life_Standing) {
            s.health = 1e9f;      // Not to be downed by hundreds of claws in a tick
            s.choosing = false;   // ...nor the run held still by a level's offer
            s.pending = 0;
            tide_set_Survivor(&world, e, s);
        }
        tide_world_set_input(&world, tide_player_from_index(seat), held[seat]);
        if (seat == 0) tide_world_set_server_input(&world, held[seat]);
    }
    tide_world_tick(&world);
}

int main(int argc, char **argv)
{
    const int count = argc > 1 ? atoi(argv[1]) : 150;
    const int ticks = argc > 2 ? atoi(argv[2]) : 600;
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    held[0].ready = held[1].ready = true;
    tick();
    held[0].pick1 = held[1].pick1 = true;      // A survivor each, or nothing starts
    tick();
    held[0].pick1 = held[1].pick1 = false;
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick();
    // The dead that don't die, coming one a tick till there are `count` of them, then no more (and no boss)
    world.Match.bossNext = 1e9f;
    world.Match.special = ZombieKind_Walker;      // Walkers only: a Bloater bursting through them would thin the horde
    world.Match.specials = 1 << 30;
    world.Match.scattered = true;                 // One a tick, not in hordes
    for (int i = 0; i < count * 4 && (int)world.arch6_Zombie.count < count; i++) {
        world.Match.spawnWait = 1.0f / 60;
        tick();
    }
    world.Match.spawnWait = 1e9f;
    for (uint32_t r = 0; r < world.arch6_Zombie.count; r++) {
        const tide_entity e = TIDE_ENTITY_AT(&world, arch6_Zombie, r);
        Zombie z = tide_get_Zombie(&world, e);
        z.health = 1e12f;
        tide_set_Zombie(&world, e, z);
    }
    for (int seat = 0; seat < 2; seat++) {
        const tide_entity e = world.Party.survivors.at[seat];
        Survivor s = tide_get_Survivor(&world, e);
        s.items.at[Item_GunDrone] = 2;
        s.items.at[Item_EmberRing] = 1;
        s.items.at[Item_LiveWire] = 1;
        s.pitch = -0.1f;
        tide_set_Survivor(&world, e, s);
        held[seat].attack = true;
    }
    int states[8] = {0};
    for (uint32_t r = 0; r < world.arch6_Zombie.count; r++) states[TIDE_AT(&world, arch6_Zombie, Zombie, r).state & 7]++;
    printf("spawned: rising %d hunting %d dying %d fallen %d; phase %d\n", states[0], states[1], states[2], states[3], (int)world.Match.phase);
    for (int i = 0; i < 240; i++) tick();
    const double start = now();
    for (int i = 0; i < ticks; i++) tick();
    const double per = (now() - start) / ticks;
    tide_world copy = {0};
    const double copied = now();
    for (int i = 0; i < 20; i++) tide_world_copy(&copy, &world);
    const double perCopy = (now() - copied) / 20;
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    printf("%d of the dead: %.3f ms a tick (%.1f%% of 60 Hz), a world copy %.3f ms, packed %u bytes\n", (int)world.arch6_Zombie.count,
           per * 1000, per * 60 * 100, perCopy * 1000, size);
    tide_world_free(&copy);
    tide_world_free(&world);
    return 0;
}
