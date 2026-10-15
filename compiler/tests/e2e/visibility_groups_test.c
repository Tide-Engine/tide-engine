#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

static bool sees(const tide_world *w, tide_entity e, int player)
{
    return tide_world_visible(w, e, tide_player_from_index(player));
}

static bool sees_secret(const tide_world *w, tide_entity e, int player)
{
    return tide_world_component_visible(w, e, TIDE_COMPONENT_ID_Secret, tide_player_from_index(player));
}

TIDE_TEST(visibility_groups_share_audience_and_restrict_components)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    const tide_entity object = w.Control.object, card = w.Control.card;
    TIDE_CHECK(sees(&w, object, 0));
    TIDE_CHECK(!sees(&w, object, 32));
    TIDE_CHECK(sees(&w, object, 33));
    TIDE_CHECK(sees(&w, object, 69));
    TIDE_CHECK(!sees(&w, object, 70));
    TIDE_CHECK(sees(&w, card, 1));
    TIDE_CHECK(sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    TIDE_CHECK(tide_has_Secret(&w, card));
    const uint64_t hash = tide_world_hash(&w);
    TIDE_CHECK(sees_secret(&w, object, 69));
    TIDE_CHECK(tide_world_hash(&w) == hash);
    w.Control.action = 1;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, object, 0));
    TIDE_CHECK(sees(&w, object, 1));
    w.Control.action = 2;
    tide_world_tick(&w);
    TIDE_CHECK(!sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    w.Control.action = 3;
    tide_world_tick(&w);
    TIDE_CHECK(sees_secret(&w, card, 0));
    TIDE_CHECK(sees_secret(&w, card, 1));
    w.Control.action = 4;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    tide_world_free(&w);
}

TIDE_TEST(visibility_scene_defaults_and_group_state_survive_snapshots_and_packing)
{
    tide_world w = {0}, saved = {0}, restored = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    TIDE_CHECK(sees(&w, w.Control.handCard, 0));
    TIDE_CHECK(!sees(&w, w.Control.handCard, 1));
    TIDE_CHECK(!sees_secret(&w, w.Control.handCard, 1));
    tide_world_copy(&saved, &w);
    const uint64_t hash = tide_world_hash(&w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(bytes != NULL);
    TIDE_CHECK(tide_world_pack(&w, bytes, size) == size);
    TIDE_CHECK(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    TIDE_CHECK(sees_secret(&restored, restored.Control.card, 0));
    TIDE_CHECK(!sees_secret(&restored, restored.Control.card, 1));
    w.Control.action = 1;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, w.Control.object, 0));
    TIDE_CHECK(sees(&saved, saved.Control.object, 0));
    TIDE_CHECK(tide_world_hash(&saved) == hash);
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&saved);
    tide_world_free(&w);
}
