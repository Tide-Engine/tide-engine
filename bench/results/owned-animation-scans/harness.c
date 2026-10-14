#include <stdio.h>
#define tide_game_pack tide_game_pack_function
#include "game.c"
#undef tide_game_pack
#include "tide/time.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
int main(int argc, char **argv)
{
    int frames = argc == 3 && strcmp(argv[1], "--frames") == 0 ? atoi(argv[2]) : 330; if (frames <= 30) return 2; if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_gui gui = {0};
    tide_draw_list draw = {0};
    double tick_ns = 0, hash_ns = 0, draw_ns = 0;
    uint64_t hash = 0, pixels = TIDE_HASH_START; uint32_t states = 0;
    for (int i = 0; i < frames; ++i) {
        uint64_t a = tide_time_now_ns();
        tide_world_tick(&world);
        uint64_t b = tide_time_now_ns();
        hash = tide_world_hash(&world);
        uint64_t c = tide_time_now_ns();
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
        tide_draw_prepare(&draw, NULL);
        uint64_t d = tide_time_now_ns();
        Zombie zombie;
        tide_table_gather(&world.arch0_Zombie, &tide_columns_arch0_Zombie, 0u, 2u, 7u, 4u, &zombie);
        states |= 1u << (uint32_t)zombie.state;
        for (uint32_t t = 0; t < draw.texture_count; t++) {
            const tide_draw_texture *texture = draw.textures + t;
            if (texture->format != TIDE_TEXELS_INTS) continue;
            pixels = tide_hash_more(pixels, texture->pixels,
                (size_t)texture->width * (size_t)texture->height * tide_texels_bytes(texture->format));
        }
        if (i >= 30) { tick_ns += (double)(b-a); hash_ns += (double)(c-b); draw_ns += (double)(d-c); }
    }
    printf("characters=%u pack=%zu world=%u tick_ms=%.6f hash_ms=%.6f draw_ms=%.6f draw_commands=%u hash=%llu\n", world.arch0_Zombie.count, tide_game_pack_size, tide_world_pack(&world, NULL, 0), tick_ns / ((double)(frames - 30) * 1000000.0), hash_ns / ((double)(frames - 30) * 1000000.0), draw_ns / ((double)(frames - 30) * 1000000.0), draw.count, (unsigned long long)hash);
    printf("frames=%d states=%u palette_hash=%llu\n", frames, states, (unsigned long long)tide_hash_end(pixels)); tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}
