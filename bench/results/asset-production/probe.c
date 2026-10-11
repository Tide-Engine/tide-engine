#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "../../../engine/src/asset_store.h"
#include "tide/pack.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

// Optional wire prefixes write this build's packets and decode the other
// build's packets. Full packets must also decode after unloading the pack.
static bool wire_check(const char *prefix, const char *other, const char *kind,
    const uint8_t *bytes, const uint32_t size, const uint64_t hash)
{
    char path[512];
    if (snprintf(path, sizeof path, "%s-%s.bin", prefix, kind) >= (int)sizeof path) return false;
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    const bool wrote = fwrite(bytes, 1, size, file) == size;
    fclose(file);
    if (!wrote || !other) return wrote;
    if (snprintf(path, sizeof path, "%s-%s.bin", other, kind) >= (int)sizeof path) return false;
    file = fopen(path, "rb");
    if (!file) return false;
    uint8_t *foreign = malloc((size_t)size + 1u);
    const size_t count = fread(foreign, 1, (size_t)size + 1u, file);
    fclose(file);
    tide_world back = {0};
    bool ok = count == size && !memcmp(bytes, foreign, size)
        && tide_game_api.unpack_delta(&back, NULL, foreign, size) && tide_world_hash(&back) == hash;
    tide_world_free(&back);
    if (!strcmp(kind, "full")) {
        tide_pack_unload();
        ok = ok && tide_game_api.unpack_delta(&back, NULL, foreign, size) && tide_world_hash(&back) == hash;
        tide_world_free(&back);
    }
    free(foreign);
    return ok;
}

int main(int argc, char **argv)
{
    uint32_t frames = 0;
    if (argc >= 3 && argc <= 5 && !strcmp(argv[1], "--frames")) frames = (uint32_t)strtoul(argv[2], NULL, 10);
    if (!frames || frames > 1200u) return 2;
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 3;
    tide_world world = {0}, back = {0};
    tide_local local = {0}; tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 120.0f);
    for (uint32_t i = 0; i < frames; i++) tide_world_tick(&world);
    tide_local_init(&local);
    tide_devices devices = {0};
    tide_gui_begin(&gui, &devices, (tide_float2){960, 540}, NULL);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_draw_prepare(&draw, NULL);
    uint64_t palettes = 0;
    uint32_t instances = 0, assets = 0, leases = 0, payload = 0;
    for (uint32_t i = 0; i < draw.run_count; i++) instances += draw.runs[i].count;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *texture = &draw.textures[i];
        if (texture->space == TIDE_PIXELS_LIST && texture->format == TIDE_TEXELS_INTS)
            palettes = tide_hash_more(palettes, texture->pixels, (size_t)texture->width * (uint32_t)texture->height * 16u);
    }
    for (uint32_t i = 0; world.heap.assets && i < world.heap.assets->slots; i++) {
        const tide_asset_entry entry = world.heap.assets->entries[i];
        if (!entry.data) continue;
        assets++; leases += entry.users; payload += entry.data->count * entry.data->element;
    }
    uint32_t known_size, full_size;
    uint8_t *known = tide_game_api.pack_delta(&world, NULL, NULL, 0, true, &known_size);
    uint8_t *full = tide_game_api.pack_delta(&world, NULL, NULL, 0, false, &full_size);
    const uint64_t hash = tide_world_hash(&world);
    if (!tide_game_api.unpack_delta(&back, NULL, known, known_size) || tide_world_hash(&back) != hash) return 4;
    printf("{\"frames\":%u,\"instances\":%u,\"assets\":%u,\"leases\":%u,\"payload_bytes\":%u,\"heap_bytes\":%u,\"packed_bytes\":%u,\"known_pack_delta_bytes\":%u,\"self_contained_delta_bytes\":%u,\"world_hash\":\"%016llx\",\"palette_hash\":\"%016llx\",\"known_wire_hash\":\"%016llx\",\"full_wire_hash\":\"%016llx\"}\n",
        frames, instances, assets, leases, payload, world.heap.used, tide_world_pack(&world, NULL, 0), known_size, full_size,
        (unsigned long long)hash, (unsigned long long)palettes,
        (unsigned long long)tide_hash(known, known_size), (unsigned long long)tide_hash(full, full_size));
    if (argc >= 4 && (!wire_check(argv[3], argc == 5 ? argv[4] : NULL, "known", known, known_size, hash)
        || !wire_check(argv[3], argc == 5 ? argv[4] : NULL, "full", full, full_size, hash))) return 5;
    free(known); free(full); tide_world_free(&back); tide_world_free(&world);
    tide_local_free(&local); tide_draw_free(&draw); tide_pack_unload(); return 0;
}
