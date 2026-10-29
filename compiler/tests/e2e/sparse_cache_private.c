// This target compiles generated game.c only through this include, keeping
// implementation checks private without a testing API in generated headers.
#include "game.c"
#include "tide_test.h"

static void init_small(tide_world *w)
{
    tide_world_init(w, 1.0f / 64.0f);
    w->Rules.count = 129;
    tide_world_tick(w);
    tide_rows_cache_prepare(w);
    TIDE_REQUIRE(w->tide_rows_cache && w->tide_rows_cache->plans > 0);
}

static const tide_rows_chunk *find_first(const tide_world *w, const tide_rows_filter *filter, const uint32_t filters)
{
    return tide_rows_cache_find(w->tide_rows_cache, 0u, 0u, &w->arch0_Item,
                                &tide_columns_arch0_Item, 0u, filter, filters);
}

TIDE_TEST(sparse_cache_private_keys_reject_same_layout_filter_changes)
{
    tide_world w = {0};
    init_small(&w);
    tide_rows_plan *p = &w.tide_rows_cache->plan[0];
    TIDE_REQUIRE(p->filters == 2u && p->chunks == 1u);
    tide_rows_filter original[2];
    memcpy(original, p->filter, sizeof original);
    const tide_rows_chunk *entry = find_first(&w, original, 2u);
    TIDE_REQUIRE(entry && entry->ready && entry->pass < (entry->rows + 15u) / 16u);
    // A same-layout reload can reuse the ordinal and every presence page
    // while reversing with/without. The owned descriptor must still differ.
    tide_rows_filter flipped[2];
    memcpy(flipped, original, sizeof flipped);
    flipped[0].need ^= 1u;
    TIDE_REQUIRE(find_first(&w, flipped, 2u) == NULL);
    flipped[0] = original[0];
    flipped[1].need ^= 1u;
    TIDE_REQUIRE(find_first(&w, flipped, 2u) == NULL);
    flipped[1] = original[1];
    flipped[0].column = original[1].column;
    TIDE_REQUIRE(find_first(&w, flipped, 2u) == NULL);
    TIDE_REQUIRE(find_first(&w, original, 1u) == NULL);
    TIDE_REQUIRE(tide_rows_cache_find(w.tide_rows_cache, w.tide_rows_cache->plans, 0u, &w.arch0_Item,
                                    &tide_columns_arch0_Item, 0u, original, 2u) == NULL);
    TIDE_REQUIRE(tide_rows_cache_find(w.tide_rows_cache, 0u, 1u, &w.arch0_Item,
                                    &tide_columns_arch0_Item, 0u, original, 2u) == NULL);
    const uint32_t abi = w.tide_rows_cache->abi;
    w.tide_rows_cache->abi++;
    TIDE_REQUIRE(find_first(&w, original, 2u) == NULL);
    w.tide_rows_cache->abi = abi;

    // Re-preparing with a changed descriptor releases the old tuple and
    // records an owned copy, without retaining a module's static pointer.
    memcpy(flipped, original, sizeof flipped);
    flipped[1].need ^= 1u;
    tide_rows_plan_prepare(p, 0u, &w.arch0_Item, &tide_columns_arch0_Item, flipped, 2u);
    TIDE_REQUIRE(p->filter != flipped && p->filter[1].need == flipped[1].need);
    TIDE_REQUIRE(find_first(&w, original, 2u) == NULL && find_first(&w, flipped, 2u) == NULL);
    tide_rows_cache_prepare(&w);
    TIDE_REQUIRE(find_first(&w, original, 2u) != NULL);
    tide_world_free(&w);
}

TIDE_TEST(sparse_cache_private_lookup_rejects_replaced_pages_and_row_counts)
{
    tide_world w = {0};
    init_small(&w);
    tide_rows_filter filter[2];
    memcpy(filter, w.tide_rows_cache->plan[0].filter, sizeof filter);
    TIDE_REQUIRE(find_first(&w, filter, 2u) != NULL);
    const uint32_t rows = w.arch0_Item.count;
    w.arch0_Item.count--;
    TIDE_REQUIRE(find_first(&w, filter, 2u) == NULL);
    w.arch0_Item.count = rows;
    TIDE_REQUIRE(find_first(&w, filter, 2u) != NULL);
    tide_page *const held = w.arch0_Item.pages[filter[0].column];
    // The cache's retained reference forces this existing mutation path to
    // replace the page, even though its bytes need not change for this probe.
    (void)tide_table_column_mut(&w.arch0_Item, &tide_columns_arch0_Item, 0u, filter[0].column);
    TIDE_REQUIRE(w.arch0_Item.pages[filter[0].column] != held);
    TIDE_REQUIRE(find_first(&w, filter, 2u) == NULL);
    tide_rows_cache_prepare(&w);
    TIDE_REQUIRE(find_first(&w, filter, 2u) != NULL);
    tide_world_free(&w);
}

