# tide: the command users run to build, run and upgrade games. It runs tidec's
# front end in-process and calls clang (see AGENTS.md, Packaging and releases).
add_executable(tide_cli main.c android.c build.c deploy.c editors.c fetch.c install.c libraries.c line.c open.c packages_cmd.c publish.c release.c
    serve.c sys.c toolchain.c unzip.c upgrade.c ${TIDE_APK_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/json.c" "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/packages.c")
# Android apps are signed with the platform layer's crypto (apk.c).
target_include_directories(tide_cli PRIVATE . ../lsp "${PROJECT_SOURCE_DIR}/platform/src/rtc")
target_link_libraries(tide_cli PRIVATE tidec_core)
# The engine library is already the `tide` target; the program is still called tide.
set_target_properties(tide_cli PROPERTIES OUTPUT_NAME tide)
target_compile_definitions(tide_cli PRIVATE
    "TIDE_VERSION=\"${TIDE_VERSION}\""
    "TIDE_CHANNEL=\"${TIDE_CHANNEL}\"")
if(WIN32)
    target_link_libraries(tide_cli PRIVATE ws2_32 crypt32) # Web serving and user-bound encrypted hosting credentials.
else()
    find_package(Threads REQUIRED)
    target_link_libraries(tide_cli PRIVATE Threads::Threads) # Reads the terminal while a game runs
endif()
install(TARGETS tide_cli RUNTIME DESTINATION bin COMPONENT tide)

# Versions and picking releases, a package put over an installation, which
# platform libraries are for, what tide.packages and git's lists of branches
# and tags say, without the network, Android apps as Android checks them,
# zips, and the path of a file that isn't there yet.
add_executable(tide_cli_tests tests/release_test.c tests/install_test.c tests/libraries_test.c tests/packages_test.c tests/apk_test.c
    tests/unzip_test.c tests/rsa_test.c tests/sys_test.c release.c install.c libraries.c fetch.c sys.c unzip.c ${TIDE_APK_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/json.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/packages.c" "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_cli_tests PRIVATE . ../lsp "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/platform/src/rtc")
target_link_libraries(tide_cli_tests PRIVATE tide tidec_core)
if(NOT WIN32)
    target_link_libraries(tide_cli_tests PRIVATE Threads::Threads)
endif()
add_test(NAME tide_cli_tests COMMAND tide_cli_tests)

# The command must finish when its game does, with the game's exit code.
# Exercise main's real dispatch with runs and update checks supplied by a
# fixture, so shutdown order is checked without a game window or network.
add_executable(tide_cli_run_exit_tests tests/run_exit_test.c sys.c)
target_include_directories(tide_cli_run_exit_tests PRIVATE . ../lsp)
target_link_libraries(tide_cli_run_exit_tests PRIVATE tidec_core)
if(NOT WIN32)
    target_link_libraries(tide_cli_run_exit_tests PRIVATE Threads::Threads)
endif()
add_test(NAME tide_cli_run_exit COMMAND tide_cli_run_exit_tests)

# A download tool can report success without writing its output. Fetching
# must reject that result before it tries to unpack a nonexistent archive.
add_executable(tide_cli_fetch_tests tests/fetch_download_test.c sys.c)
target_include_directories(tide_cli_fetch_tests PRIVATE . ../lsp)
target_link_libraries(tide_cli_fetch_tests PRIVATE tide_flags)
if(NOT WIN32)
    target_link_libraries(tide_cli_fetch_tests PRIVATE Threads::Threads)
endif()
add_test(NAME tide_cli_fetch COMMAND tide_cli_fetch_tests)

# Games with packages: in folders, and from git through a folder that stands
# in for git hosts.
add_test(NAME tide_cli_packages
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/packages_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/packages_test.cmake")

# tide publish, against the build service (builds/) on this machine.
if(TIDE_NODE)
    add_test(NAME tide_cli_installers
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/installers_test.mjs")
    # Assets: the pack tidec writes, and URLs fetched once and kept with their ETag, against a server on this machine
    add_test(NAME tide_cli_assets
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets_test.mjs" --tidec "$<TARGET_FILE:tidec>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/assets_test")
    add_test(NAME tide_cli_publish
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/publish_test.mjs" --tide "$<TARGET_FILE:tide_cli>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/publish_test")
endif()

# tide editors, with fake editors where their installers put them.
add_test(NAME tide_cli_editors
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/editors_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/editors_test.cmake")

# tide upgrade, with a folder that stands in for the site's releases.
add_test(NAME tide_cli_upgrade
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/upgrade_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/upgrade_test.cmake")

# tide run where nobody watches (TIDE_HIDDEN): refused before it builds
# anything, unless the run ends by itself.
add_test(NAME tide_cli_hidden
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/hidden_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/hidden_test.cmake")

# Packages build clang and lld into tide, so users need no compiler of their
# own (see cmake/LLVM.cmake).
option(TIDE_EMBED_LLVM "Build clang and lld into tide, from LLVM's static libraries" OFF)
if(TIDE_EMBED_LLVM)
    include("${PROJECT_SOURCE_DIR}/cmake/LLVM.cmake")
    target_link_libraries(tide_cli PRIVATE tide_cc)
    target_compile_definitions(tide_cli PRIVATE TIDE_EMBEDDED_CLANG "TIDE_LLVM_VERSION=\"${TIDE_LLVM_VERSION}\"")
endif()
