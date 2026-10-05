# Each example is a separate game: its displayed source is exactly what runs.
file(GLOB TIDE_DOC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
add_custom_target(docs_examples)
foreach(source IN LISTS TIDE_DOC_SOURCES)
    get_filename_component(id "${source}" NAME_WE)
    string(REPLACE "-" "_" name "${id}")
    set(target "docs_example_${name}")
    tide_add_game(${target} SOURCES "${source}" TITLE "Tide: ${id}")
    if(TIDE_WEB)
        # Copy on every invocation, including when the game is already built,
        # so a cleaned docs/static/ can always be restored by this target.
        set(destination "${PROJECT_SOURCE_DIR}/docs/static/examples/${id}")
        add_custom_target(${target}_page
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE_DIR:${target}>/${target}.html" "${destination}/index.html"
            DEPENDS ${target}
            COMMENT "Staging the docs' ${id} example"
            VERBATIM)
        add_dependencies(docs_examples ${target}_page)
    else()
        add_dependencies(docs_examples ${target})
    endif()
endforeach()
