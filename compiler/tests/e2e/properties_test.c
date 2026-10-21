#include <stdlib.h>
#include "game.h"
#include "tide_test.h"

static int property_order;
int PropertyIndex(void) { property_order = property_order % 100000 * 10 + 1; return 0; }
void PropertyGet(void) { property_order = property_order % 100000 * 10 + 2; }
int PropertyRhs(void) { property_order = property_order % 100000 * 10 + 3; return 5; }
void PropertySet(int value) { property_order = property_order % 100000 * 10 + 4; (void)value; }

TIDE_TEST(properties_get_set_compound_and_steps)
{
    static tide_world world;
    property_order = 0;
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.before == 8);
    TIDE_CHECK(world.Results.after == 9);
    TIDE_CHECK(world.Results.sum == 17);
    TIDE_CHECK(world.Results.identity.index == 2);
    TIDE_CHECK(world.Results.position.x == 14 && world.Results.position.y == 9 && world.Results.position.z == 16);
    TIDE_CHECK(world.Results.queried.x == 14 && world.Results.queried.y == 9 && world.Results.queried.z == 16);
    TIDE_CHECK(world.Results.direct.x == 6 && tide_abs_f(world.Results.direct.y - 8) < 0.00001f && world.Results.direct.z == 10);
    TIDE_CHECK(world.Results.current.x == 12 && world.Results.current.y == 6 && world.Results.current.z == 12);
    TIDE_CHECK(world.Results.ordered == 18 && property_order == 1234);
    TIDE_CHECK(world.Results.heap == 19);
    tide_world_free(&world);
}

TIDE_TEST(properties_keep_verified_query_and_local_worlds)
{
    static tide_world predicted, verified;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&predicted, 1.0f);
    tide_world_init(&verified, 1.0f);
    tide_local_init(&local);
    Transform root = tide_get_Transform(&verified, verified.Results.root);
    root.localPosition.x = 100;
    tide_set_Transform(&verified, verified.Results.root, root);
    tide_world_tick(&predicted);
    const uint64_t a = tide_world_hash(&predicted), b = tide_world_hash(&verified);
    tide_frame(&predicted, NULL, 1, &verified, NULL, 1, &local, &draw, &gui);
    tide_draw_reset(&draw);
    tide_frame(&predicted, NULL, 1, &verified, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LocalResults.position.x == 30 && local.LocalResults.position.y == 2 && local.LocalResults.position.z == 3);
    TIDE_CHECK(local.LocalResults.verified.x == 104 && local.LocalResults.verified.y == 9 && local.LocalResults.verified.z == 16);
    TIDE_CHECK(local.LocalResults.query.x == 104 && local.LocalResults.query.y == 9 && local.LocalResults.query.z == 16);
    TIDE_CHECK(tide_world_hash(&predicted) == a && tide_world_hash(&verified) == b);
    tide_world_free(&predicted);
    tide_world_free(&verified);
    tide_local_free(&local);
    tide_draw_free(&draw);
}

TIDE_TEST(properties_singular_parent_replay_and_packing)
{
    static tide_world world, snapshot, decoded;
    tide_world_init(&world, 1);
    Transform root = tide_get_Transform(&world, world.Results.root);
    root.localScale = tide_f3_splat(0);
    tide_set_Transform(&world, world.Results.root, root);
    tide_world_copy(&snapshot, &world);
    tide_world_tick(&world);
    tide_world_tick(&snapshot);
    TIDE_CHECK(world.Results.writtenLocal.x == 2 && world.Results.writtenLocal.y == 3 && world.Results.writtenLocal.z == 4);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&snapshot));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&decoded));
    tide_world_tick(&world);
    tide_world_tick(&decoded);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&decoded));
    free(bytes);
    tide_world_free(&world);
    tide_world_free(&snapshot);
    tide_world_free(&decoded);
}
