#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define tide_game_pack tide_world_pack_callback
#include "game.c"
#undef tide_game_pack
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void metric(const char *name, double *samples, const int count)
{
    double mean = 0;
    for (int i = 0; i < count; i++) mean += samples[i];
    qsort(samples, (size_t)count, sizeof *samples, compare_double);
    const double median = (count & 1) ? samples[count / 2] : (samples[count / 2 - 1] + samples[count / 2]) * 0.5;
    printf(",\"%s\":{\"mean_ms\":%.9f,\"median_ms\":%.9f,\"p95_ms\":%.9f}", name, mean / count, median, samples[(count * 95 + 99) / 100 - 1]);
}
int main(int argc, char **argv)
{
    int frames = 330;
    if (argc != 1) {
        if (argc != 3 || strcmp(argv[1], "--frames") != 0) return 2;
        char *end = NULL;
        const long value = strtol(argv[2], &end, 10);
        if (!end || *end || value < 31 || value > 10000) return 2;
        frames = (int)value;
    }
    double *samples = calloc((size_t)frames * 5u, sizeof *samples);
    if (!samples || !tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_gui gui = {0};
    tide_draw_list draw = {0};
    uint64_t state_hash = 0, palette_hash = TIDE_HASH_START;
    uint32_t states = 0, integer_textures = 0;
    for (int frame = 0; frame < frames; frame++) {
        const uint64_t a = tide_time_now_ns();
        tide_world_tick(&world);
        const uint64_t b = tide_time_now_ns();
        state_hash = tide_world_hash(&world);
        const uint64_t c = tide_time_now_ns();
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, tide_f2(1024, 576));
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const uint64_t d = tide_time_now_ns();
        tide_draw_prepare(&draw, NULL);
        const uint64_t e = tide_time_now_ns();
        if (frame >= 30) {
            const int at = frame - 30;
            samples[at] = (double)(b - a) / 1e6;
            samples[frames + at] = (double)(c - b) / 1e6;
            samples[frames * 2 + at] = (double)(d - c) / 1e6;
            samples[frames * 3 + at] = (double)(e - d) / 1e6;
            samples[frames * 4 + at] = (double)(e - a) / 1e6;
        }
        const Zombie first = TIDE_AT(&world, arch0_Zombie, Zombie, 0);
        states |= 1u << (uint32_t)first.state;
        integer_textures = 0;
        for (uint32_t t = 0; t < draw.texture_count; t++) {
            const tide_draw_texture *texture = draw.textures + t;
            if (texture->frame != draw.frame || texture->format != TIDE_TEXELS_INTS) continue;
            integer_textures++;
            palette_hash = tide_hash_more(palette_hash, texture->pixels,
                (size_t)texture->width * (size_t)texture->height * tide_texels_bytes(texture->format));
        }
        if (world.arch0_Zombie.count != 2500u || draw.count != 1u || integer_textures != 2u) return 3;
    }
    printf("{\"characters\":%u,\"frames\":%d,\"warmup\":30,\"state_mask\":%u,\"draw_commands\":%u,\"integer_textures\":%u,\"pack_bytes\":%zu,\"world_bytes\":%u,\"world_hash\":\"%016" PRIx64 "\",\"palette_hash\":\"%016" PRIx64 "\"",
        world.arch0_Zombie.count, frames, states, draw.count, integer_textures, tide_game_pack_size,
        tide_world_pack(&world, NULL, 0), state_hash, palette_hash);
    const char *names[] = {"tick", "hash", "view", "prepare", "cpu_total"};
    for (int i = 0; i < 5; i++) metric(names[i], samples + frames * i, frames - 30);
    puts("}");
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
    free(samples);
    return 0;
}
