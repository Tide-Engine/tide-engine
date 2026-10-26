#include <string.h>

#include "tide/events.h"
#include "tide_test.h"

// What a client remembers of the ticks it ran (tide/events.h): each event is heard once however often its tick runs, and again as verified once confirmed.

#define BUILD 7u

static void sent(tide_event_log *run, const uint16_t event, const uint32_t index, const uint32_t born, const uint64_t same,
                 const uint8_t wants)
{
    tide_heard *heard = tide_event_log_push(run, (uint32_t)sizeof *heard + 4u, BUILD);
    heard->event = event;
    heard->target = (tide_entity){index, index ? 1u : 0u};
    heard->born = born;
    heard->same = same;
    heard->wants = wants;
}

static uint32_t count(const tide_event_log *log, const uint8_t as, const uint16_t event)
{
    uint32_t n = 0, at = 0;
    for (const tide_heard *heard; (heard = tide_event_log_next(log, &at));) {
        if (heard->as == as && (!event || heard->event == event)) n++;
    }
    return n;
}

static const uint8_t BOTH = TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED;

TIDE_TEST(events_a_tick_run_again_says_nothing_twice)
{
    tide_event_log out = {.taken = true};
    tide_hearing h = {0};
    tide_hearing_init(&h, 8, &out);

    tide_event_log *run = tide_hearing_begin(&h, 5);
    TIDE_REQUIRE(run != NULL);
    sent(run, 1, 10, 100, 0xA, BOTH);
    sent(run, 2, 0, 0, 0xB, BOTH);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 2 && count(&out, TIDE_HEARD_PREDICTED, 0) == 2);
    TIDE_CHECK(out.first_tick == 5);
    tide_event_log_clear(&out);

    run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, BOTH);
    sent(run, 3, 10, 100, 0xC, BOTH);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 1 && count(&out, TIDE_HEARD_PREDICTED, 3) == 1);
    tide_event_log_clear(&out);

    run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, BOTH);
    sent(run, 2, 0, 0, 0xB, BOTH);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 0);

    tide_hearing_confirm(&h, 5);
    TIDE_CHECK(out.count == 2 && count(&out, TIDE_HEARD_VERIFIED, 1) == 1 && count(&out, TIDE_HEARD_VERIFIED, 2) == 1);
    tide_hearing_confirm(&h, 5);
    TIDE_CHECK(out.count == 2);
    tide_hearing_free(&h);
    tide_event_log_free(&out);
}

TIDE_TEST(events_alike_in_one_tick_are_each_heard_once)
{
    tide_event_log out = {.taken = true};
    tide_hearing h = {0};
    tide_hearing_init(&h, 8, &out);
    tide_event_log *run = tide_hearing_begin(&h, 3);
    sent(run, 1, 10, 100, 0xA, TIDE_HEARD_PREDICTED);
    sent(run, 1, 10, 100, 0xA, TIDE_HEARD_PREDICTED);
    tide_hearing_end(&h, 3);
    TIDE_CHECK(out.count == 2);
    tide_event_log_clear(&out);
    run = tide_hearing_begin(&h, 3);
    for (int i = 0; i < 3; i++) sent(run, 1, 10, 100, 0xA, TIDE_HEARD_PREDICTED);
    tide_hearing_end(&h, 3);
    TIDE_CHECK(out.count == 1);
    tide_event_log_clear(&out);
    tide_hearing_confirm(&h, 3);
    TIDE_CHECK(out.count == 0);
    run = tide_hearing_begin(&h, 4);
    sent(run, 1, 10, 100, 0xA, TIDE_HEARD_PREDICTED);
    tide_hearing_end(&h, 4);
    TIDE_CHECK(out.count == 1);
    tide_hearing_free(&h);
    tide_event_log_free(&out);
}

