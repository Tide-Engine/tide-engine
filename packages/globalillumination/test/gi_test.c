#include <limits.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
extern int32_t GIRecordMark(tide_draw_list *list);
extern int32_t GIReplay(tide_draw_list *list, int32_t first, int32_t end);
extern int32_t GIRecordMarkTo(tide_draw_list *list, tide_render_target target);
extern int32_t GIReplayTo(tide_draw_list *list, tide_render_target target, int32_t first, int32_t end);

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(const int scenario)
{
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    world.GIFixture.scenario = scenario;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 128;
    gui.height = 64;
}

static void frame(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(gui.width, gui.height));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == hash);
}

static void finish(void)
{
    local.GIProbe.disabled = true;
    frame();
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static int shaded_into(const uint64_t id)
{
    uint64_t destination = 0;
    int count = 0;
    for (uint32_t at = 0; at < draw.count; at++) {
        const tide_draw_command *command = &draw.commands[at];
        if (command->kind == TIDE_DRAW_TARGET)
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        if (command->kind == TIDE_DRAW_SHADED && destination == id) count++;
    }
    return count;
}

static const GlobalIllumination_ProbeBlock *block_at(const int slot)
{
    return tide_list_at(local.GlobalIllumination_Environment.blocks, slot, sizeof(GlobalIllumination_ProbeBlock));
}

static int first_logical_probe(const GlobalIllumination_ProbeBlock *block)
{
    if (!block) return -1;
    for (int logical = 0; logical < tide_list_count(block->probes); logical++) {
        const int32_t *physical = tide_list_at(block->probes, logical, sizeof(int32_t));
        if (physical && *physical >= 0) return logical;
    }
    return -1;
}

static tide_float3 probe_position(const int index)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const tide_float3 *anchor = tide_list_at(environment->anchors, index, sizeof(tide_float3));
    const tide_float3 *offset = tide_list_at(environment->offsets, index, sizeof(tide_float3));
    TIDE_CHECK(anchor && offset);
    if (!anchor || !offset) return tide_f3(0, 0, 0);
    return tide_f3(anchor->x + offset->x, anchor->y + offset->y, anchor->z + offset->z);
}

static bool flag_at(const tide_list flags, const int index)
{
    const bool *value = tide_list_at(flags, index, sizeof(bool));
    TIDE_CHECK(value);
    return value && *value;
}

static void covers(const tide_float3 lo, const tide_float3 hi)
{
    GIProbe *probe = &local.GIProbe;
    probe->coverageCount = 8;
    for (int at = 0; at < 8; at++)
        probe->coveragePositions.at[at] = tide_f3(at & 1 ? hi.x : lo.x,
            at & 2 ? hi.y : lo.y, at & 4 ? hi.z : lo.z);
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->layout.sparse && environment->activeBlocks > 0);
    TIDE_CHECK(environment->activeProbes > 0 && environment->activeProbes <= environment->layout.total);
    TIDE_CHECK(environment->activeProbes <= environment->activeBlocks * 27);
    TIDE_CHECK(environment->layout.total > 0 && environment->layout.total <= probe->settings.maxProbes);
    TIDE_CHECK(environment->layout.spacing > 0 && isfinite(environment->layout.spacing));
    for (int at = 0; at < 8; at++) {
        const int slot = probe->coverageSlots.at[at];
        TIDE_CHECK(slot >= 0);
        if (slot < 0) continue;
        const GlobalIllumination_ProbeBlock *block = block_at(slot);
        TIDE_CHECK(block && block->active && block->spacing > 0 && isfinite(block->spacing));
        if (!block) continue;
        const tide_float3 p = probe->coveragePositions.at[at];
        TIDE_CHECK(p.x >= block->minimum.x && p.x <= block->minimum.x + 2 * block->spacing);
        TIDE_CHECK(p.y >= block->minimum.y && p.y <= block->minimum.y + 2 * block->spacing);
        TIDE_CHECK(p.z >= block->minimum.z && p.z <= block->minimum.z + 2 * block->spacing);
    }
}

static void fill(void)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int budget = local.GIProbe.settings.updatesPerFrame;
    TIDE_CHECK(budget > 0);
    const int cycle = (environment->activeProbes + budget - 1) / budget;
    for (int at = 0; at < cycle; at++) frame();
    TIDE_CHECK(environment->initialized == environment->activeProbes && environment->dirtyProbes == 0);
}

TIDE_TEST(globalillumination_empty_or_disabled_world_has_no_capture_resources)
{
    start(0);
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->settings.enabled && !environment->ready);
    TIDE_CHECK(!environment->resources.atlas.id && !environment->resources.face0.id);
    TIDE_CHECK(!environment->irradiance.target.id && !environment->distance.target.id);
    finish();

    start(1);
    local.GIProbe.settings.enabled = false;
    frame();
    TIDE_CHECK(!environment->ready && !environment->resources.atlas.id && !environment->resources.face5.id);
    TIDE_CHECK(environment->initialized == 0);
    finish();
}

TIDE_TEST(globalillumination_automatic_bounds_include_receivers_and_cache_shared_assets)
{
    start(1);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const GlobalIllumination_WorldBounds *bounds = &local.GlobalIllumination_WorldBounds;
    TIDE_CHECK(environment->ready && bounds->ready && bounds->staticScans == 1);
    covers(tide_f3(1, -3, 0), tide_f3(3, 1, 6));
    frame();
    TIDE_CHECK(bounds->staticScans == 1);
    const int64_t generation = bounds->generation;
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    TIDE_CHECK(bounds->generation != generation && bounds->staticScans == 1);
    const GlobalIllumination_FieldLayout first = environment->layout;
    world.GIFixture.offset.x = 0.01f;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(bounds->staticScans == 1);
    TIDE_CHECK(environment->layout.minimum.x == first.minimum.x && environment->layout.minimum.y == first.minimum.y
        && environment->layout.minimum.z == first.minimum.z && environment->layout.spacing == first.spacing
        && environment->layout.total == first.total);
    world.GIFixture.spawnFar = true;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(bounds->staticScans == 1); // The new entity uses the same mesh asset.
    covers(tide_f3(1.01f, -3, 0), tide_f3(3.01f, 1, 6));
    covers(tide_f3(29, 1, -12), tide_f3(31, 3, -10));
    world.GIFixture.offset = tide_f3(-80, 12, -40);
    local.GIProbe.cameraPosition = tide_f3(-78, 11, -39);
    tide_world_tick(&world);
    frame();
    covers(tide_f3(-79, 9, -40), tide_f3(-77, 13, -34));
    finish();

    start(3);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    // These are the actual floor and wall extents from the visual regression.
    // Both exact receiver surfaces must be inside automatic coverage.
    covers(tide_f3(-4.3f, -0.3f, -4), tide_f3(-4, 6, 4));
    covers(tide_f3(-4, -0.3f, -4), tide_f3(4, 0, 4));
    // The room must retain the requested spatial detail within its budget;
    // excess exterior padding previously doubled spacing and left dark gaps.
    TIDE_CHECK(environment->layout.spacing == local.GIProbe.settings.probeSpacing);
    TIDE_CHECK(bounds->staticScans == 1);
    finish();

    start(4);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    // The authored ribbon spans [-1,1] in X/Y and zero Z. Its independent
    // rest bound is transformed by the entity's translation/nonuniform scale.
    covers(tide_f3(5, -6, 2), tide_f3(9, 0, 2));
    TIDE_CHECK(bounds->skinnedScans == 1 && bounds->staticScans == 0);
    frame();
    TIDE_CHECK(bounds->skinnedScans == 1);
    finish();
}

