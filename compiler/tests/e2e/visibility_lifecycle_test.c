#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_draw_list draw;
static tide_gui gui;

static tide_entity marker(const tide_world *w)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Marker(w, e)) return e;
    }
    return (tide_entity){0};
}

static void frame(tide_local *local, const tide_world *w, tide_event_log *events)
{
    tide_local_events(local, events);
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, local, &draw, &gui);
}

TIDE_TEST(visibility_lifecycle_component_transitions)
{
    tide_world full = {0}, partial = {0};
    tide_local local = {0};
    tide_event_log events = {0};
    tide_world_init(&full, 1.0f / 60.0f);
    tide_local_init(&local);
    const tide_entity e = marker(&full);
    TIDE_REQUIRE(tide_game_api.hear_arrived && tide_game_api.hear_visibility);
    tide_game_api.hear_arrived(&full, NULL, &events);
    frame(&local, &full, &events);
    TIDE_CHECK(local.Observed.shown == 1 && local.Observed.detailsShown == 1);
    TIDE_CHECK(local.Observed.verifiedShown == 7 && local.Observed.spawned == 0);
    TIDE_CHECK(tide_get_Marker_local(&local, e).seen == 1);
    tide_game_api.hear_arrived(&full, &full, &events);
    TIDE_CHECK(events.count == 0);

    tide_world_copy(&partial, &full);
    partial.Actions.next = 1;
    tide_world_tick(&partial);
    TIDE_REQUIRE(!tide_has_Details(&partial, e));
    tide_game_api.hear_visibility(&partial, &full, NULL, 0, &events);
    frame(&local, &partial, &events);
    TIDE_CHECK(local.Observed.detailsHidden == 1 && local.Observed.hidden == 0);
    TIDE_CHECK(local.Observed.lastSecret == 84 && local.Observed.hiddenRows == 77);
    TIDE_CHECK(local.Observed.withoutShown == 1 && local.Observed.shownRows == 0);
    TIDE_CHECK(local.Observed.shown == 1 && local.Observed.forgotten == 0);

    tide_game_api.hear_visibility(&full, &partial, NULL, 0, &events);
    frame(&local, &full, &events);
    TIDE_CHECK(local.Observed.detailsShown == 2 && local.Observed.withoutHidden == 1);
    TIDE_CHECK(local.Observed.shown == 1 && tide_get_Marker_local(&local, e).seen == 1);
    tide_game_api.hear_visibility(&full, &full, NULL, 0, &events);
    TIDE_CHECK(events.count == 0);

    tide_game_api.hear_visibility(NULL, &full, NULL, 0, &events);
    frame(&local, NULL, &events);
    TIDE_CHECK(local.Observed.hidden == 7 && local.Observed.detailsHidden == 2);
    TIDE_CHECK(local.Observed.destroyed == 0 && local.Observed.forgotten == 1);
    tide_game_api.hear_arrived(&full, NULL, &events);
    frame(&local, &full, &events);
    TIDE_CHECK(local.Observed.shown == 2 && tide_get_Marker_local(&local, e).seen == 1);

    tide_world_free(&partial);
    tide_world_copy(&partial, &full);
    partial.Actions.next = 3;
    tide_world_tick(&partial);
    tide_game_api.hear_visibility(&partial, &full, &e, 1, &events);
    TIDE_CHECK(events.count == 0); // Genuine destruction is never fabricated as Hidden or Destroyed.
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&partial);
    tide_world_free(&full);
}

