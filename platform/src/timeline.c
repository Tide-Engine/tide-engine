// A match's ticks, recorded, kept and shown again (tide/timeline.h).

#include "tide/timeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/migrate.h"
#include "tide/page.h"

#define KEEP_DEFAULT 600u
// Bytes of records kept: past them the recording starts again from the first
// world it keeps, so a long run doesn't grow without end (64 MiB is hours of a
// match whose ticks take a few dozen bytes each)
#define RECORDS_MOST ((size_t)64u << 20)

// A recording as bytes: "TIDETIME", the version, the game's layout hash and
// simulation, the tick rate, the player, the first tick, how its worlds are
// packed (1 as a delta from nothing, tide_game.pack_delta, which every
// platform reads; 2 as the raw world, for a game without one), the layout it
// was recorded with (tide_layout_pack: its size, 0 for none, and bytes), its
// first world, the records, each its size and bytes, then the jumps (the
// worlds a debugger made the match go back to), each its tick and its world.
// Numbers are little-endian u32s and u64s. A world is its size and bytes as
// packed, then its size and bytes as its layout has them
// (tide_game.pack_world; size 0 without a layout), which a build of another
// layout carries over by name. Version 1 had no layout and no second form of
// a world, and still loads into a build of its layout.
static const char MAGIC[8] = {'T', 'I', 'D', 'E', 'T', 'I', 'M', 'E'};
#define VERSION 2u
#define PACKED_DELTA 1u
#define PACKED_RAW 2u

typedef struct kept {
    void *world;
    uint32_t tick;
    bool used;
    tide_event_log heard; // A match followed: what the tick that made it sent (tide_timeline_follow)
    void **beside;        // ...and a sharded one's other worlds drawn beside it, with their shards
    uint32_t *beside_shards;
    uint32_t beside_count, beside_room;
} kept;

// The match went on from `world` as tick `tick`'s, whatever replaying makes of it (tide_timeline_cut)
typedef struct jump {
    uint32_t tick;
    void *world;
} jump;

struct tide_timeline {
    const tide_game *game;
    const tide_layout *layout; // The game's, which its recordings carry (tide_timeline_desc.layout), or NULL
    const tide_jobs *jobs;
    uint32_t keep;
    bool unchecked;  // A recording of another layout, carried over: its ticks' hashes aren't of this build's worlds
    bool started;
    bool following;  // A match another machine runs: no records, the worlds kept alone (tide_timeline_follow)
    uint32_t first;  // The tick it starts at...
    void *start;     // ...and its world
    uint8_t **records;
    uint32_t *sizes;
    uint32_t count, room;
    size_t record_bytes;
    jump *jumps;     // In the order of their ticks, after first
    uint32_t jump_count;
    kept *kept;      // The worlds of the latest ticks made, by tick % keep: those from made + 1 - keep to made
    uint32_t made;
    uint32_t server_next; // The server's tick whose record comes next (UINT32_MAX: whichever, after a cut)
    void *seek;      // A world made again from the start, before the ones kept...
    uint32_t seek_tick;
    bool seeking;    // ...while it's at seek_tick
    void *seek_before; // ...and the one it was made from, for what its tick sent (tide_timeline_heard)
    bool seek_had;     // ...while that's the world after seek_tick - 1 ticks
    tide_timeline_check check;
};

static void *new_world(const tide_timeline *t)
{
    void *world = calloc(1, t->game->world_size ? t->game->world_size : 1u);
    if (!world) tide_out_of_memory();
    return world;
}

static void drop_world(const tide_timeline *t, void *world)
{
    if (!world) return;
    if (t->game->free_world) t->game->free_world(world);
    free(world);
}

static void drop_beside(const tide_timeline *t, kept *k)
{
    for (uint32_t i = 0; i < k->beside_count; i++) drop_world(t, k->beside[i]);
    k->beside_count = 0;
}

static void drop_jumps_after(tide_timeline *t, const uint32_t tick)
{
    while (t->jump_count && t->jumps[t->jump_count - 1u].tick > tick) {
        t->jump_count--;
        drop_world(t, t->jumps[t->jump_count].world);
    }
}