TIDE_TEST(globalillumination_update_budget_freeze_invalidation_and_disposal)
{
    start(1);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 2;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->ready && environment->initialized == 2 && environment->updates == 2);
    const uint64_t atlas = environment->resources.atlas.id;
    TIDE_CHECK(shaded_into(atlas) >= 2);
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->updates == 2 && environment->resources.atlas.id == atlas);
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->updates == 0 && shaded_into(atlas) == 0);
    local.GIProbe.settings.intensity = 2;
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->updates == 0 && environment->resources.atlas.id == atlas);
    local.GIProbe.settings.updatesPerFrame = 1;
    world.GIFixture.offset.x = 0.01f;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(environment->initialized == 5 && environment->updates == 1 && environment->resources.atlas.id == atlas);
    const uint64_t ids[] = {environment->resources.atlas.id, environment->resources.rays.id,
        environment->resources.face0.id, environment->resources.face1.id, environment->resources.face2.id,
        environment->resources.face3.id, environment->resources.face4.id, environment->resources.face5.id};
    local.GIProbe.settings.enabled = false;
    frame();
    TIDE_CHECK(!environment->ready && !environment->resources.atlas.id);
    TIDE_CHECK(!environment->irradiance.target.id && !environment->distance.target.id);
    for (unsigned at = 0; at < sizeof ids / sizeof ids[0]; at++) TIDE_CHECK(!tide_render_target_live(ids[at]));
    TIDE_CHECK(local.Render3D_Renderer.ready);
    local.GIProbe.settings.enabled = true;
    frame();
    TIDE_CHECK(environment->ready && environment->initialized == 1);
    const uint64_t renewed = environment->resources.atlas.id;
    local.GIProbe.dispose = true;
    frame();
    TIDE_CHECK(environment->ready && environment->initialized == 1 && !tide_render_target_live(renewed));
    local.GIProbe.dispose = false;
    local.GIProbe.disabled = true;
    frame();
    TIDE_CHECK(!environment->ready && !local.Render3D_Renderer.ready);
    finish();
}

TIDE_TEST(globalillumination_settings_and_probe_count_remain_bounded)
{
    start(1);
    local.GIProbe.raw.probeSpacing = NAN;
    local.GIProbe.raw.maxProbes = INT_MIN;
    local.GIProbe.raw.updatesPerFrame = INT_MAX;
    local.GIProbe.raw.intensity = INFINITY;
    local.GIProbe.raw.viewDistance = NAN;
    local.GIProbe.raw.captureDistance = INFINITY;
    local.GIProbe.raw.refreshInterval = INT_MIN;
    local.GIProbe.settings.maxProbes = 125;
    local.GIProbe.settings.probeSpacing = 0.01f;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    const GlobalIllumination_Settings bounded = local.GIProbe.bounded;
    TIDE_CHECK(isfinite(bounded.probeSpacing) && bounded.probeSpacing > 0);
    TIDE_CHECK(bounded.maxProbes >= 125 && bounded.maxProbes <= 2048);
    TIDE_CHECK(bounded.updatesPerFrame == 16);
    TIDE_CHECK(isfinite(bounded.intensity) && bounded.intensity >= 0 && bounded.intensity <= 16);
    TIDE_CHECK(isfinite(bounded.viewDistance) && bounded.viewDistance > 0);
    TIDE_CHECK(isfinite(bounded.captureDistance) && bounded.captureDistance > 0);
    TIDE_CHECK(bounded.refreshInterval >= 1 && bounded.refreshInterval <= 36000);
    TIDE_CHECK(local.GlobalIllumination_Environment.layout.total <= 125);
    local.GIProbe.raw.maxProbes = INT_MAX;
    local.GIProbe.raw.updatesPerFrame = INT_MIN;
    local.GIProbe.raw.intensity = NAN;
    frame();
    TIDE_CHECK(local.GIProbe.bounded.maxProbes == 2048);
    TIDE_CHECK(local.GIProbe.bounded.updatesPerFrame == 0 && local.GIProbe.bounded.intensity == 0);
    local.GIProbe.settings.maxProbes = 2048;
    frame();
    TIDE_CHECK(local.GlobalIllumination_Environment.resources.atlas.size.width <= 2048);
    TIDE_CHECK(local.GlobalIllumination_Environment.resources.atlas.size.height <= 2048);
    finish();
}

TIDE_TEST(globalillumination_remote_geometry_preserves_local_detail_and_captured_history)
{
    start(1);
    local.GIProbe.settings.updatesPerFrame = 16;
    local.GIProbe.coverageCount = 1;
    local.GIProbe.coveragePositions.at[0] = tide_f3(1, -1, 3);
    frame();
    fill();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int slot = local.GIProbe.coverageSlots.at[0];
    TIDE_REQUIRE(slot >= 0);
    const GlobalIllumination_ProbeBlock before = *block_at(slot);
    const int logical = first_logical_probe(&before);
    TIDE_REQUIRE(logical >= 0);
    const int32_t *physical = tide_list_at(before.probes, logical, sizeof(int32_t));
    TIDE_REQUIRE(physical && *physical >= 0);
    const int index = *physical;
    const tide_float3 *offset = tide_list_at(environment->offsets, index, sizeof(tide_float3));
    TIDE_CHECK(offset && flag_at(environment->captured, index));
    const tide_float3 before_offset = *offset;
    const int initialized = environment->initialized;
    const uint64_t atlas = environment->resources.atlas.id;
    local.GIProbe.settings.updatesPerFrame = 0;
    world.GIFixture.farPosition = tide_f3(1024, 2, -11);
    world.GIFixture.spawnFar = true;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(local.GlobalIllumination_WorldBounds.staticScans == 1);
    TIDE_CHECK(local.GIProbe.coverageSlots.at[0] == slot);
    const GlobalIllumination_ProbeBlock *after = block_at(slot);
    TIDE_CHECK(after && after->active && after->spacing == before.spacing);
    const int32_t *after_physical = tide_list_at(after->probes, logical, sizeof(int32_t));
    TIDE_CHECK(after_physical && *after_physical == index);
    TIDE_CHECK(after->minimum.x == before.minimum.x && after->minimum.y == before.minimum.y
        && after->minimum.z == before.minimum.z);
    TIDE_CHECK(environment->resources.atlas.id == atlas && environment->initialized == initialized && environment->updates == 0);
    TIDE_CHECK(flag_at(environment->captured, index));
    offset = tide_list_at(environment->offsets, index, sizeof(tide_float3));
    TIDE_CHECK(offset && offset->x == before_offset.x && offset->y == before_offset.y && offset->z == before_offset.z);
    finish();
}