TIDE_TEST(visibility_lifecycle_structural_changes)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_event_log events = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_game_api.hear_arrived(&world, NULL, &events);
    frame(&local, &world, &events);
    world.Actions.next = 1;
    tide_world_tick_events(&world, NULL, &events);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.detailsHidden == 1 && local.Observed.withoutShown == 1);
    TIDE_CHECK(local.Observed.hiddenRows == 77); // Last delivered permitted query snapshot.
    world.Actions.next = 2;
    tide_world_tick_events(&world, NULL, &events);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.detailsShown == 2 && local.Observed.withoutHidden == 1);
    TIDE_CHECK(local.Observed.lastSecret == 106 && local.Observed.shown == 1);
    tide_world_tick_events(&world, NULL, &events); // Replacing an existing component is no availability change.
    TIDE_CHECK(events.count == 0);
    world.Actions.next = 4;
    tide_world_tick_events(&world, NULL, &events);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.spawned == 8 && local.Observed.shown == 2);
    TIDE_CHECK(local.Observed.detailsShown == 3 && local.Observed.verifiedShown == 15);
    world.Actions.next = 3;
    tide_world_tick_events(&world, NULL, &events);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.destroyed == 15 && local.Observed.hidden == 0);
    TIDE_CHECK(local.Observed.detailsHidden == 1 && local.Observed.forgotten == 2);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(visibility_lifecycle_replay_discovers_new_listener)
{
    tide_world full = {0}, partial = {0};
    tide_local local = {0};
    tide_event_log events = {.taken = true};
    tide_hearing hearing = {0};
    tide_world_init(&full, 1.0f / 60.0f);
    tide_world_copy(&partial, &full);
    partial.Actions.next = 1;
    tide_world_tick(&partial);
    tide_local_init(&local);
    tide_hearing_init(&hearing, 4, &events);
    tide_game_api.hear_arrived(&partial, NULL, tide_hearing_begin(&hearing, 17));
    tide_hearing_end(&hearing, 17);
    frame(&local, &partial, &events);
    TIDE_CHECK(local.Observed.shown == 1 && local.Observed.detailsShown == 0);

    // Same tick/entity/event, but correction makes another listener eligible.
    // Marker presentation stays initialized once; Details initializes late.
    tide_game_api.hear_arrived(&full, NULL, tide_hearing_begin(&hearing, 17));
    tide_hearing_end(&hearing, 17);
    frame(&local, &full, &events);
    TIDE_CHECK(local.Observed.shown == 1 && local.Observed.detailsShown == 1);
    tide_game_api.hear_arrived(&full, NULL, tide_hearing_begin(&hearing, 17));
    tide_hearing_end(&hearing, 17);
    TIDE_CHECK(events.count == 0);
    tide_hearing_confirm(&hearing, 17);
    frame(&local, &full, &events);
    TIDE_CHECK(local.Observed.verifiedShown == 7 && local.Observed.shown == 1);
    tide_hearing_free(&hearing);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&partial);
    tide_world_free(&full);
}

TIDE_TEST(visibility_lifecycle_same_tick_toggles_preserve_final_resources)
{
    tide_world base = {0}, world = {0};
    tide_local local = {0};
    tide_event_log events = {.taken = true};
    tide_hearing hearing = {0};
    tide_world_init(&base, 1.0f / 60.0f);
    base.Actions.next = 1;
    tide_world_tick(&base); // Details is initially unavailable.
    tide_local_init(&local);
    frame(&local, &base, &events);
    tide_hearing_init(&hearing, 4, &events);
    for (int replay = 0; replay < 2; replay++) {
        tide_world_copy(&world, &base);
        world.Actions.next = 10; // Add, Remove, Add inside one tick.
        tide_world_tick_events(&world, NULL, tide_hearing_begin(&hearing, 17));
        tide_hearing_end(&hearing, 17);
        frame(&local, &world, &events);
        TIDE_CHECK(tide_has_Details(&world, marker(&world)) && local.Observed.active == 1);
        TIDE_CHECK(local.Observed.detailsShown == 1 && local.Observed.detailsHidden == 0);
    }
    tide_hearing_free(&hearing);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&base);
}

