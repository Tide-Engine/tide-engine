# Runs `tidec --hlsl` on SOURCE (or, with TOOL, `tide_spirv_shaders --hlsl`
# for the renderer's own shaders), which writes each shader's HLSL, Unity's,
# into WORK, and has FXC (the Windows SDK's) compile every one's vertex and
# fragment stages, failing at a warning, which Unity would show: as
# Direct3D's, OpenGL's (each texture's own sampler) and OpenGL ES 3.0's and
# WebGL 2's (buffers as textures), as Unity compiles the HLSL for each with fxc
# first. A folder named as SOURCE is without its extension holds its packages,
# as for expect_schedule.cmake.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
if(TOOL)
    execute_process(COMMAND "${TOOL}" --hlsl "${WORK}" RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
else()
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
    execute_process(COMMAND "${TIDEC}" "${SOURCE}" ${package_args} --hlsl "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "--hlsl failed:\n${stderr}")
endif()
file(GLOB programs "${WORK}/*.hlsl")
if(NOT programs)
    message(FATAL_ERROR "--hlsl wrote no HLSL")
endif()
# Each through the warnings Unity's shaders leave out (com.tide.unity's TideWarnings.hlsl, which they include)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(program IN LISTS programs)
    file(WRITE "${program}.unity.hlsl" "#include \"${repository}/unity/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl\"\n#include \"${program}\"\n")
    foreach(api SHADER_API_D3D11 SHADER_API_GLCORE SHADER_API_GLES3)
        foreach(stage "vs_5_0;tidehl_vertex" "ps_5_0;tidehl_fragment")
            list(GET stage 0 profile)
            list(GET stage 1 entry)
            execute_process(
                COMMAND "${FXC}" /nologo /WX /T ${profile} /E ${entry} /D ${api}=1 /Fo "${program}.${api}.${profile}.dxbc" "${program}.unity.hlsl"
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
            if(NOT result EQUAL 0)
                get_filename_component(file "${program}" NAME)
                string(REGEX MATCH "${file}: [^\n]*" which "${stdout}")
                message(FATAL_ERROR "fxc refuses ${file} (${which}) as ${profile} for ${api}:\n${out}${err}")
            endif()
        endforeach()
    endforeach()
endforeach()