TIDE_TEST(sparse_cache_private_empty_plan_releases_retained_pages)
{
    tide_world w = {0};
    init_small(&w);
    tide_rows_plan *const p = &w.tide_rows_cache->plan[0];
    tide_rows_filter filter[2];
    memcpy(filter, p->filter, sizeof filter);
    tide_page *const held = w.arch0_Item.pages[filter[0].column];
    const uint32_t refs = held->refs;
    const tide_table empty = {0};
    tide_rows_plan_prepare(p, 0u, &empty, &tide_columns_arch0_Item, filter, 2u);
    TIDE_REQUIRE(p->chunks == 0u && !p->chunk && !p->pages);
    TIDE_REQUIRE(held->refs == refs - 1u);
    tide_world_free(&w);
}

TIDE_TEST(sparse_cache_private_saturated_references_keep_the_fallback_safe)
{
    tide_world source = {0}, cold = {0};
    init_small(&source);
    tide_rows_filter filter[2];
    memcpy(filter, source.tide_rows_cache->plan[0].filter, sizeof filter);
    tide_world_copy(&cold, &source);
    TIDE_REQUIRE(cold.tide_rows_cache == NULL);
    tide_page *const page = cold.arch0_Item.pages[filter[0].column];
    const uint32_t refs = page->refs;
    page->refs = UINT32_MAX;
    // There are no structural changes in this tick. Update changes only its
    // ordinary component lanes; cache preparation must not wrap this count.
    tide_world_tick(&cold);
    TIDE_REQUIRE(page->refs == UINT32_MAX);
    TIDE_REQUIRE(find_first(&cold, filter, 2u) == NULL);
    page->refs = refs;
    tide_world_tick(&source);
    TIDE_REQUIRE(tide_world_hash(&cold) == tide_world_hash(&source));
    tide_rows_cache_prepare(&cold);
    TIDE_REQUIRE(find_first(&cold, filter, 2u) != NULL);
    tide_world_free(&cold);
    tide_world_free(&source);
}

typedef struct private_arch {
    tide_table *table;
    const tide_columns *columns;
    uint32_t item, extra, gauge, needed, blocked;
} private_arch;

static private_arch private_arch_at(tide_world *w, const uint32_t archetype)
{
    if (archetype == 0u) {
        return (private_arch){&w->arch0_Item, &tide_columns_arch0_Item,
            tide_column_arch0_Item_Item, 0u, 0u,
            tide_column_arch0_Item_Needed - 1u, tide_column_arch0_Item_Blocked - 1u};
    }
    return (private_arch){&w->arch1_Item_Extra_Gauge, &tide_columns_arch1_Item_Extra_Gauge,
        tide_column_arch1_Item_Extra_Gauge_Item, tide_column_arch1_Item_Extra_Gauge_Extra,
        tide_column_arch1_Item_Extra_Gauge_Gauge,
        tide_column_arch1_Item_Extra_Gauge_Needed - 1u, tide_column_arch1_Item_Extra_Gauge_Blocked - 1u};
}

static tide_page *private_page(private_arch a, const uint32_t chunk, const uint32_t column)
{
    return a.table->pages[(size_t)chunk * a.columns->count + column];
}

static void private_init_large(tide_world *w)
{
    tide_world_init(w, 1.0f / 64.0f);
    w->Rules.count = 4163;
    w->Rules.extras = true;
    tide_world_tick(w);
    TIDE_REQUIRE(w->arch0_Item.count == 2081u && w->arch1_Item_Extra_Gauge.count == 2082u);
    for (uint32_t a = 0u; a < 2u; a++) {
        const private_arch at = private_arch_at(w, a);
        TIDE_REQUIRE(at.table->chunks == 2u && at.columns->shift == 11u);
        for (uint32_t c = 0u; c < at.table->chunks; c++) {
            const uint32_t rows = tide_table_rows(at.table, at.columns->shift, c);
            uint8_t *needed = tide_table_column_mut(at.table, at.columns, c, at.needed);
            uint8_t *blocked = tide_table_column_mut(at.table, at.columns, c, at.blocked);
            // Both full chunks and both short tails have a rare positive
            // selection, independently of spawn ordering or entity IDs.
            for (uint32_t i = 0u; i < rows; i++) {
                needed[i] = (uint8_t)(i % 97u == 0u);
                blocked[i] = (uint8_t)((i + 53u) % 191u == 0u);
            }
        }
    }
    tide_rows_cache_prepare(w);
}

