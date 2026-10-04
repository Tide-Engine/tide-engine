#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

static _Thread_local int32_t begun;
static _Thread_local int32_t marks;
int32_t batch_begin(const int32_t i) { begun = i; return i; }
int32_t batch_call(const int32_t i, const int32_t j) { return begun == i ? j : -1000000; }
int32_t batch_mark(const int32_t value) { (void)value; return ++marks; }

static void work(void *self, void (*run)(void *, uint32_t), void *context)
{
    (void)self;
    run(context, 0);
}

static Item item_at(const tide_world *w, const int32_t i)
{
    return *(const Item *)tide_list_read(&w->heap, w->Field.items, i, sizeof(Item));
}

static void reference(Item *next, const Item *was, const int32_t count, const float dt)
{
    for (int32_t i = 0; i < count; i++) {
        Item item = was[i];
        tide_float3 pull = {0};
        float ordered = 0.0f;
        for (int32_t j = 0; j < count; j++) {
            const Item other = was[j];
            const tide_float3 d = tide_sub_f3(other.position, item.position);
            const float r2 = tide_dot_f3(d, d) + 0.01f;
            pull = tide_add_f3(pull, tide_mul_f3(d, tide_f3_splat(other.mass / (r2 * tide_sqrt_f(r2)))));
            ordered += other.mass;
        }
        item.velocity = tide_add_f3(item.velocity, tide_mul_f3(pull, tide_f3_splat(dt)));
        item.root = tide_sqrt_f(item.seed);
        item.ordered = ordered;
        // Adding zero still matters for signed zero; keep the source's order.
        item.position = tide_add_f3(item.position, tide_mul_f3((tide_float3){0}, tide_f3_splat(dt)));
        item.divergent = 0;
        for (int32_t j = 0; j < count; j++) { j += i % 2; item.divergent += j; }
        item.bounded = 0;
        for (int32_t j = 0; j < i % 5; j++) item.bounded += j;
        item.calls = i + count * (count - 1) / 2;
        item.defaults = count * (count + 1) / 2;
        item.aggregateDefaults = 2 * count * (count + 1);
        next[i] = item;
    }
}

TIDE_TEST(parallel_list_batches_keep_exact_order_bounds_calls_and_snapshots)
{
    // Empty/short lists, four lanes, task tails and elements across heap pages.
    const int32_t counts[] = {0, 1, 2, 3, 4, 7, 17, 321, 322, 337};
    for (uint32_t c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        const int32_t count = counts[c];
        tide_world one = {0}, many = {0}, snapshot = {0};
        tide_world_init(&one, 1.0f / 60.0f);
        one.Field.count = count;
        tide_world_tick(&one);
        // The first three make reassociation of the reduction change its bits.
        tide_text_use(&one.heap, NULL);
        const float ordered[] = {1e20f, -1e20f, 1.0f};
        for (int32_t i = 0; i < count && i < 3; i++) {
            ((Item *)tide_list_at_mut(one.Field.items, i, sizeof(Item)))->mass = ordered[i];
        }
        // Exact sqrt for signed zero, positive infinity and subnormals,
        // without NaNs in the world hashes or reduction oracle.
        const uint32_t seeds[] = {0x80000000u, 0x7f800000u, 1u};
        for (int32_t i = 3; i < count && i < 6; i++) {
            Item *item = tide_list_at_mut(one.Field.items, i, sizeof(Item));
            memcpy(&item->seed, &seeds[i - 3], sizeof item->seed);
        }
        one.Field.run = true;
        tide_world_copy(&many, &one);
        tide_world_copy(&snapshot, &one);
        const uint64_t before = tide_world_hash(&snapshot);
        Item *was = malloc((size_t)(count ? count : 1) * sizeof *was);
        Item *next = malloc((size_t)(count ? count : 1) * sizeof *next);
        TIDE_REQUIRE(was && next);
        for (int32_t i = 0; i < count; i++) was[i] = item_at(&one, i);
        const tide_jobs jobs = {NULL, 2u, work, NULL};
        for (int tick = 0; tick < 3; tick++) {
            reference(next, was, count, one.Time.dt);
            tide_world_tick(&one);
            tide_world_tick_on(&many, &jobs);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
            TIDE_CHECK(tide_world_hash(&snapshot) == before);
            for (int32_t i = 0; i < count; i++) {
                const Item got = item_at(&one, i);
                TIDE_CHECK(memcmp(&got, &next[i], sizeof got) == 0);
            }
            memcpy(was, next, (size_t)count * sizeof *was);
        }
        free(next);
        free(was);
        tide_world_free(&snapshot);
        tide_world_free(&many);
        tide_world_free(&one);
    }
}

TIDE_TEST(parallel_list_sqrt_keeps_nan_classification)
{
    // NaN payloads are not portable (docs/multiplayer/determinism.md), so
    // check classification for NaN-producing inputs, and exact bits for
    // every other field. Include both full batches and partial task tails.
    const int32_t counts[] = {9, 321, 337};
    const uint32_t seeds[] = {0xff800000u, 0x7fc12345u, 0xbf800000u};
    for (uint32_t c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        const int32_t count = counts[c];
        tide_world one = {0}, many = {0};
        tide_world_init(&one, 1.0f / 60.0f);
        one.Field.count = count;
        tide_world_tick(&one);
        tide_text_use(&one.heap, NULL);
        // Put special values in vector lanes and at the scalar tail too.
        const int32_t indices[] = {3, 4, count - 1};
        for (int i = 0; i < 3; i++) {
            Item *item = tide_list_at_mut(one.Field.items, indices[i], sizeof(Item));
            memcpy(&item->seed, &seeds[i], sizeof item->seed);
        }
        one.Field.run = true;
        tide_world_copy(&many, &one);
        Item *was = malloc((size_t)count * sizeof *was), *next = malloc((size_t)count * sizeof *next);
        TIDE_REQUIRE(was && next);
        for (int32_t i = 0; i < count; i++) was[i] = item_at(&one, i);
        reference(next, was, count, one.Time.dt);
        const tide_jobs jobs = {NULL, 2u, work, NULL};
        tide_world_tick(&one);
        tide_world_tick_on(&many, &jobs);
        for (int32_t i = 0; i < count; i++) {
            Item a = item_at(&one, i), b = item_at(&many, i), want = next[i];
            if (want.root != want.root) {
                TIDE_CHECK(a.root != a.root && b.root != b.root);
                a.root = b.root = want.root = 0.0f;
            }
            TIDE_CHECK(memcmp(&a, &want, sizeof a) == 0 && memcmp(&b, &want, sizeof b) == 0);
        }
        free(next);
        free(was);
        tide_world_free(&many);
        tide_world_free(&one);
    }
}
