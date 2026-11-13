# Tidec's argument and file errors identify the failure and a way to proceed.
file(MAKE_DIRECTORY "${OUT}")
set(source "${OUT}/game.tide")
file(WRITE "${source}" "scene Main { }\n")

function(expect expected_result expected)
    execute_process(COMMAND "${TIDEC}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR "expected exit ${expected_result}, got ${result}:\n${stdout}${stderr}")
    endif()
    if("${stdout}${stderr}" MATCHES "AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:")
        message(FATAL_ERROR "a sanitizer's report:\n${stdout}${stderr}")
    endif()
    string(FIND "${stderr}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "expected '${expected}':\n${stdout}${stderr}")
    endif()
endfunction()

expect(2 "unknown option '--scheduel'; check its spelling" "${source}" --scheduel)
expect(2 "'-o' needs a value after it" "${source}" -o)
expect(2 "'--define' needs a value after it" "${source}" --define)
expect(2 "--source-overlay needs the original source path and a replacement file path" "${source}" --source-overlay "${source}")
expect(2 "'--scene' can be specified only once; remove the duplicate option" "${source}" --scene Main --scene Main)
expect(2 "'--depfile' can be specified only once; remove the duplicate option" "${source}" --depfile one.d --depfile two.d)
expect(2 "no source files; pass one or more .tide files" -o "${OUT}")
expect(2 "no output folder; pass '-o <output-dir>'" "${source}")
expect(2 "--depfile requires C generation; remove it for an inspection mode" "${source}" --schedule --depfile "${OUT}/game.d")
expect(2 "can't read the list of files '${OUT}/missing.list'; check that it exists and is readable" "@${OUT}/missing.list")
expect(2 "can't read the source overlay replacement '${OUT}/missing.tide'; check that it exists and is readable"
    "${source}" --source-overlay "${source}" "${OUT}/missing.tide" --schedule)
expect(1 "can't read source file '${OUT}/missing.tide'; check that it exists and is readable" "${OUT}/missing.tide" --schedule)
expect(1 "can't read '${OUT}/missing.json'; --measured takes the report `tide run --profile <report.json>` writes"
    "${source}" --schedule --measured "${OUT}/missing.json")
expect(1 "no measured systems in '${source}'; --measured takes the report" "${source}" --schedule --measured "${source}")
expect(1 "can't write dependency file '${OUT}/missing-parent/game.d'; create its parent folder and check write permissions"
    "${source}" -o "${OUT}" --depfile "${OUT}/missing-parent/game.d")
