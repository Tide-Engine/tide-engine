#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide_test.h"

#ifdef _WIN32
#include <crtdbg.h>

__declspec(dllimport) unsigned int __stdcall GetErrorMode(void); // Windows', without its headers
#endif

// A run nobody watches is one with TIDE_HIDDEN set to anything but nothing or
// 0, and on Windows its dialogs are off before main, in a program with no
// window too, like this one. CTest runs this with the variable set and with it
// empty (tests/CMakeLists.txt), and it holds either way for whoever runs
// tide_tests.
TIDE_TEST(hidden_is_what_the_environment_says)
{
    const char *value = getenv("TIDE_HIDDEN");
    const bool hidden = value && value[0] && strcmp(value, "0") != 0;
    TIDE_CHECK(tide_hidden() == hidden);
#ifdef _WIN32
    // Asked without changing them: no bit in the mask, and the modes' own way of asking
    const unsigned stops = _set_abort_behavior(0, 0);
    const int asserts = _set_error_mode(_REPORT_ERRMODE);
    const unsigned boxes = 0x0001u | 0x0002u | 0x8000u; // As engine/src/page.c sets them
    if (hidden) {
        TIDE_CHECK(stops == 0);
        TIDE_CHECK(asserts == _OUT_TO_STDERR);
        TIDE_CHECK((GetErrorMode() & boxes) == boxes);
    } else { // As the C runtime starts with them. The system's are this program's parent's
        TIDE_CHECK(stops != 0);
        TIDE_CHECK(asserts == _OUT_TO_DEFAULT);
    }
#ifdef _DEBUG
    for (int kind = _CRT_ERROR; kind <= _CRT_ASSERT; kind++) {
        TIDE_CHECK(_CrtSetReportMode(kind, _CRTDBG_REPORT_MODE) == (hidden ? _CRTDBG_MODE_FILE : _CRTDBG_MODE_WNDW));
    }
#endif
#endif
}
