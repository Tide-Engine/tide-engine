#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/internal/mesh_lod.h"
#include "tide/pack.h"
#include "tide/worker.h"
#include "tide_test.h"

// Levels of detail for meshes a game makes (lod_generated.tide): a draw with `lod:` asks for its mesh's levels,
// which tide_draw_prepare hands to a background (here, one that runs when the test says), and the frame after
// they're made has them, as an asset's levels are: picked by each draw, made by the same code from the same
// vertices and triangles, kept by what's in the mesh.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// A background that runs what it's given when the test says, or when it's helped
static tide_background *queued[64];
static uint32_t queued_count, submitted_count;

static void background(void *self, tide_background *job)
{
    (void)self;
    TIDE_REQUIRE(queued_count < 64u);
    queued[queued_count++] = job;
    submitted_count++;
}

static bool help(void *self, const bool helpers_only)
{
    (void)self, (void)helpers_only;
    if (queued_count == 0) return false;
    tide_background *job = queued[0];
    memmove(&queued[0], &queued[1], (size_t)--queued_count * sizeof queued[0]);
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

static void run_all(void)
{
    while (help(NULL, false)) {}
}

static const tide_jobs jobs = {.threads = 1, .background = background, .help = help};

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_text_use(&world.heap, &local.heap);
    queued_count = submitted_count = 0;
}

static void finish(void)
{
    tide_draw_free(&draw); // Waits for what it gave the background, helping it
    TIDE_CHECK(queued_count == 0);
    draw = (tide_draw_list){0};
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}

// A frame of the view's `mode`, in a window 600 pixels high, then prepared as a renderer would, with the jobs
static void frame(const int32_t mode)
{
    local.Art.mode = mode;
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(800.0f, 600.0f));
    tide_gui_begin(&gui, &devices, tide_f2(800.0f, 600.0f), NULL);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_text_use(&world.heap, &local.heap);
    tide_draw_prepare(&draw, &jobs);
}

// The meshes the list has that drew this frame, whole (no level's), which asked for levels or didn't
static uint32_t meshes(const tide_draw_mesh_data **out, const uint32_t most, const bool asked)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < draw.mesh_count && count < most; i++) {
        const tide_draw_mesh_data *m = &draw.meshes[i];
        if (m->space == TIDE_MESH_NONE || m->base || m->frame != draw.frame || (m->lod_key != 0) != asked) continue;
        out[count++] = m;
    }
    return count;
}

// The 3D runs the frame recorded that draw something: the mesh each names
static uint32_t runs(const tide_draw_mesh_data **out, const uint32_t most)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < draw.count && count < most; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_MESH_3D && c->mesh3.mesh && c->mesh3.count) out[count++] = &draw.meshes[c->mesh3.mesh - 1u];
        if (c->kind == TIDE_DRAW_SHADED && draw.runs[c->shaded.run].shader && draw.runs[c->shaded.run].count)
            out[count++] = &draw.meshes[draw.runs[c->shaded.run].mesh - 1u];
    }
    return count;
}

// The levels tidec would make for an asset of the same vertices and triangles, from what the list copied
static void made_as_an_asset(const tide_draw_mesh_data *m, tide_mesh_lod *out)
{
    float *position = malloc((size_t)m->vertex_count * 3u * sizeof(float));
    float *uv = malloc((size_t)m->vertex_count * 2u * sizeof(float));
    float *color = malloc((size_t)m->vertex_count * 4u * sizeof(float));
    TIDE_REQUIRE(position && uv && color && m->vertex_size == sizeof(tide_vertex3));
    for (uint32_t v = 0; v < m->vertex_count; v++) {
        memcpy(&position[v * 3u], &m->vertices[v].position, 3u * sizeof(float));
        memcpy(&uv[v * 2u], &m->vertices[v].uv, 2u * sizeof(float));
        memcpy(&color[v * 4u], &m->vertices[v].color, 4u * sizeof(float));
    }
    const tide_mesh_lod_source source = {
        .vertices = m->vertex_count, .position = position, .uv = uv, .color = color, .index = m->indices,
        .indices = m->index_count, .mesh_least = TIDE_MESH_LOD_MESH_LEAST, .triangles_least = TIDE_MESH_LOD_TRIANGLES_LEAST};
    tide_mesh_lod_make(&source, out);
    free(position), free(uv), free(color);
}