TIDE_TEST(visibility_lifecycle_corrected_conceal_restores_resources)
{
    tide_world base = {0}, world = {0};
    tide_local local = {0};
    tide_event_log events = {.taken = true};
    tide_hearing hearing = {0};
    tide_world_init(&base, 1.0f / 60.0f);
    base.Actions.next = 1;
    tide_world_tick(&base);
    tide_local_init(&local);
    frame(&local, &base, &events);
    tide_hearing_init(&hearing, 4, &events);
    tide_world_copy(&world, &base);
    world.Actions.next = 12;
    tide_world_tick_events(&world, NULL, tide_hearing_begin(&hearing, 16));
    tide_hearing_end(&hearing, 16);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.active == 1);
    // A predicted removal was shown, then disappears from this tick's replay.
    tide_world_copy(&base, &world);
    world.Actions.next = 1;
    tide_world_tick_events(&world, NULL, tide_hearing_begin(&hearing, 17));
    tide_hearing_end(&hearing, 17);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.active == 0);
    tide_world_copy(&world, &base);
    world.Actions.next = 0;
    tide_world_tick_events(&world, NULL, tide_hearing_begin(&hearing, 17));
    tide_hearing_end(&hearing, 17);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.active == 1 && local.Observed.detailsShown == 2);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.active == 1 && local.Observed.detailsShown == 2);
    tide_hearing_free(&hearing);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&base);
}

TIDE_TEST(visibility_lifecycle_verified_endpoints_keep_their_query_snapshots)
{
    tide_world full = {0}, partial = {0}, revealed = {0};
    tide_local local = {0};
    tide_event_log events = {0};
    tide_world_init(&full, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_frame(&full, NULL, 1, &full, NULL, 1, &local, &draw, &gui);
    tide_world_copy(&partial, &full);
    partial.Actions.next = 1;
    tide_world_tick(&partial);
    tide_frame(&partial, NULL, 1, &full, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.active == 0 && local.Observed.verifiedActive == 1);
    TIDE_CHECK(local.Observed.hiddenRows == 77 && local.Observed.verifiedHiddenRows == 0);
    tide_frame(&partial, NULL, 1, &partial, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.verifiedActive == 0 && local.Observed.verifiedHiddenRows == 77);
    tide_world_copy(&revealed, &partial);
    revealed.Actions.next = 2;
    tide_world_tick(&revealed);
    tide_frame(&revealed, NULL, 1, &partial, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.active == 1 && local.Observed.verifiedActive == 0);
    TIDE_CHECK(local.Observed.verifiedShownRows == 77);
    tide_frame(&revealed, NULL, 1, &revealed, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.verifiedActive == 1 && local.Observed.verifiedShownRows == 176);
    tide_frame(&revealed, NULL, 1, &revealed, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.verifiedShownRows == 176 && local.Observed.verifiedHiddenRows == 77);
    // A known genuine death must remain known while verified presentation lags.
    revealed.Actions.next = 3;
    tide_world_tick_events(&revealed, NULL, &events);
    tide_local_events(&local, &events);
    tide_frame(&revealed, NULL, 1, &full, NULL, 1, &local, &draw, &gui);
    const int hidden = local.Observed.verifiedHiddenRows;
    tide_frame(&revealed, NULL, 1, &revealed, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.verifiedHiddenRows == hidden);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&revealed);
    tide_world_free(&partial);
    tide_world_free(&full);
}

