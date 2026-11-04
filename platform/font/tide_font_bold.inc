
// The bold weight as drawing takes it (TIDE_FONT_BOLD, tide/draw.h), in the
// array's object, so a program that names one has both: its file, which the
// renderer reads into font data (tide/font.h) the first time it's drawn.
#include "tide/font.h"
const tide_font tide_font_bold = {.data = 0, .size = 0, .id = 1, .file = tide_font_bold_ttf, .file_size = sizeof tide_font_bold_ttf};
