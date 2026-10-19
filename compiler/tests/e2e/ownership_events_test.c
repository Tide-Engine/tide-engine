#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"

static tide_draw_list draw;
static tide_gui gui;
enum { ALL = 0x1e };

static tide_entity target(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Target(w, e) && tide_get_Target(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

static void init(tide_world *w, tide_local *l, const int32_t player)
{
    tide_world_init(w, 1.0f / 60.0f);
    tide_local_init(l);
    tide_local_set_session(l, TIDE_SESSION_CONNECTED, tide_player_from_index(player), 0, false, false, "");
}

static void frame(tide_local *l, const tide_world *w, const tide_world *verified, tide_event_log *events)
{
    tide_local_events(l, events);
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1, verified, NULL, 1, l, &draw, &gui);
}

TIDE_TEST(ownership_events_capture_before_later_transfers_and_owner_removal)
{
    tide_world w = {0}, quiet = {0};
    tide_local l = {0};
    tide_event_log events = {0};
    init(&w, &l, 0);
    w.Actions.mode = 1;
    tide_world_copy(&quiet, &w);
    tide_world_tick_events(&w, NULL, &events);
    tide_world_tick(&quiet);
    TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&quiet));
    TIDE_REQUIRE(tide_set_Owner(&w, target(&w, 1), (Owner){.player = tide_player_from_index(1)}));
    w.Actions.mode = 2;
    tide_world_tick(&w); // Owner is gone before local delivery.
    TIDE_CHECK(!tide_has_Owner(&w, target(&w, 1)));
    frame(&l, &w, NULL, &events);
    TIDE_CHECK(l.Seen.mine == 2 && l.Seen.rest == (ALL ^ 2));
    TIDE_CHECK(l.Seen.alone == 2 && l.Seen.declared == 2);
    TIDE_CHECK(l.Seen.ownedRest == ((1 << 2) | (1 << 4)) && l.Seen.noOwner == (1 << 3));
    TIDE_CHECK(l.Seen.sureMine == 2 && l.Seen.sureRest == (ALL ^ 2) && l.Seen.rows == 10);
    TIDE_CHECK(tide_get_Target_local(&l, target(&w, 1)).hits == 1);
    tide_local_free(&l); tide_world_free(&quiet); tide_world_free(&w); tide_event_log_free(&events);
}

TIDE_TEST(ownership_events_match_handlers_finish_before_capture_and_sparse_filters_apply)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0};
    init(&w, &l, 1);
    w.Actions.mode = 6;
    tide_world_tick(&w);
    w.Actions.mode = 1; w.Actions.transfer = true;
    tide_world_tick_events(&w, NULL, &events);
    frame(&l, &w, NULL, &events);
    // Transfer runs for every Owner, including the initially empty one.
    TIDE_CHECK(l.Seen.mine == ((1 << 1) | (1 << 2) | (1 << 4)) && l.Seen.rest == (1 << 3));
    TIDE_CHECK(l.Seen.gated == ((1 << 2) | (1 << 4)));
    TIDE_CHECK(l.Seen.rows == 30);
    tide_local_free(&l); tide_world_free(&w); tide_event_log_free(&events);
}

TIDE_TEST(ownership_events_absent_owner_differs_from_owner_with_no_player)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0};
    init(&w, &l, -1);
    w.Actions.mode = 1;
    tide_world_tick_events(&w, NULL, &events);
    frame(&l, &w, NULL, &events);
    TIDE_CHECK(l.Seen.mine == (1 << 4) && l.Seen.rest == (ALL ^ (1 << 4)));
    TIDE_CHECK(l.Seen.noOwner == (1 << 3) && l.Seen.alone == (1 << 4));
    tide_local_free(&l); tide_world_free(&w); tide_event_log_free(&events);
}

TIDE_TEST(ownership_events_lifecycle_uses_recorded_owner_even_when_entity_is_gone)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0};
    init(&w, &l, 0);
    w.Actions.mode = 4;
    tide_world_tick_events(&w, NULL, &events);
    TIDE_REQUIRE(tide_set_Owner(&w, target(&w, 5), (Owner){.player = tide_player_from_index(1)}));
    w.Actions.mode = 3;
    tide_world_tick_events(&w, NULL, &events);
    frame(&l, &w, NULL, &events);
    TIDE_CHECK(l.Seen.spawnedMine == (1 << 5) && l.Seen.spawnedRest == 0);
    TIDE_CHECK(l.Seen.lifecycleAlone == 1);
    TIDE_CHECK(l.Seen.destroyedMine == 2 && l.Seen.destroyedRest == ((ALL ^ 2) | (1 << 5)));
    TIDE_CHECK(l.Seen.hiddenMine == 0 && l.Seen.hiddenRest == 0);
    tide_local_free(&l); tide_world_free(&w); tide_event_log_free(&events);
}