TIDE_TEST(visibility_lifecycle_predicted_death_without_matching_destroyed_listener)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_event_log events = {.taken = true};
    tide_hearing hearing = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    frame(&local, &world, &events);
    tide_hearing_init(&hearing, 4, &events);
    world.Actions.next = 13; // Ghost matches no local Destroyed listener.
    const uint32_t tick = (uint32_t)world.Time.tick;
    tide_world_tick_events(&world, NULL, tide_hearing_begin(&hearing, tick));
    tide_hearing_end(&hearing, tick);
    frame(&local, &world, &events);
    TIDE_CHECK(local.Observed.ghostHidden == 0 && local.Observed.destroyed == 0);
    tide_hearing_free(&hearing);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(visibility_lifecycle_local_pack_excludes_endpoints_and_rebuild_prepares_again)
{
    tide_world world = {0};
    tide_local local = {0}, restored = {0};
    tide_event_log events = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    frame(&local, &world, &events);
    TIDE_CHECK(local.tide_available_predicted && local.tide_available_verified);
    const uint64_t hash = tide_world_hash(&world);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    const tide_local *header = (const tide_local *)bytes;
    TIDE_CHECK(!header->tide_available_predicted && !header->tide_available_verified
               && !header->tide_available_causes.bytes);
    TIDE_CHECK(tide_local_unpack(&restored, bytes, size));
    TIDE_CHECK(!restored.tide_available_predicted && !restored.tide_available_verified);
    frame(&restored, &world, &events);
    TIDE_CHECK(restored.Observed.shown == 2 && restored.Observed.detailsShown == 2 && restored.Observed.active == 1);
    frame(&restored, &world, &events);
    TIDE_CHECK(restored.Observed.shown == 2 && restored.Observed.detailsShown == 2);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    free(bytes);
    tide_event_log_free(&events);
    tide_local_free(&restored);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(visibility_lifecycle_cancelled_predicted_cause_does_not_suppress_later_hide)
{
    tide_world base = {0}, alive = {0}, hidden = {0};
    tide_local local = {0};
    tide_event_log events = {.taken = true};
    tide_hearing hearing = {0};
    tide_world_init(&base, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_frame(&base, NULL, 1, &base, NULL, 1, &local, &draw, &gui);
    tide_hearing_init(&hearing, 4, &events);
    tide_world_copy(&alive, &base);
    alive.Actions.next = 3;
    tide_world_tick_events(&alive, NULL, tide_hearing_begin(&hearing, 0));
    tide_hearing_end(&hearing, 0);
    // The predicted death is canceled before this presentation endpoint.
    tide_world_copy(&alive, &base);
    alive.Time.tick = 1;
    tide_local_events(&local, &events);
    tide_frame(&alive, NULL, 1, &base, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.tide_available_causes.count != 0 && local.Observed.active == 1);
    tide_frame(&alive, NULL, 1, &alive, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.tide_available_causes.count == 0);
    // No death event: the same live handle is now concealed authoritatively.
    tide_world_copy(&hidden, &alive);
    hidden.Actions.next = 3;
    tide_world_tick(&hidden);
    tide_frame(&hidden, NULL, 1, &hidden, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.hidden == 7 && local.Observed.detailsHidden == 1);
    tide_hearing_free(&hearing);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&hidden);
    tide_world_free(&alive);
    tide_world_free(&base);
}

TIDE_TEST(visibility_lifecycle_bulk_duplicate_causes_and_cancelled_prediction)
{
    enum { count = 4096 };
    tide_world world = {0}, before = {0};
    tide_local local = {0};
    tide_event_log events = {0}, direct = {0};
    tide_entity *destroyed = malloc(count * sizeof *destroyed);
    TIDE_REQUIRE(destroyed != NULL);
    tide_world_init(&world, 1.0f / 60.0f);
    world.Actions.next = 19;
    world.Actions.count = count;
    tide_world_tick(&world);
    tide_world_copy(&before, &world);
    uint32_t found = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Ghost(&world, e) && tide_get_Ghost(&world, e).id >= 100) destroyed[count - ++found] = e;
    }
    TIDE_REQUIRE(found == count && destroyed[0].index > destroyed[count - 1].index);
    tide_local_init(&local);
    frame(&local, &world, &events);
    world.Actions.next = 20;
    tide_world_tick_events(&world, NULL, &events);
    TIDE_REQUIRE(events.count == count);
    const uint32_t original_size = events.size;
    for (uint32_t at = 0; at < original_size;) {
        tide_heard *r = (tide_heard *)(events.bytes + at);
        const uint32_t size = r->size;
        r->as = TIDE_HEARD_PREDICTED;
        tide_heard duplicate = *r;
        duplicate.as = TIDE_HEARD_VERIFIED;
        *tide_event_log_push(&events, sizeof duplicate, events.build) = duplicate;
        at += size;
    }
    // This guessed cause is canceled: the confirmed endpoint still has Marker.
    tide_heard *cancelled = tide_event_log_push(&events, sizeof *cancelled, events.build);
    cancelled->event = TIDE_HEARD_DESTROYED_CAUSE;
    cancelled->tick = (uint32_t)world.Time.tick - 1u;
    cancelled->target = marker(&world);
    cancelled->born = tide_entity_born(&world.entities, cancelled->target);
    cancelled->as = TIDE_HEARD_PREDICTED;
    tide_local_events(&local, &events);
    tide_frame(&world, NULL, 1, &world, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Observed.ghostHidden == 0 && local.tide_available_causes.count == 0);
    TIDE_CHECK(tide_has_Marker(&world, marker(&world)) && local.Observed.shown == 1);
    // Callback input may arrive in arbitrary order and remains caller-owned.
    const tide_entity first = destroyed[0], last = destroyed[count - 1];
    tide_game_api.hear_visibility(&world, &before, destroyed, count, &direct);
    TIDE_CHECK(direct.count == 0);
    TIDE_CHECK(tide_entity_equal(destroyed[0], first) && tide_entity_equal(destroyed[count - 1], last));
    free(destroyed);
    tide_event_log_free(&direct);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&world);
}

