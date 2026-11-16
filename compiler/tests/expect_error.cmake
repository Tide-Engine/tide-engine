# Runs tidec on SOURCE and checks that it fails with the message written as
# `// expect: <text>` on the first line. SOURCE is a .tide file, or a folder of
# them compiled together as one program (the expectation is on the first line
# of one of its files). `// expect: <text>` lines right after the first are
# checked too, somewhere in what tidec says: the error's note, say. A
# `// define: <S>` line among them builds with `--define <S>` (S or S=value),
# and a `// build: dev` line builds a dev build (`--dev`).
# The folders in a folder are packages the program builds with, each named as
# its package is: errors/<name>/Mod/*.tide are package Mod's files.
# Files are read as one string: file(STRINGS) would split the line on ';'. The
# leading [^/]* skips a byte-order mark.
set(PACKAGES "")
if(IS_DIRECTORY "${SOURCE}")
    file(GLOB files "${SOURCE}/*.tide")
    set(PACKAGES "${SOURCE}")
else()
    set(files "${SOURCE}")
endif()
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

set(expected "")
set(rest "")
foreach(file IN LISTS files)
    file(READ "${file}" content)
    if(content MATCHES "^[^/]*// expect: ([^\r\n]+)")
        set(expected "${CMAKE_MATCH_1}")
        string(REPLACE "\r" "" content "${content}")
        if(content MATCHES "^[^\n]*\n(.*)$")
            set(rest "${CMAKE_MATCH_1}")
        endif()
        break()
    endif()
endforeach()
if(expected STREQUAL "")
    message(FATAL_ERROR "${SOURCE}: the first line must be `// expect: <message>`")
endif()

# BUILD_CC, BUILD_INCLUDE and BUILD_ENGINE: the compiler and engine build code runs with, for errors only running
# it finds (build_errors/)
set(build_args "")
if(BUILD_CC)
    set(build_args --build-cc "${BUILD_CC}" --build-include "${BUILD_INCLUDE}" --build-engine "${BUILD_ENGINE}")
endif()
set(define_args "")
set(lines "${rest}")
while(lines MATCHES "^// (expect|define|build): ([^\n]*)\n(.*)$")
    set(lines "${CMAKE_MATCH_3}")
    if(CMAKE_MATCH_1 STREQUAL "define")
        list(APPEND define_args --define "${CMAKE_MATCH_2}")
    elseif(CMAKE_MATCH_1 STREQUAL "build" AND CMAKE_MATCH_2 STREQUAL "dev") # `// build: dev`: a dev build (--dev)
        list(APPEND define_args --dev)
    endif()
endwhile()
file(MAKE_DIRECTORY "${OUT}")
execute_process(
    COMMAND "${TIDEC}" ${files} ${package_args} -o "${OUT}" ${build_args} ${define_args}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

# A compiler error printed before memory corruption must not make an ASan or
# UBSan failure look like the expected diagnostic.
if("${stdout}${stderr}" MATCHES "AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:")
    message(FATAL_ERROR "a sanitizer's report:\n${stdout}${stderr}")
endif()

if(result EQUAL 0)
    message(FATAL_ERROR "expected tidec to fail with: ${expected}")
endif()

string(FIND "${stderr}" "${expected}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "expected an error containing: ${expected}\ntidec said:\n${stderr}")
endif()

while(rest MATCHES "^// (expect|define|build): ([^\n]*)\n(.*)$")
    set(kind "${CMAKE_MATCH_1}")
    set(expected "${CMAKE_MATCH_2}")
    set(rest "${CMAKE_MATCH_3}")
    if(NOT kind STREQUAL "expect")
        continue()
    endif()
    string(FIND "${stderr}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "expected tidec to say: ${expected}\ntidec said:\n${stderr}")
    endif()
endwhile()
