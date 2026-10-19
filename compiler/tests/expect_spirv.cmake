# Runs `tidec --spirv` on SOURCE, which writes each of its shaders as Vulkan's
# SPIR-V into WORK, and has SPIRV_VAL (the Vulkan SDK's spirv-val) check
# every one of them for Vulkan 1.1's rules, which SPIR-V 1.3 is. A folder
# named as SOURCE is without its extension holds its packages, as for
# expect_schedule.cmake.
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
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(
    COMMAND "${TIDEC}" "${SOURCE}" ${package_args} --spirv "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tidec --spirv failed:\n${stderr}")
endif()
file(GLOB modules "${WORK}/*.spv")
if(NOT modules)
    message(FATAL_ERROR "tidec --spirv wrote no SPIR-V for ${SOURCE}")
endif()
foreach(module IN LISTS modules)
    execute_process(
        COMMAND "${SPIRV_VAL}" --target-env vulkan1.1 "${module}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        get_filename_component(file "${module}" NAME)
        string(REGEX MATCH "${file}: [^\n]*" which "${stdout}")
        message(FATAL_ERROR "spirv-val refuses ${which}:\n${out}${err}")
    endif()
endforeach()
