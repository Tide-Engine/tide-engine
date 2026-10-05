#include "tide/events.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

void tide_event_log_clear(tide_event_log *log)
{
    log->size = 0;
    log->count = 0;
    log->first_tick = 0;
}

void tide_event_log_free(tide_event_log *log)
{
    free(log->bytes);
    *log = (tide_event_log){0};
}

static uint8_t *room_for(tide_event_log *log, const uint32_t size)
{
    if (size > UINT32_MAX - log->size) tide_out_of_memory();
    if (log->size + size > log->capacity) {
        uint32_t capacity = log->capacity ? log->capacity : 256u;
        while (capacity < log->size + size) {
            if (capacity > UINT32_MAX / 2u) tide_out_of_memory();
            capacity *= 2u;
        }
        log->bytes = tide_realloc(log->bytes, log->capacity, capacity);
        log->capacity = capacity;
    }
    return log->bytes + log->size;
}

tide_heard *tide_event_log_push(tide_event_log *log, uint32_t size, const uint32_t build)
{
    if (log->build != build) { // Another build's events mean nothing to this one
        tide_event_log_clear(log);
        log->build = build;
    }
    if (size < sizeof(tide_heard)) size = sizeof(tide_heard);
    if (size > UINT32_MAX - 7u) tide_out_of_memory();
    size = (size + 7u) & ~7u;
    tide_heard *heard = (tide_heard *)room_for(log, size);
    memset(heard, 0, size);
    heard->size = size;
    log->size += size;
    log->count++;
    return heard;
}

// What the log's first event says of when it was sent, once the event is whole.
static void note_first(tide_event_log *log, const tide_heard *heard)
{
    if (log->count == 1u) log->first_tick = heard->tick;
}

void tide_event_log_copy(tide_event_log *to, const tide_event_log *from, const tide_heard *heard, const uint8_t as)
{
    if (to->build != from->build) {
        tide_event_log_clear(to);
        to->build = from->build;
    }
    const uint32_t size = heard->size;
    tide_heard *copy = (tide_heard *)room_for(to, size);
    memcpy(copy, heard, size);
    copy->as = as;
    to->size += size;
    to->count++;
    note_first(to, copy);
}

void tide_event_log_take(tide_event_log *to, tide_event_log *from)
{
    if (from->count == 0u) return;
    if (to->build != from->build) {
        tide_event_log_clear(to);
        to->build = from->build;
    }
    memcpy(room_for(to, from->size), from->bytes, from->size);
    if (to->count == 0u) to->first_tick = from->first_tick;
    to->size += from->size;
    to->count += from->count;
    tide_event_log_clear(from);
}

// ---------------------------------------------------------------------------

void tide_hearing_init(tide_hearing *h, const uint32_t ring, tide_event_log *out)
{
    tide_hearing_free(h);
    h->ticks = tide_alloc_zeroed(ring ? ring : 1u, sizeof *h->ticks);
    h->ring = ring ? ring : 1u;
    h->out = out;
}

void tide_hearing_free(tide_hearing *h)
{
    for (uint32_t i = 0; h->ticks && i < h->ring; i++) {
        tide_event_log_free(&h->ticks[i].run);
        free(h->ticks[i].marks);
    }
    free(h->ticks);
    tide_event_log_free(&h->fresh);
    *h = (tide_hearing){0};
}

static void forget_tick(tide_heard_tick *t)
{
    tide_event_log_clear(&t->run);
    t->count = 0;
    t->used = false;
}

static tide_heard_tick *tick_of(tide_hearing *h, const uint32_t tick)
{
    tide_heard_tick *t = &h->ticks[tick % h->ring];
    if (!t->used || t->tick != tick) {
        forget_tick(t);
        t->used = true;
        t->tick = tick;
    }
    return t;
}

tide_event_log *tide_hearing_begin(tide_hearing *h, const uint32_t tick)
{
    tide_event_log *out = h->out;
    if (!out || !out->taken || !h->ticks) return NULL;
    // A host that stopped taking them hears nothing more until it asks again
    if (out->count && tick > out->first_tick && tick - out->first_tick > TIDE_HEARD_TICKS) {
        tide_event_log_clear(out);
        out->taken = false;
        return NULL;
    }
    tide_heard_tick *t = tick_of(h, tick);
    for (uint32_t i = 0; i < t->count; i++) t->marks[i].met = false;
    tide_event_log_clear(&h->fresh);
    return &h->fresh;
}

// Whether a mark is of the event `heard`: the same event, with the same
// fields, sent to the same entity. An entity is the same by what it was born
// as, where both are known (a rollback can give one's handle to another, and
// another handle to the same one), and by its handle otherwise.
static bool same_event(const tide_heard_mark *mark, const tide_heard *heard)
{
    if (mark->event != heard->event || mark->same != heard->same) return false;
    if (mark->born && heard->born) return mark->born == heard->born;
    return tide_entity_equal(mark->target, heard->target);
}

void tide_hearing_end(tide_hearing *h, const uint32_t tick)
{
    tide_heard_tick *t = tick_of(h, tick);
    tide_event_log_clear(&t->run);
    for (uint32_t at = 0; at < h->fresh.size;) {
        tide_heard *heard = (tide_heard *)(h->fresh.bytes + at);
        at += heard->size;
        heard->tick = tick;
        if (heard->wants & TIDE_HEARD_VERIFIED) tide_event_log_copy(&t->run, &h->fresh, heard, 0);
        if (!(heard->wants & TIDE_HEARD_PREDICTED)) continue;
        tide_heard_mark *mark = NULL;
        for (uint32_t i = 0; i < t->count && !mark; i++) {
            if (!t->marks[i].met && same_event(&t->marks[i], heard)) mark = &t->marks[i];
        }
        if (mark) { // The tick delivered it already
            mark->met = true;
            continue;
        }
        if (t->count == t->room) {
            const uint32_t room = t->room ? t->room * 2u : 8u;
            t->marks = tide_realloc(t->marks, (size_t)t->room * sizeof *t->marks, (size_t)room * sizeof *t->marks);
            t->room = room;
        }
        t->marks[t->count++] = (tide_heard_mark){heard->same, heard->target, heard->born, heard->event, true};
        tide_event_log_copy(h->out, &h->fresh, heard, TIDE_HEARD_PREDICTED);
    }
    tide_event_log_clear(&h->fresh);
}

void tide_hearing_confirm(tide_hearing *h, const uint32_t tick)
{
    if (!h->ticks) return;
    tide_heard_tick *t = &h->ticks[tick % h->ring];
    if (!t->used || t->tick != tick) return;
    uint32_t at = 0;
    for (const tide_heard *heard; h->out && (heard = tide_event_log_next(&t->run, &at));) {
        tide_event_log_copy(h->out, &t->run, heard, TIDE_HEARD_VERIFIED);
    }
    forget_tick(t);
}

void tide_hearing_skip_to(tide_hearing *h, const uint32_t tick)
{
    for (uint32_t i = 0; h->ticks && i < h->ring; i++) {
        if (h->ticks[i].used && h->ticks[i].tick < tick) forget_tick(&h->ticks[i]);
    }
}

void tide_hearing_rebuilt(tide_hearing *h)
{
    for (uint32_t i = 0; h->ticks && i < h->ring; i++) tide_event_log_clear(&h->ticks[i].run);
    tide_event_log_clear(&h->fresh);
    if (h->out) tide_event_log_clear(h->out);
}
