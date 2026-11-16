#include "tide/entity.h"
#include "tide/page.h"
#include "tide/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS (1u << TIDE_ENTITY_PAGE_SHIFT)
#define PAGE_BYTES (SLOTS * (uint32_t)sizeof(tide_entity_slot))
#define BORN_BYTES (SLOTS * (uint32_t)sizeof(uint32_t))
#define INDEX_SHIFT 8u
#define INDEX_ENTRIES (1u << INDEX_SHIFT)

typedef struct entity_entry
{
    tide_entity key;
    uint32_t slot, state;
} entity_entry;
_Static_assert(sizeof(tide_entity) == 16, "logical entity key layout");
_Static_assert(sizeof(tide_entity_slot) == 32, "entity storage has no padding");
#define INDEX_BYTES (INDEX_ENTRIES * (uint32_t)sizeof(entity_entry))
struct tide_entity_index
{
    uint64_t capacity;
    uint32_t count, tombs;
    struct tide_entity_index *previous;
    tide_page *page[];
};

// An order is of the table it was made in and of every copy of that table (tide_entities_copy), until each
// changes which entities it has: they share it, and the last to let go frees it. Nothing writes one once made.
struct tide_entity_order
{
    uint32_t refs; // Tables that have it as theirs or as one they retired
    uint32_t count;
    const uint32_t *slots; // A subset's (tide_entities_ordered_archetypes_slots): each key's slot, after the keys
    tide_entity keys[];
};

// One a table's change put aside: borrowed arrays stay valid until the table settles
struct tide_entity_retired_order
{
    struct tide_entity_retired_order *next;
    struct tide_entity_order *order;
};

struct tide_entity_filtered_order
{
    struct tide_entity_filtered_order *next;
    struct tide_entity_order *order;
    uint64_t at;   // The table's changes `order` has taken in (tide_entity_order_log)
    uint32_t idle; // Settles since it was asked for, while it lacked some
    uint32_t count;
    uint32_t archetypes[];
};

// What came, went or changed archetype since an order was made. A world with tens of thousands of entities has a
// few dozen come and go in a tick, and sorting every one of them again for each set of archetypes its systems go
// through was most of such a tick: an order asked for after changes is the one before with those taken in
// (order_follow), which costs a copy. The table's own, as its orders are: nothing of it is in the hash or the bytes.
#define ORDER_ABSENT 0xFFFFFFFDu // The archetype of one that isn't alive here, in a change
#define ORDER_LOG_MOST 65536u    // Changes kept at most: past them every order is made from the slots again
#define ORDER_IDLE_MOST 4u       // Settles an order nobody asks for keeps the changes it lacks
#define ORDER_COPY_MOST 4096u    // Changes a copy of the table takes with the orders that lack them
typedef struct order_change
{
    tide_entity key;
    uint32_t slot, was, now; // Its slot, and the archetype it had and has
} order_change;
struct tide_entity_order_log
{
    uint64_t first; // Changes there were before entries[0]
    uint32_t count, room;
    order_change *entries;
    uint64_t full_at; // What `ordered` has taken in, as a subset's `at`
    uint32_t full_idle;
};

static struct tide_entity_order *order_retain(struct tide_entity_order *order)
{
    if (order)
        __atomic_add_fetch(&order->refs, 1u, __ATOMIC_RELAXED);
    return order;
}
static void order_release(struct tide_entity_order *order)
{
    if (order && __atomic_sub_fetch(&order->refs, 1u, __ATOMIC_ACQ_REL) == 0u)
        free(order);
}
// The table's reference becomes its retired list's
static void order_retire(tide_entities *t, struct tide_entity_order *order)
{
    struct tide_entity_retired_order *retired = tide_alloc(sizeof *retired);
    retired->next = t->retired_order;
    retired->order = order;
    t->retired_order = retired;
}
static void filtered_invalidate(tide_entities *t)
{
    for (struct tide_entity_filtered_order *f = t->filtered_order; f; f = f->next)
    {
        if (!f->order)
            continue;
        order_retire(t, f->order);
        f->order = NULL;
    }
}
static void filtered_free(struct tide_entity_filtered_order *f)
{
    while (f)
    {
        struct tide_entity_filtered_order *next = f->next;
        order_release(f->order);
        free(f);
        f = next;
    }
}

static void order_lock(tide_entities *t)
{
    while (__atomic_exchange_n(&t->order_lock, 1u, __ATOMIC_ACQUIRE))
        tide_yield();
}
static void order_unlock(tide_entities *t) { __atomic_store_n(&t->order_lock, 0u, __ATOMIC_RELEASE); }
static void order_invalidate(tide_entities *t)
{
    filtered_invalidate(t);
    if (!t->ordered)
        return;
    order_retire(t, t->ordered);
    t->ordered = NULL;
}

static uint64_t order_log_end(const tide_entities *t)
{
    return t->order_log ? t->order_log->first + t->order_log->count : 0u;
}
static void order_log_free(tide_entities *t)
{
    if (t->order_log)
        free(t->order_log->entries);
    free(t->order_log);
    t->order_log = NULL;
}

// The entity in `slot` came, went or changed archetype (ORDER_ABSENT for one not alive): written down for the
// orders there are, which take it in when they're next asked for. Under the order lock, as every change is.
static void order_changed(tide_entities *t, const tide_entity key, const uint32_t slot, const uint32_t was,
                          const uint32_t now)
{
    if (was == now)
        return;
    struct tide_entity_order_log *log = t->order_log;
    if (!log)
    {
        bool any = t->ordered != NULL;
        for (const struct tide_entity_filtered_order *f = t->filtered_order; f && !any; f = f->next)
            any = f->order != NULL;
        if (!any)
            return; // No order to keep right: one made later is made of the slots as they are then
        log = t->order_log = tide_alloc_zeroed(1, sizeof *log);
    }
    if (log->count == ORDER_LOG_MOST)
    {
        // More than an order would take in: they're all made again
        order_invalidate(t);
        log->first += log->count;
        log->count = 0;
        return;
    }
    if (log->count == log->room)
    {
        const uint32_t room = log->room ? log->room * 2u : 64u;
        order_change *grown = tide_alloc((size_t)room * sizeof *grown);
        if (log->count)
            memcpy(grown, log->entries, (size_t)log->count * sizeof *grown);
        free(log->entries);
        log->entries = grown;
        log->room = room;
    }
    log->entries[log->count++] = (order_change){key, slot, was, now};
}

static bool admitted_archetype(const uint32_t *arches, uint32_t n, uint32_t arch);

// A change that puts an entity into an order or takes it out, in the order the changes were made
typedef struct order_event
{
    tide_entity key; // First: compared as the keys are
    uint32_t slot, seq;
    bool add;
} order_event;
static int order_event_compare(const void *a, const void *b)
{
    const order_event *x = a, *y = b;
    const int keys = tide_entity_compare(x->key, y->key);
    return keys ? keys : x->seq < y->seq ? -1 : x->seq > y->seq;
}