static bool contains_value(const tide_event_log *log, const int32_t value)
{
    for (uint32_t at = 0; at + sizeof value <= log->size; at++) {
        if (memcmp(log->bytes + at, &value, sizeof value) == 0) return true;
    }
    return false;
}

static void same_journal(const tide_event_log *a, const tide_event_log *b)
{
    TIDE_REQUIRE(a->size == b->size);
    TIDE_CHECK(a->count == b->count && a->build == b->build);
    TIDE_CHECK(!a->size || memcmp(a->bytes, b->bytes, a->size) == 0);
    TIDE_CHECK(tide_hash(a->bytes, a->size) == tide_hash(b->bytes, b->size));
}

TIDE_TEST(visibility_lifecycle_private_spawn_values_do_not_fingerprint_journals)
{
    for (int ghost = 0; ghost < 2; ghost++) {
        tide_world base = {0}, a = {0}, b = {0};
        tide_event_log events_a[2] = {{0}}, events_b[2] = {{0}};
        tide_world_init(&base, 1.0f / 60.0f);
        base.Actions.player = tide_player_from_index(0);
        base.Actions.next = 5;
        tide_world_tick(&base);
        const tide_entity e = marker(&base);
        tide_world_copy(&a, &base);
        tide_world_copy(&b, &base);
        TIDE_REQUIRE(tide_set_Details(&a, e, (Details){.secret = 111}));
        TIDE_REQUIRE(tide_set_Details(&b, e, (Details){.secret = 222}));
        a.Actions.next = b.Actions.next = ghost ? 15 : 14;
        tide_game_api.tick_client_events(&a, NULL, events_a);
        tide_game_api.tick_client_events(&b, NULL, events_b);
        const tide_player_id recipient = tide_player_from_index(1);
        TIDE_CHECK(tide_game_api.client_hash(&a, recipient) == tide_game_api.client_hash(&b, recipient));
        TIDE_CHECK(tide_game_api.client_hash(&a, tide_player_from_index(0)) != tide_game_api.client_hash(&b, tide_player_from_index(0)));
        same_journal(&events_a[1], &events_b[1]);
        if (!ghost) TIDE_CHECK(events_a[1].count != 0);
        a.Actions.next = b.Actions.next = ghost ? 16 : 6;
        tide_game_api.tick_client_events(&a, NULL, events_a);
        tide_game_api.tick_client_events(&b, NULL, events_b);
        same_journal(&events_a[1], &events_b[1]);
        TIDE_CHECK(tide_game_api.check_client_events(events_a[1].bytes, events_a[1].size, NULL, NULL));
        if (ghost) {
            // No eligible local Destroyed listener: even the private cause's
            // birth identity must not fingerprint the concealed Details value.
            TIDE_REQUIRE(events_a[1].count == 1 && events_a[1].size == sizeof(tide_heard));
            TIDE_CHECK(((const tide_heard *)events_a[1].bytes)->event == TIDE_HEARD_DESTROYED_CAUSE);
        }
        for (int i = 0; i < 2; i++) { tide_event_log_free(&events_a[i]); tide_event_log_free(&events_b[i]); }
        tide_world_free(&b);
        tide_world_free(&a);
        tide_world_free(&base);
    }
}

