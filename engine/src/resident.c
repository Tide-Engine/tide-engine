#include "tide/resident.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__wasm__)
#include <sys/mman.h>
#endif

// ---------------------------------------------------------------------------
// The arena's memory, and the allocator in it.
//
// The allocator's own state is in the arena's first page, so that a commit
// keeps it with the blocks and a restore brings it back with them: after a
// rollback the free lists are the ones the restored state had. Blocks under a
// page come from slabs, a page each, of one size class (64 bytes to 8 KiB,
// doubling), linked through their first 4 bytes as offsets from the base.
// Blocks of a page and more are runs of whole pages, bumped from the end of
// what's used, or taken from the runs freed (kept in a small table, joined
// with their neighbours). Nothing moves, and no block has a header: the
// library says a block's size when it frees it, as Box2D and Box3D do.

enum {
    CLASS_LEAST = 6,                          // 64 bytes
    CLASSES = TIDE_ARENA_PAGE_SHIFT - CLASS_LEAST, // ...up to half a page
    FREE_RUNS = 256,
    MAGIC = 0x52455349,                       // "RESI"
};

typedef struct free_run {
    uint32_t page, pages;
} free_run;

// The first page.
typedef struct arena_header {
    uint32_t magic;
    uint32_t pages_used;            // The pages handed out so far, this one included
    uint32_t class_free[CLASSES];   // Free list heads: a block's offset, 0 for none
    uint32_t runs;
    free_run run[FREE_RUNS];
    uint8_t user[TIDE_ARENA_USER];
} arena_header;

struct tide_arena {
    uint8_t *base;
    size_t bytes;      // Reserved
    size_t committed;  // Native: what's been asked of the system so far
    uint32_t pages;    // bytes / TIDE_ARENA_PAGE
    uint64_t kind;
    char name[64];
    uint64_t stamp;
    uint32_t lock;
    uint8_t *touched;  // A byte a page, written from any thread
    tide_page **live;  // The page last committed or restored at each place, a reference each
};

_Static_assert(sizeof(arena_header) <= TIDE_ARENA_PAGE, "the allocator's state fits the first page");

