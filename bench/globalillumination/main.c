// Identical requested scene/camera for GI off, frozen GI and bounded live updates.
// Query reset, convergence, shader warmup and pixel checks are outside timing.
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/platform.h"
#include "tide/time.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { MODES = 5, METRICS = 5, DRAIN = 16 };
enum { RECORD, SUBMIT, COMPLETE, TOTAL, GPU };
enum { REFERENCE, VALIDATE_GI, CONVERGE, WARMUP, MEASURE, REPORT };
enum { ROOM, DISTRICT, ATRIUM, COURTYARD, SCENES };
static const char *const scene_names[SCENES] = {"room", "district", "atrium", "courtyard"};
static const char *const mode_names[MODES] = {"off", "frozen", "update1", "update4", "update16"};
static const int budgets[MODES] = {0, 0, 1, 4, 16};
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint64_t world_hash;
static int modes[MODES] = {0, 1, 2, 3, 4}, mode_count = MODES, mode_at;
static int width = 960, height = 720, frames = 100, warmup = 20;
static int phase, warm_at, measured, drain, gpu_count, convergence_frames, validation_frames;
static bool quick, debug, overview, travel, moving_light;
static int scene, buildings = 64, probe_capacity = 512, motion_frame, idle_capture_frames;
static const char *output_path, *capture_path;
static FILE *output;
static double *samples[METRICS];
static uint32_t dark_reference, lit_reference;
static uint64_t calls_sum, vertices_sum, uploads_sum;
static uint64_t updates_sum, candidates_sum, capture_draws_sum, frozen_atlas;
static int frozen_initialized;
static uint32_t max_textures;
static double setup_record_ms, convergence_record_ms, untimed_record_ms;
static float primary_spacing;
enum { REGION_COLUMNS = 5, REGION_ROWS = 3, REGION_SAMPLES = REGION_COLUMNS * REGION_ROWS };
static tide_float3 region_world[REGION_SAMPLES];
static tide_float2 region_pixels[REGION_SAMPLES];
static uint32_t region_off[REGION_SAMPLES], region_on[REGION_SAMPLES];
static float region_spacing[REGION_SAMPLES];
static bool region_on_valid;
static int region_count, region_nonzero, region_covered, region_gain_min, region_gain_max, region_gain_sum;

static int expected_meshes(void)
{
    // Independent inventory of authored instances; not a counter supplied by
    // scene construction. Each Pillar is a shaft, a base and a capital.
    if (scene == ATRIUM) return 216 + 3 + 18 * 3
        + 9 /* arches */ + 9 /* roof beams */ + 10 /* galleries and ramps */
        + 2 /* accents */ + 5 /* roof ribs */ + 6 /* sculptures */ + 1 /* witness */;
    if (scene == COURTYARD) return 300 + 5 + 24 * 3 + 22 + 4 + 24 + 4 * 3 + 3 + 2 + 2 + 10 + 12 + 3 + 1 + 4;
    return 7 * (scene == DISTRICT ? buildings : 1);
}

static bool number(const char *text, const int minimum, const int maximum, int *value)
{
    char *end;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno || !*text || *end || parsed < minimum || parsed > maximum) return false;
    *value = (int)parsed;
    return true;
}

static bool record(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(width, height));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    return true;
}

static bool validate(void)
{
    if (tide_world_hash(&world) != world_hash) {
        fputs("GI benchmark changed match state\n", stderr);
        return false;
    }
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_shader *shader = draw.runs[i].shader;
        if (!shader) continue;
        if (shader->texture_count > 12) return false;
        if (shader->texture_count > max_textures) max_textures = shader->texture_count;
    }
    return true;
}

static bool completed(void)
{
#ifndef __wasm__
    int actual_width, actual_height;
    if (!tide_platform_frame_pixels(&actual_width, &actual_height)
        || actual_width != width || actual_height != height) return false;
#endif
    return true;
}

static bool untimed(void)
{
    const uint64_t began = quick ? 0 : tide_time_now_ns();
    if (!record()) return false;
    const uint64_t recorded = quick ? 0 : tide_time_now_ns();
    untimed_record_ms = (double)(recorded - began) / 1e6;
    if (!validate()) return false;
    tide_platform_draw(&draw);
    return completed();
}

static bool converged(void)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    return environment->ready && environment->activeProbes > 0
        && environment->initialized == environment->activeProbes;
}

