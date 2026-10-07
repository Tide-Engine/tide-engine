# Each invocation is another process, as players joining a match are.
file(MAKE_DIRECTORY "${OUT}")
if(EMULATOR MATCHES "run_android\\.mjs")
    # Each device process has its own folder. Carry this one file through the
    # host's test folder so the next process can read what the writer saved.
    list(APPEND EMULATOR --fixture state.bin)
endif()
foreach(mode write read delta)
    execute_process(COMMAND ${EMULATOR} "${PROGRAM}" "${mode}" state.bin
        WORKING_DIRECTORY "${OUT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 35)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Box3D fresh-process ${mode} failed (${result}):\n${stdout}\n${stderr}")
    endif()
    message(STATUS "${stdout}")
endforeach()
