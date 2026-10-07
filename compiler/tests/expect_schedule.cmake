# Runs `tidec --schedule` on SOURCE (or with OPTION in its place, like
# --shaders) and checks that it prints EXPECTED exactly. A folder named as
# SOURCE is without its extension holds the packages the program builds with,
# a folder each, named as the package is: schedule/overrides/Mod/*.tide are
# package Mod's files, for schedule/overrides.tide.
if(NOT DEFINED OPTION)
    set(OPTION --schedule)
endif()
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
execute_process(
    COMMAND "${TIDEC}" "${SOURCE}" ${package_args} ${OPTION}
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