static bool checked_budget(const int mode)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    if (environment->updates < 0 || environment->updates > budgets[mode]) {
        fputs("GI exceeded its capture budget\n", stderr);
        return false;
    }
    const int expected_count = expected_meshes();
    if (mode && local.GlobalIllumination_WorldBounds.geometryCount != expected_count) {
        fprintf(stderr, "GI scene has %d mesh instances, expected %d\n",
            local.GlobalIllumination_WorldBounds.geometryCount, expected_count);
        return false;
    }
    if (mode && (!environment->ready || environment->activeProbes <= 0
        || environment->layout.total != probe_capacity
        || environment->activeProbes > environment->layout.total
        || environment->initialized < 0 || environment->initialized > environment->activeProbes
        || environment->dirtyProbes < 0 || environment->dirtyProbes > environment->activeProbes)) {
        fputs("GI resident field accounting is invalid\n", stderr);
        return false;
    }
    if (mode == 1 && (environment->updates != 0 || environment->resources.atlas.id != frozen_atlas
        || (!travel && environment->initialized != frozen_initialized))) {
        fputs("Frozen GI changed capture history\n", stderr);
        return false;
    }
    if (mode && budgets[mode] && !travel && !moving_light && environment->updates == 0)
        idle_capture_frames++;
    return true;
}

static void advance_scene(void)
{
    local.Controls.seconds = travel || moving_light ? (float)motion_frame++ / 60.0f : 0;
}

static tide_float3 receiver_position(void)
{
    if (scene == ATRIUM) return (tide_float3){0, 2.5f, 20.41f};
    if (scene == COURTYARD) return (tide_float3){10, 2.5f, 12.41f};
    return (tide_float3){1.3f, 0, -0.6f};
}

// Read the actual adaptive allocation after timing at this scene's receiver,
// independent of the current overview/travel camera.
static void primary_probe_spacing(void)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    primary_spacing = 0;
    const tide_float3 position = receiver_position();
    const int32_t count = tide_list_read_count(&local.heap, environment->blocks);
    for (int32_t index = 0; index < count; index++) {
        const GlobalIllumination_ProbeBlock *block = tide_list_read(&local.heap,
            environment->blocks, index, sizeof *block);
        if (!block || !block->active) continue;
        const float size = block->spacing * 2;
        if (position.x < block->minimum.x || position.y < block->minimum.y || position.z < block->minimum.z
            || position.x > block->minimum.x + size || position.y > block->minimum.y + size
            || position.z > block->minimum.z + size) continue;
        if (primary_spacing == 0 || block->spacing < primary_spacing) primary_spacing = block->spacing;
    }
}

static bool project_receiver(const tide_float3 p, tide_float2 *point)
{
    // Independent double pinhole projection through the actual rendered camera.
    const Render3D_Camera camera = local.Render3D_Renderer.camera;
    const tide_float3 eye = camera.position;
    const tide_float3 forward = tide_forward_q(camera.rotation);
    const tide_float3 right = tide_right_q(camera.rotation);
    const tide_float3 up = tide_up_q(camera.rotation);
    const double x = p.x - eye.x, y = p.y - eye.y, z = p.z - eye.z;
    const double depth = x * forward.x + y * forward.y + z * forward.z;
    const double tangent = tan((double)camera.verticalFov * 0.5);
    if (!isfinite(depth) || depth <= 0 || !isfinite(tangent) || tangent <= 0) {
        fprintf(stderr, "GI %s receiver has invalid camera depth/FOV\n", scene_names[scene]);
        return false;
    }
    const double scale = height / (2 * depth * tangent);
    const double screen_x = floor(width * 0.5 + (x * right.x + y * right.y + z * right.z) * scale);
    const double screen_y = floor(height * 0.5 - (x * up.x + y * up.y + z * up.z) * scale);
    if (!isfinite(screen_x) || !isfinite(screen_y)
        || screen_x < 0 || screen_x >= width || screen_y < 0 || screen_y >= height) {
        fprintf(stderr, "GI %s receiver projects outside the frame: %.6g, %.6g\n",
            scene_names[scene], screen_x, screen_y);
        return false;
    }
    *point = (tide_float2){(float)screen_x, (float)screen_y};
    return true;
}

static bool receiver_pixel(uint32_t *pixel)
{
    tide_float2 point = {(float)width * 0.4f, (float)height * 0.62f};
    if (scene >= ATRIUM && !project_receiver(receiver_position(), &point)) return false;
    *pixel = 0;
    tide_platform_read_pixels(&draw, &point, 1, pixel);
    return true;
}

