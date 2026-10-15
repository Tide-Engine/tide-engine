# Opt-in co-op recursive puzzle; one match implementation on desktop and web.
file(GLOB INSIDE_OUT_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT INSIDE_OUT_SOURCES)
set(INSIDE_OUT_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

tide_add_game(inside_out_tests SOURCES game.tide levels.tide test/cues.tide NAME game ${INSIDE_OUT_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c test/campaign.c)
target_include_directories(inside_out_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME inside_out_tests COMMAND inside_out_tests)
set_tests_properties(inside_out_tests PROPERTIES TIMEOUT 120 LABELS "private-prototype")

add_executable(inside_out_invite_test invite.c test/invite.c)
target_link_libraries(inside_out_invite_test PRIVATE tide_flags)
set(invite_fallback "https://tide-engine.dev/prototypes/inside-out/")
add_test(NAME inside_out_invite_fallback COMMAND inside_out_invite_test "${invite_fallback}")
if(TIDE_WEB)
    add_test(NAME inside_out_invite_local COMMAND inside_out_invite_test
        "http://127.0.0.1:8790/prototypes/inside-out/"
        "--inside-out-invite-base=http://127.0.0.1:8790/prototypes/inside-out/")
    add_test(NAME inside_out_invite_hosted COMMAND inside_out_invite_test
        "https://preview.example/games/inside-out/"
        "--inside-out-invite-base=https://preview.example/games/inside-out/")
    add_test(NAME inside_out_invite_invalid COMMAND inside_out_invite_test "${invite_fallback}"
        "--inside-out-invite-base=javascript:alert(1)"
        "--inside-out-invite-base=https://preview.example/?join=OLD"
        "--inside-out-invite-base=https://preview.example/#fragment")
    string(REPEAT "x" 2100 invite_long_path)
    add_test(NAME inside_out_invite_overlong COMMAND inside_out_invite_test "${invite_fallback}"
        "--inside-out-invite-base=https://preview.example/${invite_long_path}")
endif()

# The puzzle solver and inspector (usage at the top of test/solve.c), a
# desktop tool rather than a test. It moves through the generated Move
# handler, so the game's C is optimized with it in every configuration; the
# engine's float flags (cmake/TideFlags.cmake) come after -O2 and still hold.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    tide_add_game(inside_out_solve SOURCES game.tide levels.tide test/solve.tide NAME game ${INSIDE_OUT_ROOMS}
        HOST test/solve.c)
    target_compile_options(inside_out_solve PRIVATE -O2)
endif()

if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(inside_out SOURCES ${INSIDE_OUT_SOURCES} invite.c ${INSIDE_OUT_ROOMS} STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/inside-out")
    add_custom_target(inside_out_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:inside_out>/inside_out.html" "${destination}/index.html"
        DEPENDS inside_out VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME inside_out_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:inside_out>/inside_out.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(inside_out_play_${backend} PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(inside_out_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
