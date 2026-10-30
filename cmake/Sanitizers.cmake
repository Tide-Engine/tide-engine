# Instrument the engine, platform, generated games and build/test tools together.
option(TIDE_SANITIZE "Build native tests with ASan (and UBSan on Linux/macOS)" OFF)
if(NOT TIDE_SANITIZE)
    return()
endif()
if(TIDE_WEB OR TIDE_ANDROID OR CMAKE_CROSSCOMPILING OR MINGW)
    message(FATAL_ERROR "TIDE_SANITIZE is for native desktop tests. Use the sanitize preset with the native clang toolchain.")
endif()

set(tide_sanitizers address,undefined)
if(WIN32)
    set(tide_sanitizers address)
    # /OPT:REF otherwise enables lld-link's identical/tail data folding too.
    # Folding ASan-padded literals can alias different-sized narrow/wide
    # strings or place an empty string in another global's poisoned redzone.
    add_link_options("LINKER:/OPT:NOICF")
    # Instrumented fixture frames can exceed Windows' 1 MiB stack (the rewind
    # fixture alone needs 0x10a5f8 bytes). Reserve room for ASan's stack overhead;
    # ordinary game builds retain their platform's normal stack limit.
    add_link_options("LINKER:/STACK:8388608")

    # Ask the selected compiler, not PATH or a hard-coded LLVM version. Some
    # Windows LLVM distributions return a nonexistent --print-runtime-dir;
    # --print-file-name also finds their legacy lib/windows runtime layout.
    set(tide_sanitizer_target_args)
    if(CMAKE_C_COMPILER_TARGET)
        list(APPEND tide_sanitizer_target_args "--target=${CMAKE_C_COMPILER_TARGET}")
    endif()
    execute_process(COMMAND "${CMAKE_C_COMPILER}" ${tide_sanitizer_target_args} -dumpmachine
        OUTPUT_VARIABLE tide_sanitizer_triple OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(tide_sanitizer_triple MATCHES "^(x86_64|amd64)-")
        set(tide_sanitizer_arch x86_64)
    elseif(tide_sanitizer_triple MATCHES "^i[3-6]86-")
        set(tide_sanitizer_arch i386)
    elseif(tide_sanitizer_triple MATCHES "^(aarch64|arm64)-")
        set(tide_sanitizer_arch aarch64)
    else()
        message(FATAL_ERROR "No Windows ASan runtime name is known for ${tide_sanitizer_triple}.")
    endif()
    set(tide_asan_dll)
    foreach(name "clang_rt.asan_dynamic-${tide_sanitizer_arch}.dll" clang_rt.asan_dynamic.dll)
        execute_process(COMMAND "${CMAKE_C_COMPILER}" ${tide_sanitizer_target_args} "--print-file-name=${name}"
            OUTPUT_VARIABLE candidate OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(IS_ABSOLUTE "${candidate}" AND EXISTS "${candidate}")
            file(TO_CMAKE_PATH "${candidate}" tide_asan_dll)
            break()
        endif()
    endforeach()
    if(NOT tide_asan_dll)
        message(FATAL_ERROR "The selected clang has no Windows AddressSanitizer runtime. Install LLVM with compiler-rt, then configure again (LLVM_ROOT can select it).")
    endif()

    # DLL lookup beside the executable also covers tidec/spirv build commands
    # and direct test invocations, without changing the developer's PATH.
    file(COPY "${tide_asan_dll}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
        file(COPY "${tide_asan_dll}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${config}")
    endforeach()
    get_filename_component(tide_asan_runtime_dir "${tide_asan_dll}" DIRECTORY)
    get_filename_component(tide_asan_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(tide_asan_symbolizer NAMES llvm-symbolizer
        PATHS "${tide_asan_compiler_dir}" NO_DEFAULT_PATH NO_CACHE REQUIRED)

    message(STATUS "Windows AddressSanitizer: ${tide_asan_dll}")
endif()
# CLI integration tests copy executables into scratch installations. CTest's
# environment reaches those children too, including ctest --test-dir runs.
configure_file("${CMAKE_CURRENT_LIST_DIR}/sanitizer_test_environment.cmake.in"
    "${CMAKE_BINARY_DIR}/sanitizer_test_environment.cmake" @ONLY)
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_BINARY_DIR}/sanitizer_test_environment.cmake")
add_compile_options("-fsanitize=${tide_sanitizers}" -fno-omit-frame-pointer -fno-sanitize-recover=all)
add_link_options("-fsanitize=${tide_sanitizers}")