TIDE_TEST(globalillumination_neighboring_bricks_share_unique_world_anchor_records)
{
    start(3);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    int references[2048] = {0};
    int total = 0;
    int unused = 0;
    for (int slot = 0; slot < tide_list_count(environment->blocks); slot++) {
        const GlobalIllumination_ProbeBlock *block = block_at(slot);
        if (!block || !block->active) continue;
        TIDE_CHECK(tide_list_count(block->probes) == 27);
        TIDE_CHECK(block->probeMask > 0 && block->probeMask <= 134217727);
        for (int corner = 0; corner < 27; corner++) {
            const int32_t *record = tide_list_at(block->probes, corner, sizeof(int32_t));
            if ((block->probeMask & (1 << corner)) == 0) {
                TIDE_CHECK(record && *record == -1);
                unused++;
                continue;
            }
            TIDE_CHECK(record && *record >= 0 && *record < environment->layout.total);
            if (!record || *record < 0 || *record >= environment->layout.total) continue;
            const int index = *record;
            TIDE_CHECK(flag_at(environment->active, index));
            const tide_float3 *anchor = tide_list_at(environment->anchors, index, sizeof(tide_float3));
            TIDE_CHECK(anchor);
            if (!anchor) continue;
            TIDE_CHECK(anchor->x == block->minimum.x + (float)(corner % 3) * block->spacing);
            TIDE_CHECK(anchor->y == block->minimum.y + (float)(corner / 3 % 3) * block->spacing);
            TIDE_CHECK(anchor->z == block->minimum.z + (float)(corner / 9) * block->spacing);
            references[index]++;
            total++;
        }
    }
    int unique = 0;
    int shared = 0;
    for (int index = 0; index < environment->layout.total; index++) {
        if (!flag_at(environment->active, index)) { TIDE_CHECK(references[index] == 0); continue; }
        unique++;
        TIDE_CHECK(references[index] > 0);
        if (references[index] > 1) shared++;
        const tide_float3 *anchor = tide_list_at(environment->anchors, index, sizeof(tide_float3));
        for (int previous = 0; previous < index; previous++) {
            if (!flag_at(environment->active, previous)) continue;
            const tide_float3 *other = tide_list_at(environment->anchors, previous, sizeof(tide_float3));
            TIDE_CHECK(anchor && other);
            if (anchor && other) TIDE_CHECK(anchor->x != other->x || anchor->y != other->y || anchor->z != other->z);
        }
    }
    TIDE_CHECK(unique == environment->activeProbes && unique <= local.GIProbe.settings.maxProbes);
    TIDE_CHECK(total + unused == environment->activeBlocks * 27);
    TIDE_CHECK(total > unique && shared > 0 && unused > 0);
    finish();
}

TIDE_TEST(globalillumination_thin_wall_keeps_receiver_support_at_requested_spacing)
{
    start(7);
    GIProbe *probe = &local.GIProbe;
    probe->settings.probeSpacing = 3;
    probe->settings.maxProbes = 125;
    probe->settings.updatesPerFrame = 0;
    probe->coverageCount = 3;
    for (int at = 0; at < 3; at++) probe->coveragePositions.at[at] = tide_f3((float)(at - 1), 0, -0.1f);
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->activeProbes > 0 && environment->activeProbes <= 125);
    for (int at = 0; at < 3; at++) {
        TIDE_CHECK(probe->coverageSlots.at[at] >= 0);
        TIDE_CHECK(probe->coverageSpacing.at[at] == 3);
    }
    // Real receiver support must retain all eight interpolation records in
    // every block touched by the biased cross-block filter, even when empty
    // outer portions of those blocks have no physical captures allocated.
    for (int witness = 0; witness < 3; witness++) {
        const tide_float3 position = tide_f3((float)(witness - 1), 0, -0.1f + 0.04f * 3);
        const float radius = 0.25f * 3;
        probe->coverageCount = 8;
        for (int corner = 0; corner < 8; corner++)
            probe->coveragePositions.at[corner] = tide_f3(position.x + (corner & 1 ? radius : -radius),
                position.y + (corner & 2 ? radius : -radius), position.z + (corner & 4 ? radius : -radius));
        frame();
        for (int corner = 0; corner < 8; corner++) {
            const int slot = probe->coverageSlots.at[corner];
            TIDE_CHECK(slot >= 0);
            const GlobalIllumination_ProbeBlock *block = block_at(slot);
            TIDE_CHECK(block && block->active && block->spacing == 3);
            if (!block) continue;
            const float gx = fminf(fmaxf((position.x - block->minimum.x) / block->spacing, 0), 2);
            const float gy = fminf(fmaxf((position.y - block->minimum.y) / block->spacing, 0), 2);
            const float gz = fminf(fmaxf((position.z - block->minimum.z) / block->spacing, 0), 2);
            const int x = (int)fminf(floorf(gx), 1);
            const int y = (int)fminf(floorf(gy), 1);
            const int z = (int)fminf(floorf(gz), 1);
            for (int sample = 0; sample < 8; sample++) {
                const int logical = x + (sample & 1) + 3 * (y + (sample >> 1 & 1)) + 9 * (z + (sample >> 2));
                const int32_t *record = tide_list_at(block->probes, logical, sizeof(int32_t));
                TIDE_CHECK((block->probeMask & (1 << logical)) != 0);
                TIDE_CHECK(record && *record >= 0 && *record < environment->layout.total);
                if (record && *record >= 0) TIDE_CHECK(flag_at(environment->active, *record));
            }
        }
    }
    finish();
}

TIDE_TEST(globalillumination_large_coordinate_camera_streaming_keeps_spacing_stable)
{
    start(10);
    GIProbe *probe = &local.GIProbe;
    probe->settings.viewDistance = 48;
    probe->settings.captureDistance = 8;
    probe->settings.updatesPerFrame = 0;
    probe->cameraPosition = tide_f3(2000000, 0, -2);
    probe->coverageCount = 2;
    probe->coveragePositions.at[0] = tide_f3(2000000, 0, -0.5f);
    probe->coveragePositions.at[1] = tide_f3(2001000, 0, -0.5f);
    const uint64_t hash = tide_world_hash(&world);
    frame();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_REQUIRE(environment->ready && probe->coverageSlots.at[0] >= 0);
    TIDE_CHECK(probe->coverageSlots.at[1] < 0 && environment->updates == 0);
    const float spacing = environment->layout.spacing;
    const tide_float3 root = environment->layout.minimum;
    const uint64_t atlas = environment->resources.atlas.id;
    const int slot = probe->coverageSlots.at[0];
    const GlobalIllumination_ProbeBlock *block = block_at(slot);
    TIDE_REQUIRE(block);
    const int logical = first_logical_probe(block);
    TIDE_REQUIRE(logical >= 0);
    const int32_t *record = tide_list_at(block->probes, logical, sizeof(int32_t));
    TIDE_REQUIRE(record && *record >= 0);
    const int physical = *record;
    TIDE_CHECK(isfinite(spacing) && spacing >= 2);
    // A small move within the same precision band retains overlapping slots
    // and physical probe IDs rather than changing spacing with every meter.
    probe->cameraPosition.x += 1;
    frame();
    TIDE_CHECK(environment->layout.spacing == spacing && environment->resources.atlas.id == atlas);
    TIDE_CHECK(probe->coverageSlots.at[0] == slot);
    block = block_at(slot);
    record = block ? tide_list_at(block->probes, logical, sizeof(int32_t)) : NULL;
    TIDE_CHECK(record && *record == physical);
    // The second cluster is beyond the old resident root. A proportional
    // precision spacing made eye/spacing constant and incorrectly suppressed
    // this camera-only coverage rebuild at large coordinates.
    probe->cameraPosition = tide_f3(2001000, 0, -2);
    frame();
    TIDE_CHECK(environment->ready && probe->coverageSlots.at[1] >= 0 && probe->coverageSlots.at[0] < 0);
    TIDE_CHECK(environment->layout.spacing == spacing && environment->layout.minimum.x != root.x);
    TIDE_CHECK(environment->resources.atlas.id == atlas && environment->updates == 0);
    TIDE_CHECK(local.GlobalIllumination_WorldBounds.staticScans == 1);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    finish();
}

