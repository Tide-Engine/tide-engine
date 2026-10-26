#include <stdlib.h>
#include <string.h>

#include "tide/resident.h"
#include "tide_test.h"

// Resident state (tide/resident.h): the arena a library allocates from, what
// a commit keeps of it and what a restore brings back, and the field's own
// functions. No library here: the test stands in for one.

static tide_arena *arena_of(const char *name)
{
    tide_resident_kind k = {.name = name};
    return tide_resident_arena(tide_resident_register(&k));
}

static void fill(uint8_t *p, const size_t size, uint32_t dice)
{
    for (size_t i = 0; i < size; i++) {
        dice = dice * 1664525u + 1013904223u;
        p[i] = (uint8_t)(dice >> 24);
    }
}

static bool filled(const uint8_t *p, const size_t size, uint32_t dice)
{
    for (size_t i = 0; i < size; i++) {
        dice = dice * 1664525u + 1013904223u;
        if (p[i] != (uint8_t)(dice >> 24)) return false;
    }
    return true;
}

TIDE_TEST(resident_arena_hands_out_aligned_blocks_and_takes_them_back)
{
    tide_arena *a = arena_of("blocks");
    const size_t sizes[] = {1, 40, 64, 65, 100, 1000, 4096, 8192, 8193, 16384, 100000, 1u << 20};
    void *blocks[12];
    for (int i = 0; i < 12; i++) {
        blocks[i] = tide_arena_alloc(a, sizes[i]);
        TIDE_REQUIRE(blocks[i]);
        TIDE_CHECK(tide_arena_holds(a, blocks[i]));
        TIDE_CHECK(((uintptr_t)blocks[i] & 63u) == 0);
        if (sizes[i] > TIDE_ARENA_PAGE / 2) TIDE_CHECK(((uintptr_t)blocks[i] & (TIDE_ARENA_PAGE - 1u)) == 0);
        fill(blocks[i], sizes[i], (uint32_t)i + 1u);
    }
    for (int i = 0; i < 12; i++) TIDE_CHECK(filled(blocks[i], sizes[i], (uint32_t)i + 1u));
    const uint32_t used = tide_arena_pages(a);
    tide_arena_free(a, blocks[11], sizes[11]);
    TIDE_CHECK(tide_arena_pages(a) < used);
    tide_arena_free(a, blocks[1], sizes[1]);
    void *again = tide_arena_alloc(a, 40);
    TIDE_CHECK(again == blocks[1]);
    uint8_t *r = tide_arena_realloc(a, blocks[5], sizes[5], 5000);
    TIDE_CHECK(filled(r, sizes[5], 6u));
    tide_arena_free(a, r, 5000);
    void *x = tide_arena_alloc(a, 3 * TIDE_ARENA_PAGE), *y = tide_arena_alloc(a, 3 * TIDE_ARENA_PAGE),
         *z = tide_arena_alloc(a, 3 * TIDE_ARENA_PAGE);
    void *after = tide_arena_alloc(a, TIDE_ARENA_PAGE);
    tide_arena_free(a, x, 3 * TIDE_ARENA_PAGE);
    tide_arena_free(a, z, 3 * TIDE_ARENA_PAGE);
    tide_arena_free(a, y, 3 * TIDE_ARENA_PAGE);
    void *big = tide_arena_alloc(a, 9 * TIDE_ARENA_PAGE);
    TIDE_CHECK(big == x);
    tide_arena_free(a, big, 9 * TIDE_ARENA_PAGE);
    tide_arena_free(a, after, TIDE_ARENA_PAGE);
    TIDE_CHECK(tide_arena_holds(a, tide_arena_user(a)));
}