TIDE_TEST(events_the_same_entity_is_told_by_what_it_was_born_as)
{
    tide_event_log out = {.taken = true};
    tide_hearing h = {0};
    tide_hearing_init(&h, 8, &out);
    tide_event_log *run = tide_hearing_begin(&h, 9);
    sent(run, 1, 10, 100, 0xA, TIDE_HEARD_PREDICTED);
    sent(run, 1, 11, 0, 0xA, TIDE_HEARD_PREDICTED);
    tide_hearing_end(&h, 9);
    TIDE_CHECK(out.count == 2);
    tide_event_log_clear(&out);

// The replay gave handle 10 to another entity: the event to the other is heard, and the first isn't heard twice.
    run = tide_hearing_begin(&h, 9);
    sent(run, 1, 10, 200, 0xA, TIDE_HEARD_PREDICTED);
    sent(run, 1, 12, 100, 0xA, TIDE_HEARD_PREDICTED);
    sent(run, 1, 11, 0, 0xA, TIDE_HEARD_PREDICTED);
    tide_hearing_end(&h, 9);
    TIDE_CHECK(out.count == 1);
    uint32_t at = 0;
    const tide_heard *heard = tide_event_log_next(&out, &at);
    TIDE_CHECK(heard && heard->born == 200 && heard->tick == 9);
    tide_hearing_free(&h);
    tide_event_log_free(&out);
}

TIDE_TEST(events_nobody_takes_are_not_recorded)
{
    tide_event_log out = {0};
    tide_hearing h = {0};
    tide_hearing_init(&h, 8, &out);
    TIDE_CHECK(tide_hearing_begin(&h, 1) == NULL);
    out.taken = true;
    tide_event_log *run = tide_hearing_begin(&h, 1);
    TIDE_REQUIRE(run != NULL);
    sent(run, 1, 0, 0, 0xA, BOTH);
    tide_hearing_end(&h, 1);
    TIDE_CHECK(out.count == 1);
    tide_hearing_skip_to(&h, 4);
    tide_hearing_confirm(&h, 1);
    TIDE_CHECK(out.count == 1);
    TIDE_CHECK(tide_hearing_begin(&h, 1 + TIDE_HEARD_TICKS) != NULL);
    tide_hearing_end(&h, 1 + TIDE_HEARD_TICKS);
    TIDE_CHECK(tide_hearing_begin(&h, 2 + TIDE_HEARD_TICKS) == NULL);
    TIDE_CHECK(out.count == 0 && !out.taken);
    TIDE_CHECK(tide_hearing_begin(&h, 3 + TIDE_HEARD_TICKS) == NULL);
    tide_hearing_free(&h);
    tide_event_log_free(&out);
}

TIDE_TEST(events_of_another_build_are_dropped)
{
    tide_event_log log = {0};
    tide_event_log taker = {0};
    sent(&log, 1, 0, 0, 0xA, BOTH);
    sent(&log, 2, 0, 0, 0xB, BOTH);
    TIDE_CHECK(log.count == 2 && log.build == BUILD);
    tide_event_log_take(&taker, &log);
    TIDE_CHECK(taker.count == 2 && taker.build == BUILD && log.count == 0);
    tide_heard *other = tide_event_log_push(&log, (uint32_t)sizeof *other, BUILD + 1u);
    other->event = 9;
    TIDE_CHECK(log.count == 1 && log.build == BUILD + 1u);
    tide_event_log_take(&taker, &log);
    TIDE_CHECK(taker.count == 1 && taker.build == BUILD + 1u);
    uint32_t at = 0;
    TIDE_CHECK(tide_event_log_next(&taker, &at)->event == 9 && tide_event_log_next(&taker, &at) == NULL);
    tide_event_log_free(&log);
    tide_event_log_free(&taker);
}

static void recorded(tide_event_log *run, const uint64_t which, const uint32_t rows, const uint8_t wants)
{
    tide_heard *heard = tide_event_log_push(run, (uint32_t)sizeof *heard + rows * 8u + 64u, BUILD);
    heard->event = TIDE_HEARD_ROWS;
    heard->handlers = which;
    heard->same = rows;
    heard->wants = wants;
    for (uint32_t i = 0; i < rows; i++) ((uint64_t *)(heard + 1))[i] = which * 100u + i;
    tide_event_log_trim(run, heard, (uint32_t)sizeof *heard + rows * 8u);
}

