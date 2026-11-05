#pragma once

#include <stdbool.h>
#include <stddef.h>

// A keyboard on screen that the game's C shows, where the system shows none
// by itself while the player types: Steam's, on a Steam Deck and in Big
// Picture (packages/steam). Phones and their browsers show their own anyway.
// A game library has its own registration, as for tide/network.h; the
// standard host asks its game's table for it once a frame.
typedef struct tide_screen_keyboard {
    void *self;
    // The player started typing (true), into a field of the GUI's or into
    // what a view said shows the keyboard (GUI.ShowKeyboard), or stopped
    // (false): once each time that changes. A keyboard registered while the
    // player types is told so at the next frame.
    void (*typing)(void *self, bool typing);
} tide_screen_keyboard;

// Registers `keyboard`, copied, from now on, or with NULL, none. What `self`
// points at stays the game's, and has to stay good while it's registered.
void tide_screen_keyboard_set(const tide_screen_keyboard *keyboard);
// What's registered, or NULL.
const tide_screen_keyboard *tide_screen_keyboard_get(void);
