#include "line.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

static int drawn; // How long the line the terminal's cursor is still on is
static bool paired;

void line_draw(const char *text)
{
    if (paired) line_clear();
    const int n = (int)strlen(text);
    printf("%s%s%*s", drawn ? "\r" : "", text, drawn > n ? drawn - n : 0, "");
    fflush(stdout);
    drawn = n;
}

void line_draw_pair(const char *label, const char *bar)
{
    int width = 70;
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(output, &mode)) SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(output, &info)) width = info.srWindow.Right - info.srWindow.Left;
#else
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 1) width = size.ws_col - 1;
#endif
    if (width < 1) width = 1;
    line_clear();
    printf("%.*s\n%.*s", width, label, width, bar);
    fflush(stdout);
    drawn = (int)strlen(bar);
    paired = true;
}

void line_end(void)
{
    if (drawn) printf("\n");
    fflush(stdout);
    drawn = 0;
    paired = false;
}

void line_clear(void)
{
    if (paired) printf("\r\x1b[2K\x1b[1A\r\x1b[2K");
    else if (drawn) printf("\r%*s\r", drawn, "");
    fflush(stdout);
    drawn = 0;
    paired = false;
}
