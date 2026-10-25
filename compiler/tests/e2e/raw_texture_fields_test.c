#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide/pack.h"
#include "tide_test.h"

#ifdef TIDE_RAW_TEXTURE_MIGRATION
#include "tide/migrate.h"
extern const tide_layout tide_game_layout;
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

_Static_assert(sizeof(((Art *)0)->fromGrid) == sizeof(tide_texture_ref), "Raw fields retain ordinary Texture references");

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static const tide_draw_run *runs[3];

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    uint32_t count = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHADED) continue;
        TIDE_REQUIRE(count < 3);
        runs[count++] = &draw.runs[c->shaded.run];
    }
    TIDE_REQUIRE(count == 3);
}

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame();
    frame(); // Attached local writes and their readonly captures have settled.
}

static void finish(void)
{
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}

static uint32_t texture(const uint32_t space, const int32_t width)
{
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space == space && draw.textures[i].width == width) return i + 1u;
    }
    return 0;
}

static uint32_t samples(const tide_draw_run *run, const uint32_t source, const tide_filter filter, const tide_wrap wrap)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < run->shader->texture_count; i++) {
        count += run->textures[i] == source && tide_sampling_filter(run->sampling, i) == filter
              && tide_sampling_wrap(run->sampling, i) == wrap;
    }
    return count;
}

static void check_dimensions(const int32_t width, const int32_t height)
{
    const Art *art = &local.Art;
    TIDE_CHECK(art->initialSize.x == width && art->initialSize.y == height);
    TIDE_CHECK(art->assetSize.x == width && art->assetSize.y == height);
    TIDE_CHECK(art->cellsSize.x == width && art->cellsSize.y == height);
    TIDE_CHECK(art->attachedInitialSize.x == width && art->attachedInitialSize.y == height);
    TIDE_CHECK(art->attachedSize.x == width && art->attachedSize.y == height);
    TIDE_CHECK(art->gridSize.x == 2 && art->gridSize.y == 1);
    TIDE_CHECK(art->noneSize.x == 0 && art->noneSize.y == 0);
}

static void check_size_uniform(const tide_draw_run *run, const int32_t width, const int32_t height)
{
    TIDE_REQUIRE(run->shader->uniform_ints == 1);
    int32_t size[2];
    memcpy(size, draw.uniforms + run->uniforms + run->shader->uniform_floats * 16u, sizeof size);
    TIDE_CHECK(size[0] == width && size[1] == height);
}

static void check_capture(void)
{
    const uint32_t asset = texture(TIDE_PIXELS_PACK, 8), grid = texture(TIDE_PIXELS_GRID, 2), cells = texture(TIDE_PIXELS_GRID, 8);
    TIDE_REQUIRE(asset && grid && cells);
    const tide_draw_texture *image = &draw.textures[asset - 1u], *numeric = &draw.textures[grid - 1u];
    const tide_draw_texture *packed_grid = &draw.textures[cells - 1u];
    TIDE_REQUIRE(image->pixels && numeric->pixels && packed_grid->pixels);
    TIDE_CHECK(image->height == 6 && packed_grid->height == 6 && numeric->height == 1);
    TIDE_CHECK(image->format == TIDE_TEXELS_BYTES && numeric->format == TIDE_TEXELS_BYTES && packed_grid->format == TIDE_TEXELS_BYTES);
    const size_t clear = (5u * 8u + 7u) * 4u;
    const uint8_t original[] = {210, 200, 100, 0};
    TIDE_CHECK(memcmp(image->pixels + clear, original, sizeof original) == 0);
    TIDE_CHECK(memcmp(packed_grid->pixels + clear, original, sizeof original) == 0);
    // These cells are Tide-owned: C pixel capture would premultiply their channels.
    const uint8_t values[] = {64, 128, 255, 0, 255, 128, 64, 128};
    TIDE_CHECK(memcmp(numeric->pixels, values, sizeof values) == 0);
    for (uint32_t i = 0; i < 3; i++) {
        const tide_shader *shader = runs[i]->shader;
        TIDE_REQUIRE(shader && shader->texture_count == 3 && shader->glsl_fragment);
        TIDE_CHECK(strstr(shader->glsl_fragment, "tf_SamplePair") != NULL);
        TIDE_CHECK(strstr(shader->glsl_fragment, "textureLod(pixels") != NULL);
        TIDE_CHECK(strstr(shader->glsl_fragment, "tide_straight(") == NULL);
#ifdef __wasm__
        TIDE_REQUIRE(shader->wgsl != NULL);
        TIDE_CHECK(strstr(shader->wgsl, "textureSampleLevel(pixels") != NULL);
        TIDE_CHECK(strstr(shader->wgsl, "tide_straight(") == NULL);
#endif
    }
    TIDE_CHECK(samples(runs[0], asset, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT) == 1);
    TIDE_CHECK(samples(runs[0], asset, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR) == 1);
    TIDE_CHECK(samples(runs[0], asset, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) == 1);
    TIDE_CHECK(samples(runs[1], grid, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT) == 1);
    TIDE_CHECK(samples(runs[1], grid, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR) == 1);
    TIDE_CHECK(samples(runs[1], grid, TIDE_FILTER_TRILINEAR, TIDE_WRAP_REPEAT) == 1);
    TIDE_CHECK(strstr(runs[1]->shader->glsl_fragment, "textureGrad(pixels") != NULL);
#ifdef __wasm__
    TIDE_CHECK(strstr(runs[1]->shader->wgsl, "textureSampleGrad(pixels") != NULL);
#endif
    TIDE_CHECK(samples(runs[2], cells, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP) == 1);
    TIDE_CHECK(samples(runs[2], cells, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR) == 1);
    TIDE_CHECK(samples(runs[2], asset, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) == 1);
    check_size_uniform(runs[1], 2, 1);
    check_size_uniform(runs[2], 8, 6);
}

