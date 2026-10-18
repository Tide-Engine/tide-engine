#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static bool text_is(const tide_world *w, const tide_list words, const char *expected)
{
    const tide_text *word = tide_list_read(&w->heap, words, 0, sizeof *word);
    const tide_str s = word ? tide_text_read(&w->heap, *word) : TIDE_STR_EMPTY;
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, (size_t)s.bytes) == 0;
}

TIDE_TEST(component_values_capture_owned_payloads_and_preserve_order)
{
    tide_world w = {0}, copy = {0}, restored = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    TIDE_CHECK(w.Checks.order == 12 && w.Checks.commandTotal == 17 && w.Checks.eventTotal == 2);
    const Item made = tide_get_Item(&w, w.Checks.made);
    const Item added = tide_get_Item(&w, w.Checks.added);
    const Item loaded = tide_get_Item(&w, w.Checks.loaded);
    TIDE_CHECK(made.number == 42 && text_is(&w, made.words, "original"));
    TIDE_CHECK(added.number == 99 && text_is(&w, added.words, "changed"));
    TIDE_CHECK(loaded.number == 99 && text_is(&w, loaded.words, "changed"));
    TIDE_CHECK(tide_get_Item(&w, w.Checks.prefabbed).number == 99
               && text_is(&w, tide_get_Item(&w, w.Checks.prefabbed).words, "changed"));
    TIDE_CHECK(tide_get_Item(&w, w.Checks.prefabLoaded).number == 99
               && text_is(&w, tide_get_Item(&w, w.Checks.prefabLoaded).words, "changed"));
    TIDE_CHECK(tide_get_Extra(&w, w.Checks.made).number == 13);
    TIDE_CHECK(tide_get_Item(&w, w.Checks.ordered).number == 1);
    TIDE_CHECK(tide_get_Other(&w, w.Checks.ordered).number == 12);
    tide_world_copy(&copy, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    for (int i = 0; i < 3; i++) {
        tide_world_tick(&w); tide_world_tick(&copy); tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&copy));
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&restored));
    }
    tide_world_free(&restored); tide_world_free(&copy); tide_world_free(&w);
}

TIDE_TEST(component_values_spawn_local_entities_from_values)
{
    tide_world w = {0}; tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const tide_local_component_Picture value = tide_get_tide_local_component_Picture(&local, local.Images.made);
    TIDE_CHECK(value.number == 5 && tide_list_count(value.words) == 1);
    TIDE_CHECK(local.Images.notices == 1 && local.Images.seen == 7 && local.Images.tags == 3);
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&w);
}
