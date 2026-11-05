#include "tide/screen_keyboard.h"

static tide_screen_keyboard keyboard;

void tide_screen_keyboard_set(const tide_screen_keyboard *k)
{
    keyboard = k ? *k : (tide_screen_keyboard){0};
}

const tide_screen_keyboard *tide_screen_keyboard_get(void)
{
    return keyboard.typing ? &keyboard : NULL;
}
