# tide upgrade, from a release in a folder that stands in for GitHub
# (TIDE_RELEASES_URL): an upgrade to a newer version, and an installation an
# upgrade left partway, which tide builds nothing with and `tide upgrade`
# installs again, though its tide is the newest version already. The tide that
# upgrades is running, so it replaces its own program.
#
# The packages have no editor extension, so no editor on this machine is
# touched.
#
# cmake -DTIDE=<the tide program> -DWORK=<a scratch folder> -P upgrade_test.cmake

cmake_minimum_required(VERSION 3.25)
foreach(var TIDE WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "Pass -D${var}=...")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
get_filename_component(tide_name "${TIDE}" NAME)

# The package's name for this machine, as upgrade.c has it.
if(WIN32)
    set(package tide-windows-x64.zip)
    set(archive cf)
    set(format --format=zip)
else()
    set(archive czf)
    set(format "")
    if(APPLE)
        set(package tide-macos-arm64.tar.gz)
    else()
        set(package tide-linux-x64.tar.gz)
    endif()
endif()

function(file_url out path)
    if(path MATCHES "^/")
        set(${out} "file://${path}" PARENT_SCOPE)
    else()
        set(${out} "file:///${path}" PARENT_SCOPE)
    endif()
endfunction()

function(run_tide install result_var output_var)
    execute_process(COMMAND "${install}/bin/${tide_name}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

function(check_output output)
    foreach(wanted ${ARGN})
        string(FIND "${output}" "${wanted}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "tide didn't say \"${wanted}\":\n${output}")
        endif()
    endforeach()
endfunction()

function(check_file file wanted)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "${file} isn't there")
    endif()
    file(READ "${file}" text)
    if(NOT text STREQUAL wanted)
        message(FATAL_ERROR "${file} says \"${text}\", not \"${wanted}\"")
    endif()
endfunction()

# An installation of `version`, with this tide as its program, a header the
# next package has none of, and what isn't a package's.
function(make_installation install version)
    file(COPY "${TIDE}" DESTINATION "${install}/bin")
    file(WRITE "${install}/include/tide/run.h" "old run.h")
    file(WRITE "${install}/include/tide/gone.h" "a header the release has none of")
    file(WRITE "${install}/lib/native/libtide_platform.a" "old platform")
    file(WRITE "${install}/VERSION" "${version}\n")
    file(WRITE "${install}/packages/dice/dice.tide" "a game's package")
endfunction()

# A release of `version` in `dir`, with its package, checksums and the list
# GitHub would give; the list's URL goes in TIDE_RELEASES_URL.
function(make_release dir version)
    set(stage "${dir}/tide")
    file(COPY "${TIDE}" DESTINATION "${stage}/bin")
    file(WRITE "${stage}/include/tide/run.h" "new run.h")
    file(WRITE "${stage}/lib/native/libtide_platform.a" "new platform")
    file(WRITE "${stage}/web/tide.js" "new tide.js")
    file(WRITE "${stage}/VERSION" "${version}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar ${archive} "${dir}/${package}" ${format} bin include lib web VERSION
        WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Making ${dir}/${package} failed")
    endif()
    file(SHA256 "${dir}/${package}" hash)
    file(WRITE "${dir}/SHA256SUMS" "${hash}  ${package}\n")
    file(SIZE "${dir}/${package}" size)
    file_url(package_url "${dir}/${package}")
    file_url(sums_url "${dir}/SHA256SUMS")
    file(WRITE "${dir}/releases.json" "[{\"tag_name\": \"v${version}\", \"prerelease\": true, \"draft\": false, "
        "\"html_url\": \"https://example.invalid/v${version}\", \"assets\": ["
        "{\"name\": \"${package}\", \"size\": ${size}, \"browser_download_url\": \"${package_url}\"}, "
        "{\"name\": \"SHA256SUMS\", \"size\": 1, \"browser_download_url\": \"${sums_url}\"}]}]")
    file_url(releases_url "${dir}/releases.json")
    set(ENV{TIDE_RELEASES_URL} "${releases_url}")
endfunction()

# The release's files are the installation's, and nothing of the old version is.
function(check_installed install version)
    check_file("${install}/include/tide/run.h" "new run.h")
    check_file("${install}/lib/native/libtide_platform.a" "new platform")
    check_file("${install}/web/tide.js" "new tide.js")
    check_file("${install}/VERSION" "${version}\n")
    check_file("${install}/packages/dice/dice.tide" "a game's package")
    check_file("${install}/channel" "nightly")
    if(EXISTS "${install}/include/tide/gone.h")
        message(FATAL_ERROR "The upgrade left a header the release has none of")
    endif()
    if(EXISTS "${install}/.upgrade")
        message(FATAL_ERROR "The upgrade left its work folder")
    endif()
endfunction()

# This tide's version: "tide 0.3.0-dev (nightly)".
execute_process(COMMAND "${TIDE}" version OUTPUT_VARIABLE said RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT said MATCHES "^tide ([^ ]+) ")
    message(FATAL_ERROR "tide version failed (${result}):\n${said}")
endif()
set(version "${CMAKE_MATCH_1}")

# --- An upgrade to a newer version.
set(install "${WORK}/newer/Tide")
make_installation("${install}" "${version}")
make_release("${WORK}/newer/release" "99.0.0")
run_tide("${install}" result output upgrade)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tide upgrade failed (${result}):\n${output}")
endif()
check_output("${output}" "Downloading tide 99.0.0" "Upgraded tide ${version} -> 99.0.0 (nightly).")
check_installed("${install}" "99.0.0")

# --- The newest version already, and whole: nothing to do.
set(install "${WORK}/same/Tide")
make_installation("${install}" "${version}")
make_release("${WORK}/same/release" "${version}")
run_tide("${install}" result output upgrade)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tide upgrade failed (${result}):\n${output}")
endif()
check_output("${output}" "tide ${version} is the newest nightly version.")
check_file("${install}/include/tide/run.h" "old run.h")

# --- The same, after an upgrade that stopped partway: the new tide is in, and
# most of what it builds with isn't.
file(REMOVE_RECURSE "${install}/lib")
file(REMOVE "${install}/VERSION")
run_tide("${install}" result output version)
check_output("${output}" "isn't whole" "tide upgrade")
file(WRITE "${WORK}/same/game/game.tide" "")
run_tide("${install}" result output build "${WORK}/same/game")
if(result EQUAL 0)
    message(FATAL_ERROR "tide built a game with an installation that isn't whole:\n${output}")
endif()
check_output("${output}" "isn't whole" "run `tide upgrade`")

run_tide("${install}" result output upgrade)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tide upgrade didn't finish an upgrade that stopped (${result}):\n${output}")
endif()
check_output("${output}" "isn't whole" "Downloading tide ${version}" "Installed tide ${version} again (nightly).")
check_installed("${install}" "${version}")
run_tide("${install}" result output version)
string(FIND "${output}" "isn't whole" at)
if(NOT result EQUAL 0 OR NOT at EQUAL -1)
    message(FATAL_ERROR "The installation still isn't whole after tide upgrade (${result}):\n${output}")
endif()

message(STATUS "tide upgrade: ok")