static void receiver_records(const int at)
{
    const GIProbe *probe = &local.GIProbe;
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int slot = probe->coverageSlots.at[at];
    TIDE_CHECK(slot >= 0 && probe->coverageEligible.at[at]);
    if (slot < 0) return;
    const GlobalIllumination_ProbeBlock *block = block_at(slot);
    TIDE_CHECK(block && block->active && block->spacing > 0);
    if (!block || block->spacing <= 0) return;
    const tide_float3 point = probe->coveragePositions.at[at];
    const int x = (int)fminf(fmaxf(floorf((point.x - block->minimum.x) / block->spacing), 0), 1);
    const int y = (int)fminf(fmaxf(floorf((point.y - block->minimum.y) / block->spacing), 0), 1);
    const int z = (int)fminf(fmaxf(floorf((point.z - block->minimum.z) / block->spacing), 0), 1);
    for (int sample = 0; sample < 8; sample++) {
        const int logical = x + (sample & 1) + 3 * (y + (sample >> 1 & 1)) + 9 * (z + (sample >> 2));
        const int32_t *record = tide_list_at(block->probes, logical, sizeof(int32_t));
        TIDE_CHECK((block->probeMask & (1 << logical)) != 0);
        TIDE_CHECK(record && *record >= 0 && *record < environment->layout.total);
        if (record && *record >= 0 && *record < environment->layout.total)
            TIDE_CHECK(flag_at(environment->active, *record));
    }
}

TIDE_TEST(globalillumination_visible_receiver_regions_keep_support_across_probe_budgets)
{
    start(13);
    GIProbe *probe = &local.GIProbe;
    probe->settings.viewDistance = 48;
    probe->settings.updatesPerFrame = 0;
    const uint64_t hash = tide_world_hash(&world);
    const int budgets[] = {125, 512, 2048};
    float previous_near_spacing = INFINITY;
    for (int budget = 0; budget < 3; budget++) {
        probe->settings.maxProbes = budgets[budget];
        probe->coverageCount = 16;
        // This independent grid spans the whole interior of an actual wall
        // ninety units from the camera, well beyond the near-detail radius.
        for (int at = 0; at < 16; at++)
            probe->coveragePositions.at[at] = tide_f3((float)(at % 4) * 6 - 9,
                (float)(at / 4) * 4 - 6, 87.94f);
        frame();
        const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
        TIDE_CHECK(environment->ready && environment->layout.total == budgets[budget]);
        TIDE_CHECK(environment->activeProbes > 0 && environment->activeProbes <= budgets[budget]);
        TIDE_CHECK(environment->activeBlocks > 0 && environment->activeBlocks <= 128);
        for (int at = 0; at < 16; at++) receiver_records(at);
        probe->coverageCount = 2;
        probe->coveragePositions.at[0] = tide_f3(0, 0, 5.94f);
        probe->coveragePositions.at[1] = tide_f3(0, 0, -11.94f);
        frame();
        receiver_records(0);
        receiver_records(1); // Near receivers remain resident behind the eye.
        TIDE_CHECK(probe->coverageSpacing.at[0] <= previous_near_spacing);
        previous_near_spacing = probe->coverageSpacing.at[0];
        TIDE_CHECK(environment->updates == 0 && tide_world_hash(&world) == hash);
    }
    finish();
}

TIDE_TEST(globalillumination_offscreen_geometry_does_not_consume_visible_field_budget)
{
    GlobalIllumination_FieldLayout baseline = {0};
    int baseline_probes = 0, baseline_blocks = 0;
    float baseline_spacing = 0;
    for (int scenario = 13; scenario <= 14; scenario++) {
        start(scenario);
        GIProbe *probe = &local.GIProbe;
        probe->settings.maxProbes = 512;
        probe->settings.updatesPerFrame = 0;
        probe->coverageCount = 1;
        probe->coveragePositions.at[0] = tide_f3(0, 0, 87.94f);
        frame();
        receiver_records(0);
        const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
        if (scenario == 13) {
            baseline = environment->layout;
            baseline_probes = environment->activeProbes;
            baseline_blocks = environment->activeBlocks;
            baseline_spacing = probe->coverageSpacing.at[0];
        } else {
            TIDE_CHECK(environment->activeProbes == baseline_probes && environment->activeBlocks == baseline_blocks);
            TIDE_CHECK(environment->layout.size == baseline.size && environment->layout.nodeCount == baseline.nodeCount);
            TIDE_CHECK(environment->layout.minimum.x == baseline.minimum.x
                && environment->layout.minimum.y == baseline.minimum.y && environment->layout.minimum.z == baseline.minimum.z);
            TIDE_CHECK(probe->coverageSpacing.at[0] == baseline_spacing);
        }
        finish();
    }
}

TIDE_TEST(globalillumination_camera_projection_and_hysteresis_preserve_visible_support)
{
    start(13);
    GIProbe *probe = &local.GIProbe;
    probe->settings.updatesPerFrame = 0;
    probe->coverageCount = 2;
    probe->coveragePositions.at[0] = tide_f3(0, 0, 87.94f);
    probe->coveragePositions.at[1] = tide_f3(108, 0, 87.94f);
    frame();
    receiver_records(0);
    receiver_records(1); // Retained angular guard includes the incoming edge.
    GlobalIllumination_WorldBounds *bounds = &local.GlobalIllumination_WorldBounds;
    int revision = bounds->topologyRevision;
    probe->cameraRotation = tide_axisangle_q(tide_f3(0, 1, 0), TIDE_PI_F * (8.0f / 180.0f));
    probe->cameraPosition.x = 0.5f;
    frame();
    TIDE_CHECK(bounds->topologyRevision == revision);
    receiver_records(0);
    receiver_records(1);
    probe->cameraRotation = tide_axisangle_q(tide_f3(0, 1, 0), TIDE_PI_F * (12.0f / 180.0f));
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision);
    receiver_records(1);
    revision = bounds->topologyRevision;
    probe->cameraRotation = tide_identity_q();
    probe->cameraFar = 30;
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision && !probe->coverageEligible.at[0]);
    revision = bounds->topologyRevision;
    probe->cameraFar = 100;
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision);
    receiver_records(0);
    revision = bounds->topologyRevision;
    probe->cameraFov = TIDE_PI_F / 6;
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision && !probe->coverageEligible.at[1]);
    revision = bounds->topologyRevision;
    probe->cameraFov = TIDE_PI_F / 3;
    gui.width = 64;
    gui.height = 128;
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision && !probe->coverageEligible.at[1]);
    receiver_records(0);
    revision = bounds->topologyRevision;
    probe->cameraNear = 1;
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision && bounds->requestedNear == 1);
    revision = bounds->topologyRevision;
    probe->cameraRotation = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F / 4);
    frame();
    TIDE_CHECK(bounds->topologyRevision > revision); // Roll changes the frustum.
    receiver_records(0);
    finish();
}

TIDE_TEST(globalillumination_translated_open_receiver_covers_biased_filter_support)
{
    start(11);
    GIProbe *probe = &local.GIProbe;
    probe->settings.probeSpacing = 1.5f;
    probe->settings.maxProbes = 512;
    probe->settings.updatesPerFrame = 0;
    const uint64_t hash = tide_world_hash(&world);
    frame();
    const float radius = probe->settings.probeSpacing * 0.25f;
    const float biased_z = 1.089f - probe->settings.probeSpacing * 0.04f;
    // The actual tiny -Z-facing quad is .339 beyond the half-base-phase
    // brick face at Z=.75. Its biased filter crosses that face despite no
    // geometry lying in the adjoining brick. Synthetic authored atlases
    // cannot exercise the automatic geometry-coverage padding here.
    covers(tide_f3(-radius, -radius, biased_z - radius),
           tide_f3(radius, radius, biased_z + radius));
    TIDE_CHECK(probe->coverageSlots.at[0] != probe->coverageSlots.at[4]);
    for (int at = 0; at < 8; at++) TIDE_CHECK(probe->coverageSpacing.at[at] == probe->settings.probeSpacing);
    TIDE_CHECK(local.GlobalIllumination_WorldBounds.staticScans == 1);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    finish();
}

