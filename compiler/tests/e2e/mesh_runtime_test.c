#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
#include "draw_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
_Static_assert(sizeof(((Shapes *)0)->made) == 24, "a Mesh<V> is its id and two lists");
_Static_assert(sizeof(((Shapes *)0)->reference) == 8, "an Asset<Mesh<V>> is its id alone");

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_test_flatten(&draw);
}

static void check_values(const Shapes *s)
{
    TIDE_CHECK(s->made.id < 0 && s->same.id == s->made.id); // What's in it, the same on every machine
    TIDE_CHECK(s->asset.id > 0 && s->reference.id == s->asset.id);
    TIDE_CHECK(s->madeCount == 6 && s->sourceCount == 4);   // Its own copy, made before the list grew
    TIDE_CHECK(s->mixedCount == 9 + 3 + 3 + 9 && s->referenceCount == 9);
    TIDE_CHECK(s->equal && s->differ && !s->emptyIsDefault);
}

TIDE_TEST(made_meshes_are_values_in_the_match)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, .1f);
    tide_world_tick(&world);
    check_values(&world.Shapes);

    // Snapshots, hashes and packing carry the made geometry as ordinary lists
    tide_world snapshot = {0}, restored = {0};
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&world);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    tide_world_tick(&world);
    tide_world_tick(&restored);
    check_values(&world.Shapes);
    check_values(&restored.Shapes);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&restored));
    tide_world_free(&snapshot);
    tide_world_free(&restored);
}

TIDE_TEST(made_meshes_draw_as_one_mesh_by_their_key)
{
    tide_local_init(&local);
    frame();
    // The local copy, the match's list's, and one made in the view: one mesh, so one run of three instances. Then
    // the map's other triangle, the box, and the first mesh again with levels asked for: another mesh
    TIDE_REQUIRE(draw.count == 4);
    for (uint32_t i = 0; i < 4; i++) TIDE_CHECK(draw.commands[i].kind == TIDE_DRAW_MESH_3D);
    const uint32_t made = draw.commands[0].mesh3.mesh;
    TIDE_CHECK(made && draw.commands[0].mesh3.count == 3);
    TIDE_CHECK(draw.commands[1].mesh3.mesh != made && draw.commands[2].mesh3.mesh != made);
    TIDE_CHECK(draw.commands[1].mesh3.mesh != draw.commands[2].mesh3.mesh);
    const uint32_t leveled = draw.commands[3].mesh3.mesh;
    TIDE_CHECK(leveled && leveled != made && draw.mesh_count == 4);
    const tide_draw_mesh_data *m = &draw.meshes[made - 1u];
    TIDE_CHECK(m->vertex_count == 3 && m->index_count == 3 && m->vertices[1].position.x == 1.0f);
    TIDE_CHECK(draw.mesh_key == 0); // Told no more after each draw

    // Kept from frame to frame with no list read: a mesh's key is its version
    frame();
    TIDE_REQUIRE(draw.count == 4);
    TIDE_CHECK(draw.commands[0].mesh3.mesh == made && draw.commands[0].mesh3.count == 3);
    TIDE_CHECK(draw.commands[3].mesh3.mesh == leveled && draw.mesh_count == 4);
    tide_world_free(&world);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_pack_unload();
}

TIDE_TEST(a_key_is_only_for_its_lists)
{
    // Told of other lists than it draws, a draw reads its own, and the key is gone either way
    tide_draw_list d = {0};
    tide_draw_reset(&d);
    const tide_vertex3 corners[3] = {{.position = {0, 0, 0}}, {.position = {1, 0, 0}}, {.position = {0, 1, 0}}};
    const uint32_t triangle[3] = {0, 1, 2};
    const tide_list v = tide_list_from(corners, 3, sizeof corners[0]);
    const tide_list i = tide_list_from(triangle, 3, sizeof triangle[0]);
    const uint64_t key = tide_mesh_key(v, sizeof corners[0], i);
    TIDE_CHECK(key >> 63 == 1);
    tide_draw_mesh_key(&d, (tide_mesh_lists){i, v}, key);
    tide_draw_mesh3_lists(&d, v, i, (tide_float4x4){{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}});
    TIDE_CHECK(d.mesh_key == 0 && d.mesh_count == 1);
    tide_draw_mesh_key(&d, (tide_mesh_lists){v, i}, key);
    tide_draw_mesh3_lists(&d, v, i, (tide_float4x4){{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}});
    TIDE_CHECK(d.mesh_key == 0 && d.mesh_count == 2); // Keyed: another mesh than the same lists by their bytes
    tide_draw_mesh_key(&d, (tide_mesh_lists){v, i}, 12345); // No key: its top bit is clear
    TIDE_CHECK(d.mesh_key == 0);
    tide_draw_free(&d);
}
