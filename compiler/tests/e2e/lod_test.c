#include <math.h>
#include <string.h>

#include "game.h"
#include "tide/math.h"
#include "tide/pack.h"
#include "tide_test.h"

// Levels of detail from Tide (lod.tide): the meshes marked [LOD] have levels
// in the pack that no code reads, each draw records the level its place
// picks, and the settings name a level or the camera to pick by.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.shadow = (tide_render_target){.id = 11, .size = {.width = 100, .height = 100, .scale = 1}};
    tide_text_use(&world.heap, &local.heap);
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_pack_unload();
}

// A frame of the view's `mode`, in a window 600 pixels high: through the view's camera, a unit of the world a
// unit away is 300 / tan(30 degrees) pixels.
static void frame(const int32_t mode)
{
    local.Art.mode = mode;
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(800.0f, 600.0f));
    tide_gui_begin(&gui, &devices, tide_f2(800.0f, 600.0f), NULL);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_text_use(&world.heap, &local.heap);
}

static const float window_scale = 519.6152f, target_scale = 86.60254f; // The target is 100 pixels high

typedef struct run {
    uint32_t mesh, count, first;
    uint32_t target; // 1 + its place among the list's targets, or 0 for the window
    bool shaded;
} run;

// The runs of meshes the frame recorded that draw something, in order.
static uint32_t runs_of(run *out, const uint32_t most)
{
    uint32_t count = 0, target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target;
        if (count == most) break;
        if (c->kind == TIDE_DRAW_MESH_3D && c->mesh3.mesh && c->mesh3.count) {
            out[count++] = (run){.mesh = c->mesh3.mesh, .count = c->mesh3.count, .first = c->mesh3.first, .target = target};
        } else if (c->kind == TIDE_DRAW_SHADED && draw.runs[c->shaded.run].shader && draw.runs[c->shaded.run].count) {
            const tide_draw_run *r = &draw.runs[c->shaded.run];
            out[count++] = (run){.mesh = r->mesh, .count = r->count, .first = r->first, .target = target, .shaded = true};
        }
    }
    return count;
}

static const tide_draw_mesh_data *mesh_of(const run *r)
{
    return &draw.meshes[r->mesh - 1u];
}

// The full mesh a run's mesh is a level of, or is.
static const tide_draw_mesh_data *whole_of(const run *r)
{
    const tide_draw_mesh_data *m = mesh_of(r);
    return m->base ? &draw.meshes[m->base - 1u] : m;
}

// The level a mesh whose center is `z` along the camera's view draws at, where a unit a unit away is `scale`
// pixels: the simplest that's off by no more than a pixel at the nearest its ball comes.
static uint32_t level_at(const tide_draw_mesh_data *whole, const float z, const float scale)
{
    const float allowed = (z - whole->radius) / scale;
    uint32_t level = 0;
    while (level < whole->levels && whole->level_error[level] <= allowed) level++;
    return level;
}

// Whether a level's triangles are the mesh's own corners, in whole triangles, fewer than the level before's.
static bool level_holds(const tide_draw_mesh_data *whole, const uint32_t level)
{
    const uint32_t count = whole->level_count[level - 1u];
    const uint32_t before = level > 1u ? whole->level_count[level - 2u] : whole->index_count;
    if (count == 0 || count % 3u || count > before - before / 4u) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (whole->level_indices[whole->level_first[level - 1u] + i] >= whole->vertex_count) return false;
    }
    return level == 1u || whole->level_error[level - 1u] >= whole->level_error[level - 2u];
}

