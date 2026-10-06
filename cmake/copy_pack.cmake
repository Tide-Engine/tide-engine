# Puts a game's pack of assets beside its program (tide_add_game): FROM copied
# to TO when there is one, and TO removed when there isn't, so a game whose
# assets went has none left over.
#
# cmake -DFROM=<pack> -DTO=<pack beside the program> -P copy_pack.cmake

if(EXISTS "${FROM}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FROM}" "${TO}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "can't copy ${FROM} to ${TO}")
    endif()
elseif(EXISTS "${TO}")
    file(REMOVE "${TO}")
endif()