// A draw with `lod:` asks once, and draws whole until its levels are made; the frame after, it has the levels an
// asset of the same mesh would, and picks among them; the same lists drawn without asking are another mesh, whole.
TIDE_TEST(lod_generated_meshes_have_an_assets_levels_once_made)
{
    start();
    frame(0);
    const tide_draw_mesh_data *asked[4], *plain[4];
    TIDE_REQUIRE(meshes(asked, 4, true) == 1 && meshes(plain, 4, false) == 1);
    TIDE_CHECK(asked[0]->levels == 0 && plain[0]->levels == 0 && asked[0]->index_count == 3200u * 3u);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 1 && submitted_count == 1); // The near and far draws are one mesh
    const tide_draw_mesh_data *drawn[8];
    TIDE_REQUIRE(runs(drawn, 8) == 2); // The far and near draws one run, as one mesh
    for (uint32_t i = 0; i < 2; i++) TIDE_CHECK(drawn[i]->base == 0); // Whole, as there's nothing to pick from yet
    run_all();
    frame(0);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 0 && submitted_count == 1);
    TIDE_REQUIRE(meshes(asked, 4, true) == 1 && meshes(plain, 4, false) == 1);
    const tide_draw_mesh_data *m = asked[0];
    TIDE_REQUIRE(m->levels >= 2);
    TIDE_CHECK(plain[0]->levels == 0 && m->lod_multiplier == 1.0f);
    // Bit for bit what an asset's would be
    tide_mesh_lod lod;
    made_as_an_asset(m, &lod);
    TIDE_REQUIRE(lod.levels == m->levels);
    for (uint32_t level = 0; level < lod.levels; level++) {
        TIDE_CHECK(lod.error[level] == m->level_error[level] && lod.count[level] == m->level_count[level]);
        TIDE_CHECK(memcmp(&lod.index[lod.first[level]], &m->level_indices[m->level_first[level]],
                          lod.count[level] * sizeof(uint32_t)) == 0);
    }
    TIDE_CHECK(memcmp(lod.center, m->center, sizeof lod.center) == 0 && lod.radius == m->radius);
    tide_mesh_lod_free(&lod);
    // Gathered, finest first: the near draw whole, the far one at a level, and the one that didn't ask whole far away
    TIDE_REQUIRE(runs(drawn, 8) == 3);
    TIDE_CHECK(drawn[0]->base == 0 && drawn[0]->lod_key == m->lod_key);
    TIDE_CHECK(drawn[1]->base != 0 && drawn[1]->level >= 1 && &draw.meshes[drawn[1]->base - 1u] == m);
    TIDE_CHECK(drawn[2]->base == 0 && drawn[2]->lod_key == 0);
    finish();
}

// Instances through a frag, of a game's own vertex, each pick a level, with the draw's multiplier; a mesh of too
// few triangles never asks the background.
TIDE_TEST(lod_generated_instances_pick_and_small_meshes_are_left_whole)
{
    start();
    frame(1);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 1);
    run_all();
    frame(1);
    const tide_draw_mesh_data *asked[4];
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    TIDE_CHECK(asked[0]->levels >= 2 && asked[0]->lod_multiplier == 2.0f && asked[0]->vertex_size == 32u);
    const tide_draw_mesh_data *drawn[8];
    const uint32_t count = runs(drawn, 8);
    // The two far instances at a level, the near one whole: a run each, finest first once gathered
    TIDE_REQUIRE(count == 2);
    TIDE_CHECK(drawn[0]->base == 0 && drawn[1]->base != 0);
    frame(2);
    frame(2);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 0 && submitted_count == 1);
    finish();
}

// Levels are kept by what's in the mesh: drawn again after its place in the list went, it has them at once, and so
// does the same mesh made again in the scratch area; lists the world keeps made again with other numbers stop what
// was being made of them before, which keeps nothing.
TIDE_TEST(lod_generated_levels_are_kept_by_contents_and_superseded_lists_stop)
{
    start();
    frame(0);
    run_all();
    frame(6);
    frame(6); // The mesh's place in the list goes: nothing drew it
    const tide_draw_mesh_data *asked[4];
    TIDE_CHECK(meshes(asked, 4, true) == 0);
    frame(0);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 0 && submitted_count == 1);
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    TIDE_CHECK(asked[0]->levels >= 2); // The same frame it was copied in again
    // Made again each frame, and never helped: the first is submitted, the next ones stop it and the one before
    local.Art.multiplier = 3;
    frame(4);
    TIDE_CHECK(submitted_count == 2 && queued_count == 1);
    frame(4);
    frame(4);
    // Two waiting, beside the first: the background was given the most it runs at once (two), and no more
    TIDE_CHECK(submitted_count == 3 && queued_count == 2);
    run_all();
    frame(4);
    // The two that ran had stopped: nothing of them is kept, and the newest goes to the background
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 1 && submitted_count == 4);
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    TIDE_CHECK(asked[0]->levels == 0 && asked[0]->lod_want == 3.0f);
    run_all();
    frame(6);
    // A scratch mesh, with a multiplier that's none: 1
    frame(5);
    run_all();
    frame(5);
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    TIDE_CHECK(asked[0]->levels >= 2 && asked[0]->lod_multiplier == 1.0f);
    finish();
}

