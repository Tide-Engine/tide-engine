# The platform layer's tests in a build with no window of its own (TIDE_IOS: platform/embedded), included by
# platform/CMakeLists.txt in place of the rest of it. Each is the test of the same name there, which says what it
# checks: keep the two alike.

tide_add_game(tide_platform_migrate SOURCES tests/migrate.tide NAME game LAYOUT
    HOST tests/migrate.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_migrate PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_migrate PRIVATE tide_platform)
add_test(NAME tide_platform_migrate COMMAND tide_platform_migrate)

tide_add_game(tide_platform_jobs SOURCES tests/jobs.tide tests/jobs_tasks.c NAME game
    HOST tests/jobs.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_jobs PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_jobs PRIVATE tide_platform)
add_test(NAME tide_platform_jobs COMMAND tide_platform_jobs)
set_tests_properties(tide_platform_jobs PROPERTIES TIMEOUT 120)

# Tide inside another program's frame: what an iOS game under Unity is
foreach(embedded embed travel embed_shards)
    tide_add_game(tide_platform_${embedded} SOURCES tests/${embedded}.tide NAME game EMBED
        HOST tests/${embedded}.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tide_platform_${embedded} PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
    target_link_libraries(tide_platform_${embedded} PRIVATE tide_platform)
    add_test(NAME tide_platform_${embedded} COMMAND tide_platform_${embedded})
    set_tests_properties(tide_platform_${embedded} PROPERTIES TIMEOUT 120)
endforeach()

add_executable(tide_platform_font tests/font.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_font PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
target_link_libraries(tide_platform_font PRIVATE tide_platform)
add_test(NAME tide_platform_font COMMAND tide_platform_font)

add_executable(tide_platform_audio tests/audio.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_audio PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
target_link_libraries(tide_platform_audio PRIVATE tide_platform tide_audio_refs)
add_test(NAME tide_platform_audio COMMAND tide_platform_audio)

add_executable(tide_platform_io tests/io.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_io PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_io PRIVATE tide_platform)
add_test(NAME tide_platform_io COMMAND tide_platform_io)

add_executable(tide_platform_rtc tests/rtc.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_rtc PRIVATE "${PROJECT_SOURCE_DIR}/tests" src)
target_link_libraries(tide_platform_rtc PRIVATE tide_platform)
add_test(NAME tide_platform_rtc COMMAND tide_platform_rtc)
set_tests_properties(tide_platform_rtc PROPERTIES ENVIRONMENT TIDE_RTC_LOCAL=1)

add_executable(tide_platform_rtc_fuzz tests/rtc_fuzz.c)
target_include_directories(tide_platform_rtc_fuzz PRIVATE src)
target_link_libraries(tide_platform_rtc_fuzz PRIVATE tide_platform)
add_test(NAME tide_platform_rtc_fuzz COMMAND tide_platform_rtc_fuzz 2000)
set_tests_properties(tide_platform_rtc_fuzz PROPERTIES ENVIRONMENT TIDE_RTC_LOCAL=1)

add_executable(tide_platform_udp tests/udp.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
target_include_directories(tide_platform_udp PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_platform_udp PRIVATE tide_platform)
add_test(NAME tide_platform_udp COMMAND tide_platform_udp)
