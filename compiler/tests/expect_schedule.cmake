# Runs `tidec --schedule` on SOURCE (or with OPTION in its place, like
# --shaders) and checks that it prints EXPECTED exactly. A folder named as
# SOURCE is without its extension holds the packages the program builds with,
# a folder each, named as the package is: schedule/overrides/Mod/*.tide are
# package Mod's files, for schedule/overrides.tide. With RELATIVE, it runs in
# SOURCE's folder and names the files from there, for output that prints paths.
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
get_filename_component(folder "${SOURCE}" DIRECTORY)
set(source "${SOURCE}")
if(RELATIVE)
    file(RELATIVE_PATH source "${folder}" "${SOURCE}")
    set(relative_args "")
    foreach(argument IN LISTS package_args)
        if(IS_ABSOLUTE "${argument}")
            file(RELATIVE_PATH argument "${folder}" "${argument}")
        endif()
        list(APPEND relative_args "${argument}")
    endforeach()
    set(package_args ${relative_args})
endif()
execute_process(
    COMMAND "${TIDEC}" "${source}" ${package_args} ${OPTION}
    WORKING_DIRECTORY "${folder}"
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
