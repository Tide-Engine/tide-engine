# Private prototypes have the same native/web game path as the public demos.
# Their pages are staged beneath the existing server-side admin guard.
add_subdirectory(the-last-light)
add_custom_target(private_games)
if(TARGET last_light_page)
    add_dependencies(private_games last_light_page sample_two_forks_page)
elseif(TARGET last_light)
    add_dependencies(private_games last_light sample_two_forks)
endif()
