#include <string.h>

#include "game.h"
#include "tide/page.h"
#include "tide_test.h"

static tide_world w;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The one read asked for and not begun, or NULL.
static tide_gpu_read_request *asked(void)
{
    tide_gpu_read_request *found = NULL;
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &tide_gpu_read_requests()[i];
        if (r->state != TIDE_GPU_READ_ASKED) continue;
        if (found) return NULL; // The reads here are one after another
        found = r;
    }
    return found;
}

// What a renderer does with a read: `width` by `height` pixels of `stride` words, each word its number from `first`.
static void answer(tide_gpu_read_request *r, const int32_t width, const int32_t height, const uint32_t stride, const uint32_t first)
{
    const uint32_t words = (uint32_t)(width * height) * stride;
    r->width = width, r->height = height;
    r->bytes = (words + TIDE_GPU_LIST_HEADER_WORDS) * 4u;
    r->data = tide_alloc_zeroed(1, r->bytes);
    r->data[0] = r->data[1] = (uint32_t)(width * height);
    for (uint32_t k = 0; k < words; k++) r->data[TIDE_GPU_LIST_HEADER_WORDS + k] = first + k;
    r->state = TIDE_GPU_READ_BEGUN;
    tide_gpu_read_finished(r, true);
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

// The grid's cell (x, y) is the first `stride` words of its pixel, of a read `width` wide that gave `held` words a
// pixel, numbered from `first`.
static bool cell(const tide_grid grid, const tide_grid_shape *shape, const int32_t x, const int32_t y, const int32_t width,
                 const uint32_t stride, const uint32_t held, const uint32_t first)
{
    uint32_t want[4] = {0};
    for (uint32_t k = 0; k < stride; k++) want[k] = first + ((uint32_t)(y * width + x)) * held + k;
    const void *got = tide_grid_read(&local.heap, grid, x, y, 0, shape);
    return got && shape->cell == stride * 4u && memcmp(got, want, shape->cell) == 0;
}

static bool whole(const tide_grid grid, const tide_grid_shape *shape, const int32_t width, const int32_t height,
                  const uint32_t stride, const uint32_t held, const uint32_t first)
{
    int32_t size[3];
    tide_grid_size(grid, size);
    bool good = size[0] == width && size[1] == height;
    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) good = good && cell(grid, shape, x, y, width, stride, held, first);
    }
    return good;
}