static void private_same_worlds(tide_world *a, tide_world *b)
{
    // Compare validity metadata before world_hash can fill a wrongly dirtied
    // unchanged page. In particular, a late shared lane must not let a fast
    // probe invalidate earlier private lanes before entering the slow path.
    for (uint32_t arch = 0u; arch < 2u; arch++) {
        const private_arch aa = private_arch_at(a, arch), bb = private_arch_at(b, arch);
        TIDE_REQUIRE(aa.table->count == bb.table->count && aa.table->chunks == bb.table->chunks);
        for (uint32_t c = 0u; c < aa.table->chunks; c++) {
            const uint32_t rows = tide_table_rows(aa.table, aa.columns->shift, c);
            for (uint32_t column = 0u; column < aa.columns->count; column++) {
                const tide_page *pa = private_page(aa, c, column), *pb = private_page(bb, c, column);
                TIDE_REQUIRE(pa->refs == pb->refs && pa->hashed == pb->hashed && pa->was == pb->was);
                if (pa->hashed != UINT32_MAX) TIDE_REQUIRE(pa->hash == pb->hash);
                TIDE_REQUIRE(memcmp(tide_page_data(pa), tide_page_data(pb), (size_t)rows * aa.columns->sizes[column]) == 0);
            }
        }
    }
    TIDE_REQUIRE(tide_world_hash(a) == tide_world_hash(b));
    const uint32_t size = tide_world_pack(a, NULL, 0u);
    TIDE_REQUIRE(size == tide_world_pack(b, NULL, 0u));
    uint8_t *const left = tide_alloc(size), *const right = tide_alloc(size);
    TIDE_REQUIRE(tide_world_pack(a, left, size) == size && tide_world_pack(b, right, size) == size);
    TIDE_CHECK(memcmp(left, right, size) == 0);
    free(right);
    free(left);
}

typedef struct private_holds {
    tide_page *pages[64];
    uint32_t count;
} private_holds;

static void private_hold(private_holds *holds, tide_page *page)
{
    TIDE_REQUIRE(holds->count < sizeof holds->pages / sizeof holds->pages[0]);
    tide_page_retain(page);
    holds->pages[holds->count++] = page;
}

static void private_release(private_holds *holds)
{
    for (uint32_t i = 0u; i < holds->count; i++) tide_page_release(holds->pages[i], 1u);
    holds->count = 0u;
}

static void private_share_gauges(tide_world *w, private_holds *holds)
{
    const private_arch a = private_arch_at(w, 1u);
    for (uint32_t c = 0u; c < a.table->chunks; c++) {
        for (uint32_t k = 0u; k < 4u; k++) {
            tide_page *const page = private_page(a, c, a.gauge + k);
            while (page->refs < 3u) private_hold(holds, page);
        }
    }
}

static void private_mixed_refs(tide_world *w, private_holds *holds)
{
    static const uint32_t mixed[] = {1u, 2u, 3u, 1u, 2u, 1u, 1u, 3u};
    for (uint32_t arch = 0u; arch < 2u; arch++) {
        const private_arch a = private_arch_at(w, arch);
        for (uint32_t c = 0u; c < a.table->chunks; c++) {
            for (uint32_t k = 0u; k < 8u; k++) {
                // A late shared Item lane in arch0 and a late shared Extra
                // lane in arch1 check all-page proof before hash mutation.
                const uint32_t wanted = c ? mixed[k] : arch == 0u && k == 7u ? 2u : 1u;
                if (wanted == 1u) (void)tide_table_column_mut(a.table, a.columns, c, a.item + k);
                tide_page *const page = private_page(a, c, a.item + k);
                while (page->refs < wanted) private_hold(holds, page);
                TIDE_REQUIRE(page->refs == wanted);
            }
            for (uint32_t k = 0u; arch == 1u && k < 5u; k++) {
                const uint32_t wanted = c ? mixed[k] : k == 4u ? 3u : 1u;
                if (wanted == 1u) (void)tide_table_column_mut(a.table, a.columns, c, a.extra + k);
                tide_page *const page = private_page(a, c, a.extra + k);
                while (page->refs < wanted) private_hold(holds, page);
                TIDE_REQUIRE(page->refs == wanted);
            }
        }
    }
    private_share_gauges(w, holds);
}

