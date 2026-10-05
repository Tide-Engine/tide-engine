#include <string.h>

#include "game.h"
#include "tide_test.h"

// Frames of gui.tide's views on a 1920 x 1080 window, played with made-up
// devices. Widgets are found by their text in the draw list.

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_pointer_poll(&devices, true); // As the platform polls it: the pointer follows the mouse
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_frame(NULL, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    devices.keyboard.text.count = 0;
}

static void start(void)
{
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_init(&local);
    frame();
}

// The `nth` text reading `text` the last frame drew, or NULL.
static const tide_draw_command *find(const char *text, int nth)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(draw.text + c->text, text) == 0 && nth-- == 0) return c;
    }
    return NULL;
}

// Puts the mouse `dx` right of where the text starts, in its middle.
static bool point_at(const char *text, const int nth, const float dx)
{
    const tide_draw_command *c = find(text, nth);
    if (!c) return false;
    devices.mouse.position = tide_f2(c->a.x + dx, 1080.0f - (c->a.y + c->b.x * 0.5f));
    return true;
}

// Moves there, presses and lets go, a frame each, then draws what that did.
static bool click_at(const char *text, const int nth, const float dx)
{
    if (!point_at(text, nth, dx)) return false;
    frame();
    tide_button_set(&devices.mouse.left, true);
    frame();
    tide_button_set(&devices.mouse.left, false);
    frame();
    frame();
    return true;
}

static bool click(const char *text, const int nth)
{
    return click_at(text, nth, 4.0f);
}

static void press(tide_button *key)
{
    tide_button_set(key, true);
    frame();
    tide_button_set(key, false);
    frame();
}

TIDE_TEST(gui_buttons_change_local_state)
{
    start();
    TIDE_REQUIRE(click("Play", 0));
    TIDE_CHECK(local.Menu.plays == 1);
    TIDE_CHECK(local.Menu.page == Page_Options);
    TIDE_CHECK(find("Play", 0) == NULL);
    TIDE_REQUIRE(click("Back", 0));
    TIDE_CHECK(local.Menu.page == Page_Title);
    TIDE_CHECK(local.Menu.plays == 1);
}

TIDE_TEST(gui_rects_use_the_screen)
{
    start();
    const tide_draw_command *corner = find("Corner", 0);
    TIDE_REQUIRE(corner != NULL);
    TIDE_CHECK(corner->a.x > 1920.0f - 210.0f && corner->a.x < 1920.0f - 10.0f);
    TIDE_REQUIRE(click("Corner", 0));
    TIDE_CHECK(local.Menu.corner == 1);
}

TIDE_TEST(gui_functions_that_draw_have_their_own_widgets)
{
    start();
    TIDE_REQUIRE(click("Quit", 0));
    TIDE_CHECK(local.Menu.quits == 1);
    TIDE_CHECK(local.Menu.plays == 0);
    TIDE_CHECK(local.Menu.page == Page_Title);
}

TIDE_TEST(gui_containers_of_our_own)
{
    start();
    TIDE_REQUIRE(click("Play", 0));
    TIDE_CHECK(find("Volume", 0) == NULL); // Audio is folded
    TIDE_REQUIRE(click("Audio", 0));
    TIDE_CHECK(local.Menu.audioOpen);
    TIDE_CHECK(find("Volume", 0) != NULL);

    // Twice ran its block twice, side by side: two buttons, each its own.
    const tide_draw_command *first = find("Again", 0);
    const tide_draw_command *second = find("Again", 1);
    TIDE_REQUIRE(first && second);
    TIDE_CHECK(first->a.y == second->a.y && first->a.x < second->a.x);
    TIDE_REQUIRE(click("Again", 1));
    TIDE_CHECK(local.Menu.twice == 1);
}

TIDE_TEST(gui_entities_have_their_own_widgets)
{
    start();
    TIDE_REQUIRE(click("Tag", 1));
    const Tag first = tide_get_Tag(&local, (tide_entity){1, 1});
    const Tag second = tide_get_Tag(&local, (tide_entity){2, 1});
    TIDE_REQUIRE(tide_has_Tag(&local, (tide_entity){1, 1}) && tide_has_Tag(&local, (tide_entity){2, 1}));
    TIDE_CHECK(first.clicks == 0);
    TIDE_CHECK(second.clicks == 1);
}

TIDE_TEST(gui_loops_make_widgets_of_their_own)
{
    start();
    TIDE_CHECK(find("Row", 2) != NULL);
    TIDE_CHECK(find("Rows: 0", 0) != NULL);
    TIDE_REQUIRE(click("Row", 1));
    TIDE_CHECK(local.Menu.rows == 1);
    TIDE_CHECK(find("Rows: 1", 0) != NULL); // A label with a value in it
}

