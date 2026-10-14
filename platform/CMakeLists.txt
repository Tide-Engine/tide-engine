# The platform layer: a window, the frame loop, input devices and the renderer,
# written against src/gpu.h, with Vulkan (src/gpu_vk.c, on desktop and Android), OpenGL
# (src/gpu_gl.c) and WebGPU (web/gpu_web.c) under it. Hosts and views link it;
# the simulation never does (see AGENTS.md).
file(GLOB TIDE_PLATFORM_SOURCES CONFIGURE_DEPENDS src/*.c)
if(TIDE_WEB)
    list(REMOVE_ITEM TIDE_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu_vk.c") # The web has WebGPU
else()
    # Vulkan's SPIR-V of the renderer's own shaders, from their WGSL (tools/spirv_shaders.c)
    set(TIDE_SPIRV_C "${CMAKE_CURRENT_BINARY_DIR}/shaders_spirv.c")
    add_custom_command(OUTPUT "${TIDE_SPIRV_C}"
        COMMAND "${TIDE_SPIRV_SHADERS_PATH}" "${TIDE_SPIRV_C}"
        DEPENDS ${TIDE_SPIRV_SHADERS_DEPENDS} src/shaders.h
        COMMENT "The renderer's shaders as SPIR-V"
        VERBATIM)
    list(APPEND TIDE_PLATFORM_SOURCES "${TIDE_SPIRV_C}")
    # The renderer's shaders' SPIR-V passes spirv-val, where there's one (cmake/VulkanTests.cmake)
    if(TIDE_SPIRV_VAL)
        add_test(NAME tide_spirv_shaders_valid
            COMMAND "${CMAKE_COMMAND}" "-DTOOL=${TIDE_SPIRV_SHADERS_PATH}" "-DSPIRV_VAL=${TIDE_SPIRV_VAL}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/spirv_shaders" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/spirv_shaders.cmake")
    endif()
    # The prebuilt library is compiled with Khronos's headers (third_party/vulkan), under Apache 2.0
    install(FILES third_party/vulkan/LICENSE.md third_party/vulkan/Apache-2.0.txt DESTINATION licenses/vulkan-headers
        COMPONENT tide)
endif()
if(NOT TIDE_WEB)
    # WebRTC for rooms on desktop (src/rtc/rtc.h); the web has the browser's.
    file(GLOB TIDE_RTC_SOURCES CONFIGURE_DEPENDS src/rtc/*.c)
    list(APPEND TIDE_PLATFORM_SOURCES ${TIDE_RTC_SOURCES})
endif()
# The window (src/window.h): the page's canvas, Android's activity, or GLFW's.
# ...and the audio device (src/audio_out.h): the page's worklet, AAudio, or WASAPI, CoreAudio and ALSA.
if(TIDE_WEB)
    list(APPEND TIDE_PLATFORM_SOURCES web/canvas.c web/gpu_web.c web/audio.c)
elseif(TIDE_ANDROID)
    list(APPEND TIDE_PLATFORM_SOURCES android/activity.c android/audio.c)
else()
    include("${PROJECT_SOURCE_DIR}/cmake/GLFW.cmake")
    list(APPEND TIDE_PLATFORM_SOURCES desktop/glfw.c desktop/gl.c desktop/audio.c)
endif()

# The font text is drawn in (src/font.c), inside the library, as arrays: a
# file a weight, each an object of its own with nothing else in it. The
# regular one, src/font.c names. The bold one, nothing in the library does:
# the linker only takes its object out of the library for a program whose own
# code names tide_font_bold_ttf (tide/draw.h), which a game's does when it
# says Font.Bold. So the library stays a static one, of objects linkers pick
# from, and no source file of it may name the bold's array.
set(TIDE_FONT_C "${CMAKE_CURRENT_BINARY_DIR}/tide_font.c")
set(TIDE_FONT_BOLD_C "${CMAKE_CURRENT_BINARY_DIR}/tide_font_bold.c")
add_custom_command(OUTPUT "${TIDE_FONT_C}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/font/Tuffy.ttf" "-DOUTPUT=${TIDE_FONT_C}"
        -DNAME=tide_font_ttf -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS font/Tuffy.ttf "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "The platform's font as C"
    VERBATIM)
add_custom_command(OUTPUT "${TIDE_FONT_BOLD_C}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/font/Tuffy_Bold.ttf" "-DOUTPUT=${TIDE_FONT_BOLD_C}"
        -DNAME=tide_font_bold_ttf -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS font/Tuffy_Bold.ttf "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "The platform's bold font as C"
    VERBATIM)

add_library(tide_platform STATIC ${TIDE_PLATFORM_SOURCES} "${TIDE_FONT_C}" "${TIDE_FONT_BOLD_C}")
# Hosts go through include/ only; third_party/ is stb_truetype, for the font.
target_include_directories(tide_platform PUBLIC include PRIVATE src third_party)
target_link_libraries(tide_platform PUBLIC tide)
if(TIDE_ANDROID)
    target_link_libraries(tide_platform PUBLIC android log EGL GLESv3 aaudio) # ...and the audio device
elseif(NOT TIDE_WEB)
    target_link_libraries(tide_platform PRIVATE glfw)
endif()
if(WIN32)
    target_link_libraries(tide_platform PUBLIC ws2_32 secur32 ole32) # UDP, for sessions; TLS, for the relay; COM, for WASAPI
elseif(APPLE)
    target_link_libraries(tide_platform PUBLIC "-framework Security" "-framework AudioToolbox" "-framework CoreAudio") # TLS, for the relay; the audio device, and whether there's one
endif()
if(NOT WIN32 AND NOT TIDE_WEB AND NOT TIDE_ANDROID) # Android's are in its C library
    # Threads for ticks (src/jobs.c)
    find_package(Threads REQUIRED)
    target_link_libraries(tide_platform PUBLIC Threads::Threads)
endif()
# Hot reloading loads the game's library (src/reload.c).
target_link_libraries(tide_platform PUBLIC ${CMAKE_DL_LIBS})

if(TIDE_WEB)
    # The page's JavaScript (web/tide.js) provides the canvas, input and frames
    # (web/tide_web.h), and implements the GL functions the renderer imports
    # by name (src/gl.h).
    target_include_directories(tide_platform PUBLIC web)
    target_link_options(tide_platform INTERFACE -Wl,--allow-undefined)
endif()

# tide_android_app(<target> [LABEL <name>])
#
# Android builds: makes the shared library <target> an app, <target>.apk next
# to it, which runs the program's main() in Android's NativeActivity
# (platform/android/activity.c). Its ID is dev.tide.<target>,
# and it's signed with a key every build of this repo shares
# (build/android-debug.pem, made the first time), so a new build installs
# over the last. Does nothing in other builds, so it can be called
# unconditionally.
set(TIDE_ANDROID_KEY "${PROJECT_SOURCE_DIR}/build/android-debug.pem" CACHE INTERNAL "") # For every folder's apps
if(TIDE_ANDROID)
    # Made once, before any app, so apps built at once share it.
    add_custom_target(tide_android_key COMMAND "${TIDE_APK_PATH}" --key "${TIDE_ANDROID_KEY}" VERBATIM)
    add_dependencies(tide_android_key tidec_host)
endif()
function(tide_android_app target)
    if(NOT TIDE_ANDROID)
        return()
    endif()
    # PACK: the game's pack of assets (tide_add_game), which goes in the app when there is one
    cmake_parse_arguments(ARG "" "LABEL;PACK" "" ${ARGN})
    set(label "${target}")
    if(ARG_LABEL)
        set(label "${ARG_LABEL}")
    endif()
    set(pack "")
    if(ARG_PACK)
        set(pack "--pack-if-any" "${ARG_PACK}")
    endif()
    # The activity's way in is in the platform layer, which nothing in the
    # program calls.
    target_link_options(${target} PRIVATE -Wl,-u,ANativeActivity_onCreate)
    string(REPLACE "-" "_" id "${target}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${TIDE_APK_PATH}" "$<TARGET_FILE_DIR:${target}>/${target}.apk" --package "dev.tide.${id}"
            --label "${label}" --lib "${target}" --key "${TIDE_ANDROID_KEY}" --min ${TIDE_ANDROID_API} --debuggable
            --icon "${PROJECT_SOURCE_DIR}/tools/tide-vscode/icon.png" ${pack}
            "${TIDE_ANDROID_ABI}=$<TARGET_FILE:${target}>"
        COMMENT "Android app ${target}.apk"
        VERBATIM)
    add_dependencies(${target} tide_android_key)
endfunction()

# In the package, the platform layer is prebuilt: tide links games with it,
# native, web or Android. It's one library, GLFW inside on desktop
# (cmake/GLFW.cmake).
install(DIRECTORY include/tide DESTINATION include COMPONENT tide)
install(FILES web/shell.html DESTINATION web COMPONENT tide)
if(TIDE_NODE)
    add_test(NAME tide_web_runtime COMMAND "${TIDE_NODE}" --test "${PROJECT_SOURCE_DIR}/cmake/web_runtime.test.mjs")
endif()
if(TIDE_WEB)
    set(TIDE_PACKAGE_LIBS lib/web)
    install(FILES web/tide.js web/tide.release.js DESTINATION web COMPONENT tide)
    install(FILES third_party/fflate/LICENSE DESTINATION licenses/fflate COMPONENT tide)
    # The C library tide links web games with, from cmake/wasi-toolchain.cmake:
    # only what its target's C programs use (threads are in libc.a). Games are
    # C, so not the C++ headers.
    install(DIRECTORY "${TIDE_WASI_SYSROOT}/include/${TIDE_WASI_TARGET}" DESTINATION wasi/sysroot/include COMPONENT tide
        PATTERN c++ EXCLUDE)
    install(FILES "${TIDE_WASI_SYSROOT}/lib/${TIDE_WASI_TARGET}/crt1-command.o"
        "${TIDE_WASI_SYSROOT}/lib/${TIDE_WASI_TARGET}/libc.a" "${TIDE_WASI_SYSROOT}/lib/${TIDE_WASI_TARGET}/libm.a"
        DESTINATION wasi/sysroot/lib/${TIDE_WASI_TARGET} COMPONENT tide)
    install(FILES "${TIDE_WASI_BUILTINS}" DESTINATION wasi RENAME libclang_rt.builtins.a COMPONENT tide)
elseif(MINGW)
    # tide builds Windows games for MinGW, with this C runtime (see
    # cmake/mingw-toolchain.cmake): every C header, since a game's C can include
    # any of Windows', and only the libraries games link (compiler/cli/build.c).
    # Not the C++ headers (the Windows Runtime's C++ library's too), nor the
    # interface definitions and type libraries, which only MIDL and C++ read.
    set(TIDE_PACKAGE_LIBS lib/native)
    set(mingw_target "${TIDE_MINGW_SYSROOT}/x86_64-w64-mingw32")
    install(DIRECTORY "${mingw_target}/include" DESTINATION mingw/sysroot/x86_64-w64-mingw32 COMPONENT tide
        PATTERN c++ EXCLUDE PATTERN wrl EXCLUDE PATTERN wrl.h EXCLUDE PATTERN "*.hxx" EXCLUDE PATTERN "*.idl" EXCLUDE
        PATTERN "*.tlb" EXCLUDE)
    set(mingw_libs crt2.o dllcrt2.o crtbegin.o crtend.o)
    foreach(lib IN ITEMS mingw32 mingwex moldname msvcrt kernel32 user32 gdi32 shell32 advapi32 ws2_32 secur32 ole32)
        list(APPEND mingw_libs lib${lib}.a)
    endforeach()
    list(TRANSFORM mingw_libs PREPEND "${mingw_target}/lib/")
    install(FILES ${mingw_libs} DESTINATION mingw/sysroot/x86_64-w64-mingw32/lib COMPONENT tide)
    install(FILES "${TIDE_MINGW_BUILTINS}" DESTINATION mingw COMPONENT tide)
elseif(TIDE_ANDROID)
    # tide builds Android games against the NDK the user has (see
    # compiler/cli/android.c), so only the platform layer for each CPU, and
    # the icon apps get when their game has none.
    set(TIDE_PACKAGE_LIBS lib/android/${TIDE_ANDROID_ABI})
    install(FILES "${PROJECT_SOURCE_DIR}/tools/tide-vscode/icon.png" DESTINATION lib/android COMPONENT tide)
elseif(NOT WIN32)
    set(TIDE_PACKAGE_LIBS lib/native)
endif()
# On Windows, only the MinGW build's platform layer goes in the package.
if(TIDE_PACKAGE_LIBS)
    install(FILES $<TARGET_FILE:tide_platform> DESTINATION ${TIDE_PACKAGE_LIBS} COMPONENT tide)
endif()

# Carrying a world over to a build whose data layout changed, for hot
# reloading: on layouts written by hand, and on a game's, as tidec describes it.
tide_add_game(tide_platform_migrate SOURCES tests/migrate.tide NAME game LAYOUT
    HOST tests/migrate.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_migrate PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_migrate PRIVATE tide_platform)
add_test(NAME tide_platform_migrate COMMAND tide_platform_migrate)

# Timers must never wait for the GPU or reuse a query still in flight.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    add_executable(tide_platform_gpu_timing tests/gpu_timing.c)
    target_include_directories(tide_platform_gpu_timing PRIVATE src)
    target_link_libraries(tide_platform_gpu_timing PRIVATE tide_platform)
    add_test(NAME tide_platform_gpu_timing COMMAND tide_platform_gpu_timing)
endif()
if(TIDE_NODE)
    add_test(NAME tide_platform_gpu_timing_web
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu_timing_web.mjs")
    add_test(NAME tide_platform_reload_web
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reload_web.mjs")
endif()
if(NOT TIDE_ANDROID)
    add_executable(tide_platform_gpu_time tests/gpu_time.c)
    target_link_libraries(tide_platform_gpu_time PRIVATE tide_platform)
    target_include_directories(tide_platform_gpu_time PRIVATE third_party) # Vulkan's headers, for src/gpu_vk.h
    if(TIDE_WEB)
        tide_web_page(tide_platform_gpu_time SHELL web/test_shell.html)
        tide_add_web_test(tide_platform_gpu_time TARGET tide_platform_gpu_time WEBGPU)
    else()
        add_test(NAME tide_platform_gpu_time COMMAND tide_platform_gpu_time)
        add_test(NAME tide_platform_gpu_time_opengl COMMAND tide_platform_gpu_time)
        tide_gpu_test(tide_platform_gpu_time)
        tide_gpu_test(tide_platform_gpu_time_opengl OPENGL)
    endif()
endif()

# Ticks on every core, against ticks on one thread
tide_add_game(tide_platform_jobs SOURCES tests/jobs.tide tests/jobs_tasks.c NAME game
    HOST tests/jobs.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_jobs PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_jobs PRIVATE tide_platform)
add_test(NAME tide_platform_jobs COMMAND tide_platform_jobs)
set_tests_properties(tide_platform_jobs PROPERTIES TIMEOUT 120)

# The font's glyphs, fitted to the pixels they're drawn on: no window or GPU.
add_executable(tide_platform_font tests/font.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_font PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
target_link_libraries(tide_platform_font PRIVATE tide_platform)
add_test(NAME tide_platform_font COMMAND tide_platform_font)

# The mixer and the WAV writer, with no device: the same bytes on every platform.
add_executable(tide_platform_audio tests/audio.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_audio PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
target_link_libraries(tide_platform_audio PRIVATE tide_platform tide_audio_refs) # tests/audio: the Vorbis reference files
add_test(NAME tide_platform_audio COMMAND tide_platform_audio)

# The page's microphone lifecycle and worklet bridge, with a fake device:
# no browser permissions or physical microphone are needed.
if(TIDE_NODE)
    add_test(NAME tide_platform_web_audio
        COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_audio.mjs")
endif()

# The audio device, where the machine has one: opened, played through silently, forgotten and closed.
if(NOT TIDE_WEB)
    add_executable(tide_platform_audio_device tests/audio_device.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_audio_device PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
    target_link_libraries(tide_platform_audio_device PRIVATE tide_platform)
    add_test(NAME tide_platform_audio_device COMMAND tide_platform_audio_device)
    set_tests_properties(tide_platform_audio_device PROPERTIES TIMEOUT 60)
endif()

# A game carries the bold weight's file when it says Font.Bold, and only then:
# two games as tide_add_game builds them, read as the build left them. No
# window or GPU, so every platform's tests run it. tide_platform_text is also
# text to look at (tests/text.tide says how).
tide_add_game(tide_platform_text SOURCES tests/text.tide)
tide_add_game(tide_platform_text_plain SOURCES tests/text_plain.tide)
if(TIDE_NODE)
    add_test(NAME tide_platform_font_files
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/font_files.mjs" "${CMAKE_CURRENT_SOURCE_DIR}/font"
            $<TARGET_FILE:tide_platform_text> $<TARGET_FILE:tide_platform_text_plain>)
endif()

# The PNG writer (src/png.c), which captures are written with: pictures made
# up from numbers, read back with Node's zlib. No window or GPU, so every
# platform's tests run it.
add_executable(tide_platform_png tests/png.c)
target_include_directories(tide_platform_png PRIVATE src)
target_link_libraries(tide_platform_png PRIVATE tide_platform)
if(TIDE_NODE)
    add_test(NAME tide_platform_png
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/png.mjs" ${CMAKE_CROSSCOMPILING_EMULATOR}
            $<TARGET_FILE:tide_platform_png>)
endif()

# The engine's PNG reader (engine/src/png_read.c, grid.Decode) against pictures
# Node's zlib writes: every kind of PNG, and files wrong in each way. No GPU.
add_executable(tide_platform_png_decode tests/png_decode.c)
target_link_libraries(tide_platform_png_decode PRIVATE tide)
if(TIDE_NODE)
    add_test(NAME tide_platform_png_decode
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/png_decode.mjs" ${CMAKE_CROSSCOMPILING_EMULATOR}
            $<TARGET_FILE:tide_platform_png_decode>)
endif()

if(NOT TIDE_WEB)
    # Files and directories, with no window or GPU, including Unicode names
    # and recursive operations that do not enter directory symlinks.
    add_executable(tide_platform_io tests/io.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_io PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tide_platform_io PRIVATE tide_platform)
    add_test(NAME tide_platform_io COMMAND tide_platform_io)

    # The WebRTC stack (src/rtc), piece by piece.
    add_executable(tide_platform_rtc tests/rtc.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_rtc PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
    target_link_libraries(tide_platform_rtc PRIVATE tide_platform)
    add_test(NAME tide_platform_rtc COMMAND tide_platform_rtc)
    set_tests_properties(tide_platform_rtc PROPERTIES ENVIRONMENT TIDE_RTC_LOCAL=1) # Sockets on loopback only

    # Everything in it that reads what other machines send, fed real messages
    # broken at random. With TIDE_SANITIZE, AddressSanitizer watches it too.
    add_executable(tide_platform_rtc_fuzz tests/rtc_fuzz.c)
    target_include_directories(tide_platform_rtc_fuzz PRIVATE src)
    target_link_libraries(tide_platform_rtc_fuzz PRIVATE tide_platform)
    add_test(NAME tide_platform_rtc_fuzz COMMAND tide_platform_rtc_fuzz 2000)
    set_tests_properties(tide_platform_rtc_fuzz PROPERTIES ENVIRONMENT TIDE_RTC_LOCAL=1)

    # Its crypto against Node's (OpenSSL's), on random inputs every run.
    # Node runs it on this machine, so not on Android.
    if(NOT TIDE_ANDROID)
        add_executable(tide_platform_rtc_vectors tests/rtc_vectors.c)
        target_include_directories(tide_platform_rtc_vectors PRIVATE src)
        target_link_libraries(tide_platform_rtc_vectors PRIVATE tide_platform)
        if(TIDE_NODE)
            add_test(NAME tide_platform_rtc_vectors
                COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rtc_vectors.mjs"
                    $<TARGET_FILE:tide_platform_rtc_vectors> "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()

    # Rooms on desktop, through a relay on this machine: two desktop players,
    # and a browser's WebRTC against ours, both ways round (tests/rooms.mjs).
    add_executable(tide_platform_rooms tests/rooms_native.c)
    target_link_libraries(tide_platform_rooms PRIVATE tide_platform)
    if(TIDE_NODE AND NOT TIDE_ANDROID)
        add_test(NAME tide_platform_rooms
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rooms.mjs" "${TIDE_BROWSER}"
                $<TARGET_FILE:tide_platform_rooms> "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_rooms-browser"
                "${TIDE_FIREFOX}")
        set_tests_properties(tide_platform_rooms PROPERTIES TIMEOUT 180)
    endif()

    # On Android, rooms need an app around them: the relay's TLS is Java's,
    # which a program run from a shell has none of.
    if(TIDE_ANDROID)
        add_library(tide_platform_rooms_app SHARED tests/rooms_native.c)
        target_link_libraries(tide_platform_rooms_app PRIVATE tide_platform)
        tide_android_app(tide_platform_rooms_app LABEL "Tide rooms")
    endif()

    # Sessions over real UDP, on this machine.
    add_executable(tide_platform_udp tests/udp.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_udp PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tide_platform_udp PRIVATE tide_platform)
    add_test(NAME tide_platform_udp COMMAND tide_platform_udp)
endif()

# A test whose shaders are written by hand, in WGSL alone: on desktop it
# brings tidec's translator, for Vulkan's SPIR-V (tide_gpu_spirv_of).
function(tide_platform_spirv_of target)
    if(NOT TIDE_WEB)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src/spirv.c")
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    endif()
endfunction()

# The renderer: shapes, text and the GUI, read back from an offscreen render.
# `tide_platform_draw --bench` (`?bench` on the web) times frames of many shapes.
add_executable(tide_platform_draw tests/draw.c)
target_link_libraries(tide_platform_draw PRIVATE tide_platform)
tide_platform_spirv_of(tide_platform_draw)
target_compile_definitions(tide_platform_draw PRIVATE TIDE_BENCH_RESULTS="${PROJECT_SOURCE_DIR}/bench/results")
if(TIDE_WEB)
    tide_web_page(tide_platform_draw SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_draw TARGET tide_platform_draw WEBGPU EXPECT "renderer: WebGPU")
    tide_add_web_test(tide_platform_draw_rgba_fallback TARGET tide_platform_draw QUERY "texture-compression=off"
        WEBGPU EXPECT "BC3 fixture: 65536 upload bytes")
elseif(NOT TIDE_ANDROID)
    # A hidden window, so it needs a desktop session with a GPU: drawn with
    # Vulkan where there's one, and with OpenGL again
    target_include_directories(tide_platform_draw PRIVATE third_party) # Vulkan's headers, for src/gpu_vk.h
    add_test(NAME tide_platform_draw COMMAND tide_platform_draw)
    add_test(NAME tide_platform_draw_opengl COMMAND tide_platform_draw)
    tide_gpu_test(tide_platform_draw)
    tide_gpu_test(tide_platform_draw_opengl OPENGL)
    # ...and with Vulkan's descriptor sets, as devices without push descriptors draw (Android's emulator, some phones)
    add_test(NAME tide_platform_draw_vulkan_sets COMMAND tide_platform_draw)
    tide_gpu_test(tide_platform_draw_vulkan_sets)
    set_property(TEST tide_platform_draw_vulkan_sets APPEND PROPERTY ENVIRONMENT TIDE_BACKEND=vulkan TIDE_VULKAN_DESCRIPTOR_SETS=1)
else()
    # On Android a renderer needs an app's window: install
    # tide_platform_draw_app.apk, start it, and read what it prints
    # (`adb logcat -s tide`).
    add_library(tide_platform_draw_app SHARED tests/draw.c)
    target_link_libraries(tide_platform_draw_app PRIVATE tide_platform)
    target_include_directories(tide_platform_draw_app PRIVATE third_party) # Vulkan's headers, for src/gpu_vk.h
    tide_platform_spirv_of(tide_platform_draw_app)
    target_compile_definitions(tide_platform_draw_app PRIVATE TIDE_BENCH_RESULTS="${PROJECT_SOURCE_DIR}/bench/results")
    tide_android_app(tide_platform_draw_app LABEL "Tide draw")
endif()

# Targets are sampleable images: multiple passes preserve their color/depth,
# and sampling keeps the same top-left UVs as uploaded images on every GPU.
if(NOT TIDE_ANDROID)
    add_executable(tide_platform_targets tests/targets.c)
    target_include_directories(tide_platform_targets PRIVATE src)
    target_link_libraries(tide_platform_targets PRIVATE tide_platform)
    tide_platform_spirv_of(tide_platform_targets)
    if(TIDE_WEB)
        tide_web_page(tide_platform_targets SHELL web/test_shell.html)
        tide_add_web_test(tide_platform_targets TARGET tide_platform_targets WEBGPU EXPECT "renderer: WebGPU")
    else()
        add_test(NAME tide_platform_targets COMMAND tide_platform_targets)
        add_test(NAME tide_platform_targets_opengl COMMAND tide_platform_targets)
        tide_gpu_test(tide_platform_targets)
        tide_gpu_test(tide_platform_targets_opengl OPENGL)
    endif()
else()
    # As the draw checks, an app: install tide_platform_targets_app.apk, start
    # it, and read what it prints (`adb logcat -s tide`).
    add_library(tide_platform_targets_app SHARED tests/targets.c)
    target_include_directories(tide_platform_targets_app PRIVATE src)
    target_link_libraries(tide_platform_targets_app PRIVATE tide_platform)
    tide_platform_spirv_of(tide_platform_targets_app)
    tide_android_app(tide_platform_targets_app LABEL "Tide targets")
endif()

# frag and vert, written in Tide: every shader tidec writes for
# tests/shaders.tide has to compile on the backend that draws, without a
# warning, and give the pixels tests/shaders.c expects.
if(NOT TIDE_ANDROID)
    tide_add_game(tide_platform_shaders SOURCES tests/shaders.tide NAME game HOST tests/shaders.c
                  WARNINGS "'source' may say Filter.Trilinear, which this Sample can't read")
    target_include_directories(tide_platform_shaders PRIVATE src)
    target_link_libraries(tide_platform_shaders PRIVATE tide_platform)
    if(TIDE_WEB)
        tide_web_page(tide_platform_shaders SHELL web/test_shell.html)
        tide_add_web_test(tide_platform_shaders TARGET tide_platform_shaders WEBGPU EXPECT "renderer: WebGPU")
    else()
        add_test(NAME tide_platform_shaders COMMAND tide_platform_shaders)
        add_test(NAME tide_platform_shaders_opengl COMMAND tide_platform_shaders)
        tide_gpu_test(tide_platform_shaders)
        tide_gpu_test(tide_platform_shaders_opengl OPENGL)
    endif()
else()
    # An app there too, to install and start by hand: OpenGL ES 3 has to
    # compile what tidec writes as the others do (`adb logcat -s tide`).
    tide_add_game(tide_platform_shaders_app SOURCES tests/shaders.tide NAME game HOST tests/shaders.c WINDOW
                  WARNINGS "'source' may say Filter.Trilinear, which this Sample can't read")
    target_include_directories(tide_platform_shaders_app PRIVATE src)
endif()

if(NOT TIDE_ANDROID)
    # Variable influence streams, deterministic pose samples, generated skin stage and matching shadow draws.
    tide_add_game(tide_platform_skinned SOURCES tests/skinned.tide NAME game HOST tests/skinned.c)
    target_link_libraries(tide_platform_skinned PRIVATE tide_platform)
    if(TIDE_WEB)
        tide_web_page(tide_platform_skinned SHELL web/test_shell.html)
        tide_add_web_test(tide_platform_skinned TARGET tide_platform_skinned WEBGPU EXPECT "renderer: WebGPU")
    else()
        add_test(NAME tide_platform_skinned COMMAND tide_platform_skinned)
        add_test(NAME tide_platform_skinned_opengl COMMAND tide_platform_skinned)
        tide_gpu_test(tide_platform_skinned)
        tide_gpu_test(tide_platform_skinned_opengl OPENGL)
    endif()
endif()

if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    # The watching host's Ctrl+R: a library is reopened so C statics reset
    # along with its local state and match. Scripted headless input, no visible window.
    add_library(tide_platform_reload_game SHARED tests/reload_game.c)
    target_include_directories(tide_platform_reload_game PRIVATE include "${PROJECT_SOURCE_DIR}/engine/include")
    target_link_libraries(tide_platform_reload_game PRIVATE tide_flags)
    # ...and a build of another layout, which the host swaps in while the game runs
    add_library(tide_platform_reload_game_next SHARED tests/reload_game.c)
    target_compile_definitions(tide_platform_reload_game_next PRIVATE RELOAD_GAME_HASH=4343)
    target_include_directories(tide_platform_reload_game_next PRIVATE include "${PROJECT_SOURCE_DIR}/engine/include")
    target_link_libraries(tide_platform_reload_game_next PRIVATE tide_flags)
    add_executable(tide_platform_reload tests/reload_host.c)
    target_link_libraries(tide_platform_reload PRIVATE tide_platform)
    add_executable(tide_platform_reload_plain tests/reload_host.c tests/reload_game.c)
    target_compile_definitions(tide_platform_reload_plain PRIVATE TIDE_RELOAD_PLAIN)
    target_link_libraries(tide_platform_reload_plain PRIVATE tide_platform)
    if(TIDE_NODE)
        add_test(NAME tide_platform_reload
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reload.mjs" $<TARGET_FILE:tide_platform_reload>
                $<TARGET_FILE:tide_platform_reload_plain> $<TARGET_FILE:tide_platform_reload_game>
                "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_reload-run" $<TARGET_FILE:tide_platform_reload_game_next>)
        set_tests_properties(tide_platform_reload PROPERTIES TIMEOUT 120)
        tide_gpu_test(tide_platform_reload)
    endif()

    # A game with no window (tide/run.h's --capture, --frames and --script): a
    # run with a script of every kind of input, whose pictures are read back
    # and checked. Nothing shows, but it needs a desktop session with OpenGL,
    # as tide_platform_draw does. What a run refuses before it opens anything,
    # a flag or a script's line that's wrong, needs none.
    tide_add_game(tide_platform_capture SOURCES tests/capture.tide)
    if(TIDE_NODE)
        add_test(NAME tide_platform_capture
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capture.mjs" $<TARGET_FILE:tide_platform_capture>
                "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_capture-run")
        set_tests_properties(tide_platform_capture PROPERTIES TIMEOUT 120)
        tide_gpu_test(tide_platform_capture)
        add_test(NAME tide_platform_capture_refused
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capture.mjs" $<TARGET_FILE:tide_platform_capture>
                "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_capture-refused" refused)
    endif()

    # The standard host's stats (tide/stats.h), in such a run: a drawer that
    # starts as one line, which a script's click, tap and F8 open and close,
    # and whose presses never reach the game.
    tide_add_game(tide_platform_stats SOURCES tests/stats.tide)
    if(TIDE_NODE)
        add_test(NAME tide_platform_stats
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/stats.mjs" $<TARGET_FILE:tide_platform_stats>
                "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_stats-run")
        set_tests_properties(tide_platform_stats PROPERTIES TIMEOUT 120)
        tide_gpu_test(tide_platform_stats)
    endif()

    # A match started from a menu, in such a run: its first tick reads what's
    # held as it starts, never what the devices kept from before it.
    tide_add_game(tide_platform_first_tick SOURCES tests/first_tick.tide)
    if(TIDE_NODE)
        add_test(NAME tide_platform_first_tick
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/first_tick.mjs" $<TARGET_FILE:tide_platform_first_tick>
                "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_first_tick-run")
        set_tests_properties(tide_platform_first_tick PROPERTIES TIMEOUT 120)
        tide_gpu_test(tide_platform_first_tick)
    endif()

    # Such a run of several players (tide/run.h's --players): each a process of
    # its own, a frame at a time together on a made-up network. A game whose
    # Main is the match's, where the first player opens it and the others join,
    # and one that starts in a menu, where the script has them open and join by
    # the rooms' codes: every player's pictures are read, and a run repeats byte
    # for byte. What such a run refuses before it opens anything needs no GPU.
    # Its network alone, with no window at all (tests/players_net.c): three
    # processes that send text through it and check what arrives, frame by
    # frame, and one that ends before the run does. It starts processes and
    # listens on this machine, with no GPU, so every desktop's tests run it.
    add_executable(tide_platform_players_net tests/players_net.c)
    target_include_directories(tide_platform_players_net PRIVATE src)
    target_link_libraries(tide_platform_players_net PRIVATE tide_platform)
    add_test(NAME tide_platform_players_net COMMAND tide_platform_players_net)
    add_test(NAME tide_platform_players_net_ended COMMAND tide_platform_players_net die)
    set_tests_properties(tide_platform_players_net tide_platform_players_net_ended PROPERTIES TIMEOUT 60)

    tide_add_game(tide_platform_players SOURCES tests/players.tide)
    tide_add_game(tide_platform_players_menu SOURCES tests/players_menu.tide)
    if(TIDE_NODE)
        add_test(NAME tide_platform_players
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/players.mjs" $<TARGET_FILE:tide_platform_players>
                $<TARGET_FILE:tide_platform_players_menu> "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_players-run")
        set_tests_properties(tide_platform_players PROPERTIES TIMEOUT 180)
        tide_gpu_test(tide_platform_players)
        add_test(NAME tide_platform_players_refused
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/players.mjs" $<TARGET_FILE:tide_platform_players>
                $<TARGET_FILE:tide_platform_players_menu> "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_players-refused" refused)
    endif()

    # A run nobody watches (TIDE_HIDDEN in the environment, as an agent's
    # shell and a build server have it): no window shows, a game or a host
    # that only its window's closing would end doesn't start, and a stop shows
    # no dialog. None of that needs a window. That a run with no window and a
    # test's hidden window go as they do without the variable, but that the
    # window opens no audio device, does, as tide_platform_draw does. The
    # program's audio devices are its own (tests/hidden.c), in the place of
    # desktop/audio.c's.
    add_executable(tide_platform_hidden tests/hidden.c)
    target_include_directories(tide_platform_hidden PRIVATE src)
    target_link_libraries(tide_platform_hidden PRIVATE tide_platform)
    if(TIDE_NODE)
        add_test(NAME tide_platform_hidden
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/hidden.mjs" $<TARGET_FILE:tide_platform_hidden>
                $<TARGET_FILE:tide_platform_capture> "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_hidden-refused")
        add_test(NAME tide_platform_hidden_runs
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/hidden.mjs" $<TARGET_FILE:tide_platform_hidden>
                $<TARGET_FILE:tide_platform_capture> "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_hidden-runs" runs)
        set_tests_properties(tide_platform_hidden_runs PROPERTIES TIMEOUT 120)
        tide_gpu_test(tide_platform_hidden_runs)
    endif()

    # The window's images on Vulkan: made again when the window is resized and
    # when the device is lost, in a hidden window, so it needs a desktop
    # session with a GPU. It's Vulkan's alone, so it refuses to draw with
    # anything else.
    add_executable(tide_platform_swapchain tests/swapchain.c)
    target_include_directories(tide_platform_swapchain PRIVATE src third_party)
    target_link_libraries(tide_platform_swapchain PRIVATE tide_platform)
    add_test(NAME tide_platform_swapchain COMMAND tide_platform_swapchain)
    set_tests_properties(tide_platform_swapchain PROPERTIES TIMEOUT 120)
    tide_gpu_test(tide_platform_swapchain)
    set_property(TEST tide_platform_swapchain APPEND PROPERTY ENVIRONMENT TIDE_BACKEND=vulkan)

    # Vulkan's pipelines kept across runs, in the game's temporary cache folder,
    # and every way the file it reads can be wrong (tests/pipeline_cache.c)
    add_executable(tide_platform_pipeline_cache tests/pipeline_cache.c)
    target_include_directories(tide_platform_pipeline_cache PRIVATE src third_party)
    target_link_libraries(tide_platform_pipeline_cache PRIVATE tide_platform)
    add_test(NAME tide_platform_pipeline_cache
        COMMAND "${CMAKE_COMMAND}" -DPROGRAM=$<TARGET_FILE:tide_platform_pipeline_cache>
            "-DDIR=${CMAKE_CURRENT_BINARY_DIR}/tide_platform_pipeline_cache" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/pipeline_cache.cmake")
    set_tests_properties(tide_platform_pipeline_cache PROPERTIES TIMEOUT 300)
    tide_gpu_test(tide_platform_pipeline_cache)
    set_property(TEST tide_platform_pipeline_cache APPEND PROPERTY ENVIRONMENT TIDE_BACKEND=vulkan)

    # The cursor's lock on desktop: only a window with the focus holds it, and
    # a test's window, hidden, never has it. It needs a desktop session, as
    # tide_platform_draw does.
    add_executable(tide_platform_cursor tests/cursor.c)
    target_link_libraries(tide_platform_cursor PRIVATE tide_platform)
    add_test(NAME tide_platform_cursor COMMAND tide_platform_cursor)
    tide_gpu_test(tide_platform_cursor)
    # What a window with the focus does with it (desktop/cursor.h): Escape gives
    # the cursor back to the player, and a click or a key takes it again. No
    # window, so it runs everywhere.
    add_executable(tide_platform_cursor_rules tests/cursor_rules.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_cursor_rules PRIVATE "${PROJECT_SOURCE_DIR}/tests" desktop)
    target_link_libraries(tide_platform_cursor_rules PRIVATE tide)
    add_test(NAME tide_platform_cursor_rules COMMAND tide_platform_cursor_rules)
endif()

if(TIDE_WEB)
    # Keys by physical position on the web, where the typed character would be wrong.
    add_executable(tide_platform_web_io tests/web_io.c)
    target_link_libraries(tide_platform_web_io PRIVATE tide_platform)
    tide_web_page(tide_platform_web_io SHELL tests/web_io.html)
    add_test(NAME tide_platform_web_io
        COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_web_served_test.mjs" "${TIDE_BROWSER}"
            "$<TARGET_FILE_DIR:tide_platform_web_io>/tide_platform_web_io.html"
            "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_web_io-browser" "backend=webgl")
    # The guard around the runner's own 100 seconds and what it asks the page after them (cmake/WebTest.cmake)
    set_tests_properties(tide_platform_web_io PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 150)

    add_executable(tide_platform_web_keys tests/web_keys.c)
    target_link_libraries(tide_platform_web_keys PRIVATE tide_platform)
    tide_web_page(tide_platform_web_keys SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_keys TARGET tide_platform_web_keys)

    # Fingers on the canvas: the touchscreen's, never the mouse's.
    add_executable(tide_platform_web_touch tests/web_touch.c)
    target_link_libraries(tide_platform_web_touch PRIVATE tide_platform)
    tide_web_page(tide_platform_web_touch SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_touch TARGET tide_platform_web_touch)

    # A game in a page that scrolls (`embedded` in its page's address) leaves
    # the page the wheel, and a finger until one taps the game.
    add_executable(tide_platform_web_embedded tests/web_embedded.c)
    target_link_libraries(tide_platform_web_embedded PRIVATE tide_platform)
    tide_web_page(tide_platform_web_embedded SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_embedded TARGET tide_platform_web_embedded QUERY embedded)

    # The cursor's lock: the browser's to give at a click or a key press, and
    # to take back. The page stands in for the browser, which gives a script's
    # events no lock.
    add_executable(tide_platform_web_cursor tests/web_cursor.c)
    target_link_libraries(tide_platform_web_cursor PRIVATE tide_platform)
    tide_web_page(tide_platform_web_cursor SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_cursor TARGET tide_platform_web_cursor)

    # The standard host's debug views (tide/host.h): F7 shows the profiler where
    # they're on, by the command line or the game's setting, and nowhere else.
    tide_add_game(tide_platform_web_debug_views SOURCES tests/debug_views.tide NAME game HOST tests/web_debug_views.c)
    target_link_libraries(tide_platform_web_debug_views PRIVATE tide_platform)
    tide_web_page(tide_platform_web_debug_views SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_debug_views TARGET tide_platform_web_debug_views QUERY debug-views)

    # ...and its stats (tide/stats.h): a drawer that a game's setting shows
    # closed, which F8, a click and a tap open and close, whose presses the
    # game never reads, and for which nothing is measured while it's closed.
    tide_add_game(tide_platform_web_stats SOURCES tests/stats.tide NAME game HOST tests/web_stats.c)
    target_link_libraries(tide_platform_web_stats PRIVATE tide_platform)
    tide_web_page(tide_platform_web_stats SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_stats TARGET tide_platform_web_stats WEBGPU)

    # ...and what its devices keep for a tick's sample: nothing while no tick
    # can sample, so a match started from a menu doesn't read what was pressed
    # before it, and everything between two ticks of a match that runs.
    tide_add_game(tide_platform_web_first_tick SOURCES tests/first_tick.tide NAME game HOST tests/web_first_tick.c)
    target_link_libraries(tide_platform_web_first_tick PRIVATE tide_platform)
    tide_web_page(tide_platform_web_first_tick SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_first_tick TARGET tide_platform_web_first_tick)

    # Two players in a room: a relay on this machine, and WebRTC between two
    # frames of one page, in real time rather than the other tests' virtual time.
    add_executable(tide_platform_web_rooms tests/web_rooms.c)
    target_link_libraries(tide_platform_web_rooms PRIVATE tide_platform)
    tide_web_page(tide_platform_web_rooms SHELL tests/web_rooms.html)
    add_test(NAME tide_platform_web_rooms
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_rooms.mjs" "${TIDE_BROWSER}"
            "$<TARGET_FILE_DIR:tide_platform_web_rooms>/tide_platform_web_rooms.html"
            "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_web_rooms-browser")
    set_tests_properties(tide_platform_web_rooms PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)
    # Host migration in the browser: the host's frame leaves, and the two
    # others carry on in the room, one of them hosting it.
    add_test(NAME tide_platform_web_rooms_handover
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_rooms.mjs" "${TIDE_BROWSER}"
            "$<TARGET_FILE_DIR:tide_platform_web_rooms>/tide_platform_web_rooms.html"
            "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_web_rooms_handover-browser" handover)
    set_tests_properties(tide_platform_web_rooms_handover PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)
    # ...and a match its host ended stays ended, for a player who missed the goodbye.
    add_test(NAME tide_platform_web_rooms_ended
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_rooms.mjs" "${TIDE_BROWSER}"
            "$<TARGET_FILE_DIR:tide_platform_web_rooms>/tide_platform_web_rooms.html"
            "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_web_rooms_ended-browser" ended)
    set_tests_properties(tide_platform_web_rooms_ended PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)

    # A page told to play at a slow connection (`Tide.lag`): its lag is the
    # round trip its player gets, what the trip takes already in it. The page
    # stands in for the relay and for WebRTC, in the other tests' virtual time.
    add_executable(tide_platform_web_lag tests/web_lag.c)
    target_link_libraries(tide_platform_web_lag PRIVATE tide_platform)
    tide_web_page(tide_platform_web_lag SHELL web/test_shell.html)
    tide_add_web_test(tide_platform_web_lag TARGET tide_platform_web_lag)

    # Ticks on threads in a browser: a cross-origin isolated page, whose
    # threads are workers sharing its memory, against ticks on one thread.
    tide_add_game(tide_platform_web_threads SOURCES tests/jobs.tide tests/jobs_tasks.c NAME game HOST tests/web_threads.c)
    target_link_libraries(tide_platform_web_threads PRIVATE tide_platform)
    tide_web_page(tide_platform_web_threads SHELL tests/web_threads.html)
    add_test(NAME tide_platform_web_threads
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_threads.mjs" "${TIDE_BROWSER}"
            "$<TARGET_FILE_DIR:tide_platform_web_threads>/tide_platform_web_threads.html"
            "${CMAKE_CURRENT_BINARY_DIR}/tide_platform_web_threads-browser")
    set_tests_properties(tide_platform_web_threads PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)
endif()
