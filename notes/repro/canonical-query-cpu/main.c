#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

double bench_wall(void);
double bench_cpu(void);
uint64_t bench_cycles(void);

int32_t CheckWhole(Wide value)
{
    return value.index + (int32_t)value.payload.at[0];
}

int main(int argc, char **argv)
{
    const int ticks = argc > 1 ? atoi(argv[1]) : 500;
    if (ticks < 1 || ticks > 10000) return 2;
    static tide_world world;
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 20; i++) tide_world_tick(&world);
    const uint64_t initial_hash = tide_world_hash(&world);
    uint64_t sequence = UINT64_C(14695981039346656037);
    const double start_wall = bench_wall(), start_cpu = bench_cpu();
    const uint64_t start_cycles = bench_cycles();
    for (int i = 0; i < ticks; i++) {
        tide_world_tick(&world);
        sequence = (sequence ^ tide_world_hash(&world)) * UINT64_C(1099511628211);
    }
    const uint64_t cycles = bench_cycles() - start_cycles;
    const double cpu = bench_cpu() - start_cpu, wall = bench_wall() - start_wall;
    printf("{\"ticks\":%d,\"warmup\":20,\"initial_hash\":\"%016" PRIx64 "\",\"final_hash\":\"%016" PRIx64 "\",\"sequence_hash\":\"%016" PRIx64 "\",\"sum\":%.9g,\"wall_seconds\":%.9g,\"thread_cpu_seconds\":%.9g,\"thread_cycles\":%" PRIu64 "}\n",
           ticks, initial_hash, tide_world_hash(&world), sequence, (double)world.Probe.sum, wall, cpu, cycles);
    tide_world_free(&world);
    return 0;
}
