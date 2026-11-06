#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
#include "draw_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void check_values(const Decoded *d)
{
    TIDE_CHECK(d->quadSame && d->stripSame && d->boxSame && d->legsSame);
    TIDE_CHECK(d->extraKept && d->aliasSame && d->sameKey);
    TIDE_CHECK(d->refused && d->emptyRefused && d->unwrapped);
    TIDE_CHECK(d->quad.id < 0 && d->box.id < 0 && d->legs.id < 0); // Made by the game: what's in it
    TIDE_CHECK(d->listCorners > 0);
}

TIDE_TEST(decoded_meshes_are_their_assets_vertices_in_the_match)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, .1f);
    tide_world_tick(&world);
    check_values(&world.Decoded);

    tide_world snapshot = {0}, restored = {0};
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&world);
    // The decoded vertices are match state: the same bits natively and on the web
    if (hash != UINT64_C(0xbe9ca227b3c95593)) printf("mesh_decode world hash %016llx\n", (unsigned long long)hash);
    TIDE_CHECK(hash == UINT64_C(0xbe9ca227b3c95593));
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
    check_values(&world.Decoded);
    check_values(&restored.Decoded);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&restored));
    tide_world_free(&snapshot);
    tide_world_free(&restored);
}

TIDE_TEST(decoded_meshes_draw_as_one_mesh_by_their_key)
{
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_test_flatten(&draw);
    // The local decode and the match's are one mesh: one run of two instances
    TIDE_REQUIRE(draw.count == 1);
    TIDE_CHECK(draw.commands[0].kind == TIDE_DRAW_MESH_3D && draw.commands[0].mesh3.count == 2);
    TIDE_CHECK(draw.mesh_count == 1);
    tide_world_free(&world);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_pack_unload();
}
