if(NOT DEFINED GAME_C OR NOT EXISTS "${GAME_C}")
    message(FATAL_ERROR "GAME_C must name the generated readonly_aggregates C file")
endif()
file(READ "${GAME_C}" generated)
# A helper that only reads a resource's size (calls_c, no extern) borrows its aggregate
if(NOT generated MATCHES "tide_function_Measured\\([^)]*const Big \\*value")
    message(FATAL_ERROR "Measured copies its read-only Big though nothing it calls could change it")
endif()
# One that reaches the game's C keeps its value snapshot
if(generated MATCHES "tide_function_WithC\\([^)]*const Big \\*value")
    message(FATAL_ERROR "WithC borrows its Big though the C it calls can change the original")
endif()
