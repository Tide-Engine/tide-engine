#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static void checked(const tide_world *world)
{
    TIDE_CHECK(world->Probe.count == 6 && world->Probe.emptyCount == 0);
    TIDE_CHECK(world->Probe.equal && world->Probe.differentNames && world->Probe.defaults);
    TIDE_CHECK(world->Probe.beforeEffect && world->Probe.ordered);
    TIDE_CHECK(world->Probe.order == 12 && world->Probe.calls == 2);
    TIDE_CHECK(world->Probe.folded.at[0] == 0 && sizeof world->Probe.folded.at / sizeof world->Probe.folded.at[0] == 2);
}

static void capture(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    tide_draw_reset(draw);
    tide_draw_screen_size(draw, (tide_float2){800, 600});
    gui->width = 800; gui->height = 600;
    tide_frame(world, NULL, 1, NULL, NULL, 1, local, draw, gui);
}

static void record(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    capture(world, local, draw, gui);
    tide_draw_prepare(draw, NULL);
}

static void borrowed_paths(const tide_draw_list *draw, const bool full)
{
    // A component-owned model would bypass the palette memo and lose packed
    // LOD metadata. The reference resolver must preserve both original borrows.
    TIDE_REQUIRE(draw->skinned != NULL);
    uint32_t small = 0, detailed_items = 0;
    bool batched = false;
    for (uint32_t r = 0; r < draw->run_count; r++) {
        const tide_draw_run *run = &draw->runs[r];
        if (!run->count) continue;
        TIDE_REQUIRE(run->mesh && run->mesh <= draw->mesh_count);
        const tide_draw_mesh_data *mesh = &draw->meshes[run->mesh - 1u];
        const tide_draw_mesh_data *base = mesh->base ? &draw->meshes[mesh->base - 1u] : mesh;
        if (base->levels) {
            detailed_items += run->count;
            TIDE_CHECK(full ? mesh->level == 0 : mesh->level > 0);
            TIDE_CHECK(mesh->level <= base->levels);
            TIDE_CHECK(mesh->level == 0 || mesh->index_count < base->index_count);
        } else {
            small += run->count;
            batched |= run->count >= 2;
        }
    }
    TIDE_CHECK(small == 3 && detailed_items == 1 && batched);
}

// Compare the same generated draws prepared before/after actual pack unload.
// Slots and atlas identities are list-local; logical geometry, packets and
// captured GPU data must agree. Low-level skinning tests own palette math.
static void same_captures(const tide_draw_list *a, const tide_draw_list *b)
{
    TIDE_REQUIRE(a->run_count == b->run_count);
    for (uint32_t r = 0; r < a->run_count; r++) {
        const tide_draw_run *x = &a->runs[r], *y = &b->runs[r];
        TIDE_REQUIRE(x->shader && y->shader && x->shader->id == y->shader->id && x->count == y->count);
        if (!x->count) continue;
        const size_t packets = (size_t)x->count * tide_shader_instance_size(x->shader);
        const size_t uniforms = tide_shader_uniform_size(x->shader);
        TIDE_CHECK(memcmp(a->instances + x->first, b->instances + y->first, packets) == 0);
        TIDE_CHECK(memcmp(a->uniforms + x->uniforms, b->uniforms + y->uniforms, uniforms) == 0);
        TIDE_REQUIRE(x->mesh && x->mesh <= a->mesh_count && y->mesh && y->mesh <= b->mesh_count);
        const tide_draw_mesh_data *xm = &a->meshes[x->mesh - 1u], *ym = &b->meshes[y->mesh - 1u];
        const tide_draw_mesh_data *xb = xm->base ? &a->meshes[xm->base - 1u] : xm;
        const tide_draw_mesh_data *yb = ym->base ? &b->meshes[ym->base - 1u] : ym;
        TIDE_REQUIRE(xm->level == ym->level && xm->index_count == ym->index_count);
        TIDE_REQUIRE(xb->vertex_size == yb->vertex_size && xb->vertex_count == yb->vertex_count);
        TIDE_REQUIRE(xb->bytes && yb->bytes && xm->indices && ym->indices);
        TIDE_CHECK(memcmp(xb->bytes, yb->bytes, (size_t)xb->vertex_count * xb->vertex_size) == 0);
        TIDE_CHECK(memcmp(xm->indices, ym->indices, (size_t)xm->index_count * sizeof(uint32_t)) == 0);
        for (uint32_t binding = 0; binding < x->shader->texture_count; binding++) {
            const uint32_t xs = x->textures[binding], ys = y->textures[binding];
            TIDE_REQUIRE(xs && xs <= a->texture_count && ys && ys <= b->texture_count);
            const tide_draw_texture *xt = &a->textures[xs - 1u], *yt = &b->textures[ys - 1u];
            TIDE_REQUIRE(xt->format == TIDE_TEXELS_INTS && yt->format == TIDE_TEXELS_INTS);
            TIDE_REQUIRE(xt->width == yt->width && xt->height == yt->height && xt->pixels && yt->pixels);
            TIDE_CHECK(memcmp(xt->pixels, yt->pixels, (size_t)xt->width * (size_t)xt->height * 16u) == 0);
        }
    }
}