// `old`, which has taken in `at` of the table's changes, with the later ones: the entities of `arches` (every
// alive one for none) in the keys' order, as sorting the slots would give them. `old` itself where none of the
// changes is its own; NULL where the changes don't fit it, and it's made from the slots.
static struct tide_entity_order *order_follow(const tide_entities *t, struct tide_entity_order *old, const uint64_t at,
                                              const uint32_t *arches, const uint32_t n)
{
    const struct tide_entity_order_log *log = t->order_log;
    const uint32_t from = (uint32_t)(at - log->first), pending = log->count - from;
    order_event *events = tide_alloc((size_t)pending * sizeof *events);
    uint32_t count = 0;
    for (uint32_t i = from; i < log->count; i++)
    {
        const order_change *c = &log->entries[i];
        const bool before = c->was != ORDER_ABSENT && (!arches || admitted_archetype(arches, n, c->was));
        const bool after = c->now != ORDER_ABSENT && (!arches || admitted_archetype(arches, n, c->now));
        if (before != after)
            events[count++] = (order_event){c->key, c->slot, i, after};
    }
    if (!count)
    {
        free(events);
        return old;
    }
    if (count > 1u)
        qsort(events, count, sizeof *events, order_event_compare);
    // An entity's first change says whether the order has it, its last whether it stays
    uint32_t gone = 0, come = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        gone += (!i || !tide_entity_equal(events[i - 1u].key, events[i].key)) && !events[i].add;
        come += (i + 1u == count || !tide_entity_equal(events[i + 1u].key, events[i].key)) && events[i].add;
    }
    if (gone > old->count || (!gone && !come))
    {
        free(events);
        return gone ? NULL : old; // (Came and went again between two askings: as it was)
    }
    const uint32_t made = old->count - gone + come;
    const bool slotted = old->slots != NULL;
    const uint64_t each = sizeof(tide_entity) + (slotted ? sizeof(uint32_t) : 0u);
    if ((uint64_t)made * each > SIZE_MAX - sizeof(struct tide_entity_order))
        tide_out_of_memory();
    struct tide_entity_order *order = tide_alloc(sizeof *order + (size_t)made * (size_t)each);
    uint32_t *slots = slotted ? (uint32_t *)(order->keys + made) : NULL;
    order->refs = 1u;
    order->count = made;
    order->slots = slots;
    uint32_t in = 0, out = 0;
    bool fits = true;
    for (uint32_t i = 0; i < count && fits;)
    {
        uint32_t last = i;
        while (last + 1u < count && tide_entity_equal(events[last + 1u].key, events[i].key))
            last++;
        const tide_entity key = events[i].key;
        // The keys before it are as they were
        uint32_t begin = in, end = old->count;
        while (begin < end)
        {
            const uint32_t mid = begin + (end - begin) / 2u;
            if (tide_entity_compare(old->keys[mid], key) < 0)
                begin = mid + 1u;
            else
                end = mid;
        }
        const uint32_t kept = begin - in;
        const bool has = begin < old->count && tide_entity_equal(old->keys[begin], key);
        fits = has == !events[i].add && out + kept + events[last].add <= made;
        if (!fits)
            break;
        if (kept)
        {
            memcpy(order->keys + out, old->keys + in, (size_t)kept * sizeof *order->keys);
            if (slotted)
                memcpy(slots + out, old->slots + in, (size_t)kept * sizeof *slots);
            out += kept;
        }
        in = begin + has;
        if (events[last].add)
        {
            order->keys[out] = key;
            if (slotted)
                slots[out] = events[last].slot;
            out++;
        }
        i = last + 1u;
    }
    fits = fits && out + (old->count - in) == made;
    if (fits && in < old->count)
    {
        memcpy(order->keys + out, old->keys + in, (size_t)(old->count - in) * sizeof *order->keys);
        if (slotted)
            memcpy(slots + out, old->slots + in, (size_t)(old->count - in) * sizeof *slots);
    }
    free(events);
    if (!fits)
    {
        free(order);
        return NULL;
    }
    return order;
}

// The order at `*order`, which has taken in `at` changes, as the table is now: itself, the one that follows it,
// or none where it has to be made from the slots. One it replaces stays until the table settles, as borrowed.
static void order_update(tide_entities *t, struct tide_entity_order **order, const uint64_t at, const uint32_t *arches,
                         const uint32_t n)
{
    const struct tide_entity_order_log *log = t->order_log;
    if (!*order || !log || at == log->first + log->count)
        return;
    struct tide_entity_order *next = at >= log->first ? order_follow(t, *order, at, arches, n) : NULL;
    if (next == *order)
        return;
    order_retire(t, *order);
    *order = next;
}
static void retired_free(struct tide_entity_retired_order *retired)
{
    while (retired)
    {
        struct tide_entity_retired_order *next = retired->next;
        order_release(retired->order);
        free(retired);
        retired = next;
    }
}
static int entity_compare_qsort(const void *a, const void *b);

const tide_entity *tide_entities_ordered(const tide_entities *entities, uint32_t *count)
{
    // This cache is derived machine-local metadata, as page hash caches are.
    tide_entities *t = (tide_entities *)entities;
    order_lock(t);
    if (t->order_log)
        order_update(t, &t->ordered, t->order_log->full_at, NULL, 0);
    if (t->order_log)
    {
        t->order_log->full_at = order_log_end(t);
        t->order_log->full_idle = 0;
    }
    if (!t->ordered)
    {
        uint32_t n = 0;
        for (uint32_t at = 0; at < t->next_unused; at++)
            n += tide_entity_slot_of(t, at)->state == TIDE_ENTITY_ALIVE;
        if ((uint64_t)n * sizeof(tide_entity) > SIZE_MAX - sizeof(struct tide_entity_order))
            tide_out_of_memory();
        struct tide_entity_order *order = tide_alloc(sizeof *order + (size_t)n * sizeof(tide_entity));
        order->refs = 1u;
        order->count = n;
        order->slots = NULL;
        uint32_t out = 0;
        for (uint32_t at = 0; at < t->next_unused; at++)
        {
            const tide_entity_slot *slot = tide_entity_slot_of(t, at);
            if (slot->state == TIDE_ENTITY_ALIVE)
                order->keys[out++] = slot->entity;
        }
        if (n > 1u)
            qsort(order->keys, n, sizeof(tide_entity), entity_compare_qsort);
        t->ordered = order;
    }
    *count = t->ordered->count;
    const tide_entity *keys = t->ordered->keys;
    order_unlock(t);
    return keys;
}

static bool admitted_archetype(const uint32_t *arches, uint32_t n, uint32_t arch)
{
    uint32_t begin = 0, end = n;
    while (begin < end)
    {
        const uint32_t mid = begin + (end - begin) / 2u;
        if (arches[mid] < arch)
            begin = mid + 1u;
        else
            end = mid;
    }
    return begin < n && arches[begin] == arch;
}

typedef struct ordered_slot
{
    tide_entity key; // First: sorted with the keys' comparison
    uint32_t slot;
} ordered_slot;

