# Opens a web build's page in a headless browser and fails unless the page exits
# with 0. See WebTest.cmake.
#
# cmake -DNODE=<node> -DBROWSER=<Chrome or Edge> -DPAGE=<page.html> [-DQUERY=<query>] -DPROFILE=<scratch dir>
#       [-DANGLE_BACKEND=swiftshader|d3d11-warp]
#       [-DTIDE_WEB_TEST_TIMEOUT_SECONDS=<integer 1..600, default 120>]
#       -P run_web_test.cmake

if(NOT DEFINED TIDE_WEB_TEST_TIMEOUT_SECONDS)
    set(TIDE_WEB_TEST_TIMEOUT_SECONDS 120)
endif()
if(NOT "${TIDE_WEB_TEST_TIMEOUT_SECONDS}" MATCHES "^[0-9]+$"
        OR TIDE_WEB_TEST_TIMEOUT_SECONDS LESS 1
        OR TIDE_WEB_TEST_TIMEOUT_SECONDS GREATER 600)
    message(FATAL_ERROR "TIDE_WEB_TEST_TIMEOUT_SECONDS must be an integer from 1 to 600 (got '${TIDE_WEB_TEST_TIMEOUT_SECONDS}').")
endif()

if(NOT EXISTS "${BROWSER}")
    message("SKIPPED: no Chrome or Edge found. Set TIDE_BROWSER to run web tests.")
    return()
endif()

if(PAGE MATCHES "^/")
    set(url "file://${PAGE}")
else()
    set(url "file:///${PAGE}")
endif()
if(QUERY)
    string(APPEND url "?${QUERY}")
endif()

if(NOT ANGLE_BACKEND)
    set(ANGLE_BACKEND swiftshader)
endif()
set(angle_flags "--use-angle=${ANGLE_BACKEND}")
if(ANGLE_BACKEND STREQUAL "swiftshader")
    list(APPEND angle_flags --enable-unsafe-swiftshader)
endif()

file(REMOVE_RECURSE "${PROFILE}")
# Through browser_dump.mjs, which stops the browser once the page is out.
# --mute-audio: headless, it still plays through the machine's device once a
# page's AudioContext runs.
execute_process(
    COMMAND "${NODE}" "${CMAKE_CURRENT_LIST_DIR}/browser_dump.mjs" "${BROWSER}"
        --headless
        --no-first-run
        --no-default-browser-check
        --disable-extensions
        --mute-audio
        "--user-data-dir=${PROFILE}"
        ${angle_flags}
        --virtual-time-budget=60000
        --dump-dom "${url}"
    OUTPUT_VARIABLE dom
    ERROR_VARIABLE browser_errors
    RESULT_VARIABLE browser_result
    TIMEOUT ${TIDE_WEB_TEST_TIMEOUT_SECONDS})
if(browser_result MATCHES "timeout")
    message("The browser didn't exit within ${TIDE_WEB_TEST_TIMEOUT_SECONDS} seconds, and was stopped.")
endif()

string(REGEX MATCH "<pre id=\"log\">([^<]*)</pre>" _ "${dom}")
message("${CMAKE_MATCH_1}")

string(REGEX MATCH "data-exit=\"([^\"]*)\"" _ "${dom}")
if(NOT CMAKE_MATCH_1 STREQUAL "0")
    message(FATAL_ERROR "The page didn't exit with 0 (got '${CMAKE_MATCH_1}').\n${browser_errors}")
endif()