// Everything it has let go, but its kept slots' room
static void clear(tide_timeline *t)
{
    drop_world(t, t->start);
    t->start = NULL;
    drop_world(t, t->seek);
    t->seek = NULL;
    t->seeking = false;
    drop_world(t, t->seek_before);
    t->seek_before = NULL;
    t->seek_had = false;
    for (uint32_t i = 0; i < t->keep; i++) {
        drop_world(t, t->kept[i].world);
        tide_event_log_free(&t->kept[i].heard);
        drop_beside(t, &t->kept[i]);
        free(t->kept[i].beside);
        free(t->kept[i].beside_shards);
        t->kept[i] = (kept){0};
    }
    t->following = false;
    for (uint32_t i = 0; i < t->count; i++) free(t->records[i]);
    t->count = 0;
    t->record_bytes = 0;
    drop_jumps_after(t, 0);
    t->started = false;
    t->first = t->made = 0;
    t->check = (tide_timeline_check){0};
}

tide_timeline *tide_timeline_create(const tide_timeline_desc *desc)
{
    if (!desc || !desc->game || !desc->game->copy_world) return NULL;
    tide_timeline *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->game = desc->game;
    t->layout = desc->layout;
    t->jobs = desc->jobs;
    t->keep = desc->keep ? desc->keep : KEEP_DEFAULT;
    if (t->keep < 2u) t->keep = 2u; // A tick's world and the one it's made from
    t->kept = calloc(t->keep, sizeof *t->kept);
    if (!t->kept) {
        free(t);
        return NULL;
    }
    return t;
}

void tide_timeline_destroy(tide_timeline *t)
{
    if (!t) return;
    clear(t);
    free(t->records);
    free(t->sizes);
    free(t->jumps);
    free(t->kept);
    free(t);
}

static void *kept_at(const tide_timeline *t, const uint32_t tick)
{
    const kept *k = &t->kept[tick % t->keep];
    return k->used && k->tick == tick ? k->world : NULL;
}

// The slot for `tick`'s world, emptied of the one it had
static void *slot_for(tide_timeline *t, const uint32_t tick)
{
    kept *k = &t->kept[tick % t->keep];
    if (!k->world) k->world = new_world(t);
    k->used = false;
    k->tick = tick;
    tide_event_log_clear(&k->heard);
    drop_beside(t, k);
    return k->world;
}

static void keep_world(tide_timeline *t, const uint32_t tick, const void *world)
{
    void *to = slot_for(t, tick);
    t->game->copy_world(to, world);
    t->kept[tick % t->keep].used = true;
}

static const jump *jump_at(const tide_timeline *t, const uint32_t tick)
{
    for (uint32_t i = 0; i < t->jump_count; i++) {
        if (t->jumps[i].tick == tick) return &t->jumps[i];
    }
    return NULL;
}

static void add_jump(tide_timeline *t, const uint32_t tick, const void *world)
{
    jump *grown = realloc(t->jumps, ((size_t)t->jump_count + 1u) * sizeof *grown);
    if (!grown) tide_out_of_memory();
    t->jumps = grown;
    void *copy = new_world(t);
    t->game->copy_world(copy, world);
    t->jumps[t->jump_count++] = (jump){tick, copy};
}

uint32_t tide_timeline_kept(const tide_timeline *t)
{
    if (!t || !t->started) return 0;
    const uint32_t from = t->made + 1u >= t->keep ? t->made + 1u - t->keep : 0u;
    return from > t->first ? from : t->first;
}

void tide_timeline_begin(tide_timeline *t, const void *world, const uint32_t tick)
{
    if (!t || !world) return;
    clear(t);
    t->started = true;
    t->first = t->made = tick;
    t->server_next = tick;
    t->start = new_world(t);
    t->game->copy_world(t->start, world);
    keep_world(t, tick, world);
    t->check.checked = tick;
}

static void add_record(tide_timeline *t, const uint8_t *record, const uint32_t size)
{
    if (t->count == t->room) {
        const uint32_t room = t->room ? 2u * t->room : 1024u;
        uint8_t **records = realloc(t->records, room * sizeof *records);
        if (!records) tide_out_of_memory();
        t->records = records;
        uint32_t *sizes = realloc(t->sizes, room * sizeof *sizes);
        if (!sizes) tide_out_of_memory();
        t->sizes = sizes;
        t->room = room;
    }
    uint8_t *copy = malloc(size ? size : 1u);
    if (!copy) tide_out_of_memory();
    memcpy(copy, record, size);
    t->records[t->count] = copy;
    t->sizes[t->count] = size;
    t->count++;
    t->record_bytes += size;
}