TIDE_TEST(resident_commits_what_was_touched_and_restores_it)
{
    tide_arena *a = arena_of("states");
    tide_resident r1 = {0}, r2 = {0}, r3 = {0};
    enum { N = 6 };
    uint8_t *blocks[N];
    for (int i = 0; i < N; i++) blocks[i] = tide_arena_alloc(a, 3000);
    for (int i = 0; i < N; i++) fill(blocks[i], 3000, 10u + (uint32_t)i);
    memcpy(tide_arena_user(a), "one", 4);
    tide_arena_commit(a, &r1, 1, 101);
    TIDE_CHECK(r1.stamp == 1 && r1.digest == 101 && r1.kind == tide_resident_kind_of("states"));
    TIDE_CHECK(tide_arena_stamp(a) == 1);
    TIDE_CHECK(tide_arena_differs(a, &r1) == -1);
    TIDE_REQUIRE(r1.pages && r1.pages->count == tide_arena_pages(a));

    tide_resident_copy(&r2, &r1);
    TIDE_CHECK(r2.pages == r1.pages && r1.pages->refs == 2);

    fill(blocks[2], 3000, 99u);
    tide_arena_touch(a, blocks[2], 3000);
    memcpy(tide_arena_user(a), "two", 4);
    tide_arena_commit(a, &r1, 2, 102);
    TIDE_CHECK(r1.pages != r2.pages && r2.pages->refs == 1 && r1.pages->refs == 1);
    TIDE_CHECK(r2.stamp == 1 && r1.stamp == 2);
    TIDE_CHECK(tide_arena_differs(a, &r1) == -1);
    uint32_t shared = 0, apart = 0;
    for (uint32_t p = 0; p < r2.pages->count; p++) {
        if (r1.pages->page[p] == r2.pages->page[p]) shared++;
        else apart++;
    }
    TIDE_CHECK(apart == 2 && shared >= 1);

    blocks[4][7] ^= 1u;
    TIDE_CHECK(tide_arena_differs(a, &r1) >= 1);
    blocks[4][7] ^= 1u;

    // A read-side operation may write scratch inside the arena. Its page
    // identity still matches, but an uncommitted write must be restored too.
    TIDE_CHECK(tide_arena_is(a, &r1));
    blocks[4][7] ^= 1u;
    tide_arena_touch(a, &blocks[4][7], 1);
    TIDE_CHECK(!tide_arena_is(a, &r1));
    tide_arena_restore(a, &r1);
    TIDE_CHECK(filled(blocks[4], 3000, 14u));
    TIDE_CHECK(tide_arena_is(a, &r1) && tide_arena_differs(a, &r1) == -1);

    tide_arena_restore(a, &r2);
    TIDE_CHECK(tide_arena_stamp(a) == 1);
    TIDE_CHECK(filled(blocks[2], 3000, 12u));
    TIDE_CHECK(memcmp(tide_arena_user(a), "one", 4) == 0);
    TIDE_CHECK(tide_arena_differs(a, &r2) == -1);
    tide_arena_restore(a, &r1);
    TIDE_CHECK(filled(blocks[2], 3000, 99u));
    TIDE_CHECK(memcmp(tide_arena_user(a), "two", 4) == 0);
    TIDE_CHECK(tide_arena_differs(a, &r1) == -1);

    uint8_t *more = tide_arena_alloc(a, 5 * TIDE_ARENA_PAGE);
    fill(more, 5 * TIDE_ARENA_PAGE, 7u);
    tide_arena_commit(a, &r3, 3, 103);
    TIDE_CHECK(r3.pages->count > r1.pages->count);
    tide_arena_restore(a, &r2);
    TIDE_CHECK(tide_arena_pages(a) == r2.pages->count);
    tide_arena_restore(a, &r3);
    TIDE_CHECK(filled(more, 5 * TIDE_ARENA_PAGE, 7u));
    TIDE_CHECK(tide_arena_differs(a, &r3) == -1);
    tide_arena_free(a, more, 5 * TIDE_ARENA_PAGE);
    TIDE_CHECK(tide_arena_pages(a) == r1.pages->count);

    tide_resident none = {0};
    tide_arena_restore(a, &none);
    TIDE_CHECK(tide_arena_pages(a) == 1 && tide_arena_stamp(a) == 0);
    tide_resident_free(&r1);
    tide_resident_free(&r2);
    tide_resident_free(&r3);
    TIDE_CHECK(!r1.pages && !r1.kind);
}

TIDE_TEST(resident_tables_are_shared_until_a_commit)
{
    tide_arena *a = arena_of("shares");
    tide_resident r = {0}, s = {0}, t = {0};
    uint8_t *b = tide_arena_alloc(a, 100);
    fill(b, 100, 1u);
    tide_arena_commit(a, &r, 1, 1);
    tide_resident_copy(&s, &r);
    tide_resident_copy(&t, &r);
    TIDE_CHECK(r.pages->refs == 3);
    fill(b, 100, 2u);
    tide_arena_touch(a, b, 100);
    tide_arena_commit(a, &r, 2, 2);
    TIDE_CHECK(s.pages->refs == 2 && r.pages->refs == 1);
    tide_resident_free(&s);
    TIDE_CHECK(t.pages->refs == 1);
    tide_resident_copy(&t, &r);
    TIDE_CHECK(r.pages->refs == 2);
    tide_arena_restore(a, &t);
    TIDE_CHECK(filled(b, 100, 2u));
    tide_resident_free(&t);
    tide_resident_free(&r);
}

static int packed_calls;

static void stand_in_pack(void *context, const tide_resident *r, tide_writer *w)
{
    (void)context;
    packed_calls++;
    tide_write_u64(w, r->stamp * 10u);
    tide_write_bytes(w, "hello", 5);
}

static bool stand_in_unpack(void *context, tide_resident *r, tide_reader *rd, const uint32_t size)
{
    (void)context;
    if (size != 13) return false;
    const uint64_t n = tide_read_u64(rd);
    if (memcmp(tide_read_bytes(rd, 5), "hello", 5) != 0) return false;
    r->stamp = n / 10u;
    r->digest = 7;
    return true;
}