const tide_entity *tide_entities_ordered_archetypes_slots(const tide_entities *entities, const uint32_t *arches,
                                                          uint32_t n, uint32_t *count, const uint32_t **slots)
{
    *count = 0;
    *slots = NULL;
    if (!n)
        return NULL;
    if ((uint64_t)n * sizeof(uint32_t) > SIZE_MAX - sizeof(struct tide_entity_filtered_order))
        tide_out_of_memory();
    tide_entities *t = (tide_entities *)entities;
    order_lock(t);
    struct tide_entity_filtered_order *f = t->filtered_order;
    while (f && (f->count != n || memcmp(f->archetypes, arches, (size_t)n * sizeof *arches)))
        f = f->next;
    if (!f)
    {
        f = tide_alloc(sizeof *f + (size_t)n * sizeof *arches);
        f->next = t->filtered_order;
        f->order = NULL;
        f->count = n;
        memcpy(f->archetypes, arches, (size_t)n * sizeof *arches);
        t->filtered_order = f;
    }
    order_update(t, &f->order, f->at, f->archetypes, n);
    f->at = order_log_end(t);
    f->idle = 0;
    if (!f->order)
    {
        uint32_t selected = 0;
        for (uint32_t at = 0; at < t->next_unused; at++)
        {
            const tide_entity_slot *slot = tide_entity_slot_of(t, at);
            selected += slot->state == TIDE_ENTITY_ALIVE && admitted_archetype(f->archetypes, n, slot->archetype);
        }
        const uint64_t each = sizeof(tide_entity) + sizeof(uint32_t);
        if ((uint64_t)selected * each > SIZE_MAX - sizeof(struct tide_entity_order) ||
            (uint64_t)selected * sizeof(ordered_slot) > SIZE_MAX)
            tide_out_of_memory();
        struct tide_entity_order *order = tide_alloc(sizeof *order + (size_t)selected * (size_t)each);
        ordered_slot *sorted = selected ? tide_alloc((size_t)selected * sizeof *sorted) : NULL;
        uint32_t *order_slots = (uint32_t *)(order->keys + selected);
        order->refs = 1u;
        order->count = selected;
        order->slots = order_slots;
        uint32_t out = 0;
        for (uint32_t at = 0; at < t->next_unused; at++)
        {
            const tide_entity_slot *slot = tide_entity_slot_of(t, at);
            if (slot->state == TIDE_ENTITY_ALIVE && admitted_archetype(f->archetypes, n, slot->archetype))
            {
                sorted[out].key = slot->entity;
                sorted[out++].slot = at;
            }
        }
        if (selected > 1u)
            qsort(sorted, selected, sizeof *sorted, entity_compare_qsort);
        for (uint32_t i = 0; i < selected; i++)
        {
            order->keys[i] = sorted[i].key;
            order_slots[i] = sorted[i].slot;
        }
        free(sorted);
        f->order = order;
    }
    *count = f->order->count;
    *slots = f->order->slots;
    const tide_entity *keys = f->order->keys;
    order_unlock(t);
    return keys;
}

const tide_entity *tide_entities_ordered_archetypes(const tide_entities *entities, const uint32_t *arches, uint32_t n,
                                                    uint32_t *count)
{
    const uint32_t *slots;
    return tide_entities_ordered_archetypes_slots(entities, arches, n, count, &slots);
}

static _Noreturn void identity_failure(const char *reason)
{
    fprintf(stderr, "tide: entity identity error: %s\n", reason);
    abort();
}

static uint32_t index_pages(const struct tide_entity_index *m)
{
    return (uint32_t)((m->capacity + INDEX_ENTRIES - 1u) >> INDEX_SHIFT);
}

static struct tide_entity_index *index_new(uint64_t capacity)
{
    const uint64_t pages = (capacity + INDEX_ENTRIES - 1u) >> INDEX_SHIFT;
    if (pages > UINT32_MAX || pages > (SIZE_MAX - sizeof(struct tide_entity_index)) / sizeof(tide_page *))
        tide_out_of_memory();
    struct tide_entity_index *m = tide_alloc_zeroed(1, sizeof *m + (size_t)pages * sizeof(tide_page *));
    m->capacity = capacity;
    return m;
}

static void index_free(struct tide_entity_index *m)
{
    while (m)
    {
        struct tide_entity_index *old = m->previous;
        for (uint32_t p = 0; p < index_pages(m); p++)
            tide_page_release(m->page[p], 1);
        free(m);
        m = old;
    }
}

static void index_settle(struct tide_entity_index *m)
{
    if (!m)
        return;
    index_free(m->previous);
    m->previous = NULL;
}

static struct tide_entity_index *index_copy(const struct tide_entity_index *from)
{
    if (!from)
        return NULL;
    struct tide_entity_index *to = index_new(from->capacity);
    to->count = from->count;
    to->tombs = from->tombs;
    for (uint32_t p = 0; p < index_pages(from); p++)
    {
        to->page[p] = from->page[p];
        if (to->page[p])
            tide_page_retain(to->page[p]);
    }
    return to;
}

static const entity_entry *entry_at(const struct tide_entity_index *m, uint64_t at)
{
    const tide_page *p = __atomic_load_n(&m->page[at >> INDEX_SHIFT], __ATOMIC_ACQUIRE);
    return p ? (const entity_entry *)tide_page_data(p) + (at & (INDEX_ENTRIES - 1u)) : NULL;
}

static entity_entry *entry_change(struct tide_entity_index *m, uint64_t at)
{
    const uint32_t p = (uint32_t)(at >> INDEX_SHIFT);
    tide_page *page = m->page[p] ? tide_page_own(m->page[p], 1, INDEX_BYTES) : tide_page_new(INDEX_BYTES);
    __atomic_store_n(&m->page[p], page, __ATOMIC_RELEASE);
    return (entity_entry *)tide_page_data(page) + (at & (INDEX_ENTRIES - 1u));
}

static uint64_t key_hash(tide_entity e) { return tide_hash_end(tide_entity_hash(TIDE_HASH_START, e)); }

static bool entry_key(const entity_entry *p, tide_entity key)
{
    return __atomic_load_n(&p->key.birth, __ATOMIC_RELAXED) == key.birth &&
           __atomic_load_n(&p->key.lineage, __ATOMIC_RELAXED) == key.lineage;
}

static uint32_t index_get(const struct tide_entity_index *m, tide_entity key)
{
    if (!m)
        return UINT32_MAX;
    uint64_t at = key_hash(key) & (m->capacity - 1u);
    for (uint64_t n = 0; n < m->capacity; n++, at = (at + 1u) & (m->capacity - 1u))
    {
        const entity_entry *p = entry_at(m, at);
        const uint32_t state = p ? __atomic_load_n(&p->state, __ATOMIC_ACQUIRE) : 0u;
        if (!state)
            return UINT32_MAX;
        if (state == 2u && entry_key(p, key))
            return __atomic_load_n(&p->slot, __ATOMIC_RELAXED);
    }
    return UINT32_MAX;
}

static void index_put_raw(struct tide_entity_index *m, tide_entity key, uint32_t slot)
{
    uint64_t at = key_hash(key) & (m->capacity - 1u), tomb = UINT64_MAX;
    for (;;)
    {
        const entity_entry *p = entry_at(m, at);
        const uint32_t state = p ? p->state : 0u;
        if (state == 2u && entry_key(p, key))
        {
            entity_entry *w = entry_change(m, at);
            __atomic_store_n(&w->slot, slot, __ATOMIC_RELEASE);
            return;
        }
        if (state == 1u && tomb == UINT64_MAX)
            tomb = at;
        if (!state)
            break;
        at = (at + 1u) & (m->capacity - 1u);
    }
    if (tomb != UINT64_MAX)
    {
        at = tomb;
        m->tombs--;
    }
    entity_entry *p = entry_change(m, at);
    __atomic_store_n(&p->key.birth, key.birth, __ATOMIC_RELAXED);
    __atomic_store_n(&p->key.lineage, key.lineage, __ATOMIC_RELAXED);
    __atomic_store_n(&p->slot, slot, __ATOMIC_RELAXED);
    __atomic_store_n(&p->state, 2u, __ATOMIC_RELEASE);
    m->count++;
}

