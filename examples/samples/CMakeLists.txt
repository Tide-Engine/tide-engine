# Each sample is a self-contained tide run folder. The identical lobby.tide
# copies come from the shared file beside this CMakeLists.txt.
if(NOT TARGET tide_platform)
    return()
endif()

add_custom_target(sample_games)
foreach(id pong snake tanks bomberman connect-four asteroids sumo tower-defense marbles)
    string(REPLACE "-" "_" name "${id}")
    set(target "sample_${name}")
    tide_add_game(${target} SOURCES "${id}/game.tide" "${id}/lobby.tide")
    if(TIDE_WEB)
        # Stage even an unchanged build, so clearing docs/static is recoverable.
        set(destination "${PROJECT_SOURCE_DIR}/docs/static/samples/${id}")
        add_custom_target(${target}_page
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE_DIR:${target}>/${target}.html" "${destination}/index.html"
            DEPENDS ${target}
            COMMENT "Staging the ${id} sample game"
            VERBATIM)
        add_dependencies(sample_games ${target}_page)
    else()
        add_dependencies(sample_games ${target})
    endif()
endforeach()
