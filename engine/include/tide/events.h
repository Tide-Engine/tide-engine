#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"

// What happened in a match, for this machine's local handlers to hear
// (`local event(Hit hit) ShowHit(...)`, docs/spec.md, Events): the events a
// tick sent, recorded beside the world and never hashed or packed with it.
// Visibility sessions can send validated, recipient-filtered genuine
// lifecycle records separately. Ordinary custom-event logs stay local.
//
// A tick records what local handlers take into a log (tide_event_log), when
// it's given one: each event as its turn left it, with what its handlers read
// of the entity it was sent to and of the match's singletons, so a handler
// still has them once the entity is gone. A frame's local state takes the log
// and runs the handlers, before its views.
//
// A client runs ticks again when a prediction went wrong, and hears each event
// once all the same: it remembers what each tick it hasn't had confirmed
// delivered (tide_hearing). An event a tick sends again stays silent; one only
// the corrected tick has is heard then, late; one the correction took away
// isn't taken back. Handlers of `verified` events hear a tick's once, when the
// server confirmed it.

// Which handlers an event is for.
#define TIDE_HEARD_PREDICTED 1u // Those that hear a tick as this machine ran it
#define TIDE_HEARD_VERIFIED 2u  // Those that hear it once the server confirmed it
// In `wants`, beside those: some handler of the event reads rows its tick
// recorded (TIDE_HEARD_ROWS)
#define TIDE_HEARD_READS_ROWS 4u
// Availability has no game-authored payload. Replay may discover additional
// listeners for the same entity, which must be delivered individually.
#define TIDE_HEARD_AVAILABILITY 8u
// Genuine match Spawned/Destroyed, whose authoritative state can be journaled
// separately from the client's guessed tick.
#define TIDE_HEARD_LIFECYCLE 16u

// Not an event, in `event`: rows a tick recorded for its events' handlers,
// like the entities a handler's query goes through (`query (Body body, with
// Fighter) fighters`), as the tick left them. Once for the tick, however many
// of its events read them: after the tick's events in a log, and wherever one
// that reads rows goes, they go too. `handlers` says which rows, by the
// game's numbering, `same` how many, and they follow the tide_heard.
#define TIDE_HEARD_ROWS 0xFFFFu
// Private authoritative visibility metadata: an entity was permitted at its
// genuine destruction. No payload or listener bits. A local delivery log may
// carry it for generated frame-availability reconciliation, never game handlers.
#define TIDE_HEARD_DESTROYED_CAUSE 0xFFFEu

// An event in a log. After it come the event's own value and what its handlers
// read, as the game's generated code lays them out.
typedef struct tide_heard {
    uint32_t size;      // Its bytes in the log, what follows it included: a multiple of 8
    uint32_t tick;      // The tick that sent it
    uint16_t event;     // Which event, by the game's numbering
    uint8_t wants;      // The handlers the game has for it: TIDE_HEARD_PREDICTED, TIDE_HEARD_VERIFIED
    uint8_t as;         // In what a frame takes: which of them it's for. 0, straight from a tick, is both
    uint32_t born;      // What the entity it was sent to was born as (tide_entity_born), or 0
    tide_entity target; // The entity it was sent to, or null: the world
    uint64_t handlers;  // A bit for each local handler of the event that runs for that entity
    uint64_t same;      // A hash of its fields: with its tick, its event and its entity, what makes it the same event
} tide_heard;

typedef struct tide_event_log {
    uint8_t *bytes;
    uint32_t size;       // Bytes of events in it
    uint32_t capacity;
    uint32_t count;      // Events in it
    uint32_t first_tick; // The tick of the first one
    // Whose events they are: a build of a game lays out what follows a
    // tide_heard its own way, so another build's are of no use to it.
    uint32_t build;
    // A session's: its host takes what's in it (tide_session_events). Nothing
    // is recorded for a host that doesn't.
    bool taken;
} tide_event_log;

// A new event at the end, zeroed but for its size, with room for `size` bytes
// in all. `build` says whose it is: a log with another build's is emptied
// first.
tide_heard *tide_event_log_push(tide_event_log *log, uint32_t size, uint32_t build);

