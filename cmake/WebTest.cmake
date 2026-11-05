# tide_add_web_test(<name> TARGET <target> [QUERY <query>] [PIXELS] [EXPECT <text>] [SECONDS <n>])
#
# Web builds: adds a test that serves <target>'s page to headless Chrome or
# Edge in real time (cmake/run_web_served_test.mjs) and passes if the page
# exits with 0. It draws with WebGPU, the only web backend, on the browser's
# software WebGPU (SwiftShader), so no GPU is needed; the browser only answers
# about its GPU in real time, which is why every page is served. The page's
# shell must send its output and exit code back and set Tide.checkGL, so
# WebGPU validation errors and shader warnings fail it, as
# platform/web/test_shell.html does. The page is cross-origin isolated, so the
# program runs on threads. The test is skipped if no browser is found.
# TIDE_WEB_HARDWARE=1 in a test's environment draws on the machine's own GPU
# instead, failing if the browser gives a software adapter (cmake/web_gpu.mjs;
# `ctest --preset web-hardware` runs the PIXELS tests that way).
#
# QUERY is the program's own words, before anything else in the page's
# address. EXPECT is a line the program has to print. SECONDS is how long the
# page has to give its result, 120 unless a test's work on the software
# renderer needs more; the runner then asks the page where it stands, which
# can take 50 more: CTest's guard is SECONDS + 50.
#
# PIXELS says the test reads WebGPU's pixels back (a canvas or a target read
# through tide_platform_read_pixels and the like): label `pixels`, for leaving
# them out (ctest -LE pixels) where a browser can't read WebGPU's pixels. On
# Linux without a GPU Chrome reads zeros unless its own drawing goes through
# SwiftShader's Vulkan, which the runner asks for, so CI runs them everywhere.

# For web builds' tests, and desktop rooms against a browser (platform/tests/rooms.mjs).
find_program(TIDE_BROWSER
    NAMES chrome google-chrome chromium chromium-browser msedge "Google Chrome"
    PATHS
        "C:/Program Files/Google/Chrome/Application"
        "C:/Program Files (x86)/Microsoft/Edge/Application"
        "/Applications/Google Chrome.app/Contents/MacOS"
    DOC "Chrome or Edge, for web tests")
find_program(TIDE_FIREFOX
    NAMES firefox
    PATHS "C:/Program Files/Mozilla Firefox" "/Applications/Firefox.app/Contents/MacOS"
    DOC "Firefox, for desktop rooms against a second browser")

# Chrome's software renderer uses workers across the host's CPUs. Account for
# them in CTest's budget, so concurrent browsers and WASI tests don't starve
# a page while its real-time guard is running. This is the build host even
# when the tests themselves are compiled for WebAssembly.
include(ProcessorCount)
ProcessorCount(TIDE_WEB_TEST_PROCESSORS)
if(NOT TIDE_WEB_TEST_PROCESSORS GREATER 0)
    set(TIDE_WEB_TEST_PROCESSORS 1)
endif()

function(tide_add_web_test name)
    cmake_parse_arguments(ARG "PIXELS" "TARGET;QUERY;EXPECT;SECONDS" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "tide_add_web_test(${name}): unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    set(page "$<TARGET_FILE_DIR:${ARG_TARGET}>/${ARG_TARGET}.html")
    set(expect "")
    if(ARG_EXPECT)
        set(expect --expect "${ARG_EXPECT}")
    endif()
    if(NOT ARG_SECONDS)
        set(ARG_SECONDS 120)
    endif()
    math(EXPR guard "${ARG_SECONDS} + 50")
    add_test(NAME ${name}
        COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_web_served_test.mjs" "${TIDE_BROWSER}" "${page}"
            "${CMAKE_CURRENT_BINARY_DIR}/${name}-browser" "${ARG_QUERY}" ${expect} --seconds ${ARG_SECONDS})
    set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT ${guard}
        PROCESSORS ${TIDE_WEB_TEST_PROCESSORS})
    if(ARG_PIXELS)
        set_tests_properties(${name} PROPERTIES LABELS pixels)
    endif()
endfunction()