static void index_put(struct tide_entity_index **address, tide_entity key, uint32_t slot)
{
    struct tide_entity_index *m = *address;
    if (m && m->count == UINT32_MAX)
        identity_failure("entity index capacity exhausted");
    if (!m || ((uint64_t)m->count + m->tombs + 1u) * 4u >= m->capacity * 3u)
    {
        // Tombstones require compaction, not growth proportional to lifetime churn.
        const bool grow = m && ((uint64_t)m->count + 1u) * 4u >= m->capacity * 3u;
        const uint64_t capacity = m ? m->capacity * (grow ? 2u : 1u) : INDEX_ENTRIES;
        struct tide_entity_index *grown = index_new(capacity);
        for (uint64_t i = 0; m && i < m->capacity; i++)
        {
            const entity_entry *p = entry_at(m, i);
            if (p && p->state == 2u)
                index_put_raw(grown, p->key, p->slot);
        }
        grown->previous = m;
        __atomic_store_n(address, grown, __ATOMIC_RELEASE);
        m = grown;
    }
    index_put_raw(m, key, slot);
}

static void index_remove(struct tide_entity_index *m, tide_entity key)
{
    if (!m)
        return;
    uint64_t at = key_hash(key) & (m->capacity - 1u);
    for (uint64_t n = 0; n < m->capacity; n++, at = (at + 1u) & (m->capacity - 1u))
    {
        const entity_entry *p = entry_at(m, at);
        if (!p || !p->state)
            return;
        if (p->state == 2u && entry_key(p, key))
        {
            entity_entry *w = entry_change(m, at);
            __atomic_store_n(&w->state, 1u, __ATOMIC_RELEASE);
            m->count--;
            m->tombs++;
            return;
        }
    }
}

static const tide_entity_slot *slot_of(const tide_entities *t, uint32_t index) { return tide_entity_slot_of(t, index); }

static uint32_t used_on(const tide_entities *t, uint32_t p)
{
    const uint32_t left = t->next_unused - (p << TIDE_ENTITY_PAGE_SHIFT);
    return left < SLOTS ? left : SLOTS;
}

static tide_entity_slot *slot_to_change(tide_entities *t, uint32_t index)
{
    const uint32_t p = index >> TIDE_ENTITY_PAGE_SHIFT;
    tide_page *own = tide_page_own(t->page[p], 1, used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
    __atomic_store_n(&t->page[p], own, __ATOMIC_RELEASE);
    return (tide_entity_slot *)tide_page_data(own) + (index & (SLOTS - 1u));
}

static void make_room(tide_entities *t, uint32_t pages)
{
    if (pages <= t->room)
        return;
    uint32_t room = t->room ? t->room : 4u;
    while (room < pages)
    {
        if (room > UINT32_MAX / 2u)
            tide_out_of_memory();
        room *= 2u;
    }
    if ((uint64_t)room * 2u + 1u > SIZE_MAX / sizeof(tide_page *))
        tide_out_of_memory();
    tide_page **base = tide_alloc_zeroed((size_t)room * 2u + 1u, sizeof *base), **grown = base + 1;
    if (t->page && t->pages)
    {
        memcpy(grown, t->page, (size_t)t->pages * sizeof *grown);
        memcpy(grown + room, t->page + t->room, (size_t)t->pages * sizeof *grown);
    }
    base[0] = t->page ? (tide_page *)(void *)(t->page - 1) : NULL;
    __atomic_store_n(&t->page, grown, __ATOMIC_RELEASE);
    t->room = room;
}

void tide_entities_settle(tide_entities *t)
{
    order_lock(t);
    retired_free(t->retired_order);
    t->retired_order = NULL;
    struct tide_entity_order_log *log = t->order_log;
    if (log)
    {
        // The changes every order has taken in go. An order that lacks some and wasn't asked for in a while goes
        // itself, so that it doesn't keep them: it's made from the slots if it's asked for again
        const uint64_t end = log->first + log->count;
        uint64_t least = end;
        if (t->ordered && log->full_at != end)
        {
            if (log->full_at < log->first || ++log->full_idle > ORDER_IDLE_MOST)
            {
                order_release(t->ordered);
                t->ordered = NULL;
            }
            else
                least = log->full_at;
        }
        for (struct tide_entity_filtered_order *f = t->filtered_order; f; f = f->next)
        {
            if (!f->order || f->at == end)
                continue;
            if (f->at < log->first || ++f->idle > ORDER_IDLE_MOST)
            {
                order_release(f->order);
                f->order = NULL;
            }
            else if (f->at < least)
                least = f->at;
        }
        const uint32_t taken = (uint32_t)(least - log->first);
        if (taken)
        {
            log->count -= taken;
            if (log->count)
                memmove(log->entries, log->entries + taken, (size_t)log->count * sizeof *log->entries);
            log->first = least;
        }
    }
    order_unlock(t);
    if (t->page)
    {
        void *old = t->page[-1];
        while (old)
        {
            tide_page **base = old;
            old = base[0];
            free(base);
        }
        t->page[-1] = NULL;
    }
    index_settle(t->index);
    index_settle(t->births);
}

static bool slot_key(const tide_entity_slot *slot, tide_entity e)
{
    return __atomic_load_n(&slot->entity.birth, __ATOMIC_RELAXED) == e.birth &&
           __atomic_load_n(&slot->entity.lineage, __ATOMIC_RELAXED) == e.lineage;
}

// Where this thread last found each entity, in whichever table: a slot for
// each of SLOT_GUESSES places a key's own bits pick (its lineage is a hash
// already). Code that goes through a table's rows asks for the same entity
// again and again: what it was born as, its place in last tick's world, whose
// slots are this one's, and all of it the next frame. A slot that's alive and
// holds the key is the entity's, in any table, so the guess is looked at
// before the key is hashed and looked up, and never trusted: a wrong one (two
// keys in one place, a slot that changed hands) costs a read. Nothing here is
// about where rows are, or the order entities were made in.
// Made the first time a thread looks one up; without the memory, no guesses.
#define SLOT_GUESSES 65536u
static TIDE_THREAD_LOCAL uint32_t *slot_guesses;
static TIDE_THREAD_LOCAL bool slot_guesses_tried;

static uint32_t *slot_guess(const tide_entity e)
{
    if (!slot_guesses)
    {
        if (slot_guesses_tried)
            return NULL;
        slot_guesses_tried = true;
        slot_guesses = calloc(SLOT_GUESSES, sizeof *slot_guesses); // The thread's, for as long as it lives
        if (!slot_guesses)
            return NULL;
    }
    const uint64_t mixed = e.lineage ^ (e.lineage >> 29) ^ ((uint64_t)e.birth * UINT64_C(0x9E3779B97F4A7C15));
    return &slot_guesses[(uint32_t)(mixed ^ (mixed >> 16)) & (SLOT_GUESSES - 1u)];
}

uint32_t tide_entity_slot_index(const tide_entities *t, tide_entity e)
{
    if (tide_entity_is_null(e))
        return UINT32_MAX;
    uint32_t *const kept = slot_guess(e);
    if (kept)
    {
        const tide_entity_slot *guess = slot_of(t, *kept); // One the table hasn't got reads as free
        if (__atomic_load_n(&guess->state, __ATOMIC_ACQUIRE) == TIDE_ENTITY_ALIVE && slot_key(guess, e))
            return *kept;
    }
    const uint32_t at = index_get(__atomic_load_n(&t->index, __ATOMIC_ACQUIRE), e);
    if (at == UINT32_MAX)
        return at;
    const tide_entity_slot *slot = slot_of(t, at);
    if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != TIDE_ENTITY_ALIVE || !slot_key(slot, e))
        return UINT32_MAX;
    if (kept)
        *kept = at;
    return at;
}

