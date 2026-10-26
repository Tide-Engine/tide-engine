# tidec --declarations: the game's types build while its code doesn't, as tide unity builds them for the C# of a Unity
# project's components. A game whose handler spawns a Unity prefab that isn't made yet fails as a whole and builds
# declared alone, with its component and event in the header and none of its code; a component's own error still
# fails. Package functions/shaders remain real dependencies of package types/defaults.
# Run with TIDEC, OUT and CC; TARGET/SYSROOT optionally select the configured C target.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/out")
file(WRITE "${OUT}/game.tide" "scene Main { }\n"
     "component Ball { float bounce = 0.5; }\n"
     "event Bounced { float speed; }\n"
     "event(Spawned) Go(with Main) { Spawn(UnityEngine.Prefabs.Roller); }\n"
     "system Roll(mut Ball ball) { ball.bounce += Missing(); }\n"
     "float Twice(float x) { return x * Unknown; }\n")

function(compile expect_built)
    execute_process(
        COMMAND "${TIDEC}" "${OUT}/game.tide" ${package_sources} -o "${OUT}/out" --embed ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(expect_built AND NOT result EQUAL 0)
        message(FATAL_ERROR "tidec ${ARGN} didn't build:\n${stderr}")
    elseif(NOT expect_built AND result EQUAL 0)
        message(FATAL_ERROR "tidec ${ARGN} built a game it shouldn't have")
    endif()
    set(stderr "${stderr}" PARENT_SCOPE)
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

compile(FALSE)
compile(TRUE --declarations)
# Warnings it may have (nothing sends an event, with the code left out): tide unity runs it quietly
string(FIND "${stderr}" "error" said)
if(NOT said EQUAL -1)
    message(FATAL_ERROR "the declarations' build said an error:\n${stderr}")
endif()
file(READ "${OUT}/out/game.h" header)
foreach(name Ball Bounced)
    string(FIND "${header}" "typedef struct ${name} {" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "the declarations' header has no ${name}")
    endif()
endforeach()
file(READ "${OUT}/out/game.c" code)
foreach(name "Roll[^b]" Twice) # Not Prediction's Rollback
    string(REGEX MATCH "${name}" found "${code}")
    if(found)
        message(FATAL_ERROR "the declarations' code has ${name}, which is the game's code")
    endif()
endforeach()

file(APPEND "${OUT}/game.tide" "component Broken { Nothing value; }\n")
compile(FALSE --declarations)
string(FIND "${stderr}" "Nothing" said)
if(said EQUAL -1)
    message(FATAL_ERROR "a component's own error wasn't said:\n${stderr}")
endif()

# The actual package's Material holds Func defaults, a configured Lit binding
# and optional Deform. Keeping only data declarations used to erase their types
# and targets. Game code can still be incomplete while its declarations build.
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(package_sources --package Render3D
    "${root}/packages/render3d/pbr.tide"
    "${root}/packages/render3d/materials.tide"
    "${root}/packages/render3d/vertex.tide"
    "${root}/packages/render3d/post.tide"
    "${root}/packages/render3d/render3d.tide")
file(WRITE "${OUT}/game.tide"
    "scene Main;\n"
    "component Consumer { Asset<Render3D.SkinnedMesh3D> model; float value = 7; }\n"
    "event(Spawned) PendingGameHandler(with Main) { Spawn(UnityEngine.Prefabs.Pending); }\n"
    "float PendingGameHelper(float x) { return x + UnknownGameSymbol; }\n"
    "frag PendingGameShader(Vertex vertex, out Color color) { color = UnknownGameColor; }\n")
compile(FALSE)
compile(TRUE --declarations)
file(READ "${OUT}/out/game.h" header)
foreach(name Consumer Render3D_DirectionalLight Render3D_PointLight Render3D_SpotLight
        Render3D_MeshRenderer Render3D_SkinnedMeshRenderer Render3D_Material
        Render3D_Material_local Render3D_Renderer)
    string(FIND "${header}" "typedef struct ${name} {" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Render3D declarations have no ${name}")
    endif()
endforeach()
file(READ "${OUT}/out/game.c" code)
foreach(name PendingGameHandler PendingGameHelper PendingGameShader)
    string(FIND "${code}" "${name}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "declarations retained game executable ${name}")
    endif()
endforeach()
foreach(name StandardUV StandardCoverage StandardSurface)
    string(FIND "${code}" "tide_function_Render3D_${name}(" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "declarations lost real package callback ${name}")
    endif()
endforeach()

# Retained package draw helpers can plan/emit GPU code. This mode does not make
# package bodies into stubs; the Unity caller separately skips publishing shaders.
compile(TRUE --declarations --shaders)
string(FIND "${stdout}" "Lit" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the retained Render3D draw helpers no longer have their shader plan:\n${stdout}")
endif()

# A concrete C syntax check catches Func dispatchers which reference discarded
# callback definitions, even though the declarations library is never executed.
if(NOT CC)
    message(FATAL_ERROR "the declarations regression needs the configured clang in CC")
endif()
set(c_args -std=c17 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off -fsyntax-only
    -I "${root}/engine/include" -I "${root}/platform/include" -I "${OUT}/out")
if(NOT "${TARGET}" STREQUAL "")
    list(APPEND c_args "--target=${TARGET}")
endif()
if(SYSROOT)
    list(APPEND c_args "--sysroot=${SYSROOT}")
endif()
if("${TARGET}" MATCHES "wasm")
    list(APPEND c_args -pthread -msimd128)
endif()
execute_process(COMMAND "${CC}" ${c_args} "${OUT}/out/game.c"
    RESULT_VARIABLE result OUTPUT_VARIABLE c_stdout ERROR_VARIABLE c_stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Render3D declarations did not produce valid C17:\n${c_stdout}${c_stderr}")
endif()

# Assets and package bodies retain ordinary checking; neither is bypassed by
# signature retention. Keep these negatives after the successful C probe.
file(APPEND "${OUT}/game.tide" "asset string required = \"missing-declarations-asset.txt\";\n")
compile(FALSE --declarations)
string(FIND "${stderr}" "missing-declarations-asset.txt" found)
if(found EQUAL -1)
    message(FATAL_ERROR "declarations did not report its missing asset:\n${stderr}")
endif()
file(WRITE "${OUT}/game.tide" "scene Main;\ncomponent Consumer { float value; }\n")
file(WRITE "${OUT}/broken-package.tide" "namespace BrokenPackage;\nfloat Broken(float x) { return x + UnknownPackageSymbol; }\n")
list(APPEND package_sources --package BrokenPackage "${OUT}/broken-package.tide")
compile(FALSE --declarations)
string(FIND "${stderr}" "UnknownPackageSymbol" found)
if(found EQUAL -1)
    message(FATAL_ERROR "declarations did not check the retained package body:\n${stderr}")
endif()
