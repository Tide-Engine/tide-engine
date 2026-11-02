// CPU-only placement evidence from the unchanged production game's generated
// code. Draw commands are recorded to drive GI scheduling/relocation, but no
// platform window/device is opened and no GPU work or readback is submitted.
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"

#define EXPORT_SOURCE "b1c69748f47a800269fae11fbf7d4f3370df7c8c"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static const void *read_element(const tide_list list, const int32_t index, const uint32_t size)
{
    const void *value = tide_list_read(&local.heap, list, index, size);
    if (!value) { fputs("Missing placement snapshot list element\n", stderr); exit(1); }
    return value;
}

static bool boolean(const tide_list list, const int32_t index)
{
    return *(const bool *)read_element(list, index, sizeof(bool));
}

static int32_t integer(const tide_list list, const int32_t index)
{
    return *(const int32_t *)read_element(list, index, sizeof(int32_t));
}

static tide_float3 vector(const tide_list list, const int32_t index)
{
    return *(const tide_float3 *)read_element(list, index, sizeof(tide_float3));
}

static void write_vector(FILE *file, const tide_float3 value)
{
    if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z)) {
        fputs("Nonfinite placement evidence\n", stderr); exit(1);
    }
    fprintf(file, "[%.9g,%.9g,%.9g]", (double)value.x, (double)value.y, (double)value.z);
}

static bool record_frame(const uint64_t hash)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(128, 96));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    if (tide_world_hash(&world) != hash) { fputs("Placement recording changed match state\n", stderr); return false; }
    return true;
}

static bool snapshot(const char *path, const bool district, const int buildings,
                     const bool overview, const int recorded, const uint64_t hash)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const GlobalIllumination_WorldBounds *bounds = &local.GlobalIllumination_WorldBounds;
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    FILE *file = fopen(path, "wb");
    if (!file) { perror(path); return false; }
    fprintf(file, "{\n\"source_sha\":\"%s\",\"method\":\"production tide_frame CPU recording; no GPU submission\",\n"
        "\"capture_flag_semantics\":\"captured means capture commands recorded; GPU irradiance/visibility not executed or read\",\n"
        "\"receiver_bound_semantics\":\"transformed authored asset AABBs, not exact surface silhouettes\",\n"
        "\"viewport\":[128,96],\"quick_shadow_resolution\":32,\n"
        "\"scene\":\"%s\",\"buildings\":%d,\"overview\":%s,\"recorded_frames\":%d,\"world_hash\":\"%016" PRIx64 "\",\n"
        "\"scene_instances\":%d,\"active_probes\":%d,\"active_blocks\":%d,\"initialized\":%d,\"probe_capacity\":%d,\n"
        "\"requested_spacing\":%.9g,\"base_spacing\":%.9g,\"finest_spacing\":%.9g,\"view_distance\":%.9g,\"capture_distance\":%.9g,\n"
        "\"camera_eye\":", EXPORT_SOURCE, district ? "district" : "room", district ? buildings : 1,
        overview ? "true" : "false", recorded, hash, bounds->geometryCount, environment->activeProbes,
        environment->activeBlocks, environment->initialized, environment->layout.total,
        (double)environment->settings.probeSpacing, (double)environment->layout.spacing,
        (double)environment->finestSpacing, (double)environment->settings.viewDistance,
        (double)environment->settings.captureDistance);
    write_vector(file, renderer->eye);
    fputs(",\"camera_forward\":", file); write_vector(file, tide_forward_q(renderer->camera.rotation));
    fputs(",\"coverage_root_minimum\":", file); write_vector(file, environment->layout.minimum);
    fprintf(file, ",\"coverage_root_size\":%.9g,\n\"probes\":[\n", (double)environment->layout.size);
    int probes = 0;
    for (int32_t index = 0; index < environment->layout.total; index++) {
        if (!boolean(environment->active, index)) continue;
        const tide_float3 anchor = vector(environment->anchors, index);
        const tide_float3 offset = vector(environment->offsets, index);
        if (probes++) fputs(",\n", file);
        fprintf(file, "{\"id\":%d,\"anchor\":", (int)index); write_vector(file, anchor);
        fputs(",\"offset\":", file); write_vector(file, offset);
        fputs(",\"position\":", file); write_vector(file, tide_add_f3(anchor, offset));
        fprintf(file, ",\"captured\":%s,\"dirty\":%s}", boolean(environment->captured, index) ? "true" : "false",
            boolean(environment->dirty, index) ? "true" : "false");
    }
    fputs("\n],\"blocks\":[\n", file);
    int blocks = 0;
    const int32_t block_count = tide_list_read_count(&local.heap, environment->blocks);
    for (int32_t index = 0; index < block_count; index++) {
        const GlobalIllumination_ProbeBlock *block = read_element(environment->blocks, index, sizeof *block);
        if (!block->active) continue;
        if (blocks++) fputs(",\n", file);
        int face_mask = 0;
        for (int32_t node = 0; node < environment->layout.nodeCount; node++) {
            const int code = integer(environment->lookup, node);
            if ((code & 65535) == index + 1) { face_mask = code >> 16 & 63; break; }
        }
        const int diagonal_mask = integer(environment->lookup, environment->layout.nodeCount + index * 28 + 27);
        fprintf(file, "{\"slot\":%d,\"minimum\":", (int)index); write_vector(file, block->minimum);
        fprintf(file, ",\"spacing\":%.9g,\"size\":%.9g,\"probe_mask\":%d,\"face_transition_mask\":%d,"
            "\"edge_corner_transition_mask\":%d,\"probe_ids\":[", (double)block->spacing, (double)block->spacing * 2,
            (int)block->probeMask, face_mask, diagonal_mask);
        const int32_t logical_count = tide_list_read_count(&local.heap, block->probes);
        for (int32_t logical = 0; logical < logical_count; logical++) {
            fprintf(file, "%s%d", logical ? "," : "", (int)integer(block->probes, logical));
        }
        fputs("]}", file);
    }
    fputs("\n],\"receiver_bounds\":[\n", file);
    const int32_t instances = tide_list_read_count(&local.heap, bounds->instances);
    for (int32_t index = 0; index < instances; index++) {
        const GlobalIllumination_SceneInstance *instance = read_element(bounds->instances, index, sizeof *instance);
        fprintf(file, "%s{\"index\":%d,\"entity\":[%" PRIu32 ",%" PRIu32 "],\"minimum\":",
            index ? ",\n" : "", (int)index, instance->entity.index, instance->entity.generation);
        write_vector(file, instance->bounds.minimum);
        fputs(",\"maximum\":", file); write_vector(file, instance->bounds.maximum);
        fprintf(file, ",\"skinned\":%s,\"deformed\":%s}", instance->skinned ? "true" : "false",
            instance->material.deformed ? "true" : "false");
    }
    fputs("\n]}\n", file);
    const bool valid = !ferror(file) && probes == environment->activeProbes && blocks == environment->activeBlocks;
    const bool closed = fclose(file) == 0;
    return valid && closed;
}