bool tide_entity_alive(const tide_entities *t, tide_entity e) { return tide_entity_slot_index(t, e) != UINT32_MAX; }

static tide_entity create_unchecked(tide_entities *t, tide_entity key)
{
    uint32_t at;
    if (t->free_head)
        at = t->free_head - 1u;
    else
    {
        if (t->next_unused == UINT32_MAX)
            identity_failure("physical entity slot capacity exhausted");
        at = t->next_unused;
        if (!(at & (SLOTS - 1u)))
        {
            make_room(t, t->pages + 1u);
            t->page[t->pages++] = tide_page_new(PAGE_BYTES);
        }
        __atomic_store_n(&t->next_unused, at + 1u, __ATOMIC_RELEASE);
    }
    order_changed(t, key, at, ORDER_ABSENT, TIDE_ARCHETYPE_NONE);
    tide_entity_slot *slot = slot_to_change(t, at);
    if (t->free_head)
    {
        t->free_head = slot->row;
        index_remove(t->index, slot->entity);
    }
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = 0;
    slot->snaps = 0;
    __atomic_store_n(&slot->entity.birth, key.birth, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->entity.lineage, key.lineage, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->state, TIDE_ENTITY_ALIVE, __ATOMIC_RELEASE);
    index_put(&t->index, key, at);
    if (t->page[t->room + (at >> TIDE_ENTITY_PAGE_SHIFT)])
        tide_entity_set_born(t, key, 0);
    return key;
}

tide_entity tide_entity_create_key(tide_entities *t, tide_entity key)
{
    order_lock(t);
    if (!key.birth)
        identity_failure("keyed match entities need a nonzero identity clock");
    if (key.birth < t->key_clock)
        identity_failure("entity identity clock moved backwards");
    if (key.birth != t->key_clock)
    {
        index_free(t->births);
        t->births = NULL;
        t->key_clock = key.birth;
    }
    if (index_get(t->births, key) != UINT32_MAX || index_get(t->index, key) != UINT32_MAX)
        identity_failure("duplicate or colliding causal entity key; identities are never probed or reused");
    index_put(&t->births, key, 0);
    const tide_entity result = create_unchecked(t, key);
    order_unlock(t);
    return result;
}

tide_entity tide_entity_create(tide_entities *t)
{
    order_lock(t);
    if (t->unkeyed_next == UINT64_MAX)
        identity_failure("unkeyed entity namespace exhausted");
    const tide_entity key = {0, ++t->unkeyed_next};
    if (index_get(t->index, key) != UINT32_MAX)
        identity_failure("unkeyed entity identity already installed");
    const tide_entity result = create_unchecked(t, key);
    order_unlock(t);
    return result;
}

bool tide_entity_destroy(tide_entities *t, tide_entity e)
{
    order_lock(t);
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at == UINT32_MAX)
    {
        order_unlock(t);
        return false;
    }
    tide_entity_slot *slot = slot_to_change(t, at);
    order_changed(t, e, at, slot->archetype, ORDER_ABSENT);
    __atomic_store_n(&slot->state, TIDE_ENTITY_FREE, __ATOMIC_RELEASE);
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = t->free_head;
    slot->snaps = 0;
    t->free_head = at + 1u;
    order_unlock(t);
    return true;
}

uint64_t tide_entities_slot_page_hash(const tide_entities *t, const uint32_t page)
{
    if (page >= t->pages || !t->page[page]) return 0u;
    return tide_page_hash(t->page[page], used_on(t, page) * (uint32_t)sizeof(tide_entity_slot));
}

bool tide_entity_erase(tide_entities *t, tide_entity e)
{
    order_lock(t);
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at == UINT32_MAX)
    {
        order_unlock(t);
        return false;
    }
    tide_entity_slot *slot = slot_to_change(t, at);
    order_changed(t, e, at, slot->archetype, ORDER_ABSENT);
    __atomic_store_n(&slot->state, TIDE_ENTITY_FREE, __ATOMIC_RELEASE);
    // No key stays in the slot or the index: nothing says it was here, and it can be installed again
    index_remove(t->index, e);
    __atomic_store_n(&slot->entity.birth, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->entity.lineage, 0u, __ATOMIC_RELAXED);
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = t->free_head;
    slot->snaps = 0;
    t->free_head = at + 1u;
    order_unlock(t);
    return true;
}

bool tide_entity_install(tide_entities *t, tide_entity e)
{
    if (tide_entity_is_null(e))
        return false;
    order_lock(t);
    const uint32_t at = index_get(t->index, e);
    if (at != UINT32_MAX)
    {
        const tide_entity_slot *had = slot_of(t, at);
        if (had->state != TIDE_ENTITY_AWAY || !slot_key(had, e))
        {
            order_unlock(t);
            return false;
        }
        order_changed(t, e, at, ORDER_ABSENT, TIDE_ARCHETYPE_NONE);
        tide_entity_slot *slot = slot_to_change(t, at);
        slot->archetype = TIDE_ARCHETYPE_NONE;
        slot->row = 0;
        slot->snaps = 0;
        slot->state = TIDE_ENTITY_ALIVE;
        order_unlock(t);
        return true;
    }
    create_unchecked(t, e);
    order_unlock(t);
    return true;
}

void tide_entity_forget(tide_entities *t, tide_entity e, bool away)
{
    if (!away)
    {
        (void)tide_entity_destroy(t, e);
        return;
    }
    order_lock(t);
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at != UINT32_MAX)
    {
        tide_entity_slot *slot = slot_to_change(t, at);
        order_changed(t, e, at, slot->archetype, ORDER_ABSENT);
        slot->state = TIDE_ENTITY_AWAY;
        slot->archetype = TIDE_ARCHETYPE_AWAY;
        slot->row = 0;
        slot->snaps = 0;
    }
    order_unlock(t);
}

bool tide_entity_release(tide_entities *t, tide_entity e)
{
    order_lock(t);
    const uint32_t at = index_get(t->index, e);
    if (at == UINT32_MAX || slot_of(t, at)->state != TIDE_ENTITY_AWAY)
    {
        order_unlock(t);
        return false;
    }
    tide_entity_slot *slot = slot_to_change(t, at);
    slot->state = TIDE_ENTITY_FREE;
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = t->free_head;
    slot->snaps = 0;
    t->free_head = at + 1u;
    order_unlock(t);
    return true;
}

bool tide_entity_died_here(const tide_entities *t, tide_entity e)
{
    if (tide_entity_is_null(e))
        return false;
    const uint32_t at = index_get(t->index, e);
    return at != UINT32_MAX && slot_of(t, at)->state == TIDE_ENTITY_FREE;
}

void tide_entity_set_location(tide_entities *t, tide_entity e, tide_location loc)
{
    order_lock(t);
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at != UINT32_MAX)
    {
        tide_entity_slot *slot = slot_to_change(t, at);
        order_changed(t, e, at, slot->archetype, loc.archetype);
        slot->archetype = loc.archetype;
        slot->row = loc.row;
    }
    order_unlock(t);
}

tide_location tide_entity_location(const tide_entities *t, tide_entity e)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at == UINT32_MAX)
        return (tide_location){TIDE_ARCHETYPE_NONE, 0};
    const tide_entity_slot *slot = slot_of(t, at);
    return (tide_location){slot->archetype, slot->row};
}