// The recording starts again from the first world it keeps, letting the records (and jumps) before it go
static void trim(tide_timeline *t)
{
    const uint32_t from = tide_timeline_kept(t);
    const void *world = kept_at(t, from);
    if (from <= t->first || !world) return;
    t->game->copy_world(t->start, world);
    const uint32_t gone = from - t->first;
    for (uint32_t i = 0; i < gone; i++) {
        t->record_bytes -= t->sizes[i];
        free(t->records[i]);
    }
    memmove(t->records, t->records + gone, (size_t)(t->count - gone) * sizeof *t->records);
    memmove(t->sizes, t->sizes + gone, (size_t)(t->count - gone) * sizeof *t->sizes);
    t->count -= gone;
    uint32_t kept_jumps = 0;
    for (uint32_t i = 0; i < t->jump_count; i++) {
        if (t->jumps[i].tick > from) t->jumps[kept_jumps++] = t->jumps[i];
        else drop_world(t, t->jumps[i].world); // The start is what it made
    }
    t->jump_count = kept_jumps;
    t->first = from;
    t->seeking = false;
    if (t->check.checked < from) t->check.checked = from;
}

void tide_timeline_tick(tide_timeline *t, const uint32_t tick, const uint8_t *record, const uint32_t size, const void *world)
{
    if (!t || !t->started || t->following || !record || !world || t->made != t->first + t->count) return;
    if (t->server_next != UINT32_MAX && tick != t->server_next) return;
    t->server_next = tick + 1u;
    add_record(t, record, size);
    keep_world(t, t->made + 1u, world);
    t->made++;
    t->check.checked = t->made; // The server made it: it's its own world
    if (t->record_bytes > RECORDS_MOST) trim(t);
}

void tide_timeline_follow(tide_timeline *t, const uint32_t tick, const void *world, const tide_event_log *heard)
{
    if (!t || !world) return;
    if (!t->started || !t->following || tick != t->made + 1u) { // From here: nothing says what came between
        tide_timeline_begin(t, world, tick);
        t->following = true;
        return;
    }
    keep_world(t, tick, world);
    tide_event_log *log = &t->kept[tick % t->keep].heard;
    uint32_t at = 0;
    for (const tide_heard *one; heard && (one = tide_event_log_next(heard, &at));) tide_event_log_copy(log, heard, one, one->as);
    t->made = tick;
    t->first = tide_timeline_kept(t); // Nothing before the worlds it keeps
    t->check.checked = tick;
}

void tide_timeline_follow_beside(tide_timeline *t, const uint32_t tick, const uint32_t count, const void *const *worlds,
                                 const uint32_t *shards)
{
    if (!t || !t->started || !t->following || tick != t->made) return;
    kept *k = &t->kept[tick % t->keep];
    if (!k->used || k->tick != tick) return;
    drop_beside(t, k);
    if (count > k->beside_room) {
        void **grown = realloc(k->beside, count * sizeof *grown);
        if (!grown) tide_out_of_memory();
        k->beside = grown;
        uint32_t *numbers = realloc(k->beside_shards, count * sizeof *numbers);
        if (!numbers) tide_out_of_memory();
        k->beside_shards = numbers;
        k->beside_room = count;
    }
    for (uint32_t i = 0; i < count; i++) {
        k->beside[i] = new_world(t);
        t->game->copy_world(k->beside[i], worlds[i]);
        k->beside_shards[i] = shards[i];
    }
    k->beside_count = count;
}

uint32_t tide_timeline_beside(const tide_timeline *t, const uint32_t tick, const void **worlds, uint32_t *shards,
                              const uint32_t capacity)
{
    if (!t || !t->started) return 0;
    const kept *k = &t->kept[tick % t->keep];
    if (!k->used || k->tick != tick) return 0;
    const uint32_t count = k->beside_count < capacity ? k->beside_count : capacity;
    for (uint32_t i = 0; i < count; i++) {
        worlds[i] = k->beside[i];
        shards[i] = k->beside_shards[i];
    }
    return count;
}