int main(const int argc, char **argv)
{
    if (argc < 2) { fputs("probe exporter output.json [--scene room|district] [--buildings N] [--overview]\n", stderr); return 2; }
    bool district = false, overview = false;
    int buildings = 64, maximum_frames = 160;
    for (int argument = 2; argument < argc; argument++) {
        if (!strcmp(argv[argument], "--overview")) { overview = true; continue; }
        if (!strcmp(argv[argument], "--quick")) continue;
        if (argument + 1 >= argc) return 2;
        const char *value = argv[++argument];
        if (!strcmp(argv[argument - 1], "--scene")) {
            if (strcmp(value, "room") && strcmp(value, "district")) return 2;
            district = !strcmp(value, "district");
        } else if (!strcmp(argv[argument - 1], "--buildings")) {
            char *end;
            errno = 0;
            const long parsed = strtol(value, &end, 10);
            if (errno || !*value || *end || parsed < 1 || parsed > 256) return 2;
            buildings = (int)parsed;
        } else if (!strcmp(argv[argument - 1], "--frames")) {
            char *end;
            errno = 0;
            const long parsed = strtol(value, &end, 10);
            if (errno || !*value || *end || parsed < 2 || parsed > 160) return 2;
            maximum_frames = (int)parsed;
        } else if (strcmp(argv[argument - 1], "--mode") || strcmp(value, "update4")) return 2;
    }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Workload.district = district;
    world.Workload.buildings = buildings;
    tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    tide_local_init(&local);
    local.Controls.enabled = true;
    local.Controls.quick = true;
    local.Controls.overview = overview;
    local.Controls.updates = 16;
    gui.width = 128; gui.height = 96;
    int recorded = 0;
    bool ready = false;
    for (; recorded < maximum_frames - 1; recorded++) {
        if (!record_frame(hash)) return 1;
        const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
        if (environment->ready && environment->activeProbes > 0 && environment->initialized == environment->activeProbes) {
            recorded++; ready = true; break;
        }
    }
    if (!ready || local.GlobalIllumination_WorldBounds.geometryCount != (district ? buildings : 1) * 7) {
        fputs("Placement export did not converge to the requested scene\n", stderr); return 1;
    }
    local.Controls.updates = 4;
    if (!record_frame(hash)) return 1;
    recorded++;
    const bool saved = snapshot(argv[1], district, buildings, overview, recorded, hash);
    printf("CPU placement export: %s, %d meshes, %d probes, %d blocks, viewDistance %.9g; GPU not submitted\n",
        argv[1], local.GlobalIllumination_WorldBounds.geometryCount, local.GlobalIllumination_Environment.activeProbes,
        local.GlobalIllumination_Environment.activeBlocks, (double)local.GlobalIllumination_Environment.settings.viewDistance);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_render_targets_free();
    return saved ? 0 : 1;
}