TIDE_TEST(globalillumination_regional_invalidation_prioritizes_local_work_and_static_fields_idle)
{
    start(9);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 512;
    probe->settings.viewDistance = 128;
    probe->settings.captureDistance = 6;
    probe->settings.updatesPerFrame = 16;
    frame();
    fill();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const uint64_t atlas = environment->resources.atlas.id;
    const int initialized = environment->initialized;
    frame();
    TIDE_CHECK(environment->updates == 0 && environment->captureCandidates == 0 && environment->captureDraws == 0);
    probe->settings.updatesPerFrame = 0;
    probe->invalidate = true;
    probe->invalidateRegion = (GlobalIllumination_LocalBounds){
        .minimum = {-0.5f, -0.5f, -0.5f}, .maximum = {0.5f, 0.5f, 0.5f}, .valid = true};
    frame();
    int dirty = 0;
    int distant = 0;
    for (int index = 0; index < environment->layout.total; index++) {
        if (!flag_at(environment->active, index)) continue;
        const tide_float3 position = probe_position(index);
        const float x = fmaxf(fabsf(position.x) - 0.5f, 0);
        const float y = fmaxf(fabsf(position.y) - 0.5f, 0);
        const float z = fmaxf(fabsf(position.z) - 0.5f, 0);
        const bool expected = x * x + y * y + z * z <= 36;
        TIDE_CHECK(flag_at(environment->dirty, index) == expected);
        TIDE_CHECK(flag_at(environment->captured, index));
        if (expected) dirty++;
        if (position.x > 20) { distant++; TIDE_CHECK(!expected); }
    }
    TIDE_CHECK(dirty > 4 && dirty < environment->activeProbes && distant > 0);
    TIDE_CHECK(environment->dirtyProbes == dirty && environment->updates == 0);
    TIDE_CHECK(environment->initialized == initialized && environment->resources.atlas.id == atlas);
    probe->settings.updatesPerFrame = 4;
    frame();
    TIDE_CHECK(environment->updates == 4 && environment->dirtyProbes == dirty - 4);
    // The far cluster is outside every selected probe's capture range. Count
    // actual candidates/submissions rather than inferring culling from time.
    TIDE_CHECK(environment->captureCandidates > 0 && environment->captureCandidates <= environment->updates);
    TIDE_CHECK(environment->captureDraws <= environment->updates * 6);
    TIDE_CHECK(environment->initialized == initialized && environment->resources.atlas.id == atlas);
    fill();
    frame();
    TIDE_CHECK(environment->updates == 0);
    probe->settings.refreshInterval = 1;
    frame();
    TIDE_CHECK(environment->updates == 4 && environment->initialized == initialized);
    finish();
}

TIDE_TEST(globalillumination_continuously_dirty_field_refreshes_every_probe_fairly)
{
    start(1);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 125;
    probe->settings.updatesPerFrame = 16;
    frame();
    fill();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int before = environment->frame;
    const int active = environment->activeProbes;
    probe->settings.updatesPerFrame = 4;
    const int cycle = (active + 3) / 4;
    for (int at = 0; at < cycle; at++) {
        // Repeated light/animation changes may dirty a field every frame.
        // Nearest-first selection must not keep refreshing the same few IDs.
        probe->invalidate = true;
        probe->invalidateRegion = (GlobalIllumination_LocalBounds){0};
        frame();
        TIDE_CHECK(environment->updates == 4 && environment->initialized == active);
    }
    for (int index = 0; index < environment->layout.total; index++) {
        if (!flag_at(environment->active, index)) continue;
        const int32_t *last = tide_list_at(environment->lastCapture, index, sizeof(int32_t));
        TIDE_CHECK(last && *last > before);
    }
    finish();
}

TIDE_TEST(globalillumination_numeric_light_changes_invalidate_despite_hash_collisions)
{
    start(1);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 125;
    probe->settings.updatesPerFrame = 16;
    const tide_float3 first = {1, 1, 1};
    // Distinct finite positive inputs with identical Math.Hash(float3).
    // Pin their bits so decimal conversion cannot accidentally remove the
    // collision or make this merely a probabilistic light-change test.
    const tide_float3 second = {tide_asfloat_i(0x3f80001d), 1, tide_asfloat_i(0x3d2f6a5a)};
    TIDE_CHECK(tide_hash_f3(first) == tide_hash_f3(second));
    TIDE_CHECK(first.x != second.x && first.z != second.z);
    probe->directRadiance = first;
    frame();
    fill();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int initialized = environment->initialized;
    TIDE_CHECK(tide_list_count(environment->previousLights) == 1);
    probe->settings.updatesPerFrame = 0;
    probe->directRadiance = second;
    frame();
    TIDE_CHECK(environment->updates == 0 && environment->initialized == initialized);
    TIDE_CHECK(environment->dirtyProbes == environment->activeProbes && environment->dirtyProbes > 0);
    for (int index = 0; index < environment->layout.total; index++)
        if (flag_at(environment->active, index)) TIDE_CHECK(flag_at(environment->dirty, index));
    finish();
}

static const Render3D_LightData *shadow_point_light(void)
{
    const tide_list lights = local.GlobalIllumination_Environment.previousLights;
    for (int index = 0; index < tide_list_count(lights); index++) {
        const Render3D_LightData *light = tide_list_read(&local.heap, lights, index, sizeof *light);
        if (light && light->kind == Render3D_LightKind_Point) return light;
    }
    return NULL;
}

static void settle_shadow_field(void)
{
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.viewDistance = 64;
    local.GIProbe.settings.captureDistance = 6;
    local.GIProbe.settings.updatesPerFrame = 16;
    frame();
    fill();
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const Render3D_LightData *light = shadow_point_light();
    TIDE_CHECK(environment->updates == 0 && environment->dirtyProbes == 0);
    TIDE_CHECK(environment->shadowsEnabled && light && light->castShadows && light->shadowIndex >= 0);
    TIDE_CHECK(light && light->range == 100);
    local.GIProbe.settings.updatesPerFrame = 0;
}

static void distant_shadow_probes(const bool dirty)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    int count = 0;
    for (int index = 0; index < environment->layout.total; index++) {
        if (!flag_at(environment->active, index)) continue;
        const tide_float3 eye = probe_position(index);
        if (eye.x <= 20) continue;
        // Every witness lies far outside a six-unit capture of the changed
        // caster near0, yet is illuminated by the same shadowed range100 light.
        count++;
        TIDE_CHECK(flag_at(environment->captured, index));
        TIDE_CHECK(flag_at(environment->dirty, index) == dirty);
    }
    TIDE_CHECK(count > 0 && environment->updates == 0);
}

TIDE_TEST(globalillumination_shadow_casting_motion_and_flags_invalidate_distant_receivers)
{
    start(12);
    settle_shadow_field();
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const uint64_t atlas = environment->resources.atlas.id;
    distant_shadow_probes(false);
    world.GIFixture.offset.x = 0.25f;
    tide_world_tick(&world);
    frame();
    distant_shadow_probes(true);
    TIDE_CHECK(tide_list_count(local.GlobalIllumination_WorldBounds.shadowChanges) > 0);
    TIDE_CHECK(environment->resources.atlas.id == atlas);
    local.GIProbe.settings.updatesPerFrame = 16;
    fill();
    local.GIProbe.settings.updatesPerFrame = 0;
    frame(); // Apply the frozen budget before inspecting per-frame work.
    distant_shadow_probes(false);
    const int topology = local.GlobalIllumination_WorldBounds.topologyRevision;
    world.GIFixture.casterShadows = false;
    tide_world_tick(&world);
    frame();
    distant_shadow_probes(true);
    // A caster flag is a lighting dependency; toggling it must neither leave
    // the field stale nor rebuild the unchanged geometry allocation.
    TIDE_CHECK(local.GlobalIllumination_WorldBounds.topologyRevision == topology);
    TIDE_CHECK(environment->resources.atlas.id == atlas);
    finish();
}