static unsigned brightness(const uint32_t color)
{
    return (color >> 24) + (color >> 16 & 255u) + (color >> 8 & 255u);
}

static bool regional_reference(void)
{
    region_count = scene >= ATRIUM ? REGION_SAMPLES : 0;
    for (int index = 0; index < region_count; index++) {
        const int column = index % REGION_COLUMNS, row = index / REGION_COLUMNS;
        // These are authored planar receiver patches, not selected screenshot
        // pixels. The atrium's central rear wall is above the display/sculptures,
        // inside the side galleries and visible through the open arch interiors.
        // Its spots aim outward, away from this central patch. The courtyard
        // patch is above the bridge/arcades and all spot positions. Both +Z faces
        // oppose the sun, so only indirect light should brighten these grids.
        region_world[index] = scene == ATRIUM
            ? (tide_float3){-4 + (float)column * 2, 6 + (float)row * 2, -35.69f}
            : (tide_float3){-46 + (float)column * 5, 16 + (float)row * 2, -51.99f};
        if (!project_receiver(region_world[index], &region_pixels[index])) return false;
    }
    if (region_count) tide_platform_read_pixels(&draw, region_pixels, region_count, region_off);
    for (int index = 0; index < region_count; index++) {
        region_on[index] = region_off[index];
        region_spacing[index] = 0;
        // Catch a misplaced/occluded receiver rather than interpreting a bright
        // direct-lit column or arch as successful indirect illumination.
        if (brightness(region_off[index]) > 3) {
            fprintf(stderr, "GI %s regional receiver %d is not directly shaded: %08x\n",
                scene_names[scene], index, region_off[index]);
            return false;
        }
    }
    return true;
}

static float regional_support(const tide_float3 world_point)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const tide_float3 point = {world_point.x, world_point.y, world_point.z + environment->layout.spacing * 0.04f};
    const int32_t count = tide_list_read_count(&local.heap, environment->blocks);
    for (int32_t index = 0; index < count; index++) {
        const GlobalIllumination_ProbeBlock *block = tide_list_read(&local.heap,
            environment->blocks, index, sizeof *block);
        if (!block || !block->active || !(block->spacing > 0)) continue;
        const float size = block->spacing * 2;
        if (point.x < block->minimum.x || point.y < block->minimum.y || point.z < block->minimum.z
            || point.x > block->minimum.x + size || point.y > block->minimum.y + size
            || point.z > block->minimum.z + size) continue;
        const int x = (int)fmax(0, fmin(1, floor((point.x - block->minimum.x) / block->spacing)));
        const int y = (int)fmax(0, fmin(1, floor((point.y - block->minimum.y) / block->spacing)));
        const int z = (int)fmax(0, fmin(1, floor((point.z - block->minimum.z) / block->spacing)));
        bool complete = true;
        for (int corner = 0; corner < 8; corner++) {
            const int logical = x + (corner & 1) + 3 * (y + ((corner >> 1) & 1)) + 9 * (z + (corner >> 2));
            const int32_t *probe = tide_list_read(&local.heap, block->probes, logical, sizeof *probe);
            if (!probe || *probe < 0 || *probe >= environment->layout.total) { complete = false; break; }
            const bool *active = tide_list_read(&local.heap, environment->active, *probe, sizeof *active);
            const bool *captured = tide_list_read(&local.heap, environment->captured, *probe, sizeof *captured);
            if (!active || !*active || !captured || !*captured) { complete = false; break; }
        }
        if (complete) return block->spacing;
    }
    return 0;
}

static void regional_report(FILE *file)
{
    fprintf(file, ",\"regional_camera\":\"hero_validation\",\"regional_count\":%d,\"regional_on_valid\":%s,\"regional_covered\":%d,"
        "\"regional_nonzero\":%d,\"regional_gain_min\":%d,\"regional_gain_mean\":%.6f,"
        "\"regional_gain_max\":%d,\"regional_samples\":[",
        region_count, region_on_valid ? "true" : "false", region_covered, region_nonzero,
        region_gain_min, region_count ? (double)region_gain_sum / region_count : 0, region_gain_max);
    for (int index = 0; index < region_count; index++) {
        const tide_float3 p = region_world[index];
        const tide_float2 pixel = region_pixels[index];
        fprintf(file, "%s{\"world\":[%.6g,%.6g,%.6g],\"pixel\":[%.0f,%.0f],"
            "\"off\":%u,\"on\":%u,\"spacing\":%.6g}", index ? "," : "",
            (double)p.x, (double)p.y, (double)p.z, (double)pixel.x, (double)pixel.y,
            region_off[index], region_on[index], (double)region_spacing[index]);
    }
    fputc(']', file);
}

