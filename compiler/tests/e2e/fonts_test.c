#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/font.h"
#include "tide/pack.h"
#include "tide_test.h"

// A game's own fonts (fonts.tide): each is a number of the font registry's,
// which the draw list and the renderer's measure get as its font.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
extern const unsigned char tide_font_bold_ttf[];
extern const unsigned int tide_font_bold_ttf_size;

static const tide_font *measured_in;
static int measures;

static tide_float2 measure(const char *text, const float size, const tide_target_size *target, const tide_font *font)
{
    (void)target;
    if (strcmp(text, "abcd") == 0) measured_in = font, measures++;
    return tide_f2((float)strlen(text) * size * 0.5f, size);
}

static const tide_font *font_of(const tide_draw_list *draw, const char *text, bool *found)
{
    *found = false;
    for (uint32_t i = 0; i < draw->count; i++) {
        const tide_draw_command *c = &draw->commands[i];
        if (c->kind != TIDE_DRAW_TEXT || strcmp(draw->text + c->text, text) != 0) continue;
        *found = true;
        return c->words.font && c->words.font <= draw->font_count ? draw->fonts[c->words.font - 1u] : NULL;
    }
    return NULL;
}

TIDE_TEST(fonts_of_the_game_draw_as_their_registry_fonts)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_local local = {0};
    tide_draw_list draw = {0};
    static tide_gui gui;
    tide_devices devices = {0};
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), measure);
    tide_frame(NULL, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);

    // The bold file decoded is the same outlines as the glob's second font, so the same number and font
    uint8_t *data;
    size_t size;
    TIDE_REQUIRE(tide_font_data_make(tide_font_bold_ttf, tide_font_bold_ttf_size, &data, &size) == TIDE_FONT_OK);
    const uint64_t bold = tide_font_data_header_of(data)->id;
    free(data);
    const Fonts *fonts = &local.Fonts;
    TIDE_CHECK(fonts->decodedOk && fonts->refused);
    TIDE_CHECK(fonts->decoded.id == bold && fonts->kept.id == bold);
    TIDE_CHECK(fonts->listed == 2);
    const tide_font *decoded = tide_font_find(bold);
    TIDE_REQUIRE(decoded != NULL);

    bool found;
    const tide_font *asset = font_of(&draw, "asset", &found);
    TIDE_CHECK(found && asset != NULL && asset != decoded && asset->id >= 2);
    TIDE_CHECK(asset == tide_font_find(asset ? asset->id : 0));
    TIDE_CHECK(font_of(&draw, "decoded", &found) == decoded && found);
    TIDE_CHECK(font_of(&draw, "listed", &found) == decoded && found);
    TIDE_CHECK(font_of(&draw, "keyed", &found) == decoded && found);
    TIDE_CHECK(font_of(&draw, "engine", &found) == TIDE_FONT_REGULAR && found);
    TIDE_CHECK(font_of(&draw, "passed", &found) == asset && found);
    TIDE_CHECK(measures == 1 && measured_in == asset);
    // The GUI's widgets, in its theme's font
    TIDE_CHECK(font_of(&draw, "themed", &found) == decoded && found);
    // Each font once in the list, however many commands name it
    TIDE_CHECK(draw.font_count == 2);

    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_pack_unload();
}