TIDE_TEST(globalillumination_cutout_shadow_changes_propagate_but_emission_stays_local)
{
    start(12);
    settle_shadow_field();
    world.GIFixture.casterEmission = tide_f3(1, 0.2f, 0.1f);
    world.GIFixture.casterRoughness = 0.9f;
    tide_world_tick(&world);
    frame();
    distant_shadow_probes(false);
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->dirtyProbes > 0 && environment->dirtyProbes < environment->activeProbes);
    TIDE_CHECK(tide_list_count(local.GlobalIllumination_WorldBounds.shadowChanges) == 0);
    world.GIFixture.casterCutout = true;
    tide_world_tick(&world);
    local.GIProbe.settings.updatesPerFrame = 16;
    frame();
    fill();
    local.GIProbe.settings.updatesPerFrame = 0;
    frame(); // Apply the frozen budget before inspecting per-frame work.
    distant_shadow_probes(false);
    // The white base texture multiplied by alpha.25 falls below cutoff.5,
    // removing this cutout caster from the real shadow pass.
    world.GIFixture.casterAlpha = 0.25f;
    tide_world_tick(&world);
    frame();
    distant_shadow_probes(true);
    TIDE_CHECK(tide_list_count(local.GlobalIllumination_WorldBounds.shadowChanges) > 0);
    finish();
}

TIDE_TEST(globalillumination_external_caster_edits_propagate_without_recursive_invalidation)
{
    start(12);
    settle_shadow_field();
    local.GIProbe.invalidate = true;
    local.GIProbe.invalidateRegion = (GlobalIllumination_LocalBounds){
        .minimum = {-0.5f, -0.5f, -0.5f}, .maximum = {0.5f, 0.5f, 0.5f}, .valid = true};
    frame();
    distant_shadow_probes(true);
    TIDE_CHECK(tide_list_count(local.GlobalIllumination_WorldBounds.changes) == 0);
    TIDE_CHECK(tide_list_count(local.GlobalIllumination_WorldBounds.shadowChanges) == 0);
    finish();
}

TIDE_TEST(globalillumination_shadow_enable_and_atlas_budget_changes_invalidate_lighting)
{
    for (int change = 0; change < 2; change++) {
        start(12);
        settle_shadow_field();
        const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
        const uint64_t atlas = environment->resources.atlas.id;
        const Render3D_LightData *before = shadow_point_light();
        TIDE_REQUIRE(before && before->shadowIndex >= 0);
        const tide_float3 intensity = before->intensity;
        const tide_float3 position = before->position;
        if (change == 0) local.GIProbe.shadowsEnabled = false;
        else local.GIProbe.shadowMaps = 0;
        frame();
        const Render3D_LightData *after = shadow_point_light();
        TIDE_REQUIRE(after && after->castShadows && after->shadowIndex < 0);
        TIDE_CHECK(after->intensity.x == intensity.x && after->intensity.y == intensity.y && after->intensity.z == intensity.z);
        TIDE_CHECK(after->position.x == position.x && after->position.y == position.y && after->position.z == position.z);
        TIDE_CHECK(environment->dirtyProbes == environment->activeProbes);
        TIDE_CHECK(environment->resources.atlas.id == atlas);
        distant_shadow_probes(true);
        finish();
    }
}

TIDE_TEST(globalillumination_placement_geometry_uses_closed_surfaces_and_world_distances)
{
    start(0);
    local.GIPlacementProbe.enabled = true;
    local.GIPlacementProbe.invalidPoint = tide_f3(NAN, 0, 0);
    local.GIPlacementProbe.invalidTransform.c0.x = INFINITY;
    local.GIPlacementProbe.invalidRadius = INFINITY;
    frame();
    const GIPlacementProbe *probe = &local.GIPlacementProbe;
    const GlobalIllumination_GeometryHit *hit = probe->hits.at;
    TIDE_CHECK(probe->done && probe->cubeClosed && probe->sphereClosed && !probe->planeClosed);
    TIDE_CHECK(hit[0].found && hit[0].insideKnown && hit[0].inside);
    TIDE_CHECK(fabsf(hit[0].distance - 0.5f) < 1e-6f);
    TIDE_CHECK(fabsf(tide_dot_f3(hit[0].point, hit[0].normal) - 0.5f) < 1e-6f);
    TIDE_CHECK(hit[1].found && hit[1].insideKnown && !hit[1].inside);
    TIDE_CHECK(fabsf(hit[1].distance - 0.5f) < 1e-6f && hit[1].normal.x > 0.999f);
    // Reflection/nonuniform scale must preserve the actual outward direction.
    TIDE_CHECK(hit[2].found && hit[2].insideKnown && !hit[2].inside);
    TIDE_CHECK(fabsf(hit[2].distance - 1) < 1e-6f && hit[2].point.x == -1 && hit[2].normal.x < -0.999f);
    // The sheared face is x-y=.5; its closest point and distance are analytic.
    TIDE_CHECK(hit[3].found && hit[3].insideKnown && !hit[3].inside);
    TIDE_CHECK(fabsf(hit[3].distance - 0.141421356f) < 1e-6f);
    TIDE_CHECK(fabsf(hit[3].point.x - 0.7f) < 1e-6f && fabsf(hit[3].point.y - 0.2f) < 1e-6f);
    TIDE_CHECK(fabsf(hit[3].normal.x - 0.707106781f) < 1e-6f && fabsf(hit[3].normal.y + 0.707106781f) < 1e-6f);
    // This is the embedded probe from the contact image, in sphere-local space.
    // The faceted mesh lies slightly inside its analytic unit-sphere envelope.
    TIDE_CHECK(hit[4].found && hit[4].insideKnown && hit[4].inside);
    TIDE_CHECK(hit[4].distance > 0.35f && hit[4].distance < 0.4f);
    TIDE_CHECK(tide_dot_f3(hit[4].point, hit[4].normal) > 0.95f);
    // Reversing a closed shell creates an inward-facing room, not a solid.
    TIDE_CHECK(hit[5].found && hit[5].insideKnown && !hit[5].inside);
    TIDE_CHECK(hit[6].found && !hit[6].insideKnown && !hit[6].inside);
    TIDE_CHECK(fabsf(hit[6].distance - 0.01f) < 1e-6f && hit[6].normal.z < -0.999f);
    TIDE_CHECK(!hit[7].found && !hit[8].found && !hit[9].found && !hit[10].found);
    // Two closed parts leave free space inside their common enclosing bounds.
    TIDE_CHECK(hit[11].found && hit[11].insideKnown && !hit[11].inside);
    TIDE_CHECK(fabsf(hit[11].distance - 1.5f) < 1e-6f);
    TIDE_CHECK(!hit[12].found);
    TIDE_CHECK(hit[13].found && hit[13].distance == 0 && hit[13].normal.x > 0.999f);
    TIDE_CHECK(hit[14].found && hit[14].insideKnown && hit[14].inside);
    TIDE_CHECK(fabsf(hit[14].distance - 5e-9f) < 1e-13f);
    TIDE_CHECK(hit[15].found && !hit[15].inside && hit[15].normal.x > 0.999f);
    TIDE_CHECK(hit[15].distance > 9e-6f && hit[15].distance < 11e-6f);
    TIDE_CHECK(fabsf(probe->relocated.x - 0.18f) < 1e-6f && probe->relocated.y == 0 && probe->relocated.z == 0);
    TIDE_CHECK(probe->retained.x == probe->relocated.x && probe->retained.y == 0 && probe->retained.z == 0);
    TIDE_CHECK(tide_dot_f3(probe->unresolved, probe->unresolved) == 0);
    // Squared distances exceed binary32 range here; the relocation bound must
    // still reject an escape beyond 45% spacing instead of accepting Inf<=Inf.
    TIDE_CHECK(probe->largeUnresolved.x == 0 && probe->largeUnresolved.y == 0 && probe->largeUnresolved.z == 0);
    finish();
}