static bool regional_check(void)
{
    if (!region_count) return true;
    tide_platform_read_pixels(&draw, region_pixels, region_count, region_on);
    region_on_valid = true;
    region_gain_min = 765;
    region_gain_max = -765;
    for (int index = 0; index < region_count; index++) {
        region_spacing[index] = regional_support(region_world[index]);
        region_covered += region_spacing[index] > 0;
        const int gain = (int)brightness(region_on[index]) - (int)brightness(region_off[index]);
        region_gain_sum += gain;
        if (gain < region_gain_min) region_gain_min = gain;
        if (gain > region_gain_max) region_gain_max = gain;
        // One summed level is the readback/quantization margin, not useful fill.
        region_nonzero += gain > 1;
    }
    // A conservative sunlit floor patch lies in front of each receiver grid:
    // courtyard x[-40,-24], z[-30,-20] is past the rear facade's ~14.4m shadow;
    // atrium x[-7,-2] U [0,7], z[-20.5,-18.5] avoids roof/arch shadow strips.
    // Lambert transfer is positive throughout these patches. Only a very small
    // aggregate increase (>3 summed RGB levels per point) is required here;
    // this is a coverage/nonzero-energy oracle, not an exact bounce solution.
    if (region_covered == region_count && region_nonzero == region_count && region_gain_sum > region_count * 3)
        return true;
    fprintf(stderr, "GI %s regional fill failed: %d/%d mapped, %d/%d nonzero, mean gain %.6f\n",
        scene_names[scene], region_covered, region_count, region_nonzero, region_count,
        (double)region_gain_sum / region_count);
    fprintf(stderr, "{\"diagnostic\":\"gi_regional_fill\",\"scene\":\"%s\",\"probe_capacity\":%d",
        scene_names[scene], probe_capacity);
    regional_report(stderr);
    fputs("}\n", stderr);
    return false;
}