// A mesh marked [LOD] has levels, made as the game was built: its lists are
// the full mesh's all the same, and one with nothing to take, or with no
// [LOD], has none.
TIDE_TEST(lod_assets_have_levels_beside_the_full_mesh)
{
    start();
    // 9,024 triangles of 4,751 corners (the file's 4,753, less the two no triangle names), with or without levels
    TIDE_CHECK(world.Seen.corners == 4751 && world.Seen.indices == 9024 * 3 && world.Seen.wholeIndices == 9024 * 3);
    TIDE_CHECK(world.Seen.armIndices == 32 * 32 * 6);
    frame(0);
    tide_draw_prepare(&draw, NULL);
    run runs[16];
    const uint32_t count = runs_of(runs, 16);
    TIDE_REQUIRE(count >= 4);
    const tide_draw_mesh_data *ball = whole_of(&runs[0]);
    // Four levels: 4,512, 2,256, 1,128 and 564 triangles or so, and none of the 282 after them, which is under 500
    TIDE_REQUIRE(ball->levels == 4 && ball->index_count == 9024 * 3 && ball->vertex_count == 4751);
    TIDE_CHECK(ball->level_count[3] >= 500 * 3 && ball->level_count[3] < 600 * 3);
    for (uint32_t level = 1; level <= ball->levels; level++) TIDE_CHECK(level_holds(ball, level));
    TIDE_CHECK(fabsf(ball->radius - 1.0f) < 1e-3f && fabsf(ball->center[0]) < 1e-3f && fabsf(ball->center[1]) < 1e-3f);
    TIDE_CHECK(ball->level_error[0] > 0.0f && ball->level_error[ball->levels - 1u] < 0.5f);
    // The same file with no [LOD], and the two that are marked and too simple (two triangles, and 960, which
    // is under twice the fewest a level has): no levels, and one run each, whole
    uint32_t plain = 0;
    for (uint32_t i = 0; i < count; i++) {
        const tide_draw_mesh_data *m = mesh_of(&runs[i]);
        if (m->base == 0 && m->levels == 0) {
            plain++;
            TIDE_CHECK(m->index_count == 9024 * 3 || m->index_count == 6 || m->index_count == 960 * 3);
            TIDE_CHECK(m != ball && runs[i].count == 1);
        }
    }
    TIDE_CHECK(plain == 3);
    finish();
}

// Each draw picks by where it is: near is the full mesh, far its last level,
// and the draws of one level are a run, the finest first. A mesh named by its
// two lists, by its place in a list and by its key are all the one mesh.
TIDE_TEST(lod_draws_pick_by_how_far_they_are)
{
    start();
    frame(0);
    tide_draw_prepare(&draw, NULL);
    run runs[16];
    const uint32_t count = runs_of(runs, 16);
    TIDE_REQUIRE(count >= 4);
    const tide_draw_mesh_data *ball = whole_of(&runs[0]);
    const uint32_t near = level_at(ball, 2.0f, window_scale), mid = level_at(ball, 6.0f, window_scale);
    const uint32_t far = level_at(ball, 900.0f, window_scale);
    TIDE_REQUIRE(near == 0 && mid > near && far > mid && far == ball->levels);
    // Three runs of it, a level each, then the three meshes with no levels, which come between: the three far
    // draws after them (by its lists, from a list and by its key) are the far level's again, one run
    TIDE_REQUIRE(count == 7);
    TIDE_CHECK(mesh_of(&runs[0]) == ball && runs[0].count == 1);
    TIDE_CHECK(whole_of(&runs[1]) == ball && mesh_of(&runs[1])->level == mid && runs[1].count == 1);
    TIDE_CHECK(whole_of(&runs[2]) == ball && mesh_of(&runs[2])->level == far && runs[2].count == 1);
    TIDE_CHECK(mesh_of(&runs[2])->index_count == ball->level_count[far - 1u] && mesh_of(&runs[2])->vertices == NULL);
    TIDE_CHECK(mesh_of(&runs[3])->levels == 0 && mesh_of(&runs[4])->levels == 0 && mesh_of(&runs[5])->levels == 0);
    TIDE_CHECK(mesh_of(&runs[5])->base == 0 && mesh_of(&runs[5])->index_count == 960 * 3);
    TIDE_CHECK(runs[6].mesh == runs[2].mesh && runs[6].count == 3);
    finish();
}