bool tide_timeline_follows(const tide_timeline *t)
{
    return t && t->started && t->following;
}

static void recorder_begin(void *user, const void *world, const uint32_t tick)
{
    tide_timeline_begin(user, world, tick);
}

static void recorder_tick(void *user, const uint32_t tick, const uint8_t *record, const uint32_t size, const void *world)
{
    tide_timeline_tick(user, tick, record, size, world);
}

static void recorder_verified(void *user, const uint32_t tick, const void *world, const tide_event_log *heard)
{
    tide_timeline_follow(user, tick, world, heard);
}

static void recorder_beside(void *user, const uint32_t tick, const uint32_t count, const void *const *worlds,
                            const uint32_t *shards)
{
    tide_timeline_follow_beside(user, tick, count, worlds, shards);
}

tide_session_recorder tide_timeline_recorder(tide_timeline *t)
{
    return (tide_session_recorder){.user = t, .begin = recorder_begin, .tick = recorder_tick, .verified = recorder_verified,
                                   .beside = recorder_beside};
}

void tide_timeline_set_game(tide_timeline *t, const tide_game *game)
{
    if (!t || !game) return;
    // What a followed match's ticks sent is laid out as the build that heard it has it: not this one's to hear
    for (uint32_t i = 0; t->game != game && i < t->keep; i++) tide_event_log_clear(&t->kept[i].heard);
    t->game = game;
}

void tide_timeline_set_layout(tide_timeline *t, const tide_layout *layout)
{
    if (t) t->layout = layout;
}

bool tide_timeline_started(const tide_timeline *t)
{
    return t && t->started;
}

uint32_t tide_timeline_first(const tide_timeline *t)
{
    return t && t->started ? t->first : 0u;
}

uint32_t tide_timeline_last(const tide_timeline *t)
{
    return !t || !t->started ? 0u : t->following ? t->made : t->first + t->count;
}

uint32_t tide_timeline_made(const tide_timeline *t)
{
    return t && t->started ? t->made : 0u;
}

// `world`, the world after `tick` ticks, made the world after the next, and that world jumped to where the match
// went on from there: false for a record this build can't replay
static bool replay(tide_timeline *t, void *world, const uint32_t tick)
{
    uint64_t expected = 0, got = 0;
    const uint32_t i = tick - t->first;
    const tide_replay_result result = tide_replay_check(t->game, world, t->records[i], t->sizes[i], t->jobs, &expected, &got);
    const uint32_t made = tick + 1u;
    if (result == TIDE_REPLAY_UNREADABLE) {
        if (!t->check.unreadable || made < t->check.unreadable) t->check.unreadable = made;
        return false;
    }
    if (!t->unchecked && result == TIDE_REPLAY_DIFFERENT && (!t->check.differs || made < t->check.differs)) {
        t->check.differs = made;
        t->check.expected = expected;
        t->check.got = got;
    }
    if (!t->unchecked && made > t->check.checked) t->check.checked = made;
    const jump *j = jump_at(t, made);
    if (j) t->game->copy_world(world, j->world);
    return true;
}

const void *tide_timeline_world(tide_timeline *t, const uint32_t tick)
{
    if (!t || !t->started || tick < t->first || tick > tide_timeline_last(t)) return NULL;
    const void *world = kept_at(t, tick);
    if (world || t->following) return world;
    if (tick > t->made) { // Onwards from the last one made, each kept
        for (uint32_t at = t->made; at < tick; at++) {
            const void *from = kept_at(t, at);
            if (!from) return NULL;
            void *to = slot_for(t, at + 1u);
            t->game->copy_world(to, from);
            if (!replay(t, to, at)) return NULL;
            t->kept[(at + 1u) % t->keep].used = true;
            t->made = at + 1u;
        }
        return kept_at(t, tick);
    }
    // Before the ones it keeps: made again from the start, or from the last one made so, if it's before this
    if (!t->seeking || t->seek_tick > tick) {
        if (!t->seek) t->seek = new_world(t);
        t->game->copy_world(t->seek, t->start);
        t->seek_tick = t->first;
        t->seeking = true;
        t->seek_had = false;
    }
    while (t->seek_tick < tick) {
        t->seek_had = t->seek_tick + 1u == tick; // The last one before it, kept: a copy shares its pages
        if (t->seek_had) {
            if (!t->seek_before) t->seek_before = new_world(t);
            t->game->copy_world(t->seek_before, t->seek);
        }
        if (!replay(t, t->seek, t->seek_tick)) {
            t->seeking = false;
            return NULL;
        }
        t->seek_tick++;
    }
    return t->seek;
}

