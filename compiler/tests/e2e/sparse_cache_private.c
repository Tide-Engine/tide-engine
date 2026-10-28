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
