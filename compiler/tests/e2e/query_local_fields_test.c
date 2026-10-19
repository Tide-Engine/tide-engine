#include "game.h"
#include "tide_test.h"

static tide_draw_list draw;
static tide_gui gui;
static const tide_entity FIRST = {1, 1}, SECOND = {2, 1};
static void frame(tide_world *w, const tide_world *verified, tide_local *l)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1, verified, NULL, 1, l, &draw, &gui);
}
TIDE_TEST(query_local_fields_reads_defaults_and_writes_through_borrowed_helpers)
{
    tide_world w = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    const uint64_t hash = tide_world_hash(&w);
    l.Control.mode = 1; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 18);
    TIDE_CHECK(!tide_has_Body_local(&l, FIRST) && !tide_has_Body_local(&l, SECOND));
    l.Control.mode = 2; frame(&w, NULL, &l);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 5 && tide_get_Body_local(&l, SECOND).flash == 5);
    TIDE_CHECK(tide_list_count(tide_get_Body_local(&l, FIRST).trail) == 2 && tide_get_Body_local(&l, FIRST).pair.at[0] == 1);
    l.Control.mode = 1; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 28 && tide_world_hash(&w) == hash);
    tide_local_free(&l); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_read_rows_resolve_current_local_values)
{
    tide_world w = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    l.Control.mode = 3; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.before == 8 && l.Seen.after == 12);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 7);
    l.Control.mode = 5; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 16);
    tide_local_free(&l); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_verified_match_values_keep_current_local_state)
{
    tide_world w = {0}, confirmed = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&confirmed, &w);
    tide_world_tick(&w);
    l.Control.mode = 4; frame(&w, &confirmed, &l);
    TIDE_CHECK(l.Seen.positions == 3 && tide_get_Body_local(&l, FIRST).flash == 4);
    frame(&w, &confirmed, &l);
    TIDE_CHECK(l.Seen.positions == 6 && tide_get_Body_local(&l, FIRST).flash == 5);
    tide_local_free(&l); tide_world_free(&confirmed); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_events_record_only_match_values)
{
    tide_world w = {0}; tide_local l = {0}; tide_event_log events = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    w.Actions.send = true; tide_world_tick_events(&w, NULL, &events); w.Actions.send = false;
    l.Control.mode = 2; frame(&w, NULL, &l); // Locals change after recording.
    const uint64_t hash = tide_world_hash(&w);
    l.Control.mode = 0; tide_local_events(&l, &events); frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.heard == 10 && l.Seen.sure == 30 && l.Seen.positions == 5);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 15 && tide_world_hash(&w) == hash);
    tide_event_log_free(&events); tide_local_free(&l); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_methods_inline_actions_and_operand_order)
{
    tide_world w = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    l.Control.mode = 6; frame(&w, NULL, &l);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 5);
    l.Control.mode = 7; frame(&w, NULL, &l);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 6 && l.Seen.total == 2);
    l.Control.mode = 8; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 30132);
    tide_local_free(&l); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_handle_reuse_uses_defaults_and_releases_old_fields)
{
    tide_world w = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    l.Control.mode = 2; frame(&w, NULL, &l);
    tide_entity_set_born(&w.entities, FIRST, 12345);
    l.Control.mode = 1; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 23); // Old local fields aren't the new entity's.
    l.Control.mode = 2; frame(&w, NULL, &l);
    TIDE_CHECK(tide_get_Body_local(&l, FIRST).flash == 5 && tide_get_Body_local(&l, FIRST).pair.at[0] == 1);
    TIDE_CHECK(l.Seen.forgotten == 5);
    tide_local_free(&l); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_chunked_iteration_preserves_match_snapshots)
{
    tide_world w = {0}, snapshot = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
    w.Actions.count = 5000; tide_world_tick(&w); w.Actions.count = 0;
    tide_world_copy(&snapshot, &w);
    const uint64_t hash = tide_world_hash(&w);
    l.Control.mode = 2; frame(&w, NULL, &l);
    l.Control.mode = 1; frame(&w, NULL, &l);
    TIDE_CHECK(l.Seen.total == 5002 * 14);
    TIDE_CHECK(tide_world_hash(&w) == hash && tide_world_hash(&snapshot) == hash);
    tide_local_free(&l); tide_world_free(&snapshot); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_birth_reset_preserves_an_active_outer_alias)
{
    tide_world w = {0}, before = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&before, &w);
    const tide_entity first = {3, 1}, second = {4, 1};
    tide_entity_set_born(&w.entities, first, 54321);
    frame(&w, NULL, &l); // The outer binding is now the predicted entity's.
    l.Control.mode = 9; frame(&w, &before, &l);
    TIDE_CHECK(tide_get_Small_local(&l, first).value == 5);
    TIDE_CHECK(tide_get_Small_local(&l, second).value == 7);
    tide_local_free(&l); tide_world_free(&before); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_mixed_verified_bindings_share_current_local_fields)
{
    tide_world w = {0}, confirmed = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&confirmed, &w);
    tide_world_tick(&w);
    l.Control.mode = 10; frame(&w, &confirmed, &l);
    TIDE_CHECK(l.Seen.total == 4 && l.Seen.positions == 1);
    TIDE_CHECK(!tide_has_Body_local(&l, SECOND));
    tide_local_free(&l); tide_world_free(&confirmed); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_mutable_reference_is_prepared_before_assignment_operands)
{
    tide_world w = {0}, confirmed = {0}; tide_local l = {0};
    tide_world_init(&w, 1.0f / 60); tide_local_init(&l); tide_world_copy(&confirmed, &w);
    l.Control.mode = 5; frame(&w, NULL, &l);
    const tide_entity first = {3, 1}, second = {4, 1};
    tide_entity_set_born(&w.entities, first, 54321);
    l.Control.mode = 11; frame(&w, &confirmed, &l);
    TIDE_CHECK(tide_get_Small_local(&l, first).value == 15);
    TIDE_CHECK(tide_get_Small_local(&l, second).value == 8);
    tide_local_free(&l); tide_world_free(&confirmed); tide_world_free(&w);
}
TIDE_TEST(query_local_fields_readonly_outer_and_row_borrows_observe_query_writes)
{
    for (int mode = 12; mode <= 13; mode++) {
        tide_world w = {0}; tide_local l = {0};
        tide_world_init(&w, 1.0f / 60); tide_local_init(&l);
        l.Control.mode = mode; frame(&w, NULL, &l);
        TIDE_CHECK(l.Seen.total == 2);
        TIDE_CHECK(tide_get_Plain_local(&l, (tide_entity){5, 1}).value == 9);
        TIDE_CHECK(tide_get_Plain_local(&l, (tide_entity){6, 1}).value == 9);
        tide_local_free(&l); tide_world_free(&w);
    }
}
