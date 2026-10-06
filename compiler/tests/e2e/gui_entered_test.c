#include <string.h>

#include "game.h"
#include "tide_test.h"

// Frames of gui_entered.tide's views on a 1920 x 1080 window, in a host's
// order (the game's sample, then the frame), played with made-up devices.
// Fields are found by their labels in the draw list.

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_devices sampled; // What the last frame's sample had of the devices

static void frame(void)
{
    tide_pointer_poll(&devices, true); // As the platform polls it: the pointer follows the mouse
    sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_devices_consume(&devices);
    tide_draw_reset(&draw);
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

// The text reading `text` the last frame drew, or NULL.
static const tide_draw_command *find(const char *text)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(draw.text + c->text, text) == 0) return c;
    }
    return NULL;
}

// A click `dx` right of where the text starts, in its middle: the pointer
// comes, presses and lets go, a frame each.
static bool click_at(const char *text, const float dx)
{
    const tide_draw_command *c = find(text);
    if (!c) return false;
    devices.mouse.position = tide_f2(c->a.x + dx, 1080.0f - (c->a.y + c->b.x * 0.5f));
    frame();
    tide_button_set(&devices.mouse.left, true);
    frame();
    tide_button_set(&devices.mouse.left, false);
    frame();
    return true;
}

// A frame in which the player typed `text`.
static void type(const char *text)
{
    for (const char *c = text; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
    frame();
}

static void press(tide_button *key)
{
    tide_button_set(key, true);
    frame();
    tide_button_set(key, false);
    frame();
}

static bool is(const tide_text field, const char *text)
{
    const tide_str s = tide_text_read(&local.heap, field);
    return s.bytes == (int32_t)strlen(text) && memcmp(s.ptr, text, strlen(text)) == 0;
}

static bool any_of(const tide_button b)
{
    return b.pressed || b.down || b.up || b.held;
}

TIDE_TEST(gui_entered_joins_with_enter_or_the_button)
{
    start();
    const Lobby *lobby = &local.Lobby;
    TIDE_REQUIRE(click_at("Code", 250.0f)); // Past the label, in the field
    type("K7QF2M");
    TIDE_CHECK(is(lobby->code, "K7QF2M") && lobby->joins == 0 && lobby->entries == 0);

    // Enter ends the typing: entered, in that frame alone, and the key is the GUI's
    tide_button_set(&devices.keyboard.enter, true);
    frame();
    TIDE_CHECK(lobby->joins == 1 && lobby->entries == 1 && is(lobby->joined, "K7QF2M"));
    TIDE_CHECK(!any_of(sampled.keyboard.enter));
    frame();
    TIDE_CHECK(lobby->joins == 1 && lobby->entries == 1);
    TIDE_CHECK(!any_of(sampled.keyboard.enter));
    tide_button_set(&devices.keyboard.enter, false);
    frame();
    TIDE_CHECK(lobby->joins == 1 && lobby->entries == 1 && !any_of(sampled.keyboard.enter));

    // On the field, which kept the focus, Enter starts the typing again: that enters nothing
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->joins == 1 && lobby->entries == 1);
    type("9");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->joins == 2 && lobby->entries == 2 && is(lobby->joined, "K7QF2M9"));

    // The button joins too, and enters nothing
    TIDE_REQUIRE(click_at("Join", 4.0f));
    TIDE_CHECK(lobby->joins == 3 && lobby->entries == 2);

    // Escape stops the typing, and enters nothing
    TIDE_REQUIRE(click_at("Code", 250.0f));
    type("X");
    press(&devices.keyboard.escape);
    TIDE_CHECK(is(lobby->code, "K7QF2M9X") && lobby->joins == 3 && lobby->entries == 2);
}