static const tide_heard *find(const tide_event_log *log, const uint16_t event, uint32_t *at)
{
    *at = 0;
    for (const tide_heard *heard; (heard = tide_event_log_next(log, at));) {
        if (heard->event == event) return heard;
    }
    return NULL;
}

TIDE_TEST(events_rows_go_where_an_event_that_reads_them_goes)
{
    tide_event_log out = {.taken = true};
    tide_hearing h = {0};
    tide_hearing_init(&h, 8, &out);
    const uint8_t reads = BOTH | TIDE_HEARD_READS_ROWS;
    uint32_t at = 0;

    tide_event_log *run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, reads);
    sent(run, 2, 0, 0, 0xB, BOTH);
    recorded(run, 0, 3, BOTH);
    recorded(run, 1, 2, TIDE_HEARD_VERIFIED);
    TIDE_CHECK(run->count == 4 && run->size == 4u * sizeof(tide_heard) + 2u * 8u + 3u * 8u + 2u * 8u);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 3 && count(&out, TIDE_HEARD_PREDICTED, TIDE_HEARD_ROWS) == 1);
    const tide_heard *event = find(&out, 1, &at);
    const tide_heard *rows = tide_event_log_rows(&out, at, event, 0);
    TIDE_REQUIRE(event && rows);
    TIDE_CHECK(rows->tick == 5 && tide_heard_row_count(rows, 8) == 3 && ((const uint64_t *)(rows + 1))[2] == 2);
    TIDE_CHECK(tide_heard_row_count(rows, 16) == 0 && tide_heard_row_count(NULL, 8) == 0);
    TIDE_CHECK(tide_event_log_rows(&out, at, event, 1) == NULL);
    tide_event_log_clear(&out);

    run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, reads);
    sent(run, 2, 0, 0, 0xB, BOTH);
    recorded(run, 0, 4, BOTH);
    recorded(run, 1, 2, TIDE_HEARD_VERIFIED);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 0);

    run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, reads);
    sent(run, 3, 0, 0, 0xC, BOTH);
    recorded(run, 0, 4, BOTH);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 1 && count(&out, TIDE_HEARD_PREDICTED, 3) == 1);
    tide_event_log_clear(&out);

    run = tide_hearing_begin(&h, 5);
    sent(run, 1, 10, 100, 0xA, reads);
    sent(run, 4, 10, 100, 0xD, reads);
    recorded(run, 0, 5, BOTH);
    recorded(run, 1, 2, TIDE_HEARD_VERIFIED);
    tide_hearing_end(&h, 5);
    TIDE_CHECK(out.count == 2);
    event = find(&out, 4, &at);
    TIDE_REQUIRE(event != NULL);
    TIDE_CHECK(tide_heard_row_count(tide_event_log_rows(&out, at, event, 0), 8) == 5);

    run = tide_hearing_begin(&h, 6);
    sent(run, 5, 0, 0, 0xE, reads);
    recorded(run, 1, 7, BOTH);
    tide_hearing_end(&h, 6);
    event = find(&out, 4, &at);
    TIDE_REQUIRE(event != NULL);
    TIDE_CHECK(tide_event_log_rows(&out, at, event, 1) == NULL);
    event = find(&out, 5, &at);
    TIDE_REQUIRE(event != NULL);
    TIDE_CHECK(tide_heard_row_count(tide_event_log_rows(&out, at, event, 1), 8) == 7);
    tide_event_log_clear(&out);

    tide_hearing_confirm(&h, 5);
    TIDE_CHECK(out.count == 4 && count(&out, TIDE_HEARD_VERIFIED, 0) == 4);
    event = find(&out, 4, &at);
    TIDE_REQUIRE(event != NULL);
    TIDE_CHECK(tide_heard_row_count(tide_event_log_rows(&out, at, event, 0), 8) == 5);
    rows = tide_event_log_rows(&out, at, event, 1);
    TIDE_CHECK(tide_heard_row_count(rows, 8) == 2 && rows && ((const uint64_t *)(rows + 1))[1] == 101);
    tide_hearing_free(&h);
    tide_event_log_free(&out);
}
