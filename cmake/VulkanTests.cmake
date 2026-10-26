# What the tests check Vulkan with: the Vulkan SDK's spirv-val, which checks
# the SPIR-V tidec writes, and its validation layer, which checks every call
# a GPU test makes, where there's an SDK (VULKAN_SDK, as its installer sets
# it, or TIDE_VULKAN_SDK). Neither is needed to build or to run the tests:
# without them, the SPIR-V isn't checked by spirv-val and the GPU tests draw
# with Vulkan unchecked.
set(TIDE_VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE PATH "The Vulkan SDK, whose spirv-val and validation layer tests check with")
if(TIDE_VULKAN_SDK)
    find_program(TIDE_SPIRV_VAL spirv-val HINTS "${TIDE_VULKAN_SDK}/Bin" "${TIDE_VULKAN_SDK}/bin" NO_DEFAULT_PATH)
    find_path(TIDE_VULKAN_LAYERS VkLayer_khronos_validation.json
        HINTS "${TIDE_VULKAN_SDK}/Bin" "${TIDE_VULKAN_SDK}/share/vulkan/explicit_layer.d"
              "${TIDE_VULKAN_SDK}/etc/vulkan/explicit_layer.d" NO_DEFAULT_PATH)
endif()
find_program(TIDE_SPIRV_VAL spirv-val) # One the system has, where the SDK isn't said
find_path(TIDE_VULKAN_LAYERS VkLayer_khronos_validation.json
    PATHS /usr/share/vulkan/explicit_layer.d /usr/local/share/vulkan/explicit_layer.d NO_DEFAULT_PATH)

# A GPU test (label gpu) draws with what the machine has, Vulkan first. Its
# implicit layers (overlays, capture tools) stay out of it, and the
# validation layer, where there's one, stops it at the first thing it finds
# wrong (TIDE_VULKAN_VALIDATION, src/gpu_vk.h).
set(TIDE_GPU_TEST_ENVIRONMENT "VK_LOADER_LAYERS_DISABLE=~implicit~")
if(TIDE_VULKAN_LAYERS)
    list(APPEND TIDE_GPU_TEST_ENVIRONMENT TIDE_VULKAN_VALIDATION=1 "VK_ADD_LAYER_PATH=${TIDE_VULKAN_LAYERS}")
elseif(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    message(STATUS "No Vulkan validation layer (set VULKAN_SDK or TIDE_VULKAN_SDK): GPU tests draw with Vulkan unchecked")
endif()

# Labels the tests `gpu` and gives them that environment: `OPENGL` has them draw with OpenGL instead.
function(tide_gpu_test)
    cmake_parse_arguments(ARG "OPENGL" "" "" ${ARGN})
    foreach(name IN LISTS ARG_UNPARSED_ARGUMENTS)
        set_property(TEST ${name} APPEND PROPERTY LABELS gpu)
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT ${TIDE_GPU_TEST_ENVIRONMENT})
        if(ARG_OPENGL)
            set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT TIDE_BACKEND=opengl)
        endif()
    endforeach()
endfunction()