// The instances of one draw are a run a level, in the order the list has
// them: places 1 and 4 are near, 2 between, and 0 and 3 far.
TIDE_TEST(lod_instances_each_pick_their_own)
{
    start();
    frame(1);
    run runs[8];
    TIDE_REQUIRE(runs_of(runs, 8) == 3);
    const tide_draw_mesh_data *ball = whole_of(&runs[0]);
    const uint32_t mid = level_at(ball, 6.0f, window_scale);
    TIDE_CHECK(mesh_of(&runs[0]) == ball && runs[0].count == 2);
    TIDE_CHECK(draw.matrices[runs[0].first].c3.z == 2.0f && draw.matrices[runs[0].first + 1u].c3.z == 2.5f);
    TIDE_CHECK(mesh_of(&runs[1])->level == mid && runs[1].count == 1 && draw.matrices[runs[1].first].c3.z == 6.0f);
    TIDE_CHECK(mesh_of(&runs[2])->level == ball->levels && runs[2].count == 2);
    TIDE_CHECK(draw.matrices[runs[2].first].c3.z == 900.0f && draw.matrices[runs[2].first + 1u].c3.z == 950.0f);
    finish();
}

// Through a frag, a vertex of the game's: the same picks, alone and as
// instances, each instance's transform going with it.
TIDE_TEST(lod_draws_through_a_frag_pick_too)
{
    start();
    frame(2);
    tide_draw_prepare(&draw, NULL);
    run runs[8];
    const uint32_t count = runs_of(runs, 8);
    TIDE_REQUIRE(count == 3 && runs[0].shaded && runs[1].shaded && runs[2].shaded);
    const tide_draw_mesh_data *lit = whole_of(&runs[0]);
    TIDE_REQUIRE(lit->space == TIDE_MESH_BYTES && lit->levels >= 4 && lit->vertex_size == sizeof(Corner));
    const uint32_t mid = level_at(lit, 6.0f, window_scale);
    // The single draws and the instances are the same draw to the list: near ones 1 + 2, between 1, far 1 + 2
    TIDE_CHECK(mesh_of(&runs[0]) == lit && runs[0].count == 3);
    TIDE_CHECK(mesh_of(&runs[1])->level == mid && whole_of(&runs[1]) == lit && runs[1].count == 1);
    TIDE_CHECK(mesh_of(&runs[2])->level == lit->levels && runs[2].count == 3);
    TIDE_CHECK(mesh_of(&runs[2])->vertex_size == sizeof(Corner) && mesh_of(&runs[2])->bytes == NULL);
    finish();
}

// Draw.LOD and Draw.SetLOD name the level: 0 is the full mesh wherever it is,
// a number past the last is the last, and with no number each draw picks
// again. Every instance of a draw has the level, in the order it was given.
TIDE_TEST(lod_levels_are_named_by_draw_lod)
{
    start();
    frame(3);
    run runs[12];
    const uint32_t count = runs_of(runs, 12);
    TIDE_REQUIRE(count == 6);
    const tide_draw_mesh_data *ball = whole_of(&runs[0]);
    TIDE_CHECK(mesh_of(&runs[0]) == ball && draw.matrices[runs[0].first].c3.z == 900.0f); // LOD(0), far
    TIDE_CHECK(mesh_of(&runs[1])->level == 1 && runs[1].count == 1);                      // SetLOD(1), near
    TIDE_CHECK(mesh_of(&runs[2])->level == ball->levels && runs[2].count == 1);           // LOD(99)
    TIDE_CHECK(mesh_of(&runs[3]) == ball && runs[3].count == 1);                          // LOD(): picked, near
    // Back out of both blocks, SetLOD(1) is what's set: the instances are one run, as the list has them
    TIDE_CHECK(mesh_of(&runs[4])->level == 1 && runs[4].count == 5);
    TIDE_CHECK(draw.matrices[runs[4].first].c3.z == 900.0f && draw.matrices[runs[4].first + 4u].c3.z == 2.5f);
    TIDE_CHECK(mesh_of(&runs[5]) == ball && runs[5].count == 1); // SetLOD(): picked again
    finish();
}