TIDE_TEST(raw_texture_fields_capture_numbers_with_their_sampling)
{
    start();
    check_dimensions(8, 6);
    const Art *art = &local.Art;
    TIDE_CHECK(memcmp(&art->initial, &art->assigned, sizeof(tide_texture_ref)) == 0);
    TIDE_CHECK(memcmp(&art->assigned, &art->forwarded, sizeof(tide_texture_ref)) == 0);
    TIDE_CHECK(!art->forwarded.image.pixels && !art->forwarded.target.id);
    tide_grid grid;
    tide_grid_shape shape;
    TIDE_CHECK(tide_texture_grid(art->fromGrid, &grid, &shape) && grid.at == art->values.at && shape.cell == sizeof(tide_color));
    TIDE_CHECK(tide_texture_grid(art->fromCells, &grid, &shape) && tide_pack_holds(grid.at));
    check_capture();
    finish();
}

TIDE_TEST(raw_texture_fields_keep_references_across_pack_unload_and_reload)
{
    start();
    const tide_texture_ref asset = local.Art.forwarded, cells = local.Art.fromCells, grid = local.Art.fromGrid;
    tide_pack_unload();
    frame();
    check_dimensions(0, 0);
    TIDE_CHECK(memcmp(&local.Art.forwarded, &asset, sizeof asset) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromCells, &cells, sizeof cells) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromGrid, &grid, sizeof grid) == 0);
    TIDE_CHECK(samples(runs[0], 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT) == 1);
    TIDE_CHECK(samples(runs[2], 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP) == 1);
    check_size_uniform(runs[1], 2, 1);
    check_size_uniform(runs[2], 0, 0);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    check_dimensions(8, 6);
    TIDE_CHECK(memcmp(&local.Art.forwarded, &asset, sizeof asset) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromCells, &cells, sizeof cells) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromGrid, &grid, sizeof grid) == 0);
    check_capture();
    finish();
}

#ifdef TIDE_RAW_TEXTURE_MIGRATION
static const tide_layout_type *layout_type(const tide_layout *layout, const char *name)
{
    for (uint32_t i = 0; i < layout->type_count; i++) {
        if (strcmp(layout->types[i].name, name) == 0) return &layout->types[i];
    }
    return NULL;
}

static void check_raw_fields(const char *type_name, const char *const *names, const uint32_t count)
{
    const tide_layout_type *type = layout_type(&tide_game_layout, type_name);
    TIDE_REQUIRE(type);
    for (uint32_t i = 0; i < count; i++) {
        const tide_layout_field *field = NULL;
        for (uint32_t k = 0; k < type->field_count; k++) {
            if (strcmp(type->fields[k].name, names[i]) == 0) field = &type->fields[k];
        }
        TIDE_REQUIRE(field);
        TIDE_CHECK(strcmp(field->type, "[Raw] Texture") == 0);
        TIDE_CHECK(field->kind == TIDE_LAYOUT_PLAIN && field->size == sizeof(tide_texture_ref));
    }
}

