# Each example is a separate game: its displayed source is exactly what runs.

# An example that uses a package that comes with tide (packages/, see
# AGENTS.md, Packaging and releases) is named here with the package's folder,
# as tide_add_game reads no tide.packages. It's built with the package as tide
# builds a game that lists it: the package's Tide files before its own, the
# package's C with its own, and the library's sources, which packages/ builds
# once for its tests (tide_<folder>).
set(TIDE_DOC_PACKAGE_physics-2d box2d)
set(TIDE_DOC_PACKAGE_physics-3d box3d)

file(GLOB TIDE_DOC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
add_custom_target(docs_examples)
foreach(source IN LISTS TIDE_DOC_SOURCES)
    get_filename_component(id "${source}" NAME_WE)
    string(REPLACE "-" "_" name "${id}")
    set(target "docs_example_${name}")
    set(package "${TIDE_DOC_PACKAGE_${id}}")
    if(package)
        set(folder "${PROJECT_SOURCE_DIR}/packages/${package}")
        if(NOT TARGET tide_${package})
            message(FATAL_ERROR "The docs' ${id} example is built with the package ${package}: add packages/ before "
                                "docs/examples, and its library there as tide_${package}")
        endif()
        # The package's name, which is its namespace: `package Box2D`, its tide.packages' first line
        file(STRINGS "${folder}/tide.packages" named LIMIT_COUNT 1 REGEX "^package ")
        string(REGEX REPLACE "^package +" "" namespace "${named}")
        file(GLOB package_tide CONFIGURE_DEPENDS "${folder}/*.tide")
        file(GLOB package_c CONFIGURE_DEPENDS "${folder}/*.c")
        tide_add_game(${target} SOURCES "${source}" ${package_c} TITLE "Tide: ${id}" PACKAGES ${namespace} ${package_tide})
        target_link_libraries(${target} PRIVATE tide_${package})
    else()
        tide_add_game(${target} SOURCES "${source}" TITLE "Tide: ${id}")
    endif()
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