tide_entity tide_entity_in_slot(const tide_entities *t, uint32_t at)
{
    if (at >= t->next_unused)
        return (tide_entity){0};
    const tide_entity_slot *slot = slot_of(t, at);
    return slot->state == TIDE_ENTITY_ALIVE ? slot->entity : (tide_entity){0};
}

void tide_entity_snap(tide_entities *t, tide_entity e)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at != UINT32_MAX)
        slot_to_change(t, at)->snaps++;
}
void tide_entity_set_snaps(tide_entities *t, tide_entity e, uint32_t snaps)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at != UINT32_MAX && slot_of(t, at)->snaps != snaps)
        slot_to_change(t, at)->snaps = snaps;
}
uint32_t tide_entity_snaps(const tide_entities *t, tide_entity e)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    return at == UINT32_MAX ? 0 : slot_of(t, at)->snaps;
}

void tide_entity_set_born(tide_entities *t, tide_entity e, uint32_t born)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    if (at == UINT32_MAX)
        return;
    tide_page **page = &t->page[t->room + (at >> TIDE_ENTITY_PAGE_SHIFT)];
    *page = *page ? tide_page_own(*page, 1, BORN_BYTES) : tide_page_new(BORN_BYTES);
    ((uint32_t *)tide_page_data(*page))[at & (SLOTS - 1u)] = born;
}
uint32_t tide_entity_born_at(const tide_entities *t, uint32_t at)
{
    if (at >= t->next_unused)
        return 0;
    const tide_page *p = t->page[t->room + (at >> TIDE_ENTITY_PAGE_SHIFT)];
    return p ? ((const uint32_t *)tide_page_data(p))[at & (SLOTS - 1u)] : 0;
}
uint32_t tide_entity_born(const tide_entities *t, tide_entity e)
{
    const uint32_t at = tide_entity_slot_index(t, e);
    return at == UINT32_MAX ? 0 : tide_entity_born_at(t, at);
}

void tide_entities_copy(tide_entities *to, const tide_entities *from)
{
    if (to == from)
        return;
    struct tide_entity_index *index = index_copy(from->index), *births = index_copy(from->births);
    for (uint32_t p = 0; p < from->pages; p++)
    {
        tide_page_retain(from->page[p]);
        if (from->page[from->room + p])
            tide_page_retain(from->page[from->room + p]);
    }
    for (uint32_t p = 0; p < to->pages; p++)
    {
        tide_page_release(to->page[p], 1);
        tide_page_release(to->page[to->room + p], 1);
    }
    tide_entities_settle(to);
    order_release(to->ordered);
    filtered_free(to->filtered_order);
    order_log_free(to);
    // The orders made of `from` as it is are the copy's too: the same entities in the same slots. A world that's
    // copied each tick (a predicting client's, a server's kept ticks) would otherwise sort every entity it has
    // again for each system and query, each tick, though nothing came or went.
    tide_entities *source = (tide_entities *)from; // Its orders are derived: see tide_entities_ordered
    order_lock(source);
    // ...and so are the ones that have yet to take in changes made since, with those changes, where they're few
    // (ORDER_COPY_MOST): the copy takes them in as its source would. A copy of a table nothing settles (a world
    // kept and changed, never ticked) copies no more than that of them.
    const struct tide_entity_order_log *had = source->order_log;
    const uint64_t end = order_log_end(source);
    uint64_t least = end;
    if (had)
    {
        if (source->ordered && had->full_at >= had->first)
            least = had->full_at;
        for (const struct tide_entity_filtered_order *f = source->filtered_order; f; f = f->next)
        {
            if (f->order && f->at >= had->first && f->at < least)
                least = f->at;
        }
        if (end - least > ORDER_COPY_MOST)
            least = end;
        struct tide_entity_order_log *log = to->order_log = tide_alloc_zeroed(1, sizeof *log);
        log->first = least;
        log->full_at = had->full_at;
        log->full_idle = had->full_idle;
        const uint32_t count = (uint32_t)(end - least);
        if (count)
        {
            log->entries = tide_alloc((size_t)count * sizeof *log->entries);
            memcpy(log->entries, had->entries + (had->count - count), (size_t)count * sizeof *log->entries);
            log->count = log->room = count;
        }
    }
    to->ordered = !had || had->full_at >= least ? order_retain(source->ordered) : NULL;
    to->filtered_order = NULL;
    struct tide_entity_filtered_order **last = &to->filtered_order;
    for (const struct tide_entity_filtered_order *f = source->filtered_order; f; f = f->next)
    {
        if (!f->order || (had && f->at < least))
            continue;
        struct tide_entity_filtered_order *mine = tide_alloc(sizeof *mine + (size_t)f->count * sizeof *f->archetypes);
        mine->next = NULL;
        mine->order = order_retain(f->order);
        mine->at = f->at;
        mine->idle = f->idle;
        mine->count = f->count;
        memcpy(mine->archetypes, f->archetypes, (size_t)f->count * sizeof *f->archetypes);
        *last = mine;
        last = &mine->next;
    }
    order_unlock(source);
    index_free(to->index);
    index_free(to->births);
    make_room(to, from->pages);
    if (to->room)
        memset(to->page + to->room, 0, (size_t)to->room * sizeof *to->page);
    if (from->pages)
    {
        memcpy(to->page, from->page, (size_t)from->pages * sizeof *to->page);
        memcpy(to->page + to->room, from->page + from->room, (size_t)from->pages * sizeof *to->page);
    }
    to->next_unused = from->next_unused;
    to->free_head = from->free_head;
    to->pages = from->pages;
    to->key_clock = from->key_clock;
    to->unkeyed_next = from->unkeyed_next;
    to->index = index;
    to->births = births;
}