TIDE_TEST(raw_texture_fields_generated_layout_records_local_roles)
{
    const char *const art[] = {"initial", "assigned", "forwarded", "fromGrid", "fromCells", "none"};
    const char *const panel[] = {"initial", "image"};
    check_raw_fields("Art", art, 6);
    check_raw_fields("Panel local", panel, 2);
    const tide_layout *layout = &tide_game_layout;
    bool art_is_local = false, panel_is_attached = false;
    for (uint32_t i = 0; i < layout->local.singleton_count; i++) {
        art_is_local |= strcmp(layout->types[layout->local.singletons[i].type].name, "Art") == 0;
    }
    for (uint32_t i = 0; i < layout->local.attached_count; i++) {
        panel_is_attached |= strcmp(layout->types[layout->local.attached[i].type].name, "Panel local") == 0;
    }
    TIDE_CHECK(art_is_local && panel_is_attached);
    for (uint32_t i = 0; i < layout->match.singleton_count; i++) {
        TIDE_CHECK(strcmp(layout->types[layout->match.singletons[i].type].name, "Art") != 0);
    }
    const tide_layout_type *match_panel = layout_type(layout, "Panel");
    TIDE_REQUIRE(match_panel);
    TIDE_CHECK(match_panel->field_count == 0); // Both Texture fields belong only to its attached local type.
}

// Clone every field array before changing only the generated Texture role text.
// The C representation and the generated destination default functions stay valid.
static tide_layout *plain_texture_layout(void)
{
    const tide_layout *raw = &tide_game_layout;
    size_t field_count = 0;
    for (uint32_t i = 0; i < raw->type_count; i++) field_count += raw->types[i].field_count;
    tide_layout *plain = malloc(sizeof *plain + raw->type_count * sizeof(tide_layout_type)
                               + field_count * sizeof(tide_layout_field));
    if (!plain) return NULL;
    *plain = *raw;
    tide_layout_type *types = (void *)(plain + 1);
    tide_layout_field *fields = (void *)(types + raw->type_count);
    plain->types = types;
    for (uint32_t i = 0; i < raw->type_count; i++) {
        types[i] = raw->types[i];
        types[i].fields = types[i].field_count ? fields : NULL;
        for (uint32_t k = 0; k < types[i].field_count; k++) {
            *fields = raw->types[i].fields[k];
            if (strcmp(fields->type, "[Raw] Texture") == 0) fields->type = "Texture";
            fields++;
        }
    }
    return plain;
}

static bool migrate_bytes(const tide_layout *from, const tide_layout *to, const bool local_world,
                          const void *bytes, const uint32_t size, void **carried, uint32_t *carried_size)
{
    // Exercise the web reload boundary too: only destination types have defaults.
    uint32_t layout_size = 0;
    void *layout_bytes = tide_layout_pack(from, &layout_size);
    if (!layout_bytes) return false;
    const tide_layout *source = tide_layout_unpack(layout_bytes, layout_size);
    if (!source) { free(layout_bytes); return false; }
    for (uint32_t i = 0; i < source->type_count; i++) TIDE_CHECK(source->types[i].defaults == NULL);
    tide_migration migration = {0};
    const bool ok = tide_migrate_world(source, local_world ? &source->local : &source->match, bytes, size,
                                       to, local_world ? &to->local : &to->match, carried, carried_size, &migration);
    TIDE_CHECK(migration.failed[0] == '\0');
    TIDE_CHECK(migration.entities_dropped == 0 && migration.tasks_dropped == 0);
    free(layout_bytes);
    return ok;
}

static bool migrate_local(const tide_layout *from, const tide_layout *to)
{
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    void *bytes = malloc(size), *carried = NULL;
    uint32_t carried_size = 0;
    if (!bytes) return false;
    const bool ok = tide_local_pack(&local, bytes, size) == size
        && migrate_bytes(from, to, true, bytes, size, &carried, &carried_size)
        && tide_local_unpack(&local, carried, carried_size);
    free(carried);
    free(bytes);
    return ok;
}

static void check_match_migration(const tide_layout *from, const tide_layout *to)
{
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size), *carried = NULL;
    uint32_t carried_size = 0;
    tide_world restored = {0};
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(migrate_bytes(from, to, false, bytes, size, &carried, &carried_size));
    TIDE_REQUIRE(tide_world_unpack(&restored, carried, carried_size));
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    TIDE_CHECK(restored.arch0_Panel.count == world.arch0_Panel.count);
    tide_world_free(&restored);
    free(carried);
    free(bytes);
}

