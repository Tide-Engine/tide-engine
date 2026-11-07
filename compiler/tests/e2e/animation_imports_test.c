#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(animation_imports_select_clean_names_and_own_combined_catalogs)
{
    tide_world world = {0}, copy = {0};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.checks == 21 && world.Probe.passed == world.Probe.checks);
    tide_world_copy(&copy, &world);
    const uint64_t before = tide_world_hash(&copy);
    tide_pack_unload();
    for (int i = 0; i < 8; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Probe.checks == world.Probe.passed && tide_world_hash(&copy) == before);
    for (int i = 0; i < 8; i++) tide_world_tick(&copy);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&copy);
    tide_world_free(&world);
}

TIDE_TEST(animation_imports_authenticate_every_source_and_keep_the_old_pack)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_pack_entry *entry = tide_pack_find("movement", TIDE_PACK_LIST);
    TIDE_REQUIRE(entry != NULL);
    const tide_list *sources = tide_list_at((tide_list){tide_pack_handle(entry)}, 1, sizeof(tide_list));
    TIDE_REQUIRE(sources != NULL);
    const uint8_t *source = tide_list_at(*sources, 0, 1u);
    TIDE_REQUIRE(source != NULL);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(source, (size_t)tide_list_count(*sources), &view));
    const tide_pack_header *header = (const tide_pack_header *)(const void *)tide_game_pack;
    const size_t at = header->region_at + tide_handle_offset(sources->at) - TIDE_PACK_BASE + sizeof(tide_block);
    uint8_t *changed = malloc(tide_game_pack_size);
    TIDE_REQUIRE(changed != NULL);
    memcpy(changed, tide_game_pack, tide_game_pack_size);
    const float duration = 2;
    memcpy(changed + at + view.clip_at + 16u, &duration, sizeof duration);
    TIDE_CHECK(!tide_assets_load(changed, tide_game_pack_size));
    TIDE_CHECK(tide_pack_find("movement", TIDE_PACK_LIST) == entry);
    free(changed);
    tide_pack_unload();
}