TIDE_TEST(visibility_lifecycle_private_policy_and_resident_do_not_enter_journals)
{
    tide_world base = {0}, a = {0}, b = {0};
    tide_event_log events_a[2] = {{0}}, events_b[2] = {{0}};
    tide_world_init(&base, 1.0f / 60.0f);
    base.Actions.player = tide_player_from_index(0);
    base.Actions.next = 5;
    tide_world_tick(&base);
    base.Actions.next = 17;
    tide_world_tick(&base);
    const tide_entity e = marker(&base);
    tide_world_copy(&a, &base);
    tide_world_copy(&b, &base);
    TIDE_REQUIRE(tide_set_Details(&a, e, (Details){.secret = 111}));
    TIDE_REQUIRE(tide_set_Details(&b, e, (Details){.secret = 222}));
    a.JournalCache.state.stamp = 111;
    a.JournalCache.state.digest = UINT64_C(0x5555555555555555);
    b.JournalCache.state.stamp = 222;
    b.JournalCache.state.digest = UINT64_C(0xAAAAAAAAAAAAAAAA);
    tide_entity scene = {0};
    for (uint32_t i = 0; i < base.entities.next_unused; i++) {
        const tide_entity candidate = tide_entity_in_slot(&base.entities, i);
        if (tide_has_JournalScene(&base, candidate)) scene = candidate;
    }
    TIDE_REQUIRE(!tide_entity_is_null(scene));
    JournalScene scene_a = tide_get_JournalScene(&a, scene);
    JournalScene scene_b = tide_get_JournalScene(&b, scene);
    scene_a.tide_visibility = scene_b.tide_visibility = SceneVisibility_Private;
    scene_a.tide_players = 2;
    scene_b.tide_players = 3;
    TIDE_REQUIRE(tide_set_JournalScene(&a, scene, scene_a));
    TIDE_REQUIRE(tide_set_JournalScene(&b, scene, scene_b));
    a.Actions.next = b.Actions.next = 17;
    tide_game_api.tick_client_events(&a, NULL, events_a);
    tide_game_api.tick_client_events(&b, NULL, events_b);
    // Player 1 sees the same public scene value; membership of player 0 and
    // opaque resident state differ, including scene snapshots in query rows.
    TIDE_REQUIRE(events_a[1].count != 0);
    same_journal(&events_a[1], &events_b[1]);
    TIDE_CHECK(tide_game_api.client_hash(&a, tide_player_from_index(1)) == tide_game_api.client_hash(&b, tide_player_from_index(1)));
    a.Actions.next = b.Actions.next = 18;
    tide_game_api.tick_client_events(&a, NULL, events_a);
    tide_game_api.tick_client_events(&b, NULL, events_b);
    TIDE_REQUIRE(events_a[1].count != 0);
    same_journal(&events_a[1], &events_b[1]);
    for (int i = 0; i < 2; i++) { tide_event_log_free(&events_a[i]); tide_event_log_free(&events_b[i]); }
    tide_world_free(&b);
    tide_world_free(&a);
    tide_world_free(&base);
}