TIDE_TEST(gui_entered_goes_where_the_argument_says)
{
    start();
    const Lobby *lobby = &local.Lobby;

    // A field of local state: a number is kept as it's entered
    TIDE_REQUIRE(click_at("Count", 250.0f));
    type("12");
    TIDE_CHECK(lobby->count == 4 && lobby->counts == 0);
    tide_button_set(&devices.keyboard.enter, true);
    frame();
    TIDE_CHECK(lobby->count == 12 && lobby->countChanges == 1 && lobby->counts == 1 && lobby->countEntered);
    tide_button_set(&devices.keyboard.enter, false);
    frame();
    TIDE_CHECK(lobby->counts == 1 && !lobby->countEntered); // Each call says: false again

    // A local with its type written
    TIDE_REQUIRE(click_at("Ratio", 250.0f));
    type("0.25");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->ratio == 0.25f && lobby->ratios == 1);

    // Left out
    TIDE_REQUIRE(click_at("Note", 250.0f));
    type("hi");
    press(&devices.keyboard.enter);
    TIDE_CHECK(is(lobby->note, "hi") && lobby->noteChanges == 1);

    // Each field is entered by itself
    TIDE_CHECK(lobby->entries == 0 && lobby->joins == 0 && lobby->counts == 1 && lobby->ratios == 1 && lobby->plain == 0);
}

TIDE_TEST(gui_entered_leaves_the_fields_that_dont_say_as_they_were)
{
    start();
    const Lobby *lobby = &local.Lobby;
    TIDE_REQUIRE(click_at("Name", 250.0f));
    type("cat");
    TIDE_CHECK(is(lobby->name, "cat") && lobby->plain == 1);
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->plain == 1);
    TIDE_REQUIRE(click_at("Players", 250.0f));
    type("7");
    TIDE_CHECK(lobby->players == 2);
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->players == 7 && lobby->plain == 2);
    TIDE_REQUIRE(click_at("Volume", 250.0f));
    type("0.5");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->volume == 0.5f && lobby->plain == 3);
    TIDE_CHECK(lobby->entries == 0 && lobby->counts == 0 && lobby->ratios == 0);
}

TIDE_TEST(gui_entered_at_a_rect)
{
    start();
    const Lobby *lobby = &local.Lobby;
    // The view's local was true before the call that makes it
    TIDE_CHECK(lobby->rooms == 0 && lobby->seated == 0 && lobby->scaled == 0);
    TIDE_REQUIRE(click_at("Room", 250.0f));
    type("K7");
    press(&devices.keyboard.enter);
    TIDE_CHECK(is(lobby->room, "K7") && lobby->rooms == 1);
    // A local the view changes after the call
    TIDE_REQUIRE(click_at("Seats", 250.0f));
    type("6");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->seats == 6 && lobby->seated == 1);
    // By name, in another order than the parameters'
    TIDE_REQUIRE(click_at("Scale", 250.0f));
    type("2");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->scale == 2.0f && lobby->scaled == 1);
    // The numpad's Enter, and Escape, which enters nothing
    TIDE_REQUIRE(click_at("Room", 250.0f));
    type("Q");
    press(&devices.keyboard.numpadEnter);
    TIDE_CHECK(is(lobby->room, "K7Q") && lobby->rooms == 2);
    TIDE_REQUIRE(click_at("Room", 250.0f));
    type("F");
    press(&devices.keyboard.escape);
    TIDE_CHECK(is(lobby->room, "K7QF") && lobby->rooms == 2 && lobby->seated == 1 && lobby->scaled == 1);
}

TIDE_TEST(gui_entered_is_made_in_its_turn)
{
    start();
    const Lobby *lobby = &local.Lobby;
    // Read before the call in its statement, the variable is what it held; the call then made it
    TIDE_CHECK(lobby->before == 1 && lobby->after == 0);
    TIDE_REQUIRE(click_at("After", 250.0f));
    type("5");
    press(&devices.keyboard.enter);
    TIDE_CHECK(lobby->order == 5 && lobby->after == 3); // Changed, and entered: read after the call that made it
    TIDE_REQUIRE(click_at("Before", 250.0f));
    type("2");
    tide_button_set(&devices.keyboard.enter, true);
    frame();
    TIDE_CHECK(lobby->turn == 2.0f && lobby->before == 3);
    tide_button_set(&devices.keyboard.enter, false);
    frame();
    TIDE_CHECK(lobby->before == 1);

    // A function that draws passes its own `out` parameter on
    TIDE_REQUIRE(click_at("Tag", 250.0f));
    type("x");
    TIDE_CHECK(lobby->tagged == 10);
    press(&devices.keyboard.enter);
    TIDE_CHECK(is(lobby->tag, "x") && lobby->tagged == 11);
}