static void check_local_grid(const tide_texture_ref texture)
{
    tide_grid grid;
    tide_grid_shape shape;
    TIDE_REQUIRE(tide_texture_grid(texture, &grid, &shape));
    TIDE_CHECK(grid.at == local.Art.values.at && !tide_pack_holds(grid.at));
    const tide_color expected[] = {{0.25f, 0.5f, 1, 0}, {1, 0.5f, 0.25f, 0.5f}};
    for (int32_t x = 0; x < 2; x++) {
        const tide_color *cell = tide_grid_read(&local.heap, grid, x, 0, 0, &shape);
        TIDE_REQUIRE(cell);
        TIDE_CHECK(memcmp(cell, &expected[x], sizeof *cell) == 0);
    }
}

TIDE_TEST(raw_texture_fields_compatible_reload_preserves_resource_and_local_grid_references)
{
    start();
    const uint64_t hash = tide_world_hash(&world);
    const Art before = local.Art;
    const tide_entity entity = TIDE_ENTITY_AT(&world, arch0_Panel, 0);
    const Panel_local panel = tide_get_Panel_local(&local, entity);
    TIDE_CHECK(tide_layout_fields_reset(&tide_game_layout, &tide_game_layout) == 0);
    TIDE_REQUIRE(migrate_local(&tide_game_layout, &tide_game_layout));
    TIDE_CHECK(memcmp(&local.Art, &before, sizeof before) == 0);
    const Panel_local restored = tide_get_Panel_local(&local, entity);
    TIDE_CHECK(memcmp(&restored, &panel, sizeof panel) == 0);
    check_local_grid(local.Art.fromGrid);
    frame();
    check_dimensions(8, 6);
    check_capture();
    TIDE_CHECK(tide_world_hash(&world) == hash);
    finish();
}

static void check_role_change(const bool to_raw)
{
    start();
    const tide_layout *raw = &tide_game_layout;
    tide_layout *plain = plain_texture_layout();
    TIDE_REQUIRE(plain);
    const tide_layout *from = to_raw ? plain : raw, *to = to_raw ? raw : plain;
    TIDE_CHECK(tide_layout_fields_reset(from, to) == 8);
    const uint64_t hash = tide_world_hash(&world);
    const tide_texture_ref asset_default = local.Art.initial, cells_default = local.Art.fromCells, zero = {0};
    TIDE_REQUIRE(memcmp(&asset_default, &zero, sizeof zero) != 0);
    TIDE_REQUIRE(memcmp(&cells_default, &zero, sizeof zero) != 0);
    const tide_texture_ref numeric = local.Art.fromGrid;
    local.Art.initial = numeric; // Reset must restore the nonzero declared asset, not merely clear it.
    local.Art.fromCells = asset_default; // This field's different nonzero default is a packed numeric grid.
    local.Art.assigned = numeric;
    local.Art.none = asset_default;
    const tide_entity entity = TIDE_ENTITY_AT(&world, arch0_Panel, 0);
    tide_set_Panel_local(&local, entity, (Panel_local){.initial = numeric, .image = asset_default});
    const tide_grid values = local.Art.values;
    const tide_list vertices = local.Art.vertices, indices = local.Art.indices;
    check_match_migration(from, to);
    TIDE_REQUIRE(migrate_local(from, to));
    TIDE_CHECK(memcmp(&local.Art.initial, &asset_default, sizeof asset_default) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromCells, &cells_default, sizeof cells_default) == 0);
    TIDE_CHECK(memcmp(&local.Art.assigned, &zero, sizeof zero) == 0);
    TIDE_CHECK(memcmp(&local.Art.forwarded, &zero, sizeof zero) == 0);
    TIDE_CHECK(memcmp(&local.Art.fromGrid, &zero, sizeof zero) == 0);
    TIDE_CHECK(memcmp(&local.Art.none, &zero, sizeof zero) == 0);
    const Panel_local panel = tide_get_Panel_local(&local, entity);
    TIDE_CHECK(memcmp(&panel.initial, &asset_default, sizeof asset_default) == 0);
    TIDE_CHECK(memcmp(&panel.image, &zero, sizeof zero) == 0);
    TIDE_CHECK(local.Art.ready && local.Art.values.at == values.at);
    TIDE_CHECK(memcmp(&local.Art.vertices, &vertices, sizeof vertices) == 0);
    TIDE_CHECK(memcmp(&local.Art.indices, &indices, sizeof indices) == 0);
    check_local_grid(numeric); // An unchanged local grid and its cells survive either Texture-role change.
    TIDE_CHECK(tide_world_hash(&world) == hash);
    free(plain);
    finish();
}

TIDE_TEST(raw_texture_fields_plain_to_raw_reload_uses_declared_destination_defaults)
{
    check_role_change(true);
}

TIDE_TEST(raw_texture_fields_raw_to_plain_reload_uses_declared_destination_defaults)
{
    check_role_change(false);
}
#endif