bool tide_timeline_heard(tide_timeline *t, const uint32_t tick, tide_event_log *heard)
{
    if (!t || !t->started || !heard || tick <= t->first || tick > tide_timeline_last(t)) return false;
    if (t->following) {
        const kept *k = &t->kept[tick % t->keep];
        if (!k->used || k->tick != tick) return false;
        uint32_t at = 0;
        for (const tide_heard *one; (one = tide_event_log_next(&k->heard, &at));) tide_event_log_copy(heard, &k->heard, one, one->as);
        return true;
    }
    const void *before = kept_at(t, tick - 1u);
    if (!before && t->seeking && t->seek_had && t->seek_tick == tick) before = t->seek_before;
    if (!before) before = tide_timeline_world(t, tick - 1u);
    if (!before) return false;
    void *world = new_world(t);
    t->game->copy_world(world, before);
    uint64_t expected = 0, got = 0;
    const uint32_t i = tick - 1u - t->first;
    const tide_replay_result result =
        tide_replay_heard(t->game, world, t->records[i], t->sizes[i], t->jobs, heard, &expected, &got);
    drop_world(t, world);
    return result != TIDE_REPLAY_UNREADABLE;
}

bool tide_timeline_cut(tide_timeline *t, const uint32_t tick, const void *world)
{
    if (!t || !t->started || t->following || !world || tick < t->first || tick > tide_timeline_last(t)) return false;
    const void *made = tide_timeline_world(t, tick);
    const bool same = made && t->game->hash_world(made) == t->game->hash_world(world);
    drop_jumps_after(t, tick > t->first ? tick - 1u : 0u); // A jump at `tick` is this one's now
    for (uint32_t i = tick - t->first; i < t->count; i++) {
        t->record_bytes -= t->sizes[i];
        free(t->records[i]);
    }
    t->count = tick - t->first;
    for (uint32_t i = 0; i < t->keep; i++) {
        if (t->kept[i].used && t->kept[i].tick > tick) t->kept[i].used = false;
    }
    keep_world(t, tick, world);
    if (tick == t->first) t->game->copy_world(t->start, world);
    else if (!same) add_jump(t, tick, world);
    t->made = tick;
    t->server_next = UINT32_MAX;
    t->seeking = false;
    if (t->check.checked > tick) t->check.checked = tick;
    if (t->check.differs > tick) {
        t->check.differs = 0;
        t->check.expected = t->check.got = 0;
    }
    if (t->check.unreadable > tick) t->check.unreadable = 0;
    return true;
}

void tide_timeline_check_of(const tide_timeline *t, tide_timeline_check *out)
{
    *out = t ? t->check : (tide_timeline_check){0};
}

// Bytes, little-endian
static void put_u32(uint8_t *at, const uint32_t v)
{
    for (uint32_t i = 0; i < 4u; i++) at[i] = (uint8_t)(v >> (8u * i));
}

static void put_u64(uint8_t *at, const uint64_t v)
{
    for (uint32_t i = 0; i < 8u; i++) at[i] = (uint8_t)(v >> (8u * i));
}

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static uint64_t get_u64(const uint8_t *at)
{
    return (uint64_t)get_u32(at) | (uint64_t)get_u32(at + 4) << 32;
}

// A world as bytes, to free(), as the recording packs them
static uint8_t *pack(const tide_game *g, const void *world, const uint32_t packed, uint32_t *size)
{
    *size = 0;
    if (packed == PACKED_DELTA) return g->pack_delta(world, NULL, NULL, 0, false, size);
    const uint32_t n = g->pack_world(world, NULL, 0);
    uint8_t *bytes = malloc(n ? n : 1u);
    if (bytes) g->pack_world(world, bytes, n);
    *size = n;
    return bytes;
}