TIDE_TEST(gui_typing_text)
{
    start();
    TIDE_CHECK(find("Hello, cat", 0) != NULL);
    TIDE_REQUIRE(click_at("Name", 0, 250.0f)); // Past the label, in the field
    devices.keyboard.text.count = 3;
    devices.keyboard.text.chars[0] = 's';
    devices.keyboard.text.chars[1] = 0xE9; // e with an acute accent: two bytes in UTF-8
    devices.keyboard.text.chars[2] = '!';
    frame();
    frame();
    const tide_str name = tide_text_read(&local.heap, local.Menu.player);
    TIDE_CHECK(name.bytes == 7 && name.chars == 6 && memcmp(name.ptr, "cats\xC3\xA9!", 7) == 0);
    TIDE_CHECK(find("Hello, cats\xC3\xA9!", 0) != NULL);
    press(&devices.keyboard.backspace);
    press(&devices.keyboard.backspace); // The accent's two bytes go as one character
    TIDE_CHECK(tide_text_read(&local.heap, local.Menu.player).bytes == 4);
}

TIDE_TEST(gui_typing_into_fields)
{
    start();
    TIDE_REQUIRE(click("Play", 0));
    TIDE_REQUIRE(click("Audio", 0));
    TIDE_REQUIRE(click_at("Players", 0, 250.0f)); // Past the label, in the field
    devices.keyboard.text.count = 1;
    devices.keyboard.text.chars[0] = '7';
    frame();
    TIDE_CHECK(local.Menu.players == 2); // Kept on Enter
    press(&devices.keyboard.enter);
    TIDE_CHECK(local.Menu.players == 7);

    // A float2 field is two fields, showing 0 and 0: the second is y.
    TIDE_REQUIRE(click("0", 1));
    devices.keyboard.text.count = 2;
    devices.keyboard.text.chars[0] = '1';
    devices.keyboard.text.chars[1] = '5';
    frame();
    press(&devices.keyboard.enter);
    TIDE_CHECK(local.Menu.offset.x == 0.0f && local.Menu.offset.y == 15.0f);
}

TIDE_TEST(gui_keyboard_moves_between_widgets)
{
    start();
    // In order: Corner, then Play and Quit, then the tags.
    press(&devices.keyboard.tab);
    press(&devices.keyboard.tab);
    press(&devices.keyboard.enter);
    TIDE_CHECK(local.Menu.corner == 0);
    TIDE_CHECK(local.Menu.plays == 1);
}

TIDE_TEST(gui_hides_what_it_uses_from_the_game)
{
    start();
    TIDE_REQUIRE(point_at("Play", 0, 4.0f));
    frame();
    tide_devices sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed);

    devices.mouse.position = tide_f2(5.0f, 5.0f); // Nothing there
    frame();
    sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.mouse.left.pressed);
}

TIDE_TEST(gui_disabled_blocks_gray_out_their_widgets)
{
    start();
    TIDE_REQUIRE(click("Connect", 0));
    TIDE_CHECK(local.Menu.connects == 1 && local.Menu.waiting);
    TIDE_REQUIRE(click("Connect", 0)); // Waiting: grayed out, and clicks do nothing
    TIDE_CHECK(local.Menu.connects == 1);
    const tide_draw_command *text = find("Connect", 0);
    TIDE_REQUIRE(text != NULL);
    TIDE_CHECK(text->color.a == 0.5f);
    local.Menu.waiting = false;
    TIDE_REQUIRE(click("Connect", 0));
    TIDE_CHECK(local.Menu.connects == 2);
}

// What the last frame drew just before the `nth` text reading `text`: the box
// a button's text is written on.
static const tide_draw_command *box_of(const char *text, const int nth)
{
    const tide_draw_command *c = find(text, nth);
    return c && c > draw.commands ? c - 1 : NULL;
}