static uint64_t births_hash(const tide_entities *t)
{
    uint64_t h = 0;
    const struct tide_entity_index *m = t->births;
    for (uint64_t i = 0; m && i < m->capacity; i++)
    {
        const entity_entry *p = entry_at(m, i);
        if (p && p->state == 2u)
            h += key_hash(p->key);
    }
    return h;
}
uint64_t tide_entities_hash(uint64_t h, const tide_entities *t)
{
    h = tide_hash_more(h, &t->next_unused, sizeof t->next_unused);
    h = tide_hash_more(h, &t->free_head, sizeof t->free_head);
    h = tide_hash_more(h, &t->key_clock, sizeof t->key_clock);
    h = tide_hash_more(h, &t->unkeyed_next, sizeof t->unkeyed_next);
    const uint32_t count = t->births ? t->births->count : 0;
    const uint64_t born = births_hash(t);
    h = tide_hash_more(h, &count, sizeof count);
    h = tide_hash_more(h, &born, sizeof born);
    for (uint32_t p = 0; p < t->pages; p++)
    {
        const uint64_t page = tide_page_hash(t->page[p], used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
        h = tide_hash_more(h, &page, sizeof page);
    }
    return h;
}
void tide_entities_stale(const tide_entities *t, tide_stale_pages *s)
{
    for (uint32_t p = 0; p < t->pages; p++)
        tide_page_stale(s, t->page[p], used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
}
void tide_entities_free(tide_entities *t)
{
    for (uint32_t p = 0; t->page && p < t->room && p < t->pages; p++)
    {
        tide_page_release(t->page[p], 1);
        tide_page_release(t->page[t->room + p], 1);
    }
    tide_entities_settle(t);
    order_release(t->ordered);
    filtered_free(t->filtered_order);
    order_log_free(t);
    if (t->page)
        free(t->page - 1);
    index_free(t->index);
    index_free(t->births);
    memset(t, 0, sizeof *t);
}

static bool rebuild_index(tide_entities *t)
{
    for (uint32_t at = 0; at < t->next_unused; at++)
    {
        const tide_entity_slot *s = slot_of(t, at);
        if (s->state > TIDE_ENTITY_AWAY)
            return false;
        if (s->state == TIDE_ENTITY_FREE)
        {
            if (s->archetype != TIDE_ARCHETYPE_NONE || s->row > t->next_unused || s->snaps)
                return false;
            // A slot tide_entity_erase freed has no key at all: nothing of what was in it is known
            if (tide_entity_is_null(s->entity))
                continue;
            if (index_get(t->index, s->entity) != UINT32_MAX)
                return false;
            index_put(&t->index, s->entity, at);
            continue;
        }
        if (tide_entity_is_null(s->entity) || index_get(t->index, s->entity) != UINT32_MAX)
            return false;
        if (s->state == TIDE_ENTITY_AWAY && (s->archetype != TIDE_ARCHETYPE_AWAY || s->row || s->snaps))
            return false;
        index_put(&t->index, s->entity, at);
    }
    uint32_t free = t->free_head, count = 0, expected = 0;
    for (uint32_t at = 0; at < t->next_unused; at++)
        expected += slot_of(t, at)->state == TIDE_ENTITY_FREE;
    while (free)
    {
        if (free > t->next_unused || count++ >= expected)
            return false;
        const tide_entity_slot *s = slot_of(t, free - 1u);
        if (s->state != TIDE_ENTITY_FREE)
            return false;
        free = s->row;
    }
    return count == expected;
}

static int entity_compare_qsort(const void *a, const void *b)
{
    return tide_entity_compare(*(const tide_entity *)a, *(const tide_entity *)b);
}
static tide_entity *birth_keys(const tide_entities *t, uint32_t *count)
{
    *count = t->births ? t->births->count : 0;
    if (!*count)
        return NULL;
    if ((uint64_t)*count * sizeof(tide_entity) > SIZE_MAX)
        tide_out_of_memory();
    tide_entity *keys = tide_alloc((size_t)*count * sizeof *keys);
    uint32_t n = 0;
    for (uint64_t i = 0; i < t->births->capacity; i++)
    {
        const entity_entry *p = entry_at(t->births, i);
        if (p && p->state == 2u)
            keys[n++] = p->key;
    }
    qsort(keys, n, sizeof *keys, entity_compare_qsort);
    return keys;
}
uint32_t tide_entities_packed_size(const tide_entities *t)
{
    const uint64_t n =
        28u + (uint64_t)t->next_unused * sizeof(tide_entity_slot) + (uint64_t)(t->births ? t->births->count : 0) * 16u;
    if (n > UINT32_MAX)
        identity_failure("packed entity state exceeds the 4 GiB serialization bound");
    return (uint32_t)n;
}
void tide_entities_pack(const tide_entities *t, tide_writer *w)
{
    tide_write_u32(w, t->next_unused);
    tide_write_u32(w, t->free_head);
    tide_write_u64(w, t->key_clock);
    tide_write_u64(w, t->unkeyed_next);
    uint32_t count;
    tide_entity *keys = birth_keys(t, &count);
    tide_write_u32(w, count);
    for (uint32_t i = 0; i < count; i++)
    {
        tide_write_u64(w, keys[i].birth);
        tide_write_u64(w, keys[i].lineage);
    }
    free(keys);
    for (uint32_t p = 0; p < t->pages; p++)
        tide_write_bytes(w, tide_page_data(t->page[p]), used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
}
static bool read_births_after(tide_entities *t, tide_reader *r, uint32_t count, tide_entity *previous)
{
    if (count > (r->size - r->at) / 16u)
        return false;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint64_t birth = tide_read_u64(r);
        const uint64_t lineage = tide_read_u64(r);
        tide_entity key = {birth, lineage};
        if (r->failed || !key.birth || key.birth != t->key_clock ||
            (!tide_entity_is_null(*previous) && tide_entity_compare(*previous, key) >= 0))
            return false;
        index_put(&t->births, key, 0);
        *previous = key;
    }
    return !r->failed;
}
static bool read_births(tide_entities *t, tide_reader *r, uint32_t count)
{
    tide_entity previous = {0};
    return read_births_after(t, r, count, &previous);
}
bool tide_entities_unpack(tide_entities *t, tide_reader *r)
{
    tide_entities next = {0};
    next.next_unused = tide_read_u32(r);
    next.free_head = tide_read_u32(r);
    next.key_clock = tide_read_u64(r);
    next.unkeyed_next = tide_read_u64(r);
    const uint32_t count = tide_read_u32(r);
    bool ok = !r->failed && next.free_head <= next.next_unused && read_births(&next, r, count);
    ok = ok && next.next_unused <= (r->size - r->at) / sizeof(tide_entity_slot);
    if (ok)
    {
        next.pages = (uint32_t)(((uint64_t)next.next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
        make_room(&next, next.pages);
        for (uint32_t p = 0; ok && p < next.pages; p++)
        {
            const uint32_t n = used_on(&next, p) * (uint32_t)sizeof(tide_entity_slot);
            const uint8_t *bytes = tide_read_bytes(r, n);
            ok = bytes != NULL;
            if (ok)
            {
                next.page[p] = tide_page_new(PAGE_BYTES);
                memcpy(tide_page_data(next.page[p]), bytes, n);
            }
        }
        ok = ok && rebuild_index(&next);
    }
    if (!ok)
    {
        tide_entities_free(&next);
        return false;
    }
    tide_entities_settle(&next);
    tide_entities_free(t);
    *t = next;
    return true;
}

#define SLOT ((uint32_t)sizeof(tide_entity_slot))
#define BIRTH_KEYS 1024u
#define BIRTH_BYTES (BIRTH_KEYS * (uint32_t)sizeof(tide_entity))
static uint32_t birth_pages(uint32_t count) { return (uint32_t)(((uint64_t)count + BIRTH_KEYS - 1u) / BIRTH_KEYS); }
static uint32_t birth_used(uint32_t count, uint32_t page)
{
    if (page >= birth_pages(count)) return 0;
    const uint32_t first = page * BIRTH_KEYS;
    return count - first < BIRTH_KEYS ? count - first : BIRTH_KEYS;
}
void tide_entities_pack_delta(const tide_entities *t, const tide_entities *base, tide_delta_writer *d)
{
    tide_delta_number(d, t->next_unused);
    tide_delta_number(d, t->free_head);
    tide_delta_number64(d, t->key_clock);
    tide_delta_number64(d, t->unkeyed_next);
    uint32_t count;
    tide_entity *keys = birth_keys(t, &count);
    tide_delta_number(d, count);
    uint32_t removed_count = 0, added_count = 0;
    const uint32_t base_count = base && base->births ? base->births->count : 0;
    const bool shared_clock = d->xor && base && base->key_clock == t->key_clock;
    tide_entity *removed = NULL;
    if (shared_clock)
    {
        if ((uint64_t)base_count * sizeof *removed > SIZE_MAX)
            tide_out_of_memory();
        if (base_count)
            removed = tide_alloc((size_t)base_count * sizeof *removed);
        for (uint64_t i = 0; base->births && i < base->births->capacity; i++)
        {
            const entity_entry *entry = entry_at(base->births, i);
            if (entry && entry->state == 2u && index_get(t->births, entry->key) == UINT32_MAX)
                removed[removed_count++] = entry->key;
        }
        for (uint32_t i = 0; i < count; i++)
            added_count += index_get(base->births, keys[i]) == UINT32_MAX;
    }
    // A clock-local registry is a set, not an ordered byte stream. Inserting
    // one key must not resend the sorted tail of thousands of stable keys.
    const bool edits = shared_clock && ((uint64_t)removed_count + added_count) * 8u + 10u < (uint64_t)count * 16u;
    tide_delta_number(d, d->by_need ? 2u : edits ? 1u : 0u);
    if (d->by_need)
    {
        for (uint32_t p = 0; p < birth_pages(count); p++)
            tide_delta_region(d, keys + (size_t)p * BIRTH_KEYS, birth_used(count, p) * 16u, NULL, 0, false, 16u);
    }
    else if (edits)
    {
        if (removed_count > 1u)
            qsort(removed, removed_count, sizeof *removed, entity_compare_qsort);
        tide_delta_number(d, removed_count);
        tide_delta_number(d, added_count);
        for (uint32_t i = 0; i < removed_count; i++)
            tide_write_u64(&d->bytes, removed[i].lineage);
        for (uint32_t i = 0; i < count; i++)
            if (index_get(base->births, keys[i]) == UINT32_MAX)
                tide_write_u64(&d->bytes, keys[i].lineage);
    }
    else
        for (uint32_t i = 0; i < count; i++)
        {
            tide_write_u64(&d->bytes, keys[i].birth);
            tide_write_u64(&d->bytes, keys[i].lineage);
        }
    free(removed);
    free(keys);
    tide_delta_close(d);
    for (uint32_t p = 0; p < t->pages; p++)
    {
        const tide_page *b = base && p < base->pages ? base->page[p] : NULL;
        tide_delta_region(d, tide_page_data(t->page[p]), used_on(t, p) * SLOT, b ? tide_page_data(b) : NULL,
                          b ? used_on(base, p) * SLOT : 0u, b == t->page[p], SLOT);
    }
    tide_delta_close(d);
}
bool tide_entities_unpack_delta(tide_entities *t, const tide_entities *base, tide_delta_reader *d)
{
    tide_entities next = {0};
    next.next_unused = tide_delta_get_number(d);
    next.free_head = tide_delta_get_number(d);
    next.key_clock = tide_delta_get_number64(d);
    next.unkeyed_next = tide_delta_get_number64(d);
    const uint32_t count = tide_delta_get_number(d);
    const uint32_t registry_mode = tide_delta_get_number(d);
    bool ok = !d->bytes.failed && next.free_head <= next.next_unused;
    if (ok && registry_mode == 0u)
        ok = read_births(&next, &d->bytes, count);
    else if (ok && registry_mode == 1u)
    {
        const uint32_t removed = tide_delta_get_number(d);
        const uint32_t added = tide_delta_get_number(d);
        const uint32_t base_count = base && base->births ? base->births->count : 0;
        ok = !d->bytes.failed && d->xor && base && base->key_clock == next.key_clock && next.key_clock &&
             removed <= base_count && (uint64_t)base_count - removed + added == count &&
             (uint64_t)removed + added <= (d->bytes.size - d->bytes.at) / 8u;
        if (ok)
            next.births = index_copy(base->births);
        uint64_t previous = 0;
        for (uint32_t i = 0; ok && i < removed; i++)
        {
            const uint64_t lineage = tide_read_u64(&d->bytes);
            const tide_entity key = {next.key_clock, lineage};
            ok = !d->bytes.failed && (!i || lineage > previous) && index_get(next.births, key) != UINT32_MAX;
            if (ok)
                index_remove(next.births, key);
            previous = lineage;
        }
        previous = 0;
        for (uint32_t i = 0; ok && i < added; i++)
        {
            const uint64_t lineage = tide_read_u64(&d->bytes);
            const tide_entity key = {next.key_clock, lineage};
            ok = !d->bytes.failed && (!i || lineage > previous) && index_get(base->births, key) == UINT32_MAX;
            if (ok)
                index_put(&next.births, key, 0);
            previous = lineage;
        }
    }
    else if (ok && registry_mode == 2u)
    {
        uint32_t base_count = 0;
        tide_entity *base_keys = base ? birth_keys(base, &base_count) : NULL;
        const uint32_t pages = birth_pages(count);
        ok = !d->xor && pages <= (uint64_t)birth_pages(base_count) + (d->bytes.size - d->bytes.at);
        uint8_t *scratch = ok && pages ? tide_alloc(BIRTH_BYTES) : NULL;
        tide_entity previous = {0};
        for (uint32_t p = 0; ok && p < pages; p++)
        {
            const uint32_t n = birth_used(count, p), was = birth_used(base_count, p);
            const tide_entity *before = was ? base_keys + (size_t)p * BIRTH_KEYS : NULL;
            memset(scratch, 0, n * 16u);
            ok = tide_delta_get(d, scratch, n * 16u, before, was * 16u);
            tide_reader region = {scratch, n * 16u, 0, false};
            ok = ok && read_births_after(&next, &region, n, &previous);
        }
        free(scratch);
        free(base_keys);
    }
    else
        ok = false;
    ok = ok && tide_delta_closed(d);
    next.pages = (uint32_t)(((uint64_t)next.next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
    ok = ok && next.pages <= (uint64_t)(base ? base->pages : 0u) + (d->bytes.size - d->bytes.at);
    if (ok)
    {
        make_room(&next, next.pages);
        for (uint32_t p = 0; ok && p < next.pages; p++)
        {
            tide_page *b = base && p < base->pages ? base->page[p] : NULL;
            next.page[p] =
                tide_delta_page(d, used_on(&next, p) * SLOT, PAGE_BYTES, 1, b, b ? used_on(base, p) * SLOT : 0u, true);
            ok = next.page[p] != NULL;
        }
        ok = ok && tide_delta_closed(d) && rebuild_index(&next);
    }
    if (!ok)
    {
        tide_entities_free(&next);
        return false;
    }
    tide_entities_settle(&next);
    tide_entities_free(t);
    *t = next;
    return true;
}
void tide_entities_hash_pages(const tide_entities *t, tide_writer *w)
{
    uint32_t count;
    tide_entity *keys = birth_keys(t, &count);
    tide_write_varint(w, birth_pages(count));
    for (uint32_t p = 0; p < birth_pages(count); p++)
        tide_write_u64(w, tide_hash(keys + (size_t)p * BIRTH_KEYS, birth_used(count, p) * 16u));
    free(keys);
    tide_write_varint(w, t->pages);
    for (uint32_t p = 0; p < t->pages; p++)
        tide_write_u64(w, tide_page_hash(t->page[p], used_on(t, p) * SLOT));
}
void tide_entities_need_pages(const tide_entities *base, tide_needs *n)
{
    uint32_t base_count;
    tide_entity *keys = birth_keys(base, &base_count);
    const uint32_t registry_pages = tide_needs_count(n);
    for (uint32_t p = 0; p < registry_pages; p++)
    {
        const uint32_t used = birth_used(base_count, p);
        tide_needs_put(n, used != 0, used ? tide_hash(keys + (size_t)p * BIRTH_KEYS, used * 16u) : 0u);
    }
    free(keys);
    const uint32_t count = tide_needs_count(n);
    for (uint32_t p = 0; p < count; p++)
    {
        const bool have = p < base->pages;
        tide_needs_put(n, have, have ? tide_page_hash(base->page[p], used_on(base, p) * SLOT) : 0u);
    }
}
