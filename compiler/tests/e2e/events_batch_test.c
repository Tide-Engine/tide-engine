#include <stdlib.h>

#include "game.h"
#include "entity_order_test.h"
#include "tide/jobs.h"
#include "tide_test.h"

static tide_world *observed;
static int32_t bad_records;
static int32_t calls;

// C is allowed to observe the queue during expression evaluation. A hidden
// call in a default or expanded constant must not see reserved empty events.
int32_t event_mark(const int32_t value)
{
    calls++;
    const tide_queue *q = tide_recording(&observed->commands);
    tide_queue_place at = {q->pages ? q->pages - 1u : 0u, 0u};
    const tide_command *c;
    while ((c = tide_queue_next(q, &at))) {
        if (c->kind == 0u) bad_records++; // A reserved, unfinished event is still zeroed.
    }
    return value;
}

static void work(void *self, void (*run)(void *, uint32_t), void *context)
{
    (void)self;
    run(context, 0u);
}

static void check_trace(const tide_world *w, const entity_order_value *senders, const int32_t sender_count)
{
    int32_t at = 0;
    for (int32_t group = 1; group <= 9; group++) {
        for (int32_t index = 0; index < sender_count; index++) {
            const int32_t i = senders[index].value;
            if ((group == 2 && i % 2 != 0) || (group == 3 && i % 2 == 0)) continue;
            const int32_t got = *(const int32_t *)tide_list_read(&w->heap, w->Audit.trace, at++, sizeof got);
            TIDE_CHECK(got == group * 100000 + i);
        }
    }
    // Handlers append behind the entire recorded tick, across page boundaries.
    for (int32_t index = 0; index < sender_count; index++) {
        const int32_t i = senders[index].value;
        if (i % 100 != 0) continue;
        const int32_t got = *(const int32_t *)tide_list_read(&w->heap, w->Audit.trace, at++, sizeof got);
        TIDE_CHECK(got == 1000000 + i);
    }
    TIDE_CHECK(tide_list_count(w->Audit.trace) == at);
    TIDE_CHECK(w->Audit.seenBefore == 0);
    TIDE_CHECK(w->commands.count == 0);
    for (int32_t i = 0; i < sender_count; i++) TIDE_CHECK(tide_get_Health(w, senders[i].key).hits == 7);
}

TIDE_TEST(events_batches_keep_order_filters_calls_and_snapshots)
{
    tide_world one = {0}, many = {0}, snapshot = {0}, back = {0};
    tide_world_init(&one, 1.0f);
    // Derive expected send order independently from logical keys and payloads.
    // The actual trace remains an ordinary list: never sort its event records.
    entity_order_value *senders = malloc(4097u * sizeof *senders);
    TIDE_REQUIRE(senders);
    int32_t sender_count = 0;
    for (uint32_t slot = 0; slot < one.entities.next_unused; slot++) {
        const tide_entity key = tide_entity_in_slot(&one.entities, slot);
        if (!tide_has_Sender(&one, key)) continue;
        TIDE_REQUIRE(sender_count < 4097);
        senders[sender_count++] = (entity_order_value){key, tide_get_Sender(&one, key).sequence};
    }
    TIDE_REQUIRE(sender_count == 4097);
    qsort(senders, (size_t)sender_count, sizeof *senders, entity_order_compare);
    // One spawn site's entities for one cause come in the order they were made: a site's count is added to its
    // key, not hashed into it (tide_causal_next), so their rows are in the entities' order too.
    bool creation_order = true;
    for (int32_t i = 0; i < sender_count; i++) creation_order &= senders[i].value == i;
    TIDE_CHECK(creation_order);
    tide_world_copy(&many, &one);
    tide_world_copy(&snapshot, &one);
    const uint64_t before = tide_world_hash(&one);
    const uint32_t size = tide_world_pack(&one, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&one, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    free(bytes);
    const tide_jobs jobs = {NULL, 4u, work, NULL, NULL, NULL, NULL, NULL, false};
    tide_world *worlds[] = {&one, &many, &back};
    for (uint32_t i = 0; i < 3u; i++) {
        observed = worlds[i];
        calls = bad_records = 0;
        if (i == 1u) tide_world_tick_on(observed, &jobs);
        else tide_world_tick(observed);
        TIDE_CHECK(calls == 5 * 4097);
        TIDE_CHECK(bad_records == 0);
        check_trace(observed, senders, sender_count);
        TIDE_CHECK(tide_world_hash(&snapshot) == before);
        TIDE_CHECK(tide_world_hash(observed) == tide_world_hash(&one));
    }
    observed = NULL;
    free(senders);
    tide_world_free(&back);
    tide_world_free(&snapshot);
    tide_world_free(&many);
    tide_world_free(&one);
}
