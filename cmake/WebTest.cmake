# tide_add_web_test(<name> TARGET <target> [QUERY <query>] [WEBGPU [EXPECT <text>] [SECONDS <n>]] [D3D11])
#
# Web builds: adds a test that opens <target>'s page in headless Chrome or Edge
# and passes if the page exits with 0. It draws with WebGL 2 (backend=webgl in
# the page's address), on the browser's software renderer, so no GPU is
# needed, in virtual time. The page's shell must write stdout into
# <pre id="log"> and the exit code into <body data-exit>, and set
# Tide.checkGL so mistakes in drawing fail it, as platform/web/test_shell.html
# does. The test is skipped if no browser is found.
#
# With WEBGPU, it also adds <name>_webgpu, labelled webgpu: the same page
# drawing with WebGPU (backend=webgpu, which fails rather than falls back to
# WebGL), on the browser's software WebGPU. The browser only answers about its
# GPU in real time, so the page is served (cmake/run_web_served_test.mjs), and
# its shell must send its output and exit code back, as test_shell.html does.
# EXPECT is a line the program has to print. SECONDS is how long the page has
# to give its result, 100 unless a test's work on the software renderer needs
# more.
# D3D11 adds a Windows-host test through ANGLE's software D3D11/WARP backend,
# checking its renderer identity as well as the page's existing assertions.

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
    cmake_parse_arguments(ARG "WEBGPU;D3D11" "TARGET;QUERY;EXPECT;SECONDS" "" ${ARGN})
    set(page "$<TARGET_FILE_DIR:${ARG_TARGET}>/${ARG_TARGET}.html")
    set(query "backend=webgl")
    set(webgpu_query "backend=webgpu")
    if(ARG_QUERY) # The program's own words first: they're its arguments, in order
        set(query "${ARG_QUERY}&${query}")
        set(webgpu_query "${ARG_QUERY}&${webgpu_query}")
    endif()
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DNODE=${TIDE_NODE}"
            "-DBROWSER=${TIDE_BROWSER}"
            "-DPAGE=${page}"
            "-DQUERY=${query}"
            "-DPROFILE=${CMAKE_CURRENT_BINARY_DIR}/${name}-browser"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_web_test.cmake")
    set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:"
        PROCESSORS ${TIDE_WEB_TEST_PROCESSORS})
    if(ARG_D3D11 AND CMAKE_HOST_WIN32)
        add_test(NAME ${name}_d3d11
            COMMAND "${CMAKE_COMMAND}"
                "-DNODE=${TIDE_NODE}"
                "-DBROWSER=${TIDE_BROWSER}"
                "-DPAGE=${page}"
                "-DQUERY=${query}&expect-angle=Direct3D11"
                "-DANGLE_BACKEND=d3d11-warp"
                "-DPROFILE=${CMAKE_CURRENT_BINARY_DIR}/${name}_d3d11-browser"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_web_test.cmake")
        set_tests_properties(${name}_d3d11 PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:"
            PROCESSORS ${TIDE_WEB_TEST_PROCESSORS} LABELS webgl_d3d11 TIMEOUT 150)
    endif()
    if(ARG_WEBGPU)
        set(expect "")
        if(ARG_EXPECT)
            set(expect --expect "${ARG_EXPECT}")
        endif()
        if(NOT ARG_SECONDS)
            set(ARG_SECONDS 100)
        endif()
        math(EXPR guard "${ARG_SECONDS} + 50")
        add_test(NAME ${name}_webgpu
            COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_web_served_test.mjs" "${TIDE_BROWSER}" "${page}"
                "${CMAKE_CURRENT_BINARY_DIR}/${name}_webgpu-browser" "${webgpu_query}" ${expect} --seconds ${ARG_SECONDS})
        # The runner gives up after its seconds by itself, and then asks the
        # page where it stands, which can take 30 more: this is the guard around it.
        set_tests_properties(${name}_webgpu PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" LABELS webgpu TIMEOUT ${guard}
            PROCESSORS ${TIDE_WEB_TEST_PROCESSORS})
    endif()
endfunction()