TIDE_TEST(ownership_events_replay_stays_silent_and_verified_uses_its_recording)
{
    tide_world w = {0}, base = {0}; tide_local l = {0}; tide_event_log events = {.taken = true}; tide_hearing hearing = {0};
    init(&w, &l, 0);
    w.Actions.mode = 1;
    tide_world_copy(&base, &w);
    tide_hearing_init(&hearing, 8, &events);
    const uint32_t tick = (uint32_t)w.Time.tick;
    tide_world_tick_events(&w, NULL, tide_hearing_begin(&hearing, tick));
    tide_hearing_end(&hearing, tick);
    frame(&l, &w, NULL, &events);
    TIDE_CHECK(l.Seen.mine == 2 && l.Seen.sureMine == 0);
    // The same occurrence stays silent on replay, even if captured state changed.
    tide_world_copy(&w, &base);
    w.Actions.transfer = true;
    tide_world_tick_events(&w, NULL, tide_hearing_begin(&hearing, tick));
    tide_hearing_end(&hearing, tick);
    TIDE_CHECK(events.count == 0);
    tide_hearing_confirm(&hearing, tick);
    frame(&l, &w, NULL, &events);
    TIDE_CHECK(l.Seen.mine == 2 && l.Seen.sureMine == 0 && l.Seen.sureRest == ALL);
    TIDE_CHECK(tide_get_Target_local(&l, target(&w, 1)).hits == 1);
    tide_hearing_free(&hearing); tide_local_free(&l); tide_world_free(&w); tide_world_free(&base); tide_event_log_free(&events);
}

TIDE_TEST(ownership_events_availability_tracks_owner_values_presence_and_verified_endpoints)
{
    tide_world w = {0}, before = {0}; tide_local l = {0}; tide_event_log events = {0};
    init(&w, &l, 0);
    frame(&l, &w, &w, &events);
    TIDE_CHECK(l.Seen.shownMine == 2 && l.Seen.shownRest == (ALL ^ 2) && l.Seen.sureShown == 2);
    tide_world_copy(&before, &w);
    TIDE_REQUIRE(tide_set_Owner(&w, target(&w, 1), (Owner){.player = tide_player_from_index(1)}));
    frame(&l, &w, &before, &events);
    TIDE_CHECK(l.Seen.hiddenMine == 2 && l.Seen.shownRest == ALL && l.Seen.sureHidden == 0);
    frame(&l, &w, &w, &events);
    TIDE_CHECK(l.Seen.sureHidden == 2);
    w.Actions.mode = 5;
    tide_world_tick(&w);
    frame(&l, &w, &w, &events);
    TIDE_CHECK(l.Seen.shownMine == 10 && l.Seen.hiddenRest == 8 && l.Seen.sureShown == 10);
    frame(&l, &before, &before, &events); // Corrected ownership restores its availability.
    TIDE_CHECK(l.Seen.shownMine == 12 && l.Seen.hiddenMine == 10 && l.Seen.sureHidden == 10);
    const Seen seen = l.Seen;
    frame(&l, &before, &before, &events);
    TIDE_CHECK(memcmp(&seen, &l.Seen, sizeof seen) == 0);
    tide_local_free(&l); tide_world_free(&w); tide_world_free(&before); tide_event_log_free(&events);
}

// The generated journal prefix; following union slots are intentionally private.
typedef struct owner_prefix { Spawned event; tide_player_id owner; uint32_t present; } owner_prefix;

TIDE_TEST(ownership_events_recipient_journals_hide_private_owner_and_validate_metadata)
{
    tide_world w = {0}, other = {0}; tide_local l = {0};
    tide_event_log logs[TIDE_GAME_PLAYERS] = {0}, variants[TIDE_GAME_PLAYERS] = {0}, empty = {0};
    init(&w, &l, 1);
    w.Actions.mode = 4; w.Actions.privateOwner = true;
    tide_world_copy(&other, &w);
    other.Actions.owner = 1;
    tide_game_api.tick_client_events(&w, NULL, logs);
    tide_game_api.tick_client_events(&other, NULL, variants);
    TIDE_CHECK(logs[1].size == variants[1].size && logs[1].count == variants[1].count);
    TIDE_CHECK(memcmp(logs[1].bytes, variants[1].bytes, logs[1].size) == 0);
    TIDE_CHECK(tide_game_api.check_client_events(logs[0].bytes, logs[0].size, NULL, NULL));
    TIDE_CHECK(tide_game_api.check_client_events(logs[1].bytes, logs[1].size, NULL, NULL));
    const tide_heard *r = (const tide_heard *)logs[1].bytes;
    TIDE_REQUIRE(r && logs[1].size >= sizeof *r + sizeof(owner_prefix));
    owner_prefix prefix;
    memcpy(&prefix, r + 1, sizeof prefix);
    TIDE_CHECK(prefix.present == 0 && tide_player_equal(prefix.owner, (tide_player_id){0}));
    uint8_t *bad = malloc(logs[1].size);
    TIDE_REQUIRE(bad != NULL);
    memcpy(bad, logs[1].bytes, logs[1].size);
    prefix.present = 2;
    memcpy(bad + sizeof *r, &prefix, sizeof prefix);
    TIDE_CHECK(!tide_game_api.check_client_events(bad, logs[1].size, NULL, NULL));
    prefix.present = 0; prefix.owner = tide_player_from_index(0);
    memcpy(bad + sizeof *r, &prefix, sizeof prefix);
    TIDE_CHECK(!tide_game_api.check_client_events(bad, logs[1].size, NULL, NULL));
    free(bad);
    tide_local_events(&l, &logs[1]);
    frame(&l, NULL, NULL, &empty); // No current world is needed for the captured event.
    TIDE_CHECK(l.Seen.spawnedMine == 0 && l.Seen.spawnedRest == (1 << 5));
    TIDE_CHECK(l.Seen.lifecycleAlone == 0);
    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) { tide_event_log_free(&logs[i]); tide_event_log_free(&variants[i]); }
    tide_local_free(&l); tide_world_free(&w); tide_world_free(&other);
}