TIDE_TEST(globalillumination_world_updates_relocate_and_hold_bounded_capture_positions)
{
    start(6);
    local.GIProbe.settings.updatesPerFrame = 16;
    local.GIPlacementAudit.enabled = true;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const int cycle = (environment->activeProbes + 15) / 16;
    for (int at = 0; at < cycle; at++) frame();
    TIDE_CHECK(environment->initialized == environment->activeProbes);
    TIDE_CHECK(local.GIPlacementAudit.relocated > 0 && local.GIPlacementAudit.maxFraction <= 0.450001f);
    local.GIPlacementAudit.compare = true;
    local.GIProbe.invalidate = true;
    local.GIProbe.invalidateRegion = (GlobalIllumination_LocalBounds){
        .minimum = {-20, -20, -20}, .maximum = {20, 20, 20}, .valid = true};
    for (int at = 0; at < cycle; at++) frame();
    TIDE_CHECK(local.GIPlacementAudit.stable);
    finish();
}

static const tide_shader replay_shader = {
    .dims = 3, .vertex_size = sizeof(tide_float3), .instance_floats = 4,
    .uniform_floats = 1, .texture_count = 1,
};

static uint32_t replay_mesh(tide_draw_list *list)
{
    static const tide_float3 vertices[] = {{-1, -1, 1}, {1, -1, 1}, {0, 1, 1}};
    static const uint32_t indices[] = {0, 1, 2};
    bool fill_mesh = false;
    const uint32_t mesh = tide_draw_mesh_slot(list, TIDE_MESH_BYTES, 71001, 71002, 1, &fill_mesh);
    if (fill_mesh) tide_draw_mesh_fill_bytes(list, mesh, vertices, sizeof vertices[0], 3, indices, 3);
    uint8_t *pixels = NULL;
    const uint32_t texture = tide_draw_texture_slot(list, TIDE_PIXELS_MEMORY, 71003, 1, 1, 1, &pixels);
    TIDE_CHECK(texture == 1 && pixels);
    if (pixels) memset(pixels, 255, 4);
    tide_draw_set_frustum_culling(list, false);
    return mesh;
}

static void replay_source(tide_draw_list *list, const uint32_t mesh)
{
    const tide_float4x4 transform = tide_identity_f4x4();
    const float values[4] = {2, 3, 5, 7};
    const uint32_t textures[TIDE_SHADER_TEXTURES] = {1};
    tide_draw_shaded(list, &replay_shader, mesh, &transform, values, textures, 0);
}

static void replay_rejected(tide_draw_list *list, const int32_t first, const int32_t end)
{
    const tide_draw_list before = *list;
    tide_draw_command last = {0};
    tide_draw_run run = {0};
    if (list->commands && list->count) memcpy(&last, &list->commands[list->count - 1u], sizeof last);
    if (list->runs && list->run_count) memcpy(&run, &list->runs[0], sizeof run);
    TIDE_CHECK(GIReplay(list, first, end) == 0);
    TIDE_CHECK(list->count == before.count && list->run_count == before.run_count);
    TIDE_CHECK(list->commands == before.commands && list->runs == before.runs);
    TIDE_CHECK(list->instance_used == before.instance_used && list->uniform_used == before.uniform_used);
    TIDE_CHECK(list->plain_open == before.plain_open && list->target == before.target && list->unsettled == before.unsettled);
    TIDE_CHECK(memcmp(&list->place, &before.place, sizeof list->place) == 0);
    if (list->commands && list->count) TIDE_CHECK(memcmp(&last, &list->commands[list->count - 1u], sizeof last) == 0);
    if (list->runs && list->run_count) TIDE_CHECK(memcmp(&run, &list->runs[0], sizeof run) == 0);
}

