# Prototypes have the same native/web game path as the public demos.
# Catalog games are staged at public addresses that can be shared.
add_subdirectory(the-last-light)
# Dead Signal remains a local opt-in prototype, outside catalog staging.
if(TIDE_PRIVATE_PROTOTYPES)
    add_subdirectory(dead-signal)
endif()
add_custom_target(private_games)
if(TARGET last_light_page)
    add_dependencies(private_games last_light_page sample_two_forks_page)
elseif(TARGET last_light)
    add_dependencies(private_games last_light sample_two_forks)
endif()
if(TARGET cabin_nine_page)
    add_dependencies(private_games cabin_nine_page)
elseif(TARGET cabin_nine)
    add_dependencies(private_games cabin_nine)
endif()
