# One WGSL fixture (compiler/tests/wgsl/*.wgsl) through tidec's translators by
# TOOL (tide_wgsl_translate), into WORK:
#
# - MODE spv: SPIR-V, which SPIRV_VAL (the Vulkan SDK's spirv-val) checks for
#   Vulkan 1.3's rules.
# - MODE hlsl: HLSL, Unity's, which FXC (the Windows SDK's) compiles, failing at
#   a warning, as Direct3D's, OpenGL's and OpenGL ES 3.0's (the defines Unity's
#   platforms have), through the warnings Unity's shaders leave out
#   (com.tide.unity's TideWarnings.hlsl): a compute shader's kernel as cs_5_0,
#   a vertex and fragment stage as vs_5_0 and ps_5_0.
# - MODE refuse: both translators refuse it, saying what its first line's
#   comment, `// refused: <text>`, says.
get_filename_component(name "${SOURCE}" NAME_WE)
file(MAKE_DIRECTORY "${WORK}")
if(MODE STREQUAL "refuse")
    file(STRINGS "${SOURCE}" first LIMIT_COUNT 1)
    string(REGEX REPLACE "^// refused: " "" text "${first}")
    execute_process(COMMAND "${TOOL}" --refuse "${SOURCE}" "${text}" RESULT_VARIABLE result ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${err}")
    endif()
    return()
endif()
if(MODE STREQUAL "spv")
    set(module "${WORK}/${name}.spv")
    execute_process(COMMAND "${TOOL}" --spv "${SOURCE}" "${module}" RESULT_VARIABLE result ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${err}")
    endif()
    execute_process(COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${module}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "spirv-val refuses ${name}.spv:\n${out}${err}")
    endif()
    return()
endif()
set(program "${WORK}/${name}.hlsl")
execute_process(COMMAND "${TOOL}" --hlsl "${SOURCE}" "${program}" RESULT_VARIABLE result ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${err}")
endif()
file(READ "${SOURCE}" wgsl)
set(stages)
if(wgsl MATCHES "@compute")
    list(APPEND stages "cs_5_0;tidehl_compute")
    file(READ "${program}.compute" unity)
    if(NOT unity MATCHES "#pragma kernel tidehl_compute\n")
        message(FATAL_ERROR "${name}.hlsl.compute has no kernel")
    endif()
endif()
if(wgsl MATCHES "@vertex")
    list(APPEND stages "vs_5_0;tidehl_vertex" "ps_5_0;tidehl_fragment")
endif()
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(WRITE "${program}.unity.hlsl" "#include \"${repository}/unity/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl\"\n#include \"${program}\"\n")
foreach(api SHADER_API_D3D11) # Every API Tide draws on in Unity compiles alike (hlsl.c: reversed depth)
    set(index 0) # Pairs: a profile, then its entry point
    list(LENGTH stages count)
    while(index LESS count)
        list(GET stages ${index} profile)
        math(EXPR index "${index} + 1")
        list(GET stages ${index} entry)
        math(EXPR index "${index} + 1")
        execute_process(
            COMMAND "${FXC}" /nologo /WX /T ${profile} /E ${entry} /D ${api}=1 /Fo "${program}.${api}.${profile}.dxbc" "${program}.unity.hlsl"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "fxc refuses ${name}.hlsl as ${profile} for ${api}:\n${out}${err}")
        endif()
    endwhile()
endforeach()