TIDE_TEST(target_reads_ask_for_their_image_and_kind_and_own_typed_grids)
{
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    // Each read in ReadAll's order: what it asks for, and the words a pixel a renderer answers with (a face's
    // and a layer's numbers come as all four channels, of which the grid keeps its own)
    const struct {
        uint32_t kind, image, stride, held;
        int32_t width, height;
    } reads[] = {
        {TIDE_TARGET_READ_COLOR, 0, 4, 4, 2, 2},  {TIDE_TARGET_READ_COLOR, 0, 4, 4, 3, 2},  {TIDE_TARGET_READ_FLOAT4, 0, 4, 4, 3, 2},
        {TIDE_TARGET_READ_FLOAT, 1, 1, 1, 3, 2},  {TIDE_TARGET_READ_FLOAT2, 2, 2, 2, 3, 2}, {TIDE_TARGET_READ_INT, 3, 1, 1, 3, 2},
        {TIDE_TARGET_READ_FLOAT, 0, 1, 1, 2, 1},  {TIDE_TARGET_READ_FLOAT2, 1, 2, 2, 2, 1}, {TIDE_TARGET_READ_FLOAT4, 2, 4, 4, 2, 1},
        {TIDE_TARGET_READ_DEPTH, 0, 1, 1, 3, 2},  {TIDE_TARGET_READ_DEPTH, 0, 1, 1, 2, 2},  {TIDE_TARGET_READ_COLOR, 0, 4, 4, 2, 2},
        {TIDE_TARGET_READ_DEPTH, 0, 1, 1, 2, 2},  {TIDE_TARGET_READ_COLOR, 0, 4, 4, 2, 2},  {TIDE_TARGET_READ_DEPTH, 0, 1, 1, 2, 2},
        {TIDE_TARGET_READ_FLOAT, 0, 1, 4, 2, 2},  {TIDE_TARGET_READ_FLOAT2, 0, 2, 4, 2, 2}, {TIDE_TARGET_READ_COLOR, 0, 4, 4, 2, 2},
    };
    const int count = (int)(sizeof reads / sizeof reads[0]);
    frame();
    // 1 for the plain target, 4, 3, none for depth alone, none for no target, and a face's one
    TIDE_CHECK(local.Reads.counts == 11430);
    for (int i = 0; i < count; i++) {
        tide_gpu_read_request *r = asked();
        TIDE_REQUIRE(r != NULL);
        TIDE_CHECK(r->target.id != 0 && r->kind == reads[i].kind && r->image == reads[i].image);
        TIDE_CHECK(tide_target_read_words(r->kind) == reads[i].stride);
        // Which target: a face and a layer are named by the read's own id
        if (i == 11) TIDE_CHECK(tide_render_target_face(r->target.id) == 4);
        if (i == 12) TIDE_CHECK(tide_render_target_face(r->target.id) == 1);
        if (i == 13) TIDE_CHECK(tide_render_target_layer(r->target.id) == 2);
        if (i == 14) TIDE_CHECK(tide_render_target_layer(r->target.id) == 0);
        if (i == 15) TIDE_CHECK(tide_render_target_face(r->target.id) == 3);
        if (i == 16) TIDE_CHECK(tide_render_target_layer(r->target.id) == 1);
        answer(r, reads[i].width, reads[i].height, reads[i].held, 1000u * (uint32_t)(i + 1));
        frame();
        TIDE_CHECK(local.Reads.stage == i + 1);
    }
    TIDE_CHECK(local.Reads.failed == 0 && asked() == NULL);
    const tide_grid grids[] = {local.Reads.color, local.Reads.hdr, local.Reads.raw, local.Reads.scalar, local.Reads.pair,
                               local.Reads.ids, local.Reads.mask, local.Reads.mask2, local.Reads.full, local.Reads.depth,
                               local.Reads.shadowDepth, local.Reads.face, local.Reads.faceDepth, local.Reads.layer,
                               local.Reads.layerDepth, local.Reads.height, local.Reads.flow, local.Reads.given};
    const tide_grid_shape *shapes[] = {&tide_shape_Grid2_Color, &tide_shape_Grid2_Color, &tide_shape_Grid2_float4,
                                       &tide_shape_Grid2_float, &tide_shape_Grid2_float2, &tide_shape_Grid2_int,
                                       &tide_shape_Grid2_float, &tide_shape_Grid2_float2, &tide_shape_Grid2_float4,
                                       &tide_shape_Grid2_float, &tide_shape_Grid2_float, &tide_shape_Grid2_Color,
                                       &tide_shape_Grid2_float, &tide_shape_Grid2_Color, &tide_shape_Grid2_float,
                                       &tide_shape_Grid2_float, &tide_shape_Grid2_float2, &tide_shape_Grid2_Color};
    for (int i = 0; i < count; i++) {
        TIDE_CHECK(whole(grids[i], shapes[i], reads[i].width, reads[i].height, reads[i].stride, reads[i].held,
                         1000u * (uint32_t)(i + 1)));
    }
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&w);
    tide_render_targets_free();
}

// A read that's begun, and finished at once as failed or not.
static bool begins(const tide_render_target target, const int32_t image, const uint32_t kind)
{
    const uint32_t ticket = tide_render_target_read_begin_as(target, image, kind);
    tide_gpu_read_result result;
    const bool failed = ticket && tide_gpu_list_read_poll(ticket, &result) && !result.ok;
    tide_gpu_list_read_forget(ticket);
    return ticket && !failed;
}

