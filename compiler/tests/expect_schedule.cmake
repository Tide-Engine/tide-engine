# Runs `tidec --schedule` on SOURCE (or with OPTION in its place, like
# --shaders) and checks that it prints EXPECTED exactly.
if(NOT DEFINED OPTION)
    set(OPTION --schedule)
endif()
execute_process(
    COMMAND "${TIDEC}" "${SOURCE}" ${OPTION}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tidec ${OPTION} failed:\n${stderr}")
endif()
file(READ "${EXPECTED}" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" stdout "${stdout}")
if(NOT stdout STREQUAL expected)
    message(FATAL_ERROR "what tidec ${OPTION} prints changed. Expected (${EXPECTED}):\n${expected}\nGot:\n${stdout}")
endif()