TIDE_TEST(globalillumination_replay_preserves_face_camera_and_independent_instance_runs)
{
    tide_draw_list list = {0};
    const uint32_t mesh = replay_mesh(&list);
    const int32_t first = GIRecordMark(&list);
    TIDE_CHECK(first == 0);
    replay_source(&list, mesh);
    replay_source(&list, mesh);
    TIDE_REQUIRE(list.count == 1 && list.run_count == 1 && list.runs[0].count == 2);
    const int32_t end = (int32_t)list.count;
    const tide_draw_run original = list.runs[0];
    const uint32_t instance_bytes = list.instance_used;
    const uint32_t uniform_bytes = list.uniform_used;
    tide_draw_to(&list, 71004, 32, 32);
    tide_draw_set_camera_3d(&list, tide_f3(1, 2, -3), tide_identity_q(), 75);
    tide_draw_clear(&list, TIDE_COLOR_BLACK);
    const uint32_t target = list.target;
    const uint32_t camera = list.place.camera_3d;
    const uint32_t commands = list.count;
    TIDE_REQUIRE(!list.unsettled && GIReplay(&list, first, end) == 1);
    TIDE_CHECK(list.count == commands + 1 && list.run_count == 2);
    TIDE_CHECK(list.target == target && list.place.camera_3d == camera && !list.unsettled);
    TIDE_CHECK(list.commands[commands].kind == TIDE_DRAW_SHADED && list.commands[commands].shaded.run == 1);
    TIDE_CHECK(memcmp(&original, &list.runs[0], sizeof original) == 0);
    TIDE_CHECK(memcmp(&original, &list.runs[1], sizeof original) == 0);
    TIDE_CHECK(list.instance_used == instance_bytes && list.uniform_used == uniform_bytes);
    tide_draw_to(&list, 71005, 32, 32);
    tide_draw_set_camera_3d(&list, tide_f3(-3, 4, 2), tide_identity_q(), 90);
    tide_draw_clear(&list, TIDE_COLOR_BLACK);
    TIDE_REQUIRE(GIReplay(&list, first, end) == 1 && list.run_count == 3);
    list.runs[2].count = 1;
    TIDE_CHECK(list.runs[0].count == 2 && list.runs[1].count == 2);
    list.runs[2].count = 2;
    replay_source(&list, mesh);
    // A later matching draw may extend this face, while all earlier faces
    // retain the original count and frame-owned instance/uniform offsets.
    TIDE_CHECK(list.runs[0].count == 2 && list.runs[1].count == 2 && list.runs[2].count == 3);
    const uint32_t previous_commands = list.count;
    const int32_t next = GIRecordMark(&list);
    TIDE_CHECK(next == (int32_t)previous_commands + 1);
    TIDE_CHECK(list.commands[previous_commands].kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(list.commands[previous_commands].mesh3.count == 0);
    replay_source(&list, mesh);
    TIDE_CHECK(list.run_count == 4 && list.runs[2].count == 3 && list.runs[3].count == 1);
    TIDE_CHECK(list.commands[next].kind == TIDE_DRAW_SHADED && list.commands[next].shaded.run == 3);
    tide_draw_free(&list);
}

TIDE_TEST(globalillumination_replay_ignores_non_capture_packets_and_future_run_indices)
{
    tide_draw_list list = {0};
    const uint32_t mesh = replay_mesh(&list);
    replay_source(&list, mesh);
    TIDE_REQUIRE(list.count == 1 && list.run_count == 1 && list.capacity >= 16 && list.run_capacity >= 3);
    static const uint32_t ignored[] = {TIDE_DRAW_CLIP, TIDE_DRAW_TARGET, TIDE_DRAW_CAMERA_3D,
        TIDE_DRAW_CLEAR, TIDE_DRAW_BLIT, TIDE_DRAW_MESH, TIDE_DRAW_MESH_3D, TIDE_DRAW_PLAIN};
    for (size_t at = 0; at < sizeof ignored / sizeof ignored[0]; at++)
        list.commands[list.count++] = (tide_draw_command){.kind = ignored[at]};
    tide_shader flat = replay_shader;
    flat.dims = 2;
    list.runs[list.run_count] = list.runs[0];
    list.runs[list.run_count++].shader = &flat;
    list.commands[list.count++] = (tide_draw_command){.kind = TIDE_DRAW_SHADED, .shaded = {.run = 1}};
    // This invalid index becomes allocated while valid packets are replayed.
    // It must remain invalid according to the original source run count.
    list.commands[list.count++] = (tide_draw_command){.kind = TIDE_DRAW_SHADED, .shaded = {.run = 2}};
    const uint32_t before = list.count;
    TIDE_CHECK(GIReplay(&list, 0, (int32_t)before) == 1);
    TIDE_CHECK(list.count == before + 1 && list.run_count == 3);
    TIDE_CHECK(list.commands[before].kind == TIDE_DRAW_SHADED && list.commands[before].shaded.run == 2);
    TIDE_CHECK(list.runs[2].shader == &replay_shader && list.runs[2].count == 1);
    tide_draw_free(&list);
}

TIDE_TEST(globalillumination_replay_target_wrapper_restores_face_after_draw_list_selects_window)
{
    tide_draw_list list = {0};
    const uint32_t mesh = replay_mesh(&list);
    const int32_t first = GIRecordMark(&list);
    replay_source(&list, mesh);
    const int32_t end = (int32_t)list.count;
    TIDE_REQUIRE(first == 0 && end == 1 && list.run_count == 1);
    const tide_render_target face = tide_render_target_make(32, 32);
    tide_draw_render_target(&list, face);
    tide_draw_set_camera_3d(&list, tide_f3(3, 5, 7), tide_identity_q(), 90);
    tide_draw_clear(&list, TIDE_COLOR_BLACK);
    const uint32_t face_target = list.target;
    const uint32_t face_camera = list.place.camera_3d;
    const uint32_t before = list.count;
    TIDE_REQUIRE(face_target && face_camera && !list.unsettled);
    // Evaluating Tide's Draw.list selects the window before the extern call.
    // The wrapper must restore this face and its already settled camera.
    tide_draw_render_target(&list, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    TIDE_CHECK(list.target == 0 && list.place.camera_3d != face_camera);
    TIDE_CHECK(GIReplayTo(&list, face, first, end) == 1);
    TIDE_CHECK(list.target == face_target && list.place.camera_3d == face_camera && !list.unsettled);
    TIDE_CHECK(list.count == before + 3 && list.run_count == 2);
    TIDE_CHECK(list.commands[before].kind == TIDE_DRAW_TARGET && list.commands[before].target == 0);
    TIDE_CHECK(list.commands[before + 1].kind == TIDE_DRAW_TARGET && list.commands[before + 1].target == face_target);
    TIDE_CHECK(list.commands[before + 2].kind == TIDE_DRAW_SHADED && list.commands[before + 2].shaded.run == 1);
    TIDE_CHECK(list.runs[0].count == 1 && list.runs[1].count == 1);
    TIDE_CHECK(list.runs[0].mesh == list.runs[1].mesh && list.runs[0].first == list.runs[1].first
        && list.runs[0].uniforms == list.runs[1].uniforms);
    const uint32_t marked = list.count;
    tide_draw_render_target(&list, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    const int32_t next = GIRecordMarkTo(&list, face);
    TIDE_CHECK(next == (int32_t)marked + 2);
    TIDE_CHECK(list.target == face_target && list.place.camera_3d == face_camera && !list.unsettled);
    // The explicit target switches already separate the old shaded run;
    // MarkTo need not insert an additional empty-mesh barrier in this case.
    TIDE_CHECK(list.commands[marked].kind == TIDE_DRAW_TARGET && list.commands[marked].target == 0);
    TIDE_CHECK(list.commands[marked + 1].kind == TIDE_DRAW_TARGET && list.commands[marked + 1].target == face_target);
    replay_source(&list, mesh);
    TIDE_CHECK(list.run_count == 3 && list.runs[0].count == 1 && list.runs[1].count == 1 && list.runs[2].count == 1);
    tide_draw_free(&list);
    tide_render_target_dispose(face);
}

TIDE_TEST(globalillumination_replay_rejects_invalid_ranges_and_payload_spans_without_mutation)
{
    tide_draw_list list = {0};
    const uint32_t mesh = replay_mesh(&list);
    replay_source(&list, mesh);
    TIDE_REQUIRE(list.count == 1 && list.run_count == 1);
    TIDE_CHECK(GIRecordMark(NULL) == -1 && GIReplay(NULL, 0, 1) == 0);
    replay_rejected(&list, -1, 1);
    replay_rejected(&list, 0, 0);
    replay_rejected(&list, 1, 1);
    replay_rejected(&list, 0, 2);
    tide_draw_set_camera_3d(&list, tide_f3(0, 0, -1), tide_identity_q(), 90);
    TIDE_CHECK(list.unsettled);
    replay_rejected(&list, 0, 1);
    tide_draw_clear(&list, TIDE_COLOR_BLACK);
    TIDE_REQUIRE(!list.unsettled);
    const tide_draw_run valid = list.runs[0];
    const tide_draw_mesh_data valid_mesh = list.meshes[mesh - 1u];
    tide_shader changed = replay_shader;
    list.runs[0].shader = NULL; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].shader = &changed;
    changed.dims = 2; replay_rejected(&list, 0, 1);
    changed = replay_shader; changed.texture_count = TIDE_SHADER_TEXTURES + 1; replay_rejected(&list, 0, 1);
    changed = replay_shader; changed.instance_floats = UINT32_MAX; replay_rejected(&list, 0, 1);
    changed = replay_shader; changed.uniform_floats = UINT32_MAX; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].count = 0; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].count = UINT32_MAX; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].mesh = 0; replay_rejected(&list, 0, 1);
    list.runs[0].mesh = list.mesh_count + 1; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].first = list.instance_used + 1; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].first++; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].uniforms = list.uniform_used + 1; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].uniforms++; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.runs[0].textures[0] = list.texture_count + 1; replay_rejected(&list, 0, 1);
    list.runs[0] = valid;
    list.meshes[mesh - 1u].space = TIDE_MESH_MEMORY; replay_rejected(&list, 0, 1);
    list.meshes[mesh - 1u] = valid_mesh;
    list.meshes[mesh - 1u].vertex_size++; replay_rejected(&list, 0, 1);
    list.meshes[mesh - 1u] = valid_mesh;
    list.meshes[mesh - 1u].index_count = 0; replay_rejected(&list, 0, 1);
    list.meshes[mesh - 1u] = valid_mesh;
    list.commands[0].shaded.run = list.run_count; replay_rejected(&list, 0, 1);
    list.commands[0].shaded.run = UINT32_MAX; replay_rejected(&list, 0, 1);
    list.commands[0].shaded.run = 0;
    uint8_t *instances = list.instances;
    list.instances = NULL; replay_rejected(&list, 0, 1); list.instances = instances;
    uint8_t *uniforms = list.uniforms;
    list.uniforms = NULL; replay_rejected(&list, 0, 1); list.uniforms = uniforms;
    tide_draw_mesh_data *meshes = list.meshes;
    list.meshes = NULL; replay_rejected(&list, 0, 1); list.meshes = meshes;
    tide_draw_run *runs = list.runs;
    list.runs = NULL; replay_rejected(&list, 0, 1); list.runs = runs;
    TIDE_CHECK(GIReplay(&list, 0, 1) == 1);
    tide_draw_free(&list);
}
