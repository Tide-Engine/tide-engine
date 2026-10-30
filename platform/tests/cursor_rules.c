// Desktop cursor policy, including hidden windows reporting focus. No window
// or GPU: these cases must never take the owner's screen or mouse.

#include "cursor.h"
#include "tide_test.h"

TIDE_TEST(cursor_is_held_while_the_game_asks_and_the_window_has_the_focus)
{
    tide_cursor cursor = {0};
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true)); // Nothing asked yet
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));   // At once: no click to wait for, as a browser has
    TIDE_CHECK(!tide_cursor_held(&cursor, false, true)); // Alt+Tab
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));   // ...and back, the game still asking
    tide_cursor_ask(&cursor, false);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));
}

TIDE_TEST(cursor_escape_gives_it_back_until_a_click_or_a_key)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true)); // The player has the cursor back
    for (int frame = 0; frame < 5; frame++) {
        tide_cursor_ask(&cursor, true);
        TIDE_CHECK(!tide_cursor_held(&cursor, true, true)); // ...however many frames the game goes on asking
    }
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true)); // Escape again doesn't take it
    tide_cursor_click(&cursor);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true)); // A click on the window does

    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));
    tide_cursor_key(&cursor, false);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true)); // And so does another key
}

TIDE_TEST(cursor_escape_outlasts_the_focus)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, false, true)); // Alt+Tab, with the cursor free
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));  // Back: it's still the player's
    tide_cursor_click(&cursor);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));
    tide_cursor_click(&cursor); // A click while it's held changes nothing
    tide_cursor_key(&cursor, false);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));
}

TIDE_TEST(cursor_escape_is_forgotten_once_the_game_stops_asking)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    // A game whose Escape is its pause menu: it stops asking in that frame
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, false);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));
    tide_cursor_click(&cursor); // On Resume
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true)); // Asked for again, it's held at once

    // ...and one whose Escape closes the menu too: the game wasn't asking when it was pressed
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, false);
    tide_cursor_key(&cursor, true);
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));
}

TIDE_TEST(cursor_hidden_window_never_holds_even_with_focus)
{
    tide_cursor cursor = {0};
    tide_cursor_ask(&cursor, true);
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));
    TIDE_CHECK(!tide_cursor_held(&cursor, true, false));
    TIDE_CHECK(!tide_cursor_held(&cursor, false, false));
    tide_cursor_click(&cursor);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, false));
    tide_cursor_key(&cursor, false);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, false));
    // Becoming visible again preserves the ordinary focus/Escape rules.
    TIDE_CHECK(tide_cursor_held(&cursor, true, true));
    tide_cursor_key(&cursor, true);
    TIDE_CHECK(!tide_cursor_held(&cursor, true, false));
    TIDE_CHECK(!tide_cursor_held(&cursor, true, true));
}