static bool same_color(const tide_color a, const tide_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static tide_color color_of(const char *text)
{
    const tide_draw_command *c = find(text, 0);
    return c ? c->color : (tide_color){-1.0f, -1.0f, -1.0f, -1.0f};
}

TIDE_TEST(gui_themes_wrap_a_block)
{
    start();
    const tide_gui_theme usual = tide_gui_theme_default();
    const tide_draw_command *themed = box_of("Themed", 0);
    const tide_draw_command *plain = box_of("Plain", 0);
    TIDE_REQUIRE(themed && plain);
    // In the block: the theme's color, and its round corners
    TIDE_REQUIRE(themed->kind == TIDE_DRAW_SHAPE && themed->shape < draw.shape_count);
    TIDE_CHECK(same_color(themed->color, (tide_color){1.0f, 0.0f, 0.0f, 1.0f}));
    TIDE_CHECK(draw.shapes[themed->shape].radius == 6.0f);
    TIDE_CHECK(same_color(color_of("Themed"), usual.text)); // What it didn't name is as ever
    // After it: as before
    TIDE_CHECK(plain->kind == TIDE_DRAW_RECT && same_color(plain->color, usual.control));
    TIDE_REQUIRE(click("Themed", 0));
    TIDE_CHECK(local.Menu.themed == 1);
    // The control beside them draws nothing, and takes its clicks
    TIDE_REQUIRE(click_at("Plain", 0, 100.0f)); // Past the button, on the control
    TIDE_CHECK(local.Menu.themed == 11);
}

TIDE_TEST(gui_set_themes_hold_for_the_views_after)
{
    start();
    const tide_gui_theme usual = tide_gui_theme_default();
    const tide_color green = {0.0f, 1.0f, 0.0f, 1.0f};
    TIDE_CHECK(same_color(color_of("Set here"), usual.text) && same_color(color_of("And after"), usual.text));
    local.Menu.look = 1;
    frame();
    TIDE_CHECK(same_color(color_of("Set here"), green));
    TIDE_CHECK(same_color(color_of("And after"), green)); // The next view's too
    TIDE_CHECK(same_color(color_of("Plain"), usual.text)); // Not the ones before it
    TIDE_CHECK(same_color(color_of("card 0"), (tide_color){1.0f, 1.0f, 1.0f, 1.0f})); // Nor what a view draws itself
    local.Menu.look = 0;
    frame(); // Every frame starts from the usual look
    TIDE_CHECK(same_color(color_of("Set here"), usual.text) && same_color(color_of("And after"), usual.text));

    // A theme names what it changes: the rest of a GUITheme is the usual look.
    local.Menu.look = 2;
    frame();
    tide_gui_theme rounder = usual;
    rounder.radius = 3.0f;
    TIDE_CHECK(memcmp(&gui.theme, &rounder, sizeof rounder) == 0);
}

TIDE_TEST(gui_controls_are_the_views_to_draw)
{
    start();
    TIDE_CHECK(find("card 0", 0) && find("card 1", 0) && find("card 2", 0));
    TIDE_REQUIRE(point_at("card 1", 0, 4.0f));
    frame();
    TIDE_CHECK(find("card 1 hovered", 0) != NULL && find("card 0", 0) != NULL && find("card 2", 0) != NULL);
    tide_button_set(&devices.mouse.left, true);
    frame();
    TIDE_CHECK(find("card 1 pressed", 0) != NULL);
    TIDE_CHECK(local.Menu.picks == 0); // On release
    // The press is the GUI's: the game doesn't see it
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed && !sampled.pointer.press.pressed);
    tide_button_set(&devices.mouse.left, false);
    frame();
    TIDE_CHECK(local.Menu.picked == 1 && local.Menu.picks == 1); // Made in a loop, and the second one's click is its own
    TIDE_REQUIRE(click("card 2", 0));
    TIDE_CHECK(local.Menu.picked == 2 && local.Menu.picks == 2);
}

TIDE_TEST(gui_controls_take_the_focus_with_the_other_widgets)
{
    start();
    tide_button_set(&devices.keyboard.leftShift, true);
    press(&devices.keyboard.tab); // Back from nothing: the last widget, which is the third card
    tide_button_set(&devices.keyboard.leftShift, false);
    frame();
    TIDE_CHECK(find("card 2 focused", 0) != NULL);
    press(&devices.keyboard.leftArrow); // Left and right move it too, in the order they're called
    TIDE_CHECK(find("card 1 focused", 0) != NULL && find("card 2", 0) != NULL);
    press(&devices.keyboard.enter);
    TIDE_CHECK(local.Menu.picked == 1 && local.Menu.picks == 1);

    // The keys the GUI doesn't use are the cards' view's meanwhile, and no other view's.
    press(&devices.keyboard.digit2);
    TIDE_CHECK(local.Menu.digits == 1 && local.Menu.others == 0);
    press(&devices.keyboard.escape); // Lets go
    TIDE_CHECK(find("card 1", 0) != NULL && find("card 1 focused", 0) == NULL);
    press(&devices.keyboard.digit2);
    TIDE_CHECK(local.Menu.digits == 2 && local.Menu.others == 1);
}

TIDE_TEST(gui_navigate_lets_the_arrows_start_under_a_match)
{
    start();
    // The game reads the devices: the arrows are its own
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    press(&devices.keyboard.upArrow);
    TIDE_CHECK(find("card 2 focused", 0) == NULL);
    // ...until a view says a menu is up
    local.Menu.menuUp = true;
    frame();
    sampled = devices;
    tide_gui_hide(&gui, &sampled);
    press(&devices.keyboard.upArrow); // Back from nothing: the last one
    TIDE_CHECK(find("card 2 focused", 0) != NULL);
}

TIDE_TEST(gui_disabled_blocks_stop_controls)
{
    start();
    local.Menu.locked = true;
    frame();
    TIDE_REQUIRE(click("card 1", 0));
    TIDE_CHECK(local.Menu.picks == 0);
    TIDE_CHECK(find("card 1", 0) != NULL); // Not hovered either, with the mouse on it
    // A press there still isn't the game's
    tide_devices sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed);
    tide_button_set(&devices.keyboard.leftShift, true);
    press(&devices.keyboard.tab); // The focus skips them: the last widget is the one before the cards
    tide_button_set(&devices.keyboard.leftShift, false);
    TIDE_CHECK(find("card 2 focused", 0) == NULL);
}
