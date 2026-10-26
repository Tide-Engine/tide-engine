#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// Tide's Wrap is in this game's header, as its code names it, with tide_wrap's values
_Static_assert(Wrap_Clamp == (int)TIDE_WRAP_CLAMP && Wrap_Repeat == (int)TIDE_WRAP_REPEAT && Wrap_Mirror == (int)TIDE_WRAP_MIRROR,
               "Wrap has tide_wrap's values");

// A command that draws, and the destination the commands before it left it to draw into.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target; // 0 for the window, or 1 + its place in the list's targets
} drawn;

static drawn draws[64];
static int draw_count;

static void frame(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_test_flatten(&draw); // Its plain shapes a command each, as the checks read them
    draw_count = 0;
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target;
        const bool draws_something = c->kind == TIDE_DRAW_MESH || c->kind == TIDE_DRAW_MESH_3D || c->kind == TIDE_DRAW_SHADED
                                  || c->kind == TIDE_DRAW_RECT;
        if (draws_something && draw_count < 64) draws[draw_count++] = (drawn){c, target};
    }
}

// Whether what was drawn `i`th is `triangles` triangles of a 2D mesh, sampled that way.
static bool mesh(const int i, const uint32_t triangles, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH && c->mesh.count == triangles * 3u && c->mesh.texture != 0 && c->mesh.filter == filter
        && c->mesh.wrap == wrap;
}

// ...or `count` instances of a 3D one.
static bool mesh_3d(const int i, const uint32_t count, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH_3D && c->mesh3.count == count && c->mesh3.texture != 0 && c->mesh3.filter == filter
        && c->mesh3.wrap == wrap;
}

// ...or a run through a shader with `textures` textures, sampled as `sampling` says.
static const tide_draw_run *shaded(const int i, const uint32_t textures, const uint32_t sampling)
{
    const tide_draw_command *c = draws[i].c;
    if (c->kind != TIDE_DRAW_SHADED) return NULL;
    const tide_draw_run *r = &draw.runs[c->shaded.run];
    return r->shader && r->shader->texture_count == textures && r->sampling == sampling ? r : NULL;
}

static bool is_target(const uint32_t texture)
{
    return texture != 0 && draw.textures[texture - 1u].space == TIDE_PIXELS_TARGET;
}

