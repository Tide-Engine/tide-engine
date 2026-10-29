#pragma once

// When a desktop window holds the cursor (see tide_window_cursor_lock in
// src/window.h). It's apart from glfw.c, and has nothing of the window's, so
// it's checked without one (tests/cursor_rules.c). A hidden window must never
// hold the cursor, even if it reports focus.
//
// - The game asks, every frame it wants the cursor, and only a window with the
//   focus and is visible holds it. Hidden/quiet runs never hold it.
// - Escape gives the cursor back to the player while the game still asks, as a
//   browser does. The player's next click on the window, or press of another
//   key, takes it again. Escape never does.
// - A game that stops asking starts over: the next time it asks, the window
//   takes the cursor at once, as after a pause menu's Resume.

#include <stdbool.h>

typedef struct tide_cursor {
    bool wanted; // The game asks for the cursor
    bool let_go; // ...which the player took back with Escape, until a click or another key
} tide_cursor;

// The game says whether it wants the cursor.
static inline void tide_cursor_ask(tide_cursor *cursor, const bool wanted)
{
    cursor->wanted = wanted;
    if (!wanted) cursor->let_go = false;
}

// A key went down: not one that's still held, repeating.
static inline void tide_cursor_key(tide_cursor *cursor, const bool escape)
{
    cursor->let_go = escape && cursor->wanted;
}

// A mouse button went down on the window.
static inline void tide_cursor_click(tide_cursor *cursor)
{
    cursor->let_go = false;
}

// Whether the window holds the cursor now.
static inline bool tide_cursor_held(const tide_cursor *cursor, const bool focused, const bool visible)
{
    return cursor->wanted && !cursor->let_go && focused && visible;
}
