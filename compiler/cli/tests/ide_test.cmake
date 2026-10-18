cmake_minimum_required(VERSION 3.25)

# Install the actual editor payload, with spaces, quotes and an option separator
# in its path. The helper must print valid editor code without touching dotfiles.
set(install "${WORK}/Tide's $ editor, config")
file(MAKE_DIRECTORY "${install}/bin" "${WORK}/home" "${WORK}/project")
get_filename_component(tide_name "${TIDE}" NAME)
get_filename_component(server_name "${TIDELS}" NAME)
file(COPY_FILE "${TIDE}" "${install}/bin/${tide_name}")
file(COPY_FILE "${TIDELS}" "${install}/bin/${server_name}")
set(tide "${install}/bin/${tide_name}")
set(ENV{HOME} "${WORK}/home")
set(ENV{USERPROFILE} "${WORK}/home")
set(ENV{LOCALAPPDATA} "${WORK}/home")
file(WRITE "${WORK}/home/init.el" ";; keep my settings\n")
file(WRITE "${WORK}/home/.vimrc" "\" keep my settings\n")

function(run expected)
    execute_process(COMMAND "${tide}" ide ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 5)
    if(NOT "${code}" STREQUAL "${expected}")
        message(FATAL_ERROR "tide ide ${ARGN}: expected ${expected}, got ${code}: ${out}${err}")
    endif()
    if(expected EQUAL 0 AND NOT err STREQUAL "")
        message(FATAL_ERROR "Unexpected stderr: ${err}")
    elseif(NOT expected EQUAL 0 AND NOT out STREQUAL "")
        message(FATAL_ERROR "Failed helper printed partial configuration: ${out}")
    endif()
    set(out "${out}" PARENT_SCOPE)
    set(err "${err}" PARENT_SCOPE)
endfunction()

function(contains text needle)
    string(FIND "${text}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Missing '${needle}' in: ${text}")
    endif()
endfunction()

run(2)
contains("${err}" "emacs, vim or neovim")
run(2 unknown)
run(2 emacs extra)
run(2 --install)
file(REMOVE "${install}/editors/tide-emacs/tide-lang-mode.el")
run(1 emacs)
contains("${err}" "reinstall tide")

# Exercise tools/CMakeLists.txt's real installation rules, not a hand-copied
# approximation of what a package should contain.
file(WRITE "${WORK}/project/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\nproject(EditorFiles NONE)\n"
    "set(TIDE_EDITOR_EXTENSIONS OFF CACHE BOOL \"\" FORCE)\n"
    "add_subdirectory(\"${SOURCE}/tools\" editors)\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${WORK}/project" -B "${WORK}/build"
    RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "Editor payload configure failed: ${log}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${WORK}/build" --prefix "${install}" --component tide
    RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "Editor payload install failed: ${log}")
endif()
foreach(editor emacs vim)
    if(EXISTS "${install}/editors/tide-${editor}/test")
        message(FATAL_ERROR "Editor tests must not ship in the package")
    endif()
endforeach()

foreach(editor emacs vim neovim)
    run(0 "${editor}")
    set(first "${out}")
    contains("${out}" "${install}/bin/${server_name}")
    if(editor STREQUAL "emacs")
        set(extension el)
        contains("${out}" "${install}/editors/tide-emacs")
        contains("${out}" "eglot-ensure")
    elseif(editor STREQUAL "vim")
        set(extension vim)
        contains("${out}" "${install}/editors/tide-vim")
        contains("${out}" "let &runtimepath = escape(")
    else()
        set(extension lua)
        contains("${out}" "${install}/editors/tide-vim")
        contains("${out}" "vim.lsp.enable('tidels')")
    endif()
    file(WRITE "${WORK}/${editor}.${extension}" "${out}")
    run(0 "${editor}")
    if(NOT out STREQUAL first)
        message(FATAL_ERROR "Repeated setup changed the configuration")
    endif()
endforeach()

# Optional real editor coverage. Pass explicit executables through these
# environment variables; ordinary CI needs no editor installations. Personal
# configuration/plugins are excluded. Generated snippets load installed files.
if(DEFINED ENV{TIDE_TEST_EMACS})
    set(ENV{TIDELS} "${install}/bin/${server_name}")
    execute_process(COMMAND "$ENV{TIDE_TEST_EMACS}" -Q --batch -l "${WORK}/emacs.el"
        --eval "(progn (unless (memq 'eglot-ensure tide-lang-mode-hook) (error \"Missing automatic setup\")) (unless (equal (car tide-lang-server-command) (getenv \"TIDELS\")) (error \"Wrong server path\")) (setq tide-lang-mode-hook nil))"
        -l "${SOURCE}/tools/tide-emacs/test/tide-lang-mode-test.el" -f ert-run-tests-batch-and-exit
        RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE log TIMEOUT 30)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Generated Emacs setup failed: ${log}")
    endif()
endif()
if(DEFINED ENV{TIDE_TEST_VIM})
    execute_process(COMMAND "$ENV{TIDE_TEST_VIM}" -N -u "${WORK}/vim.vim" --noplugin -i NONE -n -es
        -S "${CMAKE_CURRENT_LIST_DIR}/ide_config.vim"
        RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE log TIMEOUT 30)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Generated Vim setup failed: ${log}")
    endif()
endif()
if(DEFINED ENV{TIDE_TEST_NVIM})
    execute_process(COMMAND "$ENV{TIDE_TEST_NVIM}" --headless --noplugin -i NONE -u "${WORK}/neovim.lua"
        -l "${CMAKE_CURRENT_LIST_DIR}/ide_config.lua"
        RESULT_VARIABLE code OUTPUT_VARIABLE log ERROR_VARIABLE log TIMEOUT 30)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Generated Neovim setup failed: ${log}")
    endif()
endif()

foreach(entry "emacs|tide-emacs/tide-lang-mode.el" "vim|tide-vim/ftdetect/tide.vim"
              "vim|tide-vim/ftplugin/tide.vim" "vim|tide-vim/syntax/tide.vim" "neovim|tide-vim/lsp/tidels.lua")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 editor)
    list(GET parts 1 relative)
    set(path "${install}/editors/${relative}")
    file(RENAME "${path}" "${path}.saved")
    run(1 "${editor}")
    contains("${err}" "${path}")
    file(RENAME "${path}.saved" "${path}")
endforeach()
file(RENAME "${install}/bin/${server_name}" "${install}/bin/${server_name}.saved")
run(1 neovim)
contains("${err}" "${server_name}")
file(RENAME "${install}/bin/${server_name}.saved" "${install}/bin/${server_name}")

# Vim expands wildcard characters in runtime directories on Windows as well as
# Unix. A legal filesystem path must not produce a silently unusable snippet.
set(bracketed "${WORK}/[wildcard]")
file(MAKE_DIRECTORY "${bracketed}/bin")
file(COPY_FILE "${TIDE}" "${bracketed}/bin/${tide_name}")
file(COPY_FILE "${TIDELS}" "${bracketed}/bin/${server_name}")
file(COPY "${install}/editors" DESTINATION "${bracketed}")
set(tide "${bracketed}/bin/${tide_name}")
run(0 emacs)
foreach(editor vim neovim)
    run(1 "${editor}")
    contains("${err}" "install tide in a folder without")
endforeach()

file(READ "${WORK}/home/init.el" init)
file(READ "${WORK}/home/.vimrc" vimrc)
if(NOT init STREQUAL ";; keep my settings\n" OR NOT vimrc STREQUAL "\" keep my settings\n")
    message(FATAL_ERROR "The helper changed an existing editor configuration")
endif()