static void private_run_task_pairs(tide_world *fast, tide_world *slow)
{
    uint64_t work = 0u;
    const uint32_t update_tasks = tide_tasks_Update(fast, &work);
    TIDE_REQUIRE(update_tasks == 4u && update_tasks == tide_tasks_Update(slow, &work));
    for (uint32_t task = 0u; task < update_tasks; task++) {
        tide_task_Update(fast, task, 1u);
        tide_task_Update_slow(slow, task, 1u);
        private_same_worlds(fast, slow);
    }
    const uint32_t extra_tasks = tide_tasks_UpdateExtra(fast, &work);
    TIDE_REQUIRE(extra_tasks == 2u && extra_tasks == tide_tasks_UpdateExtra(slow, &work));
    for (uint32_t task = 0u; task < extra_tasks; task++) {
        tide_task_UpdateExtra(fast, task, 1u);
        tide_task_UpdateExtra_slow(slow, task, 1u);
        private_same_worlds(fast, slow);
    }
}

TIDE_TEST(sparse_cache_private_task_fast_paths_match_slow_across_archetypes_and_tails)
{
    tide_world fast = {0}, slow = {0};
    private_holds fast_holds = {0}, slow_holds = {0};
    private_init_large(&fast);
    private_init_large(&slow);
    private_share_gauges(&fast, &fast_holds);
    private_share_gauges(&slow, &slow_holds);
    private_same_worlds(&fast, &slow);
    private_run_task_pairs(&fast, &slow);
    private_release(&fast_holds);
    private_release(&slow_holds);
    tide_world_free(&fast);
    tide_world_free(&slow);
}

TIDE_TEST(sparse_cache_private_task_fallback_keeps_mixed_reference_snapshots_and_hashes)
{
    tide_world fast = {0}, slow = {0}, fast_kept = {0}, slow_kept = {0};
    private_holds fast_holds = {0}, slow_holds = {0};
    private_init_large(&fast);
    private_init_large(&slow);
    tide_world_copy(&fast_kept, &fast);
    tide_world_copy(&slow_kept, &slow);
    private_mixed_refs(&fast, &fast_holds);
    private_mixed_refs(&slow, &slow_holds);
    private_same_worlds(&fast, &slow);
    const uint64_t fast_before = tide_world_hash(&fast_kept), slow_before = tide_world_hash(&slow_kept);
    private_run_task_pairs(&fast, &slow);
    TIDE_REQUIRE(tide_world_hash(&fast_kept) == fast_before && tide_world_hash(&slow_kept) == slow_before);
    private_same_worlds(&fast_kept, &slow_kept);
    private_release(&fast_holds);
    private_release(&slow_holds);
    tide_world_free(&fast_kept);
    tide_world_free(&slow_kept);
    tide_world_free(&fast);
    tide_world_free(&slow);
}

TIDE_TEST(sparse_cache_private_task_stale_keys_and_zero_matches_use_the_correct_offsets)
{
    tide_world fast = {0}, slow = {0};
    private_init_large(&fast);
    private_init_large(&slow);
    const private_arch fa = private_arch_at(&fast, 1u), sa = private_arch_at(&slow, 1u);
    // Retained presence references force replacement without changing bytes;
    // neither cache may be prepared again before these direct task calls.
    tide_page *const was = private_page(fa, 1u, fa.needed);
    (void)tide_table_column_mut(fa.table, fa.columns, 1u, fa.needed);
    (void)tide_table_column_mut(sa.table, sa.columns, 1u, sa.needed);
    TIDE_REQUIRE(private_page(fa, 1u, fa.needed) != was);
    private_same_worlds(&fast, &slow);
    private_run_task_pairs(&fast, &slow);

    for (uint32_t a = 0u; a < 2u; a++) {
        const private_arch ff = private_arch_at(&fast, a), ss = private_arch_at(&slow, a);
        // All chunks become zero-pass, including both tails and the second
        // archetype's nonzero task offset. No changed lane should be dirtied.
        for (uint32_t c = 0u; c < ff.table->chunks; c++) {
            const uint32_t rows = tide_table_rows(ff.table, ff.columns->shift, c);
            memset(tide_table_column_mut(ff.table, ff.columns, c, ff.needed), 0, rows);
            memset(tide_table_column_mut(ss.table, ss.columns, c, ss.needed), 0, rows);
        }
    }
    tide_rows_cache_prepare(&fast);
    tide_rows_cache_prepare(&slow);
    private_same_worlds(&fast, &slow);
    const uint64_t before = tide_world_hash(&fast);
    private_run_task_pairs(&fast, &slow);
    TIDE_REQUIRE(tide_world_hash(&fast) == before);
    tide_world_free(&fast);
    tide_world_free(&slow);
}
