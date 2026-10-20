# versus_draw: tide_platform_draw --bench's workloads written by hand in
# OpenGL 3.3 core and Vulkan 1.3, timed at the same boundaries, as a baseline
# for Tide's renderer (bench/README.md, Drawing). Desktop only: it draws
# through GLFW's context and Vulkan loader, as the platform layer does.
# Its Vulkan shaders are WGSL, which tidec's translator makes SPIR-V of as the
# program starts, as tide_platform_draw's own are.
add_executable(versus_draw versus_draw.c draw_gl.c draw_vk.c "${PROJECT_SOURCE_DIR}/compiler/src/spirv.c")
target_include_directories(versus_draw PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src"
    "${PROJECT_SOURCE_DIR}/platform/third_party") # Vulkan's headers
target_link_libraries(versus_draw PRIVATE tide glfw)
target_compile_definitions(versus_draw PRIVATE TIDE_BENCH_RESULTS="${PROJECT_SOURCE_DIR}/bench/results")

# A few frames of each workload in each API, checking the pixel read back;
# Vulkan's under the validation layer where there's one (tide_gpu_test).
add_test(NAME versus_draw COMMAND versus_draw --quick)
add_test(NAME versus_draw_opengl COMMAND versus_draw --quick)
tide_gpu_test(versus_draw)
tide_gpu_test(versus_draw_opengl OPENGL)
set_property(TEST versus_draw APPEND PROPERTY ENVIRONMENT TIDE_BACKEND=vulkan)
