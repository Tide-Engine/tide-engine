#include <string.h>

#include "tide/mesh.h"
#include "tide_test.h"

static void one_word(const void *element, int32_t *words)
{
    memcpy(words, element, sizeof *words);
}

static void two_words(const void *element, int32_t *words)
{
    memcpy(words, element, 2u * sizeof *words);
}

static uint32_t capture(tide_draw_list *d, tide_list list, int32_t expected_count)
{
    int32_t count = -1;
    const uint32_t slot = tide_draw_list_data(d, list, sizeof(int32_t), 1, 0, one_word, &count);
    TIDE_CHECK(count == expected_count);
    TIDE_CHECK((slot != 0) == (expected_count != 0));
    return slot;
}

// A newly empty draw list takes the full conversion path. Compare not just
// values but public captured identity/shape/version and every padding word.
static void same_as_fresh(tide_draw_list *d, tide_list list, uint32_t size, uint32_t words,
                          uint32_t layout, tide_list_words_fn *write)
{
    tide_draw_list fresh = {0};
    int32_t n = -1, m = -1;
    const uint32_t a = tide_draw_list_data(d, list, size, words, layout, write, &n);
    const uint32_t b = tide_draw_list_data(&fresh, list, size, words, layout, write, &m);
    TIDE_REQUIRE(n == m && a && b);
    const tide_draw_texture *x = &d->textures[a - 1u], *y = &fresh.textures[b - 1u];
    TIDE_REQUIRE(x->id == y->id && x->version == y->version && x->format == y->format);
    TIDE_REQUIRE(x->width == y->width && x->height == y->height);
    TIDE_CHECK(memcmp(x->pixels, y->pixels, (size_t)x->width * (size_t)x->height * 16u) == 0);
    tide_draw_free(&fresh);
}

