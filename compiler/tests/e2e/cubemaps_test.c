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

// The list's texture a run reads as its texture `i`, or NULL for none.
static const tide_draw_texture *texture_of(const tide_draw_run *run, const uint32_t i)
{
    const uint32_t at = run->textures[i];
    return at && at <= draw.texture_count ? &draw.textures[at - 1u] : NULL;
}

// Which of a run's textures is the cube `id` that's drawn into, or -1.
static int reads_cube(const tide_draw_run *run, const uint64_t id)
{
    for (uint32_t i = 0; i < run->shader->texture_count; i++) {
        const tide_draw_texture *t = texture_of(run, i);
        if (t && t->space == TIDE_PIXELS_TARGET_CUBE && t->id == id) return (int)i;
    }
    return -1;
}

// ...and which is a cube of the pack's.
static int reads_pack_cube(const tide_draw_run *run)
{
    for (uint32_t i = 0; i < run->shader->texture_count; i++) {
        const tide_draw_texture *t = texture_of(run, i);
        if (t && t->space == TIDE_PIXELS_PACK_CUBE) return (int)i;
    }
    return -1;
}

// The destination the command draws into: 0 for the window, or the target's id.
static uint64_t destination_of(const tide_draw_command *command)
{
    uint32_t selected = 0;
    for (const tide_draw_command *c = draw.commands; c <= command; c++) {
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
    }
    return selected ? draw.targets[selected - 1u].id : 0;
}

static bool is_destination(const uint64_t id)
{
    for (uint32_t i = 0; i < draw.target_count; i++) {
        if (draw.targets[i].id == id) return true;
    }
    return false;
}

static tide_entity prop(void)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Prop(&world, e)) return e;
    }
    return (tide_entity){0};
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
}

static bool pixel_is(const uint8_t *pixel, const uint8_t r, const uint8_t g, const uint8_t b)
{
    return pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == 255;
}