static void collect_gpu(void)
{
    for (int i = 0; i < 8; i++) {
        const double value = tide_platform_gpu_ms();
        if (!(value >= 0)) break;
        if (gpu_count < frames && isfinite(value)) samples[GPU][gpu_count++] = value;
    }
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left, b = *(const double *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static double percentile(double *values, const int count, const double fraction)
{
    if (!count) return 0;
    qsort(values, (size_t)count, sizeof *values, compare_double);
    return values[(int)((count - 1) * fraction + 0.5)];
}

static void report_to(FILE *file)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    fprintf(file, "{\"benchmark\":\"globalillumination\",\"scene\":\"%s\",\"buildings\":%d,"
        "\"debug\":%s,\"overview\":%s,\"travel\":%s,\"moving_light\":%s,"
        "\"mode\":\"%s\",\"width\":%d,\"height\":%d,"
        "\"frames\":%d,\"warmup\":%d,\"validation_frames\":%d,\"convergence_frames\":%d,"
        "\"gi_scene_instances\":%d,\"probes\":%d,\"probe_capacity\":%d,\"active_blocks\":%d,\"dirty_probes\":%d,"
        "\"finest_spacing\":%.6g,\"primary_spacing\":%.6g,\"updates_per_frame\":%d,"
        "\"capture_updates_mean\":%.3f,\"capture_faces_per_frame\":%.3f,"
        "\"capture_candidates_mean\":%.3f,\"capture_draws_mean\":%.3f,\"idle_capture_frames\":%d,"
        "\"max_shader_textures\":%u,\"gpu_samples\":%d,"
        "\"world_hash\":\"%016llx\",\"quick\":%s,\"receiver_off\":%u,\"receiver_on\":%u",
        scene_names[scene], scene == DISTRICT ? buildings : 1,
        debug ? "true" : "false", overview ? "true" : "false", travel ? "true" : "false", moving_light ? "true" : "false",
        mode_names[modes[mode_at]], width, height, frames, warmup, validation_frames, convergence_frames,
        local.GlobalIllumination_WorldBounds.geometryCount, environment->activeProbes, environment->ready ? environment->layout.total : 0,
        environment->activeBlocks, environment->dirtyProbes,
        (double)environment->finestSpacing, (double)primary_spacing, budgets[modes[mode_at]],
        (double)updates_sum / frames, (double)updates_sum * 6 / frames,
        (double)candidates_sum / frames, (double)capture_draws_sum / frames, idle_capture_frames,
        max_textures, gpu_count, (unsigned long long)world_hash, quick ? "true" : "false", dark_reference, lit_reference);
    if (scene >= ATRIUM) regional_report(file);
    if (!quick) {
        fprintf(file, ",\"setup_record_ms\":%.6f,\"convergence_record_ms\":%.6f,"
            "\"cpu_record_ms\":%.6f,\"cpu_record_p95_ms\":%.6f,\"cpu_submit_ms\":%.6f,"
            "\"draw_calls_mean\":%.3f,\"submitted_vertices_mean\":%.3f,\"texture_upload_bytes_mean\":%.3f,"
            "\"gpu_memory_bytes\":%llu,\"gpu_ms\":",
            setup_record_ms, convergence_record_ms,
            percentile(samples[RECORD], frames, 0.5), percentile(samples[RECORD], frames, 0.95),
            percentile(samples[SUBMIT], frames, 0.5), (double)calls_sum / frames, (double)vertices_sum / frames,
            (double)uploads_sum / frames, (unsigned long long)tide_platform_gpu_memory());
        if (gpu_count) fprintf(file, "%.6f", percentile(samples[GPU], gpu_count, 0.5));
        else fputs("null", file);
        fputs(",\"gpu_p95_ms\":", file);
        if (gpu_count) fprintf(file, "%.6f", percentile(samples[GPU], gpu_count, 0.95));
        else fputs("null", file);
#ifndef __wasm__
        fprintf(file, ",\"completion_readback_ms\":%.6f,\"total_readback_ms\":%.6f",
            percentile(samples[COMPLETE], frames, 0.5), percentile(samples[TOTAL], frames, 0.5));
#endif
    }
    fputs("}\n", file);
    fflush(file);
}

static int finish(void)
{
    int result = 0;
#ifndef __wasm__
    if (capture_path && !tide_platform_capture(capture_path)) result = 1;
#endif
    tide_platform_gpu_timing(false);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_render_targets_free();
    for (int i = 0; i < METRICS; i++) free(samples[i]);
    if (output && fclose(output)) result = 1;
    if (!result) puts("GlobalIllumination benchmark correctness passed");
    return result;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    tide_platform_next_frame(0);
    const int mode = modes[mode_at];
    Controls *controls = &local.Controls;
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    if (phase == REFERENCE) {
        controls->validationView = true;
        controls->seconds = 0;
        controls->enabled = false;
        controls->updates = 0;
        motion_frame = idle_capture_frames = validation_frames = convergence_frames = 0;
        setup_record_ms = convergence_record_ms = 0;
        max_textures = 0;
        lit_reference = 0;
        region_on_valid = false;
        region_nonzero = region_covered = region_gain_min = region_gain_max = region_gain_sum = 0;
        memset(region_spacing, 0, sizeof region_spacing);
        // Quick static smoke modes share one fully validated field. Repeating
        // the same 512-probe convolution four times dominates software WebGL
        // without adding a new lighting oracle. Timed and moving/view-changing
        // runs still start each mode from an independent cold field.
        if (quick && mode_at > 1 && !overview && !travel && !moving_light
            && converged() && environment->dirtyProbes == 0) {
            controls->enabled = true;
            controls->updates = 16;
            phase = VALIDATE_GI;
            return TIDE_KEEP_RUNNING;
        }
        // Let the locally spawned point light and its shadow resources settle
        // before taking the GI-off reference, including the first mode.
        if (!untimed() || !untimed()) return 1;
        if (!receiver_pixel(&dark_reference)) return 1;
        if (!regional_reference()) return 1;
        phase = VALIDATE_GI;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == VALIDATE_GI) {
        controls->enabled = mode != 0;
        controls->updates = mode ? 16 : 0;
        if (!untimed()) return 1;
        if (!validation_frames) setup_record_ms = untimed_record_ms;
        convergence_record_ms += untimed_record_ms;
        validation_frames++;
        if (mode && !converged()) {
            if (validation_frames > 160) { fputs("GI validation field did not converge\n", stderr); return 1; }
            return TIDE_KEEP_RUNNING;
        }
        if (mode) {
            if (!receiver_pixel(&lit_reference)) return 1;
            if (brightness(lit_reference) <= brightness(dark_reference) + 15u) {
                fprintf(stderr, "GI %s receiver did not brighten: %08x -> %08x\n", scene_names[scene], dark_reference, lit_reference);
#ifndef __wasm__
                if (capture_path && !tide_platform_capture(capture_path))
                    fputs("Could not save diagnostic GI capture\n", stderr);
#endif
                return 1;
            }
            if (!regional_check()) {
#ifndef __wasm__
                if (capture_path && !tide_platform_capture(capture_path))
                    fputs("Could not save diagnostic GI capture\n", stderr);
#endif
                return 1;
            }
        }
        controls->validationView = false;
        phase = CONVERGE;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == CONVERGE) {
        if (!untimed()) return 1;
        convergence_record_ms += untimed_record_ms;
        convergence_frames++;
        if (mode && !converged()) {
            if (convergence_frames > 160) { fputs("GI field did not converge\n", stderr); return 1; }
            return TIDE_KEEP_RUNNING;
        }
        controls->updates = budgets[mode];
        frozen_atlas = environment->resources.atlas.id;
        frozen_initialized = environment->initialized;
        warm_at = 0;
        phase = WARMUP;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == WARMUP) {
        advance_scene();
        if (!untimed() || !checked_budget(mode)) return 1;
        if (++warm_at < warmup) return TIDE_KEEP_RUNNING;
        measured = gpu_count = 0;
        calls_sum = vertices_sum = uploads_sum = 0;
        updates_sum = candidates_sum = capture_draws_sum = 0;
        if (!quick) { tide_platform_gpu_timing(false); tide_platform_gpu_timing(true); }
        phase = MEASURE;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == MEASURE) {
        advance_scene();
        const uint64_t began = quick ? 0 : tide_time_now_ns();
        if (!record()) return 1;
        const uint64_t recorded = quick ? 0 : tide_time_now_ns();
        const uint64_t uploaded = tide_platform_gpu_written();
        tide_platform_draw(&draw);
        const uint64_t submitted = quick ? 0 : tide_time_now_ns();
        if (!completed()) return 1;
        const uint64_t complete = quick ? 0 : tide_time_now_ns();
        if (!validate() || !checked_budget(mode)) return 1;
        updates_sum += (uint64_t)environment->updates;
        candidates_sum += (uint64_t)environment->captureCandidates;
        capture_draws_sum += (uint64_t)environment->captureDraws;
        if (!quick) {
            samples[RECORD][measured] = (double)(recorded - began) / 1e6;
            samples[SUBMIT][measured] = (double)(submitted - recorded) / 1e6;
            samples[COMPLETE][measured] = (double)(complete - submitted) / 1e6;
            samples[TOTAL][measured] = (double)(complete - began) / 1e6;
            calls_sum += tide_platform_draw_calls();
            vertices_sum += tide_platform_draw_vertices();
            uploads_sum += tide_platform_gpu_written() - uploaded;
            collect_gpu();
        }
        if (++measured == frames) { phase = REPORT; drain = quick ? 1 : DRAIN; }
        return TIDE_KEEP_RUNNING;
    }
    if (!quick) collect_gpu();
    if (--drain) return TIDE_KEEP_RUNNING;
    if (mode && budgets[mode] && !travel && !moving_light && !idle_capture_frames) {
        fputs("Static GI kept recapturing after convergence\n", stderr);
        return 1;
    }
    primary_probe_spacing();
    if (mode && !overview && !travel && primary_spacing <= 0) {
        fputs("GI omitted the primary floor receiver\n", stderr);
        return 1;
    }
    report_to(stdout);
    if (output) report_to(output);
    tide_platform_gpu_timing(false);
    if (++mode_at == mode_count) return finish();
    phase = REFERENCE;
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *equal = strchr(arg, '=');
        char option[64];
        if (equal) {
            const size_t length = (size_t)(equal - arg);
            if (length >= sizeof option) return 2;
            memcpy(option, arg, length);
            option[length] = '\0';
            arg = option;
        }
        if (!equal && !strcmp(arg, "--quick")) { quick = true; width = 128; height = 96; frames = 2; warmup = 1; continue; }
        if (!equal && !strcmp(arg, "--debug")) { debug = true; continue; }
        if (!equal && !strcmp(arg, "--overview")) { overview = true; continue; }
        if (!equal && !strcmp(arg, "--travel")) { travel = true; continue; }
        if (!equal && !strcmp(arg, "--moving-light")) { moving_light = true; continue; }
        if (!equal && !strcmp(arg, "--help")) {
            puts("bench_globalillumination [--mode off|frozen|update1|update4|update16] [--frames 100] [--warmup 20]\n"
                "  [--scene room|district|atrium|courtyard] [--buildings 64] [--probes 512]\n"
                "  [--debug] [--overview] [--travel] [--moving-light]\n"
                "  [--output file.jsonl] [--capture final.png] [--quick]\n"
                "District has1..256 separated rooms; travel follows them at12units/second.\n"
                "Atrium48x72x18 and courtyard160x120 are static architecture; probes125..2048.\n"
                "Values also accept --key=value (browser query arguments).\n"
                "Same scene/camera at960x720; quick uses128x96 and disables timing.\n"
                "GPU and CPU overlap; native completion/readback is not pipelined FPS.");
            return 0;
        }
        const char *value = equal ? equal + 1 : i + 1 < argc ? argv[++i] : NULL;
        bool ok = value && *value;
        if (!strcmp(arg, "--frames")) ok = ok && number(value, 1, 10000, &frames);
        else if (!strcmp(arg, "--warmup")) ok = ok && number(value, 1, 1000, &warmup);
        else if (!strcmp(arg, "--buildings")) ok = ok && number(value, 1, 256, &buildings);
        else if (!strcmp(arg, "--probes")) ok = ok && number(value, 125, 2048, &probe_capacity);
        else if (!strcmp(arg, "--scene")) {
            ok = false;
            if (value) for (int choice = 0; choice < SCENES; choice++) if (!strcmp(value, scene_names[choice])) {
                scene = choice; ok = true;
            }
        }
        else if (!strcmp(arg, "--output")) output_path = value;
        else if (!strcmp(arg, "--capture")) capture_path = value;
        else if (!strcmp(arg, "--mode")) {
            ok = false;
            if (value) for (int mode = 0; mode < MODES; mode++) if (!strcmp(value, mode_names[mode])) {
                modes[0] = mode; mode_count = 1; ok = true;
            }
        } else ok = false;
        if (!ok) { fprintf(stderr, "Invalid GI benchmark argument: %s\n", arg); return 2; }
    }
    if (travel && (scene != DISTRICT || overview)) {
        fputs("--travel requires --scene district and cannot be combined with --overview\n", stderr);
        return 2;
    }
    if (scene >= ATRIUM && moving_light) {
        fputs("--moving-light is supported by room and district only; atrium/courtyard lights are static\n", stderr);
        return 2;
    }
#ifdef __wasm__
    if (capture_path || output_path) return 2;
#endif
    for (int i = 0; i < METRICS; i++) {
        samples[i] = calloc((size_t)frames, sizeof *samples[i]);
        if (!samples[i]) return 1;
    }
    if (output_path) { output = fopen(output_path, "ab"); if (!output) return 1; }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Workload.sceneKind = scene;
    world.Workload.buildings = buildings;
    tide_world_tick(&world);
    world_hash = tide_world_hash(&world);
    tide_local_init(&local);
    local.Controls.quick = quick;
    local.Controls.probes = probe_capacity;
    local.Controls.debug = debug;
    local.Controls.overview = overview;
    local.Controls.travel = travel;
    local.Controls.movingLight = moving_light;
    gui.width = width;
    gui.height = height;
    draw.target_pixels = tide_platform_target_pixels;
#ifdef __wasm__
    tide_platform_open(&(tide_window_desc){.title = "GI benchmark", .width = width, .height = height, .hidden = true});
#else
    const uint32_t limit = (uint32_t)((frames + warmup + 360) * mode_count + 1);
    tide_platform_open_headless(&(tide_headless_desc){.width = width, .height = height, .scale = 1, .frames = limit});
#endif
    printf("renderer: %s; %dx%d; scene=%s; buildings=%d; %s\n", tide_platform_renderer(), width, height,
        scene_names[scene], scene == DISTRICT ? buildings : 1,
        quick ? "correctness only" : "same-scene work budget comparison; CPU/GPU overlap; readback is not FPS");
    tide_platform_background_pipelines(false);
    tide_platform_run(frame, NULL);
}
