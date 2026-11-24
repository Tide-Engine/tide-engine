# The tests an iOS build runs in the simulator (the ios-simulator preset; CI's iOS job): the engine's own, the
# platform layer's with no window (platform/tests/ios.cmake), and the compiler's fixtures that pin a simulation's
# bits or run a match, compiled by Xcode's clang as a Unity player's Xcode project compiles them. Not every e2e
# fixture: Apple's clang takes minutes over some games' generated C.
#
#   cmake --build --preset ios-simulator --target tide_ios_tests
#   ctest --preset ios-simulator -R "$(cat build/ios-simulator/ios_tests.txt)"
set(TIDE_IOS_TESTS
    tide_tests
    tide_platform_migrate tide_platform_jobs tide_platform_embed tide_platform_travel tide_platform_embed_shards
    tide_platform_font tide_platform_audio tide_platform_io tide_platform_rtc tide_platform_rtc_fuzz tide_platform_udp)
foreach(fixture crossplatform crossplatform_text random random_local_order random_net rolls math double float_bits
        vector_equality edge_cases transforms net predict delta handover skinned baked_poses retargeting storage
        shards visibility_repairs tasks parallel sort maps)
    list(APPEND TIDE_IOS_TESTS tidec_e2e_${fixture})
endforeach()
# The physics packages' C, which a game that lists one compiles with the same compiler: their pinned hashes
list(APPEND TIDE_IOS_TESTS tide_box2d_stack tide_box2d_course tide_box2d_complete tide_box2d_rewind
    tide_box3d_stack tide_box3d_course tide_box3d_complete tide_box3d_rewind tide_box3d_joints tide_box3d_mesh)
foreach(test IN LISTS TIDE_IOS_TESTS)
    if(NOT TARGET ${test})
        message(FATAL_ERROR "cmake/ios_tests.cmake lists ${test}, which this build has no target of")
    endif()
endforeach()
add_custom_target(tide_ios_tests DEPENDS ${TIDE_IOS_TESTS} tide_ios_run)
list(JOIN TIDE_IOS_TESTS "|" tide_ios_regex)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/ios_tests.txt" CONTENT "^(${tide_ios_regex})$\n" @ONLY)