TIDE_TEST(cubemaps_are_assets_of_six_files_and_cubes_a_view_draws_into)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 1280.0f, gui.height = 720.0f;
    frame();

    // An asset's cube is its six files, in the faces' order whatever order their names sort in, each
    // a square of the same size, rows from the top: every face here is its own color, its top left white
    const tide_cubemap_ref sky = tide_pack_cubemap_of(tide_pack_find("sky", TIDE_PACK_CUBEMAP));
    TIDE_REQUIRE(sky.named != 0 && sky.target.id == 0);
    int32_t size = 0;
    uint64_t version = 0;
    const uint8_t *const *faces = NULL;
    TIDE_REQUIRE(tide_pack_cubemap_find(sky.named, &size, &version, &faces) && size == 4 && version != 0);
    static const uint8_t colors[6][3] = {{255, 0, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {0, 0, 255}, {255, 255, 0}};
    for (int face = 0; face < 6; face++) {
        TIDE_CHECK(pixel_is(faces[face], 255, 255, 255));
        TIDE_CHECK(pixel_is(faces[face] + 4 * (size * size - 1), colors[face][0], colors[face][1], colors[face][2]));
    }
    // Two declarations of the same six files are two names of the same images, each in the pack
    // once: one of them as a Texture is that face's pixels too
    const tide_cubemap_ref again = tide_pack_cubemap_of(tide_pack_find("skyAgain", TIDE_PACK_CUBEMAP));
    int32_t size_again = 0;
    uint64_t version_again = 0;
    const uint8_t *const *faces_again = NULL;
    TIDE_REQUIRE(again.named != 0 && again.named != sky.named);
    TIDE_REQUIRE(tide_pack_cubemap_find(again.named, &size_again, &version_again, &faces_again));
    TIDE_CHECK(version_again == version && faces_again[3] == faces[3]);
    tide_texture right;
    const tide_texture_ref right_ref = tide_pack_texture_of(tide_pack_find("right", TIDE_PACK_TEXTURE));
    TIDE_REQUIRE(tide_pack_texture_find(right_ref.image.version & TIDE_TEXTURE_NAME, &right));
    TIDE_CHECK(right.pixels == faces[0] && right.width == 4);
    // A name the pack has no cube under, and no name at all, are no cube
    TIDE_CHECK(!tide_pack_cubemap_find(sky.named + 1u, &size_again, &version_again, &faces_again));
    TIDE_CHECK(!tide_pack_cubemap_find(0, &size_again, &version_again, &faces_again));
    TIDE_CHECK(tide_pack_cubemap_of(NULL).named == 0 && tide_pack_cubemap_of(tide_pack_find("right", TIDE_PACK_TEXTURE)).named == 0);

    // A cube a view makes: its faces' side in pixels, whatever the display, and what they hold
    const Room *room = &local.Room;
    const uint64_t cube = room->reflection.target.id;
    TIDE_REQUIRE(tide_render_target_cubic(cube) && tide_render_target_face(cube) == -1 && room->reflection.named == 0);
    TIDE_CHECK(room->reflection.target.size.width == 64.0f && room->reflection.target.size.height == 64.0f);
    TIDE_CHECK(room->reflection.target.size.scale == 1.0f && tide_render_target_live(cube));
    TIDE_CHECK(tide_render_target_format(cube) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_cubic(room->bright.target.id) && tide_render_target_format(room->bright.target.id) == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(room->bright.target.size.width == 16.0f && room->bright.target.id != cube);
    // An asset's is a value too, kept in a field; one never set is no cube
    TIDE_CHECK(room->held.named == sky.named && room->held.target.id == 0);
    TIDE_CHECK(room->none.named == 0 && room->none.target.id == 0);
    // ...through a function, in a struct, and in a match component's local field
    TIDE_CHECK(room->probe.cube.target.id == cube);
    const tide_entity entity = prop();
    TIDE_REQUIRE(entity.generation != 0);
    TIDE_CHECK(tide_get_Prop_local(&local, entity).around.target.id == cube);

    // A face is a RenderTarget: the cube's state, its size, and which face its id says. Each is a
    // destination of its own, so each has its own camera and clear
    TIDE_CHECK(tide_render_target_face(room->probe.front.id) == 4 && tide_render_target_cube_of(room->probe.front.id) == cube);
    TIDE_CHECK(tide_render_target_state_of(room->probe.front.id) == tide_render_target_state_of(cube));
    TIDE_CHECK(room->probe.front.size.width == 64.0f && tide_render_target_live(room->probe.front.id));
    for (int face = 0; face < 6; face++) {
        const tide_render_target of_face = tide_cubemap_face(room->reflection, face);
        TIDE_CHECK(tide_render_target_face(of_face.id) == face && is_destination(of_face.id));
        // A face has no color or depth of two dimensions to hand out: the cube is what's read
        TIDE_CHECK(tide_render_target_color(of_face).target.id == TIDE_RENDER_INVALID);
        TIDE_CHECK(tide_render_target_depth(of_face).target.id == TIDE_RENDER_INVALID);
    }
    TIDE_CHECK(!is_destination(cube)); // The cube itself is no destination
    TIDE_CHECK(is_destination(tide_cubemap_face(room->bright, 2).id));
    // A face there's none of, and one of a cube that isn't drawn into, are no destination
    TIDE_CHECK(tide_cubemap_face(room->reflection, 6).id == TIDE_RENDER_INVALID && tide_cubemap_face(room->reflection, -1).id == TIDE_RENDER_INVALID);
    TIDE_CHECK(tide_cubemap_face(sky, 0).id == TIDE_RENDER_INVALID && tide_cubemap_face(room->none, 0).id == TIDE_RENDER_INVALID);

    // A frag reads the asset by its name: a texture of the list's that points at the pack's six
    // faces, read as a cube, blended where the Sample says nothing
    const tide_draw_run *sky_run = run_through("Sky on Corner");
    TIDE_REQUIRE(sky_run && sky_run->shader->texture_count == 1 && sky_run->shader->texture_reads[0] == TIDE_SHADER_READ_CUBE);
    const tide_draw_texture *packed = texture_of(sky_run, 0);
    TIDE_REQUIRE(packed && packed->space == TIDE_PIXELS_PACK_CUBE && packed->id == version && packed->width == 4);
    TIDE_CHECK((const uint8_t *const *)(uintptr_t)packed->pixels == faces && packed->capacity == 0);
    TIDE_CHECK(tide_sampling_filter(sky_run->sampling, 0) == TIDE_FILTER_BILINEAR);
    TIDE_CHECK(strstr(sky_run->shader->glsl_fragment, "uniform highp samplerCube pixels0;\n") != NULL);
    TIDE_CHECK(strstr(sky_run->shader->glsl_fragment, "textureLod(pixels0, t_corner.m_normal, 0.0)") != NULL);
#ifdef __wasm__
    TIDE_CHECK(strstr(sky_run->shader->wgsl, "var pixels0: texture_cube<f32>;") != NULL);
#endif
    // It draws into a face of the cube, and into the window
    TIDE_CHECK(destination_of(drawn_through("Sky on Corner", 0)) == room->probe.front.id);
    TIDE_CHECK(destination_of(drawn_through("Sky on Corner", 1)) == 0);

    // Three cubes in one frag, each a binding with the filter its Sample says: the asset between
    // its smaller images, with the direction's derivatives asked as the frag starts, the cube
    // that's drawn into at its nearest pixel, in a branch, and the float one blended
    const tide_draw_run *mirror = run_through("Mirror on Corner");
    TIDE_REQUIRE(mirror && mirror->shader->texture_count == 3);
    for (int i = 0; i < 3; i++) TIDE_CHECK(mirror->shader->texture_reads[i] == TIDE_SHADER_READ_CUBE);
    const int from_pack = reads_pack_cube(mirror), from_cube = reads_cube(mirror, cube), from_bright = reads_cube(mirror, room->bright.target.id);
    TIDE_REQUIRE(from_pack >= 0 && from_cube >= 0 && from_bright >= 0);
    TIDE_CHECK(tide_sampling_filter(mirror->sampling, (uint32_t)from_pack) == TIDE_FILTER_TRILINEAR);
    TIDE_CHECK(tide_sampling_filter(mirror->sampling, (uint32_t)from_cube) == TIDE_FILTER_POINT);
    TIDE_CHECK(tide_sampling_filter(mirror->sampling, (uint32_t)from_bright) == TIDE_FILTER_BILINEAR);
    TIDE_CHECK(strstr(mirror->shader->glsl_fragment, "vec3 tide_dx0 = dFdx(tide_uv0);") != NULL);
    TIDE_CHECK(strstr(mirror->shader->glsl_fragment, "textureGrad(pixels") != NULL);
    // A cube that's drawn into is a texture of the list's with no pixels: its id, size and version
    const tide_draw_texture *drawn = texture_of(mirror, (uint32_t)from_cube);
    TIDE_CHECK(drawn->pixels == NULL && drawn->target.width == 64.0f && drawn->version == tide_render_target_version(cube));
    // The draw into a face of the cube it reads is recorded: the renderer passes it by
    TIDE_CHECK(destination_of(drawn_through("Mirror on Corner", 1)) == tide_cubemap_face(room->reflection, 1).id);

    // A match component's local field and a kept asset, and a vert's read
    const tide_draw_run *around = run_through("Around on Corner");
    TIDE_REQUIRE(around && around->shader->texture_count == 2);
    TIDE_CHECK(reads_cube(around, cube) >= 0 && reads_pack_cube(around) >= 0);
    TIDE_CHECK(tide_sampling_filter(around->sampling, (uint32_t)reads_pack_cube(around)) == TIDE_FILTER_POINT);
    const tide_draw_run *lift = run_through("Lift on Corner");
    TIDE_REQUIRE(lift && lift->shader->texture_count == 1 && lift->shader->texture_reads[0] == TIDE_SHADER_READ_CUBE);
    TIDE_CHECK(reads_cube(lift, cube) == 0 && strstr(lift->shader->glsl_vertex, "samplerCube pixels0") != NULL);

    // Two names of the same six files are one texture of the list's, told by what's in them
    const tide_draw_run *twice = run_through("Twice on Corner");
    TIDE_REQUIRE(twice && twice->shader->texture_count == 3);
    TIDE_CHECK(twice->shader->texture_reads[0] == TIDE_SHADER_READ_CUBE && twice->shader->texture_reads[1] == TIDE_SHADER_READ_CUBE
               && twice->shader->texture_reads[2] == TIDE_SHADER_READ_COLOR);
    TIDE_CHECK(twice->textures[0] == twice->textures[1] && twice->textures[0] != twice->textures[2]);
    TIDE_CHECK(texture_of(twice, 2)->space == TIDE_PIXELS_PACK && texture_of(twice, 2)->pixels == faces[0]);

    // No cube reads as nothing: the source of one never set is no texture
    TIDE_CHECK(tide_draw_cube_source(&draw, room->none) == 0);
    TIDE_CHECK(tide_draw_cube_source(&draw, (tide_cubemap_ref){.named = sky.named + 1u}) == 0);

    // Another build of the game takes them over: where the state can't be read as an address, a
    // cube is made again from what its id and its size say, and a face is that face of the new one
    const uint32_t bytes_size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(bytes_size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, bytes_size) == bytes_size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, bytes_size));
    free(bytes);
    tide_local_rebind_resources(&restored, false);
    const Room *next = &restored.Room;
    const uint64_t next_cube = next->reflection.target.id;
    TIDE_REQUIRE(tide_render_target_cubic(next_cube) && next_cube != cube && tide_render_target_face(next_cube) == -1);
    TIDE_CHECK(next->reflection.target.size.width == 64.0f && next->reflection.target.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_format(next->bright.target.id) == TIDE_TARGET_RGBA16F && tide_render_target_cubic(next->bright.target.id));
    TIDE_CHECK(next->probe.cube.target.id == next_cube);
    TIDE_CHECK(tide_render_target_face(next->probe.front.id) == 4 && tide_render_target_cube_of(next->probe.front.id) == next_cube);
    TIDE_CHECK(tide_get_Prop_local(&restored, entity).around.target.id == next_cube);
    TIDE_CHECK(next->held.named == sky.named && next->held.target.id == 0 && next->none.named == 0);
    tide_render_targets_recreated();
    // ...and where it can, the same cube goes on
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Room.reflection.target.id == cube && local.Room.probe.front.id == room->probe.front.id);

    // Resize is another size for all six faces, and another version: its images are made again
    const uint64_t before = tide_render_target_version(cube);
    frame();
    TIDE_CHECK(local.Room.resized && tide_render_target_version(cube) == before + 1u);
    TIDE_CHECK(tide_render_target_resolve(local.Room.reflection.target).size.width == 128.0f);
    TIDE_CHECK(tide_cubemap_face(local.Room.reflection, 3).size.width == 128.0f);
    // A face is no target to resize or dispose by itself: the cube stays as it is
    tide_render_target_resize(tide_cubemap_face(local.Room.reflection, 0), 8.0f, 8.0f);
    tide_render_target_dispose(tide_cubemap_face(local.Room.reflection, 0));
    TIDE_CHECK(tide_render_target_live(cube) && tide_render_target_resolve(local.Room.reflection.target).size.width == 128.0f);
    // Dispose lets every copy's cube go, its faces with it
    frame();
    TIDE_CHECK(local.Room.disposed && !tide_render_target_live(local.Room.bright.target.id));
    TIDE_CHECK(!tide_render_target_live(tide_cubemap_face(local.Room.bright, 2).id) && tide_render_target_live(cube));
    TIDE_CHECK(tide_draw_cube_source(&draw, local.Room.bright) != 0); // Named all the same: the renderer passes its draws by

    // The pack gone, an asset's cube is no texture
    tide_pack_unload();
    TIDE_CHECK(tide_draw_cube_source(&draw, sky) == 0);

    tide_local_free(&restored);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
