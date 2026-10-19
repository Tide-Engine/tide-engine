#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"

static tide_draw_list draw;
static tide_gui gui;
static void deliver(tide_world *w, tide_local *l, tide_event_log *events)
{
    tide_local_events(l, events);
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1, NULL, NULL, 1, l, &draw, &gui);
}

TIDE_TEST(event_containers_capture_turn_and_tick_values_and_own_local_copies)
{
    tide_world w = {0}, quiet = {0}; tide_local l = {0}; tide_event_log events = {0};
    tide_world_init(&w, 1.0f / 60); tide_world_init(&quiet, 1.0f / 60); tide_local_init(&l);
    w.Actions.fire = quiet.Actions.fire = 2;
    tide_world_tick_events(&w, NULL, &events); tide_world_tick(&quiet);
    TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&quiet));
    const uint64_t hash = tide_world_hash(&w);
    deliver(&w, &l, &events);
    TIDE_CHECK(l.Seen.direct == 72 && l.Seen.rows == 42 && l.Seen.scalar == 2);
    TIDE_CHECK(l.Seen.payload == 14 && l.Seen.sure == 32);
    TIDE_CHECK(tide_world_hash(&w) == hash);
    tide_text_use(&w.heap, &l.heap);
    TIDE_CHECK(tide_str_eq(tide_text_view(l.Seen.title), tide_str_from_cstr("turn")));
    TIDE_CHECK(tide_str_eq(tide_text_view(l.Seen.word), tide_str_from_cstr("world")));
    TIDE_CHECK(*(const int32_t *)tide_list_at(l.Seen.saved, 0, sizeof(int32_t)) == 21);
    tide_world_free(&w); tide_world_free(&quiet);
    tide_text_use(NULL, &l.heap);
    TIDE_CHECK(*(const int32_t *)tide_list_at(l.Seen.saved, 0, sizeof(int32_t)) == 21);
    tide_event_log_free(&events); tide_local_free(&l);
}

TIDE_TEST(event_containers_unread_large_fields_add_no_journal_bytes)
{
    tide_world a = {0}, b = {0}; tide_event_log x = {0}, y = {0};
    tide_world_init(&a, 1.0f / 60); tide_world_init(&b, 1.0f / 60);
    a.Actions.fire = b.Actions.fire = 1; b.Actions.grow = 5000;
    tide_world_tick_events(&a, NULL, &x); tide_world_tick_events(&b, NULL, &y);
    TIDE_REQUIRE(x.size == y.size);
    TIDE_CHECK(x.count == y.count && x.build == y.build && !memcmp(x.bytes, y.bytes, x.size));
    uint32_t at = 0, rows = 0;
    for (const tide_heard *h; (h = tide_event_log_next(&x, &at));) if (h->event == TIDE_HEARD_ROWS) rows++;
    TIDE_CHECK(rows == 1); // The scalar and container query readers share one record.
    tide_event_log_free(&x); tide_event_log_free(&y); tide_world_free(&a); tide_world_free(&b);
}

TIDE_TEST(event_containers_record_survives_source_destruction_and_log_copy)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0}, copy = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    w.Actions.fire = 1; w.Actions.kill = true;
    tide_world_tick_events(&w, NULL, &events);
    uint32_t at = 0;
    for (const tide_heard *h; (h = tide_event_log_next(&events, &at));) tide_event_log_copy(&copy, &events, h, 0);
    tide_event_log_free(&events); tide_world_free(&w);
    tide_world_init(&w, 1.0f / 60);
    deliver(&w, &l, &copy);
    TIDE_CHECK(l.Seen.direct == 31 && l.Seen.rows == 0 && l.Seen.deaths == 11);
    tide_event_log_free(&copy); tide_local_free(&l); tide_world_free(&w);
}

TIDE_TEST(event_containers_replay_dedup_and_delayed_confirmation)
{
    tide_world w = {0}, before = {0}; tide_local l = {0}; tide_event_log events = {.taken = true}; tide_hearing hearing = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&before, &w);
    tide_hearing_init(&hearing, 8, &events);
    for (int repeat = 0; repeat < 2; repeat++) {
        tide_world_copy(&w, &before); w.Actions.fire = 1;
        tide_world_tick_events(&w, NULL, tide_hearing_begin(&hearing, 0));
        tide_hearing_end(&hearing, 0); deliver(&w, &l, &events);
        TIDE_CHECK(l.Seen.direct == 31 && l.Seen.sure == 0);
    }
    tide_hearing_confirm(&hearing, 0); deliver(&w, &l, &events);
    TIDE_CHECK(l.Seen.direct == 31 && l.Seen.sure == 11);
    tide_hearing_free(&hearing); tide_event_log_free(&events); tide_local_free(&l); tide_world_free(&before); tide_world_free(&w);
}

