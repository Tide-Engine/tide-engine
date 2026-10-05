#pragma once

// A line of the terminal that's drawn over itself: how far a wait is, and
// then the one line a command ends with, in its place.

// Writes `text` over what's on the cursor's line.
void line_draw(const char *text);

// Leaves the line as it is, for the next one.
void line_end(void);

// Takes what's on the line away, for what's said next to start there.
void line_clear(void);
