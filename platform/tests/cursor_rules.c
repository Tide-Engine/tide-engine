// Desktop only: when a window holds the cursor (desktop/cursor.h), which
// desktop/glfw.c follows with GLFW's. No window: a test's is hidden and never
// has the focus, so it never holds the cursor (see cursor.c), and what one with
// the focus does is checked here, as the game, the player and the system say
// what happens to it.

#include "cursor.h"
#include "tide_test.h"

TIDE_TEST(cursor_is_held_while_the_game_asks_and_the_window_has_the_focus)
{
    tide_cursor cursor = {0};
    TIDE_CHECK(!tide_cursor_held(&cursor, true)); // Nothing asked yet
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true));   // At once: no click to wait for, as a browser has
    TIDE_CHECK(!tide_cursor_held(&cursor, false)); // Alt+Tab
    TIDE_CHECK(tide_cursor_held(&cursor, true));   // ...and back, the game still asking
    tide_cursor_ask(&cursor, false);
    TIDE_CHECK(!tide_cursor_held(&cursor, true));
}

TIDE_TEST(cursor_escape_gives_it_back_until_a_click_or_a_key)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true)); // The player has the cursor back
    for (int frame = 0; frame < 5; frame++) {
        tide_cursor_ask(&cursor, true);
        TIDE_CHECK(!tide_cursor_held(&cursor, true)); // ...however many frames the game goes on asking
    }
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true)); // Escape again doesn't take it
    tide_cursor_click(&cursor);
    TIDE_CHECK(tide_cursor_held(&cursor, true)); // A click on the window does

    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true));
    tide_cursor_key(&cursor, false);
    TIDE_CHECK(tide_cursor_held(&cursor, true)); // And so does another key
}

TIDE_TEST(cursor_escape_outlasts_the_focus)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, false)); // Alt+Tab, with the cursor free
    TIDE_CHECK(!tide_cursor_held(&cursor, true));  // Back: it's still the player's
    tide_cursor_click(&cursor);
    TIDE_CHECK(tide_cursor_held(&cursor, true));
    tide_cursor_click(&cursor); // A click while it's held changes nothing
    tide_cursor_key(&cursor, false);
    TIDE_CHECK(tide_cursor_held(&cursor, true));
}

TIDE_TEST(cursor_escape_is_forgotten_once_the_game_stops_asking)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    // A game whose Escape is its pause menu: it stops asking in that frame
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, false);
    TIDE_CHECK(!tide_cursor_held(&cursor, true));
    tide_cursor_click(&cursor); // On Resume
    TIDE_CHECK(!tide_cursor_held(&cursor, true));
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true)); // Asked for again, it's held at once

    // ...and one whose Escape closes the menu too: the game wasn't asking when it was pressed
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, false);
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true));
}
