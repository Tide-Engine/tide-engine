#include <stdlib.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(texture3d_pack_resolves_shared_resized_dimensions)
{
    tide_world world;
    tide_world_init(&world, 1.0f);
    tide_local local = {0};
    tide_local_init(&local);
    const uint64_t kind = tide_resident_kind_of("volume-reload-test");
    tide_arena *arena = tide_resident_arena(kind);
    TIDE_REQUIRE(arena != NULL);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, &world, 1.0f, &world, &world, 1.0f, &local, &draw, &gui);
    const tide_texture3d_ref before = local.Volumes.volume;
    TIDE_CHECK(local.Volumes.beforeResize == 5);
    TIDE_CHECK(before.depth == 2u); // Resize changes the shared state, not every reference's cached metadata.
    const uint32_t size = tide_local_pack(&local, NULL, 0u);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(local.Volumes.volume.depth == 2u && local.Volumes.alias.depth == 2u);
    TIDE_CHECK(tide_resident_arena(kind) == arena);
    tide_local next = {0};
    TIDE_REQUIRE(tide_local_unpack(&next, bytes, size));
    free(bytes);
    TIDE_CHECK(next.Volumes.volume.depth == 5u && next.Volumes.alias.depth == 5u);
    TIDE_CHECK(next.Volumes.holder.volume.depth == 5u);
    tide_text_use(NULL, &next.heap);
    const Holder *item = tide_list_at(next.Volumes.items, 0, sizeof(Holder));
    TIDE_REQUIRE(item != NULL);
    TIDE_CHECK(item->volume.depth == 5u);
    tide_local_rebind_resources(&next, false);
    TIDE_CHECK(next.Volumes.volume.target.id != before.target.id);
    TIDE_CHECK(next.Volumes.volume.depth == 5u && next.Volumes.alias.depth == 5u);
    const tide_int3 extent = tide_texture3d_size(next.Volumes.volume);
    TIDE_CHECK(extent.x == 3 && extent.y == 4 && extent.z == 5);
    tide_local_free(&next);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_render_targets_free();
}
