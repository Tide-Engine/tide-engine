# tide: the command users run to build, run and upgrade games. It runs tidec's
# front end in-process and calls clang.
add_executable(tide_cli main.c help.c android.c build.c deploy.c editors.c jetbrains.c fetch.c install.c libraries.c line.c open.c packages_cmd.c publish.c release.c
    serve.c sys.c telemetry.c toolchain.c unity_assets.c unity_code.c unity_mapping.c unity_shaders.c unity_yaml.c unzip.c upgrade.c ${TIDE_APK_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/json.c" "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/packages.c" "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/unity_names.c")
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
    target_link_libraries(tide_cli PRIVATE Threads::Threads ${CMAKE_DL_LIBS}) # Reads the terminal while a game runs; loads Unity builds' schemas
endif()
install(TARGETS tide_cli RUNTIME DESTINATION bin COMPONENT tide)

# Versions and picking releases, a package put over an installation, which
# platform libraries are for, what tide.packages and git's lists of branches
# and tags say, without the network, Android apps as Android checks them,
# zips, and the path of a file that isn't there yet.
add_executable(tide_cli_tests tests/release_test.c tests/install_test.c tests/libraries_test.c tests/packages_test.c tests/apk_test.c
    tests/unzip_test.c tests/rsa_test.c tests/sys_test.c tests/unity_assets_test.c tests/unity_code_test.c tests/unity_shaders_test.c release.c
    install.c libraries.c fetch.c sys.c unity_assets.c unity_code.c unity_mapping.c unity_shaders.c unity_yaml.c unzip.c ${TIDE_APK_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/json.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/packages.c" "${CMAKE_CURRENT_SOURCE_DIR}/../lsp/unity_names.c" "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_cli_tests PRIVATE . ../lsp "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/platform/src/rtc")
# A Unity project's Assets, as Unity wrote them, for tide unity --assets
target_compile_definitions(tide_cli_tests PRIVATE "TIDE_UNITY_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_project/Assets\""
    "TIDE_UNITY_COMPONENTS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_components/Assets\""
    "TIDE_UNITY_SAMPLE_PACKAGE=\"${PROJECT_SOURCE_DIR}/unity/Sample/Packages/com.tide.game\""
    "TIDE_UNITY_SCRATCH=\"${CMAKE_CURRENT_BINARY_DIR}/unity_unsaved_test\""
    "TIDE_UNITY_PHYSICS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_physics/Assets\""
    "TIDE_BOX3D_PACKAGE=\"${PROJECT_SOURCE_DIR}/packages/box3d\""
    "TIDE_BOX2D_PACKAGE=\"${PROJECT_SOURCE_DIR}/packages/box2d\""
    "TIDE_UNITY_RULES_PACKAGE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_rules\""
    "TIDE_UNITY_RULES_PHYSICS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_rules_physics\""
    "TIDE_UNITY_NAMES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_names\"")
target_link_libraries(tide_cli_tests PRIVATE tide tidec_core)
if(NOT WIN32)
    target_link_libraries(tide_cli_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
add_test(NAME tide_cli_tests COMMAND tide_cli_tests)

# The command must finish when its game does, with the game's exit code.
# Exercise main's real dispatch with runs and update checks supplied by a
# fixture, so shutdown order is checked without a game window or network.
add_executable(tide_cli_run_exit_tests tests/run_exit_test.c help.c sys.c)
target_include_directories(tide_cli_run_exit_tests PRIVATE . ../lsp)
target_link_libraries(tide_cli_run_exit_tests PRIVATE tidec_core)
if(NOT WIN32)
    target_link_libraries(tide_cli_run_exit_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
add_test(NAME tide_cli_run_exit COMMAND tide_cli_run_exit_tests)

# Apple SDK discovery must follow the selected linker even when a newer
# standalone Command Line Tools package is installed. Stub discovery only.
add_executable(tide_cli_toolchain_tests tests/toolchain_test.c sys.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_cli_toolchain_tests PRIVATE . "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_cli_toolchain_tests PRIVATE tide tidec_core)
if(NOT WIN32)
    target_link_libraries(tide_cli_toolchain_tests PRIVATE Threads::Threads)
endif()
add_test(NAME tide_cli_toolchain COMMAND tide_cli_toolchain_tests)

# A download tool can report success without writing its output. Fetching
# must reject that result before it tries to unpack a nonexistent archive.
add_executable(tide_cli_fetch_tests tests/fetch_download_test.c sys.c)
target_include_directories(tide_cli_fetch_tests PRIVATE . ../lsp)
target_link_libraries(tide_cli_fetch_tests PRIVATE tide_flags)
if(NOT WIN32)
    target_link_libraries(tide_cli_fetch_tests PRIVATE Threads::Threads)
endif()
add_test(NAME tide_cli_fetch COMMAND tide_cli_fetch_tests)

# Help must be available without a project, installation or side effects.
add_test(NAME tide_cli_help
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/help_test.cmake")

# Games with packages: in folders, and from git through a folder that stands
# in for git hosts.
add_test(NAME tide_cli_packages
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/packages_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/packages_test.cmake")

if(TIDE_NODE)
    add_test(NAME tide_cli_installers
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/installers_test.mjs")
    # Assets: the pack tidec writes, and URLs fetched once and kept with their ETag, against a server on this machine
    # Usage data: the events a copy of tide queues and sends to a server on
    # this machine, what they hold and never hold, and every way it's off.
    add_test(NAME tide_cli_telemetry
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/telemetry_test.mjs" --tide "$<TARGET_FILE:tide_cli>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/telemetry_test")
    add_test(NAME tide_cli_assets
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets_test.mjs" --tidec "$<TARGET_FILE:tidec>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/assets_test")
endif()

# tide editors, with fake editors where their installers put them.
add_test(NAME tide_cli_editors
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/editors_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/editors_test.cmake")

add_test(NAME tide_cli_jetbrains
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/jetbrains test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/jetbrains_test.cmake")

add_test(NAME tide_cli_jetbrains_package
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/jetbrains package test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/jetbrains_package_test.cmake")

add_test(NAME tide_cli_ide
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DTIDELS=$<TARGET_FILE:tidels>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/ide_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ide_test.cmake")

# A Unity project's game with the physics packages, as their unity.json bakes it, checked against them.
add_test(NAME tide_cli_unity_physics
    COMMAND "${CMAKE_COMMAND}" "-DTIDE=$<TARGET_FILE:tide_cli>" "-DSOURCE=${PROJECT_SOURCE_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/unity_physics_test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/unity_physics_test.cmake")

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
