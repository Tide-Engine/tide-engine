#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(blend_shapes_bake_in_the_match_survive_snapshots_and_draw)
{
    tide_world world = {0}, snapshot = {0};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    const ShapeProbe *p = &world.ShapeProbe;
    if (p->firstFailure) printf("    check %d failed\n", p->firstFailure);
    TIDE_REQUIRE(p->checks == 22 && p->passed == p->checks);
    // The stored model's vertices are its own; its model bytes and indices the asset's.
    TIDE_CHECK(tide_list_read_count(&world.heap, p->shaped.vertices) == 3);
    const Corner *moved = tide_list_read(&world.heap, p->shaped.vertices, 1, sizeof *moved);
    TIDE_REQUIRE(moved);
    TIDE_CHECK(moved->position.y == -0.5f && moved->tag == 7.0f);
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&snapshot);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    TIDE_CHECK(p->passed == p->checks && p->checks == 25);
    moved = tide_list_read(&world.heap, p->shaped.vertices, 1, sizeof *moved);
    TIDE_REQUIRE(moved);
    TIDE_CHECK(moved->position.y == -2.0f);
    // The snapshot kept its own vertices, and ticks to the same world.
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    for (int i = 0; i < 3; i++) tide_world_tick(&snapshot);
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&snapshot, bytes, size));
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&world));
    free(bytes);
    // Both draws record a skinned run of their own vertices and the model's palette.
    tide_local local = {0};
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(draw.mesh_count == 2 && draw.texture_count >= 2);
    // What each draw recorded is its shaped vertices: the stored model's jaw at 2, and [1, 1].
    bool stored = false, inline_shaped = false;
    for (uint32_t i = 0; i < draw.mesh_count; i++) {
        const tide_draw_mesh_data *mesh = &draw.meshes[i];
        if (mesh->vertex_size != sizeof(Corner) || mesh->vertex_count != 3) continue;
        const Corner *corners = mesh->bytes;
        stored |= corners[1].position.y == -2.0f && corners[0].position.y == 0.125f;
        inline_shaped |= corners[1].position.y == -1.0f && corners[0].position.y == 0.5f && corners[2].tag == 7.0f;
    }
    TIDE_CHECK(stored && inline_shaped);
    tide_draw_free(&draw);
    tide_local_free(&local);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&snapshot);
    tide_world_free(&world);
    tide_pack_unload();
}