TIDE_TEST(resident_packs_through_its_kind)
{
    tide_resident_kind k = {.name = "packs", .pack = stand_in_pack, .unpack = stand_in_unpack};
    const uint64_t kind = tide_resident_register(&k);
    tide_resident r = {.kind = kind, .stamp = 4, .digest = 9};
    TIDE_CHECK(tide_resident_packed_size(&r) == 8 + 4 + 13);
    uint8_t buf[64];
    tide_writer w = {buf, sizeof buf, 0, false, false};
    tide_resident_pack(&r, &w);
    TIDE_CHECK(w.size == 25 && packed_calls == 1);
    tide_resident back = {0};
    tide_reader rd = {buf, w.size, 0, false};
    TIDE_CHECK(tide_resident_unpack(&back, &rd));
    TIDE_CHECK(back.kind == kind && back.stamp == 4 && back.digest == 7 && rd.at == w.size);
    tide_resident_kind again = {.name = "packs"};
    TIDE_CHECK(tide_resident_register(&again) == kind);
    tide_resident none = {0};
    TIDE_CHECK(tide_resident_packed_size(&none) == 12);
    w.size = 0;
    tide_resident_pack(&none, &w);
    rd = (tide_reader){buf, w.size, 0, false};
    TIDE_CHECK(tide_resident_unpack(&back, &rd) && back.kind == 0);
    rd = (tide_reader){buf, 3, 0, false};
    TIDE_CHECK(!tide_resident_unpack(&back, &rd));
}

TIDE_TEST(resident_registry_is_adopted_with_its_arenas)
{
    tide_arena *a = arena_of("kept");
    tide_residents table = {0};
    tide_residents_adopt(&table);
    TIDE_CHECK(tide_resident_arena(tide_resident_kind_of("kept")) == a);
    bool there = false;
    for (int i = 0; i < TIDE_RESIDENT_KINDS; i++) there = there || table.arena[i] == a;
    TIDE_CHECK(there);
    memset(&table, 0, sizeof table);
    tide_residents_adopt(&table);
    TIDE_CHECK(tide_resident_arena(tide_resident_kind_of("kept")) == a);
    tide_residents_release(&table);
    memset(&table, 0xa5, sizeof table);
    TIDE_CHECK(tide_resident_arena(tide_resident_kind_of("kept")) == a);
}

static void pack_arena_value(void *context, const tide_resident *r, tide_writer *w)
{
    tide_arena *a = context;
    tide_arena_restore(a, r);
    uint64_t value;
    memcpy(&value, tide_arena_user(a), sizeof value);
    tide_write_u64(w, value);
}

TIDE_TEST(resident_canonical_size_cache_keeps_the_state_it_sized)
{
    const uint64_t kind = tide_resident_kind_of("cache");
    tide_arena *a = tide_resident_arena(kind);
    const tide_resident_kind k = {.name = "cache", .pack = pack_arena_value, .context = a};
    tide_resident_register(&k);
    tide_resident r = {0};
    uint64_t value = 11;
    memcpy(tide_arena_user(a), &value, sizeof value);
    tide_arena_commit(a, &r, 1, value);
    TIDE_CHECK(tide_resident_packed_size(&r) == 20);
    tide_resident_pages *sized = r.pages;
    TIDE_CHECK(sized->refs == 2);

    value = 22;
    memcpy(tide_arena_user(a), &value, sizeof value);
    tide_arena_commit(a, &r, 1, value);
    TIDE_CHECK(r.pages != sized);
    uint8_t bytes[20];
    tide_writer out = {bytes, sizeof bytes, 0, false, false};
    tide_resident_pack(&r, &out);
    tide_reader in = {bytes, out.size, 0, false};
    TIDE_CHECK(tide_read_u64(&in) == kind);
    TIDE_CHECK(tide_read_u32(&in) == sizeof value);
    TIDE_CHECK(tide_read_u64(&in) == value && !in.failed);
    TIDE_CHECK(r.pages->refs == 1);
    TIDE_CHECK(tide_resident_packed_size(&r) == 20 && r.pages->refs == 2);
    const tide_resident empty = {0};
    out.size = 0;
    tide_resident_pack(&empty, &out);
    TIDE_CHECK(out.size == 12 && r.pages->refs == 1);
    tide_resident_free(&r);
}

static void pack_digest(void *context, const tide_resident *r, tide_writer *w)
{
    (void)context;
    tide_write_u64(w, r->digest);
}

TIDE_TEST(resident_canonical_size_cache_checks_the_digest_without_pages)
{
    const tide_resident_kind k = {.name = "digest-cache", .pack = pack_digest};
    tide_resident r = {.kind = tide_resident_register(&k), .stamp = 1, .digest = 11};
    TIDE_CHECK(tide_resident_packed_size(&r) == 20);
    r.digest = 22;
    uint8_t bytes[20];
    tide_writer out = {bytes, sizeof bytes, 0, false, false};
    tide_resident_pack(&r, &out);
    tide_reader in = {bytes, out.size, 0, false};
    TIDE_CHECK(tide_read_u64(&in) == r.kind);
    TIDE_CHECK(tide_read_u32(&in) == sizeof r.digest);
    TIDE_CHECK(tide_read_u64(&in) == 22 && !in.failed);
}
