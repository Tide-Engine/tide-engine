#include "tide/shard.h"

#include <stdlib.h>
#include <string.h>

#include "tide/text.h"

// See tide/shard.h. What generated shard code calls: the record of a tick's
// runs (which code touched what its shard doesn't own, and so is skipped the
// next time the tick runs), Shards.Owns and Shards.delay, and byte lists.

TIDE_THREAD_LOCAL uint32_t tide_shard_code;
TIDE_THREAD_LOCAL uint32_t tide_shard_occurrence;
TIDE_THREAD_LOCAL tide_entity tide_shard_this;
TIDE_THREAD_LOCAL int32_t tide_shard_delay_now;

static uint64_t dropped;

uint64_t tide_shard_dropped(void)
{
    return __atomic_load_n(&dropped, __ATOMIC_RELAXED);
}

void tide_shard_note_dropped(const uint32_t count)
{
    __atomic_add_fetch(&dropped, count, __ATOMIC_RELAXED);
}

static TIDE_THREAD_LOCAL const void *owner_world;
static TIDE_THREAD_LOCAL tide_shard_owner owner_of;

static void lock(tide_shard_tick *t)
{
    while (__atomic_exchange_n(&t->lock, 1, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(tide_shard_tick *t)
{
    __atomic_store_n(&t->lock, 0, __ATOMIC_RELEASE);
}

static int compare(const tide_shard_attempt *a, const tide_shard_attempt *b)
{
    if (a->code != b->code) return a->code < b->code ? -1 : 1;
    if (a->entity.index != b->entity.index) return a->entity.index < b->entity.index ? -1 : 1;
    if (a->entity.generation != b->entity.generation) return a->entity.generation < b->entity.generation ? -1 : 1;
    if (a->occurrence != b->occurrence) return a->occurrence < b->occurrence ? -1 : 1;
    if (a->touched.index != b->touched.index) return a->touched.index < b->touched.index ? -1 : 1;
    if (a->touched.generation != b->touched.generation) return a->touched.generation < b->touched.generation ? -1 : 1;
    return 0;
}

static int compare_any(const void *a, const void *b)
{
    return compare(a, b);
}

void tide_shard_note(tide_shard_tick *t, const tide_entity touched)
{
    if (!t) return;
    const tide_shard_attempt a = {tide_shard_code, tide_shard_occurrence, tide_shard_this, touched};
    lock(t);
    if (!a.code) t->unknown = true; // Code that knows no way to wait: no run would leave it out
    if (t->noted_count == t->noted_room) {
        const uint32_t room = t->noted_room ? t->noted_room * 2u : 16u;
        tide_shard_attempt *grown = realloc(t->noted, (size_t)room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        t->noted = grown;
        t->noted_room = room;
    }
    t->noted[t->noted_count++] = a;
    unlock(t);
}

// The first skipped attempt of the code, entity and occurrence, or skip_count
static uint32_t first_skipped(const tide_shard_tick *t, const uint32_t code, const tide_entity entity,
                              const uint32_t occurrence)
{
    const tide_shard_attempt key = {code, occurrence, entity, {0, 0}};
    uint32_t lo = 0, hi = t->skip_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;
        if (compare(&t->skip[mid], &key) < 0) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

static bool same_run(const tide_shard_attempt *a, const uint32_t code, const tide_entity entity, const uint32_t occurrence)
{
    return a->code == code && tide_entity_equal(a->entity, entity) && a->occurrence == occurrence;
}

bool tide_shard_skips(const tide_shard_tick *t, const uint32_t code, const tide_entity entity, const uint32_t occurrence)
{
    if (!t || !t->skip_count) return false;
    const uint32_t i = first_skipped(t, code, entity, occurrence);
    return i < t->skip_count && same_run(&t->skip[i], code, entity, occurrence);
}

uint32_t tide_shard_touched(const tide_shard_tick *t, const uint32_t code, const tide_entity entity,
                            const uint32_t occurrence, tide_entity *out, const uint32_t capacity)
{
    if (!t) return 0;
    uint32_t count = 0;
    for (uint32_t i = first_skipped(t, code, entity, occurrence);
         i < t->skip_count && same_run(&t->skip[i], code, entity, occurrence); i++) {
        if (out && count < capacity) out[count] = t->skip[i].touched;
        count++;
    }
    return count;
}

bool tide_shard_tick_again(tide_shard_tick *t)
{
    if (!t->noted_count) return false;
    const uint32_t total = t->skip_count + t->noted_count;
    if (total > t->skip_room) {
        tide_shard_attempt *grown = realloc(t->skip, (size_t)total * sizeof *grown);
        if (!grown) tide_out_of_memory();
        t->skip = grown;
        t->skip_room = total;
    }
    memcpy(t->skip + t->skip_count, t->noted, (size_t)t->noted_count * sizeof *t->noted);
    // In one order whatever order threads noted them in, and once each
    qsort(t->skip, total, sizeof *t->skip, compare_any);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < total; i++) {
        if (kept && compare(&t->skip[kept - 1u], &t->skip[i]) == 0) continue;
        t->skip[kept++] = t->skip[i];
    }
    t->skip_count = kept;
    t->noted_count = 0;
    return true;
}

void tide_shard_tick_begin(tide_shard_tick *t, const uint32_t codes)
{
    t->noted_count = 0;
    t->runs++;
    if (codes > t->codes) {
        uint32_t *grown = realloc(t->occurrences, (size_t)codes * sizeof *grown);
        if (!grown) tide_out_of_memory();
        t->occurrences = grown;
        t->codes = codes;
    }
    if (t->codes) memset(t->occurrences, 0, (size_t)t->codes * sizeof *t->occurrences);
}

void tide_shard_tick_free(tide_shard_tick *t)
{
    free(t->noted);
    free(t->skip);
    free(t->occurrences);
    memset(t, 0, sizeof *t);
}

void tide_shard_use(const void *world, const tide_shard_owner owns)
{
    owner_world = world;
    owner_of = owns;
}

bool tide_shard_owns_now(const tide_entity e)
{
    return !owner_of || owner_of(owner_world, e);
}

bool tide_shard_next_outgoing(tide_reader *r, tide_shard_outgoing *out)
{
    if (r->at >= r->size) return false;
    out->route = tide_read_u32(r);
    out->target = tide_read_u32(r);
    out->entity.index = tide_read_u32(r);
    out->entity.generation = tide_read_u32(r);
    out->number = tide_read_u32(r);
    out->size = tide_read_u32(r);
    out->data = tide_read_bytes(r, out->size);
    return !r->failed && out->data && out->size;
}

const uint8_t *tide_shard_bytes_flat(const tide_list l, uint32_t *size, bool *copied)
{
    const int32_t count = tide_list_count(l);
    *size = count > 0 ? (uint32_t)count : 0u;
    *copied = false;
    if (!*size) return NULL;
    return tide_list_flatten(l, 1, false, copied);
}

void tide_shard_bytes_done(const tide_list l, const uint8_t *flat, const bool copied)
{
    if (flat) tide_list_unflatten(l, (void *)(uintptr_t)flat, 1, false, copied);
}

void tide_shard_bytes_set(tide_list *l, const void *data, const uint32_t size, const uint32_t where)
{
    if (size > INT32_MAX) tide_out_of_memory();
    const uint32_t mark = tide_scratch_mark();
    const tide_list from = tide_list_from(data, (int32_t)size, 1);
    tide_list_set(l, from, 1, where);
    tide_scratch_reset(mark);
}

void tide_shard_bytes_append(tide_list *l, const void *data, const uint32_t size, const uint32_t where)
{
    if (!size) return;
    uint32_t had;
    bool copied;
    const uint8_t *flat = tide_shard_bytes_flat(*l, &had, &copied);
    if ((uint64_t)had + size > INT32_MAX) tide_out_of_memory();
    uint8_t *all = malloc((size_t)had + size);
    if (!all) tide_out_of_memory();
    if (had) memcpy(all, flat, had);
    tide_shard_bytes_done(*l, flat, copied);
    memcpy(all + had, data, size);
    tide_shard_bytes_set(l, all, had + size, where);
    free(all);
}
