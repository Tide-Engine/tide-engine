#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// Tide's Filter has its fourth member in this game's header, as its code names it, with tide_filter's values
_Static_assert(Filter_Point == (int)TIDE_FILTER_POINT && Filter_Bilinear == (int)TIDE_FILTER_BILINEAR
               && Filter_Trilinear == (int)TIDE_FILTER_TRILINEAR && Filter_Anisotropic == (int)TIDE_FILTER_ANISOTROPIC,
               "Filter has tide_filter's values");

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static const tide_draw_command *draws[32];
static int draw_count;

static void frame(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size)); // The asset's Texture, as its declaration says it's read
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
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

TIDE_TEST(anisotropic_a_draw_records_it_wherever_its_said)
{
    frame();
    TIDE_REQUIRE(draw_count == 11);
    // Said at the draw; the grid's declaration's, with its wrap; an asset's; a Texture's own, with the draw's over
    // it; a filter that's a value; a target's color, as its field says
    TIDE_CHECK(mesh(0, TIDE_FILTER_ANISOTROPIC, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(1, TIDE_FILTER_ANISOTROPIC, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(2, TIDE_FILTER_ANISOTROPIC, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(3, TIDE_FILTER_TRILINEAR, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(4, TIDE_FILTER_ANISOTROPIC, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(5, TIDE_FILTER_ANISOTROPIC, TIDE_WRAP_CLAMP));
    TIDE_CHECK(draws[6]->kind == TIDE_DRAW_MESH_3D && draws[6]->mesh3.filter == TIDE_FILTER_ANISOTROPIC
               && draws[6]->mesh3.wrap == TIDE_WRAP_MIRROR);
    // An asset declared Trilinear is born so (it was born blended until Anisotropic was added)
    TIDE_CHECK(mesh(10, TIDE_FILTER_TRILINEAR, TIDE_WRAP_CLAMP));
    // The Texture a field holds says it itself, and a draw's filter over it keeps its wrap
    TIDE_CHECK(tide_texture_filter(local.Art.kept) == TIDE_FILTER_ANISOTROPIC && tide_texture_wrap(local.Art.kept) == TIDE_WRAP_MIRROR);
    TIDE_CHECK(tide_texture_filter(tide_texture_filtered(local.Art.kept, TIDE_FILTER_POINT)) == TIDE_FILTER_POINT);
    TIDE_CHECK(tide_texture_wrap(tide_texture_filtered(local.Art.kept, TIDE_FILTER_BILINEAR)) == TIDE_WRAP_MIRROR);
    const tide_str said = tide_text_read(&local.heap, local.Art.said);
    const char *want = "Anisotropic Trilinear Anisotropic";
    TIDE_CHECK((size_t)said.bytes == strlen(want) && memcmp(said.ptr, want, (size_t)said.bytes) == 0);
}

TIDE_TEST(anisotropic_a_shader_run_says_how_each_texture_is_read)
{
    frame();
    TIDE_REQUIRE(draw_count == 11);
    // One shader, whose text says nothing of which filter a texture it's given has, in three runs
    const tide_draw_run *kept = shaded(7), *plain = shaded(8), *said = shaded(9);
    TIDE_REQUIRE(kept && plain && said && kept->shader == plain->shader && plain->shader == said->shader);
    const tide_shader *shader = kept->shader;
    uint8_t sampler_reads[TIDE_SHADER_TEXTURES];
    const uint32_t samplers = tide_shader_samplers(shader, sampler_reads);
    // The asset's, the grid read anisotropic, the source's SampleGrad that says it and mirrors, the grid read
    // trilinear, and the source's
    TIDE_REQUIRE(samplers == 5);
    int anisotropic[3] = {0}, trilinear[3] = {0}, bilinear[3] = {0};
    const tide_draw_run *runs[3] = {kept, plain, said};
    for (int r = 0; r < 3; r++) {
        for (uint32_t t = 0; t < samplers; t++) {
            const tide_filter filter = tide_sampling_filter(runs[r]->sampling, t);
            anisotropic[r] += filter == TIDE_FILTER_ANISOTROPIC;
            trilinear[r] += filter == TIDE_FILTER_TRILINEAR;
            bilinear[r] += filter == TIDE_FILTER_BILINEAR;
            // Read along what a pixel covers, a texture is read blended and between its smaller images
            TIDE_CHECK(filter != TIDE_FILTER_ANISOTROPIC || tide_sampling_blended(runs[r]->sampling, t));
        }
    }
    // The source's Sample says nothing: the draw's texture says Anisotropic of a field, nothing, or at the draw
    TIDE_CHECK(anisotropic[0] == 4 && trilinear[0] == 1 && bilinear[0] == 0);
    TIDE_CHECK(anisotropic[1] == 3 && trilinear[1] == 1 && bilinear[1] == 1);
    TIDE_CHECK(anisotropic[2] == 4 && trilinear[2] == 1 && bilinear[2] == 0);
    // Every texture the run reads so has its smaller images made for it
    for (uint32_t i = 0; i < shader->texture_count; i++) {
        TIDE_CHECK(tide_filter_mips(tide_shader_texture_filter(shader, kept->sampling, i)));
    }
    // The frag asks how fast its uvs change as it starts, and reads with the answer: what the GPU reads along
#ifdef __wasm__ // A shader's text is its WGSL, which a build keeps on the web
    TIDE_CHECK(strstr(shader->wgsl, "dpdx(tide_uv0)") && strstr(shader->wgsl, "textureSampleGrad(pixels"));
    TIDE_CHECK(!strstr(shader->wgsl, "textureSampleLevel(pixels"));
#endif
}
