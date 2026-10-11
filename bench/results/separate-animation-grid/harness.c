#include <stdio.h>
#define tide_game_pack tide_game_pack_function
#include "game.c"
#undef tide_game_pack
#include "tide/time.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_gui gui = {0};
    tide_draw_list draw = {0};
    double tick_ns = 0, hash_ns = 0, draw_ns = 0;
    uint64_t hash = 0;
    for (int i = 0; i < 30; ++i) {
        uint64_t a = tide_time_now_ns();
        tide_world_tick(&world);
        uint64_t b = tide_time_now_ns();
        hash = tide_world_hash(&world);
        uint64_t c = tide_time_now_ns();
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
        tide_draw_prepare(&draw, NULL);
        uint64_t d = tide_time_now_ns();
        if (i >= 10) { tick_ns += (double)(b-a); hash_ns += (double)(c-b); draw_ns += (double)(d-c); }
    }
    printf("characters=%u pack=%zu world=%u tick_ms=%.6f hash_ms=%.6f draw_ms=%.6f draw_commands=%u hash=%llu\n", world.arch1_Zombie.count, tide_game_pack_size, tide_world_pack(&world, NULL, 0), tick_ns / 20000000, hash_ns / 20000000, draw_ns / 20000000, draw.count, (unsigned long long)hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}