TIDE_TEST(skinned_asset_references_keep_identity_snapshots_and_current_pack_borrows)
{
    // A known declaration can exist before a pack is loaded. Value is empty,
    // while the stored identity remains available for equality/serialization.
    tide_world unloaded = {0};
    tide_world_init(&unloaded, 0.1f);
    tide_world_tick(&unloaded);
    TIDE_CHECK(unloaded.Probe.heldCount == 0 && unloaded.Probe.clips == 0 && unloaded.Probe.bones == 0);
    TIDE_CHECK(unloaded.Probe.equal && unloaded.Probe.differentNames);
    tide_world_free(&unloaded);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0}, copy = {0}, decoded = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
    checked(&world);
    TIDE_CHECK(world.Probe.heldCount == 6 && world.Probe.clips == 2 && world.Probe.bones > 0);
    const int64_t identity = world.Probe.held.id;
    const uint64_t hash = tide_world_hash(&world);
    tide_world_copy(&copy, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(decoded.Probe.held.id == identity && tide_world_hash(&decoded) == hash);
    tide_local_init(&local);
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, false);
    // Local Spawn is deferred until the current view finishes. The new
    // entity's asset reference is readable on the following frame.
    TIDE_CHECK(local.LocalProbe.count == 0 && tide_world_hash(&world) == hash);
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, false);
    TIDE_CHECK(local.LocalProbe.count == 6 && tide_world_hash(&world) == hash);
    local.LocalProbe.lod = 0;
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, true);

    const tide_skinned_lists before = tide_pack_skinned_of(tide_pack_find("first", TIDE_PACK_SKINNED), sizeof(tide_vertex3));
    uint64_t old_proof, new_proof;
    TIDE_REQUIRE(tide_pack_skinned_proof(before.data, &old_proof, NULL));
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_skinned_lists after = tide_pack_skinned_of(tide_pack_find("first", TIDE_PACK_SKINNED), sizeof(tide_vertex3));
    TIDE_REQUIRE(tide_pack_skinned_proof(after.data, &new_proof, NULL));
    TIDE_CHECK(old_proof != new_proof && world.Probe.held.id == identity);
    local.LocalProbe.lod = -1;
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, false);
    TIDE_CHECK(tide_world_hash(&world) == hash);

    // Existing skinned compatibility rejects a changed candidate before the
    // current globals/proofs are replaced. References retain that contract.
    unsigned char *changed = malloc(tide_game_pack_size);
    TIDE_REQUIRE(changed != NULL);
    memcpy(changed, tide_game_pack, tide_game_pack_size);
    tide_pack_header header;
    memcpy(&header, changed, sizeof header);
    const size_t vertex_at = (size_t)header.region_at
        + tide_handle_offset(after.vertices.at) - TIDE_PACK_BASE + sizeof(tide_block);
    TIDE_REQUIRE(vertex_at + sizeof(float) <= tide_game_pack_size);
    const float replacement = 123;
    memcpy(changed + vertex_at, &replacement, sizeof replacement);
    TIDE_CHECK(!tide_assets_load(changed, tide_game_pack_size));
    // Missing model entries retain the existing skinned compatibility rule:
    // reject the candidate atomically instead of replacing current globals.
    memcpy(changed, tide_game_pack, tide_game_pack_size);
    tide_pack_header *missing = (tide_pack_header *)(void *)changed;
    missing->entries = 0;
    TIDE_CHECK(!tide_assets_load(changed, tide_game_pack_size));
    free(changed);
    uint64_t retained_proof;
    TIDE_REQUIRE(tide_pack_skinned_proof(after.data, &retained_proof, NULL));
    TIDE_CHECK(retained_proof == new_proof);
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, false);

    const int64_t rest_identity = decoded.Probe.left.id;
    decoded.Probe.held.id = rest_identity;
    decoded.Probe.left.id = decoded.Probe.right.id = INT64_MAX;
    tide_world_tick(&decoded);
    TIDE_CHECK(decoded.Probe.heldCount == 6 && decoded.Probe.clips == 0);
    TIDE_CHECK(decoded.Probe.receivedEqual); // Unknown IDs compare before resolution.

    decoded.Probe.held.id = INT64_MAX;
    tide_world_tick(&decoded);
    checked(&decoded);
    TIDE_CHECK(decoded.Probe.heldCount == 0 && decoded.Probe.clips == 0 && decoded.Probe.bones == 0);
    copy.Probe.held.id = identity;
    tide_world_tick(&copy);
    checked(&copy);
    TIDE_CHECK(copy.Probe.heldCount == 6);

    // Both lists capture the same .Value draws while the pack exists. Only
    // the first prepares its palettes before unload; the second must finish
    // from captured dependencies after the pack region has been released.
    record(&world, &local, &draw, &gui);
    tide_draw_list pending = {0};
    capture(&world, &local, &pending, &gui);
    TIDE_REQUIRE(pending.skinned != NULL);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_pack_unload();
    TIDE_CHECK(tide_pack_find("first", TIDE_PACK_SKINNED) == NULL);
    uint64_t absent_proof = UINT64_MAX;
    TIDE_CHECK(!tide_pack_skinned_proof(after.data, &absent_proof, NULL) && absent_proof == UINT64_MAX);
    TIDE_CHECK(world.Probe.held.id == identity && tide_world_hash(&world) == hash);
    tide_draw_prepare(&pending, NULL);
    borrowed_paths(&pending, false);
    same_captures(&draw, &pending);
    tide_draw_free(&pending);

    // Asset Value retains the named global's borrowed lifetime, just like
    // static Asset. Do not assert indices.count == 0 or dereference stale
    // typed lists. Checked catalog/draw operations reject the absent model.
    local.LocalProbe.borrowLifetime = true;
    record(&world, &local, &draw, &gui);
    TIDE_CHECK(local.LocalProbe.sameIdentity && local.LocalProbe.distinctName);
    TIDE_CHECK(local.LocalProbe.resolvedClips == 0 && local.LocalProbe.namedClips == 0);
    TIDE_CHECK(local.LocalProbe.resolvedBones == 0 && local.LocalProbe.namedBones == 0);
    TIDE_CHECK(draw.run_count == 0 && world.Probe.held.id == identity && tide_world_hash(&world) == hash);

    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_skinned_lists reloaded = tide_pack_skinned_of(tide_pack_find("first", TIDE_PACK_SKINNED), sizeof(tide_vertex3));
    uint64_t reloaded_proof;
    TIDE_REQUIRE(tide_pack_skinned_proof(reloaded.data, &reloaded_proof, NULL));
    TIDE_CHECK(reloaded_proof != new_proof && world.Probe.held.id == identity);
    record(&world, &local, &draw, &gui);
    TIDE_CHECK(local.LocalProbe.sameIdentity && local.LocalProbe.distinctName);
    TIDE_CHECK(local.LocalProbe.resolvedClips == 2 && local.LocalProbe.namedClips == 2);
    TIDE_CHECK(local.LocalProbe.resolvedBones > 0 && local.LocalProbe.resolvedBones == local.LocalProbe.namedBones);
    uint32_t restored = 0;
    for (uint32_t r = 0; r < draw.run_count; r++) restored += draw.runs[r].count;
    TIDE_CHECK(restored == 2 && tide_world_hash(&world) == hash);
    local.LocalProbe.borrowLifetime = false;
    record(&world, &local, &draw, &gui);
    borrowed_paths(&draw, false);
    TIDE_CHECK(local.LocalProbe.count == 6 && tide_world_hash(&world) == hash);

    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&copy);
    tide_world_free(&decoded);
    tide_pack_unload();
}
