#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// Tide's Filter has its third member in this game's header, as its code names it, with tide_filter's values
_Static_assert(Filter_Point == (int)TIDE_FILTER_POINT && Filter_Bilinear == (int)TIDE_FILTER_BILINEAR
               && Filter_Trilinear == (int)TIDE_FILTER_TRILINEAR, "Filter has tide_filter's values");

static const tide_draw_command *draws[32];
static int draw_count;

static void frame(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    draw_count = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        const bool draws_something = c->kind == TIDE_DRAW_MESH || c->kind == TIDE_DRAW_MESH_3D || c->kind == TIDE_DRAW_SHADED;
        if (draws_something && draw_count < 32) draws[draw_count++] = c;
    }
}

static bool mesh(const int i, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i];
    return c->kind == TIDE_DRAW_MESH && c->mesh.texture != 0 && c->mesh.filter == filter && c->mesh.wrap == wrap;
}

static const tide_draw_run *shaded(const int i)
{
    return draws[i]->kind == TIDE_DRAW_SHADED ? &draw.runs[draws[i]->shaded.run] : NULL;
}

TIDE_TEST(mipmaps_a_draw_records_trilinear_wherever_its_said)
{
    frame();
    TIDE_REQUIRE(draw_count == 10);
    // Said at the draw; the grid's declaration's, with its wrap; a Texture's own, with the draw's over it; a
    // filter that's a value; a target's color, as its field says
    TIDE_CHECK(mesh(0, TIDE_FILTER_TRILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(1, TIDE_FILTER_TRILINEAR, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(2, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(3, TIDE_FILTER_TRILINEAR, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(4, TIDE_FILTER_TRILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(draws[5]->kind == TIDE_DRAW_MESH_3D && draws[5]->mesh3.filter == TIDE_FILTER_TRILINEAR
               && draws[5]->mesh3.wrap == TIDE_WRAP_REPEAT);
    // The Texture a field holds says it itself
    TIDE_CHECK(tide_texture_filter(local.Art.kept) == TIDE_FILTER_TRILINEAR && tide_texture_wrap(local.Art.kept) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(tide_texture_filter(tide_texture_filtered(local.Art.kept, TIDE_FILTER_POINT)) == TIDE_FILTER_POINT);
    TIDE_CHECK(tide_texture_wrap(tide_texture_filtered(local.Art.kept, TIDE_FILTER_BILINEAR)) == TIDE_WRAP_REPEAT);
    const tide_str said = tide_text_read(&local.heap, local.Art.said);
    const char *want = "Trilinear Bilinear Point";
    TIDE_CHECK((size_t)said.bytes == strlen(want) && memcmp(said.ptr, want, (size_t)said.bytes) == 0);
}

TIDE_TEST(mipmaps_a_shader_run_says_how_each_texture_is_read)
{
    frame();
    TIDE_REQUIRE(draw_count == 10);
    // One shader, whose text says nothing of which filter a texture it's given has, in three runs
    const tide_draw_run *kept = shaded(6), *plain = shaded(7), *said = shaded(8);
    TIDE_REQUIRE(kept && plain && said && kept->shader == plain->shader && plain->shader == said->shader);
    const tide_shader *shader = kept->shader;
    TIDE_REQUIRE(shader->texture_count == 5);
    int trilinear[3] = {0}, bilinear[3] = {0}, point[3] = {0};
    const tide_draw_run *runs[3] = {kept, plain, said};
    for (int r = 0; r < 3; r++) {
        for (uint32_t t = 0; t < shader->texture_count; t++) {
            const tide_filter filter = tide_sampling_filter(runs[r]->sampling, t);
            trilinear[r] += filter == TIDE_FILTER_TRILINEAR;
            bilinear[r] += filter == TIDE_FILTER_BILINEAR;
            point[r] += filter == TIDE_FILTER_POINT;
        }
    }
    // The grid that says Trilinear, the one a Sample says it of, and the source, where its Sample says nothing
    // and the draw's texture says Trilinear: of a field, or at the draw
    TIDE_CHECK(trilinear[0] == 3 && bilinear[0] == 1 && point[0] == 1);
    TIDE_CHECK(trilinear[1] == 2 && bilinear[1] == 2 && point[1] == 1);
    TIDE_CHECK(trilinear[2] == 3 && bilinear[2] == 1 && point[2] == 1);
    // A texture read between its smaller images is read blended, for whoever only asks that
    for (uint32_t t = 0; t < shader->texture_count; t++) {
        TIDE_CHECK(tide_sampling_filter(kept->sampling, t) != TIDE_FILTER_TRILINEAR || tide_sampling_blended(kept->sampling, t));
    }
    // The frag asks how fast its uvs change as it starts, and reads with the answer; the vert reads the texture's own
    TIDE_CHECK(strstr(shader->glsl_fragment, "dFdx(tide_uv0)") && strstr(shader->glsl_fragment, "textureGrad(pixels"));
#ifdef __wasm__ // Where there's WebGPU to read it
    TIDE_CHECK(strstr(shader->wgsl, "dpdx(tide_uv0)") && strstr(shader->wgsl, "textureSampleGrad(pixels"));
#endif
    TIDE_CHECK(!strstr(shader->glsl_vertex, "textureGrad") && strstr(shader->glsl_vertex, "textureLod(pixels"));
    // With no frag, the draw's own texture is read as it says, by the GPU's own pick
    const tide_draw_run *bare = shaded(9);
    TIDE_REQUIRE(bare && bare->shader != shader);
    const uint32_t own = bare->shader->texture_count - 1u;
    TIDE_CHECK(tide_sampling_filter(bare->sampling, own) == TIDE_FILTER_TRILINEAR);
    TIDE_CHECK(strstr(bare->shader->glsl_fragment, "texture(pixels") != NULL);
#ifdef __wasm__
    TIDE_CHECK(strstr(bare->shader->wgsl, "textureSample(pixels") != NULL);
#endif
}