// A target picks by its own camera and its own pixels, a sixth of the
// window's here, until it's told a camera to pick through, which counts the
// window's. A level named for it is its own too.
TIDE_TEST(lod_targets_pick_by_their_own_camera_or_the_one_they_are_told)
{
    start();
    frame(4);
    run runs[12];
    const uint32_t count = runs_of(runs, 12);
    TIDE_REQUIRE(count >= 5);
    const tide_draw_mesh_data *ball = whole_of(&runs[0]);
    const uint32_t own_near = level_at(ball, 2.0f, target_scale), own_mid = level_at(ball, 6.0f, target_scale);
    const uint32_t window_mid = level_at(ball, 6.0f, window_scale);
    TIDE_REQUIRE(own_near > 0 && own_mid > window_mid && window_mid > 0 && level_at(ball, 2.0f, window_scale) == 0);
    for (uint32_t i = 0; i < count; i++) TIDE_CHECK(runs[i].target != 0 && whole_of(&runs[i]) == ball);
    // In the order they were drawn: its own pick, the window's camera's (the full mesh, near), the window's
    // camera's by its matrices, its own again, level 2 by name, and its own
    uint32_t levels[8], drawn = 0;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < runs[i].count && drawn < 8; k++) levels[drawn++] = mesh_of(&runs[i])->level;
    }
    TIDE_REQUIRE(drawn == 6);
    TIDE_CHECK(levels[0] == own_near);
    TIDE_CHECK(levels[1] == 0);
    TIDE_CHECK(levels[2] == window_mid);
    TIDE_CHECK(levels[3] == own_mid);
    TIDE_CHECK(levels[4] == 2);
    TIDE_CHECK(levels[5] == own_near);
    finish();
}

// A skinned mesh marked [LOD] has levels too, over the same vertices, which
// its bones' weights go by: far, it draws one of them through the same pose.
TIDE_TEST(lod_skinned_meshes_have_levels)
{
    start();
    frame(5);
    run runs[8];
    const uint32_t count = runs_of(runs, 8);
    TIDE_REQUIRE(count == 2 && runs[0].shaded && runs[1].shaded);
    const tide_draw_mesh_data *arm = whole_of(&runs[0]);
    TIDE_REQUIRE(arm->levels == 2 && arm->index_count == 32 * 32 * 6 && arm->vertex_count == 33 * 32);
    for (uint32_t level = 1; level <= arm->levels; level++) TIDE_CHECK(level_holds(arm, level));
    // Near, it's whichever level is within a pixel there; far, the last
    TIDE_CHECK(mesh_of(&runs[0])->level == 0 && level_at(arm, 2.2f, window_scale) == 0);
    TIDE_CHECK(whole_of(&runs[1]) == arm && mesh_of(&runs[1])->level == arm->levels);
    TIDE_CHECK(mesh_of(&runs[1])->vertex_count == arm->vertex_count && mesh_of(&runs[1])->bytes == NULL);
    // The tube is straight, so its rings go freely where they all follow one bone: the first level is hardly off
    TIDE_CHECK(arm->level_error[0] < 0.02f);
    finish();
}

// A pack loaded again has the same levels, and a mesh drawn after it is
// filled from it as before.
TIDE_TEST(lod_levels_come_back_with_the_pack)
{
    start();
    frame(1);
    run runs[8];
    TIDE_REQUIRE(runs_of(runs, 8) == 3);
    const uint32_t levels = whole_of(&runs[0])->levels, last = mesh_of(&runs[2])->index_count;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame(1);
    TIDE_REQUIRE(runs_of(runs, 8) == 3);
    TIDE_CHECK(whole_of(&runs[0])->levels == levels && mesh_of(&runs[2])->index_count == last);
    finish();
}
