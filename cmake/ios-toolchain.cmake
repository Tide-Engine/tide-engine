# iOS builds: the engine, the platform layer with no window of its own (platform/embedded: on iOS Tide runs inside
# another program's window, Unity's, through tide/embed.h) and their tests, for the iOS simulator on a Mac's own
# processor, or for devices (TIDE_IOS_SDK=iphoneos, which builds and can't run here).
#
# The compiler is Xcode's clang, which is what compiles the same sources in an Xcode project (a Unity player's):
# another clang has no iOS runtime to link programs with. Apple's SDK is Xcode's too, found by its name.
#
# Tests run in a booted simulator (cmake/run_ios.sh, which CMakeLists.txt names as the emulator).
set(CMAKE_SYSTEM_NAME iOS)
set(TIDE_IOS_SDK iphonesimulator CACHE STRING "iphonesimulator (tests run in it) or iphoneos (devices)")
set(CMAKE_OSX_SYSROOT "${TIDE_IOS_SDK}")
set(CMAKE_OSX_ARCHITECTURES arm64)
# The oldest iOS that Unity 6000.6's players run on
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "The oldest iOS builds run on")
# Plain programs, which the simulator starts by their path: not app bundles, CMake's default for iOS
set(CMAKE_MACOSX_BUNDLE OFF)
if(TIDE_IOS_SDK STREQUAL "iphoneos")
    # A device's program is signed before it links as one: CMake's checks compile a library instead
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TIDE_IOS_SDK)

execute_process(COMMAND xcrun --sdk "${TIDE_IOS_SDK}" --find clang OUTPUT_VARIABLE TIDE_CLANG
    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _tide_xcrun)
if(NOT _tide_xcrun EQUAL 0 OR NOT EXISTS "${TIDE_CLANG}")
    message(FATAL_ERROR "Xcode's clang for ${TIDE_IOS_SDK} not found (xcrun --sdk ${TIDE_IOS_SDK} --find clang): "
                        "iOS builds need a Mac with Xcode and its iOS platform installed.")
endif()
set(CMAKE_C_COMPILER "${TIDE_CLANG}")