// Numbers that are none and indices past the vertices or under 0 make whatever levels they make of the mesh's own
// corners, or none; a list freed while the background has its jobs waits for them.
TIDE_TEST(lod_generated_wild_numbers_and_freeing_while_made)
{
    start();
    frame(3);
    run_all();
    frame(3);
    const tide_draw_mesh_data *asked[4];
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    const tide_draw_mesh_data *m = asked[0];
    for (uint32_t i = 0; i < m->level_index_count; i++) TIDE_CHECK(m->level_indices[i] < m->vertex_count);
    for (uint32_t level = 1; level < m->levels; level++) TIDE_CHECK(m->level_error[level] >= m->level_error[level - 1u]);
    // Given to the background and not run when the list goes: freeing it helps them through
    frame(1);
    TIDE_CHECK(queued_count == 1);
    finish();
}

// A model with bones drawn with `lod:` (mode 7 an asset, 8 one decoded while the game runs) has its bones weighed,
// as the same file's [LOD] does as the game is built:
// the levels made while the game runs are the build's, bit for bit.
static void skinned_levels_are_the_builds(const int32_t mode)
{
    start();
    frame(mode);
    TIDE_CHECK(tide_draw_levels_pending(&draw) == 1);
    run_all();
    frame(mode);
    const tide_draw_mesh_data *asked[4];
    TIDE_REQUIRE(meshes(asked, 4, true) == 1);
    const tide_draw_mesh_data *built = NULL;
    for (uint32_t i = 0; i < draw.mesh_count; i++) {
        const tide_draw_mesh_data *m = &draw.meshes[i];
        if (m->space != TIDE_MESH_NONE && !m->base && !m->lod_key && m->levels) built = m;
    }
    TIDE_REQUIRE(built);
    const tide_draw_mesh_data *m = asked[0];
    TIDE_REQUIRE(m->levels >= 2 && m->levels == built->levels && m->vertex_count == built->vertex_count);
    TIDE_CHECK(m->index_count == built->index_count && m->radius == built->radius);
    for (uint32_t level = 0; level < m->levels; level++) {
        TIDE_CHECK(m->level_error[level] == built->level_error[level] && m->level_count[level] == built->level_count[level]);
        TIDE_CHECK(memcmp(&m->level_indices[m->level_first[level]], &built->level_indices[built->level_first[level]],
                          m->level_count[level] * sizeof(uint32_t)) == 0);
    }
    // ...where the same mesh weighed without its bones would have other levels
    static float position[4096 * 3], normal[4096 * 3], uv[4096 * 2];
    TIDE_REQUIRE(m->vertex_count <= 4096u && m->vertex_size == 32u);
    for (uint32_t v = 0; v < m->vertex_count; v++) {
        const uint8_t *corner = (const uint8_t *)m->bytes + (size_t)v * 32u;
        memcpy(&position[v * 3u], corner, 12u), memcpy(&normal[v * 3u], corner + 12u, 12u), memcpy(&uv[v * 2u], corner + 24u, 8u);
    }
    const tide_mesh_lod_source boneless = {.vertices = m->vertex_count, .position = position, .normal = normal, .uv = uv,
                                           .index = m->indices, .indices = m->index_count,
                                           .mesh_least = TIDE_MESH_LOD_MESH_LEAST,
                                           .triangles_least = TIDE_MESH_LOD_TRIANGLES_LEAST};
    tide_mesh_lod lod;
    tide_mesh_lod_make(&boneless, &lod);
    bool same = lod.levels == m->levels;
    for (uint32_t level = 0; same && level < lod.levels; level++) {
        same = lod.count[level] == m->level_count[level]
            && memcmp(&lod.index[lod.first[level]], &m->level_indices[m->level_first[level]],
                      lod.count[level] * sizeof(uint32_t)) == 0;
    }
    TIDE_CHECK(!same);
    tide_mesh_lod_free(&lod);
    finish();
}

TIDE_TEST(lod_generated_skinned_models_weigh_their_bones_as_an_asset_does)
{
    skinned_levels_are_the_builds(7);
}

// A model decoded while the game runs (SkinnedMesh<V>.Decode), which has no declaration to mark, the same.
TIDE_TEST(lod_generated_decoded_models_weigh_their_bones_too)
{
    skinned_levels_are_the_builds(8);
}

TIDE_TEST(lod_generated_skinned_operands_precede_mutating_lod)
{
    start();
    local.Art.multiplier = -1.0f;
    const uint64_t hash = tide_world_hash(&world);
    frame(9);
    TIDE_CHECK(local.Art.multiplier == 2.0f && local.Art.lodMutations == 1);
    TIDE_CHECK(local.Art.observedLodOperand == -1.0f);
    unsigned instances = 0;
    for (uint32_t r = 0; r < draw.run_count; r++) {
        const tide_draw_run *run = &draw.runs[r];
        TIDE_REQUIRE(run->shader && run->shader->instance_floats >= 4);
        for (uint32_t i = 0; i < run->count; i++) {
            tide_float4x4 transform;
            memcpy(&transform, tide_draw_run_instance(&draw, run, i), sizeof transform);
            TIDE_CHECK(transform.c3.x == -1.0f && transform.c3.z == 900.0f);
            instances++;
        }
    }
    TIDE_CHECK(instances == 1 && tide_world_hash(&world) == hash);
    finish();
}