TIDE_TEST(event_containers_chunked_values_and_changed_event_contents)
{
    tide_world w = {0}, before = {0}; tide_local l = {0}; tide_event_log events = {.taken = true}; tide_hearing hearing = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&before, &w);
    tide_hearing_init(&hearing, 8, &events);
    for (int repeat = 0; repeat < 2; repeat++) {
        tide_world_copy(&w, &before); w.Actions.fire = 1; w.Actions.readGrow = 5000; w.Actions.payload = 7 + repeat;
        tide_world_tick_events(&w, NULL, tide_hearing_begin(&hearing, 0));
        tide_hearing_end(&hearing, 0); deliver(&w, &l, &events);
        tide_text_use(&w.heap, &l.heap);
        TIDE_CHECK(tide_list_count(l.Seen.saved) == 5002);
        TIDE_CHECK(*(const int32_t *)tide_list_at(l.Seen.saved, 5001, sizeof(int32_t)) == 4999);
    }
    TIDE_CHECK(l.Seen.payload == 15 && l.Seen.direct == 62);
    tide_hearing_free(&hearing); tide_event_log_free(&events); tide_local_free(&l); tide_world_free(&before); tide_world_free(&w);
}

TIDE_TEST(event_containers_recipient_privacy_and_checked_variable_tails)
{
    tide_world a = {0}, b = {0}; tide_event_log x[2] = {{0}}, y[2] = {{0}};
    tide_world_init(&a, 1.0f / 60); tide_world_init(&b, 1.0f / 60);
    a.Actions.spawn = b.Actions.spawn = true;
    a.Actions.secret = 0x13572468; b.Actions.secret = 0x24681357;
    tide_game_api.tick_client_events(&a, NULL, x); tide_game_api.tick_client_events(&b, NULL, y);
    TIDE_REQUIRE(x[1].size == y[1].size);
    TIDE_CHECK(!memcmp(x[1].bytes, y[1].bytes, x[1].size));
    for (int player = 0; player < 2; player++) {
        tide_event_log *log = &x[player];
        TIDE_REQUIRE(tide_game_api.check_client_events(log->bytes, log->size, NULL, NULL));
        for (uint32_t cut = 1; cut < log->size; cut++) {
            // Record boundaries can be complete valid prefixes; interior truncations cannot.
            if (cut & 7u) TIDE_CHECK(!tide_game_api.check_client_events(log->bytes, cut, NULL, NULL));
        }
        uint8_t *bad = malloc(log->size); TIDE_REQUIRE(bad != NULL);
        memcpy(bad, log->bytes, log->size);
        if (log->size) bad[log->size - 1u] ^= 0xFFu;
        TIDE_CHECK(!tide_game_api.check_client_events(bad, log->size, NULL, NULL));
        free(bad);
        tide_local l = {0}; tide_local_init(&l); deliver(&a, &l, log);
        TIDE_CHECK(l.Seen.secret == (player ? 0 : 2 * 0x13572468));
        tide_local_free(&l);
    }
    for (int i = 0; i < 2; i++) { tide_event_log_free(&x[i]); tide_event_log_free(&y[i]); }
    tide_world_free(&a); tide_world_free(&b);
}

TIDE_TEST(event_containers_availability_uses_each_endpoints_heap)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    deliver(&w, &l, &events);
    TIDE_CHECK(l.Seen.shown == 1);
    w.Actions.fire = 1; w.Actions.kill = true;
    tide_world_tick(&w); deliver(&w, &l, &events);
    TIDE_CHECK(l.Seen.hidden == 1 && l.Seen.hiddenRows == 1);
    w.Actions.fire = 0; w.Actions.kill = false; w.Actions.spawn = true;
    tide_world_tick(&w); deliver(&w, &l, &events);
    TIDE_CHECK(l.Seen.shown == 2);
    tide_event_log_free(&events); tide_local_free(&l); tide_world_free(&w);
}