TIDE_TEST(visibility_lifecycle_authoritative_recipient_journals)
{
    tide_world world = {0};
    tide_local local[2] = {{0}};
    tide_event_log events[2] = {{0}};
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 2; i++) tide_local_init(&local[i]);
    world.Actions.player = tide_player_from_index(0);
    world.Actions.next = 5;
    tide_world_tick(&world); // Existing Details is private to player 0.
    TIDE_REQUIRE(tide_game_api.tick_client_events && tide_game_api.check_client_events);
    const int before = world.Time.tick;
    world.Actions.next = 4;
    tide_game_api.tick_client_events(&world, NULL, events);
    TIDE_CHECK(world.Time.tick == before + 1); // One authoritative tick, regardless of recipient count.
    for (int i = 0; i < 2; i++) {
        uint32_t build = 0, count = 0;
        TIDE_CHECK(tide_game_api.check_client_events(events[i].bytes, events[i].size, &build, &count));
        TIDE_CHECK(build == events[i].build && count == events[i].count);
        for (uint32_t cut = 1; cut < events[i].size; cut++) {
            TIDE_CHECK(!tide_game_api.check_client_events(events[i].bytes, cut, &build, &count));
        }
        tide_heard *first = (tide_heard *)events[i].bytes;
        const uint64_t handlers = first->handlers;
        first->handlers |= UINT64_C(1) << 63;
        TIDE_CHECK(!tide_game_api.check_client_events(events[i].bytes, events[i].size, &build, &count));
        first->handlers = handlers;
    }
    TIDE_CHECK(!contains_value(&events[1], 0x13572468)); // No private query row or unused union slot.
    for (int i = 0; i < 2; i++) frame(&local[i], &world, &events[i]);
    TIDE_CHECK(local[0].Observed.spawned == 8 && local[1].Observed.spawned == 8);
    TIDE_CHECK(local[0].Observed.publicRows == 15 && local[1].Observed.publicRows == 15);
    TIDE_CHECK(local[0].Observed.privateRows == 0x13572468 + 88 && local[1].Observed.privateRows == 88);
    TIDE_CHECK(local[0].Observed.absentRows == 0 && local[1].Observed.absentRows == 7);

    world.Actions.next = 6;
    tide_game_api.tick_client_events(&world, NULL, events);
    TIDE_CHECK(!contains_value(&events[1], 0x13572468 + 1000));
    for (int i = 0; i < 2; i++) {
        uint32_t build = 0, count = 0;
        TIDE_CHECK(tide_game_api.check_client_events(events[i].bytes, events[i].size, &build, &count));
        const uint32_t size = events[i].size;
        for (uint32_t cut = 1; cut < sizeof(tide_heard) && cut < size; cut++) {
            TIDE_CHECK(!tide_game_api.check_client_events(events[i].bytes, cut, &build, &count));
        }
        if (size) {
            const uint8_t first = events[i].bytes[0];
            events[i].bytes[0] = 0;
            TIDE_CHECK(!tide_game_api.check_client_events(events[i].bytes, size, &build, &count));
            events[i].bytes[0] = first;
        }
        frame(&local[i], &world, &events[i]);
    }
    // The match's Destroyed handler changed both values before journaling.
    TIDE_CHECK(local[0].Observed.destroyed == 215 && local[1].Observed.destroyed == 215);
    TIDE_CHECK(local[0].Observed.destroyedDetails == 0x13572468 + 1000 + 1088);
    TIDE_CHECK(local[1].Observed.destroyedDetails == 1088 && local[1].Observed.withoutDestroyed == 107);
    for (int i = 0; i < 2; i++) { tide_local_free(&local[i]); tide_event_log_free(&events[i]); }
    tide_world_free(&world);
}

TIDE_TEST(visibility_lifecycle_hidden_destruction_is_not_journaled)
{
    tide_world world = {0};
    tide_event_log events[2] = {{0}};
    tide_world_init(&world, 1.0f / 60.0f);
    const tide_entity e = marker(&world);
    world.Actions.next = 7;
    tide_world_tick(&world);
    world.Actions.next = 6;
    tide_game_api.tick_client_events(&world, NULL, events);
    TIDE_CHECK(events[0].count == 0 && events[1].count == 0);
    TIDE_CHECK(!tide_entity_alive(&world.entities, e));
    for (int i = 0; i < 2; i++) tide_event_log_free(&events[i]);
    tide_world_free(&world);
}

TIDE_TEST(visibility_lifecycle_destruction_cause_uses_event_time_visibility)
{
    for (int reveal = 0; reveal < 2; reveal++) {
        tide_world world = {0};
        tide_event_log events[2] = {{0}};
        tide_world_init(&world, 1.0f / 60.0f);
        const tide_entity target = marker(&world);
        if (reveal) { world.Actions.next = 7; tide_world_tick(&world); }
        world.Actions.next = reveal ? 9 : 8;
        tide_game_api.tick_client_events(&world, NULL, events);
        TIDE_CHECK(!tide_entity_alive(&world.entities, target));
        for (int player = 0; player < 2; player++) {
            uint32_t at = 0, causes = 0;
            for (const tide_heard *h; (h = tide_event_log_next(&events[player], &at));) {
                if (h->event != TIDE_HEARD_DESTROYED_CAUSE) continue;
                TIDE_CHECK(h->target.index == target.index && h->target.generation == target.generation
                           && h->handlers == 0 && h->wants == 0);
                causes++;
            }
            TIDE_CHECK(causes == (uint32_t)reveal);
            TIDE_CHECK(tide_game_api.check_client_events(events[player].bytes, events[player].size, NULL, NULL));
            tide_event_log_free(&events[player]);
        }
        tide_world_free(&world);
    }
}

