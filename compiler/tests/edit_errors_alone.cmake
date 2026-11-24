# A wrong world.Edit says what's wrong with it, first, and nothing more: the
# block's uses of its name aren't unknown names, nor refused again for what
# the call was refused for. And a command that leaves its sender out is
# told to name it, offered `with ownership` only where it runs for an entity.
# Each case is a program, the error it says first, and text it mustn't say.
# Run with TIDEC and OUT.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/out")
set(head "component Health { int value; Entity target; }\nsingleton Roster { List<Entity> all; }\nscene Main { }\n")

function(expect name body first forbidden)
    file(WRITE "${OUT}/${name}.tide" "${head}${body}\n")
    execute_process(
        COMMAND "${TIDEC}" "${OUT}/${name}.tide" -o "${OUT}/out"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if("${stdout}${stderr}" MATCHES "AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:")
        message(FATAL_ERROR "${name}: a sanitizer's report:\n${stdout}${stderr}")
    endif()
    if(result EQUAL 0)
        message(FATAL_ERROR "${name}: expected tidec to fail with: ${first}")
    endif()
    string(REGEX MATCH "error: [^\n]*" said "${stderr}")
    string(FIND "${said}" "${first}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${name}: expected the first error to say: ${first}\ntidec said:\n${stderr}")
    endif()
    string(REGEX MATCHALL "error: " errors "${stderr}")
    list(LENGTH errors count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "${name}: expected one error, not ${count}:\n${stderr}")
    endif()
    foreach(text IN LISTS forbidden)
        string(FIND "${stderr}" "${text}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "${name}: tidec said '${text}':\n${stderr}")
        endif()
    endforeach()
endfunction()

expect(no_type "system Hurt(Health health, mut World world) { world.Edit(health.target, other) { other.value -= 1; other.value += 2; } }"
       "Edit takes the component in '<>'" "unknown name")
expect(unknown_component "system Hurt(Health health, mut World world) { world.Edit<Helth>(health.target, other) { other.value -= 1; } }"
       "unknown component 'Helth'" "unknown name")
expect(misspelled "system Hurt(Health health, mut World world) { world.Edti<Health>(health.target, other) { other.value -= 1; } }"
       "the World has no method 'Edti'" "unknown name")
expect(out_name "system Hurt(Health health, mut World world) { world.Edit<Health>(health.target, out other) { other.value -= 1; } }"
       "Edit names the component its block changes" "unknown name")
expect(local_handler "component Spark { Entity follows; }\nlocal event Tap { }\nlocal event(Tap) OnTap(local Spark spark, mut World world) { world.Edit<Health>(spark.follows, other) { other.value -= 1; } }"
       "'Health' belongs to the match, and a local handler's World is this machine's" "unknown name")
expect(no_name "system Hurt(Health health, mut World world) { world.Edit<Health>(health.target) { } }"
       "'world.Edit<Health>(entity, health) { ... }'" "unknown name")
expect(in_parallel "system Hurt(mut Roster roster, mut World world) { parallel (var i in roster.all) { world.Edit<Health>(roster.all[i], other) { other.value -= 1; } } }"
       "a parallel loop's steps run at once" "unknown name")
expect(async_world "async void Later(Entity target, mut World world) { await Wait.Ticks(1); world.Edit<Health>(target, other) { other.value -= 1; } }"
       "an async function can't take the World yet" "unknown name;takes no type")
expect(global_command "singleton State { bool paused; }\ncommand(bool on) DoPause(mut State state) { state.paused = on; }"
       "command 'DoPause' must name the player who sent it" "with ownership")