TIDE_TEST(draw_list_data_cache_tracks_in_place_writes_and_cow)
{
    tide_heap heap = {0}, snapshot = {0};
    tide_draw_list d = {0};
    tide_list list = {0};
    tide_text_use(NULL, &heap);
    tide_list_resize(&list, 2, sizeof(int32_t), TIDE_IN_LOCAL);
    tide_list_cache writer = {0};
    *(int32_t *)tide_list_cached_at_mut(&writer, list, 0, sizeof(int32_t)) = 17;
    *(int32_t *)tide_list_cached_at_mut(&writer, list, 1, sizeof(int32_t)) = 29;
    const uint32_t slot = capture(&d, list, 2);
    TIDE_REQUIRE(d.list_data != NULL);
    const uint64_t initial = d.textures[slot - 1u].version;
    const uint32_t moves = heap.moves;
    TIDE_CHECK(capture(&d, list, 2) == slot && heap.moves == moves + 1u);
    // The successful hash observation invalidates a previously retained
    // writable cache. Its next write must mark the page's hash stale again.
    *(int32_t *)tide_list_cached_at_mut(&writer, list, 1, sizeof(int32_t)) = -31;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    TIDE_CHECK(d.textures[slot - 1u].version != initial);
    TIDE_CHECK(((const int32_t *)(const void *)d.textures[slot - 1u].pixels)[1] == -31);
    tide_heap_copy(&snapshot, &heap);
    *(int32_t *)tide_list_at_mut(list, 0, sizeof(int32_t)) = 53;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    TIDE_CHECK(((const int32_t *)(const void *)d.textures[slot - 1u].pixels)[0] == 53);
    tide_text_use_verified(&snapshot);
    const tide_list verified = {tide_handle_of(TIDE_IN_VERIFIED, tide_handle_offset(list.at))};
    same_as_fresh(&d, verified, sizeof(int32_t), 1, 0, one_word);
    tide_text_use_verified(NULL);
    tide_draw_free(&d);
    tide_heap_free(&snapshot);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

TIDE_TEST(draw_list_data_cache_resizes_layouts_slot_changes_and_lifecycle)
{
    tide_heap heap = {0};
    tide_draw_list d = {0};
    tide_list list = {0};
    tide_text_use(NULL, &heap);
    // Allocate room for either tested numeric layout, so reinterpreting its
    // elements through trusted C stays within the backing allocation.
    tide_list_resize(&list, 4, 2u * sizeof(int32_t), TIDE_IN_LOCAL);
    for (int i = 0; i < 4; i++) *(int32_t *)tide_list_at_mut(list, i, 2u * sizeof(int32_t)) = i + 10;
    uint32_t slot = capture(&d, list, 4);
    same_as_fresh(&d, list, sizeof(int32_t), 1, 7, one_word);
    same_as_fresh(&d, list, 2u * sizeof(int32_t), 2, 0, two_words);
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    // Another writer can replace the captured slot's size, format or version
    // while the source page remains unchanged. A cached slot must be checked.
    const uint64_t id = d.textures[slot - 1u].id;
    uint8_t *pixels;
    (void)tide_draw_texture_slot_as(&d, TIDE_PIXELS_LIST, id, 123, 2, 1, TIDE_TEXELS_BYTES, &pixels);
    if (pixels) memset(pixels, 0x5a, 8);
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    d.textures[slot - 1u].changes->kind ^= 1u;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    tide_list_resize(&list, 0, sizeof(int32_t), TIDE_IN_LOCAL);
    TIDE_CHECK(capture(&d, list, 0) == 0);
    tide_list_resize(&list, 1, sizeof(int32_t), TIDE_IN_LOCAL);
    *(int32_t *)tide_list_at_mut(list, 0, sizeof(int32_t)) = 71;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    int32_t count;
    TIDE_CHECK(tide_draw_list_data(&d, list, sizeof(int32_t), 0, 0, NULL, &count) == 0 && count == 1);
    TIDE_CHECK(tide_draw_list_data(&d, list, 0, 1, 0, one_word, &count) == 0 && count == 0);
    tide_draw_reset(&d);
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    tide_draw_reset(&d);
    tide_draw_reset(&d); // No use in a frame: pruning can move all texture slots.
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    tide_draw_forget(&d);
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    slot = capture(&d, list, 1);
    const tide_handle old = list.at;
    tide_list_release(&list, TIDE_IN_LOCAL);
    tide_heap_flush(&heap);
    tide_list_resize(&list, 4, 2u * sizeof(int32_t), TIDE_IN_LOCAL);
    TIDE_CHECK(list.at == old);
    *(int32_t *)tide_list_at_mut(list, 0, sizeof(int32_t)) = -89;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    slot = capture(&d, list, 4);
    // Captured data owns its pixels independently of the source allocation.
    tide_heap_free(&heap);
    TIDE_CHECK(((const int32_t *)(const void *)d.textures[slot - 1u].pixels)[0] == -89);
    list = (tide_list){0};
    tide_list_resize(&list, 1, sizeof(int32_t), TIDE_IN_LOCAL);
    *(int32_t *)tide_list_at_mut(list, 0, sizeof(int32_t)) = 97;
    same_as_fresh(&d, list, sizeof(int32_t), 1, 0, one_word);
    tide_draw_free(&d);
    TIDE_CHECK(d.list_data == NULL);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

TIDE_TEST(draw_list_data_cache_falls_back_for_scratch_and_chunked_sources)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_draw_list d = {0};
    tide_text_use(NULL, &heap);
    const int32_t initial[] = {101, 103};
    tide_list scratch = tide_list_from(initial, 2, sizeof(int32_t));
    same_as_fresh(&d, scratch, sizeof(int32_t), 1, 0, one_word);
    *(int32_t *)tide_list_at_mut(scratch, 0, sizeof(int32_t)) = -107;
    same_as_fresh(&d, scratch, sizeof(int32_t), 1, 0, one_word);
    tide_scratch_reset(mark);
    scratch = tide_list_from(initial, 2, sizeof(int32_t));
    same_as_fresh(&d, scratch, sizeof(int32_t), 1, 0, one_word);
    tide_list chunked = {0};
    tide_list_resize(&chunked, 10000, sizeof(int32_t), TIDE_IN_LOCAL);
    *(int32_t *)tide_list_at_mut(chunked, 9999, sizeof(int32_t)) = 109;
    same_as_fresh(&d, chunked, sizeof(int32_t), 1, 0, one_word);
    *(int32_t *)tide_list_at_mut(chunked, 9999, sizeof(int32_t)) = -113;
    same_as_fresh(&d, chunked, sizeof(int32_t), 1, 0, one_word);
    tide_draw_free(&d);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_scratch_reset(mark);
}
