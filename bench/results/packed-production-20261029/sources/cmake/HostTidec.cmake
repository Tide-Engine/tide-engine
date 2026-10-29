# Web and Android builds generate their games' C with a native tidec: a
# WebAssembly or Android one couldn't run during the build. Android builds make
# their apps with a native tide_apk too. This builds tidec from the same sources for
# the machine doing the build, in <build>/host, before anything needs it.

include(ExternalProject)
set(_tide_host_dir "${CMAKE_BINARY_DIR}/host")
if(CMAKE_HOST_WIN32)
    set(TIDEC_PATH "${_tide_host_dir}/bin/tidec.exe")
    set(TIDE_APK_PATH "${_tide_host_dir}/bin/tide_apk.exe")
    set(TIDE_SPIRV_SHADERS_PATH "${_tide_host_dir}/bin/tide_spirv_shaders.exe")
else()
    set(TIDEC_PATH "${_tide_host_dir}/bin/tidec")
    set(TIDE_APK_PATH "${_tide_host_dir}/bin/tide_apk")
    set(TIDE_SPIRV_SHADERS_PATH "${_tide_host_dir}/bin/tide_spirv_shaders")
endif()

ExternalProject_Add(tidec_host
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${_tide_host_dir}"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DTIDE_TOOLS_ONLY=ON "-DTIDE_VERSION=${TIDE_VERSION}"
        "-DTIDE_PACKED_TABLES=${TIDE_PACKED_TABLES}"
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "${_tide_host_dir}" --target tidec tide_apk tide_spirv_shaders
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS "${TIDEC_PATH}" "${TIDE_APK_PATH}" "${TIDE_SPIRV_SHADERS_PATH}")

set(TIDEC_COMMAND "${TIDEC_PATH}")
set(TIDEC_DEPENDS tidec_host "${TIDEC_PATH}")
set(TIDE_SPIRV_SHADERS_DEPENDS tidec_host "${TIDE_SPIRV_SHADERS_PATH}")
