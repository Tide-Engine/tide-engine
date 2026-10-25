# tide with packages, without the network: packages in folders, and packages
# from git through TIDE_GIT_MIRROR, a folder that stands in for every git host
# (compiler/cli/fetch.c), with each repository's list of branches and tags and
# an archive of each commit. `tide schedule` runs the compiler's front end on
# a game, packages and all, which needs no C compiler.
#
# cmake -DTIDE=<the tide program> -DWORK=<a scratch folder> -P packages_test.cmake

cmake_minimum_required(VERSION 3.25)
foreach(var TIDE WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "Pass -D${var}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
set(mirror "${WORK}/mirror")
set(ENV{TIDE_PACKAGES} "${WORK}/cache")
set(ENV{TIDE_GIT_MIRROR} "${mirror}")
set(ENV{TIDE_NO_UPDATE_CHECK} 1)

# Runs tide in `dir`, and checks how it ended, that what it said has each of
# the texts after EXPECT, and none of those after WITHOUT.
function(tide dir)
    cmake_parse_arguments(ARG "FAILS" "" "ARGS;EXPECT;WITHOUT" ${ARGN})
    execute_process(COMMAND "${TIDE}" ${ARG_ARGS} WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(said "${out}${err}")
    if(ARG_FAILS AND code EQUAL 0)
        message(FATAL_ERROR "tide ${ARG_ARGS} should have failed, and said:\n${said}")
    elseif(NOT ARG_FAILS AND NOT code EQUAL 0)
        message(FATAL_ERROR "tide ${ARG_ARGS} failed (${code}):\n${said}")
    endif()
    foreach(text IN LISTS ARG_EXPECT)
        string(FIND "${said}" "${text}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "tide ${ARG_ARGS} should have said '${text}', and said:\n${said}")
        endif()
    endforeach()
    foreach(text IN LISTS ARG_WITHOUT)
        string(FIND "${said}" "${text}" at)
        if(NOT at EQUAL -1)
            message(FATAL_ERROR "tide ${ARG_ARGS} shouldn't have said '${text}', and said:\n${said}")
        endif()
    endforeach()
endfunction()

function(expect_file path)
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}") # file(WRITE) writes "\r\n" on Windows
    foreach(want IN LISTS ARGN)
        string(FIND "${text}" "${want}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "${path} should have '${want}', and has:\n${text}")
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Symbols on the command line: every accepted repeat reaches the compiler,
# including more than 63 of them. DEBUG still follows mode.

file(WRITE "${WORK}/defines/main.tide" "
#if FLAG0 && FLAG34 && FLAG69
scene Main { }
#else
singleton MissingFlag { int value = missing_late_symbol; }
#endif
#if EXPECT_DEBUG && !DEBUG || EXPECT_RELEASE && DEBUG
singleton WrongMode { int value = wrong_debug_symbol; }
#endif
")
set(define_args)
foreach(i RANGE 0 69)
    list(APPEND define_args --define "FLAG${i}")
endforeach()
tide("${WORK}/defines" ARGS schedule . ${define_args} --define EXPECT_DEBUG EXPECT "defines: 0 systems")
tide("${WORK}/defines" ARGS schedule . --release ${define_args} --define EXPECT_RELEASE EXPECT "defines: 0 systems")

# ---------------------------------------------------------------------------
# Packages in folders

set(games "${WORK}/games")
file(WRITE "${games}/maths/tide.packages" "package Maths\n")
file(WRITE "${games}/maths/step.tide" "namespace Maths;\n\nfloat Step()\n{\n    return 0.5;\n}\n")
file(WRITE "${games}/physics/tide.packages" "package Physics\n../maths\n")
file(WRITE "${games}/physics/body.tide" "namespace Physics;

component Body
{
    float3 position;
    float3 velocity;
}

system Integrate(mut Body body)
{
    body.position += body.velocity * Maths.Step();
}
")
# A game in the package's folder, to try it: not one of the package's files.
file(WRITE "${games}/physics/example/tide.packages" "..\n../../maths\n")
file(WRITE "${games}/physics/example/main.tide" "using Physics;\n\nscene Main { }\n\nevent(Spawned) Setup(with Main)\n{\n    Spawn(Body { });\n}\n")
file(WRITE "${games}/game/main.tide" "using Physics;

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Body { velocity = float3(0, 1, 0) });
}

system Fall(mut Body body)
{
    body.velocity.y -= 1;
}
")

# tide add puts the package in, and what it needs.
tide("${games}/game" ARGS add ../physics EXPECT "added Physics, from ../physics" "added ../maths, which Physics needs")
expect_file("${games}/game/tide.packages" "../physics\n../maths\n")
tide("${games}/game" ARGS add ../physics EXPECT "Physics is in tide.packages already")
# Packages' files come first, each after those it needs.
tide("${games}/game" ARGS schedule . EXPECT "stage 1  Physics.Integrate" "stage 2  Fall")
tide("${games}/physics/example" ARGS schedule . EXPECT "example: 1 system")
tide("${games}/physics" ARGS schedule . FAILS EXPECT "is package Physics, not a game")

# Accepted package paths are preserved in full, beyond 1024 bytes.
# Redundant parts keep this portable to short OS paths.
file(WRITE "${games}/long-source/main.tide" "scene Main { }\n")
string(REPEAT "./" 600 repeated)
set(long_source "../${repeated}maths")
tide("${games}/long-source" ARGS add "${long_source}" EXPECT "added Maths")
file(READ "${games}/long-source/tide.packages" written)
string(REPLACE "\r\n" "\n" written "${written}")
if(NOT written STREQUAL "${long_source}\n")
    message(FATAL_ERROR "adding a long package source didn't preserve it: ${written}")
endif()
tide("${games}/long-source" ARGS schedule . EXPECT "long-source: 0 systems")
tide("${games}/long-source" ARGS add "${long_source}" EXPECT "Maths is in tide.packages already")

# What's wrong with a game's packages.
file(WRITE "${games}/game/tide.packages" "../physics\n")
tide("${games}/game" ARGS schedule . FAILS
    EXPECT "tide.packages:1: error: Physics needs ../maths, which this file doesn't list" "add this line: ../maths")
file(WRITE "${games}/game/tide.packages" "../physics\n../maths\n../nothing\n")
file(MAKE_DIRECTORY "${games}/nothing")
tide("${games}/game" ARGS schedule . FAILS EXPECT "tide.packages:3: error: ../nothing isn't a package: it has no tide.packages")
file(WRITE "${games}/game/tide.packages" "../physics\n../maths\ntide 99.1\n")
tide("${games}/game" ARGS schedule . FAILS EXPECT "error: this needs tide 99.1 or newer")
file(WRITE "${games}/game/tide.packages" "../physics\n../maths\n")
file(WRITE "${games}/maths/main.tide" "namespace Maths;\n\nscene Main { }\n")
tide("${games}/game" ARGS schedule . FAILS EXPECT "package Maths can't declare Main: the game that uses it does")
file(WRITE "${games}/maths/main.tide" "struct Pair { float a; }\n")
tide("${games}/game" ARGS schedule . FAILS EXPECT "what package Maths declares goes in its namespace, Maths")
file(REMOVE "${games}/maths/main.tide")

# Adding a package checks its metadata before changing the game's file.
file(WRITE "${games}/invalid/tide.packages" "# Keep the game's file.\n")
file(WRITE "${games}/bad-package/tide.packages" "package Broken\nthis has three words\n")
tide("${games}/invalid" ARGS add ../bad-package FAILS
    EXPECT "tide.packages:2: error: this line has more than two words" WITHOUT "tide: added")
file(READ "${games}/invalid/tide.packages" unchanged)
string(REPLACE "\r\n" "\n" unchanged "${unchanged}")
if(NOT unchanged STREQUAL "# Keep the game's file.\n")
    message(FATAL_ERROR "adding invalid package metadata changed tide.packages: ${unchanged}")
endif()

# Dependency lines are still added automatically, but an unusable package
# graph must fail with its diagnostic, in add and in update alike.
file(WRITE "${games}/dependencies/main.tide" "scene Main { }\n")
file(WRITE "${games}/parent/tide.packages" "package Parent\n../child\n")
tide("${games}/dependencies" ARGS add ../parent FAILS
    EXPECT "added ../child, which Parent needs" "tide.packages:2: error: ../child isn't a folder")
expect_file("${games}/dependencies/tide.packages" "../parent\n../child\n")
tide("${games}/dependencies" ARGS add ../parent FAILS
    EXPECT "Parent is in tide.packages already" "tide.packages:2: error: ../child isn't a folder")
tide("${games}/dependencies" ARGS update FAILS EXPECT "tide.packages:2: error: ../child isn't a folder")
file(WRITE "${games}/child/tide.packages" "package Child\nthis has three words\n")
tide("${games}/dependencies" ARGS update FAILS EXPECT "tide.packages:2: error: this line has more than two words")
file(WRITE "${games}/child/tide.packages" "package Child\n")
tide("${games}/dependencies" ARGS update EXPECT "no packages from git to update")
tide("${games}/dependencies" ARGS schedule . EXPECT "dependencies: 0 systems")

# ---------------------------------------------------------------------------
# Packages from git

# One of git's packet lines: its length in four hex digits, then the text.
function(packet out text)
    string(LENGTH "${text}" n)
    math(EXPR n "${n} + 4" OUTPUT_FORMAT HEXADECIMAL)
    string(SUBSTRING "${n}" 2 -1 hex)
    string(LENGTH "${hex}" digits)
    while(digits LESS 4)
        string(PREPEND hex "0")
        math(EXPR digits "${digits} + 1")
    endwhile()
    set(${out} "${${out}}${hex}${text}" PARENT_SCOPE)
endfunction()

# A repository in the mirror: its default branch is the first ref.
function(repository repo)
    set(refs "")
    packet(refs "# service=git-upload-pack\n")
    string(APPEND refs "0000")
    list(GET ARGN 1 head)
    packet(refs "${head} HEAD\n")
    list(LENGTH ARGN n)
    math(EXPR last "${n} - 1")
    foreach(i RANGE 0 ${last} 2)
        math(EXPR j "${i} + 1")
        list(GET ARGN ${i} name)
        list(GET ARGN ${j} commit)
        packet(refs "${commit} ${name}\n")
    endforeach()
    string(APPEND refs "0000")
    # Lengths count "\n" as one byte, which file(WRITE) would make "\r\n" on Windows.
    file(CONFIGURE OUTPUT "${mirror}/${repo}/refs" CONTENT "${refs}" @ONLY NEWLINE_STYLE LF)
endfunction()

# An archive of `repo` at `commit`, of the files in `folder`, in one folder
# of its own as hosts make them.
function(commit repo commit folder)
    get_filename_component(name "${repo}" NAME)
    set(top "${WORK}/archives/${name}-${commit}")
    file(COPY "${folder}/" DESTINATION "${top}")
    file(MAKE_DIRECTORY "${mirror}/${repo}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${mirror}/${repo}/${commit}.tar.gz" "${name}-${commit}"
        WORKING_DIRECTORY "${WORK}/archives" RESULT_VARIABLE code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "couldn't make the archive of ${repo} at ${commit}")
    endif()
endfunction()

set(M1 "1111111111111111111111111111111111111111")
set(P1 "2222222222222222222222222222222222222222")
set(P2 "3333333333333333333333333333333333333333")
set(P3 "4444444444444444444444444444444444444444")
set(N1 "5555555555555555555555555555555555555555")

set(src "${WORK}/sources")
file(WRITE "${src}/maths/tide.packages" "package Maths\n")
file(COPY "${games}/maths/step.tide" DESTINATION "${src}/maths")
commit(github.com/test/maths ${M1} "${src}/maths")
repository(github.com/test/maths refs/heads/main ${M1})

# Physics in a folder of its repository, needing Maths from git.
file(WRITE "${src}/physics/packages/physics/tide.packages" "package Physics\ngithub.com/test/maths ${M1}\n")
file(COPY "${games}/physics/body.tide" DESTINATION "${src}/physics/packages/physics")
commit(github.com/test/physics ${P1} "${src}/physics")
file(WRITE "${src}/physics/packages/physics/later.tide" "namespace Physics;\n\nsystem Settle(mut Body body)\n{\n    body.velocity *= 0.5;\n}\n")
commit(github.com/test/physics ${P2} "${src}/physics")
file(WRITE "${src}/physics/packages/physics/tide.packages" "package Physics\ntide 99.0\ngithub.com/test/maths ${M1}\n")
commit(github.com/test/physics ${P3} "${src}/physics")
repository(github.com/test/physics refs/heads/main ${P2} refs/heads/dev ${P3} refs/tags/v1.0.0 ${P1} refs/tags/v1.1.0 ${P2})

file(WRITE "${src}/nothing/README.md" "Not a package\n")
commit(github.com/test/nothing ${N1} "${src}/nothing")
repository(github.com/test/nothing refs/heads/main ${N1})

set(game "${WORK}/git-game")
file(COPY "${games}/game/main.tide" DESTINATION "${game}")
tide("${game}" ARGS add https://github.com/test/nothing.git FAILS EXPECT "github.com/test/nothing isn't a package")
tide("${game}" ARGS add github.com/test/physics//packages/physics@v1
    EXPECT "downloading github.com/test/physics at 3333333333" "added Physics, at 3333333 (v1.1.0)"
           "added github.com/test/maths ${M1}, which Physics needs")
expect_file("${game}/tide.packages" "github.com/test/physics//packages/physics@v1 ${P2}\n" "github.com/test/maths ${M1}\n")
tide("${game}" ARGS schedule . EXPECT "Physics.Integrate" "Physics.Settle")

# tide update moves a line to the newest commit of what it follows, keeping
# what's around it.
file(WRITE "${game}/tide.packages" "# Packages\ngithub.com/test/physics//packages/physics@v1 ${P1}  # Physics\ngithub.com/test/maths ${M1}\n")
tide("${game}" ARGS update physics EXPECT "Physics 2222222 -> 3333333 (v1.1.0)")
expect_file("${game}/tide.packages" "# Packages\ngithub.com/test/physics//packages/physics@v1 ${P2}  # Physics\n")
tide("${game}" ARGS update EXPECT "Physics is up to date, at 3333333 (v1.1.0)" "Maths is up to date, at 1111111 (main)")
tide("${game}" ARGS update nope FAILS EXPECT "tide.packages has no package called nope" "it has Physics and Maths")

# A line with no commit gets one from tide update, and a package for a newer
# tide says so.
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics@dev\ngithub.com/test/maths ${M1}\n")
tide("${game}" ARGS schedule . FAILS EXPECT "has no commit to build with" "`tide update github.com/test/physics//packages/physics`")
tide("${game}" ARGS update FAILS EXPECT "is at 4444444 (dev)" "Physics needs tide 99.0 or newer")
tide("${game}" ARGS schedule . FAILS EXPECT "Physics needs tide 99.0 or newer")

# A package from git that's gone from this machine comes back by itself.
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics ${P1}\ngithub.com/test/maths ${M1}\n")
file(REMOVE_RECURSE "${WORK}/cache")
tide("${game}" ARGS schedule . EXPECT "downloading github.com/test/physics at 2222222222" "Physics.Integrate")

# A commit is here once the file that says so is, which goes in last. One
# whose download stopped partway has some of its files and not that one: it's
# downloaded again, and what's missing comes.
set(stopped "${WORK}/cache/github.com/test/physics/${P1}")
if(NOT EXISTS "${stopped}/.tide-fetched")
    message(FATAL_ERROR "${stopped} should have .tide-fetched, which says all of its files are there")
endif()
file(REMOVE "${stopped}/.tide-fetched" "${stopped}/packages/physics/body.tide")
tide("${game}" ARGS schedule . EXPECT "downloading github.com/test/physics at 2222222222" "Physics.Integrate")
tide("${game}" ARGS schedule . EXPECT "Physics.Integrate")

# tide fetch downloads what the game lists and this machine doesn't have,
# each at its commit, and builds nothing: a build after it needs no network.
# It adds the lines of what they need, as tide add does.
file(REMOVE_RECURSE "${WORK}/cache")
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics ${P1}\n")
tide("${game}" ARGS fetch
    EXPECT "downloading github.com/test/physics at 2222222222" "added github.com/test/maths ${M1}, which Physics needs"
           "downloading github.com/test/maths at 1111111111" "the game's 2 packages are on this machine")
expect_file("${game}/tide.packages" "github.com/test/physics//packages/physics ${P1}\ngithub.com/test/maths ${M1}\n")
tide("${game}" ARGS fetch EXPECT "the game's 2 packages are on this machine" WITHOUT "downloading" "added")
# From here, with no host to download from
set(ENV{TIDE_GIT_MIRROR} "${WORK}/no-mirror")
tide("${game}" ARGS schedule . EXPECT "Physics.Integrate" WITHOUT "downloading")
tide("${WORK}" ARGS fetch "${game}" EXPECT "the game's 2 packages are on this machine")
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics ${P2}\ngithub.com/test/maths ${M1}\n")
tide("${game}" ARGS fetch FAILS EXPECT "couldn't download github.com/test/physics at ${P2}")
set(ENV{TIDE_GIT_MIRROR} "${mirror}")
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics@main\n")
tide("${game}" ARGS fetch FAILS EXPECT "has no commit to build with")
file(WRITE "${WORK}/no-packages/main.tide" "scene Main { }\n")
tide("${WORK}/no-packages" ARGS fetch EXPECT "tide.packages lists no packages")
tide("${game}" ARGS fetch one two FAILS EXPECT "fetch takes the game's folder, or none for this one")
file(WRITE "${game}/tide.packages" "github.com/test/physics//packages/physics ${P1}\ngithub.com/test/maths ${M1}\n")

# ---------------------------------------------------------------------------
# Packages that come with tide

# They're in tide's installation, <root>/src/packages/<name>, where tide is in
# <root>/bin: this tide goes in one of its own, with three of them and nothing
# to build games with, which tide schedule, add and update don't need.
set(built "${TIDE}")
set(install "${WORK}/install")
file(COPY "${TIDE}" DESTINATION "${install}/bin")
get_filename_component(tide_name "${TIDE}" NAME)
set(TIDE "${install}/bin/${tide_name}")
set(builtin "${install}/src/packages")
# The installation's packages/ is where packages from git are kept, where the
# installers put tide: nothing there comes with it.
file(WRITE "${install}/packages/cards/tide.packages" "package Cards\n")
file(WRITE "${builtin}/rolls/tide.packages" "package Rolls\n")
file(WRITE "${builtin}/rolls/sides.tide" "namespace Rolls;\n\nint Sides()\n{\n    return 6;\n}\n")
file(WRITE "${builtin}/dice/tide.packages" "package Dice\nrolls\n")
file(WRITE "${builtin}/dice/die.tide" "namespace Dice;

component Die
{
    int face;
}

system Roll(mut Die die)
{
    die.face += Rolls.Sides();
}
")
# One that names what it needs by a folder, which no game's file could.
file(WRITE "${builtin}/lonely/tide.packages" "package Lonely\n../rolls\n")
file(WRITE "${builtin}/lonely/alone.tide" "namespace Lonely;\n")

set(game "${WORK}/builtin-game")
file(WRITE "${game}/main.tide" "using Dice;

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Die { });
}

system Count(mut Die die)
{
    die.face += 1;
}
")

# tide add writes the name alone, in lower case however it's typed, and adds
# what the package needs.
tide("${game}" ARGS add Dice EXPECT "added Dice, which comes with tide" "added rolls, which Dice needs")
expect_file("${game}/tide.packages" "dice\nrolls\n")
tide("${game}" ARGS add dice EXPECT "Dice is in tide.packages already, on line 1" WITHOUT "tide update")
tide("${game}" ARGS schedule . EXPECT "stage 1  Dice.Roll" "stage 2  Count")

# tide update leaves those lines alone, and says nothing about them.
tide("${game}" ARGS update EXPECT "tide.packages has no packages from git to update" WITHOUT "Dice" "Rolls")
tide("${game}" ARGS add github.com/test/maths EXPECT "added Maths, at 1111111 (main)")
tide("${game}" ARGS update EXPECT "Maths is up to date, at 1111111 (main)" WITHOUT "Dice" "Rolls")
expect_file("${game}/tide.packages" "dice\nrolls\ngithub.com/test/maths ${M1}\n")
tide("${game}" ARGS update dice EXPECT "Dice comes with tide, so `tide upgrade` is what updates it")
tide("${game}" ARGS schedule . EXPECT "stage 1  Dice.Roll" "stage 2  Count")

# A name no package has: those there are, and the one that's close.
tide("${game}" ARGS add dise FAILS
    EXPECT "no package called dise comes with this tide" "did you mean 'dice'? It comes with dice, lonely and rolls")
tide("${game}" ARGS add cards FAILS EXPECT "no package called cards comes with this tide")
file(WRITE "${game}/tide.packages" "dice\nrolls\nnope\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:3: error: no package called nope comes with this tide" "it comes with dice, lonely and rolls")
# A folder of that name beside the game is one only as a path.
file(WRITE "${game}/shared/tide.packages" "package Shared\n")
file(WRITE "${game}/tide.packages" "dice\nrolls\nshared\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:3: error: no package called shared comes with this tide"
           "a folder is written as a path, like ./shared")
file(WRITE "${game}/tide.packages" "dice\nrolls\n./shared\n")
tide("${game}" ARGS schedule . EXPECT "stage 1  Dice.Roll")

# It's the version this tide came with: no commit, and nothing to follow.
file(WRITE "${game}/tide.packages" "dice ${M1}\nrolls\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:1: error: a package that comes with tide needs no commit"
           "a name alone, like `dice`, is a package that comes with tide: it's the version this tide came with")
file(WRITE "${game}/tide.packages" "dice@v1\nrolls\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:1: error: a package that comes with tide follows no branch or version")
tide("${game}" ARGS add rolls@v1 FAILS EXPECT "a package that comes with tide follows no branch or version")

# What it needs, the game lists, and it names that as a game does.
file(WRITE "${game}/tide.packages" "dice\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:1: error: Dice needs rolls, which this file doesn't list" "add this line: rolls")
file(WRITE "${game}/tide.packages" "dice\nrolls\nlonely\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "a package that comes with tide can't use a folder on this machine (../rolls)")

# tide eject copies a package into the game's packages/ folder and makes its
# line that folder, with what's around it kept: the game builds with the copy,
# which is its own to change.
file(WRITE "${game}/tide.packages" "dice # the dice\nrolls\ngithub.com/test/maths ${M1}\n")
file(WRITE "${builtin}/dice/.tide/work" "tide's own")
file(WRITE "${builtin}/dice/art/pips.txt" "six")
tide("${game}" ARGS eject Dice
    EXPECT "Dice is the game's own now, in packages/dice" "`tide upgrade` no longer updates it")
expect_file("${game}/tide.packages" "./packages/dice # the dice\nrolls\ngithub.com/test/maths ${M1}\n")
expect_file("${game}/packages/dice/tide.packages" "package Dice\nrolls\n")
expect_file("${game}/packages/dice/art/pips.txt" "six")
if(EXISTS "${game}/packages/dice/.tide")
    message(FATAL_ERROR "tide eject copied the .tide folder, which is tide's own work")
endif()
file(APPEND "${game}/packages/dice/die.tide" "\nsystem Reroll(mut Die die)\n{\n    die.face += 1;\n}\n")
tide("${game}" ARGS schedule . EXPECT "Dice.Roll" "Dice.Reroll" "Count")
tide("${game}" ARGS eject dice EXPECT "Dice is a folder on this machine already (./packages/dice)")
# One from git: its commit goes with its source
tide("${game}" ARGS eject maths
    EXPECT "Maths is the game's own now, in packages/maths" "`tide update` no longer moves it")
expect_file("${game}/tide.packages" "./packages/dice # the dice\nrolls\n./packages/maths\n")
expect_file("${game}/packages/maths/tide.packages" "package Maths")
if(EXISTS "${game}/packages/maths/.tide-fetched")
    message(FATAL_ERROR "tide eject copied .tide-fetched, which says a download is whole")
endif()
tide("${game}" ARGS schedule . EXPECT "Dice.Roll" "Dice.Reroll" "Count")
tide("${game}" ARGS update EXPECT "tide.packages has no packages from git to update")
# A folder in the way is left alone, and so is the file
file(WRITE "${game}/packages/rolls/mine.txt" "kept")
tide("${game}" ARGS eject rolls FAILS EXPECT "packages/rolls is there already, so Rolls wasn't copied")
expect_file("${game}/tide.packages" "./packages/dice # the dice\nrolls\n./packages/maths\n")
expect_file("${game}/packages/rolls/mine.txt" "kept")
tide("${game}" ARGS eject cards FAILS
    EXPECT "tide.packages has no package called cards" "`tide add cards` adds it first")
tide("${game}" ARGS eject FAILS EXPECT "eject takes the name of one of the game's packages")
file(REMOVE_RECURSE "${game}/packages" "${builtin}/dice/.tide" "${builtin}/dice/art")

# They're the installation's: the tide the build made, where it was built, has neither.
file(WRITE "${game}/tide.packages" "dice\nrolls\n")
tide("${game}" ARGS schedule . EXPECT "stage 1  Dice.Roll" "stage 2  Count")
set(TIDE "${built}")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: no package called dice comes with this tide")

# ---------------------------------------------------------------------------
# #if lines: which packages a build has, by its symbols

set(game "${WORK}/conditions")
file(WRITE "${game}/main.tide" "scene Main { }\n")
foreach(name Steam Ours Extra)
    string(TOLOWER "${name}" folder)
    file(WRITE "${game}/../conditions-${folder}/tide.packages" "package ${name}\n")
    file(WRITE "${game}/../conditions-${folder}/poll.tide"
        "namespace ${name};\n\nsingleton Polls\n{\n    int count;\n}\n\nsystem Poll(mut Polls polls)\n{\n    polls.count += 1;\n}\n")
endforeach()
file(WRITE "${game}/tide.packages" "# if this were a directive, it would say so
#if STEAM   # Valve's
../conditions-steam
#elif DEMO && !DEBUG
../conditions-extra
#else
../conditions-ours
#endif
")
# No symbol but DEBUG: the #else's. A symbol the command line names: its branch. --release has no DEBUG.
tide("${game}" ARGS schedule . EXPECT "Ours.Poll" WITHOUT "Steam.Poll" "Extra.Poll")
tide("${game}" ARGS schedule . --define STEAM EXPECT "Steam.Poll" WITHOUT "Ours.Poll" "Extra.Poll")
tide("${game}" ARGS schedule . --define DEMO EXPECT "Ours.Poll" WITHOUT "Steam.Poll" "Extra.Poll")
tide("${game}" ARGS schedule . --release --define DEMO EXPECT "Extra.Poll" WITHOUT "Steam.Poll" "Ours.Poll")
tide("${game}" ARGS schedule . --release --define DEMO --define STEAM EXPECT "Steam.Poll" WITHOUT "Ours.Poll" "Extra.Poll")
# A package's own symbol follows the build's packages
file(WRITE "${game}/main.tide" "scene Main { }\n#if PACKAGE_STEAM\nsystem OnSteam(mut Steam.Polls polls)\n{\n    polls.count += 2;\n}\n#endif\n")
tide("${game}" ARGS schedule . WITHOUT "OnSteam")
tide("${game}" ARGS schedule . --define STEAM EXPECT "OnSteam")
# Nested, and the same package in two branches, which are never one build's
file(WRITE "${game}/tide.packages" "#if !STEAM
#if DEBUG
../conditions-ours
#else
../conditions-extra
#endif
#else
../conditions-ours
../conditions-steam
#endif
")
tide("${game}" ARGS schedule . EXPECT "Ours.Poll" WITHOUT "Steam.Poll" "Extra.Poll")
tide("${game}" ARGS schedule . --release EXPECT "Extra.Poll" WITHOUT "Steam.Poll" "Ours.Poll")
tide("${game}" ARGS schedule . --release --define STEAM EXPECT "Steam.Poll" "Ours.Poll" WITHOUT "Extra.Poll")
# In one build, twice is twice
file(WRITE "${game}/tide.packages" "../conditions-ours\n#if DEBUG\n../conditions-ours\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:3: error: ../conditions-ours is listed twice")
tide("${game}" ARGS schedule . --release EXPECT "Ours.Poll")
# What's wrong with the lines themselves
file(WRITE "${game}/tide.packages" "#if STEAM\n../conditions-steam\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: this #if never ends" "end it with a line that says `#endif`")
file(WRITE "${game}/tide.packages" "../conditions-ours\n#else\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:2: error: this #else has no #if before it")
file(WRITE "${game}/tide.packages" "#if STEAM &&\n../conditions-steam\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: a condition is missing")
file(WRITE "${game}/tide.packages" "#if STEAM DEMO\n../conditions-steam\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: the condition ends before 'DEMO'")
file(WRITE "${game}/tide.packages" "#if PACKAGE_OURS\n../conditions-steam\n#endif\n../conditions-ours\n")
tide("${game}" ARGS schedule . FAILS
    EXPECT "tide.packages:1: error: 'PACKAGE_OURS' is a symbol this file's packages make, so no line of it depends on one")
file(WRITE "${game}/tide.packages" "#if TIDE_NEWER\n../conditions-steam\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: 'TIDE_NEWER' isn't a symbol Tide knows")
file(WRITE "${game}/tide.packages" "#if TIDE_0_1_OR_NEWER\n../conditions-steam\n#endif\n")
tide("${game}" ARGS schedule . EXPECT "Steam.Poll")
file(WRITE "${game}/tide.packages" "#ifdef STEAM\n../conditions-steam\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:1: error: #ifdef is C's: a condition here is an #if's" "write `#if SYMBOL`")
file(WRITE "${game}/tide.packages" "#if STEAM\n../conditions-steam\n#else\n#elif DEMO\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:4: error: this #elif comes after the #else of the #if on line 1")
file(WRITE "${game}/tide.packages" "#if DEBUG\ntide 0.1\n#endif\n")
tide("${game}" ARGS schedule . FAILS EXPECT "tide.packages:2: error: a `tide` line can't be inside an #if")

# The commands keep every line, a build's or not: update moves them, fetch
# downloads them, eject finds them, and add sees them listed
file(WRITE "${game}/main.tide" "scene Main { }\n")
file(WRITE "${game}/tide.packages" "#if STEAM\ngithub.com/test/maths ${M1}\n#endif\n")
tide("${game}" ARGS update EXPECT "Maths is up to date, at 1111111 (main)")
tide("${game}" ARGS update maths EXPECT "Maths is up to date, at 1111111 (main)")
tide("${game}" ARGS add github.com/test/maths EXPECT "Maths is in tide.packages already, on line 2")
file(REMOVE_RECURSE "${WORK}/cache/github.com/test/maths")
tide("${game}" ARGS fetch EXPECT "the game's 1 package is on this machine")
if(NOT EXISTS "${WORK}/cache/github.com/test/maths/${M1}/.tide-fetched")
    message(FATAL_ERROR "tide fetch left a package an #if leaves out of this build to another build's network")
endif()
tide("${game}" ARGS eject maths EXPECT "Maths is the game's own now, in packages/maths")
expect_file("${game}/tide.packages" "#if STEAM\n./packages/maths\n#endif\n")

# Authored trees are part of the same game/package source discovery, even unused.
file(WRITE "${game}/Enemy.tprefab" "prefab Enemy { MissingComponent; }\n")
tide("${game}" ARGS schedule . FAILS EXPECT "a prefab entry must name an ordinary component")
file(WRITE "${game}/Enemy.tprefab" "prefab Enemy { Transform; entity Child { Transform; } }\n")
tide("${game}" ARGS schedule .)
file(REMOVE "${game}/Enemy.tprefab")

# ---------------------------------------------------------------------------
# tide pin: `tide = <version>` in the game's tide.packages, in place of its `tide` line or first; a whole version alone;
# none for a build of tide's own source, which no release is, which builds a pinned game anyway (packages_pin_kept)
set(pinned "${WORK}/pinned")
file(MAKE_DIRECTORY "${pinned}")
file(WRITE "${pinned}/main.tide" "scene Main { }\n")
file(WRITE "${pinned}/tide.packages" "# Ours\ntide 0.1  # the oldest\n")
tide("${pinned}" ARGS pin FAILS EXPECT "which no release is" "tide pin 0.5.0")
tide("${pinned}" ARGS pin 0.5 FAILS EXPECT "'0.5' isn't a whole version")
tide("${pinned}" ARGS pin 0.5.0-nightly.12 EXPECT "tide: the game builds with tide 0.5.0-nightly.12")
expect_file("${pinned}/tide.packages" "# Ours\ntide = 0.5.0-nightly.12 # the oldest\n")
tide("${pinned}" ARGS pin v0.6.0 EXPECT "tide: the game builds with tide 0.6.0")
expect_file("${pinned}/tide.packages" "tide = 0.6.0 # the oldest\n")
tide("${pinned}" ARGS schedule . EXPECT "pinned: 0 systems")
file(WRITE "${pinned}/tide.packages" "../shared\n")
tide("${pinned}" ARGS pin 0.5.0 EXPECT "tide: the game builds with tide 0.5.0")
expect_file("${pinned}/tide.packages" "tide = 0.5.0\n../shared\n")
# Only a game pins: a package says the oldest it builds with
file(MAKE_DIRECTORY "${WORK}/shared")
file(WRITE "${WORK}/shared/tide.packages" "package Shared\ntide = 0.5.0\n")
tide("${pinned}" ARGS schedule . FAILS EXPECT "only a game pins its tide")
tide("${WORK}/shared" ARGS pin 0.5.0 FAILS EXPECT "only a game pins its tide")
