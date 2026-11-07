# Build code's sources in a game built without what nothing reads (docs/spec.md, Build code): an asset only build
# code reads, e2e/build_code.tide's heroSource, isn't in the game, and one match code reads too, beepSource, is.
# TIDEC, SOURCE, OUT, BUILD_CC, BUILD_INCLUDE and BUILD_ENGINE as expect_error.cmake takes them.
file(MAKE_DIRECTORY "${OUT}")
execute_process(
    COMMAND "${TIDEC}" "${SOURCE}" -o "${OUT}" --name game --strip-unused-assets
        --build-cc "${BUILD_CC}" --build-include "${BUILD_INCLUDE}" --build-engine "${BUILD_ENGINE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tidec failed:\n${stdout}${stderr}")
endif()
file(READ "${OUT}/game.c" game)
string(FIND "${game}" "\"heroSource\"" source)
if(NOT source EQUAL -1)
    message(FATAL_ERROR "the game loads heroSource, which only build code reads")
endif()
foreach(kept IN ITEMS "\"beepSource\"" "\"hero\"")
    string(FIND "${game}" "${kept}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "the game doesn't load ${kept}")
    endif()
endforeach()
