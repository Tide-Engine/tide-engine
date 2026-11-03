# Opens a web build's page in a headless browser and fails unless the page exits
# with 0. See WebTest.cmake.
#
# cmake -DNODE=<node> -DBROWSER=<Chrome or Edge> -DPAGE=<page.html> [-DQUERY=<query>] -DPROFILE=<scratch dir>
#       [-DANGLE_BACKEND=swiftshader|d3d11-warp]
#       -P run_web_test.cmake

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
    TIMEOUT 120)
if(browser_result MATCHES "timeout")
    message("The browser didn't exit within 120 seconds, and was stopped.")
endif()

string(REGEX MATCH "<pre id=\"log\">([^<]*)</pre>" _ "${dom}")
message("${CMAKE_MATCH_1}")

string(REGEX MATCH "data-exit=\"([^\"]*)\"" _ "${dom}")
if(NOT CMAKE_MATCH_1 STREQUAL "0")
    set(page_result "${CMAKE_MATCH_1}")
    string(LENGTH "${dom}" dom_length)
    string(SUBSTRING "${dom}" 0 4096 dom_excerpt)
    if(dom_length GREATER 4096)
        string(APPEND dom_excerpt "\n[remaining DOM omitted]")
    elseif(dom_length EQUAL 0)
        set(dom_excerpt "<empty>")
    endif()
    message(FATAL_ERROR "The page didn't exit with 0 (got '${page_result}').\nBrowser runner result: ${browser_result}.\nDOM (${dom_length} bytes, at most the first 4096 shown):\n${dom_excerpt}\n${browser_errors}")
endif()
