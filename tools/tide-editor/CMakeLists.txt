add_library(tide_editor_document STATIC document.c
    "${PROJECT_SOURCE_DIR}/compiler/cli/sys.c"
    "${PROJECT_SOURCE_DIR}/compiler/lsp/packages.c")
target_include_directories(tide_editor_document PUBLIC .
    "${PROJECT_SOURCE_DIR}/compiler/cli" "${PROJECT_SOURCE_DIR}/compiler/lsp")
target_link_libraries(tide_editor_document PUBLIC tidec_core)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(tide_editor_document PUBLIC Threads::Threads)
endif()

add_executable(tide_editor main.c model.c)
set_target_properties(tide_editor PROPERTIES OUTPUT_NAME tide-editor)
target_compile_definitions(tide_editor PRIVATE "TIDE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(tide_editor PRIVATE tide_editor_document tide_platform)
if(WIN32)
    add_custom_command(TARGET tide_editor PRE_LINK
        COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:tide_editor>"
            -P "${PROJECT_SOURCE_DIR}/compiler/lsp/move_aside.cmake" VERBATIM)
endif()
install(TARGETS tide_editor RUNTIME DESTINATION bin COMPONENT tide)

add_executable(tide_editor_tests tests/document_test.c tests/model_test.c model.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_editor_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_compile_definitions(tide_editor_tests PRIVATE "TIDE_EDITOR_TEST_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/document-tests\"")
target_link_libraries(tide_editor_tests PRIVATE tide_editor_document tide)
add_test(NAME tide_editor_document COMMAND tide_editor_tests)
if(TIDE_NODE)
    foreach(mode cli ui)
        add_test(NAME tide_editor_${mode}
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor.mjs"
                "$<TARGET_FILE:tide_cli>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project"
                "${CMAKE_CURRENT_BINARY_DIR}/editor-${mode}" ${mode})
        set_tests_properties(tide_editor_${mode} PROPERTIES TIMEOUT 120)
    endforeach()
    set_tests_properties(tide_editor_ui PROPERTIES LABELS gpu)
endif()