// Mirrors this fixture's Spawned union and scene-query row layout. These are
// private generated records: field offsets, rather than guessed byte patterns,
// let the test spoil each representation without reading it as a typed value.
typedef struct received_spawn_payload {
    Spawned event;
    JournalScene scene;
    Marker marker;
    JournalCache cache;
} received_spawn_payload;

typedef struct received_scene_row {
    tide_entity entity;
    JournalScene scene;
} received_scene_row;

static void reject_journal_byte(tide_event_log *log, const size_t offset, const uint8_t bad)
{
    TIDE_REQUIRE(offset < log->size);
    const uint8_t saved = log->bytes[offset];
    log->bytes[offset] = bad;
    TIDE_CHECK(!tide_game_api.check_client_events(log->bytes, log->size, NULL, NULL));
    log->bytes[offset] = saved;
    TIDE_CHECK(tide_game_api.check_client_events(log->bytes, log->size, NULL, NULL));
}

TIDE_TEST(visibility_lifecycle_received_plain_values_are_checked_before_casts)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_event_log events[2] = {{0}};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    world.Actions.next = 17;
    tide_game_api.tick_client_events(&world, NULL, events);
    tide_event_log *const log = &events[0];
    TIDE_REQUIRE(tide_game_api.check_client_events(log->bytes, log->size, NULL, NULL));
    uint32_t at = 0;
    bool checked_payload = false, checked_rows = false;
    for (const tide_heard *h; (h = tide_event_log_next(log, &at));) {
        const size_t payload = at - h->size + sizeof *h;
        if (h->event != TIDE_HEARD_ROWS && (h->wants & TIDE_HEARD_LIFECYCLE)) {
            TIDE_REQUIRE(h->size == ((sizeof *h + sizeof(received_spawn_payload) + 7u) & ~(size_t)7u));
            const size_t cache = payload + offsetof(received_spawn_payload, cache);
            reject_journal_byte(log, cache + offsetof(JournalCache, ready), 2);
            reject_journal_byte(log, cache + offsetof(JournalCache, ready), 255);
            // The handler forwards the complete singleton to JournalItemCount.
            // A forged List offset must fail here, before that helper can read it.
            reject_journal_byte(log, cache + offsetof(JournalCache, items), 16);
            reject_journal_byte(log, cache + offsetof(JournalCache, state), 1);
            reject_journal_byte(log, cache + offsetof(JournalCache, state) + offsetof(tide_resident, stamp), 1);
            const size_t scene = payload + offsetof(received_spawn_payload, scene);
            reject_journal_byte(log, scene + offsetof(JournalScene, tide_players), 1);
            reject_journal_byte(log, scene + offsetof(JournalScene, tide_visibility), 1);
            reject_journal_byte(log, scene + offsetof(JournalScene, bits) + offsetof(JournalBits, allowed) + 1u, 2);
            checked_payload = true;
        } else if (h->event == TIDE_HEARD_ROWS && h->same
                   && h->size == ((sizeof *h + h->same * sizeof(received_scene_row) + 7u) & ~UINT64_C(7))) {
            const size_t scene = payload + offsetof(received_scene_row, scene);
            reject_journal_byte(log, scene + offsetof(JournalScene, tide_players), 1);
            reject_journal_byte(log, scene + offsetof(JournalScene, tide_visibility), 1);
            reject_journal_byte(log, scene + offsetof(JournalScene, bits) + offsetof(JournalBits, allowed) + 1u, 255);
            checked_rows = true;
        }
    }
    TIDE_CHECK(checked_payload && checked_rows);
    frame(&local, &world, log); // Unchanged legitimate bytes still dispatch safely.
    for (int i = 0; i < 2; i++) tide_event_log_free(&events[i]);
    tide_local_free(&local);
    tide_world_free(&world);
}