// The event at `*at`, which moves on to the one after it, or NULL at the end.
// Start from 0.
static inline const tide_heard *tide_event_log_next(const tide_event_log *log, uint32_t *at)
{
    if (*at >= log->size) return NULL;
    const tide_heard *heard = (const tide_heard *)(log->bytes + *at);
    *at += heard->size;
    return heard;
}

// The event just pushed takes `size` bytes in all, no more than it was pushed
// with: what's past them is the log's again.
void tide_event_log_trim(tide_event_log *log, tide_heard *heard, uint32_t size);

// The rows numbered `rows` that the tick of `heard` recorded for its handlers,
// or NULL: the first after it in the log, which are its run's, as a tick's
// rows come after its events. `at` is where `heard` ends.
static inline const tide_heard *tide_event_log_rows(const tide_event_log *log, uint32_t at, const tide_heard *heard,
                                                    const uint64_t rows)
{
    for (const tide_heard *next; (next = tide_event_log_next(log, &at));) {
        if (next->tick != heard->tick || next->as != heard->as) return NULL; // Another run's from here on
        if (next->event == TIDE_HEARD_ROWS && next->handlers == rows) return next;
    }
    return NULL;
}

// How many rows of `size` bytes each `rows` holds, a TIDE_HEARD_ROWS record or
// NULL: none when it's not as big as it says.
static inline uint32_t tide_heard_row_count(const tide_heard *rows, const uint32_t size)
{
    if (!rows || rows->same > (rows->size - sizeof *rows) / (size ? size : 1u)) return 0u;
    return (uint32_t)rows->same;
}

// A copy of `heard`, one of `from`'s, at the end of `to`, for the handlers
// `as` says.
void tide_event_log_copy(tide_event_log *to, const tide_event_log *from, const tide_heard *heard, uint8_t as);

// Everything in `from` goes to the end of `to`, in order, and `from` is left
// empty.
void tide_event_log_take(tide_event_log *to, tide_event_log *from);

// Empties it, keeping its memory.
void tide_event_log_clear(tide_event_log *log);
void tide_event_log_free(tide_event_log *log);

// ---------------------------------------------------------------------------
// What a client remembers of the ticks it ran and hasn't had confirmed.

// An event a tick delivered to the handlers that hear it as predicted.
typedef struct tide_heard_mark {
    uint64_t same;
    tide_entity target;
    uint32_t born;
    uint16_t event;
    bool met; // The tick's run so far sent it again
    uint64_t handlers; // Availability listeners already delivered for this identity
} tide_heard_mark;

typedef struct tide_heard_tick {
    uint32_t tick;
    bool used;
    tide_event_log run; // What its last run sent, for the handlers that hear it once it's confirmed
    tide_heard_mark *marks;
    uint32_t count;
    uint32_t room;
} tide_heard_tick;

typedef struct tide_hearing {
    tide_heard_tick *ticks; // By tick % ring
    uint32_t ring;
    tide_event_log fresh;   // The run being recorded
    tide_event_log *out;    // What its host takes, or NULL: nobody listens
} tide_hearing;

// Ticks whose events a log keeps for a host that stopped taking them (a
// session's `out`): past that, it's emptied and nothing more is recorded
// until the host asks again.
#define TIDE_HEARD_TICKS 1024u

// `ring`: at least the ticks it can be ahead of the last confirmed one, plus one.
void tide_hearing_init(tide_hearing *h, uint32_t ring, tide_event_log *out);
void tide_hearing_free(tide_hearing *h);
// Before a run of `tick`: the log to record it into, or NULL when nobody
// listens (the tick then runs as any other).
tide_event_log *tide_hearing_begin(tide_hearing *h, uint32_t tick);
// After it: what it sent that the tick hadn't delivered yet goes out, as
// predicted.
void tide_hearing_end(tide_hearing *h, uint32_t tick);
// The server confirmed `tick` as it last ran: what that run sent goes out, as
// verified, and the tick is forgotten.
void tide_hearing_confirm(tide_hearing *h, uint32_t tick);
// A world came for `tick`: the ticks before it never were confirmed one by
// one, and are forgotten. Those from it on run again, and stay.
void tide_hearing_skip_to(tide_hearing *h, uint32_t tick);
// Another build of the game runs the ticks from now on: what the last one
// recorded is of no use to it. What each tick delivered stays, so the ticks
// it runs again still stay silent.
void tide_hearing_rebuilt(tide_hearing *h);