TIDE_TEST(wrap_a_mesh_draw_says_its_wrap_after_its_filter)
{
    frame();
    TIDE_REQUIRE(draw_count == 25);

    // No wrap is the cells at the edge. The same filter and wrap after another are one command: saying
    // Wrap.Clamp is saying nothing
    TIDE_CHECK(mesh(0, 1, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(1, 2, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(2, 2, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(3, 1, TIDE_FILTER_BILINEAR, TIDE_WRAP_MIRROR));
    // A target's color wraps as a grid does
    TIDE_CHECK(mesh(4, 1, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) && is_target(draws[4].c->mesh.texture));
    TIDE_CHECK(!is_target(draws[2].c->mesh.texture));

    // A wrap from a field, one picked, one made of a number, and a number that's no wrap's: the edge
    TIDE_CHECK(draws[5].c->kind == TIDE_DRAW_RECT);
    TIDE_CHECK(mesh(6, 1, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(7, 1, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(8, 1, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(9, 1, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));

    // In 3D, and for a list's instances: a run goes on only while the wrap does
    TIDE_CHECK(mesh_3d(10, 1, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh_3d(11, 1, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh_3d(12, 1, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT) && is_target(draws[12].c->mesh3.texture));
    TIDE_CHECK(mesh_3d(13, 2, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh_3d(14, 2, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR) && is_target(draws[14].c->mesh3.texture));
    for (int i = 0; i < 15; i++) TIDE_CHECK(draws[i].target == 0);

    // Into a target, by its methods
    TIDE_CHECK(mesh(15, 1, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) && draws[15].target != 0);
    TIDE_CHECK(mesh_3d(16, 1, TIDE_FILTER_BILINEAR, TIDE_WRAP_MIRROR) && draws[16].target == draws[15].target);
}

TIDE_TEST(wrap_a_shader_samples_each_texture_as_its_draw_or_its_frag_says)
{
    frame();
    TIDE_REQUIRE(draw_count == 25);

    // A vertex of the game's with no frag: the draw's filter and wrap, in its one texture's bits. The draws of one
    // shader that say no wrap clamp
    const tide_draw_run *plain = shaded(17, 1, 1u);
    const tide_draw_run *nearest = shaded(18, 1, 0u);
    const tide_draw_run *repeated = shaded(19, 1, tide_sampling_wrapped(0, TIDE_WRAP_REPEAT));
    const tide_draw_run *mirrored = shaded(20, 1, 1u | tide_sampling_wrapped(0, TIDE_WRAP_MIRROR));
    TIDE_REQUIRE(plain && nearest && repeated && mirrored);
    TIDE_CHECK(plain->shader == nearest->shader && plain->shader == repeated->shader && plain->shader == mirrored->shader);
    TIDE_CHECK(plain->textures[0] == repeated->textures[0] && !is_target(plain->textures[0]));
    TIDE_CHECK(!tide_sampling_blended(repeated->sampling, 0) && tide_sampling_wrap(repeated->sampling, 0) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(tide_sampling_blended(mirrored->sampling, 0) && tide_sampling_wrap(mirrored->sampling, 0) == TIDE_WRAP_MIRROR);
    // ...and with a vert, a target's color
    const tide_draw_run *shifted = shaded(21, 1, tide_sampling_wrapped(0, TIDE_WRAP_REPEAT));
    TIDE_REQUIRE(shifted);
    TIDE_CHECK(shifted->shader != plain->shader && is_target(shifted->textures[0]));

    // A frag: one grid read four ways is four textures, each with its own filter and wrap. The same way again,
    // and the default said outright, are no more
    const tide_draw_run *tiled = shaded(22, 4, 0xa405u);
    TIDE_REQUIRE(tiled);
    TIDE_CHECK(tiled->textures[0] == plain->textures[0] && tiled->textures[1] == tiled->textures[0]
               && tiled->textures[2] == tiled->textures[0] && tiled->textures[3] == tiled->textures[0]);
    static const struct {
        bool blended;
        tide_wrap wrap;
    } ways[4] = {{true, TIDE_WRAP_CLAMP}, {false, TIDE_WRAP_REPEAT}, {true, TIDE_WRAP_MIRROR}, {false, TIDE_WRAP_MIRROR}};
    for (uint32_t i = 0; i < 4; i++) {
        TIDE_CHECK(tide_sampling_blended(tiled->sampling, i) == ways[i].blended);
        TIDE_CHECK(tide_sampling_wrap(tiled->sampling, i) == ways[i].wrap);
    }
    // The Texture its draw gives it, the nearest mirrored and blended at the edge
    const tide_draw_run *framed = shaded(23, 2, 0x202u);
    TIDE_REQUIRE(framed);
    TIDE_CHECK(is_target(framed->textures[0]) && framed->textures[1] == framed->textures[0]);
    // A vert that samples
    TIDE_CHECK(shaded(24, 1, tide_sampling_wrapped(0, TIDE_WRAP_REPEAT)) != NULL);
}

// The list's own functions, as C calls them: a wrap beside each filter, and none for the edge.
TIDE_TEST(wrap_c_draws_with_a_wrap_too)
{
    static const tide_vertex corners[3] = {{{0, 0}, {0, 0}, {1, 1, 1, 1}}, {{1, 0}, {2, 0}, {1, 1, 1, 1}}, {{0, 1}, {0, 2}, {1, 1, 1, 1}}};
    static const tide_vertex3 solid[3] = {{{0, 0, 0}, {0, 0}, {1, 1, 1, 1}}, {{1, 0, 0}, {2, 0}, {1, 1, 1, 1}}, {{0, 1, 0}, {0, 2}, {1, 1, 1, 1}}};
    static const uint32_t faces[3] = {0, 1, 2};
    static const uint8_t pixels[4] = {255, 0, 0, 255};
    const tide_texture texture = {.pixels = pixels, .width = 1, .height = 1, .version = 1};
    const tide_mesh shape = {.vertices = solid, .vertex_count = 3, .indices = faces, .index_count = 3, .version = 1};

    tide_draw_list list = {0};
    tide_draw_mesh(&list, corners, 3, faces, 3, &texture, TIDE_FILTER_POINT);
    tide_draw_mesh_wrapped(&list, corners, 3, faces, 3, &texture, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP); // More of the same
    tide_draw_mesh_wrapped(&list, corners, 3, faces, 3, &texture, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT);
    tide_draw_triangles_wrapped(&list, tide_draw_vertices(&list, corners, 3), faces, 3, &texture, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR);
    tide_draw_mesh_3d(&list, &shape, tide_identity_f4x4(), &texture, TIDE_FILTER_POINT);
    tide_draw_mesh_3d_wrapped(&list, &shape, tide_identity_f4x4(), &texture, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT);
    tide_draw_mesh_3d_wrapped(&list, &shape, tide_identity_f4x4(), &texture, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT);
    TIDE_REQUIRE(list.count == 5);
    const tide_draw_command *c = list.commands;
    TIDE_CHECK(c[0].kind == TIDE_DRAW_MESH && c[0].mesh.count == 6 && c[0].mesh.wrap == TIDE_WRAP_CLAMP);
    TIDE_CHECK(c[1].kind == TIDE_DRAW_MESH && c[1].mesh.count == 3 && c[1].mesh.wrap == TIDE_WRAP_REPEAT);
    TIDE_CHECK(c[2].kind == TIDE_DRAW_MESH && c[2].mesh.count == 3 && c[2].mesh.wrap == TIDE_WRAP_MIRROR);
    TIDE_CHECK(c[3].kind == TIDE_DRAW_MESH_3D && c[3].mesh3.count == 1 && c[3].mesh3.wrap == TIDE_WRAP_CLAMP);
    TIDE_CHECK(c[4].kind == TIDE_DRAW_MESH_3D && c[4].mesh3.count == 2 && c[4].mesh3.wrap == TIDE_WRAP_REPEAT);

    // One list after another keeps each command's wrap
    tide_draw_list over = {0};
    tide_draw_append(&over, &list);
    TIDE_REQUIRE(over.count == 5);
    for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(over.commands[i].mesh.wrap == c[i].mesh.wrap && over.commands[i].mesh.filter == c[i].mesh.filter);
    for (uint32_t i = 3; i < 5; i++) TIDE_CHECK(over.commands[i].mesh3.wrap == c[i].mesh3.wrap);

    // What isn't a wrap's number is the edge
    TIDE_CHECK(tide_wrap_of(1) == TIDE_WRAP_REPEAT && tide_wrap_of(2) == TIDE_WRAP_MIRROR);
    TIDE_CHECK(tide_wrap_of(0) == TIDE_WRAP_CLAMP && tide_wrap_of(3) == TIDE_WRAP_CLAMP && tide_wrap_of(-1) == TIDE_WRAP_CLAMP);
    TIDE_CHECK(tide_sampling_wrapped(3, 7) == 0u && tide_sampling_wrapped(7, TIDE_WRAP_MIRROR) == 2u << 22);
}
