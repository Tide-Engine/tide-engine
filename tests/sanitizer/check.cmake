execute_process(COMMAND "${PROBE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT "${result}" STREQUAL "86" OR NOT "${stdout}${stderr}" MATCHES "ERROR: AddressSanitizer: heap-use-after-free")
    message(FATAL_ERROR "ASan did not diagnose the deliberate use-after-free (exit ${result}):\n${stdout}${stderr}")
endif()

# Expected compiler failures must not swallow a sanitizer finding emitted
# after the expected diagnostic. The same probe stands in for that compiler.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${PROBE}"
    "-DSOURCE=${CMAKE_CURRENT_LIST_DIR}/compiler_error.tide"
    "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/sanitizer-error"
    -P "${CMAKE_CURRENT_LIST_DIR}/../../compiler/tests/expect_error.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if("${result}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "a sanitizer's report:")
    message(FATAL_ERROR "The compiler diagnostic harness did not reject ASan's report:\n${stdout}${stderr}")
endif()