static void lock(tide_arena *a)
{
    while (__atomic_exchange_n(&a->lock, 1u, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(tide_arena *a)
{
    __atomic_store_n(&a->lock, 0u, __ATOMIC_RELEASE);
}

static arena_header *header(const tide_arena *a)
{
    return (arena_header *)(void *)a->base;
}

_Noreturn static void out_of_room(const tide_arena *a)
{
    fprintf(stderr, "tide: the %s state ran out of its arena (%zu MiB)\n", a->name, a->bytes >> 20);
    fflush(stderr);
    abort();
}

static uint8_t *reserve(size_t bytes)
{
#if defined(_WIN32)
    return VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_READWRITE);
#elif defined(__wasm__)
    return tide_alloc(bytes); // Linear memory, which the browser backs as it's touched
#else
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}

// Native Windows reserves and commits apart; the others back pages as they're
// touched. The arena's memory is zero the first time it's touched everywhere.
static void commit_to(tide_arena *a, const size_t bytes)
{
    if (bytes <= a->committed) return;
#if defined(_WIN32)
    const size_t step = 1u << 20;
    const size_t to = (bytes + step - 1u) / step * step < a->bytes ? (bytes + step - 1u) / step * step : a->bytes;
    if (!VirtualAlloc(a->base + a->committed, to - a->committed, MEM_COMMIT, PAGE_READWRITE)) out_of_room(a);
    a->committed = to;
#else
    a->committed = bytes;
#endif
}

static void touch_pages(tide_arena *a, const uint32_t first, const uint32_t end)
{
    for (uint32_t p = first; p < end; p++) __atomic_store_n(&a->touched[p], 1u, __ATOMIC_RELAXED);
}

static void touch_range(tide_arena *a, const size_t from, const size_t bytes)
{
    if (bytes == 0) return;
    touch_pages(a, (uint32_t)(from >> TIDE_ARENA_PAGE_SHIFT), (uint32_t)((from + bytes - 1u) >> TIDE_ARENA_PAGE_SHIFT) + 1u);
}

static void fresh(tide_arena *a)
{
    commit_to(a, TIDE_ARENA_PAGE);
    arena_header *h = header(a);
    memset(h, 0, sizeof *h);
    h->magic = MAGIC;
    h->pages_used = 1;
    touch_pages(a, 0, 1);
}

static tide_arena *arena_new(const uint64_t kind, const char *name, const size_t bytes)
{
    tide_arena *a = calloc(1, sizeof *a);
    if (!a) tide_out_of_memory();
    a->bytes = (bytes + TIDE_ARENA_PAGE - 1u) / TIDE_ARENA_PAGE * TIDE_ARENA_PAGE;
    a->pages = (uint32_t)(a->bytes / TIDE_ARENA_PAGE);
    a->base = reserve(a->bytes);
    if (!a->base) tide_out_of_memory();
    a->touched = calloc(a->pages, 1);
    a->live = calloc(a->pages, sizeof *a->live);
    if (!a->touched || !a->live) tide_out_of_memory();
    a->kind = kind;
    snprintf(a->name, sizeof a->name, "%s", name);
    fresh(a);
    return a;
}

// Pages from the end of what's used: the first of them.
static uint32_t bump(tide_arena *a, const uint32_t pages)
{
    arena_header *h = header(a);
    if ((uint64_t)h->pages_used + pages > a->pages) out_of_room(a);
    const uint32_t first = h->pages_used;
    h->pages_used += pages;
    commit_to(a, (size_t)h->pages_used << TIDE_ARENA_PAGE_SHIFT);
    return first;
}

static uint32_t take_run(tide_arena *a, const uint32_t pages)
{
    arena_header *h = header(a);
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < h->runs; i++) {
        if (h->run[i].pages >= pages && (best == UINT32_MAX || h->run[i].pages < h->run[best].pages)) best = i;
    }
    if (best == UINT32_MAX) return bump(a, pages);
    const uint32_t first = h->run[best].page;
    h->run[best].page += pages;
    h->run[best].pages -= pages;
    if (h->run[best].pages == 0) h->run[best] = h->run[--h->runs];
    return first;
}

static void give_run(tide_arena *a, uint32_t page, uint32_t pages)
{
    arena_header *h = header(a);
    // Joined with the runs that end where it starts or start where it ends
    for (uint32_t i = 0; i < h->runs;) {
        if (h->run[i].page + h->run[i].pages == page) {
            page = h->run[i].page;
            pages += h->run[i].pages;
            h->run[i] = h->run[--h->runs];
        } else if (page + pages == h->run[i].page) {
            pages += h->run[i].pages;
            h->run[i] = h->run[--h->runs];
        } else {
            i++;
        }
    }
    if (page + pages == h->pages_used) { // At the end: what's used shrinks
        h->pages_used = page;
        return;
    }
    if (h->runs == FREE_RUNS) return; // Lost until the arena is empty again: a table this full is a leak of the library's
    h->run[h->runs].page = page;
    h->run[h->runs].pages = pages;
    h->runs++;
}

static uint32_t class_of(const size_t size)
{
    uint32_t k = 0;
    while (((size_t)1 << (CLASS_LEAST + k)) < size) k++;
    return k;
}

void *tide_arena_alloc(tide_arena *a, size_t size)
{
    if (size == 0) size = 1;
    lock(a);
    arena_header *h = header(a);
    size_t at;
    if (size > TIDE_ARENA_PAGE / 2u) {
        const uint32_t pages = (uint32_t)((size + TIDE_ARENA_PAGE - 1u) >> TIDE_ARENA_PAGE_SHIFT);
        at = (size_t)take_run(a, pages) << TIDE_ARENA_PAGE_SHIFT;
        size = (size_t)pages << TIDE_ARENA_PAGE_SHIFT;
    } else {
        const uint32_t k = class_of(size);
        const size_t block = (size_t)1 << (CLASS_LEAST + k);
        if (!h->class_free[k]) { // A slab of them
            const size_t slab = (size_t)bump(a, 1) << TIDE_ARENA_PAGE_SHIFT;
            uint32_t head = 0;
            for (size_t off = TIDE_ARENA_PAGE - block; off >= block; off -= block) { // The first is handed out, the rest linked
                *(uint32_t *)(void *)(a->base + slab + off) = head;
                head = (uint32_t)(slab + off);
                if (off == block) break;
            }
            h->class_free[k] = head;
            touch_range(a, slab, TIDE_ARENA_PAGE);
            at = slab;
        } else {
            at = h->class_free[k];
            h->class_free[k] = *(uint32_t *)(void *)(a->base + at);
        }
        size = block;
    }
    touch_pages(a, 0, 1);
    touch_range(a, at, size);
    unlock(a);
    return a->base + at;
}

void tide_arena_free(tide_arena *a, void *p, const size_t size)
{
    if (!p) return;
    const size_t at = (size_t)((uint8_t *)p - a->base);
    lock(a);
    arena_header *h = header(a);
    if (size > TIDE_ARENA_PAGE / 2u) {
        give_run(a, (uint32_t)(at >> TIDE_ARENA_PAGE_SHIFT), (uint32_t)((size + TIDE_ARENA_PAGE - 1u) >> TIDE_ARENA_PAGE_SHIFT));
    } else {
        const uint32_t k = class_of(size < 1u ? 1u : size);
        *(uint32_t *)p = h->class_free[k];
        h->class_free[k] = (uint32_t)at;
        touch_range(a, at, sizeof(uint32_t));
    }
    touch_pages(a, 0, 1);
    unlock(a);
}

void *tide_arena_realloc(tide_arena *a, void *p, const size_t old_size, const size_t size)
{
    void *q = tide_arena_alloc(a, size);
    if (p) {
        memcpy(q, p, old_size < size ? old_size : size);
        tide_arena_free(a, p, old_size);
    }
    return q;
}

bool tide_arena_holds(const tide_arena *a, const void *p)
{
    return (const uint8_t *)p >= a->base && (const uint8_t *)p < a->base + a->bytes;
}

void *tide_arena_user(tide_arena *a)
{
    touch_pages(a, 0, 1);
    return header(a)->user;
}

uint64_t tide_arena_stamp(const tide_arena *a)
{
    return a->stamp;
}

void tide_arena_touch(tide_arena *a, const void *p, const size_t size)
{
    if (!tide_arena_holds(a, p)) return;
    touch_range(a, (size_t)((const uint8_t *)p - a->base), size);
}

void tide_arena_touch_all(tide_arena *a)
{
    touch_pages(a, 0, header(a)->pages_used);
}

size_t tide_arena_used(const tide_arena *a)
{
    return (size_t)header(a)->pages_used << TIDE_ARENA_PAGE_SHIFT;
}

uint32_t tide_arena_pages(const tide_arena *a)
{
    return header(a)->pages_used;
}

// ---------------------------------------------------------------------------
// Commits and restores.

static tide_resident_pages *pages_new(const uint32_t count)
{
    tide_resident_pages *t = calloc(1, sizeof *t + (size_t)count * sizeof(tide_page *));
    if (!t) tide_out_of_memory();
    t->refs = 1;
    t->count = count;
    return t;
}

static void pages_release(tide_resident_pages *t)
{
    if (!t || --t->refs) return;
    for (uint32_t i = 0; i < t->count; i++) tide_page_release(t->page[i], 1);
    free(t);
}

// A table of `count` pages this resident alone holds, with what it had.
static tide_resident_pages *pages_own(tide_resident *r, const uint32_t count)
{
    tide_resident_pages *old = r->pages;
    if (old && old->refs == 1 && old->count == count) return old;
    tide_resident_pages *t = pages_new(count);
    const uint32_t keep = old ? (old->count < count ? old->count : count) : 0;
    for (uint32_t i = 0; i < keep; i++) {
        t->page[i] = old->page[i];
        if (t->page[i]) tide_page_retain(t->page[i]);
    }
    pages_release(old);
    r->pages = t;
    return t;
}

static void set_live(tide_arena *a, const uint32_t i, tide_page *p)
{
    if (a->live[i] == p) return;
    if (p) tide_page_retain(p);
    tide_page_release(a->live[i], 1);
    a->live[i] = p;
}

void tide_arena_commit(tide_arena *a, tide_resident *r, const uint64_t stamp, const uint64_t digest)
{
    const uint32_t used = header(a)->pages_used;
    tide_resident_pages *t = pages_own(r, used);
    for (uint32_t i = 0; i < used; i++) {
        if (!a->touched[i]) continue;
        a->touched[i] = 0;
        tide_page *p = tide_page_take(TIDE_ARENA_PAGE);
        p->refs = 1;
        p->size = TIDE_ARENA_PAGE;
        p->first = 0;
        p->hashed = UINT32_MAX;
        p->was = 0;
        memcpy(tide_page_data(p), a->base + ((size_t)i << TIDE_ARENA_PAGE_SHIFT), TIDE_ARENA_PAGE);
        tide_page_release(t->page[i], 1);
        t->page[i] = p;
        set_live(a, i, p);
    }
    for (uint32_t i = used; i < a->pages; i++) { // Pages the state gave back: whatever is there now is nobody's
        a->touched[i] = 0;
        set_live(a, i, NULL);
    }
    a->stamp = stamp;
    r->kind = a->kind;
    r->stamp = stamp;
    r->digest = digest;
}

void tide_arena_restore(tide_arena *a, const tide_resident *r)
{
    const tide_resident_pages *t = r->pages;
    const uint32_t count = t ? t->count : 0;
    for (uint32_t i = 0; i < count; i++) {
        tide_page *p = t->page[i];
        if (a->live[i] == p) continue;
        if (p) {
            commit_to(a, (size_t)(i + 1u) << TIDE_ARENA_PAGE_SHIFT);
            memcpy(a->base + ((size_t)i << TIDE_ARENA_PAGE_SHIFT), tide_page_data(p), TIDE_ARENA_PAGE);
        }
        set_live(a, i, p);
    }
    for (uint32_t i = count; i < a->pages; i++) set_live(a, i, NULL);
    memset(a->touched, 0, a->pages);
    if (!t) {
        fresh(a);
        a->touched[0] = 0;
        a->stamp = 0;
        return;
    }
    a->stamp = r->stamp;
}

int64_t tide_arena_differs(const tide_arena *a, const tide_resident *r)
{
    const tide_resident_pages *t = r->pages;
    const uint32_t count = t ? t->count : 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!t->page[i]) continue;
        if (!tide_same_bytes(a->base + ((size_t)i << TIDE_ARENA_PAGE_SHIFT), tide_page_data(t->page[i]), TIDE_ARENA_PAGE)) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Kinds and the registry.

static tide_resident_kind kinds[TIDE_RESIDENT_KINDS];
static char kind_names[TIDE_RESIDENT_KINDS][64];
static uint32_t kind_count;
static tide_residents own_table;
static tide_residents *table = &own_table;
static bool relinked[TIDE_RESIDENT_KINDS]; // This build has taken the kind's arena over

uint64_t tide_resident_kind_of(const char *name)
{
    const uint64_t h = tide_hash(name, (uint32_t)strlen(name));
    return h ? h : 1u;
}

uint64_t tide_resident_register(const tide_resident_kind *kind)
{
    const uint64_t number = tide_resident_kind_of(kind->name);
    uint32_t i = 0;
    while (i < kind_count && tide_resident_kind_of(kinds[i].name) != number) i++;
    if (i == kind_count) {
        if (kind_count == TIDE_RESIDENT_KINDS) {
            fprintf(stderr, "tide: no room for a %s resident: a process has %d kinds\n", kind->name, TIDE_RESIDENT_KINDS);
            fflush(stderr);
            abort();
        }
        kind_count++;
    }
    kinds[i] = *kind;
    snprintf(kind_names[i], sizeof kind_names[i], "%s", kind->name);
    kinds[i].name = kind_names[i];
    return number;
}

static const tide_resident_kind *kind_at(const uint64_t number)
{
    for (uint32_t i = 0; i < kind_count; i++) {
        if (tide_resident_kind_of(kinds[i].name) == number) return &kinds[i];
    }
    return NULL;
}

void tide_residents_adopt(tide_residents *t)
{
    if (!t || t == table) return;
    bool empty = true;
    for (uint32_t i = 0; i < TIDE_RESIDENT_KINDS; i++) empty = empty && !t->arena[i];
    if (empty) *t = *table; // What this process has already is what the host keeps from here
    table = t;
    memset(relinked, 0, sizeof relinked);
}

tide_arena *tide_resident_arena(const uint64_t kind)
{
    uint32_t i = 0;
    while (i < TIDE_RESIDENT_KINDS && table->arena[i] && table->kind[i] != kind) i++;
    if (i == TIDE_RESIDENT_KINDS) {
        fprintf(stderr, "tide: no room for another resident's arena: a process has %d\n", TIDE_RESIDENT_KINDS);
        fflush(stderr);
        abort();
    }
    const tide_resident_kind *k = kind_at(kind);
    if (!table->arena[i]) {
        char name[64];
        snprintf(name, sizeof name, "%s", k ? k->name : "resident");
        table->kind[i] = kind;
        table->arena[i] = arena_new(kind, name, k && k->arena_bytes ? k->arena_bytes : TIDE_ARENA_DEFAULT);
        relinked[i] = true;
    } else if (!relinked[i]) { // Made by another build of the library: it holds what that build left
        relinked[i] = true;
        if (k && k->relink) k->relink(k->context, table->arena[i]);
    }
    return table->arena[i];
}

// ---------------------------------------------------------------------------
// The field.

void tide_resident_copy(tide_resident *to, const tide_resident *from)
{
    if (to == from) return;
    if (from->pages) from->pages->refs++;
    pages_release(to->pages);
    *to = *from;
}

void tide_resident_free(tide_resident *r)
{
    pages_release(r->pages);
    memset(r, 0, sizeof *r);
}

// The canonical bytes of the last state packed, so that asking their size
// and then writing them is one call of the library's.
static struct {
    const tide_resident_pages *pages;
    uint64_t kind, stamp;
    tide_writer bytes;
} packed;

static const tide_writer *canonical(const tide_resident *r)
{
    if (packed.bytes.data && packed.pages == r->pages && packed.kind == r->kind && packed.stamp == r->stamp) return &packed.bytes;
    const tide_resident_kind *k = kind_at(r->kind);
    packed.bytes.size = 0;
    packed.bytes.overflow = false;
    packed.bytes.grows = true;
    if (k && k->pack) k->pack(k->context, r, &packed.bytes);
    if (!packed.bytes.data) packed.bytes.data = tide_alloc(1); // Nothing: still a cache entry
    packed.pages = r->pages;
    packed.kind = r->kind;
    packed.stamp = r->stamp;
    return &packed.bytes;
}

uint32_t tide_resident_packed_size(const tide_resident *r)
{
    const uint32_t bytes = r->kind ? canonical(r)->size : 0;
    return 8u + 4u + bytes; // The kind, the size, the bytes
}

void tide_resident_pack(const tide_resident *r, tide_writer *w)
{
    tide_write_u64(w, r->kind);
    if (!r->kind) {
        tide_write_u32(w, 0);
        return;
    }
    const tide_writer *c = canonical(r);
    tide_write_u32(w, c->size);
    tide_write_bytes(w, c->data, c->size);
    packed.pages = NULL; // The world that's packed may be about to go, and its table's address come round again
}

bool tide_resident_unpack(tide_resident *r, tide_reader *rd)
{
    const uint64_t kind = tide_read_u64(rd);
    const uint32_t size = tide_read_u32(rd);
    if (rd->failed || size > rd->size - rd->at) return false;
    pages_release(r->pages);
    memset(r, 0, sizeof *r);
    r->kind = kind;
    if (!kind) return true;
    const tide_resident_kind *k = kind_at(kind);
    tide_reader bytes = {rd->data + rd->at, size, 0, false};
    rd->at += size;
    if (!k || !k->unpack) {
        fprintf(stderr, "tide: a world holds resident state of a kind this build doesn't have (%016llx): left empty\n",
                (unsigned long long)kind);
        return true;
    }
    return k->unpack(k->context, r, &bytes, size);
}