TIDE_TEST(target_reads_refuse_what_a_target_has_not_got)
{
    const tide_target_format formats[] = {TIDE_TARGET_RGBA8, TIDE_TARGET_R32F, TIDE_TARGET_RG8, TIDE_TARGET_R32I};
    const tide_render_target several = tide_render_target_make_scaled_of(4, 4, 1, formats, 4);
    const tide_render_target wide = tide_render_target_make_scaled_as(4, 4, 1, TIDE_TARGET_RGBA16F);
    const tide_render_target depth = tide_render_target_make_scaled_as(4, 4, 1, TIDE_TARGET_DEPTH);
    const tide_cubemap_ref cube = tide_cubemap_make(4);
    // Each image as its own kind, and as no other
    for (uint32_t kind = 0; kind < TIDE_TARGET_READ_DEPTH; kind++) {
        TIDE_CHECK(begins(several, 0, kind) == (kind == TIDE_TARGET_READ_COLOR));
        TIDE_CHECK(begins(several, 1, kind) == (kind == TIDE_TARGET_READ_FLOAT));
        TIDE_CHECK(begins(several, 2, kind) == (kind == TIDE_TARGET_READ_FLOAT2));
        TIDE_CHECK(begins(several, 3, kind) == (kind == TIDE_TARGET_READ_INT));
        TIDE_CHECK(begins(wide, 0, kind) == (kind == TIDE_TARGET_READ_COLOR || kind == TIDE_TARGET_READ_FLOAT4));
        TIDE_CHECK(!begins(depth, 0, kind)); // No color image
        TIDE_CHECK(!begins(several, 4, kind) && !begins(several, -1, kind) && !begins(several, INT32_MAX, kind));
        TIDE_CHECK(!begins(wide, 1, kind));
        TIDE_CHECK(!begins((tide_render_target){.id = TIDE_RENDER_WINDOW}, 0, kind));
        TIDE_CHECK(!begins((tide_render_target){0}, 0, kind));
        TIDE_CHECK(!begins(cube.target, 0, kind)); // A cube is read a face at a time
        TIDE_CHECK(begins(tide_cubemap_face(cube, 5), 0, kind) == (kind == TIDE_TARGET_READ_COLOR));
        TIDE_CHECK(!begins(tide_cubemap_face(cube, 6), 0, kind));
    }
    // A cube of numbers: its faces as floats, and as nothing else
    const tide_cubemap_ref heights = tide_cubemap_make_as(4, TIDE_TARGET_R16F);
    for (uint32_t kind = 0; kind < TIDE_TARGET_READ_DEPTH; kind++) {
        TIDE_CHECK(begins(tide_cubemap_face(heights, 2), 0, kind) == (kind == TIDE_TARGET_READ_FLOAT));
    }
    tide_cubemap_dispose(heights);
    TIDE_CHECK(begins(several, 0, TIDE_TARGET_READ_DEPTH) && begins(depth, 0, TIDE_TARGET_READ_DEPTH));
    TIDE_CHECK(begins(tide_cubemap_face(cube, 0), 0, TIDE_TARGET_READ_DEPTH));
    TIDE_CHECK(!begins(several, 1, TIDE_TARGET_READ_DEPTH)); // A target has one depth
    TIDE_CHECK(!begins((tide_render_target){.id = TIDE_RENDER_WINDOW}, 0, TIDE_TARGET_READ_DEPTH));
    TIDE_CHECK(!begins(several, 0, TIDE_TARGET_READ_KINDS) && !begins(several, 0, UINT32_MAX));
    tide_render_target_dispose(several);
    TIDE_CHECK(!begins(several, 0, TIDE_TARGET_READ_COLOR) && !begins(several, 0, TIDE_TARGET_READ_DEPTH));

    // A renderer's answer is held to what it says it wrote: fewer bytes than its pixels are, and it's failed
    const uint32_t ticket = tide_render_target_read_begin_as(wide, 0, TIDE_TARGET_READ_FLOAT4);
    tide_gpu_read_request *r = asked();
    TIDE_REQUIRE(ticket && r);
    answer(r, 4, 4, 1, 7); // A word a pixel, where its kind's are four
    tide_gpu_read_result result;
    TIDE_CHECK(tide_gpu_list_read_poll(ticket, &result) && !result.ok && result.words == NULL);
    r->width = 2, r->height = 2; // 16 words: what four pixels of four are
    TIDE_CHECK(tide_gpu_list_read_poll(ticket, &result) && result.ok && result.stride == 4 && result.count == 4);
    // ...and a grid is made only of a result of its own cells
    TIDE_CHECK(tide_render_target_read_grid(result, &tide_shape_Grid2_float).at == 0);
    TIDE_CHECK(tide_render_target_read_grid(result, &tide_shape_Grid2_float2).at == 0);
    result.count = 5;
    TIDE_CHECK(tide_render_target_read_grid(result, &tide_shape_Grid2_float4).at == 0);
    r->width = -4, r->height = -4;
    TIDE_CHECK(tide_gpu_list_read_poll(ticket, &result) && !result.ok);
    r->width = INT32_MAX, r->height = INT32_MAX;
    TIDE_CHECK(tide_gpu_list_read_poll(ticket, &result) && !result.ok);
    tide_gpu_list_read_forget(ticket);
    tide_cubemap_dispose(cube);
    tide_render_targets_free();
}
