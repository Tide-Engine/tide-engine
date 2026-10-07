# Runs tidec on SOURCE, which has to compile, and checks its warnings: as many
# as the first line says, `// warnings: <count>`, and each text written after
# it as `// expect: <text>`, a line each, somewhere in what tidec says (a
# warning's own text, or its note's).
# A folder named as SOURCE is without its extension holds the packages the
# program builds with, a folder each, named as the package is.
# The file is read as one string: file(STRINGS) would split a line on ';'.
file(READ "${SOURCE}" content)
string(REPLACE "\r" "" content "${content}")
if(NOT content MATCHES "^// warnings: ([0-9]+)\n")
    message(FATAL_ERROR "${SOURCE}: the first line must be `// warnings: <count>`")
endif()
set(count "${CMAKE_MATCH_1}")

string(REGEX REPLACE "\\.tide$" "" PACKAGES "${SOURCE}")
set(package_args "")
if(IS_DIRECTORY "${PACKAGES}")
    file(GLOB folders LIST_DIRECTORIES true "${PACKAGES}/*")
    foreach(folder IN LISTS folders)
        if(IS_DIRECTORY "${folder}")
            get_filename_component(package "${folder}" NAME)
            file(GLOB package_files "${folder}/*.tide")
            list(APPEND package_args --package "${package}" ${package_files})
        endif()
    endforeach()
endif()
file(MAKE_DIRECTORY "${OUT}")
execute_process(
    COMMAND "${TIDEC}" "${SOURCE}" ${package_args} -o "${OUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "expected tidec to compile ${SOURCE}\ntidec said:\n${stderr}")
endif()

# How many warnings: the pieces ": warning: " splits the output into, less one
string(REPLACE ";" "," counted "tidec said:${stderr}")
string(REPLACE ": warning: " ";" counted "${counted}")
list(LENGTH counted pieces)
math(EXPR said "${pieces} - 1")
if(NOT said EQUAL count)
    message(FATAL_ERROR "expected ${count} warnings, and tidec gave ${said}:\n${stderr}")
endif()

set(rest "${content}")
while(rest MATCHES "^// [a-z]+: ([^\n]*)\n(.*)$")
    set(line "${CMAKE_MATCH_0}")
    set(rest "${CMAKE_MATCH_2}")
    if(NOT line MATCHES "^// expect: ([^\n]*)\n")
        continue()
    endif()
    set(expected "${CMAKE_MATCH_1}")
    string(FIND "${stderr}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "expected tidec to say: ${expected}\ntidec said:\n${stderr}")
    endif()
endwhile()
