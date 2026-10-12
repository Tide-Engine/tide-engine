# Language features, as tidec builds them

## Asset value copy implementation (2026-10-11)

The owner approved ordinary asset values sharing immutable storage until the first write. Language value semantics stay unchanged: changing a field/local does not change another copy or the catalog. Generated list setters, mutable slots, RemoveAt/Sort, extern mut wrappers, nested ownership/deep-copy loops and confirmed/interpolated container traversal prepare storage with actual ownership before writing. Plain Clear can release a retained asset descriptor without copying its payload. Nested containers and text/maps still apply their existing ownership rules after outer detachment. The approved runtime boundary is `tide_list_prepare`; `notes/ecs.md` records details. Generated asset globals are marked unused because an unreferenced debug asset is legitimate and newer clang warns on set-only globals. `asset_sharing` covers that texture case and generated mutation/ownership paths. `docs/spec.md` and `docs/game/assets.md` record the agreed copy behavior; Texture/Clip remain resource references.

## Functions and methods start tasks, 2026-10-11

The owner's two standing rules (a function does what the code that calls it
could, with an error only at a call from code that can't; no "not yet" where a
feature could work) and no word of theirs on this one: the lead agent said it
would lift the restriction and the owner didn't object. Every rule is the
agent's pick, Provisional in `docs/spec.md` (Functions and blocks; Tasks). It
was the first of the lobby package's engine limits (`notes/lobby-package.md`).

**Surface.** An async function called as a statement in a plain function or a
method starts a task, as in a system. No new syntax.

**Checker** (`check.c`). `check_task_call` lets an inheriting function through
(`inherits`) and notes `decl.starts_tasks`, `starts_at` and `starts_what`
("starts 'Countdown'") on it, the fields a system's own start always set. An
operator and `Interpolate` are refused where the start is written, with
messages of their own; a frag or vert keeps the old one. The call is still a
`task_call`, with the function as `from`.

- **Which world.** A task's world is `decl.task_side`, which follows from what
  the task does, what it awaits or starts and what the functions it calls do;
  a function that starts a decided task acts in that world (`acts[0]` or
  `acts[1]`, with "starts 'X', a task that changes the match" as its `what`).
  Each depends on the others, so `settle_task_sides` settles them together to a
  fixed point, from `check_acts`, before its errors: `mark_act_callers`, then
  an async caller's side from a callee's acts, an async caller's from what it
  awaits or starts, and a plain starter's acts from its task's side. It only
  assigns where nothing was decided; what disagrees is said by the loops that
  always said it (`check_acts`, `resolve_tasks`). It runs only in a program
  where a function starts a task, so no other program's errors change order.
- **The errors are `check_acts`'s**, at each call from code with abilities of
  its own, exactly as a send's: a view or a local handler calling what starts a
  match task, the match calling what starts a local one, `Sample` and
  `Sanitize`, a frag or a vert, an operator or `Interpolate`, a parallel loop's
  step and a comparison. `check_acts` also looks at callees that only start
  tasks nothing decides (`tasks`), which have no `acts`. `check_audio_bodies`
  says "start tasks" for an `audio` declaration.