static bool unpack(const tide_game *g, void *world, const uint32_t packed, const uint8_t *bytes, const uint32_t size)
{
    return packed == PACKED_DELTA ? g->unpack_delta && g->unpack_delta(world, NULL, bytes, size)
         : packed == PACKED_RAW && g->unpack_world && g->unpack_world(world, bytes, size);
}

#define HEAD_SIZE (8u + 4u + 8u + 32u + 4u + 4u + 4u + 4u)

// A world as the build that recorded it lays it out (tide_game.pack_world), for a build of another layout to carry
// over: only where the recording carries its layout too
static uint8_t *pack_raw(const tide_timeline *t, const void *world, uint32_t *size)
{
    *size = 0;
    if (!t->layout || !t->game->pack_world) return NULL;
    const uint32_t n = t->game->pack_world(world, NULL, 0);
    uint8_t *bytes = malloc(n ? n : 1u);
    if (bytes) t->game->pack_world(world, bytes, n);
    *size = bytes ? n : 0u;
    return bytes;
}

uint8_t *tide_timeline_save(tide_timeline *t, const uint32_t tick_rate, const uint32_t player, uint32_t *size)
{
    if (size) *size = 0;
    // Not one carried over from another layout: its records' hashes are of that layout's worlds, which a recording
    // of this build's layout would be checked against
    if (!t || !t->started || t->following || t->unchecked || !size) return NULL;
    const tide_game *g = t->game;
    const uint32_t packed = g->pack_delta ? PACKED_DELTA : PACKED_RAW;
    if (packed == PACKED_RAW && !g->pack_world) return NULL;
    uint32_t layout_size = 0;
    void *layout = t->layout && g->pack_world ? tide_layout_pack(t->layout, &layout_size) : NULL;
    if (!layout) layout_size = 0;
    // Its first world and its jumps' worlds as bytes, and as their layout has them
    const uint32_t worlds = 1u + t->jump_count;
    uint8_t **bytes = calloc(worlds, sizeof *bytes), **raw = calloc(worlds, sizeof *raw);
    uint32_t *sizes = calloc(worlds, sizeof *sizes), *raw_sizes = calloc(worlds, sizeof *raw_sizes);
    bool ok = bytes && raw && sizes && raw_sizes;
    for (uint32_t i = 0; ok && i < worlds; i++) {
        const void *world = i ? t->jumps[i - 1u].world : t->start;
        bytes[i] = pack(g, world, packed, &sizes[i]);
        ok = bytes[i] != NULL;
        if (ok && layout) {
            raw[i] = pack_raw(t, world, &raw_sizes[i]);
            ok = raw[i] != NULL;
        }
    }
    uint64_t total = (uint64_t)HEAD_SIZE + 4u + layout_size + 4u + 4u;
    for (uint32_t i = 0; ok && i < worlds; i++) total += 4u + (uint64_t)sizes[i] + 4u + (uint64_t)raw_sizes[i] + (i ? 4u : 0u);
    for (uint32_t i = 0; i < t->count; i++) total += 4u + (uint64_t)t->sizes[i];
    uint8_t *out = ok && total <= UINT32_MAX ? malloc((size_t)total) : NULL;
    if (out) {
        uint8_t *at = out;
        memcpy(at, MAGIC, 8), at += 8;
        put_u32(at, VERSION), at += 4;
        put_u64(at, g->hash), at += 8;
        memcpy(at, g->simulation, 32), at += 32;
        put_u32(at, tick_rate), at += 4;
        put_u32(at, player), at += 4;
        put_u32(at, t->first), at += 4;
        put_u32(at, packed), at += 4;
        put_u32(at, layout_size), at += 4;
        if (layout_size) memcpy(at, layout, layout_size), at += layout_size;
        put_u32(at, sizes[0]), at += 4;
        memcpy(at, bytes[0], sizes[0]), at += sizes[0];
        put_u32(at, raw_sizes[0]), at += 4;
        if (raw_sizes[0]) memcpy(at, raw[0], raw_sizes[0]), at += raw_sizes[0];
        put_u32(at, t->count), at += 4;
        for (uint32_t i = 0; i < t->count; i++) {
            put_u32(at, t->sizes[i]), at += 4;
            memcpy(at, t->records[i], t->sizes[i]), at += t->sizes[i];
        }
        put_u32(at, t->jump_count), at += 4;
        for (uint32_t i = 1; i < worlds; i++) {
            put_u32(at, t->jumps[i - 1u].tick), at += 4;
            put_u32(at, sizes[i]), at += 4;
            memcpy(at, bytes[i], sizes[i]), at += sizes[i];
            put_u32(at, raw_sizes[i]), at += 4;
            if (raw_sizes[i]) memcpy(at, raw[i], raw_sizes[i]), at += raw_sizes[i];
        }
        *size = (uint32_t)total;
    }
    for (uint32_t i = 0; i < worlds; i++) {
        if (bytes) free(bytes[i]);
        if (raw) free(raw[i]);
    }
    free(bytes);
    free(raw);
    free(sizes);
    free(raw_sizes);
    free(layout);
    return out;
}

