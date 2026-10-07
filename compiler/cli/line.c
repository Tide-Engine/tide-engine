#include "line.h"

#include <stdio.h>
#include <string.h>

static int drawn; // How long the line the terminal's cursor is still on is

void line_draw(const char *text)
{
    const int n = (int)strlen(text);
    printf("%s%s%*s", drawn ? "\r" : "", text, drawn > n ? drawn - n : 0, "");
    fflush(stdout);
    drawn = n;
}

void line_end(void)
{
    if (drawn) printf("\n");
    fflush(stdout);
    drawn = 0;
}

void line_clear(void)
{
    if (drawn) printf("\r%*s\r", drawn, "");
    fflush(stdout);
    drawn = 0;
}
