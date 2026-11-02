// Diagnostic host: production fixture code records one frame; there is no
// platform/window/audio/device setup, rendering submission, or pixel readback.
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "game.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static double elapsed_ms(const struct timespec begin, const struct timespec end)
{
    return (double)(end.tv_sec - begin.tv_sec) * 1000.0
        + (double)(end.tv_nsec - begin.tv_nsec) / 1000000.0;
}

int main(const int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "512") && strcmp(argv[1], "2048"))) {
        fputs("usage: cpu-room-record 512|2048\n", stderr);
        return 2;
    }
    const int capacity = !strcmp(argv[1], "512") ? 512 : 2048;
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    world.GIFixture.scenario = 2;
    world.GIFixture.roomExtents = tide_f3(24, 12, 32);
    tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    tide_local_init(&local);
    local.GIProbe.settings.maxProbes = capacity;
    local.GIProbe.settings.captureDistance = 128;
    local.GIProbe.settings.updatesPerFrame = 0;
    local.GIProbe.cameraPosition = tide_f3(0, 0, -2);
    gui.width = 128;
    gui.height = 64;
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(128, 64));
    printf("record-only frame starting: scenario=2 extents=24,12,32 capacity=%d updates=0; no GPU\n", capacity);
    fflush(stdout);
    struct timespec begin = {0}, end = {0};
    if (timespec_get(&begin, TIME_UTC) != TIME_UTC) return 1;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    if (timespec_get(&end, TIME_UTC) != TIME_UTC) return 1;
    const GlobalIllumination_WorldBounds *bounds = &local.GlobalIllumination_WorldBounds;
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const bool valid = tide_world_hash(&world) == hash && bounds->geometryCount == 7
        && environment->ready && environment->layout.total == capacity && environment->updates == 0
        && environment->activeProbes > 0 && environment->activeProbes <= capacity
        && environment->activeBlocks > 0 && environment->activeBlocks <= 128;
    printf("{\"diagnostic\":\"GI CPU record only\",\"scenario\":2,\"room_extents\":[24,12,32],"
        "\"viewport\":[128,64],\"capacity\":%d,\"scene_instances\":%d,\"recorded_frames\":1,"
        "\"updates\":%d,\"nodes\":%d,\"probes\":%d,\"blocks\":%d,\"base_spacing\":%.9g,"
        "\"finest_spacing\":%.9g,\"capture_distance\":%.9g,\"view_distance\":%.9g,"
        "\"frame_record_wall_ms\":%.6f,\"draw_commands\":%" PRIu32 ",\"world_hash\":\"%016" PRIx64 "\","
        "\"gpu_submissions\":0,\"correctness\":%s}\n",
        capacity, bounds->geometryCount, environment->updates, environment->layout.nodeCount,
        environment->activeProbes, environment->activeBlocks, (double)environment->layout.spacing,
        (double)environment->finestSpacing, (double)environment->settings.captureDistance,
        (double)environment->settings.viewDistance, elapsed_ms(begin, end), draw.count, hash, valid ? "true" : "false");
    fflush(stdout);
    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return valid ? 0 : 1;
}
