// tide_rows_stops <call>: makes one of the calls tide/rows.h stops the program
// over, each of which has to say which function it was, in which system, and
// what to write instead (see tests/CMakeLists.txt).

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __wasi__
#include <signal.h>
#endif

#include "tide/rows.h"

typedef struct place {
    float x;
    float y;
} place;

typedef struct narrow {
    uint16_t a;
    uint16_t b;
    uint16_t c;
} narrow;

// The entity, a place the system reads, a place it writes, and a component
// whose size isn't a multiple of 4
static const uint32_t sizes[] = {sizeof(tide_entity), 4, 4, 4, 4, 2, 2, 2};
static const tide_columns columns = {8, 4, sizes};
static const tide_rows_component components[] = {{1, sizeof(place), -1}, {3, sizeof(place), 0}, {5, sizeof(narrow), 2}};

#ifndef __wasi__
// abort() ends the program by a signal on Linux and macOS, which CTest counts
// as a crash whatever the program printed: end it with an exit code instead.
static void aborted(const int sig)
{
    (void)sig;
    _Exit(1);
}
#endif

int main(const int argc, char **argv)
{
#ifdef _WIN32
    // Stopping without the debug C runtime's dialog, which would wait for a click
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifndef __wasi__
    signal(SIGABRT, aborted);
#endif
    const char *call = argc > 1 ? argv[1] : "";
    tide_table t = {0};
    for (int i = 0; i < 4; i++) tide_table_add(&t, &columns);
    tide_page *held[5];
    tide_rows rows;
    tide_rows_begin(&rows, &t, &columns, 0, 0, 4, true, components, 3, held, "Game.Move");
    tide_rows once;
    tide_rows_begin(&once, NULL, NULL, 0, 0, 0, true, NULL, 0, NULL, "Game.Step");
    place p = {0};
    if (strcmp(call, "component") == 0) tide_rows_floats(&rows, 3, 0);
    else if (strcmp(call, "none") == 0) tide_rows_ints(&once, 0, 0);
    else if (strcmp(call, "narrow") == 0) tide_rows_ints(&rows, 2, 0);
    else if (strcmp(call, "offset") == 0) tide_rows_floats(&rows, 0, 2);
    else if (strcmp(call, "past") == 0) tide_rows_floats_mut(&rows, 1, sizeof(place));
    else if (strcmp(call, "mut") == 0) tide_rows_floats_mut(&rows, 0, offsetof(place, y));
    else if (strcmp(call, "set") == 0) tide_rows_set(&rows, 0, 1, &p);
    else if (strcmp(call, "row") == 0) tide_rows_get(&rows, 1, 4, &p);
    printf("'%s' went through\n", call);
    return 1;
}