// A reader of a recording's bytes, which says it's cut short rather than reading past them
typedef struct reader {
    const uint8_t *at, *end;
    bool short_;
} reader;

static uint32_t read_u32(reader *r)
{
    if ((size_t)(r->end - r->at) < 4u) {
        r->short_ = true;
        r->at = r->end;
        return 0;
    }
    const uint32_t v = get_u32(r->at);
    r->at += 4;
    return v;
}

static const uint8_t *read_bytes(reader *r, const uint32_t n)
{
    if ((size_t)(r->end - r->at) < n) {
        r->short_ = true;
        r->at = r->end;
        return NULL;
    }
    const uint8_t *bytes = r->at;
    r->at += n;
    return bytes;
}

// A recording's world as this build has it: its bytes as they are for a build of its layout; for another, what the
// recording's layout says of `raw` carried over by name (tide/migrate.h), with what didn't carry counted in `about`
static bool take_world(const tide_game *g, void *world, const uint32_t packed, const uint8_t *bytes, const uint32_t size,
                       const tide_layout *old, const tide_layout *now, const uint8_t *raw, const uint32_t raw_size,
                       tide_timeline_about *about, char *why, const size_t room)
{
    if (!old) return unpack(g, world, packed, bytes, size);
    void *carried = NULL;
    uint32_t carried_size = 0;
    tide_migration m = {0};
    bool ok = raw && tide_migrate_world(old, &old->match, raw, raw_size, now, &now->match, &carried, &carried_size, &m);
    if (ok && !g->unpack_world(world, carried, carried_size)) {
        snprintf(m.failed, sizeof m.failed, "this build couldn't take what was carried over");
        ok = false;
    }
    free(carried);
    about->entities_dropped += m.entities_dropped;
    about->tasks_dropped += m.tasks_dropped;
    if (!ok && m.failed[0]) snprintf(why, room, "a world of it can't be carried over to this build's layout: %s", m.failed);
    return ok;
}