- **A task nothing decides** (`TASK_EITHER`) starts in the world of whatever
  calls the function. `decl.runs_in[2]`, an async function's worlds, is filled
  for functions and methods that start tasks too, from their callers
  (`runs_world`, in `resolve_tasks`' loop, over `c->calls`), and a task's
  `runs_in` comes from its starter's.
- **The schedule.** `check_acts` sets `starts_tasks`, `spawns` and
  `writes_text` on a system, view or handler that calls such a function, what
  `check_task_call` sets for a start written in it: one thread, in order.
  `mark_act_callers` carries `starts_tasks` through callers, and its early run
  (before systems' bodies) makes a grid foreach whose step calls one go in
  order (`own_cell_only_expr`). `call_acts` counts the call as one that acts.
- **A task's singleton through a function** (`param.task_own`,
  `check_task_params`). A task's component and singleton parameters are
  `task_ref`: not kept in its frame, got again from the world each time it goes
  on. A start had to pass the caller's own parameter, which a function's never
  was. A function's **singleton** parameter is now taken, marked `task_own`,
  and `check_task_params` carries the mark to the parameters of functions that
  pass theirs on to it, then requires every call to pass a parameter of its own
  (a system's, a task's `task_ref`, or a function's `task_own`): a copy is the
  error, at the call, with the line that starts the task. This is what the
  lobby's `CreateLobby(mut LobbyClient client, ...)` needed. A function's
  **component** parameter is still refused, with a note that says why: code
  that runs for an entity holds copies of its components (`gen_entity_copies`
  stores and reloads them around a start), a function's may be any entity's,
  and the task loads the one of the entity it belongs to.

**Codegen** (`codegen.c`). `takes_task(m)` is a plain function or method with
`starts_tasks`. It's handed, after `tide_scene` and before `tide_dice`:
`const tide_entity tide_task_this, const bool tide_task_this_local, const
tide_entity tide_task_scene` (what a task belongs to), and where both worlds
call it (`task_either`: `runs_in[0] && runs_in[1]`) also `tide_world
*tide_task_w, const bool tide_task_local`. Its worlds go through the
parameters there were: `takes_local` is also true of one whose callers run
locally, `takes_world` of one that has callers at all (a local task reads the
match, so it's handed `const tide_world *tide_w`), and `changes_world` of one
only the match calls.

- **What a call passes** (`gen_routine_call`): `task_owner(g, local)`, which is
  a task's own `tide_this, tide_this_local, tide_scene`, a function's own
  `tide_task_*`, or for a system, view or handler what `gen_task_start` always
  wrote (the entity it runs for, and its scene when that's of the task's
  world). `has_local` and `has_world` say whether the calling code has `tide_l`
  and `tide_w`: a local handler has no `tide_w` and the match's code no
  `tide_l`, and a function that starts tasks is handed `NULL` there, the only
  calls that pass one. `own_routine(g)` is the function whose own code is being
  generated, through functions that take an Action, which are copied into
  their caller and use the caller's names.
- **The start** (`gen_task_start`, with `gen_task_start_in` the block it always
  wrote): in a function, the task's own world, or for a task nothing decides
  the worlds the function runs in: one, both behind `if (tide_task_local)`, or
  none, where nothing that runs calls the function, no world has a table for
  the task, and only the arguments are worked out (`(void)(...)`), so a
  package's functions a game never calls still compile without a warning.
- **Order in the file.** A function that starts a task fills the task's frame,
  whose type is known once the tasks' code has been generated once
  (`gen_tasks`). So `gen_routines` leaves `takes_task` functions out and
  `gen_task_routines` writes them inside `gen_tasks`, after the frames and the
  declarations of the start functions; everything else calls them through
  `gen_routine_prototypes`' declarations.
- `call_keeps_order` counts a call of one that gives a value, as one that sends.

**A program where no function starts a task generates the C it did, byte for
byte**: old and new tidec over every source list in `build/debug`, with
`--layout` and without: 265 of 272 the same, five that use what's new and the
old compiler refuses (`tidec_e2e_fetch`, `tidec_e2e_function_tasks`,
`lobby_try`, `tide_lobby_menu`, `tide_lobby_relay`), and `last_light`'s two
failing with both, run outside the game's folder.

**Tests.** `compiler/tests/e2e/function_tasks` (three tests, natively and on
the web: a function's start, two through another function, a sum's left to
right, a struct's method's, an Action function's and its block's, each in the
order written and waiting in the match's table; a task that rolls, the same in
a second world; a task's own call of a function that starts another; a task a
fuse through a function, ending with its fuse; a crate a task spawned joining
its arena and going with it; the local world's from a view through one
function and two, from a view of the match's entities, each task its entity's
and gone with it, and from a local handler, which has no match to hand on; one
task nothing decides started by the same function from the match and from this
machine; two functions nothing calls). `compiler/tests/errors/function_tasks_*`,
13 files, a diagnostic each (`async_start_in_function`, which pinned the
refusal, is gone). Editors: `lsp_functions_send_and_spawn` (nothing was hidden:
completion always offered async functions in a function; the diagnostics at a
view's call are checked).

**Validation** (Windows, Node 24, with the request's time limit of the same
day, `notes/assets-and-files.md`): warning-free native and web debug builds;
2,230 of 2,230 native and 2,199 of 2,199 web tests, 37 of 37 relay tests, and
the docs production build. Linux, macOS, MinGW, Android and the sanitizers
were not run here, and no workflow was dispatched.

**Not built:** a function's component handed on to a task (above); a method's
own value (`this` of a singleton's method) handed on; `Snap`, `Scene.Load` and
`Scene.Unload` in a function; cancelling a task (no design).

## T? fields, 2026-10-11

The owner asked for them ("we have optional types or whatever but not fields
which is very weird; so we need to add those for consistency"): that a field
can be a `T?` is theirs, and every rule of how is the agent's pick, Provisional
in `docs/spec.md` (Errors, Provisional: T? fields; JSON).

**How it's kept.** A field declared `T?` has, as its type, a struct the
checker makes for that `T`: `value`, of type `T`, and `ok`, a bool
(`optional_field_type` in `check.c`; `decl.optional_of` is the `T?` it's for,
that `T?`'s `decl.stored` is the struct, and `program.optional_fields` lists
them). It's a `DECL_STRUCT` that's `builtin`, named as the `T?` is ("int?"),
in no list of names, ordered among `program.structs` after what its value
holds (`order_struct`). So everything that lays a struct's fields out, pads,
hashes, packs, snapshots, sends as deltas, carries over a reload, owns text
and lists, and repairs an input does a `T?` field's with no code of its own.
In generated C it's `tide_optional_<T>` (`name_program` in `codegen.c`: the
type's name with what C names can't hold as underscores, `tide_optional_int`,
`tide_optional_Combat_Stats`, `tide_optional_List_int`, text
`tide_optional_string`, and its place in the list after one that's taken),
in the header with the structs: the value, `bool ok`, padding written out.

**What code sees.** Never the struct. `check_expr_any` ends by turning the
struct into the `T?` (`read_type`), so a member or a method's own field is a
`TY_OPTIONAL` with `e->field` the field (`is_optional_field`), and everything
the language has for a `T?` local works on it unchanged. What takes a value
for a field (`check_expr_want_any`, `type_assignable` in `types.c`) takes what
the `T?` does. `check_default` lets `null` be a default. `check_writable`
refuses one as a `mut` or `out` argument (the C types differ, so nothing can
point at it as a `T?`), and `element_path_in_place` asks what the field is
kept as, so `rows[i].label = "x"` of a `string?` is refused as a `string`'s
is. `optional_field_ok` refuses a grid and a rendering resource. `T??`,
`List<int?>` (any `<>`) and `int?[4]` are the parser's errors
(`parse_decl_type`, `parse_type`), which say a struct with a `T?` field goes
there. `wrapped_what` names a field in the unwrap error.

**Generated C.** `tide_optional_<T>_get(kept)` gives the `tide_result<n>`
code reads (zeros unless `ok`; text through `tide_text_view`), and
`tide_optional_<T>_of(result)` what's kept (zeros unless `ok`; text through
`tide_text_temp`): `gen_optional_helpers`, both `TIDE_HELPER`s in the `.c`.
A read is `gen_optional_read` from `gen_expr`'s `E_MEMBER` and `E_NAME`,
beside text's `tide_text_view`, with `tide_copy_<struct>` first where the
value has a list, as any value taken out of a place is copied. A write is
`gen_as` with the struct as what's wanted (so a struct's value, a default, a
spawn and a send all go through `_of`), and in `S_ASSIGN` and
`gen_element_assign` the field's own branch: `tide_assign_<struct>` where it
keeps text or lists. `has_defaults` and `gen_value` give zeros for one with
no value, whatever its value's struct defaults to.

**A field with no value is zeros.** `_of` gives zeros; `tide_assign_<X>`
(`gen_text_helpers`) ends by releasing the text and lists of each `T?` field
it left with no value (`optional_paths`) and zeroing it, since assigning an
empty list keeps a block; an input is put right before `Sanitize`
(`gen_clear_padding`, and `needs_repair` is true of one); a command's and a
whole input's bits are read into zeros, and a delta's read ends with it
(`gen_bits`). `random_paths` skips them, so nothing seeds a `Random?`.
`type_blends` (`types.c`) says no, so views snap them. `gen_show_helpers`
has a `tide_text_<struct>` for each one shown: the value, or `null`.

**JSON** (`json_holds` in `check.c`; the struct reader and writer in
`gen_json_helpers`): no reader or writer of the kept struct's own. A struct's
reader sets the field to zeros, then, unless the value is `null`, reads the
value (a struct's from its defaults) and sets `ok`; its writer, for a struct
with any `T?` field, keeps a `first` flag instead of fixed commas and leaves
out a field with no value. A struct with none generates what it did.

**A game with none generates the C it did, byte for byte**: old and new tidec
over every source list in `build/debug` (270, with `--layout` and without).
The six that differ have `T?` fields, which the old compiler refuses: the new
e2e and safety programs, the three that build the lobby package, and the
migration test's. `last_light`'s two fail the same way with both, run outside
the game's folder.

**Tests.** `compiler/tests/e2e/optional_fields` (86 checks in Tide: every
kind of `T`, defaults, `null`, `default`, literals, methods, text and list
ownership and copies, lists, maps and arrays of structs with them, past the
end, as text, JSON both ways, C through `extern`, a scene's, events, a query's
rows, the World, a task across waits; in C: a component set and cleared is
the bytes of one never set and the world's hash where nothing's in the heap,
a copy, the world as bytes, a delta, `OPTIONAL_FIELDS_HASH`, which native and
web share, the input from C with bytes beside no value and as bits whole and
as what changed, commands' sizes on the wire and cut short, local fields,
local events and a local component, and a view between two ticks), natively
and on the web. `compiler/tests/safety/optional_fields`.
`compiler/tests/errors/optional_field_*`, 28 files, a diagnostic each
(`optional_field.tide`, the error that fields couldn't be one, is gone).
`migrate_carries_optional_fields_as_a_value_or_nothing`
(`platform/tests/migrate.c`). Editors: `lsp_optional_fields`, and five lines
of the grammar's (`notes/editors.md`).

**The lobby package** (`packages/lobby`) wrote a lobby's change a struct at a
time and joined the texts, as `Json.Write` wrote every field. It writes one
`ChangeSaid` of `T?` fields now, and `LobbyChange` has `T?` fields where it
had a `sets` flag beside each (`notes/lobby-package.md`).

**Validation** (Windows, Node 24): warning-free native and web debug builds;
2,217 native and 2,186 web tests pass, the web's with the same
`OPTIONAL_FIELDS_HASH`; 225 grammar checks; the docs production build. Linux,
macOS, MinGW, Android and the sanitizers were not run here, and no workflow
was dispatched.

**Not built**: a `T?` as what a list, a map, a set, a grid or an array holds;
a `T?` field as a `mut` or `out` argument; `??=`; `cond ? value : null`
(no local has it either); blending a value between two ticks that both have
one; a reload from `T` to `T?`; `null` written in JSON; a shorter delta for
an input's `T?` (its value's change is sent beside the flag's); a `T?` on the
GPU.

## JSON as a game's types, and text as bytes, pieces and numbers, 2026-10-11

The owner approved typed JSON generated at compile time ("yeah I like that
static thing; makes it simple, straight forward"; a dynamic JSON value was
explicitly not wanted now) and the names of the text primitives ("these are
good to have anyways"). Everything else is the agent's pick, Provisional in
`docs/spec.md` (JSON; Text, Bytes, pieces and numbers).

**Surface.** `Json.Read<T>(string | List<byte>) -> T?`, `Json.Write(T) ->
string`, `[Json("key")]` on a struct's field, `text.ToBytes() -> List<byte>`,
`string.FromBytes(List<byte>) -> string?`, `text.Split(string | char) ->
List<string>`, and `int.TryParse`, `long.TryParse` and `float.TryParse`
`(string text, out T value) -> bool`. `T` for JSON is a struct, a `List<T>` or
a `Map<string, T>` of bool, byte, int, long, float, char, string, enums,
structs of the game's, lists, arrays and maps by text.

**Parser.** `at_type_argument` (`parser.c`) takes a type with types of its own
after a method's name, `Json.Read<List<Score>>(text)`, `Map<string, int>`'s
comma only inside an inner `<>`, so `F(a.x < lo, hi > (y))` still reads as two
comparisons. The type is parsed by `parse_type` and closed by `close_angle`
as `world.Get<Health>` always was.

**Checker** (`check.c`, the block before `check_method`). `Json` is a
built-in owner (`builtin_owner`, `is_builtin_name`: a game declares nothing of
that name). `check_json_call` resolves the type in `<>` with `resolve_type`,
checks the root and everything in it (`json_takes`, `json_holds`, which names
the field that holds what JSON has nothing for), registers the type in
`program.json_roots` (one entry a type, with which of the three functions are
used) and names the call's C function (`tide_json_read<n>`,
`tide_json_read_bytes<n>`, `tide_json_write<n>`). `mark_json` sets
`decl.json_reads` or `decl.json_writes` on the type and the types in it, as
`mark_shown` does, behind `builds(prog)`. `check_from_bytes` is
`string.FromBytes`, reached before the built-in owners as `string` is no type
keyword's owner. `int`/`long`/`float.TryParse` are in the signature table
(`builtins.c`, through `add_gui`, which reads `out` in a parameter list);
`check_parse_call` checks the `out` argument as `check_gui_call` does a
widget's. `ToBytes` and `Split` are text's methods in the table; the check
makes `List<byte>` and `List<string>` (`builtins_use_io`) only where one is
called. All of them are `CALL_JSON`: c_callee with its arguments as values,
text a `tide_str`, an `out` one by address. `stands_alone` lets a `TryParse`
be a statement. `[Json]` is `check_json_attribute`; a field's key is
`json_field_key` (`ast.h`), read off the attribute, so `field` gained nothing.
Each is refused in code the GPU runs (`gpu_refuses`).

**Codegen** (`codegen.c`, `gen_json_helpers`, after `gen_show_helpers`). For
each marked enum, struct, list, map and array, a reader
`bool tide_json_get_<type>(tide_json *j, <stored> *v)` and a writer
`tide_str tide_json_put_<type>(tide_str a, ...)`, declared before any is
defined, so a struct with a list of itself works; then each root's functions,
which give a zeroed `tide_result<n>` unless the whole text was one value.
Readers fill values where they're kept (a list's slot from `tide_list_add`, a
struct's field, text as a `tide_text` in the scratch area), so no value is
copied through the stack on the way and recursion costs a frame of pointers.
A struct's reader starts from its declared defaults (`gen_default`), a `null`
is read by `tide_json_null` before each part, and keys go through one
`tide_json_field` call against a static table of the struct's keys. Writers
take structs and arrays by address. `string.FromBytes` has one adapter,
`tide_text_from_bytes`, in a game that calls it. The header includes
`tide/json.h` only when `json_roots` has entries. `writes` counts a
`CALL_JSON` with an `out` argument, so a `TryParse` keeps its turn like
`Map.TryGet`; a statement of one goes through `gen_expr`.

**A game with none of it generates the C it did, byte for byte**: old and new
tidec over every source list in `build/debug` (259; the two that differ are
the new JSON e2e and safety programs, which the old compiler refuses), with
`--layout` and without.

**Runtime** (the agent's, listed in `notes/runtime-ownership.md`):
`engine/src/json.c` with `tide/json.h` (the reader and the three writing
helpers), `tide_str_parse_int/long/float` and `tide_utf8_valid` in
`tide/text.h`, and `tide_str_to_bytes`, `tide_str_from_bytes`,
`tide_str_split` and `tide_str_split_char` in `tide/list.h`
(`engine/src/text.c`, beside the search they use).

**Numbers.** Text already showed floats exactly (`shortest_digits` in
`text.c`, Burger and Dybvig), so writing reuses `tide_str_add_float`; only
negative zero (`-0`) and the values JSON has no number for (`null`) are the
writer's own. Reading had nothing in-house: the GUI's number fields use
`strtof`, which is local and left as it is (Open in the spec). `json.c` reads
a number's digits once into `number` (128 kept, a flag for a nonzero digit
past them, a 64-bit power of ten that saturates) and makes a float by long
division of two big integers (`wide`, 40 words): 24 bits, a round bit, and
whether anything is left, rounded to even, with the floats under the smallest
normal one taking fewer bits. Past 39 decimal digits it's no float, under
1e-46 it's zero, so the integers are bounded whatever the text. Over 100,000
random texts of up to forty digits with exponents it agreed with this
machine's `strtof` on every one (a one-off check, not kept as a test: the
kept ones pin bits).

**Safety** is in `notes/memory-safety.md`.

**Also fixed**: `gen_print_value` wrote a `case` for every member of an enum,
so one with two names for a number (`Custom = 7, Alias = 7`) in a component
generated C that didn't compile. Found by the JSON e2e's enum.

**Tests.** `compiler/tests/e2e/json` (90 checks in Tide: every kind of field,
defaults, nulls, unknown and repeated keys, round trips, bytes, what's
refused, escapes and pairs, enums by number and alias, nesting at the limit,
lists and maps as the value, 21 floats at the edges, NaN and infinities,
`ToBytes`/`FromBytes`/`Split`/`TryParse`; in C, the floats' pinned bits, the
written text, a copy, the world as bytes, and `JSON_HASH`, which native and
web share), natively and on the web. `compiler/tests/safety/json`.
`compiler/tests/errors/json_*`, `string_*`, `parse_*` and
`text_split_arguments`, 27 files, a diagnostic each. `tests/test_json.c`
(eight runtime tests: the float table, every float's text read back and
against `strtof`, ints, each reader call, a hundred texts that are and aren't
JSON, depth, the two limits, a world's list in chunks, the writers) and
`text_splits_and_goes_to_bytes_and_back`; `crossplatform_numbers_from_text`
in `tests/test_crossplatform.c` pins 100,000 texts' bits and every float's
text. Editors: `lsp_json_and_text_primitives` (`notes/editors.md`).

**Validation** (Windows, Node 24): warning-free native and web debug builds;
2,170 native and 2,141 web tests pass, the web's with the same `JSON_HASH` and
cross-platform hash; 221 grammar checks; the docs production build. Linux,
macOS, MinGW, Android and the sanitizers were not run here, and no workflow
was dispatched.

**Not built**: a dynamic JSON value; `T?` fields (built since: see T? fields, above);
vectors, colors, sets and maps by an int or an enum; telling why a read gave
nothing; indented output or leaving out defaults; `Join`, `byte.TryParse`, a
`Parse` that fails; `TryParse` in the GUI's own number fields.
## AnimationCatalog and bool/out lookup, 2026-10-11

The owner approved the catalog name `AnimationCatalog` and bool/out lookup.
It replaces the former `Animations` type without an alias. Animation and bone
catalogs use `TryGetAt(index, out result)` and
`TryGetByName(name, out result)`; Skeleton uses the latter for bones.
Success assigns the selected value; failure assigns the value type's `default`.
Exact original names, ambiguity rejection, unnamed index access, checked dot
members, flattened source order and each clip's source skeleton are preserved.
The historical entries below describe the preceding API.

`skinned_arguments` checks outputs through the ordinary out-argument machinery,
including named source order, exact types, writable places, delayed locals,
discards, capability checks and definite assignment. Lookup counts as a writer
for ordering and discarded-call analysis, while its receiver is read-only.
Generated adapters copy a selected Animation before assigning through its
existing owned-struct helper; failures release any previous owned result.
Bone failure writes the default Bone. No engine API, canonical asset bytes,
pack format or sampling arithmetic changes. Editor and asset coverage are
recorded in their notes. Warning-free native and web debug builds pass all
2,150 native and 2,121 web CTests, including deterministic hashes and GPU
checks. The docs site build passes. Final diagnostic/hover wording and equivalent
lookup dispatch cleanup also pass the focused lookup/catalog/LSP checks.

## Delayed locals and switch expressions, 2026-10-11

The owner approved both features after showing a local `Animation` selected by
a statement switch. Explicitly typed locals may omit initialization. `var`
still needs a value. A delayed read-only local is initialized once on every
reachable path before reading it; `mut` permits subsequent writes. A statement
switch needs an assigning or terminating default, including for enums.

`assigned.c` tracks definite and possible assignments separately and excludes
unreachable paths from joins. Read-only delayed locals are whole values;
mutable structs retain the existing field-by-field assignment analysis.
Reachable loop back edges are visited again to reject repeated initialization,
while declarations reset their state for each iteration. Callback blocks are
also checked for repeating initialization. Sort's existing capture-write
restriction still rejects initialization of an outer delayed local; a local
declared inside its comparison can be initialized each invocation.
Plain assignments and `out` calls initialize a delayed local; compound writes
and `mut` calls require an initialized mutable value. Generated C gives storage its safe type
default, including padding, without making a source-level read legal.

Switch expressions use `selector switch { constant => value, _ => fallback }`.
The checker validates constant labels, duplicate folded values, a single final
fallback and compatible result types, supplying contextual types to default,
null and collection literals. Each arm owns its out-local scope. Parsing binds
switch after unary operators and before arithmetic. Constant folding supports
integer and boolean results; fully constant expressions also work in field
defaults and constants. Pattern matching and guards remain unbuilt.

C lowering captures the selector once and evaluates only the selected arm,
preserving surrounding operand order. An arm captures an owned collection
value before later operands can change its source, and marks that temporary
as already copied. Existing task-frame declarations preserve selectors,
branch-local temporaries and delayed locals across awaits, snapshots and
replay. No runtime API or ABI changed. GPU and editor details are in
`notes/shaders.md` and `notes/editors.md`.

Coverage: `e2e/switch_values` checks branch initialization, fresh loop locals,
mutable struct fields, out calls, Animation values, constant defaults,
integer/byte/char/enum selectors (including unnamed enum numbers and the
minimum long), inferred/contextual values, lazy effects and operand order,
owned list copies, async waits and snapshot replay. The `delayed_*` and
`switch_value_*` error fixtures pin invalid reads/writes, repeating paths,
missing fallback, duplicate/type/nonconstant labels and result mismatches.
Validation on Windows with Node 24.16: warning-free full native and web debug
builds; integrated suites 2,115/2,115 native and 2,087/2,087 web, including OpenGL,
WebGL and WebGPU CPU/pixel parity and unchanged determinism checks. The final
comparison coverage addition passes 32 focused native and 31 web checks,
including the full native language-server suite, the positive fresh-local
callback and the existing comparison-capture rejection. All 219 grammar
checks and the production docs build pass. Linux, macOS, MinGW and Android
coverage remains CI's job; no workflow was dispatched.

Integration with contextual enum shorthand supplies the selector's enum to
labels and the destination/common arm type to shorthand results. E2E, GPU
parity and LSP formatting cover both typed results and inference from an
ordinary qualified arm. Merge conflicts in the spec and subsystem notes keep
both feature descriptions; the compiler/editor implementations merge cleanly.
The subsequent enum-count/cast-guidance merge also keeps both spec/notes
descriptions. Warning-free native/web rebuilds and 42 focused checks on each
target pass, covering both features' E2E and errors, enum counts/casts, shader
goldens, OpenGL/WebGL/WebGPU parity and the full native language-server suite.
All 219 grammar checks and the production docs build pass again after that merge.

## Contextual enum members, 2026-10-11

Owner-approved `.Idle` shorthand uses the expected enum type. The parser keeps
an `E_MEMBER` with `enum_shorthand` and an implicit `E_NAME` receiver; no new
expression kind, runtime ABI or generated representation is needed. The checker
binds that receiver from typed initialization/assignment, field and parameter
defaults, returns, ordinary arguments, container elements/keys, comparisons and
switch labels. Typed conditionals carry the expected enum into both branches;
otherwise one ordinary branch can supply the other's type. Overload resolution
defers shorthand like `default`, requires enum parameters and never picks an
enum by its member names. Missing/non-enum context and ambiguous overloads need
the qualified spelling. Built-in signatures and fixed Sample filter/wrap
parameters use the same members; implicit built-in enum names retain generated
declarations, bold-font and trilinear flags. Checking still runs ordinary
assignment/capability rules. Source evaluation order and enum numeric values
are unchanged.

Regression coverage: `enum_shorthand_uses_expected_types` in the native/web
`enums` E2E, the `text` E2E's built-in enum arguments and return, four
`enum_shorthand_*` error fixtures, and unchanged GLSL/WGSL goldens for `lit`,
`maps`, `declared` and `cubemaps`. Editor behavior and validation are recorded
in `notes/editors.md`. The spec and Language basics page document the feature;
untyped `var state = .Idle` remains an error. This adds no general backward
type inference, enum-member search or attribute-specific shorthand contexts.

Final validation after merging dev: full Windows native debug (2,085 tests)
and web debug (2,057 tests, including WebGL/WebGPU) pass with Node 24.16.0.
The docs production build and all 217 shared grammar checks pass. Other native
platforms, sanitizers and Android were not run on this Windows machine.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Language

- Enum member counts and cast guidance (2026-10-11, requested by the owner):
  `EnumType.Count()` is a compile-time `int` counting declared members, including
  aliases and a member named `Count`. Explicit numeric values, gaps, negative
  values and backing storage do not change it; `EnumType.Count` remains the
  user's member. Qualified and built-in enum types work. The existing
  `named_enum` lookup preserves local/parameter/field shadowing; calls take no
  value or type arguments and are static, not enum-value methods.
  `CALL_ENUM_COUNT` retains the source call for editors, folds in integer
  constant expressions and emits a literal in C. No runtime API or layout
  changes. E2E enums cover aliases, sparse values, forward declarations, enum
  initializers, constants, field defaults, array lengths, case labels, built-in
  enums and advancing/wrapping a zero-based enum. Error fixtures pin arguments,
  type arguments, instance calls and discarded results.
  The parser recognizes unambiguous `(Type)value` forms, including qualified
  targets and grouped operands, rejects them and prints the exact `Type(value)`
  replacement. Existing parenthesized arithmetic, indexing, postfix steps and
  failable unwraps keep their parsing; ambiguous following operators remain
  expressions. Cast int/float/enum/qualified error fixtures and the LSP recovery
  regression pin the guidance. Enum `Values()` remains unimplemented.
  Editor and GPU integration are recorded in their notes. Validation: complete
  native debug (2,094 CTest entries) and web debug (2,066) suites pass after
  merging dev's contextual enums and texture work, using Node 24.19.0.
  The first native run caught Count completion leaking into enum case
  labels; after the editor-only fix, the full 215-test language-server suite
  was rebuilt and rerun successfully. Shader probes pass on native OpenGL,
  WebGL and WebGPU, and the production docs build passes. Other native platforms
  and Android were not run locally; CI was not dispatched.

- Sample argument bookkeeping (2026-10-11): `skinned_arguments` preserves the
  existing `expr.written` source list when it appends an omitted Playback.Clamp.
  `expr.args` still contains the same full parameter values; named evaluation
  order and generated operations are unchanged. This corrects editor hints for
  the synthetic default; regression and validation are in `notes/editors.md`.

- Conservative release asset pruning (2026-10-10): `codegen_options.strip_unused_assets` defaults false and invokes `program_strip_unused_assets` before any codegen asset metadata is collected. Checking remains unchanged and validates every declaration. The collector follows checked bindings, constants, all emitted helpers/methods, type and omitted argument defaults, operators, statement/expression children and GPU sampler/data metadata; generated types are conservative roots for implicit defaults and runtime Decode. It compacts only `program.assets` in declaration order, with iterative pointer-visited traversal for shared/cyclic graphs. This is declaration pruning, not routine reachability: a reference in an unused helper or type default retains its asset. External C-host/API/pack uses require caller opt-out, which is the default and the automatic CLI/CMake policy whenever C/C++ or prebuilt libraries are present. All seven `tidec_asset_usage` tests passed; asset pack, loader, fingerprint and watch behavior is recorded in `assets-and-files.md`.
- Decimal numeric literals accept a lowercase `deg` suffix (2026-10-10, approved by the owner): `180deg`, `22.5deg`, `.5deg`, `1e2deg` and digit separators. They are ordinary `float` values in radians; unary minus works normally, and variables still use `Math.Radians`. The suffix is exclusive with `f`/`L`, is not allowed on hex/binary literals, and cannot follow a variable or expression. `lex_number` makes these `T_FLOAT`; `parse_primary` strips separators, reads the degree value directly as binary32 with `strtof`, rejects a nonfinite input, then multiplies through the existing `tide_radians_f`. `expr.is_degrees` and `float_value` keep that single converted value while `text` keeps the full source spelling for editors. C codegen emits a round-tripping, nine-digit single-precision literal; GPU lowering uses the same stored value (`notes/shaders.md`). No runtime API or angle type was added. Lexer tests (`tidec_lexer`), `compiler/tests/errors/degree_*` and `compiler/tests/e2e/math` cover suffix boundaries, malformed forms, overflow, float typing, defaults, arithmetic, named scalar Euler calls and bit-identical results to `Math.Radians` on the CPU, including binary32 input rounding, signed zero, underflow and subnormals.
  - Validation: all 2,029 native debug and 2,001 web-debug tests passed, including the OpenGL/WebGL/WebGPU degree probes. The final subnormal additions passed the native math executable and web math test. Grammar tests (208), the focused LSP test and the docs production build passed. The installed package built the literal/scalar/vector/named sample for desktop and web and ran three hidden desktop frames; all 2,434 installed files matched the package, and the updated active VS Code extension's grammar matched the source.
- `quaternion.Euler` takes either a `float3 radians` or three scalar `float x, float y, float z` arguments (2026-10-10, requested by the owner). Both use radians and the existing Z-X-Y rotation order; integers widen normally and the scalar form accepts named arguments. Its signature packs the scalar arguments with `tide_f3` before calling the existing `tide_euler_q`, with no new runtime API. `gen_builtin_call` omits the pack's leading comma when the call has no hidden receiver/draw/audio argument; existing packed draw settings keep their generated C. `compiler/tests/e2e/math` checks bit-identical vector/scalar/named results, integer widening, field defaults and side effects evaluated once in written order, including reordered names. GPU lowering and editor tests are recorded in `notes/shaders.md` and `notes/editors.md`.
  - Validation: all 2,016 native debug and 1,989 web-debug tests passed, including OpenGL/WebGL/WebGPU shader probes; the docs production build passed. Packaged and installed Tide built the scalar/vector/named sample for desktop and web and ran three hidden desktop frames. All 2,434 installed files match the validated package.
- A builtin struct used only as a literal need not have any stored fields in a game. `gen_sounds`'s optional voice helper is `TIDE_HELPER`, so `Sound { }.bus` compiles under warnings-as-errors even when no local state keeps a `Sound`. The existing type and default rules remain unchanged; audio and bus regressions and the package use are described in `notes/audio.md`.
- The compiler's arena (`common.c/.h`) has `arena_checkpoint`/`arena_rewind` for a temporary allocation scope. A mark keeps the current block and used offset; rewind frees newer blocks and restores that offset, preserving everything before it. No pointer allocated after a mark may be published into surviving state, and a mark cannot cross `arena_reset` or an earlier rewind. The editor uses this only around response builders that read the completed analysis and copy all output into a heap-owned JSON buffer; writers that reset/reanalyze or populate retained caches are excluded. `notes/editors.md` records the exact scoped writers, protocol regressions and repeated-request memory measurements.
- A read-only method receiver follows the same container-copy rule as a read-only argument when a `mut` or `out` argument can change it (`gen_routine_call`): its lists, maps and sets are copied after the arguments are worked out, including implicit calls on a method's own value. Calls with no writable container argument keep the existing direct receiver path. `compiler/tests/e2e/evaluation_order` checks field, local and implicit receivers, an aliased whole-struct argument and replacement through `out`, reordered named arguments, nested lists/maps, text elements and sets, with the caller's changes checked beside the receiver's original values.
- Map/set foreach uses `tide_map_next` with insertion-order and index-hint locals (`gen_map_foreach`), rather than retaining a raw entry index through compaction. Tasks spill both integers, never an entry pointer. Generated map entries start with `long tide_order`, and `type_layout` matches the runtime's 24-byte, eight-aligned map. A map ABI marker in the generated header distinguishes games compiled against the older opaque map representation. `compiler/tests/e2e/map_iteration` covers ordinary and waiting loops; representation compatibility and hot-reload markers are described in `notes/hot-reload.md`.
- Control-flow checks share `fold_bool` (`compiler/src/check.c`, declared in `ast.h`) with compile-time integer choices: named bool constants, constant integer and bool comparisons, and their operators and conditionals mean the same thing for return completeness, switch-section exits, loops that cannot finish, and definite assignment (`assigned.c`). `breaks_out` follows the reachable side of a constant `if`, so an unreachable break doesn't make such a loop able to finish. Type and capability checking still visits both branches; only the paths considered for completeness change. `condition_text` (`codegen.c`) writes known conditions as `true` or `false`, so clang sees the same completeness even when the original expression calls an integer helper. A known condition's executed path is pure, so its hoisting is skipped too; its `out` and pattern declarations stay, so skipped locals exist and skipped calls stay skipped. `compiler/tests/e2e/constant_flow` checks those forms, `out` locals after constant `&&` and `||`, and skipped side effects (including ternaries with several calls on their skipped side); `errors/constant_flow_*.tide` keep false conditions, runtime conditions, reachable breaks and invalid unreachable code checked.
- `Color` constructors take numeric scalars and vectors totaling 3 or 4 channels, in order, as vector constructors do (agreed 2026-10-07). `Color(uv.xy, 0)` is `Color(uv.x, uv.y, 0)`, with alpha 1; four channels supply alpha. Ints and int vectors widen to floats, and each argument evaluates once, left to right, on the CPU and GPU alike.
- `Math`'s component-wise functions take a `Color` (the owner, 2026-10-08: "Color is essentially a float4 anyways"; that it's all of them, and that a color mixes with colors and numbers but no vector, are Claude's picks, Provisional in `docs/spec.md`, Vector math): `Math.Lerp(a, b, t)`, `Clamp`, `Saturate` and the rest, channel by channel, alpha too, giving a `Color`. `unify` in `compiler/src/builtins.c` picks `Color` as the call's type; a number beside one is splat (`tide_c_splat`; `vec4(x)` in a shader, `gen_as` and `ex_as`); the CPU's are `tide_<name>_c` in `tide/color.h`, `tide/math.h`'s scalar functions on each channel, and the GPU's are the float4 ones, as a color is a `vec4` there. `compiler/tests/e2e/math` and `compiler/tests/shaders/colors` check them.
- Expressions evaluate left to right on every platform and at every optimization level, whatever order C would pick for a call's arguments, an operator's operands or an initializer's fields (`docs/spec.md`, Evaluation order). Codegen runs what has side effects in a statement before it, into temporaries, in source order (`hoist_ordered` in `compiler/src/codegen.c`): spawns, widgets and calls that draw, calls to C, and calls that write (a `mut` or an `out` argument, a `mut` method, a list's, map's or set's change), with the functions, methods and operators that make them. In a statement with a call that writes, a value worked out before it that it could change is read in its turn too, into a temporary of its own (`tide_read<n>`, with its lists copied), a compound assignment's target among them, and a value worked out after it has the call run before the statement. What's written out several times in C (a vector's parts, a swizzle, a compound assignment's index) runs once, from a temporary. A statement with none of these generates the C it always did. Awaited calls collect their arguments through `arg_in_turn` too: named arguments evaluate in written order before their values go into the child task's frame in parameter order. `compiler/tests/e2e/evaluation_order` checks the results, the same natively and on the web, whose compilers pick different orders, including awaited named arguments, positional arguments before reordered named ones, reads beside writes and nested `out` calls, defaults, and arguments evaluated once before a child task waits.
- **`++i`, `i++`, `--i` and `i--` are expressions too, as C#'s** (the owner, 2026-10-08, of `(++some.count % 2 == 0) ? logo : test`: "i feel like this should be possible?"; they were statements alone, by a Provisional pick; the rules are Claude's picks, Provisional in `docs/spec.md`, Loops). What's built, and what code relies on:
  - **It's an expression that holds its statement:** `E_STEP` (`compiler/src/ast.h`: `lhs` the variable, `op`, `postfix`, and `step`, the `S_ASSIGN` a statement of it always was, sharing `lhs` as its target). `new_step` in `compiler/src/parser.c` makes both, from `parse_unary` (before) and `parse_postfix` (after). `parse_simple` gives back the `step` when the expression it read is one, so `i++;` and a for's step are the `S_ASSIGN` they were, and everything that reads statements (counted loops, a parallel loop's window, batches) sees what it did: old and new tidec over every game's source list, with `--layout` and without, 263 games the same.
  - **The checker checks the statement** (`check_step_value` in `check.c`, which runs `check_assign` on `step`): what a target can be, `mut`, views and the match, a for's variable are that code's, with its messages. The variable counts as read. The type is the target's: an int, a long, a byte or a float.
  - **Codegen runs it before its statement, always** (`hoist_ordered`): the variable read once (`tide_was<n>`), what it becomes worked out from that (`tide_stepped<n>`, by `gen_binary`, as the statement's own arithmetic), and the assignment of that, a copy of `step` with `=` and the temporary for its value. The expression is the first for `i++` and the second for `++i`. **Its value never comes of reading the variable back:** a parallel loop's step reads the grid as it was, so `++cells[at]` read back would be the old cell. It's a write (`writes`), its variable a place (`collect_ordered`), and it must hoist (`must_hoist`), so the evaluation-order rules are a `mut` call's: values beside it are read in their turn, and on a side of `?:` or the right of `&&` it's inside the unit's `if`. The assignment is generated by `gen_stmt` with `gen.bare` set to it, which skips that statement's own hoisting: what the variable runs (an index) is in temporaries already. An expression kind that isn't run through `hoist_spawns` before it's generated would write `tide_not_hoisted`.
  - **A parallel loop's step takes them** (refused at first, lifted the same day when the owner asked): what plans a step reads its assignments, so the two places that do see a step's own (`window_assign` in `check.c`, called for a statement and from `window_expr` for an `E_STEP`'s `step`: a list element's window mark, and a grid write the window can't take; `own_cell_only_expr`, which checks `step` as the assignment it is, so a foreach over a grid still runs at once). `check_writable` keeps a step to its own variables, cell and element as for any assignment. The list loops' flat and batch paths (`flat_list_expr`, `batch_list_expr` in `codegen.c`) don't know the kind, so such a step goes the general way.
  - **Code the GPU runs takes them, each run before its statement** (`st_steps` in `compiler/src/shader.c`, from `st`, which is a wrapper around `st_one`; also lifted the same day): WGSL has no `++` as a value, and GLSL leaves the order of operands open. For each, in the order written, a variable `tide_step<n>` gets the target's value before the step's assignment (`st_assign` of `step`) for `i++`, or after it for `++i`, and `ex` writes that name (`expr.hoisted`, set for the statement and cleared after it, as the CPU's code sets its own). The shader writer has no reads-in-their-turn, so what would make the value differ from the CPU's is refused, each with what to write: the statement naming the variable anywhere else (`names_variable`: a function can only reach a local it's passed), a step on a side of `?:` or the right of `&&`/`||` (WGSL's `select` works both sides out), a target that's no local or parameter, and one inside an expression on a for's or a while's own line, which runs again each round. A refused one is said once (`hoisted` is "0" for the statement).
  - **Editors:** `walk_expr` and `expr_end` in `compiler/lsp/analysis.c` know the kind, and the formatter already kept `++` against its variable (`lsp_increments_as_values`).
  - Tests: `compiler/tests/e2e/increments` (each form, its turn among reads, arguments and a compound assignment, `?:`, `&&`, a while's and a for's condition, fields, a list's and an array's element, a map's value, long, byte, float, a system, a view with a local singleton, and parallel loops over grids and a list and a foreach that runs at once, each step checking the value it was given); `compiler/tests/errors/increment_value_*.tide`; `compiler/tests/shaders/steps` pins the GLSL and WGSL; the `Rest` probe of `tide_platform_shaders` (`steps`: CPU against GPU, in a frag and a vert).
- **`char` is one character of text, and `text[i]` and a foreach over text give one** (the owner, 2026-10-08, of a card with three answers to what `text[i]` gives, a one-character `string`, the code point as an `int`, or a `char` type: "C; its the non-lazy approach"; the gap was found writing `examples/samples/two-forks`, which walked its levels' plans with `plan.Substring(i, 1)`. The rules are Claude's picks, Provisional in `docs/spec.md`, Text). What's built, and what code relies on:
  - **It's a kind of its own that's an int underneath** (`TY_CHAR`, `compiler/src/ast.h`; `int32_t` in generated C, by `types.c`'s table): its code point, so layout, hashing, snapshots, deltas and the world as bytes see four plain bytes and nothing new. `type_assignable` lets one go where an `int`, a `long` or a `float` does, never the other way. It isn't `type_is_numeric`, `type_is_integer` or `type_dim`'s: what goes by those doesn't take a char. **A place that names `TY_INT` in a list of kinds needs `TY_CHAR` beside it**, and has it: what says which values go where (`element_ok`, `grid_cell_ok`, `array_element_ok`, `map_key_ok`, `equatable`, `text_shows_value`), codegen's `gen_as` and `gen_c_as` (to a float or a long), `gen_default`, `scalar_kind`, `flat_list_value`, the lists' compare helpers, `gen_bits`, `has_enums` and `gen_repair_value`, `gen_shader_value`, and the shader writer's `ty`, `type_words`, `put_value`, `map_fits`, `st_switch` and `ex_as`.
  - **The literal is an `E_INT` that says it's a char** (`expr.is_char`, its code point in `int_value`; the lexer's `T_CHAR`, whose text keeps its quotes, read by `lex_char`; `parse_primary` decodes it). So what reads an int literal reads it with no code of its own: `fold_int` (a case's label, a constant), codegen's `E_INT`, the checks of a literal index. **What reads an `E_INT`'s digits again from `expr.text` has to ask `is_char` first**: the shader writer's `int_literal` did, and wrote every char literal as 0 until it didn't.
  - **To an operator a char is an int**: `binary_result` promotes it as it promotes a byte, after text's joins and before everything else, and unary `-` and `~` give an int. `unify` in `builtins.c` takes one as an int for `Math`'s functions. `char(n)` is `CTOR_CHAR` (`check_construct`): `tide_char_of` in generated C, inline in `tide/text.h`; the same bounds through `?:` or `select` in a shader, its argument written out five times, which costs nothing there as steps are hoisted; `fold_int` works it out for constants. `int(c)`, `long(c)` and `float(c)` are `CTOR_SCALAR`.
  - **A text's characters.** `check_index` gives a char for text, and `check_writable` refuses to write through one (an assignment, which `check_assign` hands it before anything else, a step, a `mut` or `out` argument). Codegen writes `tide_str_char(text, i)`. A foreach over text (`S_FOREACH`, in `check_stmt` and `gen_stmt`) keeps the text as the loop found it, a byte offset and a count (`tide_all<n>`, `tide_at<n>`, `tide_i<n>`, declared to a task as an array's loop declares its own, so a wait in the loop keeps them), and reads `tide_str_next(text, &at)` as many times as the text has characters.
  - **The runtime** (`tide/text.h`, `engine/src/text.c`). `tide_str_char` and `tide_str_next` are inline for text of a byte a character (`bytes == chars`, and the byte under 0x80), and call their `_wide` forms otherwise: `byte_of_char` counts the bytes that start a character eight at a time from the text's start, and `decode` gives U+FFFD for bytes that are no character's. Both go by the text's own counts, so an index anywhere, and one past a piece's own end with its text going on behind it, reads `'\0'`. `tide_str_add_char` adds nothing for 0, and `tide_str_add_char_quoted` is how a value shows one inside it (`gen_show_part`). The members' char versions (`tide_str_index_of_char` and the rest) come after the text ones in `builtins.c`'s table, which is what makes a call with text pick text's: the first that fits wins.
  - **`Substring` gives a piece where its text is** (behind the language: it copied its characters into the scratch area unless they ended its text, and walked from the start twice): `{ptr + from, bytes, length}`, the second walk going on from where the first got to. A piece's next byte is its text's, and a NUL only at the text's end: `tide_str_c` copies one that has none, as it always did for text that another was joined to in place (`grow`), so what hands text to C goes through it as before. `at_top` is false for a piece that doesn't end its text, so joining to one copies. `Trim` still copies what isn't its text's end.
  - **Inputs and commands** send one as 21 bits (`gen_bits`) and repair one as they repair an enum (`has_enums`, `gen_repair_value`: one that isn't `tide_char_of` of itself is the field's default).
  - **Hot reloading** carries a char field over to a char field of the same name (`TIDE_LAYOUT_PLAIN`: by its type's name and size). To an int, or from one, the field starts over.
  - **Editors.** In tidels, `T_CHAR` ends an operand for the formatter (`ends_operand`), nothing completes inside one, and the type has its hover, `default`'s, and `char(...)` and `int(char)` in signature help. The grammar scopes `'a'` as `string.quoted.single` with its escapes, as one `match` so a lone quote takes nothing with it, and `char` as a primitive; `language-configuration.json` pairs the quote and keeps `'{'` from indenting the next line.
  - **Measured** (2026-10-08, release on Windows, a Ryzen 9 7900X shared with other sessions and the test suite, 60 to 74% busy; the best of five rounds; a scratch harness that includes the generated C, not a benchmark of the repo): nanoseconds a character for a loop that does what two-forks' `LayOut` does with each (compares it with four literals and looks it up in `"nesw"`), over text of 72, 1,024, 4,096 and 65,536 characters. Of a byte a character: `Substring(i, 1)` before, 51, 330, 1,200 and 19,000 (it was quadratic: 1.2 s for one pass over 65,536); `Substring(i, 1)` now, 17, 17, 20 and 21; `text[i]`, 5.8, 5.9, 6.4 and 10; `foreach`, 6.0, 6.0, 6.3 and 10; C over the bytes, 3.2, 3.1, 3.4 and 4.5. With every eighth character two or three bytes: `Substring(i, 1)` before, 70, 760, 2,600 and 54,000; now 29, 137, 446 and 7,100; `text[i]` 17, 124, 479 and 7,300 (still quadratic, as it counts from the start each time); `foreach` 8.5, 8.3, 12 and 14.
  - **Tests**: `compiler/tests/e2e/chars` (literals and escapes, what compares and converts, a text's characters past both ends, foreach with `break` and `continue` and over a text its body changes, joining and showing, text's members, pieces of text, `switch` on a char and chars as an int's cases, overloads by char, int and text, `mut` and `out`, fields, arrays, lists, maps, sets and a grid's cells, a list sorted, an input repaired and packed whole and as a delta, a command over the wire and one with bits no character has, a task's loop across its waits, a parallel loop's steps, a view's copy, snapshots and the world as bytes, and a pinned hash the web shares), natively and on the web; `compiler/tests/errors/char_*.tide`, `text_index_*.tide`, `switch_text`, `foreach_text_type` and `index_not_list`, a file a diagnostic; `compiler/tests/safety/chars` (indexes at the ints' limits, megabytes of text of both kinds, numbers no character has, a piece read past its own end and joined to); `compiler/tests/shaders/chars` pins the GLSL and WGSL; the `Rest` probe of `tide_platform_shaders` (`chars`: CPU against GPU, in a frag and a vert); `lsp_char_hover_completion_and_format`; the grammar's probe (`characters`).
  - **Not built**: `++`, `--` and `+=` on a char; members that say what one is (`IsDigit`, `IsLetter`, its case); `\r`, `\t` and `\0` in text's literals; a `text[i]` that doesn't count from the start in text with characters of more than a byte (a `for` over one is quadratic, which is why the docs say `foreach`); vector constructors taking chars.
  - Provisional, awaiting the owner (`docs/spec.md`, Text): everything but that there's a `char` and that `text[i]` gives one. The ones most worth a look: a char converting to an int by itself and operators giving ints; `'\0'` as no character, which text shows as nothing; an index before the start reading `'\0'` where `Substring` clamps; `char(n)` of a number no character has being `'\0'`; the three escapes text's literals don't have.
- A game can have several `settings` blocks, in any of its files, and they're one list of what it sets (the owner, 2026-10-08: a second block used to be an error by itself, which was redundant, as what clashes is two that set the same thing; `docs/spec.md`, Settings). A setting is set once among them all: a second one is the error a setting set twice in a block always was, at the second place, with a note that says where the other one is (`set_before`, `check_settings` in `compiler/src/check.c`). The blocks are in the order their files compile, and every one's settings count. That the same value twice is still an error is Claude's pick, Provisional in the spec. A package's files still have none. Editors' completion in a block leaves out what any of the game's blocks sets (`complete_settings` in `compiler/lsp/analysis.c`). Nothing in codegen changed. Tests: `compiler/tests/e2e/settings` (three blocks in two files), `compiler/tests/errors/settings_set_twice_blocks` and `settings_set_twice_files`, `lsp_settings`.
- A plain name means what's nearest, as in C# (agreed 2026-10-05; `docs/spec.md`, Namespaces): the file's namespace, then the namespaces around it out to the global one, then the `using` namespaces, where two that both have the name are ambiguous (`lookup` in `compiler/src/check.c`). So what a game declares itself wins over what a namespace it imports has by that name. It was the other way round, `using` before the global namespace, until a game with a `Body` of its own said `using Box2D;` and got the package's, with no error. `compiler/tests/e2e/using_own` checks it, a file in a namespace of the game's own included; no other game in the repo generates other C for it (old and new tidec over every source list).
- A namespace can be a block, `namespace Combat { ... }`, for some of a file's declarations (the owner, 2026-10-07: "we should probably allow scoped namespaces, including on the same file... handy to separate concerns", then "yes build it"; `docs/spec.md`, Namespaces, where the rules beyond that are Provisional). A file can have several, the same one twice, and one inside another, whose name is inside the namespace it's written in (a file's `namespace Game;` too). What's built, and what code relies on:
  - **A block is a `unit` of its own** (`compiler/src/ast.h`), which is what kept the change small: declarations get their namespace from their unit and their code reads names through it (`decl.unit`, `decl_home`, `lookup` in `check.c`), so the checker's name index, generated C's names, the schedule's names and overrides have no rule for blocks. A block's unit has its file's `src`, a copy of its file's `usings` (complete by then: `using` comes before any declaration, and a block counts as one), `outer` (the file's unit or the block it's in), `ns` (the whole namespace), `ns_written` (the name as written at `ns_at`), and where its word and braces are (`block_at`, `open_at`, `close_at`).
  - **`program.units` has files and blocks**, a file's before its blocks', a block before those inside it. What goes through it for the namespaces there are needs nothing (`is_namespace`, the editors' completion and rename). What means a file skips a unit with `outer`: `check_units` for the `using` lines, which a block only shares. The first unit whose `src` is a file's is that file's (`doc_unit` in `compiler/lsp/analysis.c`). Nothing compares `decl.unit` to tell whether two declarations are in one file: compare `unit->src`.
  - **The parser** (`parse_file_header`, and the loop in `parse_file`): `namespace Name {` opens a block and makes its unit the parser's (`parser.unit`), a `}` where a declaration could start closes it while one is open, and the end of the file with one open is an error that names it. `namespace Name;` after any declaration, `using` in a block, and attributes, `local`, `virtual` or `override` before `namespace` are errors, each with what to write.
  - **Where a declaration starts is a column, in recovery and in the editors,** and it was always column 1. It's `unit.decl_col` now: 1 for a file, and for a block the column of its first line after the `{`, or four past its `namespace` word (what the formatter makes of it). `at_decl_start_or_function` in the parser goes by the unit being read (only with `recover`: a program that compiles never depends on a column), `skip_declaration` also stops at a `}` in the block's own column, and tidels asks `decl_col_at(loc)`, through `block_at` (the innermost block a position is in, from `A.blocks`). A check of a column that's added in either goes by those, never by 1.
  - **tidels:** a block's braces are no body: completion's context scan and the formatter's "does this line go on" both treat the inside of one as a file's top (`namespace_brace`; `top` in `analysis_format`), so an attribute's line in a block doesn't indent the declaration after it. What a request can name plainly goes by where it is (`A.from`, `from_unit`, `visible_by_name`): completion sets it to the cursor and the namespace quick fix to its diagnostic, and both clear it. A block's name is an occurrence of its whole namespace at the part written (`namespace_parts_from`), so hover, rename and references are the ones a file's namespace has. Moving a declaration to a file of its own writes the block's whole namespace as the new file's line and takes the block's indentation off.
  - A type nothing resolved is shown by the editors as the declaration of that name in any namespace (`type_written`), which used to count as the name being known and hid the "write it with its namespace" fix for a type: it's known there only when that declaration can be named plainly from where the error is.
  - A program with no block generates the C it did, byte for byte: old and new tidec over every game's source list, with `--layout` and without (249 games).
  - Tests: `compiler/tests/e2e/namespace_blocks` (three files: blocks beside the global namespace, one opened twice, a block in a block, a dotted one that another file's block in its own namespace adds to, `using` shared, `[After]` by a block's plain name and by a qualified one, a handler in its event's block, the names in C), natively and on the web; `compiler/tests/errors/namespace_block_*.tide`, a file a diagnostic; `lsp_namespace_blocks`; the grammar's probe, whose test reads a block's declarations a step in.
  - Not built: `using` inside a block, and a qualified name read from the namespaces around the code (`Rules.Cap` inside `Combat`), which C# has.
- Default parameter values, named arguments and overloads follow C# (agreed; `docs/spec.md`, Default values, named arguments and overloads). What's built, and what code relies on:
  - A function's or method's last parameters can have defaults, constants like a field's (`param.default_value`, checked in `check_param_default`). A call passes arguments by name with `:` (`expr.arg_names`), unnamed ones first. Functions can share a name, and so can a type's methods, and systems, views and handlers, and frags and verts, when their parameters' types differ, a system's filters and a view's `verified` among them (`decl.shares_name`, `same_param_lists`): names, `mut` and defaults tell nothing apart.
  - Where nothing calls (`[Before]`, `[After]`, a mesh's vert and frag), a shared name is written with its parameters' types, `Move(Position, Velocity, with Player)` (agreed: explicit, never just enough to tell them apart, and it has to be exactly one declaration's; anything else is an error that lists them). A name alone stays fine while one declaration has it. `named_decl` in `compiler/src/check.c` resolves both; the parser reads the list in `parse_signature`, and a mesh's as a call's arguments (`named_shader`).
  - A call's arguments are bound in order, then by name (`bind_args`), and the checker then makes `expr.args` what each parameter gets, in the parameters' order, with the defaults of those left out (`settle_args`): the default is the parameter's own expression, the same at every call. So codegen and the shader writer pass arguments as they always did. What the call wrote stays in `expr.written` (`expr.settled`), which the language server walks, and names out of the parameters' order leave `expr.arg_order`, the order they're worked out in (`arg_in_turn` in `compiler/src/codegen.c`): as written, like everything else (see Evaluation order).
  - A call of a shared name picks as C# does (`check_overloaded_args`): its arguments are checked once, without a type to take (`default`, `null`, `[a, b]` wait for the one picked), each candidate that binds and fits is compared by `better_candidate`, and none or a tie is an error listing them. A context-typed list's ordinary elements are checked in written order before choosing (`check_untyped_items`), so nested calls' `out` locals are in scope for later elements and arguments. The selected element types are applied afterwards without checking the calls again (`check_expr_want_checked`); lists still don't distinguish overloads. `compiler/tests/e2e/overloads` checks positional and reordered named calls, nested lists, arrays, sets, deferred defaults and byte constants, and that each call runs once; `errors/overload_list_out_*` keep ambiguity, wrong element types and reads before a declaration rejected. A name that isn't shared goes through the code it always did (`check_method_args`).
  - In generated C, a declaration that shares its name goes on with its parameters' types (`decl.c_suffix`: `tide_function_Damage_float_Weapon`), and tools show it with them (`decl.signature`: the schedule, the profiler's rows). One alone keeps its name, so a game that shares none generates the C it did, byte for byte: checked by running old and new tidec over every game's source list. `extern` functions that share a name each need a `[NativeName]`.
  - The engine's functions use the same things: a parameter a call can leave out is a default in the signature table (`signature.defaults` in `compiler/src/builtins.c`: `bool overwrite = false`, `Filter filter = Filter.Bilinear`, `string message = ""`), which editors show, and the rest are overloads (the shapes with a rotation or a width in the middle, `Math.Dot`). Their arguments take names where their parameters have them: the table's through `bind`, and the ones the checker takes by hand (`Session`, `Scene`, `Wait`, `Clipboard.Copy`, `Sample`) through `order_named_args`. Constructors, `Spawn`, `Send`, lists', maps' and the `World`'s methods, the component-wise `Math` functions and `Mesh` and `Instanced` take theirs in order (`refuse_arg_names`).
  - Provisional, awaiting the owner: what a default can be and which parameters take one; `default` as an argument staying the type's default; `[NativeName]` on externs that share a name; the names in generated C; a handler's trigger not being among the parameters that tell handlers apart.
- `out` parameters follow C# (agreed 2026-10-05: "if it's like C#'s", for the pattern of returning a bool and giving a result or the default; `docs/spec.md`, Out parameters). What's built, and what code relies on:
  - Functions, methods and extern functions declare them (`param.is_out`, with `mode` `PARAM_MUT`, so generated C, the schedule and evaluation order take one as the `mut` parameter it is underneath: a pointer to the caller's variable). What the engine calls itself (systems, views, handlers) is refused where it's parsed (`parse_query_params`), and operators, async functions, the devices, an `Action` and a default where signatures are checked (`check_signature`, `check_async_decl`, `check_param_default`). A frag and a vert have them for what the GPU takes (see Rendering and platform, What a frag and a vert write).
  - The call says `out` (`parse_out_arg`, from `parse_args`: the word then a name, `var` or `mut` is no expression, so `out` stays a name everywhere else). The argument is an ordinary expression with `expr.is_out`: the variable itself for `out health`, and for `out var health`, `out Health health` and `out mut var health` an `E_NAME` of the local it declares (`expr.out_local`, an `S_VAR` with no value and `stmt.out_decl`), which the checker binds like any local (`begin_out_arg`, `finish_out_arg`). So everything that takes a `mut` argument by address takes these with no code of its own. `out _`, where nothing is named `_`, is a local nothing names (`stmt.discard`), called `tide_out_<line>_<column>` in generated C. An `S_VAR` with no value used to mean the name after `is`: `out_decl` tells them apart.
  - An argument and its parameter have to agree about the word (`check_out_param`, `out_mismatch`), for an extern's `out List<T>` too, which took none before. The argument's type is the parameter's exactly, a copy can't be one (a list's element, and a swizzle of several components, which a `mut` parameter refuses too since then: `through_swizzle`; it used to compile and change a temporary), and an existing variable is checked as a `mut` argument's is (`check_writable`), which is what makes a system need `mut`, keeps views off the match and keeps a parallel loop's step and a sort's comparison to their own variables. `out` where no `out` parameter takes it is refused where the argument is checked (`refuse_out_arg`, from `check_expr_any`, by `checker.out_arg`).
  - A local an `out` argument declares is the enclosing block's, as in C#. The checker's scopes are what they were, but an `if`'s own, which hands its condition's to the block around it (`pop_scope_keeping_out`); a `while`'s and a `for`'s keep theirs. Generated C declares each before its statement, with its type's default (`declare_outs` at the top of `gen_stmt`; a loop's condition's in a block around the loop), and a task keeps them while it waits like any local (`task_declare`). `settled` in `codegen.c` counts such a local as one that changes, though it isn't `mut`, so reading it beside the call that makes it keeps its turn. A parallel loop's step and a sort's comparison count one declared inside them as their own (`declared_in_expr`).
  - Definite assignment is `compiler/src/assigned.c` (`check_assigned`), a pass over a body once it's checked with no error, and only for code that has `out` parameters or arguments (`checker.outs`): a byte per slot, a variable's or one for each field of a struct (which is assigned whole or a field at a time, as in C#), set where it's assigned on every path there, with C#'s rules for `&&`, `||`, `!`, `?:`, `??`, constant conditions, loops, breaks, continues, switches and returns. Assignments only add, so a loop's start is what's before it and one pass is enough. `out` parameters are assigned before they're read and at every `return` and a void function's end; locals `out` arguments declare are read only where their call ran. A block written after a call counts as code that may not run.
  - `fail` owes an `out` parameter nothing (a pick: C# has no equivalent). A function that `fails` assigns each its type's default as it starts, in generated C alone (`gen_routine`), so a failed call leaves what was assigned before it failed, or the default, and never what the variable held.
  - `Map<K, V>.TryGet(key, out value)` is the engine's own (`check_map_method`, with names through `order_named_args`): `tide_map<n>_tryget_out`, only in the C of a game that calls it (`decl.tries`), giving the value or its type's `default`.
  - Overloads: `out` tells none apart (`same_param_lists` goes by types). A call of a shared name fits an `out` argument to an `out` parameter of its own type only (`arg_fits`), and `out var` waits for the one picked, as `default` does (`untyped_arg`).
  - Code the GPU runs has them through the `mut` path (`inout` in GLSL, a pointer in WGSL), with the locals declared before their statement (`st_outs` in `compiler/src/shader.c`), and not on a counting loop's line.
  - A local an `out` argument declares that nothing reads is the unused-local warning, with `out _` as its fix (`FIX_DISCARD_OUT`); the others are `FIX_ADD_OUT`, `FIX_REMOVE_OUT` and `FIX_ASSIGN_OUT` (`x = default;` before a return, or a function's end, that leaves one unassigned; a return that's an `if`'s body with no braces goes in braces with it) and `FIX_ASSIGN_FIRST` (one read before it's assigned: `x = default;` as the function starts, or the parameter as `mut` with the word gone from every call, in any file, which isn't offered where a call declares the variable there). Where a note already says what to write in the place of what's there, it's a `suggest_replacement` and needs no fix of its own: `mut out` as `out`, `out` on what the engine calls as `mut`, `out var x` as `out x` when `x` is declared already, and a written type that isn't the parameter's as `var`.
  - A program with no `out` generates the C it did, byte for byte, and the two games that had `out List<T>` generate the same C from the new spelling: checked by running old and new tidec over every game's source list.
  - `compiler/tests/e2e/out_params` runs every form, natively and on the web; `compiler/tests/errors/out_*.tide` has a file for each diagnostic.
  - Provisional, awaiting the owner: the word at every call, extern lists included; `out var` read-only and `out mut var`; `out _` only while nothing is named `_`; what `fail` owes; a struct a field at a time, one level deep (it was whole assignment only until 2026-10-07); a block after a call not counting; which types can be `out`; the GPU's loop restriction; `TryGet` giving `default` where `map[key]` gives zeros. The owner's `TryFind(Entity target, World world, out Health health)` compiles as written: functions take the World (below).
- Functions and methods take the `World`, as their caller's, passed on (owner, 2026-10-05: "we are just talking backend stuff, let's do it"; `docs/spec.md`, The World): `bool TryFind(Entity target, World world, out Health health)`, called as `TryFind(healer.patient, world, out var health)`. What's built, and what code relies on:
  - A parameter typed `World` where no type of the game's has the name is `TY_WORLD` (`check_signature`, `check_world_param`): one a function at most, `World` or `mut World`, never `out`, with no default; operators, `extern` functions and async functions take none (an async handler takes it itself). As a value it's still nothing: a field, a local or a return type of it is `world_misplaced`.
  - The World is named as a value in one place, a call's argument that a `World` parameter takes (`checker.world_arg`, `world_value_ok`, which `check_name` reads; `check_world_args`), and `mut World` takes a `mut World`, as any `mut` argument. Nothing is passed for it in C: the function gets the worlds it reaches as hidden parameters, where its other hidden ones go, `tide_w` when it reaches the match's components (`decl.world_match`, `takes_world` in `compiler/src/codegen.c`; `const` unless it sets, `decl.world_sets`) and `tide_l` when it reaches local ones (`decl.world_local`, through `takes_local`), which are the names its caller has them under. So `world.Get` in a function is the code it is in a system.
  - `check_world_call` checks a function's calls as a system's, but for the rules that depend on who calls (`helper`), which wait. `check_world_passing`, once every body is checked, adds what each function reaches (`decl.world_uses`) to its callers', over the checker's list of calls until nothing grows, so a system's `world_uses` are its own whether it calls the World or a function does: the schedule (`accesses_of`), `world_reads_own_writes`, `system_splits`, `tidec --schedule` and the language server's lens need no rule of their own. Then it checks each call a system, view or handler makes: a local component from the match, the match's from a local handler, a view that would change the match, a `Set` in a parallel loop's step or a sort's comparison, and a `Set` of a component the caller takes as a parameter, read or `mut`.
  - `world.Set<T>(this, value)` beside a read parameter is the system's own to write: a function has no `this`, so a function that sets `T` is refused in a system that takes `T` at all, and the note says to write it in the system.
  - `world.TryGet<T>(entity, out value)` is the engine's own beside `Get` and `Has` (`check_world_call`, its `out` through `check_out_param`): `tide_tryget_<Component>`, only in the C of a game that calls it (`decl.tried`), which gives whether the entity has one and assigns the variable its copy or the component's `default`. A component with a grid has none, as it can't be copied.
  - A program that passes no `World` to a function and calls no `TryGet` generates the C it did, byte for byte: checked by running old and new tidec over every game's source list.
  - `compiler/tests/e2e/world_functions` runs it (functions, methods, a World passed through two functions, `mut World`, local components in a view, `TryGet` of every form) natively and on the web; `compiler/tests/schedule/world_functions` pins that a function's reach is in its system's schedule; `compiler/tests/errors/world_function_*.tide` and `world_try_get_*.tide` have a file for each diagnostic; `lsp_world_in_functions` checks the editor's side.
  - Provisional, awaiting the owner: `mut World` in functions; methods taking it too; async functions and code the GPU runs taking none; a function that sets a component always refused beside the caller's parameter of it; `TryGet` giving the component's `default` where `Get` gives zeros.
- **Functions and methods send, spawn and change entities as the code that calls them could** (the owner, 2026-10-08: "it should inherit their ability; if you say `Send` it should mean that what calls us can `Send` otherwise is a compiler error ONLY when its called from a place it can't"; `docs/spec.md`, Functions, with the picks made to build it Provisional under Functions and blocks). It was a recurring ask of the agent-written games (`GAME_TRIAL_FEEDBACK.md`), and Two Forks compared two copies of its board after a move to learn what to send. `Send`, `entity.Send`, `Spawn`, `Add`, `Remove` and `Destroy` are written in a function or a method as in a system. What's built, and what code relies on:
  - **A function records what it does, and nothing is wrong with it there** (`inherits`, `note_act` in `compiler/src/check.c`): `decl.acts[2]`, the match's ([0]) and this machine's ([1]), with where and what for the messages (`acts_at`, `acts_what`: "sends 'Hit'"), `acts_spawn[2]` and `acts_text`. Which world is told by what's written, as an async function's is: an event's own (a command is this machine's), the entity's type (`Entity`, `LocalEntity`), the components'; `same_world` is the one error in the function, a `Spawn`'s, an `Add`'s or a `Remove`'s components of two worlds, and an event sent to an entity of the other. A `Spawn` with no components is the match's. `check_component_side` and `check_entity_side` let an inheriting function through, where they check a system's or a view's against its own world.
  - **`refuse_act`** is what still refuses where it's written: frags and verts (the old messages), a struct's operators and a component's `Interpolate` (messages of their own), and the input's code. `passes_acts` says which declarations inherit: plain functions and methods, not async ones (their world is what they do), not operators (an operator's call is handed its operands, the dice and the seed and nothing else: `gen_operator_call`), not `Interpolate`.
  - **`mark_act_callers`** carries it from a function to the functions and methods that call it, over the checker's list of calls until nothing grows. It runs twice: once every function's body is checked and before any system's, so a foreach over a grid whose step calls one goes in order (`own_cell_only_expr`), and again in `check_acts`.
  - **`check_acts`**, after `check_sends` and before `resolve_tasks`, is where the errors are, at each call from code that has abilities of its own: a view or a local handler calling what's the match's, the match calling what's this machine's (a command's message says who sends commands), `Sample` and `Sanitize`, a frag or a vert, an operator or `Interpolate`, and any call in a parallel loop's step or a comparison (`call_site.parallel`, `comparing`). An async function that calls one gets its world from it (`note_side`), which is why it comes before `resolve_tasks`. A function's own callers answer for it. An `audio` declaration that does is `check_audio_bodies`'s, with the rest of what the audio thread can't.
  - **It's the calling system's in the schedule:** `check_acts` sets `decl.spawns` on a system, view or handler that calls a function that spawns, and `decl.writes_text` on one whose function records text, as `note_spawn` and `note_text_write` do for its own. So `analyze_parallelism`, `system_splits` and `system_settles` need no rule: a system that splits and calls a function that spawns gets temporary handles and settles them, in its queue and in the entity fields of its `mut` components, which is everything a function could have written one to. A function that sends keeps a system splitting.
  - **In generated C the function is handed what it records in**, where its other hidden parameters go (`gen_routine_signature`, `gen_routine_call` in `compiler/src/codegen.c`): `tide_world *tide_w`, not const (`takes_world`, `changes_world`), `tide_local *tide_l` (`takes_local`), and `tide_entity tide_scene` when it spawns in a world that has scenes (`takes_scene`, `scene_is_local`), which a call gives its own `tide_scene` when that's of the same world and the null entity otherwise. The `Send` and the `Spawn` in it are the lines a system has (`gen_send`, `gen_spawn`, `gen_method`), so they record through `tide_recording` into the queue of whatever task runs the caller, and `tide_new_entity` gives a temporary handle there. A function nothing could call (both worlds) still generates C that compiles.
  - **A call of one keeps its order** (`call_keeps_order`): one that gives a value runs before its statement when anything does, like a call to C. `call_acts` counts it as a call that does something, so the unused-local warning says to keep the call as a statement.
  - **A system whose whole body is one `Send`** still writes its events as a run (`batch_event`); through a function it takes the ordinary push, one event at a time.
  - A program with no such function generates the C it did, byte for byte: old and new tidec over every game's source list, with `--layout` and without (234 games).
  - Tests: `compiler/tests/e2e/function_acts` (the order among a system's own sends, a sum's calls, a block's; a struct's and a component's methods, a task and a command's handler; `Add`, `Remove` and `Destroy`; a spawn joining its caller's scene and going with it; one thread against a pool that says it has four, tick by tick, with events sent to entities spawned there; a view's functions sending a local event and a command and spawning and destroying local entities), natively and on the web; `compiler/tests/errors/function_acts_*.tide`, a file a diagnostic. `event_send_in_function` and `function_spawn`, which pinned the refusals, are gone.
  - **Editors:** completion offers `Spawn` and `Send` in a function or a method, with the events of both worlds, and an entity's `Add`, `Remove`, `Destroy` and `Send` after its dot there (`passed` in `list_members`, and the routine's branch of `complete_expression`, `compiler/lsp/analysis.c`), not in an operator, an `Interpolate` or an `audio` declaration, and never `Snap`. They were hidden in every function, async ones too, which could always send. `lsp_functions_send_and_spawn` checks them, and the diagnostic at a view's call.
  - **A dropped value warns, and `_ = value;` drops one on purpose** (built 2026-10-11, both the owner's, from `request.SetHeader(...)` written as a statement, which gave a request nobody kept). `check_stmt`'s `S_EXPR` notes a function's or method's call whose type isn't void, an error or failable (`checker.dropped`), and `warn_dropped_values`, after `warn_unused_locals`, warns for those `call_acts` says do nothing: it has to wait, as whether a function draws, calls C or acts is known once every body is checked. `_ = value;` is the parser's (`parse_simple`): a `var` named `_dropped_<line>_<column>`, marked `discard` and read, so a value dropped goes through everything a variable's does (a call's order, an `await`, text and lists) and generated C needed nothing. No game in the repository had such a call. Tests: `compiler/tests/warnings/dropped_value.tide`.
  - **Session calls and `Clipboard.Copy` in a function or a method** (built 2026-10-11; the owner, asked whether to lift "functions can't call Session yet": "yes lets improve the session stuff on Tide"). They're this machine's acts: `check_session_call` and the clipboard's check let an inheriting function through and `note_act(c, true, ...)` with "calls Session.Open" or "calls Clipboard.Copy", as a command's send does, so `mark_act_callers` and `check_acts` need no rule for them and the errors are theirs (the match, `Sample`, a frag, a parallel loop's step, a comparison); `check_acts` words the match's as "the match runs the same on every machine" for a `what` that starts with "calls ". An operator and `Interpolate` are refused where written. Generated C needed nothing: `takes_local` is true of `acts[1]`, so the function is handed `tide_l`, which `gen_session_call` and `tide_request_copy` already name. "A statement of its own" still holds in a function. `session.room` and a whole `Session` as text stay refused in functions and methods (`singleton_param_session_room`, `_text`, `_append`): the room is beside the `Session` value, in the local state, and a helper gains no implicit lookup; making the room part of the value is the runtime's side and wasn't done. tidels offers `Session.` and `Clipboard.` there (`decides_session`). Tests: `tidec_e2e_session_main` (a function a function calls, a struct's method, the clipboard, a local handler's function), `compiler/tests/errors/session_in_function`, `clipboard_in_function`, `session_function_in_sample`, `session_in_operator`; `singleton_param_session_operation`, which pinned the refusal, is gone; `lsp_functions_send_and_spawn`.
  - Not built: `Snap`, `Scene.Load` and `Scene.Unload` in a function; operators that send; a hover or a lens saying what a function sends. Two Forks still compares its boards. Starting tasks in a function is built since: Functions and methods start tasks, at the top.
  - Provisional, awaiting the owner: everything under Functions and blocks in the spec. The owner's own words cover that it's bare, inherited from the caller, and an error only at a call from a place that can't; that it's all six operations and methods too is my reading of "there are depend on who calls them", said of `Spawn` and `Destroy`.
- `virtual` and `override` follow C# (agreed 2026-10-05; `docs/spec.md`, Mods: Decided for what's agreed, Provisional for the picks made to build them). `virtual` before a declaration with code says another package can replace it, and `override` before one replaces the virtual declaration of that name and those parameters, while compiling. They're the first step of modding, and for any package. What's built, and what code relies on:
  - **The words** are read where `local` and `async` are (`at_replace_word` in `compiler/src/parser.c`): before a declaration's word or a function's type, and `virtual` before a method in a type's body. Data, constants, `extern` and the settings are refused before the declaration is read (`replace_refusal`), which then goes as it would without the word. After `override` a declaration's name can be qualified (`new_named_decl`, `parser.overriding`): `decl.override_name` is what's written, and `name` and `at` its last part.
  - **An override declares no name.** Until it's in place (`unplaced_override` in `compiler/src/check.c`), the index of names leaves it out (`decls_by`) and it clashes with nothing (`collect_decls`). One that found nothing to replace stays so.
  - **Matching** (`match_override`) is by name and by parameters: the name looked up from the override's file as any reference is (`lookup`), a method's through its type first (`override_owner`, which makes the declaration a `DECL_METHOD` with its `owner` before its signature is checked), then `same_param_lists` among the declarations that share the name. It needs every candidate's parameters' types, and has to come before the code that names what's replaced: for functions, methods, frags and verts, after every signature and before any body; for systems, views and handlers, before any of their bodies, with every one's `check_params` run first (`decl.params_checked`). A game with no override of a system has each system's parameters checked with its body, as ever, so its diagnostics come in the order they did.
  - **In place** (`place_overrides`, `replace_in`): the override goes where the replaced declaration was in `program.decls`, `systems`, `views` and `handlers`, or in its type's `methods`, and leaves where it was written. It takes the other's `qualified`, `shares_name`, `c_suffix` and `signature`, and shares its `attributes` (the same items). `decl.named_in` is the replaced declaration's file: `decl_home` (`compiler/src/ast.h`) is the file whose namespace a declaration's name is in, which the index of names, `make_cname` in `codegen.c`, `put_decl_name` and `check_attributes` go by (attributes read names as the file that wrote them), while `decl.unit` stays the override's own file, which its code reads names through. From there every lookup by name finds the override, so nothing after it has a rule for overrides: the schedule, the archetypes, the shaders and generated C have the override as the declaration.
  - **What was replaced** is in `program.replaced` and in no other list (`decl.replaced_by`; the override's `decl.replaces`). `check_replaced` checks its body against a copy of the program and of the checker (`program.dry`): what the body would add (spawns, sends, calls, Adds and Removes, mesh draws, the program's flags) goes to the copies and is dropped, and what it would mark on declarations it shares with built code is behind `builds(prog)`: a type's helpers (`shown`, `resized`, `tries`, `tried`, `optional`), a singleton's `snapped`, `Random`'s `named`, and the one warning a body gives. **A mark a body's check gains on another declaration needs that guard**, or replaced code changes the build. The quick fixes its errors offer are kept. The checks made over the whole program's calls afterwards (`check_drawing_calls` and the like) don't see it.
  - `warn_unused_params` leaves alone a parameter nothing reads in a virtual declaration or an override, whose list is the other's too, and still says `mut` that's never written.
  - **Packages.** `package_of` (`unit->src->package`) is a declaration's package, NULL for the game's own, and `same_package` compares two. `tide` and tidels give each file its package (`compile_inputs`, `analysis_file`). tidec's own command line takes `--package <Name>` before a package's files, and so does a line of an `@list` (`compiler/src/main.c`): packages' files compile first, in the order the packages are named, each by path, then the game's. `tide_add_game(... PACKAGES <Name> <file>...)` writes those lines, for tests, and lists such a file in the games' manifest with its package after it (`<game>\t<path>\t<package>`), which tidels reads (`add_manifest_game` in `compiler/lsp/server.c`).
  - **tidels** keeps both. `A.written` (`compiler/lsp/analysis.c`) is every declaration as files write them: the program's, what was replaced, and overrides of methods, which the program keeps in the method's type; what goes by positions walks it, and `method_written` is a type's method as its body has it. An override and what it replaced are one symbol (`replaced_or_self`, in `same_symbol`), and the words are `OCC_REPLACE`.
  - A program with neither word generates the C it did, byte for byte: checked by running old and new tidec over every game's source list.
  - Tests: `compiler/tests/e2e/overrides` (a package that replaces the game's systems, a function among those that share its name, a struct's and a component's method, an event's, a command's and a local handler, a view, a frag, a vert and an async function, and the game replacing the package's), natively and on the web; `compiler/tests/schedule/overrides`, `shaders/overrides` and `warnings/overrides`; `compiler/tests/errors/override_*` and `virtual_*`, one for each diagnostic; `lsp_virtual_and_override` and its neighbors for the editors' side; the grammar's probe.
  - Provisional, awaiting the owner (`docs/spec.md`, Mods, Provisional): where the words go and what refuses them; how an override names what it replaces, a method's with its type before it; what has to match beyond the parameters, and what can differ; no attributes on an override; another package, the game counting as one; what's checked of replaced code, and that it warns about nothing; no warning for a parameter a virtual declaration or an override doesn't use; no override of an override.
- A type is read as a tree (`type_ref`, `compiler/src/ast.h`), the first of three steps the owner decided generics take (2026-10-06: "we clearly need generics and I don't like making them just an engine thing that you can't touch"): a type tree, then nested containers by one rule, then user-defined generics. What's built, and what code relies on:
  - The parser builds one for every place a type is written (`parse_type` and `parse_decl_type` in `compiler/src/parser.c`): a name, maybe qualified, what it takes in `<>` as trees of their own, and `?` after it. Every field, parameter, local, constant, asset, return type, `fails` type, `is` pattern, `world.Get<T>` argument and a signature's item keeps its tree beside its text (`field.type_ref`, `param.type_ref`, `stmt.type_ref`, `decl.return_type_ref`, `decl.fails_ref`, `expr.pattern_ref`, `expr.type_arg_ref`, `sig_item.type_ref`). The text stays, as what messages and editors write (`type_name` and the rest: "Map<K, V>" with that comma and space), and nothing parses it any more. A type the checker names itself has only its text (`type_ref_or_text`).
  - `>>` and `>>=` are read where a type's `>` is (`close_angle`): the first `>` closes the inner type, and what's left of the token is the next one (`parser.rest`), so no token is split in place and the lexer stays as it is. The lookaheads that tell a declaration from an expression skip a type the same way (`skip_type`), and a syntax error inside a type's `<>` says what the type takes there (`generic_forms`).
  - The parser refuses no nesting. What a built-in generic type takes in its `<>`, and where one can be inside another, is the checker's: `resolve_type` in `compiler/src/check.c` is the one resolver for every place a type is written (`type_place`), `resolve_generic` makes the list, map, set and grid types through `list_of`, `map_of` and `grid_of` (one declaration per instantiation, as before), `refuse_nested` says what nothing holds yet, and `unknown_type` is the one path for a name no type has, whose suggestions and quick fix go by the place. What a place can't hold once the type is known (a field no component, a local no grid, a function no grid) is each place's own check after the resolver, with the messages there were. An asset's type is read off the tree too (`asset_type`), the one place a `List<Grid2<Color>>` or a `Map<string, List<byte>>` is taken.
  - tidels places type references from the tree (`type_tree` in `compiler/lsp/analysis.c`): each generic name and each type in its `<>` where it's written, however the `<>` are spaced; `type_written` stays for the names the parser keeps as text (an override's, an enum's in an attribute). `lsp_nested_types_are_placed_from_the_tree` checks it through a `>>`.
  - Nothing a game sees changed: old and new tidec over every game's source list generate the same C, byte for byte (178 games), and every error test says what it said.
- Fixed-length arrays are built (2026-10-06, the owner's `int[32]` spelling and values side by side; the remaining picks are Provisional in `docs/spec.md`, Arrays). What's built, and what code relies on:
  - A type tree keeps each `[N]` as a length expression, and `array_type` in `compiler/src/check.c` resolves its constant length. `array_of` makes one `DECL_ARRAY` per element type and length (`program.arrays`), with the element as its one field and `array_length` as the count. Elements are plain values; lengths and their product are at most 4096 (`TIDE_ARRAY_MOST`, `types.h`), but in an `audio` declaration's state, where an array can be a delay line of up to a megabyte's worth, which nothing copies (see Rendering and platform, Audio, A state can be big). Forward-declared struct elements are ordered with their arrays and checked after their fields resolve, including infinite-size cycles and fields that need the heap.
  - Generated C declares a struct of `N` elements per array type (`gen_array_types` in `compiler/src/codegen.c`), with get, set, slot and reference helpers (`gen_array_helpers`). An array is bytes of what holds it, with no engine allocation or new runtime API: snapshots, hashes, packing and layout migration use the existing plain-field paths. Indexes past the end read zero and writes do nothing. Copies, literals of exactly `N` elements, `foreach` copies, `count` and text are built; arrays go in components, structs, inputs and containers of plain values.
  - An input's arrays pack and repair element by element, with bounds and padding followed through structs and nested arrays (`needs_repair`, `gen_repair_value`, `gen_bounds`, `gen_clear_padding`). The padding and bounds loops keep distinct indices at each depth. `compiler/tests/e2e/arrays` pins the world's hash and checks copies, nested indexes, fields changed in place, text, input repair, snapshots and pack/unpack; `arrays_plain_input` checks repair is generated for integer-only struct arrays, and `arrays_forward` checks a struct element declared later. `compiler/tests/errors/array_*.tide` pin the refusals.
  - A plain field of a struct element is read through a pointer and changed at the element's slot (`element_in_place`, `element_path_in_place` in the checker): `rings[b].cells[j] = v` and `rows[i].x += 1`, in a list, map or array. A field that needs the heap still copies; the rule remains Provisional in the spec. Any member read off an indexed element marks its list or map as reached into (`decl.paths`, set in `check_expr_any`), a text's `.length` too, so the helpers that point at an element are written for types nothing reaches into that way: a map's (`tide_map<n>_at`, `_slot`) point at a value as its entry keeps it (`elem_ctype`: text as its handle), as a list's always did. They were written as the value's view type until 2026-10-08, and `names[key].length` on a `Map<string, string>` generated C that didn't compile (`compiler/tests/e2e/assets` reads one). tidels places an array's element type from its tree and offers `count` with its hover (`lsp_arrays_hover_and_count`, including `int[CAP]`).
  - **Measured** (`bench/versus/buckets`, saved as `arrays-final` and `arrays-final-kept` in `bench/results/versus.jsonl`, with the revision, binary hash and protocol in `arrays-metadata.json`): 4,096 rings of 32 values, release on Windows, one thread, three alternating rounds on a reasonably idle Ryzen 9 7900X (12 cores; CPU sampled at 0% before, 5% after), under the shared benchmark gate. Medians range from 0.04365 to 0.04420 ms a tick for nested lists, 0.01870 to 0.01880 for the flat list, 0.02030 to 0.02040 for arrays and 0.00720 to 0.00730 for C. With each tick's previous state retained, 0.06580 to 0.06600, 0.02850 to 0.02890, 0.02950 to 0.02990 and 0.01530 to 0.01700 respectively. Arrays write the ring's fields where they are, without copying the ring to a local: about 2.2 times as fast as nested lists, close to the flat list and still about 2.8 times C without snapshots, as the rings remain in an outer `List<Ring>`. Every way agrees, with and without the retained state, on one and twelve offered threads. The earlier `arrays-in-place` record was under load and is kept as history, not used as the final comparison; the twelve-thread C timings vary more than the one-thread ones.
  - Still Provisional: `count`, the 4096 cap, the plain-field write rule, no array constants, and arrays taking the latest tick's values in views without blending. The owner's approved spelling and storage are the Arrays section's Decided points.
- Containers nest, by one rule (the second step of generics, built 2026-10-06; `docs/spec.md`, Lists and Maps and sets, where the picks beyond "containers nest" are Provisional). A list's elements and a map's values can be lists, maps and sets, and structs that hold them, at any depth; a key and a grid's cell stay plain, and a grid goes inside nothing but an asset's list or map, which is never written. What's built, and what code relies on:
  - **The table** is `generic_forms` in `compiler/src/types.c` (`generic_form`, `generic_arg` in `types.h`): for each of `List`, `Map`, `Set`, `Grid2` and `Grid3`, what each type in its `<>` is (`GENERIC_ELEMENT`, `GENERIC_KEY`, `GENERIC_CELL`) and what a syntax error says of it; `generic_optional` and `generic_fails` are the forms around a type. The parser reads it for the `<>` (`parse_type`), the checker for what may go there: `resolve_generic` resolves each argument at its kind's place (`ARG_ELEMENT`, `ARG_KEY`, `ARG_CELL`) and `resolve_arg` checks it with that kind's rule (`element_ok`, `map_key_ok`, `grid_cell_ok`). `element_ok` takes containers and structs that hold them, and a grid only where the type is an asset's (`PLACE_ASSET`, which `asset_type` resolves through and then reads the shape of). `refuse_held`, `first_held`, the parser's `refuse_nested` and the fields' second look at their elements are gone, with the nine error tests that pinned what they said.
  - **An element that's a container is changed where it is** (`element_in_place` in `check.c`; `gen_slot` in `codegen.c`): `rows[i].Add(x)`, `rows[i][j] = v`, `buckets[key].Add(e)`, through any number of indexes, each a `tide_list%d_slot(l, i)` (a pointer to the element, or NULL past the end) or a `tide_map%d_slot(m, key, where)` (which adds the key). In a program that has such a type (`program_has_slots`), what changes a list or a map takes NULL for one that isn't there and does nothing; a program with none generates the helpers it did. Not through a struct element's field, and not `Sort`: both say to take the element out and put it back. A `parallel` loop's list refuses elements that hold lists as it refuses text, and C takes none (`c_refuses`).
  - **Ownership goes through the helpers there were:** a list's `put`, `drop` and `take` of an element that's a container call that container's `assign`, `release` and `own` (`gen_list_helpers`), and a map's value the same (`gen_map_slot`), so a world's inner lists are blocks of its own heap, released as the outer lets go of them (`Clear`, `RemoveAt`, `Resize`, a release), and made its own as a value comes in. Snapshots, hashes, deltas, the world as bytes and hot reloading see nothing new: an inner list is a block with pages of its own. `nested_lists_reuse_their_memory` checks the heap's use stays flat through 400 ticks of churn.
  - **A copy is deep where it has to be:** `tide_list%d_copy` and `tide_map%d_copy` of elements that hold lists copy each element's own (`element_copy_open`), as do a map's `Get` and `TryGet`, a foreach's variable and `gen_value_of`'s copies, so no scratch copy ever holds a handle into a world's heap, which a later change to the outer list would leave dangling. An element with a list in it goes into a list or a map as a copy of its own (`gen_value_of` at `Add`, `Insert` and `items[i] = x`), as `scores[k] = x` always did. Reading `rows[i][j]` and `rows[i].count` goes through the handles where they are.
  - **A `verified` value's elements say which heap they're in through every level** (`gen_confirm_value`, `gen_confirm_decl`): a list or a map whose elements hold text or containers is copied to the scratch area and each element retagged, inner lists and maps included, with `tide_e<depth>` and `tide_k<depth>` for the loops, which is the one change to the C of games that had `verified` lists of text (`tidec_e2e_verified_heap`, `view_queries`); every other game's C is byte for byte what it was (175 of 178).
  - **Measured** (`bench/versus/buckets`, `versus_buckets`, saved as `nested-lists` in `bench/results/versus.jsonl`; `nested-lists-removeat` is the same with `RemoveAt(0)` where the ring is): 4,096 buckets of 32 values each, a value a tick into each, as a `List<List<int>>` of rings, as one flat `List<int>` of rings, and in C. Release, one thread, with the run caches that write (`list-caches`, built the same day: see A world's list, under ECS and simulation state): 0.043 ms a tick nested, 0.019 flat (2.2x), 0.007 C; before them (`nested-lists`), 0.165, 0.082 and 0.009. What's left of the difference is one `tide_list_at_mut` for each inner list written, a page table lookup and the page made the world's own, as each bucket's is another list and a cache holds one run; the flat list's writes all go through its one cache.
  - Tests: `compiler/tests/e2e/nested` (every shape in a component, a singleton, an event, a local singleton and a view, changes in place through two and three indexes, copies that change nothing, text, a map's key added by `Add`, the heap's use, snapshots, the world as bytes, a pinned hash the web shares, and a `verified` view through the confirmed copy), `compiler/tests/errors/nested_*.tide` (a grid in a map, a map in a grid's cells and as a set's key, an element changed through a struct's field, `Sort` of an element, an extern's list of lists, a parallel loop over one), and the docs' Lists inside lists (`docs/language/text-and-lists.md`).
- User-defined generics, the third step (`struct Pair<T>`, `T Max<T>(T a, T b)`), are a proposal in `docs/spec.md` (Generics, Open: declaring, what a type argument can be, monomorphization and the names in C and tools, no constraints in v1 with errors at the instantiation, C#'s inference, the GPU, editors, recursion), awaiting the owner. Nothing of it is built.
- **Skinned model types and CPU operations** (2026-10-08; `docs/spec.md`, Skinned meshes and animation). `skinned_types.inc` builds ordinary generated structs for `SkinnedMesh<V, Contract>`, its animation and bone catalogs, `Skeleton`, `Animation`, `Bone`, `Pose` and `BoneTransform`, with internal `decl.skin_kind` markers. `Playback` is an ordinary enum. The parser's `SkinnedMesh` generic form takes the vertex struct and an optional ordinary struct contract; contract fields are required `Animation` or `Bone` values, with `[Name("source")]` for an exact source alias. `skinned_contract_catalogs` finalizes both asset contracts and generic helper types after their field types resolve. A plain generic parameter has the dynamic catalog; an uncontracted single imported asset has a distinct inferred catalog and `var` preserves it. Inference cleans exporter separators into writable identifiers, prefixes leading digits/keywords/reserved names, and disambiguates collisions with source ordinals; combined animation sources first use filename prefixes. Checked metadata lives on `decl.skin_catalog`; the editor reads it instead of guessing its own names.
  - **Scratch cleanup follows owned struct fields** (2026-10-10): `check_expr` marks temporary scratch use through the existing recursive `holds_heap` predicate, including `SkinnedMesh`, `Pose` and other structs holding text/lists. Previously only a direct string/list/map/set expression enabled the generated cleanup; a skinned-only view could copy a model's hidden lists without reclaiming them after its entity, leaking scratch across frames. String literals retain their exemption. Generated cleanup boundaries and runtime ownership are unchanged. `tidec_e2e_skinned_scratch` uses an omitted-pose model draw with no direct container/text expressions, checks the exact scratch mark before every entity and after each of sixteen frames, and verifies all thirty-two instances still reach the draw list.
  - **Skinned draws borrow stable operands** (2026-10-10): the existing once-only, written-order captures in `collect_ordered` keep model/Pose list handles without an owned copy when none of the later written operands has an ordered effect. The draw adapter reads these handles synchronously and captures the mesh, pose bytes and model dependencies before returning. Deferred atlas preparation reads that owned capture, never the borrowed operand handles (see `notes/shaders.md`). Later Tide writes, extern/C calls, nested effects or suspensions keep the ordinary owned snapshot, including effects in shader values and targeted draws. This changes no runtime signature, ordinary assignment/copy rule, persistent ownership or COW behavior. `tidec_e2e_skinned_draw_borrow` checks zero retained scratch allocation after asset and owned-model/shared-pose draws, a later operand releasing the model's lists, later Tide/C pose edits, and palette stability after a following statement edits the borrowed pose. The scratch-cleanup regression explicitly creates an owned local model before drawing, so it continues to pin per-entity reclamation independently of this draw-only optimization.
  - **Omitted skinned poses retain their rest identity** (2026-10-10): generated draw adapters pass omitted poses directly to `tide_draw_skinned_rest_atlas` or, when the instance attribute layout is full, `tide_draw_skinned_rest_data`. The existing literal default-pose flag selects this path without constructing a temporary editable Pose. Explicit Poses keep the same strict compatibility check, separate-file binding and generic capture calls; `model.defaultPose` and `Skeleton.RestPose()` still construct independently editable values. Written operands still evaluate once in order, and scratch marks still bound any temporary separate-rig binding. `tidec_e2e_skinned_rest` covers omission beside an edited explicit default Pose, subsequent edits, repeated authored palette bits, invalid explicit poses, operand order, textures, custom stages, targets and separate exports. `tidec_e2e_skinned_attribute_limit` covers omitted rest on both atlas and full-layout immediate paths. No syntax, pose arithmetic or shader layout changes.
  - `Animations` is also a lazy builtin value type (2026-10-10), holding an immutable list of source model-data lists. `asset Animations` imports a standalone clip catalog; a single asset receives its own inferred `SKIN_ANIMATIONS` declaration and direct dot members. `var` retains inference, while an `Animations` annotation erases it to dynamic lookup. Conversion into a checked catalog requires proof of its names, so an erased catalog cannot gain guarantees through assignment. Generated per-catalog adapters evaluate the source once and preserve the ordinary copy/ownership rules. List/map animation assets use the dynamic value type. The compiler shares normal model metadata inspection, format diagnostics, cache invalidation and content identity; no animation contract generic or runtime `Animations.Decode` method is added. `animations` E2E, `animations_*` error fixtures and `tidec_animations_identity` record coverage in `notes/assets-and-files.md`.
  - `Name` aliases must be nonempty: `skinned_contract_catalogs` diagnoses an empty alias even for a contracted helper parameter without an imported model. `skinned_source_find` skips empty exported names to agree with runtime `Find`; unnamed entries remain available through `At` only. `skinned_contract_empty_alias` pins the standalone helper diagnostic.
  - `skinned_ops.inc` checks catalog/member reads, `Find`/`At` returning optional values, `count`, animation `name`/`duration`, `Skeleton.RestPose`, `Animation.Sample(seconds[, playback])`, mutable `Pose.Sample(animation, seconds[, playback])`, `Pose.Blend(other, weight)`, `Pose.Model(bone)` and `pose[bone]` as a `BoneTransform`. Named arguments retain source evaluation order and an omitted Playback is Clamp. Pose changes use ordinary writable and copy rules; model geometry and hidden backing data stay read-only. These operations run on the CPU in systems and views; the numeric fields of `BoneTransform` remain ordinary GPU-plain fields. `SkinnedMesh<V, Contract>.Decode(List<byte>)` returns a model failing with `IOError`; generated adapters validate self-contained GLB data and the requested contract.
  - `program.asset_metadata_only` selects catalog-only inspection for the language server, with cached metadata from `model_metadata.c` (glTF JSON, or FBX without geometry and animation curves). Invalid versus unavailable catalogs stay distinct from a proven missing member; import diagnostics use ERROR versus informational `DIAG_INFO`, and dynamic lookup or a declared contract remains available. Normal builds still perform full import validation. Details, invalidation, measurements and language-server tests are in `notes/editors.md`.
  - Skinned vertex defaults can read assets whose values become owned match geometry, including defaults applied by runtime `Decode`. Codegen follows their checked expressions through constants, omitted struct and parameter defaults, user operators and helper bodies, implicit `TryGet`/out/binding defaults, and defaults of referenced static meshes; one pointer set visits shared or recursive nodes once. Only this dependency closure contributes logical pack fingerprints and its checked layout descriptors to the generated header. The same private fingerprint implementation checks incoming dependency payloads before loading the pack, preserving the old pack and globals on rejection. Compiler hashing and generated preflight each submit one batch: selected names are indexed once, the pack directory is scanned once, and the private index is released before returning. Payload digests and header identity remain identical to the scalar implementation; generated preflight compares static expected hashes without an array of digests on the stack. Imported skinned declarations submit a separate logical fingerprint batch covering canonical models, typed source vertices and indices, counts, layouts and map keys/index metadata. Those expected hashes remain private to generated C and leave the existing header identity unchanged; changing typed source geometry alone no longer passes the canonical-model check. Model-only programs also build the pack once early and embed the private checker. Defaults still fill after successful loading, and no checker adds an engine API. Unrelated visual assets do not affect this identity. JSON-only editor navigation skips this executable-pack step. A selected dependency also makes watched asset edits rebuild the game for Decode-only programs. Ordinary routine signatures follow result-type declarations and precede asset/container/Decode initializers, so operator defaults have their C declarations before use. `tidec_skinned_default_identity` covers direct and transitive dependency families, and `skinned_defaults` checks changed dependency rejection, retained values and accepted unrelated visual reloads.
  - A known inferred model converts to a contracted type only when all required animations and bones occur uniquely in its original catalog. `skin_sources` retains even invalid dot names for exact aliases; a dynamic uncontracted value cannot gain these guarantees through a conversion. Type adapters evaluate the source once, copy owned lists through the ordinary value path, then carry the same three model lists into the destination type. Built-in names are lazy fallbacks, so existing user structs such as `Pose` still resolve first. `BoneTransform` literal/default values have position zero, identity rotation and unit scale, including on the GPU and in migration layout defaults.
  - Focused checker error tests: `skinned_pose_index`, `skinned_pose_readonly`, `skinned_pose_element`, `skinned_sample_playback`, `skinned_hidden_data`, `skinned_generic_member`, `skinned_geometry_readonly`, `skinned_decode_bytes`, `skinned_decode_vertex`, `skinned_contract_conversion` and `skinned_static_method`. They pin Bone indexing, writable Pose receivers, copied list elements, argument types, hidden storage, erased helper catalogs, immutable geometry, byte decoding, validated vertex layouts, refused conversion to an unmet contract and the static method boundary.
  - `compiler/tests/e2e/skinned` has 38 Tide assertions over inferred/contracted access, two collection assets, normalized and aliased names, typed helpers and explicit conversions, dynamic enumeration, negative Loop/Clamp time, blend, scalar/swizzle/whole-transform edits, sockets and runtime GLB contract failures. Its C harness checks persistent decoded model and Animation/Pose ownership, unchanged snapshots while another world samples, replay and packed-world round trips, constant heap use, and rejection of structurally valid changed canonical data without replacing the old asset pack. The match hash after the round trip and replay is `7267824136268331061`, shared by native and web-debug. Codegen routes `CALL_SKINNED` statements through expression emission rather than entity mutations; numeric fields and swizzles after `pose[bone]` follow the element slot, and BoneTransform defaults use valid identity/scale AST values. All four E2E tests and all eleven errors passed on native and web-debug.
- Diagnostics printed to a terminal are colored (`report` in `compiler/src/common.c`: the place bold, `error` red, `warning` yellow, `note` cyan, the excerpt's gutter dim, the caret its diagnostic's color), and never to a pipe or a file, nor with `NO_COLOR` set, so tests, editors and CI read the text they did. A file under the folder the compiler was run in is named from there (`Game.tide:32:47`), and whole otherwise; the JetBrains plugin's filter resolves such a path against the game's folder.
- A local that's declared and never read is a warning (assigned but never read counts), with a note on what to do: remove it or use it, or keep its call as a statement of its own when the call does something (`warn_unused_locals` in `compiler/src/check.c`). Warnings fail this repo's builds, so its `.tide` files have none; the language server fades the local and offers both fixes.
- **`#if` reads symbols beyond tide's versions** (the owner asked for `#define`, defines from the command line, `#if DEBUG` and knowing whether a package is installed, 2026-10-07; the rules are Claude's picks, Provisional in `docs/spec.md`, Conditional compilation; `docs/language/namespaces.md`, Symbols of your own, is the users' page). What's built, and what code relies on:
  - **It's all the lexer's** (`compiler/src/lexer.c`). A symbol is defined or not; one nothing defines is false, but a `TIDE_` name that's no version, which is still the error it was. `#define` and `#undef` are a file's own (`file_symbols` in `lex_impl`, the last word standing), and only before the file's first token (`toks.count == 0`), as in C#.
  - **A build's symbols are the lexer's statics** (`build_symbols`): `lex_symbols_reset`, then `lex_define` and `lex_define_package` (`PACKAGE_BOX2D` for `Box2D`), before a game's files are lexed. `compile_inputs` does it from `codegen_options.defines` and its inputs' packages; `analysis_run` (tidels) defines `DEBUG` and the game's packages, as it can't know a command line's. Their arrays are the arena's, so the reset zeroes them whole: whoever resets the arena resets the symbols before it lexes again.
  - **Where they come from:** tidec's `--define <S>` (and a `--define <S>` line of an `@list`), `tide`'s `--define` on `run`, `build`, `schedule` and `shaders` (`build_options.defines`, with `DEBUG` added in `compiler/cli/main.c` unless `--release`), and `tide_add_game(... DEFINES ...)`, which also defines `DEBUG` when `CMAKE_BUILD_TYPE` is Debug. `lex_symbol_ok` is what both command lines check a name with: `TIDE_`, `PACKAGE_`, `true` and `false` are refused, as `#define` refuses them.
  - **A build's symbol that a condition read is in the game's hash:** `read_build_symbol` notes each, by name, with whether it was defined (`lex_symbols_read`, in order of the names), and `gen_header` writes them as a comment after `#pragma once`, only when there are any. So builds that read one differently are different games, which no session connects, and a game whose conditions read none (or only versions and their files' own) generates the header it did. A hot reload that adds the first `#if DEBUG` changes the hash once, and migrates by name.
  - **Editors:** `#define` and `#undef` lines are directive lines (`LINE_DIRECTIVE`), which the formatter leaves and the folding of `#if` branches passes by; the grammar has both words.
  - **Tests:** `compiler/tests/e2e/defines` (a file's own, `#undef` of a build's, a build's in another file, `DEBUG` by the build's configuration, a package that's there and one that isn't), `compiler/tests/errors/directive_define_*` and `directive_undef_package`, and `lsp_directives`.
  - **Not built:** values, package versions as symbols, a place in the game's folder to list defines (so an editor knows them), `DEBUG` for a game's C.

- **Combined animation imports and writable names** (2026-10-11): `check_asset_decl` records resolved constant path expressions in `decl.asset_paths`; only plain `Animations` accepts a literal file list or a glob as one combined inferred catalog. `[Name("source")]` on singular `Animation` selects a unique original clip; implicit selection requires exactly one clip. The checker, codegen and editor share cleaned members and flattened source ordinals, with filename prefixes for cross-source alias collisions. Contract matching remains first-letter-only or exact `[Name]`; dynamic `Find` remains exact and ambiguity-sensitive. `Animation` retains its existing raw model data/index ABI. `Animations.data` is now a nested source-byte list; the generated private catalog adapters borrow a model blob for direct operations and wrap it only when a catalog escapes. Ordinary nested-list ownership supplies snapshots/serialization without engine changes. Inferred conversions check source ordinals before carrying indexed members to a different catalog type. See `notes/assets-and-files.md` for fixtures and validation.

## Confirmed interpolation and singleton methods (2026-10-11)

The owner approved singleton methods. Structs, components and singletons now support ordinary readonly/mut methods, overloads and sibling calls, preserving caller capabilities and explicit singleton parameters. Singleton methods have no entity `this`; custom `Interpolate` still belongs to structs/components. Readonly receiver snapshots preserve container aliases through calls that also mutate the receiver. `methods`, `singleton_params`, readonly-error fixtures and LSP completion/hover tests cover these contracts.

Verified view parameters, mixed joins and query bindings interpolate against confirmed previous/current endpoints and their own fraction. Membership/discrete fields use the upper endpoint; Snap, birth identity and custom interpolation apply, without prediction correction smoothing. Verified handlers remain exact. Custom interpolators reading heap values get independent endpoint snapshots, including nested text/list/map/set values and readonly grid chunks. Scratch cleanup and both world hashes are pinned by `verified_heap`; ordinary numeric interpolation keeps its existing path. `tide_confirmed` retags only match offsets, preserving scratch/local values returned by custom interpolation.

`settings.maxRewind` is finite nonnegative constant seconds, default zero; positive values reserve the input field `ViewTime viewTime`. The checker blocks explicit/member and Sample/Sanitize field writes. ViewTime contains upper tick, fraction and validity. Input repair invalidates malformed timestamps before ordinary float repair. `Time.maxRewind` is retained only when used. Constant, negative/nonfinite/dynamic, reserved-name and readonly diagnostics are covered.

## Shared component placement and Transform foundation (2026-10-12)

The owner approved one component definition usable in match or local placement,
a built-in Transform, checked Unity-style parenting, descendant destruction,
and placement-specialized component methods. The prefab/editor design remains
future work; this change implements its entity foundation only.

`component` defines values/defaults. `Spawn(...)` places match entities and
`local Spawn(...)` places local entities. Access bindings use `local`, including
`mut local Marker marker` and `query (local Marker marker) markers`. Entity and
LocalEntity handles choose placement for Add/Remove and World Get/Has/TryGet/Set.
Local component declarations are rejected with the replacement spelling. Local
singletons/scenes/events and attached local fields keep their existing rules.
Rendering resources and LocalEntity fields (including optional/container shapes)
are rejected at match placement/access. Attached local fields currently restrict
a definition to match placement; local placement of that shape remains open.

Private local storage specializations share the source component's C value type
and ownership helpers. Components remain one public type; masks/storage retain
separate world identities. Component methods get independent parsed syntax trees
for local placement, instantiated on calls, with typed implicit this and ordinary
caller capability propagation. Calls through methods, overloads, mut methods,
World helpers and task starts retain their effects. Explicit Entity/LocalEntity
parameter and return types keep their meanings. Unused methods are checked once
in match placement; a method called only locally is checked in local placement.

Transform has zero localPosition, identity localRotation and unit localScale.
SetParent(parent, worldPositionStays = true) records an ordered deferred command;
null detaches. World.LocalToWorld(entity) reads stored TRS root to leaf and
World.Parent(entity) reads the typed parent. Reads contribute Transform-wide
scheduler dependencies and helper callers keep their capabilities. View reads
use stored state, as existing World.Get does, not interpolated parameter copies.
Parentless reads avoid scratch allocation; deeper reads use bounded scratch
ancestor chains. No cached world matrices or timing improvement is claimed.

Links live in private per-world Map<Entity, node> singleton state, using existing
heap ownership, snapshot/hash/packing/layout machinery. Parent commands settle
both temporary handles, reject cycles/dead handles at application time, and
leave links/local values unchanged on failed keep-world conversion. Keeping
world placement needs the child's Transform and an invertible finite parent;
TRS cannot preserve general shear from rotated nonuniform scales exactly.
Destroy walks descendants iteratively before each parent, with Destroyed while
that entity is still readable. Loaded scenes remain independent roots; parenting
an ordinary subtree into another scene transfers its membership, while detaching
retains ownership. No engine runtime API or global type registry was added.

Validation: focused Transform tests cover match/local storage and hash isolation,
implicit-this specialization, method chains/overloads/mut methods/task starts,
rotation/scale, cycle/dead/singular rejection, ordered deferred changes,
descendant destruction, COW snapshots and packed world restoration. Compiler
fixtures preserve access restrictions and cover cross-world parents, local
component declaration replacement, local-field placement and nested local handles.
Final Windows debug validation passes all 2,241 native and 2,209 web CTests,
including native GPU and WebGL/WebGPU coverage, the exact existing determinism
pins and all 224 language-server checks. All 228 shared grammar checks and the
production docs build pass with Node 24.16.0. Two initial full-suite failures
were stale schedule text after renaming old fixture Transform types; those pins
were corrected before both final full runs. A final diagnostic-only wording
cleanup is checked with the LSP suite and replacement/unknown-World fixtures.
No CI, sanitizers, Android, Linux, macOS or MinGW coverage was requested/run.

Remaining work: prefab text/editor authoring, hierarchy presentation interpolation,
world-matrix caching/deep-hierarchy performance measurements, richer hierarchy
inspection, and broader platform CI (not requested for this Windows task).
