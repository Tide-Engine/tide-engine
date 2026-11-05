#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The `nth` command that draws instances through the shader named `name`.
static const tide_draw_command *drawn_through(const char *name, uint32_t nth)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &draw.runs[c->shaded.run];
        if (r->shader && strcmp(r->shader->name, name) == 0 && nth-- == 0) return c;
    }
    return NULL;
}

static const tide_draw_run *run_through(const char *name)
{
    const tide_draw_command *c = drawn_through(name, 0);
    return c ? &draw.runs[c->shaded.run] : NULL;
}

static const tide_draw_texture *texture_of(const tide_draw_run *run, const uint32_t i)
{
    const uint32_t at = run->textures[i];
    return at && at <= draw.texture_count ? &draw.textures[at - 1u] : NULL;
}

// The destination a command draws into: the window (0), or a target's id.
static uint64_t destination_of(const tide_draw_command *command)
{
    uint32_t selected = 0;
    for (const tide_draw_command *c = draw.commands; c <= command; c++) {
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
    }
    return selected ? draw.targets[selected - 1u].id : 0;
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

TIDE_TEST(texture_arrays_are_assets_of_files_and_arrays_a_view_draws_into_a_layer_at_a_time)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 1280.0f, gui.height = 720.0f;
    frame();
    const Art *art = &local.Art;

    // An asset's array is its glob's images in their paths' order, or its list's in the list's: each layer's pixels
    // where the pack has them, all of one size
    const tide_texture_array_ref sky = tide_pack_texture_array_of(tide_pack_find("sky", TIDE_PACK_TEXTURE_ARRAY));
    TIDE_REQUIRE(sky.named != 0 && sky.target.id == 0);
    int32_t width, height;
    uint32_t layers;
    uint64_t version;
    const uint8_t *const *pixels;
    TIDE_REQUIRE(tide_pack_texture_array_find(sky.named, &width, &height, &layers, &version, &pixels));
    TIDE_CHECK(width == 4 && height == 4 && layers == 6 && version != 0);
    const tide_texture_array_ref picked = tide_pack_texture_array_of(tide_pack_find("picked", TIDE_PACK_TEXTURE_ARRAY));
    const uint8_t *const *picked_pixels;
    uint64_t picked_version;
    TIDE_REQUIRE(tide_pack_texture_array_find(picked.named, &width, &height, &layers, &picked_version, &picked_pixels));
    TIDE_CHECK(layers == 3 && picked_version != version);
    // ...the same file twice is one image in the pack, which both layers name; sky_pz is the glob's sixth
    TIDE_CHECK(picked_pixels[0] == picked_pixels[2] && picked_pixels[0] == pixels[5] && picked_pixels[1] == pixels[3]);

    // Counts, on the CPU: a drawn one's, its depth's, an asset's, and none
    TIDE_CHECK(art->count == 3 && art->depthCount == 4 && art->assetCount == 6 && art->pickedCount == 3 && art->none == 0);
    TIDE_CHECK(tide_texture_array_size(art->kept).x == 4 && tide_texture_array_size(art->kept).y == 4);
    TIDE_CHECK(art->drawn.layers == 3 && tide_render_target_arrayed(art->drawn.target.id));
    TIDE_CHECK(tide_render_target_colors(art->shadows.target.id) == 0); // Of depth alone
    TIDE_CHECK(art->depths.target.id == art->shadows.target.id);

    // Its layers are destinations, each its own: drawn into, and a target's draws there
    const tide_render_target first = tide_texture_array_layer(art->drawn, 1);
    TIDE_CHECK(tide_render_target_layer(first.id) == 1 && tide_render_target_array_of(first.id) == art->drawn.target.id);
    TIDE_CHECK(destination_of(drawn_through("Packed on Vertex", 0)) == tide_texture_array_layer(art->drawn, 2).id);
    TIDE_CHECK(destination_of(drawn_through("Packed on Vertex", 1)) == 0);
    TIDE_CHECK(tide_texture_array_layer(art->drawn, 3).id == TIDE_RENDER_INVALID); // Past its layers
    TIDE_CHECK(tide_texture_array_layer(sky, 0).id == TIDE_RENDER_INVALID);        // ...and an asset's is no destination
    // A layer has no color or depth of two dimensions to hand out: the array is what's read
    TIDE_CHECK(tide_render_target_color(first).target.id == TIDE_RENDER_INVALID);

    // A frag reads them as textures of the list's: the drawn array, by its id, with its layers; the asset's; the
    // depth array, one texture read as a number and compared through two samplers
    const tide_draw_run *show = run_through("Show on Vertex");
    TIDE_REQUIRE(show && show->shader->texture_count == 3);
    bool drawn = false, asset = false, depth = false;
    for (uint32_t i = 0; i < 3; i++) {
        const tide_draw_texture *t = texture_of(show, i);
        TIDE_REQUIRE(t != NULL);
        if (t->space == TIDE_PIXELS_TARGET_ARRAY) {
            drawn = t->id == art->drawn.target.id && t->layers == 3 && show->shader->texture_reads[i] == TIDE_SHADER_READ_ARRAY;
        } else if (t->space == TIDE_PIXELS_PACK_ARRAY) {
            asset = t->layers == 6 && t->width == 4 && show->shader->texture_reads[i] == TIDE_SHADER_READ_ARRAY;
        } else if (t->space == TIDE_PIXELS_TARGET_DEPTH) {
            depth = t->id == art->shadows.target.id && show->shader->texture_reads[i] == TIDE_SHADER_READ_DEPTH_ARRAY
                 && tide_shader_texture_samplers(show->shader, i) != 0;
        }
    }
    TIDE_CHECK(drawn && asset && depth);
    // Its samplers: a color's (both arrays read alike, blended and clamped), a depth's and a compared one's
    TIDE_REQUIRE(show->shader->sampler_count == 3);
    TIDE_CHECK(show->shader->sampler_reads[0] == TIDE_SHADER_READ_COMPARE || show->shader->sampler_reads[1] == TIDE_SHADER_READ_COMPARE
               || show->shader->sampler_reads[2] == TIDE_SHADER_READ_COMPARE);
    const tide_draw_run *packed = run_through("Packed on Vertex");
    TIDE_REQUIRE(packed && packed->shader->texture_count == 2 && packed->shader->sampler_count == 0); // Each its own
    TIDE_CHECK(tide_sampling_filter(packed->sampling, 1) == TIDE_FILTER_POINT && tide_sampling_wrap(packed->sampling, 1) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(tide_sampling_filter(packed->sampling, 0) == TIDE_FILTER_BILINEAR);
#ifdef __wasm__ // A shader's text is its WGSL, which a build keeps on the web
    TIDE_CHECK(strstr(show->shader->wgsl, "texture_2d_array<f32>") != NULL);
    TIDE_CHECK(strstr(show->shader->wgsl, "texture_depth_2d_array") != NULL);
    TIDE_CHECK(strstr(show->shader->wgsl, "textureNumLayers(") != NULL);
#endif

    // A reload in other memory makes a drawn array again, with its layers, and keeps an asset's name
    const uint64_t before = art->drawn.target.id;
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local next = {0};
    TIDE_REQUIRE(tide_local_unpack(&next, bytes, size));
    free(bytes);
    tide_local_rebind_resources(&next, false);
    TIDE_CHECK(next.Art.drawn.target.id != before && tide_render_target_arrayed(next.Art.drawn.target.id));
    TIDE_CHECK(next.Art.drawn.layers == 3 && tide_texture_array_count(next.Art.drawn) == 3);
    TIDE_CHECK(next.Art.kept.named == sky.named && next.Art.kept.target.id == 0);
    TIDE_CHECK(tide_render_target_arrayed(next.Art.depths.target.id) && tide_depth_array_count(next.Art.depths) == 4);
    tide_render_targets_recreated();
    tide_local_free(&next);

    // Resize is another size for every layer, and another version
    const uint64_t version_before = tide_render_target_version(art->drawn.target.id);
    frame();
    TIDE_CHECK(tide_render_target_version(art->drawn.target.id) == version_before + 1u);
    TIDE_CHECK(tide_render_target_resolve(art->drawn.target).size.width == 64.0f);
    TIDE_CHECK(tide_texture_array_layer(art->drawn, 0).size.width == 64.0f);
    // A layer is no target to resize or dispose by itself
    tide_render_target_resize(tide_texture_array_layer(art->drawn, 0), 8.0f, 8.0f);
    tide_render_target_dispose(tide_texture_array_layer(art->drawn, 0));
    TIDE_CHECK(tide_render_target_live(art->drawn.target.id) && tide_render_target_resolve(art->drawn.target).size.width == 64.0f);
    // Dispose lets every copy's array go, its layers with it
    frame();
    TIDE_CHECK(!tide_render_target_live(art->drawn.target.id) && art->count == 0);
    TIDE_CHECK(!tide_render_target_live(tide_texture_array_layer(art->drawn, 1).id));

    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