tide_timeline *tide_timeline_load(const tide_timeline_desc *desc, const uint8_t *data, const size_t size,
                                  tide_timeline_about *about, char *why, const size_t why_size)
{
    tide_timeline_about none;
    if (!about) about = &none;
    *about = (tide_timeline_about){0};
    char unused[1];
    const size_t room = why && why_size ? why_size : sizeof unused;
    if (!why || !why_size) why = unused;
    why[0] = '\0';
    if (!desc || !desc->game) return NULL;
    if (!data || size < HEAD_SIZE || memcmp(data, MAGIC, 8) != 0) {
        snprintf(why, room, "it isn't a recording of a match");
        return NULL;
    }
    const tide_game *g = desc->game;
    const uint8_t *at = data + 8;
    const uint32_t version = get_u32(at);
    if (version != VERSION && version != 1u) {
        snprintf(why, room, "it's a recording of version %u, and this tide reads versions 1 and %u", (unsigned)version, VERSION);
        return NULL;
    }
    at += 4;
    const bool same_layout = get_u64(at) == g->hash;
    at += 8;
    about->other_build = memcmp(at, g->simulation, 32) != 0;
    at += 32;
    reader r = {at, data + size, false};
    about->tick_rate = read_u32(&r);
    about->player = read_u32(&r);
    const uint32_t first = read_u32(&r);
    const uint32_t packed = read_u32(&r);
    // Version 2 on: the layout it was recorded with, and each world as that layout has it, where it had one to say
    const uint32_t layout_size = version >= 2u ? read_u32(&r) : 0u;
    const uint8_t *layout_bytes = read_bytes(&r, layout_size);
    const uint32_t world_size = read_u32(&r);
    const uint8_t *world = read_bytes(&r, world_size);
    const uint32_t raw_size = version >= 2u ? read_u32(&r) : 0u;
    const uint8_t *raw = read_bytes(&r, raw_size);
    if (r.short_ || !world) {
        snprintf(why, room, "it's cut short");
        return NULL;
    }
    // A build of another layout carries its worlds over, by what the recording says its layout was
    void *old_block = NULL;
    const tide_layout *old = NULL;
    if (!same_layout) {
        if (!layout_size || !desc->layout || !g->unpack_world) {
            snprintf(why, room, "it was recorded by a build whose data is laid out otherwise (a component, input, command or "
                                "setting changed)%s: replay it with a build of that layout",
                     layout_size ? "" : ", and it doesn't say how (an older tide's recording, or a game without a layout to say)");
            return NULL;
        }
        old_block = malloc(layout_size); // 8-aligned, as the layout's own block was
        if (old_block) memcpy(old_block, layout_bytes, layout_size);
        old = old_block ? tide_layout_unpack(old_block, layout_size) : NULL;
        if (!old) {
            free(old_block);
            snprintf(why, room, "the layout it says it was recorded with didn't read");
            return NULL;
        }
        about->other_layout = true;
        about->fields_reset = tide_layout_fields_reset(old, desc->layout);
    }
    tide_timeline *t = tide_timeline_create(desc);
    if (!t) {
        free(old_block);
        snprintf(why, room, "there wasn't the memory");
        return NULL;
    }
    void *start = new_world(t);
    const bool unpacked = take_world(g, start, packed, world, world_size, old, desc->layout, raw_size ? raw : NULL, raw_size,
                                     about, why, room);
    if (unpacked) tide_timeline_begin(t, start, first);
    drop_world(t, start);
    if (!unpacked) {
        tide_timeline_destroy(t);
        free(old_block);
        if (!why[0]) snprintf(why, room, "its first world isn't one this build can read");
        return NULL;
    }
    const uint32_t count = read_u32(&r);
    if (count > UINT32_MAX - first) r.short_ = true;
    for (uint32_t i = 0; i < count && !r.short_; i++) {
        const uint32_t n = read_u32(&r);
        const uint8_t *record = read_bytes(&r, n);
        if (record) add_record(t, record, n);
    }
    const uint32_t jumps = read_u32(&r);
    uint32_t after = first;
    for (uint32_t i = 0; i < jumps && !r.short_; i++) {
        const uint32_t tick = read_u32(&r);
        const uint32_t n = read_u32(&r);
        const uint8_t *bytes = read_bytes(&r, n);
        const uint32_t raw_n = version >= 2u ? read_u32(&r) : 0u;
        const uint8_t *raw_bytes = read_bytes(&r, raw_n);
        if (!bytes || r.short_) break;
        void *to = new_world(t);
        const bool ok = tick > after && tick <= first + count
                     && take_world(g, to, packed, bytes, n, old, desc->layout, raw_n ? raw_bytes : NULL, raw_n, about, why, room);
        if (ok) add_jump(t, tick, to);
        drop_world(t, to);
        if (!ok) {
            tide_timeline_destroy(t);
            free(old_block);
            if (!why[0]) snprintf(why, room, "a world it goes back to isn't one this build can read");
            return NULL;
        }
        after = tick;
    }
    free(old_block);
    if (r.short_ || r.at != r.end) {
        tide_timeline_destroy(t);
        snprintf(why, room, r.short_ ? "it's cut short" : "it has bytes after its end");
        return NULL;
    }
    t->unchecked = !same_layout; // Its hashes are of worlds laid out otherwise: nothing here to check a tick against
    t->check.checked = first; // Nothing replayed yet
    return t;
}
