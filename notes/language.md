# Language features, as tidec builds them

## Local workers (2026-10-24)

Owner-approved direction and spelling: `local worker` functions, awaited from
local async code, with owned CPU inputs/results and existing loop rules.
Bare `worker` stays rejected for a future deterministic-match contract.
Implementation choices are distinguished from decisions in the spec's Tasks.
worker_check.inc/worker_codegen.inc extend the existing checker/codegen.

Calls decide an async caller's side as local and propagate through its normal
starters/awaited parents. Bodies run to completion without awaiting. Reachable
world/frame, rendering/audio, filesystem/network, live-asset, task-start and
implicit match-randomness access is rejected. C stays trusted. Boundaries accept
owned CPU values, including nested containers, plain fixed arrays and optional/
failable results; bindings, resources, queries, devices, grids, Actions and
mut/out parameters are refused. Overrides preserve worker execution kind.

Generated walkers fully materialize inputs, including packed lists, into a
private heap before dispatch. Each job also owns an output heap. The frame
acquires detached scratch results before releasing a completed job. Ephemeral
optional/failable strings own private bytes instead of caller scratch pointers.
Only integer tickets enter local task frames; restore/rebind clears them and
restarts pending calls. An owner disappearing abandons publication while the
group retains captures until callback completion. Reload/disposal drains jobs
before code unload.

Explicit parallel loops accept owned local plain-value lists. Output lists and
readonly captures are heap-backed before sharing, with the existing independent
step rules. List foreach/for order is unchanged. Automatic foreach analysis is
currently grid-only; workers do not take grids, so this does not extend that
analysis. Worker callback emission also closes list-cache patches before
gen_tasks switches output buffers; the otherwise-open patch caused a compiler
crash with worker loops plus multiple async functions.

Coverage: local_workers checks geometry/text ownership, discarded scratch and
changed sources, real pool execution, owner loss, saved local state and retained
rebind. worker_values checks nested text lists/maps, fixed arrays, optional
strings, failable strings and void. Negative fixtures cover bare/misused worker
declarations, match starts, mut inputs, bound values, frame helpers and assets.
Pool stress runs more jobs than threads, each with parallel work. LSP and shared
grammar pin hover/symbol/formatting/highlighting.

Validation: warning-free native/web debug builds, full suites passing 2,604
native and 2,499 web tests, with the existing disabled MicroDuck right-turn case
unchanged. Final test additions pass all 12 selected checks on each target,
including readonly worker captures, threaded optional/error values, all eight
worker diagnostics, the complete 250-case LSP executable natively and a served
browser frame running background/parallel work beside simulation. The shared
grammar passes 282 cases and the production docs build passes. Clang 23.1.2,
Node 24.22.1, Windows; native GPU/WebGL/D3D11/WebGPU checks pass. This checkout
has no Vulkan validation layer. Android, other OSes, MinGW and sanitizers remain
CI coverage; no CI workflow was dispatched. Existing determinism pins are intact.

Remaining: owned grids, priorities, public handles/explicit cancellation,
preemption and match workers. Existing async component/local-field restrictions
are unchanged: publication currently uses local singletons or local entities.

Integration with incoming callable/shader support retains both notes entries.
Callable and shader-binding structs are explicitly excluded at worker value
boundaries, including nested fields: they are local execution/rendering values,
outside the initial owned CPU-data contract. worker_callable pins the rejection.
Merged validation through origin/dev d90a787c: warning-free full native/web
debug builds and all 2,636 native and 2,531 web enabled tests pass (245.43 s /
425.05 s, four jobs), retaining the existing disabled MicroDuck case. This
includes incoming callable/Render3D checks and native/browser worker coverage.
All 286 shared grammar checks and the merged docs production build pass.

## Mutable callable arguments (2026-10-24)

Owner-approved: `Func<mut Vertex, void>` accepts a matching named function or
typed expression lambda, forwards the caller's writable storage through helpers
and generated dispatch, and leaves captures read-only. This applies to ordinary
CPU values and GPU-local values under existing access rules; no runtime or
backend interface changes. `out` callable parameters remain outside this change.

The parser records each Func argument's mut location in `type_ref.arg_mut` and
retains it in type text. `decl.call_mut` participates in signature interning,
generated type/dispatcher identities, target identity and overload selection.
Mode prefixes distinguish mutable slots in those identities. Inferred function values
retain their modes. The generated dispatcher's ordinary parameters carry
PARAM_MUT, so checking, ownership, evaluation order and C/GLSL/WGSL forwarding
reuse normal calls. The result cannot be mut. A default/unknown target performs
no writes. Lambda parameters now carry function_param, including mutable ones:
without it WGSL treated a forwarded mutable lambda argument as a value and
generated an invalid pointer-to-value copy. Captures are always PARAM_READ.

`e2e/mut_callables` checks scalar and field writes, forwarding, returned captured
lambdas, named/inferred targets, exact mode separation, multiple mutable aliases,
left-to-right reads beside writes, defaults and unchanged match hashes. Array
elements retain the ordinary take-out/change/put-back rule. Seven diagnostic
fixtures cover wrong modes, lambda modes, read-only arguments/captures and a mut
result. `e2e/gpu_functions` now deforms each SDF quad through distinct named or
captured mutable callbacks, with compensated centers keeping the same pixel
expectations; its frag also reads a nonvoid callback's result and changed argument.
Configured mutable vert bindings are exercised too. The smaller `shaders/mut_callables`
fixture pins GLSL/WGSL and joins available SPIR-V/HLSL validation.

Validation: full native debug passed all 2,605 enabled tests (165.28 s, four
jobs); full web debug passed all 2,501 (334.33 s, four jobs). Each retains the
existing disabled Microduck right-turn limitation. Native pixels passed on
Vulkan/OpenGL; web pixels passed on WebGL, ANGLE D3D11 and WebGPU. FXC validated
the mutable-callable golden; the complete 250-test language-server executable,
all 282 grammar tests and the docs production build passed. No determinism
expectations changed. Web uses the same ignored SDK 34 fallback described below;
pinned SDK 24 downloads and GitHub fetch still fail DNS resolution. There is no
standalone spirv-val or Vulkan validation layer on this machine.

Integration (2026-10-24): fetched and merged origin/dev through 85f14e4f after
GitHub DNS recovered. Compiler/editor code merged cleanly; the two top-of-file
notes conflicts retain both the callable entries and incoming editor/startup
entries. The merged full native debug suite passed 2,621 enabled tests (211.08 s)
and full web debug passed 2,515 (361.12 s), each at four jobs with the existing
disabled Microduck right-turn limitation. New editor/startup coverage and the
callable CPU/GPU probes pass together. The merged docs production build passed.
Web retained the local SDK 34 fallback above. No code corrections or additional
determinism expectation changes were required for integration.

## Callable values and configured GPU stages (2026-10-24)

Owner-approved foundation for separate `render3d` and future `render2d`
packages: typed function parameters in GPU code, helper forwarding, explicit
`Shade { distance = Circle }` bindings, local component storage, and heterogeneous
instance callbacks dispatched in one shader. No rendering package is implemented
here. Render3D's PBR, baked indirect lighting, probes, shadows and post-processing
remain the agreed direction, not implemented features.

`callable_types.inc` and `callable_check.inc` implement `Func<A..., R>` as private
ordinary structs containing a target tag and owned typed capture fields. Named
functions and typed expression lambdas populate a closed target set per resolved
signature. Generated ordinary dispatcher functions initialize a result, switch
over targets, and return once; this avoids ANGLE D3D11's X4000 on returns inside
switch cases. CPU and GPU use the same checked AST and normal call/effect/resource
analysis. There are no stored native function pointers, new runtime functions or
generated-C/runtime ABI changes.

The initial language spellings and restrictions are provisional in spec.md,
Callable shader values. Captures copy plain values at construction, including
numeric structs and fixed arrays; no stack borrow escapes. Resource/heap captures,
borrowed component/world/query/input context, nested lambdas and recursive capture
layouts are diagnosed. Explicit parameters and named assets retain their normal
GPU rules. Missing/unknown callable tags return the result's default. Configured
stages with invalid required tags skip submission. These values and their
collections are local-only, including through nested structs and attached fields.

A stage binding is a private struct associated with its declaration. Its public
type is the stage's name. Each function parameter reads its named binding field;
other stage parameters retain their existing source binding. Draw wrappers receive
binding values explicitly, separately from ordinary draw values and components.
The Mesh, Instanced, target, skinned and Blit paths share this machinery. Binding
factories evaluate once, in written draw-operand order. Callable and stage types
go through normal array wrapping; stored indexed stage values can be drawn.

Capture fields and named function tags have stable qualified identities for
ordinary local serialization/migration. Lambda identity also uses its containing
declaration and source location: moving one may invalidate old bindings, which
must then be reconstructed. Unknown identities never retarget arbitrary code.
Target identity collisions are diagnosed. Captures currently reserve fields per
target of a signature; compact overlapping payloads and static-target specialization
remain performance work. Existing GPU uniform/attribute limits apply.

Coverage: `e2e/gpu_functions` exercises exact overload selection, qualified function
references, inferred named functions, helper forwarding, returned closures,
capture-at-construction semantics, local/attached storage, local pack/unpack,
stored/returned/inline/indexed stage bindings, fixed arrays, custom vert plus frag,
Blit, zero-argument/void/default callables, skipped unconfigured stages, factory
evaluation order and unchanged match hashes. Four distinct SDF instances remain
one run. Nine GPU/CPU lambda capture variants force field-vector growth: earlier
callable literals resolve their stable field names during emission, rather than
losing initializers through stale arena-copy field pointers.
`shaders/callables` pins GLSL/WGSL and joins SPIR-V/HLSL validation. Eight diagnostic
fixtures cover invalid signatures, captures, placement and bindings. GPU pixels
and actual draw counts are checked by `platform/tests/gpu_functions.c` on native
and web; editor contracts are in editors.md.

Validation: full native debug passed 2,595 enabled tests (final run 169.29 s,
four jobs), and full web debug passed 2,492 (667.80 s). Each retains the existing
disabled Microduck right-turn limitation. The final array/indexed-binding changes
also passed all 13 scoped web checks after rebuilding the affected targets.
The docs build and all 280 shared grammar tests passed. Native pixels ran on
Vulkan and OpenGL; web pixels ran on WebGL, ANGLE D3D11 and WebGPU. FXC validated
the HLSL fixture. Standalone spirv-val and a Vulkan validation layer were absent.

GitHub DNS prevented downloading pinned WASI SDK 24. Web validation used an
ignored local toolchain file with the already-installed SDK 34 sysroot/builtins,
clang 23 and Node 24.22.1; all Tide tools/programs were built in this checkout.
The checked-in toolchain is unchanged; pinned-SDK-24 coverage remains unavailable
on this machine. External prototype directories were disabled for both presets.

## Inherited prefab values and selected startup (2026-10-24)

Owner-approved: matching nested `entity ExistingChild { Component { field =
value }; }` blocks override inherited descendant values per instance. Only
existing component fields may change; no inherited component/child additions,
removals, replacement or alternate base. Nested-instance roots retain ordinary
authored additions. prefab_check.inc merges matching blocks into the existing
composition without adding entities or changing reference scopes, defaults,
placement checks, complete-tree construction or root-first Spawned order.
Duplicate blocks/fields and unknown/type-invalid/empty/bare/new component
overrides are diagnosed. The native editor writes/reset these same value blocks.

The project may choose `settings { startScene = Qualified.Name; }`; CLI
`tide run/build <project> --scene Name` overrides that choice for compilation.
Absent selection keeps existing Main generation. Names select prefab or scene
declarations, never text/calls/initializers. CLI lookup has no source-file
namespace/using context. A selected prefab lowers through a checked private
Scene.Load factory, loading its authored root in match placement without a
wrapper or handwritten Main. Ordinary selected scenes retain defaults and
match/local placement. Normal lifecycle, views, input/camera, Session.Start()
default and empty-world fallback use the selection. Main source is untouched;
duplicate Main and invalid/duplicate persistent settings remain errors even
with an override. Startup enters simulation identity and asset-use analysis.
No generated-C/runtime API was introduced.

The native editor's Play uses private original-path source replacement and an
isolated cache to compile applied drafts, then the ordinary host; these private
flags are intentionally absent from public docs/help. Persistent startup is
written separately by Set startup. Authoring preview still skips gameplay
initialization. Current user docs/spec replace earlier Main-only and inherited
read-only statements; historical sections below describe their original builds.

Validation: full native/web debug builds pass; the engine/compiler/platform
checks, 249 LSP cases and all seven final editor CTests pass. Prefab coverage
includes deep overrides/defaults/references/lifecycle/entity counts on match and
local placement, plus twelve structural/value diagnostics. scene_startup pins
normal lifecycle, default Session.Start/fallback, snapshots/packing, source
identity/asset closure and CLI/persisted/overlay selection. Startup authoring
passes 18 source-preservation/namespace/conflict cases; the complete native UI
creation/extraction/instance loop and twelve inspector focus/undo regressions
pass. The production docs build passes. The unrelated external Cracked hash-pin
failure reproduces with the HEAD compiler, and optional external browser
playthroughs were not completed; details and remaining platform coverage are in
notes/editors.md. No expected deterministic result was changed.

Remaining: inherited structure edits, top-level variants, language-server entity
path refactors/completion, child-to-prefab-root path syntax, authored attached
local fields and automatic live-instance reconciliation. Authoring transactions
and extraction/reference limits are in notes/editors.md. CLI integration and
validation belong to notes/cli-and-packages.md.

## Shard attributes (2026-10-23)

`reach`, `[Position]` deciding where an entity is, `[Native]` and `[Together]` (which replaced `shardRange`, `[Partition]`, `[PerShard]`, `[Shared]`, `[Ghosts]`, `[GoesWith]` and `Shards.moves` the same day): rules, checks and generated code are in `notes/sharding.md`, The language side; the spec's Shards section has them as Provisional. `field` has `native` as its last member, so positional `field` initializers end in `false`.

## LookRotation of any length (2026-10-22)

Found by the BANKED prototype's first-person arm IK:
`quaternion.LookRotation(elbow - shoulder, up)` with a 0.29 m forward turned the
bone the wrong way and scaled it by 0.58. `tide_lookrotation_q` used `forward`
as the matrix's third column unnormalized, so any forward that wasn't unit
length built a matrix that was no rotation and Shepperd's method returned a
wrong, non-unit quaternion. The docs gave LookRotation no unit-length
requirement, and FromToRotation (2026-10-18) and Unity accept any length, so
it was a trap rather than a contract. A conventional completion under the
existing math maintenance; no signature, builtin or generated call changed.

- **Behavior (decided, spec and `docs/language/math.md`):** forward and up are
  normalized with FromTo's rescaling (`tide_fromto_unit_f3`), so finite nonzero
  vectors across the float range work. A zero or nonfinite forward gives
  identity. If `cross(unit(up), unit(forward))` has squared length below 1e-10
  (length 1e-5, Unity's `Vector3.kEpsilon`), which covers a zero or nonfinite
  up and one along forward either way, the result is
  `tide_fromto_q(float3(0, 0, 1), forward)`: Unity's own fallback
  (`Quaternion.LookRotation(Vector3.up)` is euler (270, 0, 0)), reusing
  FromTo's decided half-turn axis for forward = -z. Otherwise the side is
  normalized, up becomes `normalize(cross(f, side))` and the right column
  `cross(up, f)`, so the frame is orthonormal to rounding even when the first
  cross product is inexact (up within a few degrees of forward), and the
  quaternion is normalized. The threshold rather than exact zero also keeps
  GPUs that fuse the cross product's arithmetic on the CPU's branch.
- **Results change for every call,** unit inputs included, by the rescale and
  final normalization (a few ulps); no cross-platform pin includes
  LookRotation, and none changed. The previous parallel case returned the
  degenerate matrix's quaternion, which only the old test's "no NaN" accepted.
- **Tests:** `math_lookrotation_takes_any_length_and_keeps_its_frame` (the
  arm's 0.29 m case, lengths from subnormal to the float maximum, agreement with
  unit inputs, 10,000 random pairs at four scales checking a unit result,
  forward equal to normalized forward, up perpendicular, on up's side and in
  the plane of up and forward, and a frame 2e-5 off up outside the fallback);
  `math_lookrotation_without_a_side_turns_from_z_and_without_forward_is_identity`
  (zero/NaN/infinite forward give identity bits; up parallel, opposite, scaled,
  1e-6 off, zero, NaN, infinite and the float maximum each equal FromTo from
  +z bit for bit; straight up is Unity's (270, 0, 0)). The old parallel check in
  `math_normalize_gives_zero_for_what_has_no_direction` now also requires a unit
  result. GPU side and probes: `notes/shaders.md`. `compiler/tests/safety/math`
  still calls it with arbitrary values.

Validation: full native debug passed all 2,515 tests (Vulkan and OpenGL
probes included). The full web debug run passed 2,415 of 2,416; the one
failure was the new helper's X4000 on ANGLE D3D11, fixed with a single return,
after which the three web shader pixel tests (WebGL, WebGL on D3D11, WebGPU),
the golden/HLSL/SPIR-V checks, engine tests and math fixtures passed on both
native and web. Other platforms are CI's; no workflow was dispatched.

## Computed properties and attached component context (2026-10-21)

Owner-approved: user-defined C#-style get/set properties, with the engine using
the same facilities; world position on Transform; attached components can reach
their entity/world; performance is a requirement. The decided/provisional split
in docs/spec.md distinguishes that direction from the implementation spellings
and edge choices below.

Parser properties are separate from stored fields. Accessors are ordinary
methods with independent component-placement bodies. Reads lower to getter
calls; assignments lower to mut setter calls. `value` is an implicit read-only
setter argument. Getter-only and setter-only declarations work. Compound
assignment evaluates the receiver once, getter, RHS, then setter; numeric
increments retain the calculated value before a clamping setter runs. CPU and
GPU lowering agree. A returned property value cannot supply a writable subfield
or mut/out reference. Literals initialize stored fields. Accessors inherit normal
caller effects, scheduling, permissions, container ownership and GPU restrictions.

Component methods/accessors have contextual `world` and existing entity `this`.
Only direct component parameters/query bindings supply ownership; synchronous
forwarding retains it and explicit copies detach. A fixed-point pass requests
entity and world side arguments only for helper parameters that need them.
Ordinary field-only helpers keep their existing ABI. `local Transform` on an
ordinary helper selects local context; a value-only helper retains the existing
copy rules. Task component references derive context anew on resume. Verified
bindings retain their world through helper/query/Action forwarding. Recorded
match-event values retain identity but do not grant historical World access.
No pointer enters stored components, snapshots, journals or task frames.

A component method uses contextual world access or an explicit World parameter,
not both. Fields/parameters named world retain their existing shadowing. Structs
and singletons gain properties but no implicit entity/world. Setter permissions
follow mut-method permissions; match writes remain forbidden in views and local
state remains forbidden in the match.

Transform.position is parsed Tide accessor code, using public
World.ParentToWorld(this) and its current localPosition. ParentToWorld is a
generated hierarchy query, including retained visibility anchors when a parent
has been projected away. Ordinary roots/dead handles give identity. The setter
inverts that parent matrix and changes only localPosition. Nonfinite inputs,
singular matrices and nonfinite results leave it unchanged. Identity parent
matrices avoid inversion. Ancestors remain stored-state reads. Engine accessors
instantiate only when called, so unused properties introduce no hierarchy state.
Storage stays the three local TRS fields. No runtime export or cache was added.

properties exercises explicit/implicit accessors, getter-only/setter-only forms,
clamping prefix/postfix increments, compound order with C probes, checked array
paths (including nested fields and invalid indexes), list ownership/copies,
attached helper/query/Action forwarding, parent-relative conversion, in-flight
local writes, local/verified worlds, unchanged frame hashes, singular parents,
snapshot replay and packing. visibility_projection compares property positions
bit-for-bit with composed stored matrices at hidden hierarchy boundaries, while
retaining its existing hash pin. Nine negative fixtures cover invalid access,
detached context and view writes. The old query-this/helper-this error fixtures
now detach explicitly; their previous direct bindings are valid. Shader goldens
and the CPU/frag/vert PropertyProbe cover accessors and clamping increments.

Performance probe: Clang 23.1.2 -O3, no fast-math/FMA; generated game and baseline
compiled together, linked against this checkout's debug engine. Baseline C takes
the same bound Transform, checks its parent, returns localPosition at a root,
otherwise transforms it by the stored parent matrix. No visibility policy in
the probe. Two noinline wrappers keep call boundaries equal; inputs vary each
iteration. Minimum of five alternating warmed C/property runs, repeated reads
at depths 0/1/32/256 (1M/300K/30K/3K iterations per run). Native nanoseconds per
read, C/property: 11.08/11.83, 74.56/76.81, 2436.88/2426.05,
19112.60/19020.87. WASI with ordinary SIMD under Node 24.21.0:
16.83/17.45, 129.18/130.32, 3520.14/3501.62, 27780.27/27621.30.
Root overhead is below 1 ns in these runs; deeper differences approach noise.
These are compiler-abstraction comparisons, not release-engine end-to-end
benchmarks. Existing hierarchy traversal/scratch cost remains; no cache or
arithmetic reordering was used to improve the numbers.

Validation: complete warning-free native/web debug builds and both full suites
completed (2,465 native / 2,406 web). Their only initial failures were the three
obsolete this/query/World diagnostic expectations described above. Updated
negative cases and final container/Action/packing/recovery additions pass all
15 selected native and 14 selected web checks, including the full language-server
test executable. Existing determinism pins are unchanged. Native GPU, OpenGL,
WebGL/D3D11 and WebGPU tests pass; this machine has no Vulkan validation layer.
All 274 shared grammar tests and the production docs build pass. Android, other
operating systems and sanitizers remain CI coverage; no CI was dispatched.
Integration: merged origin/dev through 97848ffe. Source merged cleanly; both
notes retain the incoming entries. Rebuilt overlapping compiler/editor, hierarchy,
prefab, engine/stats and embedding targets; all 1,974 selected native and 1,970
selected web checks pass, including every compiler diagnostic, complete editor
tests and the new prefab duplicate-error fixture. CLI/server tools rebuild;
stdio, pipe, CLI unit and telemetry checks pass (4/4, telemetry repeated against
the rebuilt CLI). The merged docs production build passes. A final narrow
regression ensures a property called Interpolate stays a property rather than
being interpreted as the engine's method callback; properties pass again on
native/web after that guard. Existing actual Interpolate diagnostics retain
their callback rules.

## Names in other namespaces, and Unity's folder names (2026-10-21)

An unknown name's error lists the declarations of that exact name in other
namespaces, the first four and how many more (`other_namespaces_note` in
`check.c`, before the "did you mean" note, which it takes the place of): one
"there's 'A.B.X': write it so, or 'using' its namespace", several "there's
'A.X' and 'B.X': write the one you mean". `find_in` falls back to
`find_in_folders` for a name `UnityEngine.Prefabs`, `UnityEngine.Scenes` or
`UnityEngine.SceneContents` hasn't: the one declaration of that name in their
sub-namespaces, as tide unity names a project's prefabs and scenes by their
folders (`notes/unity.md`, Named by their folders). It scans the declarations,
on a miss in those three namespaces only. Test: `tidec_unity_folder_names`.

## A diagnostic is said once (2026-10-21)

The owner removed a field from a component and Unity's Console had each of the
three prefabs' and scene's "has no field" errors twice. A prefab's values are
checked on their own (`prefab_check_values(..., neutral)` in `check()`, so an
unused prefab's errors show in editors) and again at each Spawn or Load of it
(`check_prefab_spawn`), and each check said the same errors: a prefab spawned
in three places said them four times. `common.c` now keeps a 64-bit FNV hash
of each error, warning and info said since `diag_reset` (severity, file, line,
column, message: `said_before`) and drops one it said already, with its
notes and suggestions (`muted`, until the next diagnostic), and counts it as
no error of its own; tidels gets one too, through the same path. Messages are
formatted once (`message_of`, malloc'd past 1024 bytes, where the sink cut
them at 1024 before). A collision of two hashes would drop a real
diagnostic, which 64 bits make unlikely. A "has no field" error in a Unity
prefab or scene (its source's path ends in `.prefab` or `.unity`:
`unity_authored`) is neither: the value is left out of the literal, saying
nothing (owner, the same day: `notes/unity.md`, Fields the component no longer
has). Test:
`tidec_prefab_error_once` (`compiler/tests/prefab_error_once.cmake`: a
`.tprefab` spawned twice says the error once; the same text as a `.prefab`
builds, saying nothing of it).

## Word suggestions (2026-10-21)

The owner, of hints: "the better hints the better code". AI trials wrote
`keys.right` and `keys.left` and got no "did you mean", as no key is within two
edits; the script parser already guessed `leftShift` for `shift`. `suggestion`
(`compiler/src/common.h`) now also keeps the first candidate with the wrong name
as a whole word of it, ignoring case (`word_rank` in `common.c`). A word starts
the name, or starts at a capital, a digit or after a `_`, and ends at the name's
end, a capital, a digit or a `_`. A first word beats a later one: `up` is
`upArrow` rather than `pageUp`. Candidates of the same rank go by the order
they're offered, which for fields is declaration order: `right` is `rightArrow`
rather than `rightShift`. Wrong names under two characters get none.
`suggest_note` prefers a close name, unless it is shorter than the wrong one and
a word candidate exists (`up` is `upArrow`, not `u`). Every `suggest_note` caller
gets the fallback, and so does the editors' quick fix through the suggestion sink.
`suggest_close` alone (`tide add`'s package names) is unchanged. Tests:
`tidec_error_key_word_first`, `_later` and `_prefix_wins`.

## Scene filters: `with scene Arena` (2026-10-20)

Owner's spelling (`with` alone would change `event(Spawned) Setup(with Main)`,
which runs once, into every entity of Main); the rest is Provisional in the
spec's Scenes. Parser: after `with`/`without`, the word `scene` before a name
sets `param.scene`. Checker (`check_scene_filter`): the name must be a scene
component (a prefab or another kind says so), pushed to `decl.in_scenes` /
`out_scenes` (twice, or in and out, are errors), the param typed void with
`scene_of` the scene; a scene filter alone makes the system per-entity, over
the scene's side; a scene of the other side is an error. Override signatures
compare `scene`/`scene_of`, and print `with scene Arena`. Codegen
(`gen_scene_filters`): `tide_in_scene_<S>(entities, scene)` (alive, in an
archetype with S) and `tide_entity_in_scene_<S>(w, e)` (by the entity's row's
scene column) for each scene some code filters by. Rows: `scene_filtered`
makes `rows_filtered` true; `gen_row_filter` skips by `tide_scn[tide_i]`;
`gen_rows_count` counts passing rows by `tide_scenes` (from
`gen_rows_presence`), so chunks with none are skipped and all-pass chunks take
the unfiltered loop; C systems' run matcher includes it. Handlers: the per-row
dispatch through `gen_row_filter`, async handlers' task start, availability
listeners (`tide_available_<E>`) and heard events' listener bits
(`tide_hear_<E>`) by the target. A scene load sets the root's scene column to
itself, so the root counts, which the spec says. Also: the never-runs warning
notes `with scene` when a system needs a scene's component; a filter naming a
prefab says it's a prefab (`errors/system_filter_by_prefab`).

Tests: `e2e/scene_filters` (systems in and not in an arena, a scene filter
alone counting the arena's entities with its own, a Spawned handler, a
targeted event's match handler and async handler, views with both filters; a
local handler compiles), errors `scene_filter_not_a_scene`,
`scene_filter_in_and_out`, LSP `lsp_scene_filter_names_its_scene` (rename
reaches the filter, hover), and the grammar (`keyword.other.scene` right after
with/without, before a name).

## Map views (2026-10-20)

Owner-approved runtime use (runtime-ownership.md): every function body that
can index lists or maps declares, beside `tide_lc`, `tide_map_view tide_mv[n]`
and `tide_map_views_none(tide_mv, n)` (`declare_list_caches`, both counts
patched in by `close_list_caches`), a view for each map it reads or sets by
key, slotted by what names the map (`view_slot`, as `cache_slot`), so
`scores` and `seen` don't share one. `map_views` is on wherever `list_caches`
is, parallel loops' tasks included. Through it, `m[k]` calls
`tide_map%d_cget`, `m.Has` `_chas`, `m.Get` `_ctryget`, `m.TryGet(k, out v)`
`_ctryget_out`, a path into a value (`m[k].x`) `_cat`, and `m[k] = v` (not on
an element's slot) `_cset`. The plain helpers stay for the compiler's own
generated code (visibility, hierarchy, client state) and anywhere without
views, each now its `c` form with a view of its own. Only the views' handles
are set at entry; a view's other fields are read only once it's found
something. ecs.md, Map views across a function, has the runtime contract.

## Events in runs (2026-10-20)

Compiler-private, with no language, runtime-interface, layout or hash change
(measured in notes/benchmarks.md, Events, second round). A system whose whole
body is one batchable Send (`batch_event`, unchanged) used to reserve a
24-byte `tide_command` per row; it now records runs:

- `gen_sent_runs` (only in a game with such a system, `program_batches`):
  `TIDE_CMD_EVENTS`, last in the kinds' enum, `tide_sent_<Event>` (the target,
  then the value: 12 bytes for an `int` event), and `tide_sent_push_<Event>`,
  which pushes one queue item through `tide_queue_push`: the ordinary head
  (`id` the event, `depth` the system's `tide_chain`, `entity` null), the
  count at `TIDE_COMMAND_HEAD`, then the entries at `TIDE_SENT_AT`, as many
  as fit a queue page (`TIDE_SENT_MOST`, at least one). The chunk loop
  (`gen_chunk`, `rows_batched`) pushes a run per page's worth of rows and
  hands each row its entry (`tide_reserved` is a `tide_sent_<Event> *`; NULL
  in rows functions that filter, which send the ordinary way).
- `tide_apply_from` takes a run with `tide_apply_events`, which calls the
  event's `tide_dispatch_run_<Event>`: each entry in order, its handlers, then
  `tide_hear_<Event>` when the tick records, as `tide_apply_event` takes one
  event. The run's depth is the one each event had; what handlers record goes
  behind the whole queue as before.
- Events sent the ordinary way get a loop of the same kind (`event_runs`: the
  match's events that handlers or listeners take, not commands or events with
  text). `tide_apply_event` (match) takes the queue's place too and hands the
  item to `tide_dispatch_items_<Event>`, which counts the items right after it
  in its page with its head (size, kind, event, depth: a page's items are back
  to back, so that's `used` and the item's size, read before any is taken),
  takes them all in order and moves the place past them. A system's loop
  whose Send is in an `if` or a function, or a handler sending to many,
  records such items. What a handler records meanwhile goes after them, as
  before.
- Both loops are `gen_dispatch_each`'s, `noinline` so each has the registers
  to itself, and step from one event to the next by a count, not by reading
  where the next one is, so the lookups of many targets are in flight at once.
- Those events' dispatcher is split (`gen_dispatcher`, `split`): finding the
  target (`gen_dispatch_locate`) and `tide_dispatch_at_<Event>(w, target,
  loc, event)`, everything after it (`gen_dispatch_handlers`). Where one
  archetype takes the event (`dispatch_candidate`), the loop reads that
  table's count, page array and first handle once and finds each target by
  `index - first` checked against the row's handle, with two calls of
  `tide_dispatch_at`, so the found one knows its archetype; a miss (another
  archetype, a moved row, a gone or reused handle, the null target) takes
  `tide_entity_location`. That's sound because no handler moves an
  archetype's rows: spawns, adds, removes and destroys wait in the queue,
  and what a handler writes (its `mut` components, `World.Set`) changes lane
  pages, never the entity column or the page array. Other events' loops call
  `tide_dispatch_<Event>` for each.
- `tide_settle_item` settles a run's targets and entity fields, though a
  batched system never spawns, so its queue isn't settled.
  `tide_command_fits` doesn't list the kind: a run is only in the queue during
  the tick that sends it, never in a world's bytes, and one from bytes fails.
- Old and new tidec over every game's source list (`--no-line`, the
  simulation identity lines aside): 196 of 325 generate what they did, the
  129 whose match events handlers or listeners take have the loops.
  `tide_table_column_mut` (tide/table.h) no longer writes back a page pointer
  that didn't change, for every game.
- Tests: `compiler/tests/e2e/events_runs` (3,000 targets in two archetypes,
  5,000 shooters, six ticks: runs of three events, items of a fourth sent in
  an `if`, a sparse filter, an event whose handlers take two archetypes, the
  world as target, a Destroy queued before the shots, rows a removal moved,
  slots a handler's spawn took again, handlers that send and spawn, a local
  handler that hears the runs) against the same worked out in C, on one
  thread, through a pool that says it has four (each chunk's own queue) and
  from the world as bytes. Breaking the found check, the fallback or the
  items' count fails it. `events_batch` and `events_lookup` still pass.

## Empty scene shorthand (2026-10-20)

Owner-approved: `scene Name;` is a complete empty scene declaration equivalent
to `scene Name { }`, including `local scene` and declarations in namespaces.
Braces remain valid. Only scenes gain this shorthand; other data declarations
still require braces. `parse_data_decl` receives the scene flag before reading
the body and records the semicolon as the declaration's end. Checking, generated
types, startup and scene lifetime use the existing paths, with no runtime change.

The existing scenes_fallback and scenes_local E2E fixtures now exercise shorthand
match/local Main startup and reloading. lsp_empty_scene_declarations covers
namespaced loads, local scenes, hover/references/rename, outline/completion,
formatting/idempotence, declaration boundaries and missing-body diagnostics.
Shared grammar probes cover both spellings; the all-E2E grammar check recognizes
that a shorthand scene has no body. The spec and Scenes page document the rule.

Validation: warning-free native/web debug builds; all 2,477 native CTests
(including 36 Vulkan/OpenGL GPU checks) and all 2,383 web CTests (including
WebGL, D3D11 and 11 WebGPU checks) pass. The full LSP suite, all 268 grammar
cases, production docs build and diff check pass. One-off comparisons of match
fallback, local and namespaced scenes produce byte-identical generated C and
headers for braced/semicolon forms with --no-line; no determinism pin changed.
Clang 23.1.2, Node 24.21.0 and Chrome 155.0.8059.85 were used. Other OSes,
Android, MinGW and sanitizers were not run on this Windows machine; CI was
not dispatched.

## Performance pass: list writes and grid windows (2026-10-20)

Two compiler-private changes, no language or runtime-interface change (measured
in notes/benchmarks.md, Performance pass):

- A list type's cached write helpers (`tide_list<n>_cset`, `_cslot`) skip
  `tide_list_prepare` when the function's cache is writable for that same list
  and still holds (`CACHE_OWNS` in codegen.c, `tide_list_cached_at_mut`'s own
  test). Only `tide_list_cache_fill_mut` makes a cache writable, after the
  helper prepared the list; it never caches a scratch or pack list and refuses
  an asset descriptor, and preparing only ever acts by changing the list's
  handle, so it would change nothing. Every element a loop wrote used to pay
  two block lookups and an asset-registry check for it.
- A windowed parallel loop over a grid of one-cell blocks whose step writes
  `cells[at]` at the top of its body before anything can end it
  (`writes_own_cell`, `may_leave_step` in check.c: no return, fail, or
  continue/break of the step before it; `stmt.writes_own`) no longer copies
  its whole window into the copy the steps write: every cell put back is
  written. A tile where the grid has no chunk, whose blocks are checked first
  and may skip, still copies. `compiler/tests/e2e` grid fixtures and the
  safety suite cover the loops; versus_life and versus_sand pin the results.

## Building a game for another program: --embed (2026-10-20)

`codegen_options.embed` (`tidec --embed`, `tide_add_game(EMBED)`, `tide unity`)
writes the layout and `tide_game_embed` (`compiler/src/embed_codegen.inc`,
after `gen_layout`) into the `.c` alone, so the header and the game's hash are
the same as a plain build's (checked on three games). It holds: the view packet
writer, modeled on `tide_world_print`'s walk over every match archetype and
component (sparse presence included), blending each value with views'
`tide_prev_<C>`/`tide_blend_<C>` (the owned variant where a custom
interpolator reads the heap), owner flags from `Owner` against `Session.player`,
parents from `tide_parent_of` where the game has a hierarchy, component and
singleton local fields, and `TIDE_HELPER tide_embed_fix_<T>` turning text and
plain-element list fields into STRINGS handles (maps, sets, grids and lists of
heap-holding elements zeroed); command types' layouts appended after the
layout's types and a dispatcher onto `tide_local_send_<C>` with the target,
refusing a wrong size and commands holding text or lists; START by scene name
over `start_scenes` with defaults (text zeroed); tables of components,
singletons, local parts, whether each component is a scene's (`is_scene`),
commands, scenes, prefabs and non-plain fields (text,
list with element size and type, map, grid, entity); `schema`, through the
engine's `tide_schema_json`; and `defaults` (`tide_embed_defaults`), each kept
match component's `gen_value` with no inits, text and lists zeroed under a
scratch mark as START's are, which the schema writes as hex. Entity rows end
with `tide_entity_born`, and `entities_born` is true for any `--embed` build,
so every spawn there keeps it (notes/unity.md, Identity and pooling). A build
without `--embed` generates what it did.

Check: every one of the 324 source lists in a debug build generated with
`--embed` and compiled under `-Wall -Wextra -Wpedantic -Werror`; the first run
found an unused `out` (fix helpers of types with only maps/grids) and an unused
`place` (games with no component), both fixed. Runtime contract and tests:
`notes/standard-host.md`, `notes/runtime-ownership.md`.

**Events, text commands and command defaults for another program (2026-10-22, owner: "yeah I think that makes
sense"; `notes/unity.md`, Events, commands and singletons in C#).**
- **Taps.** With `--embed`, `add_embed_taps` (`compiler/src/compile.c`, after parsing, before checking) parses a
  source `<tide-embed-events>` of two local handlers for each of the game's own match events (not built in, local
  or a command): `local event(E heard) TideEmbedHeard<n>() { }` and its `verified` twin, flagged `decl.embed_tap`.
  So another program hears what the game's local handlers would, by their machinery: predicted and verified, once
  each under rollback, late joiners' arrivals, recorded text. `gen_heard` calls `tide_embed_heard_<E>(&event,
  verified, target, tick)` for a tap instead of its handler; `collect_heard_reads` marks a tap's event read whole,
  so its text and lists are recorded. `gen_embed_heard` (embed_codegen.inc, before `gen_heard`) keeps each heard
  event's record (a 32-byte header, then the value with its text and lists copied into the store's own STRINGS)
  in a static `tide_packet`, which the next view packet writes as its EVENTS section (the target's place among
  the packet's entities where it's the same entity) and empties; outside a match it's dropped. The outline leaves
  taps out, and the header's first line doesn't name their source.
- **The same game.** A tap must change neither the header nor `TIDE_SIMULATION_ID`, or Unity builds wouldn't play
  with other builds. The local state's event log (`tide_local.tide_heard`) is now there in any game declaring a
  match event of its own (`program_has_match_events`), tapped or not, so a game with events and no local handlers
  has it in every build (its plain header changed once, for that). `listener_wire` (`compiler/src/asset_usage.c`)
  leaves taps out: they hear only the game's own events, which no server journals to recipients. Checked: every
  one of the 330 source lists of a debug build gives the same header, but its first line, with `--embed` and
  without (`TIDE_SIMULATION_ID` included); before `listener_wire` left taps out, two visibility games differed.
- **Commands with text and lists.** `tide_embed_command` takes such a command's text and `List<byte>`s after its
  value as STRINGS lays them out (a count, `{offset, length}` entries, bytes), each field holding its entry's
  place + 1: `tide_embed_command_text` (UTF-8 checked, `tide_text_temp`) and `tide_embed_command_list`
  (`tide_list_from`), under a scratch mark; the command is then written into a scratch buffer and checked
  (`tide_write_command_<C>`, `tide_game_check_command`) before `tide_local_send_<C>`, so one too big says false
  rather than ending the program as local code's Send does.
- **Schema.** Commands' and tapped events' fields are in `tide_embed_fields` (by the schema's numbers, after the
  layout's types), so their text and lists say so; extra types are the commands and then the tapped events;
  `tide_embed_game.events` names the events; `defaults` gives commands' declared defaults too.
- Tests: `embed_heard_events` (each Ticked heard once as predicted and once confirmed, with its label and its
  entity's place), `embed_commands_and_scenes` (Rename's text and bytes, cut short, a missing entry, bad UTF-8),
  `embed_schema` (Ticked an event, listed after the commands), all in `platform/tests/embed.c`.

**Containers in the view packet (2026-10-22, owner: "lets bridge any gaps").** `gen_embed_packers` (embed_codegen.inc,
emitted by `gen_embed_heard` before `gen_heard`, so heard events use them) writes a packer for every list
(`tide_embed_list<n>`), map and set (`tide_embed_map<n>`) and grid type (`tide_embed_<shape>`) and every type's fix
(`tide_embed_fix_<T>`, prototypes first, as they call each other): `embed_fix_value` makes a value's heap fields the
packet's handles wherever they are, so a list's elements, a map's keys and values and a struct's fields can hold
text and containers in turn. A list of plain elements is `tide_packet_list`; others are copied into the scratch area,
fixed and written as one entry; a map walks `tide_map_next` in insertion order into count, keys (to 8 bytes),
values; a grid writes its directory's chunks as they are (`tide_grid_records`, `tide_grid_chunk_at`), so it costs
what it holds, with a 48-byte header (dims, cell, shifts, size, chunks); a grid of cells holding text is 0. The heard
events' store copies its STRINGS into each packet first (`tide_embed_heard_strings`), so their handles are the
packet's unchanged and nothing is remapped. `tide_packet_text`/`_list` start each entry from a multiple of 8
(`align_text` in `engine/src/embed_game.c`), so another language reads elements in place. Packet version 4. Test:
`embed_containers` (a singleton's `List<string>`, `List<Note>`, `Map<string, int>`, `Set<int>` and `Grid2<byte>`,
each read back as the header says), and Ticked's `List<string>` through `embed_heard_events`.

For `tide unity --assets`: `compile_input.text` gives a file's text in place
of reading `path` (the CLI's Tide text of a Unity prefab or scene), and
`parse_file` treats paths ending `.prefab` and `.unity` as `.tprefab` files,
so they hold prefab declarations only (`notes/unity.md`, Authoring as built). A system's or view's parameter or filter naming a prefab (a Unity scene's too) says it's a prefab and how to filter its entities, rather than "unknown component" (`errors/system_filter_by_prefab`).

## Double Position correction smoothing (2026-10-20)

The owner requested [Position] on double coordinates. The checker now accepts
double and double2/3/4, alongside existing float types, while retaining the
match-only, no-arguments, Snap/conflict and custom-Interpolate restrictions.
Double matrices remain invalid; Rotation still accepts float radians/quaternion.

The existing generated correction path already stores the error in the field's
declared C type and performs subtraction, accumulation, application and decay
multiplication in that type. No numeric codegen or runtime helper was changed.
The float frame interpolation fraction and existing float exponential decay
coefficient widen for double arithmetic. The same 75 ms half-life and all float
behavior remain unchanged. Errors remain private local presentation state,
excluded from match hashes and local/match packing; snaps, identity changes,
confirmed removal and timeline replacement retain their existing reset rules.

Four double regressions extend view_corrections: all four scalar/vector sizes,
nested structs and Snap boundaries, component and singleton presentation,
billion-unit fractional offsets, fractional errors requiring binary64 precision,
ordinary movement and accumulated corrections, decay, zero-error interpolation,
two views, unchanged world hash/values/packed bytes and cache resets/lifetime.
The seven existing float/rotation regressions remain intact. New diagnostics pin
double matrix rejection and unchanged double Rotation rejection. LSP hover and
user docs/spec identify the newly accepted types.

Validation: full native debug passes 2,469/2,469, including 34 Vulkan/OpenGL
GPU tests; full web debug passes 2,378/2,378 (2,367 ordinary/WebGL and 11 WebGPU).
All 11 view_corrections cases pass native/WASI debug and optimized builds;
all 16 correction diagnostics and the full LSP suite, including double Position
hover, pass. Production docs build, all 268 shared grammar cases and the staged
diff check pass. No determinism pin changed. Debug uses clang 23.1.2 with Node
24.21.0 and Chrome 155.0.8059.85 for web; native optimized uses clang 18.1.8.
Linux, macOS, Android, MinGW and sanitizer coverage were not run on this Windows
machine; CI was not dispatched.

## Double math and matrix completion (2026-10-20)

The owner corrected the foundation's narrow scope: double trig and matrices
belong with double support. All existing component-wise Math functions now
accept doubles and double vectors, including Sin/Cos/Tan/Asin/Acos/Atan/Atan2,
Exp/Exp2/Log/Log2/Log10/Pow. double.PI/TAU/E provide binary64 constants without
changing existing Math constants. No old float arithmetic or pins change.

Double2x2/3x3/4x4 kinds are appended, eight-byte aligned, with columns,
row/column construction, identity, negation, addition/subtraction, scalar scaling,
Mul/Transpose/Inverse/Determinant, Transform/Rotate and double4x4 builders.
Matching float matrices widen implicitly and explicit constructors narrow per
component. Mixed matrix addition/subtraction retains double precision. TRS
accepts a double3x3 or the existing quaternion; quaternion construction widens
components before matrix arithmetic but cannot restore absent input precision.
The compiler walks double matrix columns for input/command repair and raw/delta
bits, interpolation and inspection. Other storage uses the existing flat value
paths. Layout metadata keeps matrices PLAIN as before: identical matrix types
survive reload; a precision/type change retains the new default.

engine/src/math64 vendors the portable non-FMA binary64 subset from musl 1.1.18
(Sun fdlibm/FreeBSD), separate from unchanged in-house float functions. Its source
and license notices, namespace/word-access adaptation and checksum are recorded
in that directory's README. Correct rounding for every input is not promised.
Tests compare 398 golden values generated independently with mpmath 1.3.0 at
400 decimal digits, within one binary64 ulp, and cover special values, huge-angle
reduction, subnormals and large-position matrix inverses. A new exact-result pin
is A8DEFF186244546C; the existing 8D8432D419DF4D83 pin is unchanged. E2E exercises
scalar/vector math, matrix builders/operators/conversions, command columns,
raw/delta input, repair, snapshots, recipient state and interpolation. Compiler
diagnostics, LSP column/constructor/math signatures and shared grammars cover
the language/tooling boundaries. Double noise, double quaternions and automatic
camera-relative rendering remain outside this work.

Validation: full native debug passes 2,467/2,467, including 34 GPU tests with
Vulkan validation/OpenGL; full web debug passes 2,376/2,376 (2,365 ordinary/WebGL
and 11 WebGPU). Both debug builds use clang 23.1.2; Node 24.21.0 and Chrome
155.0.8059.85 run web coverage. The eight focused double runtime tests and four
E2E cases pass native/WASI debug and optimized builds, with both bit pins
agreeing (native optimized also uses clang 18.1.8). The final optimized E2E
rerun includes mixed-precision rotation construction and default-overload cases.
All 398 reference cases pass within one binary64 ulp; regenerating the checked
reference header is byte-identical. Full LSP tests, the production docs build,
all 268 shared grammar cases and the staged diff check pass. No old pin changed.
Linux, macOS, Android, MinGW and sanitizer coverage were not run on this Windows
machine; CI was not dispatched.

## Double scalar and vector foundation (2026-10-20)

The owner approved double, double2, double3 and double4 as the first large-world
increment. Existing literals/default types, Transform, cameras, physics and GPU
values remain floats. The spec separates this approval from first-build spelling
and conversion choices. Binary64 type kinds are appended so existing type IDs
and their simulation-schema records do not change merely by adding the feature.

Decimal d/D literals retain binary64 precision; unsuffixed/f literals still round
to binary32 first. Byte/char/int/long/float widen to double, and matching int/float
vectors widen to double vectors. Long conversion can round beyond 53 bits.
Numeric arithmetic promotes to double before operating, including scalar splats;
vector constructors/swizzles and explicit narrowing follow existing conventions.
Float narrowing is nearest/even with signed infinity on overflow and canonical
NaN; integer narrowing truncates, saturates and maps NaN to zero. Byte keeps the
int conversion's low eight bits. Numeric defaults and floating remainder rules
are unchanged. Concrete float operands continue to select float builtins beside
`default`; a wider double overload is not an ambiguity by itself.

Basic component-wise Math, square roots, interpolation and radians/degrees,
vector dot/length/distance/normalize/reflect/reductions/cross and exact-bit Hash
use inline tide_*_d/d2/d3/d4 helpers in the existing math maintenance area.
Normalize uses the binary64 minimum-normal squared-length threshold. Transcendental
and noise double functions, double matrices/quaternions, position correction
attributes and automatic camera-relative rendering are outside this increment.
Existing Math constants remain floats. Shader checking rejects doubles before
numeric GPU packing and explains converting in local code before drawing.

Ordinary fields, structs, arrays, list/map values, grid cells, optionals, tasks,
extern signatures, events/commands, snapshots, packing, recipient codecs and
view interpolation retain full double values. Each component is eight bytes,
eight aligned; generated explicit padding/size assertions use that layout.
Views retain the existing float interpolation fraction and operate on values in
double precision. Input/command bits use two ordered 32-bit words, low first;
input deltas use one changed flag plus the 64 bits when changed. Repair and
bounds walk all double components before input Sanitize. No session protocol,
container representation or exported networking function changed.

Text generalizes the existing exact integer printer to binary64 with shortest
round-tripping digits and fixed F0..F17. double.TryParse and scalar JSON double
fields use exact integer division, not libc parsing. The reader retains 800
significant digits and a nonzero tail, with 128-word bounded integers; the
printer uses 40 words. Negative zero and nonfinite JSON match float rules.
Hot reload adds an appended DOUBLE layout kind, preserves matching double
fields and widens byte/int/long/float fields of matching dimensions; narrowing
retains the new declared default.

Coverage: double E2E checks billion-unit offsets, arithmetic, mixed promotions,
constructors/swizzles, geometry, basic math/hash, safe casts, optional/list/map/
array/grid values, text/JSON, C value/out ABI, source operand order, waiting tasks,
commands including truncated payloads, raw input/deltas and repairs, COW replay,
raw/recipient round trips and view interpolation. Eleven diagnostics cover
narrowing, invalid operations/sizes/attributes, literal overflow, transcendentals
and GPU use. Runtime tests cover all finite exponent classes of both signs,
integer/float narrowing boundaries and NaNs, shortest/JSON round trips, exact
rounding ties including a 2,200-character subnormal midpoint tail, and a 10,000
case arithmetic/geometry/text bit pin: 8D8432D419DF4D83. No prior pin changed.
Lexer, LSP, grammar and migration checks cover the tooling boundaries.

Validation: all 2,465 native debug tests pass (2,431 ordinary and 34 GPU),
including compiler diagnostics, the LSP harness, migration, Vulkan validation
and OpenGL. The uppercase degree diagnostic fixture now expects `180DE` rather
than `180D`: D is a double suffix before the continuation is rejected. The full
web debug suite passes 2,374/2,374, including WASI, WebGL/ANGLE D3D11 and WebGPU.
Both final debug builds use clang 23.1.2, Node 24.21.0 and Chrome 155.0.8059.85.
The initial fallback clang 18.1.8 web build exhausted V8's call stack in the
existing thousand-deep drawing fixture; rebuilding with clang 23 passes it
without changing source, test depth or stack limits.

The four focused runtime tests and four double E2E cases also pass optimized
native and WASI builds. The new bit pin agrees in native/web debug and optimized
builds; no prior determinism pin changed. Production docs build, all 268 shared
grammar cases and the staged diff check pass. Linux, macOS, Android, MinGW and
sanitizer coverage were not run on this Windows machine; CI was not dispatched.

## Shared player ceiling (2026-10-20)

The owner approved maxPlayers from 1 through 65,536, sharing the ceiling used
by dedicated-server connections and native room peers. The default remains 16;
the setting still determines the world's input layout, generated Audience size
and build compatibility. Compiler diagnostics use TIDE_PLAYERS_MOST; builtin
help and user documentation now report the same range. Values above it remain
compile errors, with the existing full-input-traffic scaling explanation.

Generated Session ABI headers identify player protocol 16. Generated raw-save,
network-delta and page-hash/need headers use temporary heap buffers, including
a separate baseline only when needed; all are freed after their synchronous
codec operation. This avoids stack overflow from a large match's fixed input
arrays without changing serialized values, padding, hash ordering or runtime
function calls. No new generated-C runtime function is required.

players_ceiling compiles the exact ceiling and tests input index 65,535, the
separate server slot, Audience word 2047, snapshot/delta transfer and reconnect
recovery. players now covers 270 real sessions and a 300-player projected
Audience. Runtime/protocol contracts and final validation: notes/netcode.md.

## Inside Out API and aggregate-copy completions (2026-10-20)

Authorized conventional additions: `Math.AsInt` reinterprets float/float2/float3/
float4 as int equivalents; `Math.AsFloat` is the inverse. Both use memcpy-based
inline math helpers, not pointer aliasing or numeric conversions. Float/vector
`Math.Hash` hashes exactly those int bits through the existing xxHash32 mixers
and keeps its nonnegative 31-bit result. Positive/negative zero differ. Existing
integer/long signatures precede float signatures so their overload resolution
and deterministic pins stay unchanged. CPU tests cover normal/subnormal values,
infinities and explicit NaN payloads; GPU tests compare finite input bits and
all four hash arities against CPU results.

Codegen also uses private const-pointer signatures for read-only aggregates of
at least 128 bytes and read-only method receivers when it can prove no source
mutation during the call. No Tide alias/reference behavior is added. Overlapping
mutable input types (including nested fields/containers), world writes, sends,
spawns, tasks, extern calls and Action helpers keep the ordinary value path.
Extern, async, operator, Interpolate, audio and input entry ABIs are unchanged.
Recursive type inspection is bounded and fails conservatively. Later argument
side effects retain existing ordered snapshots; non-addressable values use
caller-owned compound literals. Shared GPU helpers retain their shader ABI.

`readonly_aggregates` runs 40 KB values through 96 recursive function and method
calls and tests disjoint mutable results, overlapping values/fields, implicit
method forwarding with container outputs, later operand mutation and C mutation.
`float_bits` checks all scalar/vector bit-cast and hash overloads. `screen_scale`
checks current-frame metadata, default scale and changes between frames; three
diagnostics pin match/GPU/write rejection. Editor tests cover new math help and
Screen metadata. `notes/private-prototypes.md` (tide-prototypes) saves all remaining design ideas.

Validation: full warning-free native/web debug builds pass. All 2,452 native
CTests (2,418 ordinary plus 34 GPU) and all 2,361 web CTests pass, including WASI
execution, shader pins/SPIR-V validation and Vulkan/OpenGL/WebGL/WebGPU/D3D11-WARP
checks. The 241 editor cases and production docs build pass. Actual
unchanged Inside Out source generates successfully, with SeatOf/Together/HallOf
receiving `const Workshop *`; that generated C passes strict native and WASI
syntax checks. No private prototype browser playtest, CI dispatch or deployment
was performed. Other OSes, Android and sanitizers remain CI coverage.

## Explicit skinned instance overload (2026-10-20)

Owner-approved `Draw.Instanced(SkinnedMesh<V>, instances, ...)` follows the existing
instance struct convention, adding optional CPU `Pose pose` alongside optional
`float4x4 transform`. Checker expansion preserves written operand order and
rejects incorrectly typed pose fields; absent pose fields request authored rest.
Each valid item captures its pose using the existing skinned runtime interface.
The instance list follows the model/pose argument borrow rule when no later
operand can change it; later Tide/C effects preserve nested value snapshots.
No general GPU support for Pose or implicit shader inputs was added. See
`notes/drawing.md` for the full contract, fixtures and validation.


## Recorded containers for local handlers (2026-10-19)

Owner-approved: local match-event handlers read text, lists, maps, sets and
grids from their existing recorded endpoints. Direct parameters use the event
turn; query rows use tick end; verified delivery uses the confirmed recording.
Attached local fields remain current. Match data stays read-only and existing
copy restrictions, including grids, are unchanged. This supersedes older
omitted-heap statements below and in the local-handler/query notes.

Private read analysis follows selected member paths through synchronous
functions, method receivers, implicit method calls and borrowed queries to a
fixed point. An explicit whole-value copy reads its fields; conditional reads
are possible reads, and dynamic fixed-array indexes select the possible
elements. Unread containers remain zero in the fixed record and have no tail
encoding. Component/singleton captures use eligible listener masks; event
payloads use the union of possible local reads, and shared query shapes use
their readers' union. A container read captures that container's value, rather
than specializing its representation to an individual count/index operation.

The existing visibility value codec is shared in value_codec_codegen.inc.
Generated journals append bounded, owned serialized tails after their fixed
payload/rows. Existing log copy/take/replay/confirmation/clear/free operations
therefore own the complete value lifetime, with no runtime interface change.
Before local delivery the log is decoded once into a temporary heap, shared
across its handlers/query rows and freed after delivery. The match/local heap
resolvers and scratch mark are restored. Recording explicitly resolves each
source world's heap, including old availability endpoints. Explicit local
value assignments acquire their ordinary local ownership before that heap goes.

Recipient validation retains zero-handle, canonical bool and private/Resident
checks on fixed records, then validates tail spans/padding, presence flags and
typed values in an isolated temporary heap before dispatch. It preserves
component visibility and rejects bad data without consulting live heap offsets.
Delivery tolerates listener subsets already reduced by occurrence deduplication;
authoritative input validation still requires canonical capture presence.
Value decoding retains the existing 256-level codec nesting guard. Journal
build identity includes capture paths, reader masks and codec shape. Observed
event-payload container contents contribute to occurrence identity; component,
singleton and query snapshots do not. Unread event containers remain omitted.
No world layout, allocator, deterministic hash/packing or engine API changed.

event_containers has seven runtime cases: recorded turn versus tick-end data,
lists/text/nested maps/sets/open grids, helper/method/query forwarding, local
copies surviving world/journal disposal, chunked values, changed event content,
replay/confirmation, destruction/log copies, recipient privacy/checked tails,
old/new availability heaps and byte-identical journals beside three unread
5,000-element containers. Three obsolete rejection fixtures are removed.
visibility_lifecycle keeps its forged-handle tests with the now-variable tail.
Public Events, Functions and the decided/provisional spec document the contract.
Validation: both full debug builds pass without warnings. All 2,438 native
CTest cases pass (2,406 non-GPU in 128.78 s, then 32 GPU in 14.94 s), including
all 240 editor cases. All 2,348 web debug cases pass in 321.89 s, including the
seven new runtime cases on WASI and WebGL/D3D11/WebGPU coverage. The production
docs build passes. Final compiler-only comment/indent cleanup was rebuilt on
both host tools; emitted behavior is unchanged. Exact determinism pins remain
unchanged. Private prototypes stayed disabled; no Gravewake browser playtest,
CI or deployment was run. Linux/macOS/MinGW, Android and sanitizer configurations
were not run on this Windows machine.

## Query rows reach attached local fields (2026-10-19)

Owner-approved after reviewing the query(mut Fighter fighter) example: local
code reads current attached local fields through query bindings, and mut
bindings write only those fields. Match values and entity handles remain
read-only. This includes verified and recorded match rows: local state has no
history. Ordinary copies still omit local fields; a direct binding passed to
an ordinary helper retains its capability. Read-only borrowed queries accept
mutable sources; mutable helper bindings require mutable sources. Function,
method and recursive forwarding preserve selection, match timing and local
permission. Match systems/handlers cannot gain local access through helpers.
Queries over local-world components still have read-only bindings: this task
extends attached local fields, not whole-component query writes.

Checker propagation follows nested query bindings and component arguments from
rows, then derives local query effects for callers and warnings. Access warnings
run after this propagation, so forwarding a mut binding does not suggest a
capability-breaking remove-mut fix. Query mut calls count as writers for value
evaluation order and dropped-call analysis. Views/local handlers keep existing
serial order and named-phase constraints, without granting a match World write.
Shader inputs still cause local-field propagation before access warnings.

Generated match rows contain an extra private birth identity only when the
query reaches local fields, propagated through borrowed source layouts. It is
plain metadata; event journals never contain local fields or local heap handles.
Purely read-only access resolves current attached state or borrowed defaults
without allocating an attached row. A mutable binding that uses local fields prepares
and borrows its owned cell at row entry, before the body, then keeps that stable
reference. No lookup/reset is hidden inside a C operand. Mutable
outer parameters sharing query access use attached storage up front, so pending
local edits are immediately visible to that query. Read-only outer/row borrows
also share the cell when their routine can mutate it through a query, preserving
live aliasing rather than depending on whether defaults were stored already.
Historical birth mismatches read defaults and mutable access runs
Forgotten/releases/reinitializes in place;
moving/removing that cell could invalidate an active outer alias. This follows
existing listener birth-reset behavior while preserving live cell addresses.
Private get/mut/reset helpers use existing attached/table/ownership functions;
no new runtime API, public event header or match layout/hash/packing changed.
Affected private query/journal layouts and their build identities change.

query_local_fields has eleven runtime cases for borrowed reads/writes, methods,
inlined Actions, default strings/lists/arrays, live reads after nested writes,
outer aliases, source operand order, verified/current local state, delayed event
delivery, birth reuse/Forgotten and an active alias across differing predicted
and confirmed births, plus a filtered mixed verified/predicted row sharing local
state, preparation before assignment operands and readonly outer/row aliases
beside mutable helper queries. Its 5,002-row case preserves
a COW match snapshot/hash.
A scalar-only safety fixture exercises the same private helpers without heap
fields. Seven new diagnostic fixtures replace three obsolete view/listener
rejections. lsp_query_local_fields and existing query tests cover completion,
hover and rejected match writes. Public spec, Views, Functions, Events and
Systems document the contract.

Validation: warning-free native and web debug builds. All 2,440 native CTest
cases pass, split into 2,408 non-GPU cases (125.38 s) and 32 GPU cases (13.47 s)
so native graphics did not overlap browser graphics. All 2,350 web debug cases
pass (294.65 s), including WebGL, D3D11 and WebGPU coverage. These runs include
all eleven query_local_fields runtime cases, the safety fixture and all 240
editor cases. The production docs build passes. Exact cross-platform pins are
unchanged. Linux/macOS/MinGW, Android and sanitizers were not run on this Windows
machine; no CI or private prototype playtests were dispatched.

Correction (2026-10-20): a view's or handler's pointer to a parameter's
attached local fields was `restrict`, while its query rows reach the same cell
and write it (the outer aliases above). That is undefined, and optimized builds
acted on it: in release, query_local_fields failed two checks (the birth-reset
outer alias read 4 rather than 5; ReadWhileWriting saw no change), passing in
debug only because nothing took `restrict` at its word. The fields pointer is
no longer `restrict` (`gen_system_body`'s signature; match-data pointers keep
it, as local code never writes match data). All eleven cases pass in release.
Release CTest wasn't in the validation above, which is how it went unnoticed.

Recorded-owner local event filters (2026-10-19): the shared checker accepts
with/without ownership in local handlers of match events, including verified
and component-free entity listeners. Spawned/Destroyed infer the match side
from the filter alone. Local-world events, Forgotten, conflicting filters and
local-world components remain errors. Generated capture, delivery, availability,
privacy and replay contracts and validation are in
notes/verified-and-local-handlers.md. No new runtime function is used.

## World.Set updates active mutable bindings (2026-10-19)

Owner-approved: Set<T> may overlap a mut T parameter. A successful Set of the
current entity replaces both the stored component and its active mutable
binding immediately, through synchronous helper/method/recursive/Action calls
too. Set to another entity leaves that binding alone; absent/stale targets
remain no-ops. Setting health to 20, then incrementing the parameter, finishes
at 21. Whole replacement discards unfinished edits. Subsequent parameter
edits retain normal write-back timing, so Get/query reads keep seeing the
value published by Set until the invocation puts its later edits back.
Read-only parameters and explicit copies retain their own captured values.

Generated C passes a private chain of typed component IDs, world/entity keys
and mutable-copy addresses through synchronous helpers that can Set. Systems,
handlers and each async-handler resume build that chain on the C stack. It
never enters a task frame, world, header, snapshot, hash, wire format or reload
layout. No runtime API or public C ABI changed. Ordinary World capability,
match/local, view, parallel and comparison restrictions remain. Async ordinary
functions still cannot take World.

The private Set helper validates the target, takes current container ownership
from an active mutable copy when present, assigns the replacement through the
existing ownership helpers, then updates the stored and active values. Set
arguments retain source order and copy container values before replacing their
source. Read-only container arguments to a Set-capable helper are copied just
as beside an explicit mutable argument. Final overlapping row write-back uses
the current table rather than cached lane addresses, preserving COW snapshots
and preceding-row visibility. Other component loops retain their normal path.

Access warnings wait for transitive World effects: Set can itself write the
active binding, so suggesting removal of mut after such a call would change
the following read. The three old overlap error fixtures are replaced by
world_set_mut execution coverage, with direct/helper no-warning LSP checks.
While exercising awaited work, a pre-existing checker gap was corrected:
async handlers now record their awaited-function dependency as async functions
already did, so their frame includes the child state, ownership cleanup and
fingerprint. This is an existing backend contract, not new async syntax.

world_set_mut covers 5,000 rows over chunks/archetypes and sparse selection,
self/other/missing/stale targets, two mutable bindings, recursive methods,
by-reference aliases, Actions, expression ordering, read-only copies and nested
list/map/set ownership, command/event dispatch, async resumes/awaited components,
local views, threaded ticks, COW immutability, rollback and packed roundtrips.
Public Systems, Functions, Events and the decided spec describe the new rule.

Validation: warning-free full native/web debug builds, all 2,430 native cases
(full run plus the corrected matching-entity LSP probe recheck), all 2,340 web
cases, all 239 editor cases and the production docs build pass. The three old
overlap rejection targets became one execution target. Coverage includes native
Vulkan/OpenGL, WebGL/D3D11/WebGPU, the new seven-case fixture and unchanged exact
math/random/physics/world pins. Node 24.19.0 and clang 23.1.2 were used locally.
Android, other operating systems and sanitizers remain CI coverage; no CI was
dispatched and no private prototype browser playtest was run.

## Scalar Clip values without assets (2026-10-19)

Follow-up checking reproduced valid Tide generating invalid C when Clip was
used without an audio asset, Sound or microphone: tide_clip_ref lacked its
defining header. The earlier asset-collection fix covered collection element
types, but scalar signatures, fields and locals still missed it.

The checker now records explicit primitive Clip uses through ordinary type
resolution, field/local fast paths and Clip members such as Clip.none.
Built-in Sound's unnamed fields do not activate this flag. Header generation
includes the existing tide/audio.h definition without enabling asset loading,
playback, audio devices or any match state. Type/lifetime rules and the public
API do not change; no runtime function or layout was added.

clip_values compiles and executes scalar parameters/returns, a struct, local
singleton storage and typed/inferred locals with no assets or audio. Its C
host checks the stored empty handle, no asset/audio feature macros and an
unchanged world hash. The asset-usage unit target independently checks empty
games, unused helper and optional signatures, local and attached fields, and
inferred/typed locals, so one use cannot hide another missing header path.

Validation: warning-free full native/web debug builds and all 2,432 native
CTests (85.83 seconds) and 2,341 web CTests (220.63 seconds) pass, including
unchanged simulation pins and Vulkan/OpenGL/WebGL/WebGPU checks. No public
semantics or user documentation changed. Other operating systems, Android and
sanitizers were not run; no CI dispatched.

## Shaded drawing functions (2026-10-19)

Ordinary synchronous drawing functions can name frags/verts using the existing
mesh, instanced, skinned and blit syntax. Shader component/singleton requirements
match exactly one explicit non-out helper parameter by type, with normal
read-only value and mutable-pointer behavior. No ambient state lookup or new
function capability is introduced. Attached local fields use the existing
actual-parameter/sidecar rules through nested calls and synchronous callbacks;
plain copies have no sidecar. Methods, tasks, match code and GPU code retain
their existing drawing restrictions. The spec's former blanket restriction is
replaced by this completion of explicit helper access; public shader/function
docs and editor completion agree. Compiler contracts and coverage are in
notes/shaders.md. No generated-C/runtime API or world layout changes.

Validation: warning-free native/web debug builds pass all 2,431 native CTests
(150.63 seconds) and 2,340 web CTests (293.82 seconds), including exact-bit
simulation pins, shader goldens/SPIR-V validation, Vulkan/OpenGL and
WebGL/WebGPU pixels. The regression with an out parameter beside the real
input passes on both native and WASI. The docs production build passes. A final editor-only hover
wording adjustment was rebuilt and passed all 239 language-server tests.
Validation used Node 24.19.0 and the configured local Vulkan validation SDK.
Other operating systems, Android and sanitizers were not run; no CI dispatched.

## Borrowed resource asset collections (2026-10-18)

Implemented as an authorized completion of existing read-only helper access:
ordinary synchronous CPU functions/methods accept List<Clip>, List<Texture>,
List<Mesh<V>> and matching Map<string, T> parameters. Arguments name an asset
directly or forward another borrow of the exact type. Count, indexing, foreach,
readonly methods, overloads, named arguments, recursion, methods and synchronous
callbacks retain existing behavior. Individual elements retain ordinary value
rules and may be returned. The collection itself has no mut/out/in/default,
optional, return/error-payload, storage or async lifetime. Externs, operators,
audio declarations and GPU code take no borrow. Clip/Texture retain local-only
capabilities; numeric Mesh geometry retains its existing match reads.

Header generation includes the Clip reference definition for borrowed Clip
list/map signatures even with no declared assets. `asset_borrows_unused` compiles
all six unused collection signatures on native/web without enabling asset
loading or audio playback. This fixes a type-only header omission found in the
final review.

The resolver permits these collection types in helper signatures; the checker
validates borrow uses and direct/forwarded sources. A transitive CPU-only flag
prevents shader helper chains from reaching a resource-collection borrow.
Codegen passes the existing list/map handle, including beside mutable arguments,
in ordered operands and through inlined Action calls. It introduces no owned
collection copy, allocation, runtime call, pack ownership, persistent state,
public C ABI or release-pruning rule. Existing asset/element lifetimes and pack
replacement behavior remain in force. Other resource-collection storage and
unique grid borrowing are unchanged.

`asset_borrows` executes all six collection forms, recursive/method/named
forwarding, readonly map methods, foreach, mutable neighboring arguments,
Action/sort captures, Clip playback, Texture metadata, Mesh geometry, pack
reload and retained-world immutability. Scratch marks verify no passing copy
beside an effectful argument, a mutable list or an Action, and normal cleanup.
Fourteen diagnostic fixtures cover signature/mutation/source/escape/domain
restrictions; existing persistent-resource collection errors remain pinned.
Editor hover explains the borrow, collection completion uses the resolved type,
and GPU completion excludes direct/transitive borrow helpers.

Validation: final warning-free native/web debug builds, all 2,427 native and
2,336 web CTests, and the docs production build pass. The final suites include
the declaration-only header regression, execution/diagnostic/editor checks,
existing exact-bit/network pins, Vulkan/OpenGL and WebGL/WebGPU validation.
Native took 151.49 seconds and web 246.39 seconds. Other operating systems,
Android and sanitizers remain CI coverage; no CI was dispatched.

## World.Set beside read-only component parameters (2026-10-18)

The owner approved lifting the read-only overlap restriction as backend work
on the existing API. Systems, handlers and local views can Set any entity's T
while taking read-only T, including through ordinary helpers, methods,
recursion and inlined actions. Each invocation reads the current component on
entry, including preceding invocations' writes, then keeps that value. World
reads see Set immediately. Tasks get fresh component values on resuming.
Match/local, view, parallel and comparison rules remain in force; no new
implicit World capability is introduced. Mutable overlap was subsequently
extended by the owner-approved rule above.

At this stage check_world_call/check_world_passing still rejected mutable
overlap; that restriction is superseded above. Transitive World effects provide global
write dependencies, heap serialization and a single-threaded system traversal.
Generated rows pass page-slot arrays for components those World effects write,
and reload their current lane addresses at each row. Structural changes remain
queued, keeping the slots stable while Set replaces pages. The same path covers
input's implicit Owner read. Other components retain their cached columns and
normal staged/SIMD behavior; no extra table/entity lookup or eager page copy is
added. Read-only parameters containing lists/maps/sets use the existing scratch
value-copy helpers when Set can overlap, because assignment can reuse stored
container allocations. This preserves the current invocation's values even
after self Set through a helper. Async handler resumes apply that copy too.
No runtime API, public C ABI, world layout, queue ordering or protocol changed.

`world_set_read` checks direct and transitive/recursive/method/action writes
across 5,000 rows, chunks and archetypes, partial and fully matching sparse
chunks, ordinary mutable companion parameters, preceding-row visibility,
snapshot immutability, rollback replay, packed round-trips, the threaded
scheduler, input selection after Owner replacement, local views and heap-bearing
handler parameters through self/other writes and task resumes. The two former
read-only rejection fixtures are replaced by this execution coverage. Existing
capability diagnostics remain pinned; mutable overlap is now execution coverage above.

Validation: warning-free full native/web debug builds, all 2,411 native and
2,320 web CTests, and the docs production build pass. This includes native
Vulkan/OpenGL, WebGL/WebGPU validation, existing exact-bit/network pins, the
new snapshot/row/container/task regressions and the existing mutable-overlap
diagnostics. Native took 133.93 seconds; web took 294.26 seconds. Other operating
systems, Android and sanitizers remain CI coverage; no CI was dispatched.

## Borrowed input parameters (2026-10-18)

The owner approved ordinary parameter syntax for read-only input context:
`bool WantsJump(Controls controls)`. A caller passes its input parameter
directly; helpers forward the same current/previous tick pair. No new owner
lookup, World/singleton/entity/sender capability, allocation or whole-input
copy is introduced. Nested bool edges, held/released input, prediction's edge
suppression, owner changes, server input and replay retain existing behavior.

The checker accepts input parameters in ordinary synchronous functions/methods,
rejects mut/out/in/default/optional/return/escape and diagnoses constructed or
copied arguments that lack the caller's tick context. Async declarations,
including match handlers, cannot retain input; pass ordinary field values.
Mutation diagnostics explain readonly input instead of suggesting `mut`.
Generated private helpers receive const current/previous input pointers. Input
pointer access, calls, action inlining and function-local sort captures preserve
the pair. Existing synchronous callbacks and parallel steps can forward it.
Call operand planning treats input arguments as borrowed reads, so effectful
neighboring arguments preserve source order without making a full input temp.
Existing whole-input text formatting and comparisons remain value inspections;
they do not store or forward an input context.
There is no runtime API, public C ABI, world layout or protocol change.

Correction to the earlier deferred-input note and discussion: custom input
fields are all serialized already. Use-based pruning is for device leaves;
there was no custom input-field pruning pass to preserve or add. Existing
device reachability analysis and custom input packing are unchanged.

`input_borrows` exercises recursion, methods, named/multiple parameters,
overloads, effectful/named argument ordering, action blocks, nested sort
captures, parallel steps, event handlers, retained snapshots and replay.
Existing `input` now reads edges through helper
chains (owner/server selection and owner swaps); `predict` forwards all reads
through a helper (nested edges and guessed-input suppression); `net` forwards
movement/jump reads without changing its bad-network traffic/hash assertions;
`view_time` forwards engine-stamped timestamps. Eleven diagnostic fixtures pin
unsupported signatures, mutation, defaults, missing context and escapes.
Editor completion offers input types in readonly synchronous signatures and
edges through nested input structs; hover explains the caller's tick context.
Public contracts are in spec.md, functions.md and the input guide.

Validation: final warning-free native/web debug builds, all 2,412 native and
2,321 web CTests, and the docs production build pass. Native GPU and WebGL/WebGPU
validation, existing exact-bit/network traffic pins, input edges and guessed
edge suppression, snapshot/replay, the effectful-argument regression and full
LSP/stdio/pipe coverage pass without changing expected values. Other operating
systems, Android and sanitizers remain CI coverage; no CI was dispatched.

## Borrowed query parameters (2026-10-18)

The owner approved read-only borrowing with the existing `query (...)` syntax.
Ordinary synchronous functions and methods require explicit query arguments;
the signature describes row data/filter guarantees, never a new selection.
Bindings match by component type, independently of names/order, with one
unambiguous source binding per requested type. Extra bindings/filters remain
the caller's. Required/excluded filters must already be guaranteed. Local rows
require local bindings; verified/blended/recorded timing comes from the source.
No collection, snapshot or whole-set copy is made merely to pass a query.
Borrowed queries have no mut/out/default, cannot be stored/returned, and remain
outside callbacks, parallel steps and async signatures. Whole rows still cannot
be copied/passed/returned; component copies retain ordinary value semantics.
Recorded rows retain the journal's omitted heap fields, as existing component
forwarding does. No implicit World, singleton or entity access is introduced.

The parser reuses query binding syntax. The checker validates structural row
requirements, marks data used through forwarding, and preserves source domains.
Existing World access propagation now distinguishes explicit World reads from
query reads so a borrowed query grants no ambient world pointer/capability.
Query values are unsettled in evaluation-order analysis: reads beside a writer
remain in their written order. Automatic grid loops that borrow stay serial;
explicit parallel/callback borrows are diagnosed.

`query_codegen.inc` follows static query sources through calls, forwarding,
recursion and multiple query arguments. Source-specialized helpers receive the
existing world pointer or recorded/blended row pointer/count. Iteration reuses
normal query loops and projects only requested components into helper rows.
Equivalent live selections share code; recorded layouts retain their sources.
No runtime API, world layout, container handle or persistent borrow is added.
World.Set helper writes use the existing replaced-page reads. Action-loop reads
use the outer routine's World effects too, so callback writes cannot leave a
cached column on the old snapshot page. Actions with borrowed parameters inline
with lexical aliases; helpers starting tasks keep ordinary owner/singleton rules
and retain no query in task frames. Sort blocks
capture ordinary values, excluding queries. Internal names use reserved tide_
prefixes for count arguments/specializations.

`query_borrows` covers projection, filters, methods, recursive forwarding,
multiple/swapped queries, named arguments, empty sets, action blocks (including
World.Set page replacement under a retained snapshot), sorting,
read/write operand order, short-circuiting, deferred sends, task starts, COW and
replay. Existing `queries` now executes helper traversal across chunks and
threads with its original exact-bit pin; `view_queries` forwards live local,
blended, verified and recorded rows, including owned text/list values in views;
`handler_queries` forwards event/command rows through World-writing helpers.
Thirteen query_borrow error fixtures cover missing/ambiguous bindings, missing
filter guarantees, domain mismatch, escape, mutation and unsupported lifetimes.
Editor completion/hover/signature formatting use the same query representation.
Public contracts are in docs/spec.md, functions.md, systems.md and view docs.

A bounded optimized traversal comparison used 65,536 integer component rows,
4,000 reductions per round and eight alternating rounds. Generated borrowed
query traversal and equivalent handwritten table/chunk traversal used the same
clang 23.1.2 `-O2` strict deterministic flags, with matching sums. Best times
were 8.119 ms and 8.145 ms respectively (ratio 0.997). This measures these
optimized loops, not world construction or general engine performance; linked
engine libraries were the checkout's debug build. Scratch harness/results are
under ignored `build/query-borrow-perf/`.

Validation: final warning-free native/web debug builds, all 2,399 native and
2,309 web CTests, and the docs production build pass. This includes native GPU,
WebGL/WebGPU validation, existing exact-bit/thread/replay pins, the new borrow
and snapshot/action regressions, thirteen diagnostic fixtures and LSP coverage.
Linux, macOS, MinGW, Android and sanitizer coverage remain for CI; none was
dispatched.

Integration with origin/dev retained both editor-note entries at the sole
conflict. Incoming terminal-editor/CLI and GRAVEWAKE changes touch no shared
compiler/runtime source. Rebuilt CLI/LSP targets pass eight scoped CLI/LSP
CTests, including the new installed-editor configuration test; the combined
docs production build also passes. The full suites above preceded this merge.

## Queries in synchronous match handlers (2026-10-18)

The owner approved extending the existing read-only query functionality to
match event and command handlers. The prior audit overstated the need for a
separate ordering design: synchronous handlers already execute serially at
their existing queue turn and can use the same current-world traversal as
systems. `check_entity_query` now refuses only async declarations. No new
runtime function, generated query representation, queue flush, snapshot,
system dependency or simulation layout was introduced.

Queries do not select the event target. A handler taking only queries and
singletons runs once and accepts an untargeted event/command. Outer component
parameters still select a target. Each foreach reads the stored match as it
iterates, including completed earlier handler writes and applied structural
changes. Structural work recorded in the handler remains queued. Existing
World.Set rules and transitive helper access analysis keep query reads current
after page replacement; each row is still a read-only component copy. An own
mut component parameter retains normal write-back timing: querying the same
entity sees its stored value until the handler returns, then subsequent
handlers see the committed value. Spawned includes its new entity, Destroyed
includes the entity before removal. Async handlers/tasks, query/row escape and
match/local access restrictions retain their existing rules.

`handler_queries` covers targeted/world events, query-only handlers, nested
and empty sets, sparse required/excluded filters, interleaved Send/Spawn/Remove/
Destroy, lifecycle queries, destruction requested during traversal, own-parameter
write-back, transitive helper Set calls, heap-bearing rows, COW immutability and
identical replay. Global/targeted commands exercise actual local submission,
sender guards, tick deferral and duplicate sequence rejection. Mutation errors
replace the two old rejection fixtures. The LSP completes handler query headers
and rows, explains queue-time reads on hover and reports query access in lenses.
Public contracts are in docs/spec.md and the systems/events/commands pages.

Validation: fresh warning-free native/web debug builds, all 2,385 native and
2,295 web CTests, and the docs production build pass. The full suites include
the new handler regressions, editor tests, existing deterministic pins,
Vulkan/OpenGL and WebGL/WebGPU checks. Native took 49.77 seconds; web took
262.32 seconds, using Node 24.19.0. Other operating systems, Android and
sanitizers were not run; no CI was dispatched.

## Conventional API completions (2026-10-18)

The owner asked to clear the remaining gaps that follow existing APIs and make
game code easier, without returning for routine language/API choices.

- Contextual `[]` initializes an empty `Map<K, V>` in fields, locals, assignments,
  arguments and returns. It emits the existing zero map handle. Nonempty map
  literals still explain adding entries by key; untyped `var value = []` gains
  no inference rule. `maps` covers nested values, retaining a copied map after
  reassignment, arguments and returns; `map_nonempty_literal` pins rejection.
- Spawn/Add and Scene.Load extra root components accept existing values and
  helper results. Existing argument hoisting evaluates once in written order;
  deferred commands own their copied lists/maps. Sparse presence, placement,
  duplicate-type checks and scene restrictions remain in the existing checker.
  Bare types still apply defaults, local sidecars are not copied, and Remove
  remains type-only. `component_values` checks source mutation, nested map/list
  retention, sparse and added components, scene roots, effectful factories,
  snapshots/packing, local spawning and fresh attached-local defaults. Prefab
  root overrides also accept whole values; literals retain field-wise override
  behavior, while values replace the whole component. The same fixture covers
  prefab Spawn and Scene.Load alongside the existing prefabs regressions.
- Ordinary functions/methods take and return events and commands. Payloads use
  existing value/mut and heap-retention rules; they provide no implicit World,
  entity or authenticated sender. `component_values`,
  `maps` and the existing wire-level `commands` fixture forward complete
  payloads through helpers and retained factory returns, including a local event.
  Numeric payload helper values also work on GPU; nonnumeric fields retain
  normal GPU restrictions. Owned payload reads/returns contribute to the
  existing scratch-use analysis, as ordinary records do.
  Recorded local match-event payloads still omit heap data, as recorded
  component copies already did; forwarding cannot recover omitted values.
- `Texture.size` is a machine-local read-only int2 on CPU and GPU. Metadata-only
  shader planning/capture and runtime contracts are in `notes/textures.md` and
  `notes/shaders.md`. New errors pin assignment and match access rejection.

At this pass input parameters were deferred because `.down`/`.up` needed the
previous tick context. Borrowed input parameters above now carry that context.
The earlier concern about losing custom input-field usage was overstated:
declared custom fields are all packed, with use-based pruning only for devices.

Validation: warning-free Windows native and web debug builds, all 2,383 native
and 2,293 web CTests, and the docs production build pass on the final source.
Vulkan/OpenGL and WebGL/WebGPU pixel checks cover the metadata/payload paths;
WebGPU repeats them after device recreation. Existing determinism pins are
unchanged. An earlier serial native run had one audio-device state-cleanup
timing failure; it passed in isolation and both subsequent full native runs
passed, with no audio change in this task. The clean origin/dev merge then
brought the owning thread's audio completion fix and browser-runner fixes,
alongside unrelated Gravewake work. Rebuilt affected targets and reran five
native platform checks (audio, device, new forget regression, Vulkan/OpenGL
shaders) and eight web checks (audio/forget, WebGL/WebGPU shaders, rooms,
handover, ended rooms and threads); all passed. Unrelated game changes needed
no repeated playtest. Linux/macOS/MinGW/Android and sanitizers remain CI
coverage; no workflow was dispatched.

## Conventional math API completions (2026-10-18)

The owner approved `quaternion.FromToRotation(float3 from, float3 to)` and
`float4x4.Scale(float3 scale)` as clear wins following the existing API style,
and clarified that routine completions and edge cases should be handled within
the assigned task without approval questions. Substantive language/behavior
and ownership decisions still belong to the owner; `AGENTS.md` records this
distinction.

Both signatures live in the shared builtins table, so ordinary checking,
named arguments, C generation, editor completion/hover/signature help and
shader-call checking use the existing paths. Generated CPU calls are
`tide_fromto_q` and `tide_scaling_f4x4`, inline in `tide/math.h`. No parser,
capability, evaluation-order or storage rule changes. The existing C matrix
scalar-multiplication helper `tide_scale_f4x4(matrix, scalar)` is unchanged.

FromToRotation normalizes arbitrary finite nonzero directions after dividing
by their largest absolute component, avoiding overflow and squared-length
underflow, including subnormals. Invalid or zero directions return identity.
The ordinary shortest-arc formula is used for nonnegative dot products. Near
pi, cosine of the half-angle is derived from cross-product length rather than
`1 + dot`, preserving small deviations from an opposite direction. Exact
opposites cross normalized `from` with its least-aligned cardinal axis (ties
X, then Y, then Z), and return a unit quaternion with w = 0. Quaternion output
is finite and chooses w >= 0. The scale builder directly sets the diagonal
and homogeneous 1, leaving other entries zero; negative and zero scales work.

Runtime accuracy tests compare independently normalized double-precision
directions after rotation, exercise 10,000 direction pairs, unit lengths,
nearly/exactly opposite directions, axis ties, zero/nonfinite inputs and the
smallest subnormal/largest finite magnitudes. The new
`crossplatform_direction_rotation_and_scale` hashes 100,000 arbitrary float
direction pairs (including invalid values and opposites), plus scaled points;
its pin is `A40EAB68989DC773`. Existing pins are unchanged. The math E2E fixture
checks defaults, positional and reordered named arguments and resulting
rotations/transforms. GPU coverage is recorded in `notes/shaders.md`; public
contracts are in `docs/spec.md` and `docs/language/math.md`.

Validation: full native debug passed all 2,374 tests. The full web debug run
covered 2,284 tests; its one WebGPU shader failure (reserved helper parameter)
was fixed, and focused engine, math E2E, shader golden, WebGL and WebGPU tests
then all passed. Focused native engine/E2E/golden/SPIR-V/Vulkan checks also
passed after the fix. Both native and WASI release engine suites and math E2E
passed, retaining the new exact-bit pin across all four configurations and
all prior pins. The docs production build passed. Other platforms remain CI
coverage; CI was not dispatched.

## Attached local arrays: final prototype compiler audit (2026-10-18)

GRAVEWAKE's `local Shard[4] pieces` report still reproduced: the parser's
lookahead recognized the contextual `local` field modifier before a name,
qualified/generic type or nullable type, but omitted an array's `[`. It parsed
`local` as the type and `Shard` as the field name, then failed at the bracket.
`at_local_field` now includes `[`. This restores the existing array/local-field
contracts; it adds no type, syntax or runtime function. A type actually named
`local` remains unambiguous because the modifier still requires a following
identifier. Match layouts, seeded results and hashes are unchanged.

The existing `local_fields` E2E now keeps a vector array on each match component
and a nested integer array on a match singleton. It checks nonzero defaults
before a row exists, helper writes, vector/swizzle and out-of-bounds writes,
separate entities/machines, disconnect resets and local pack/unpack followed by
continued frames. Its existing plain-world comparison and packed/hash checks
ensure these arrays remain outside match state. User docs and the spec clarify
fixed arrays alongside the other supported local field values.

Whole-singleton replacement was a stale report: current checking/codegen accepts
ordinary `mut` singleton assignment, and `singleton_params` already executes it
through `Replace`. A fresh direct-system probe also compiles. The documented
Resident/grid ownership restrictions still apply. The exhaustive report inventory
and remaining decisions are in tide-prototypes' `notes/private-prototypes.md`, Engine gap audit;
the host save-directory fix is recorded in `notes/standard-host.md`.

Final validation passes all 2,372 native and 2,282 web debug CTests, including
the expanded local-fields fixture and unchanged world/hash comparisons. Builds
are warning-free and the docs production build passes. Toolchain, backend
coverage and unavailable platform checks are recorded with the host fix.

## Follow-up prototype gap audit (2026-10-18)

The first pass was not exhaustive. CRACKED's default singleton Random seeding
also depended on presentation-only declaration order. A checked match-only
singleton ordinal now drives both its seed sites and its simulation-graph record;
details and exact runtime coverage are in `notes/random.md`. No runtime API or
state layout changes. Explicit singleton declaration order remains a match input.

The regression exposed another implementation detail in compatibility hashing:
`_ = expression` and `out _` get C storage names containing source line/column,
and those names were hashed as though authored. The graph now omits discarded
storage names from statements and their bound expressions. Its typed nodes,
ordered edges, calls, values and distinct output bindings still participate.
Formatting/moving discards preserves compatibility; changing their executed
match code still changes it. `simulation_identity_ignores_discard_storage_source_locations`
checks both directions, including a discarded result with an `out _` argument.
Full-suite, optimized parity and docs validation are recorded in `notes/random.md`.

## Compiler gaps found in prototypes (2026-10-18)

This pass fixes three inconsistencies with existing language behavior, without
new syntax, runtime functions, ABI/layout changes or deterministic hash updates:

- SUNDAY SUPPER's `foreach (var p in kit.placed[model])` generated an address
  of a returned `tide_list` value. Lists, maps and sets now share receiver
  lowering that captures the selected indexes/keys once and reads a stored
  container again each round. It retains no pointer into an enclosing
  container across a loop body or wait, so growing/replacing outer storage
  remains safe and inner additions are reached. A computed receiver is
  evaluated once into an owned value. Existing element-copy, insertion-order,
  missing-key and bounds rules remain; reading an absent map value adds no key.
- GRAVEWAKE's integer literals in skinned-draw shader values were captured in
  temporaries, then emitted again as float literals, leaving the captures
  unused under `-Werror`. Numeric widening now reads an existing capture,
  including negative literals and int-to-long conversions; uncaptured
  constants keep their existing literal output and rounding.
- CRACKED's `arena.seats[s].velocity.y = 0` was rejected by the element-path
  checker even though plain stored-field writes and their generated swizzle
  path already existed (`docs/language/text-and-lists.md` already promised
  `trail.points[i].x`). Vector lanes and final swizzles now pass that path for
  list/map/array elements. Readonly/capability, duplicate-lane and intermediate
  temporary-swizzle checks still apply. Heap-valued field/method/argument
  restrictions remain unchanged.

`prototype_codegen` reproduces both invalid-C cases with warnings as errors,
checks live nested list/map/set traversal during inner and outer growth,
once-only/computed receivers, missing and out-of-range accesses, positive and
negative numeric captures, vector/compound/swizzle writes and captured draw
values over repeated local frames. Its waiting match loops replay from both a
COW snapshot and packed bytes. Two error fixtures retain repeated-lane and
temporary-swizzle rejection through an indexed element.

This initial pass left the other reports unverified. The completed follow-up
inventory in tide-prototypes' `notes/private-prototypes.md`, Engine gap audit, supersedes that
temporary list: attached arrays and save isolation are addressed, whole-singleton
replacement already works, and the remaining restrictions/APIs have their actual
design boundary recorded there. Original observations remain in the game notes.

Validation: complete Windows native/web debug builds are warning-free with
clang 23.1.2 and checkout-local Node 24.19.0. The full suites exercised 2,369
native cases (71.65 s, including 31 Vulkan/OpenGL checks) and 2,280 WASI/web
cases (185.35 s, including WebGL/WebGPU). Their only failures were the two new
error fixtures using `// error:` instead of the required `// expect:` header.
After correction, both fixtures and the final expanded `prototype_codegen`
target pass on native and WASI; every other full-suite case passed. Existing
math/random/simulation hash pins are unchanged. All runs used `TIDE_HIDDEN=1`
and browser runners' mute flags. Other OSes, Android, sanitizers and private
prototype browser playtests were not run; no CI or deployment was requested.

## One visibility mechanism for scenes, entities and components (2026-10-15)

The owner approved replacing the separate private-scene API with ordinary
Visibility groups. A loaded root's Visibility.group limits every entity owned
by that scene, including later spawns. Its audience intersects the entity group
and then the component restriction. An inner scope cannot widen its enclosing
scope. Ordinary transform parenting still does not propagate visibility.
Groups do not own scene lifetime; destroying a group conceals its members
without unloading them. A scene with no restriction is public.

Scene.Load(Scene { ... }, Component { ... }, ...) accepts ordinary positional
root components before Spawned initialization. They use Spawn's component
defaults, placement, ownership, duplicate checks and written evaluation order.
A single named scene argument remains supported. Authored prefab Scene.Load
uses its existing root overrides, including Visibility; its special named
visibility argument is gone. Local scene loads accept local root components
but reject Visibility through the ordinary placement diagnostic.

SceneVisibility, Scene.AddPlayer/RemovePlayer, their command kind/payload and
the generated tide_visibility/tide_players scene fields are removed, without
aliases. Old membership calls point to Visibility.Set. Scene components now
contain only authored fields, and the internal prefab scene marker is empty.
The compiler extends Audience's words directly from maxPlayers. Recipient
codecs, lifecycle sanitizers and simulation-schema traversal no longer have
special cases for private fields of scenes. The ordinary private Visibility
policy and group directory keep their existing treatment. No engine source,
exported runtime function, tide_game ABI or session protocol changes.
Generated game layout/build identities change; old world-hash fixture values
that included the removed fields must be updated deliberately and agree on
native and web. The older private-scene descriptions below are superseded.

Regressions cover root components/defaults, sparse presence, owned text/list
values, local placement and source-order side effects before initialization;
live root/content/later-spawn audiences across multiple mask words, scene/entity/
component intersection, projected absence, immutable snapshots and raw packing,
fail-closed destroyed groups, and private root/child Spawned journals. The
200-player membership fixture uses a shared group. Lifecycle query/payload tests
vary private scene-root group policy rather than removed scene metadata.
Removed methods, duplicate root components, extra scenes, wrong component types
and local group writes have diagnostic fixtures. Editor/docs details and final
validation are recorded in their notes and below.

Validation: complete warning-free Windows debug builds and full CTest runs cover
2,364 native tests and 2,276 WASI/web tests, including native Vulkan/OpenGL and
WebGL/WebGPU. Their only failures were 30 complete-world/hash/serialization
baseline cases affected by the intentionally removed fields. Native and WASI
printed identical logical replacement values; all 30 pass on both after updating
those pins. The eight scene/prefab/visibility/player targets pass again with the
final privacy and root-policy removal regressions. All 33 affected optimized
native targets pass too, including every changed physics/game/benchmark pin.
The physics body/native-image/resident-digest/query-answer pins and runtime
math/random/pose bit pins are unchanged. LSP 234/234, shared grammar 244/244 and
the production docs build pass with clang 23.1.2 and local Node 24.21.0. The
checkout's configured builds use that Node; the owner's installation is untouched.
No CI, other OS, Android or sanitizer run was requested/performed.

## Entity/component visibility and shared audiences (2026-10-15)

The owner approved client simulation culling for world chunks, individual
players and private card state. Visibility applies to entities and whole
components; the field-level proposal was withdrawn. A visible component has
all its fields, while a withheld component has ordinary absence semantics.
The server remains authoritative. Games choose spatial/ownership/team policy
and how to expose interactions with hidden state. The exact API spellings,
opaque-handle representation and initial codec choices below are implementation
picks, recorded as Provisional in the spec.

`Visibility { group = group }` is a builtin match component usable through the
ordinary spawn/Add/Remove/mut machinery. It references a shared opaque
`VisibilityGroup`. `Visibility.CreateGroup(Audience)` creates a group,
`Visibility.Set(group, audience)` changes its audience, and
`Visibility.DestroyGroup(group)` invalidates it. IDs increase monotonically and
are never reused. The default group inherits an unrestricted audience; a stale,
destroyed or otherwise invalid nondefault group fails closed. Creating/changing
groups in a projected client world does not change server policy.

`Audience.All`, `.None`, `.Only(PlayerID)` and `.From(List<PlayerID>)` construct
value bitsets sized to `maxPlayers`. `mut` audiences have `Add(player)` and
`Remove(player)`; `Contains(player)` only reads. Invalid/out-of-game player
indices are ignored and never index outside the mask. Two audiences compare
with `==` and `!=` (2026-10-24, Claude's pick, Provisional: Earth's own
comparison only went through the first 64 players), every word of the mask
(`tide_audience_equal`), as `VisibilityGroup` compares its id; the
`visibility_groups` fixture covers a player in the third word. A group's audience is a
live reference, not copied into every member. Per-component restrictions use
`visibility.Set<Inventory>(group)` or `visibility.Set<Inventory>(audience)`;
`visibility.Reset<Inventory>()` restores inheritance. They require an ordinary
writable `Visibility` value and preserve normal helper/caller capabilities.
These calls target match components, never fields, scenes, Visibility itself,
or components whose values require local placement.

Scene ownership defines the enclosing scope. Its root Visibility group limits
the scene contents; entity groups and component override audiences intersect
that limit. A child override cannot reveal its parent.
Ordinary transform parenting does not implicitly create a visibility group.
Static group mutations inherit match effects through helper chains and count
as heap writes in scheduling; input/local/GPU code cannot mutate the directory.
Audience value operations need no match capability. The checker uses the new
`CALL_VISIBILITY` and ordinary writable-place/evaluation-order machinery.

The compiler synthesizes private world-owned directory and policy maps over
existing checked map/heap helpers (`visibility_check.inc` and
`visibility_codegen.inc`). No new exported engine function is called by Tide.
These values participate in server snapshots, hashing, packing and generated
reload layouts. Component override keys hash qualified type names, so source
declaration reordering does not silently retarget retained policies; collisions
are rejected. A renamed type intentionally has another key. Builtin types and
state activate when visibility is named.

Only match component types targeted by Set/Reset gain automatic sparse presence
storage. This avoids enumerating every component subset and reuses ordinary
query/filter/Has/Get/TryGet/task/view presence checks. Such types remain present
on the server unless normal game code removes them. Generated C-system wrappers
split filtered rows into contiguous passing runs; the preexisting restriction on
explicit `[Sparse]` C-system parameters is unchanged. Entity culling omits the
row entirely. Projected worlds do not restart Main or report the match ended
merely because no scene is currently available to that recipient.

`client_state_codegen.inc` writes permitted logical values rather than raw heap
pages. It recursively encodes text, lists, maps, arrays, optional stored values,
plain structs and input device records; map insertion orders are retained.
Grid chunks sort by coordinates, and cells use their typed codec. Allocated
zero chunks are retained: dropping them would change open-grid foreach extent.
Hierarchy links retain only available direct parents/children/siblings; loading
validates reciprocal links and rejects cycles. Cutting a hidden parent entity or
an existing hidden parent Transform detaches the child. If that retained subtree
has a permitted Transform, private client metadata carries the exact parent
affine matrix and rotation, while the child's local TRS stays unchanged. The
prefix uses the server's original multiplication/normalization order, preserving
shear and nonuniform scale. A subtree with no permitted Transform gets no anchor.
Explicit reparenting and destruction clear anchors; ordinary snapshots/reload
retain them. Readers validate booleans, UTF-8,
counts, duplicate map keys/orders, grid coordinates and packet completion before
replacing the previous world. Entity IDs remain authoritative even across large
unseen gaps; their missing slot pages are left absent. Prediction appends beyond
known IDs instead of reusing concealed slots. Encoding/decoding recursive values
is bounded at 256 nested typed values; deeper encoding stops with an explicit
message recommending entity references instead of unbounded value nesting.

Private directory/policy bytes and hidden components' owned data do not enter
recipient state or hashes. Whole match singletons remain public; secret game
state belongs in visibility-controlled entities/components. The current physics
packages need visibility-aware integration before concealing bodies: public
Box2D.Physics.image contains the complete native snapshot byte list,
Box3D.Physics.stamp hashes native world operations, and both packages retain
public collider/rewind lists. Hiding Body/collider entities or omitting Resident
does not restrict those ordinary singleton fields. Internal command
delivery state is restricted to the recipient's own sequence/window; other
players' command activity is withheld. Input audience masks are derived once per
recipient by walking entities, then reused for all input slots. Remote previous
input is normalized to its filtered current input; ordinary completed-tick
endpoints already have equal previous/current input, preserving button edges.
The session uses the bulk input callback to avoid scanning the world per slot.
Source task frames
can contain secrets captured from hidden state, and opaque `Resident` storage
can contain hidden collision/runtime data. Both remain authoritative server
state in this implementation. Joining/repairing clients do not receive those
continuations or resume their server execution; permitted component outcomes
reconcile through the ordinary correction path. Clients may run the tasks and
C-side state they can derive locally, but that is prediction, not a private
server-state transfer. Full-world games retain their existing task/Resident
snapshot and transfer path. The session protocol and lifecycle contracts are
recorded in `notes/netcode.md` and `notes/verified-and-local-handlers.md`.

Coverage: `visibility_groups` checks multiword audiences, bounds, shared changes,
private components, direct overrides/reset, fail-closed destroyed handles,
scene inheritance, helper capabilities, COW snapshots and raw packing.
`visibility_projection` checks secret markers absent from payloads, nested
owned values, canonical equality despite heap/grid insertion history, actual
component/query/C-row absence, filtered hierarchy destruction, all-hidden scene
behavior, sparse high entity IDs and raw round trips, malformed truncation
atomicity, withheld input slots, private command metadata, no-listener destruction
causes and opaque Resident privacy. Exact affine boundary regressions include
nonuniform scale/rotation, hidden parent Transform, reparenting and omission of
anchors for hidden leaf Transforms. Initial recipient state pins canonical hash
`ed6d2f60242f07d3` across native debug, WASI web debug and native release. Its integrated
two-recipient loopback case exercises changing chunk/player/card audiences and
hidden impacts under latency, jitter and loss against retained authoritative
tick snapshots. `visibility_*` error fixtures pin unsupported scopes, local
values, readonly receivers and capability propagation. Final full-suite results
are recorded with the networking/editor changes after validation completes.

Performance measurement (Windows clang 23.1.2, 2026-10-15): the projection
fixture plus 10,000 visible PublicState rows with empty owned fields produced
1,087,683 logical bytes. In release/O2, three runs of 200 calls measured
2.12Ã¢â‚¬â€œ2.92 ms per recipient canonical hash after changing every added row's integer
column; unchanged worlds measured 2.22Ã¢â‚¬â€œ3.54 ms. Full-world hashes measured
4.19Ã¢â‚¬â€œ9.76 us with that column dirty and 2.57Ã¢â‚¬â€œ11.11 us unchanged. These operations
do different work: full-world hashing reuses cached page hashes and only rehashes
the changed integer-column pages, whereas recipient hashing currently traverses
and serializes every permitted logical value and allocates the temporary byte
stream each call. This is not a comparison of equivalent serializers or a
hardware-independent tick budget. Debug/O0 (40 calls) measured 6.08 ms for the
recipient hash versus 13.88 us for the changed-column full-world hash, and
8.66 ms versus 36.31 us unchanged, including full-world cache warmup. The
visibility path's linear work and allocation remain a performance limitation;
since 2026-10-24 each entity's part is hashed once a tick for every recipient
(`gen_client_hashes`, `notes/netcode.md`). The no-visibility path keeps its
ordinary cached full-world hash. The focused release projection fixture passes
all 12 cases and the same canonical pin.

Pre-integration validation: warning-free complete Windows native and WASI web
debug builds pass all 2,357 native CTests (76.53 s, including 31 GPU tests) and
2,270 web CTests (212.18 s, including WebGL/WebGPU). The lifecycle fixture passes
16 cases, covering privacy, rollback, bulk destruction and malformed received
values. LSP 234/234, shared grammar 244/244, VS Code adapter 28/28 and the
production docs build pass with Node 24.21 and clang 23.1.2. Earlier opted-in
Cracked/Foldline native/web targets and tests also passed after the reserved-name
renames; final incoming integration validation follows below. Android, other
operating systems and sanitizers were not run; no CI was dispatched.

Final integration validation: both complete standard builds remain warning-free
after the compiler/session/Box3D overlap through 2c6de193. All 2,359 native
CTests pass (69.54 s, 31 GPU tests), as do all 2,272 web CTests (221.93 s,
including WebGL/WebGPU). All seven simulation-identity cases pass. Cracked and
Foldline targets/tests pass on native/web. The subsequent hosting-console and
Foldline redesign merge through 9f1d3581 changes no engine/compiler source;
the renderer overlap preserves the redesign with eleven Shown-to-Displayed
helper renames. Fresh scoped Foldline checks pass 2/2 native (10.49 s) and 3/3
web (92.66 s), including both rendering backends. The final production docs
build and SEO 3/3 pass with Node 24.21. Both presets finish with optional private
prototypes disabled. Other-platform/sanitizer coverage remains unavailable here;
no CI was dispatched.

## Generated simulation identity (2026-10-15)

`asset_usage.c` now also walks an ordered canonical simulation graph for SHA-256
compatibility. Match schemas/defaults, transitive helpers/tasks, handlers, input
Sanitize/Predict, scenes/prefabs, settings and simulation asset contents enter;
local fields/views/presentation helpers do not. Repeated edges and ordering
participate, with stable numeric node references rather than pointer bytes.
Wire archetypes remain conservative inputs. Codegen fingerprints before server
stripping and emits both `TIDE_SIMULATION_ID` and `tide_game.simulation` outside
the old header-layout hash. CMake normalizes compiler/runtime sources into the
toolchain identity; custom C/library identity is supplied by CLI. Simulation
asset edits require regeneration while visual assets can still repack.

Visibility activation, component automatic-presence classification and resolved
visibility operation modes enter the same graph. An explicit recipient schema
fingerprints the ordered public match singletons, input and per-archetype codec
records, including every effective sparse column and its presence byte. Logical
types have stable numbered records, with fields, container elements and fixed
array lengths visited in wire order; host pointer padding does not enter this
logical schema. A Set/Reset target in an otherwise unused helper still changes
storage and therefore compatibility when it changes the emitted columns.

Visibility games additionally fingerprint the local-listener wire contract:
listener order/names, event and predicted/verified selection, required/excluded
component filters, match payload types and ordered query bindings. Global
listener order matters because it determines shared query-record IDs and the
generated journal build identity. Local bodies, local singleton/field types,
presentation helpers and read-versus-mut access to attached local fields stay
outside this fingerprint when they do not change wire metadata. Games without
visibility keep local listeners outside simulation identity entirely. This
preserves local presentation changes while rejecting different recipient state
or journal layouts before session admission.

Seven compiler regression cases cover helper/view exclusion, ordered shared
edges, simulation/visual asset bytes, listener payload/filter/query changes,
global query-record order, ordinary local-listener exclusion and sparse columns
introduced only by unused visibility helpers. The isolated scratch identity
tests passed before integration; fresh merged validation follows the full builds.
No syntax change; contracts, session tests and validation: managed-hosting.md.

## Frustum culling settings (2026-10-15)

The approved rendering work adds `Draw.FrustumCulling(bool)` / `SetFrustumCulling(bool)` and `Draw.Bounds(float3 center, float3 size)` / `SetBounds` through `add_setting`, including reset forms. Target lookup, editor signatures/docs, block/Set diagnostics and early-exit pops use the existing machinery. No parser/checker, capability or evaluation-order rule changes. Generated fragment-only 3D shaders advertise their position offset for automatic bounds; user verts and skinning do not. `draw_frustum` E2E verifies rejected operands still run, and two error fixtures pin the setting forms. API choices are provisional in the spec; runtime and validation are in `notes/drawing.md`, Frustum culling.

## Dedicated server asset selection (2026-10-15)

Server codegen preserves the ordinary client header/game identity before
selecting simulation assets through asset_usage.c's checked iterative traversal.
No new Tide syntax or match capability is added. Reachable Asset<Mesh<V>> types
keep their complete resolver domain for input/command selections. Host metadata
macros are outside the shared hash. Client-only globals can remain zero in
unreachable emitted code; loaders/preflight/packs retain only the selected
closure. Custom C requires conservative retention or explicit opt-in. Full
contracts, fixtures and validation are in dedicated-servers.md.

CI correction: server codegen skips the asset-array memcpy when the count is
zero, preserving the asset-free program's NULL array without passing it to a
nonnull C library parameter. Run `38716371711` caught this with UBSan while
compiling `server_menu.tide`; validation is recorded in dedicated-servers.md.

## View inclusion in authored preview (2026-10-13)

The owner approved views-only preview after discussing initialization and the
ambiguity of [Editor]. [RunInEditor] includes a view in both normal and preview
frames; [EditorOnly] includes it only in preview. Unmarked views are game-only.
Attributes take no arguments, are mutually exclusive, and reject systems and
handlers. Normal view capabilities and ordering still apply. The checker reads
these attributes after bodies; codegen selects the scheduled views before binding
their hidden draw/GUI parameters. Excluded views have no run/body wrapper.

Private codegen_options select an editor prefab/placement and an optional source
overlay. compile_inputs preserves the original source path for asset/namespace
resolution, rejects an overlay outside the chosen source boundary, and injects
a checked private Spawn factory. This derives ordinary archetypes and prefab
recorders instead of adding a runtime type registry. Preview world/local startup
omits Main; command dispatch and Forgotten callbacks omit user handlers, and
frames omit the match-event listener path. Systems are never ticked by this host.
Views can construct local visual state through ordinary helper/task capabilities.
Generated editor_codegen.inc owns the isolated module and viewport drawing;
details and limitations are in notes/editors.md. No new runtime function was
required, and ordinary game callback signatures remain unchanged.

Correction found by the new fixture: a nested prefab with no Transform anywhere
crashed gen_hierarchy when its optional Transform load had no C type, especially
in local placement. check_prefab_spawn now ensures the Transform placement needed
by parenting helpers, just as explicit SetParent already did. It does not add a
Transform instance to authored entities. The same transformless root/child tree
is exercised in normal game and both preview-mode tests.

Coverage: editor_views E2E proves game startup/events/systems and normal/shared
views still run, while EditorOnly does not; private preview hosts pin the
complementary selection, defaults, full nested tree, no gameplay startup,
resize and repeated cleanup for match/local placement on native and WASI.
Three compiler error fixtures reject wrong target, arguments and duplicate
inclusion. Native/LSP analysis also recognizes opted-in views against composed
prefabs that ordinary game code never spawns. Existing unmarked-game unreachable
warnings remain intact. Native integration checks actual shader output and
draft rebuild behavior through the installed CLI/editor, rather than checking
generated strings alone.

Validation on Windows: complete native/web debug builds and all 2,266 native
and 2,231 web CTests pass, including native OpenGL and WebGL/WebGPU. Final
editor/LSP changes also pass the eight focused native CTests (full server,
stdio, both preview hosts, game selection and document/CLI/UI). The installed
CLI/editor, staged with this checkout's MinGW platform library and pinned
toolchain, passes preview.mjs; bounded native PNGs show the custom shader and
unsaved overrides. The VS Code extension bundles successfully; all 259 combined
grammar/adapter checks pass. JetBrains buildPlugin and its isolated platform
tests pass on JDK 21 against IntelliJ 2024.2.6. The MinGW release native editor
also builds and passes its real --check, covering its GUI-subsystem CRT entry.
All nine Windows installer cases
pass with mocked registry writes, and the production docs build passes.
Other host OSes, Android, sanitizers and plugin verification against newer IDEs
were not run; no CI workflow or real installer/association action was invoked.

Integration: origin/dev 83075834 merged cleanly in code; three top-of-file note
conflicts retain both new sections. Incoming input prediction and render-target
changes touch the compiler/runtime boundary, so complete native/web debug builds
and suites were repeated: all 2,284 native and 2,249 web CTests pass, including
OpenGL/WebGL/WebGPU. Preview tests also pin the project's tickRate in frozen Time.
The refreshed private installation uses current engine headers/sources, compiled
editor/CLI and rebuilt MinGW platform library; preview.mjs passes again. The
merged grammar/adapter suite passes 262 checks. The merged production docs build
and all ten admin/public-index HTTP tests pass, preserving the public catalog
boundary. No CI was dispatched.

## Custom input prediction (2026-10-13)

The owner approved `Predict()` and `[DontPredict]`. Inputs now have an optional
parameterless prediction body with Sanitize's input-only capabilities, including
pure helper calls and existing scratch cleanup. The parser/checker/codegen and
asset-usage traversal carry that body separately from Sample and Sanitize.
Marked direct input fields reset to their declared defaults before Predict;
whole structs, optional fields and fixed arrays retain their ordinary value,
padding and repair rules. Duplicate marks, arguments, placement outside input
and combination with Absolute are errors. The engine-owned viewTime remains
read-only. First-build ordering and restrictions are Provisional in the spec.

Generated prediction adapters use previous guessed values, then marked defaults,
Predict, repair/bounds and Sanitize. The existing guess cutoff applies afterward.
A game's Predict can explicitly replace a marked default. No game-visible world,
local state or time parameter is added. The callback/rollback boundary is in
notes/netcode.md; the tide_game ABI marker changes, without changing world state
layout or the wire protocol. Existing games emit no prediction callback.

predict E2E checks defaults/arrays/optionals, pure computation and text scratch
cleanup, repair/bounds/Sanitize, untouched own samples, remote server input,
button-edge suppression, snapshots and high-latency/loss/jitter sessions with
zero resyncs and quiet-period replay stability. predict_fields and predict_method
exercise each feature independently. Fifteen diagnostics pin signature, purity,
helper capabilities, viewTime and attribute restrictions. Editor integration
and validation are recorded in notes/editors.md.

Remaining: nested-field attributes, age/time-aware prediction parameters and
per-field guess limits are not part of this implementation. Server ticks missing
input retain the existing authoritative held-input rule.

Validation: warning-free complete Windows native and web debug builds pass;
all 2,278 native and 2,243 web CTests pass, including native OpenGL, WebGL/WebGPU,
networking/migration/safety and unchanged determinism pins. All 235 shared grammar
checks and the production docs build pass with Clang 23.1.2/Node 24.16.0.
The predictor's text-scratch branch and idle replay checks are included in both
full suites. Initial focused checks corrected a test-harness null argument,
a viewTime diagnostic pin and Predict's expected keyword grammar scope before
these final full runs. No CI, Android, Linux, macOS, MinGW or sanitizers were run.

## Checked static asset references (2026-10-12)

The owner approved checked asset-reference fields for prefab model selections.
Asset<Mesh<V>> is a synthetic builtin ordinary struct with one hidden long
identity field, one instantiation per Mesh type. Ordinary layout/padding,
placement, ownership, task storage, snapshots, packing and hashing apply without
an engine API or runtime type registry. The first spelling/access and limits are
Provisional in docs/spec.md, Checked asset references.

A matching singular named asset converts under an expected reference type.
Component defaults, authored overrides, arguments (including overload selection),
context-typed conditionals and list elements use that conversion. Expressions
retain BIND_ASSET for dependency pruning and editor rename/navigation. A borrowed
Mesh value, collection element or different vertex type cannot supply identity.
Value reads the current asset global through a private generated typed resolver;
the resulting mesh retains existing borrowed lifetime and read-only restrictions.
Empty/unknown IDs resolve to empty geometry, never an unchecked array/pointer.

IDs are the qualified asset name's FNV-1a hash, masked to a positive 63-bit value,
with zero reserved. Codegen rejects collisions among compiled singular meshes.
Changing a model file or declaration order preserves the reference; changing its
qualified name changes identity. Renamed persisted runtime state is not migrated
to the new identity. Synthetic types introduced only in routine bodies are ordered
after checking as well as during ordinary field ordering. No-assets builds still
emit their empty resolver. JSON rejects the opaque builtin through its existing
rule rather than silently dropping its hidden identity.

asset_references E2E covers defaults, prefab/nested-root assignment, match/local
storage, overloaded calls, conditional/list conversion, current-pack reload,
snapshot and packed-world round trips, unchanged match state during local views,
and arbitrary unknown identities. asset_reference_empty covers a late local type
without any assets. Five negative fixtures cover unsupported resource types,
borrowed conversion, hidden storage, readonly Value and vertex mismatch. The LSP
test covers Value and its mesh member completion without exposing identity.
The shared grammar recognizes Asset as a generic type.
An additional --strip-unused-assets generated-C build/run retains a mesh used
only by a spawned prefab field, omits an unused declaration, and passes the same
snapshot/serialization harness with warnings as errors.

Remaining: other resource kinds, collection-element identity, JSON asset-name
serialization, declaration-rename runtime migration and skinned-model preview.
Editor implementation and native UI checks are in notes/editors.md.

Validation: warning-free complete Windows native/web debug builds and all 2,260
native and 2,225 web CTests pass, including OpenGL, WebGL/WebGPU, existing exact
determinism pins and language-server coverage. All 232 shared grammar checks and
the production docs build pass with Node 24.16.0. The final omitted-model footer
change was rebuilt and passed native UI coverage before the full native suite
reached that test. No engine runtime/API change, CI dispatch, Android, other-host
or sanitizer validation was performed for this task.

## Authored component bindings for the native editor (2026-10-12)

Prefab composition retains the resolved component declaration on the original
authored entry, alongside its checked expanded copy. The native editor uses this
source binding to patch existing overrides by the actual component identity,
including qualified/package names. Placement-specific expansion/checking and
generated code remain unchanged; no syntax, runtime call or layout was added.
The editor's package/nested-override cases exercise this binding. App contracts
and validation are recorded in notes/editors.md.

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
  - Read-only component parameters permit direct and helper `Set<T>` calls on any target and retain their invocation's value. Mutable overlap follows the owner-approved active-binding replacement above. Each row reads the current affected pages, including preceding writes.
  - `world.TryGet<T>(entity, out value)` is the engine's own beside `Get` and `Has` (`check_world_call`, its `out` through `check_out_param`): `tide_tryget_<Component>`, only in the C of a game that calls it (`decl.tried`), which gives whether the entity has one and assigns the variable its copy or the component's `default`. A component with a grid has none, as it can't be copied.
  - A program that passes no `World` to a function and calls no `TryGet` generates the C it did, byte for byte: checked by running old and new tidec over every game's source list.
  - `compiler/tests/e2e/world_functions` runs it (functions, methods, a World passed through two functions, `mut World`, local components in a view, `TryGet` of every form) natively and on the web; `compiler/tests/schedule/world_functions` pins that a function's reach is in its system's schedule; `compiler/tests/errors/world_function_*.tide` and `world_try_get_*.tide` have a file for each diagnostic; `lsp_world_in_functions` checks the editor's side.
  - Provisional implementation details: `mut World` in functions and methods; async functions and code the GPU runs taking none; `TryGet` giving the component's `default` where `Get` gives zeros. Read-only overlap and mutable active-binding replacement are owner-approved, as recorded above.
- **Functions and methods send, spawn and change entities as the code that calls them could** (the owner, 2026-10-08: "it should inherit their ability; if you say `Send` it should mean that what calls us can `Send` otherwise is a compiler error ONLY when its called from a place it can't"; `docs/spec.md`, Functions, with the picks made to build it Provisional under Functions and blocks). It was a recurring ask of the agent-written games (the 2026-10-06 trial's report, `GAME_TRIAL_FEEDBACK.md`, removed 2026-10-21), and Two Forks compared two copies of its board after a move to learn what to send. `Send`, `entity.Send`, `Spawn`, `Add`, `Remove` and `Destroy` are written in a function or a method as in a system. What's built, and what code relies on:
  - **A function records what it does, and nothing is wrong with it there** (`inherits`, `note_act` in `compiler/src/check.c`): `decl.acts[2]`, the match's ([0]) and this machine's ([1]), with where and what for the messages (`acts_at`, `acts_what`: "sends 'Hit'"), `acts_spawn[2]` and `acts_text`. Which world is told by what's written, as an async function's is: an event's own (a command is this machine's), the entity's type (`Entity`, `LocalEntity`), the components'; `same_world` is the one error in the function, a `Spawn`'s, an `Add`'s or a `Remove`'s components of two worlds, and an event sent to an entity of the other. A `Spawn` with no components is the match's. `check_component_side` and `check_entity_side` let an inheriting function through, where they check a system's or a view's against its own world.
  - **`refuse_act`** is what still refuses where it's written: frags and verts (the old messages), a struct's operators and a component's `Interpolate` (messages of their own), and the input's code. `passes_acts` says which declarations inherit: plain functions and methods, not async ones (their world is what they do), not operators (an operator's call is handed its operands, the dice and the seed and nothing else: `gen_operator_call`), not `Interpolate`.
  - **`mark_act_callers`** carries it from a function to the functions and methods that call it, over the checker's list of calls until nothing grows. It runs twice: once every function's body is checked and before any system's, so a foreach over a grid whose step calls one goes in order (`own_cell_only_expr`), and again in `check_acts`.
  - **`check_acts`**, after `check_sends` and before `resolve_tasks`, is where the errors are, at each call from code that has abilities of its own: a view or a local handler calling what's the match's, the match calling what's this machine's (a command's message says who sends commands), `Sample` and `Sanitize`, a frag or a vert, an operator or `Interpolate`, and any call in a parallel loop's step or a comparison (`call_site.parallel`, `comparing`). An async function that calls one gets its world from it (`note_side`), which is why it comes before `resolve_tasks`. A function's own callers answer for it. An `audio` declaration that does is `check_audio_bodies`'s, with the rest of what the audio thread can't.
  - **It's the calling system's in the schedule:** `check_acts` sets `decl.spawns` on a system, view or handler that calls a function that spawns, and `decl.writes_text` on one whose function records text, as `note_spawn` and `note_text_write` do for its own. So `analyze_parallelism`, `system_splits` and `system_settles` need no rule: a system that splits and calls a function that spawns gets temporary handles and settles them, in its queue and in the entity fields of its `mut` components, which is everything a function could have written one to. A function that sends keeps a system splitting.
  - **In generated C the function is handed what it records in**, where its other hidden parameters go (`gen_routine_signature`, `gen_routine_call` in `compiler/src/codegen.c`): `tide_world *tide_w`, not const (`takes_world`, `changes_world`), `tide_local *tide_l` (`takes_local`), and `tide_entity tide_scene` when it spawns in a world that has scenes (`takes_scene`, `scene_is_local`), which a call gives its own `tide_scene` when that's of the same world and the null entity otherwise. The `Send` and the `Spawn` in it are the lines a system has (`gen_send`, `gen_spawn`, `gen_method`), so they record through `tide_recording` into the queue of whatever task runs the caller, and `tide_new_entity` gives a temporary handle there. A function nothing could call (both worlds) still generates C that compiles.
  - **A call of one keeps its order** (`call_keeps_order`): one that gives a value runs before its statement when anything does, like a call to C. `call_acts` counts it as a call that does something, so the unused-local warning says to keep the call as a statement.
  - **A system whose whole body is one `Send`** still writes its events as a run (`batch_event`; since 2026-10-20 one queue item a run, Events in runs at the top); through a function it takes the ordinary push, one event at a time.
  - A program with no such function generates the C it did, byte for byte: old and new tidec over every game's source list, with `--layout` and without (234 games).
  - Tests: `compiler/tests/e2e/function_acts` (the order among a system's own sends, a sum's calls, a block's; a struct's and a component's methods, a task and a command's handler; `Add`, `Remove` and `Destroy`; a spawn joining its caller's scene and going with it; one thread against a pool that says it has four, tick by tick, with events sent to entities spawned there; a view's functions sending a local event and a command and spawning and destroying local entities), natively and on the web; `compiler/tests/errors/function_acts_*.tide`, a file a diagnostic. `event_send_in_function` and `function_spawn`, which pinned the refusals, are gone.
  - **Editors:** completion offers `Spawn` and `Send` in a function or a method, with the events of both worlds, and an entity's `Add`, `Remove`, `Destroy` and `Send` after its dot there (`passed` in `list_members`, and the routine's branch of `complete_expression`, `compiler/lsp/analysis.c`), not in an operator, an `Interpolate` or an `audio` declaration, and never `Snap`. They were hidden in every function, async ones too, which could always send. `lsp_functions_send_and_spawn` checks them, and the diagnostic at a view's call.
  - **A dropped value warns, and `_ = value;` drops one on purpose** (built 2026-10-11, both the owner's, from `request.SetHeader(...)` written as a statement, which gave a request nobody kept). `check_stmt`'s `S_EXPR` notes a function's or method's call whose type isn't void, an error or failable (`checker.dropped`), and `warn_dropped_values`, after `warn_unused_locals`, warns for those `call_acts` says do nothing: it has to wait, as whether a function draws, calls C or acts is known once every body is checked. `_ = value;` is the parser's (`parse_simple`): a `var` named `_dropped_<line>_<column>`, marked `discard` and read, so a value dropped goes through everything a variable's does (a call's order, an `await`, text and lists) and generated C needed nothing. No game in the repository had such a call. Tests: `compiler/tests/warnings/dropped_value.tide`.
  - **Session calls and `Clipboard.Copy` in a function or a method** (built 2026-10-11; the owner, asked whether to lift "functions can't call Session yet": "yes lets improve the session stuff on Tide"). They're this machine's acts: `check_session_call` and the clipboard's check let an inheriting function through and `note_act(c, true, ...)` with "calls Session.Open" or "calls Clipboard.Copy", as a command's send does, so `mark_act_callers` and `check_acts` need no rule for them and the errors are theirs (the match, `Sample`, a frag, a parallel loop's step, a comparison); `check_acts` words the match's as "the match runs the same on every machine" for a `what` that starts with "calls ". An operator and `Interpolate` are refused where written. Generated C needed nothing: `takes_local` is true of `acts[1]`, so the function is handed `tide_l`, which `gen_session_call` and `tide_request_copy` already name. "A statement of its own" still holds in a function. `session.room` (and `session.invite`, 2026-10-21: the same branch of `check_member`, its own message, `singleton_param_session_invite`) and a whole `Session` as text stay refused in functions and methods (`singleton_param_session_room`, `_text`, `_append`): the room is beside the `Session` value, in the local state, and a helper gains no implicit lookup; making the room part of the value is the runtime's side and wasn't done. tidels offers `Session.` and `Clipboard.` there (`decides_session`). Tests: `tidec_e2e_session_main` (a function a function calls, a struct's method, the clipboard, a local handler's function), `compiler/tests/errors/session_in_function`, `clipboard_in_function`, `session_function_in_sample`, `session_in_operator`; `singleton_param_session_operation`, which pinned the refusal, is gone; `lsp_functions_send_and_spawn`.
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

Integration through origin/dev 9ae734b8 merges cleanly. Incoming CLI development
optimization/cache changes rebuild warning-free and pass all 10 native CLI CTests;
the production docs build passes again. Other incoming work is prototype content
and benchmark evidence, with no compiler/runtime overlap requiring full repeats.

## First authored prefab compiler build (2026-10-12)

The owner chose ordinary root components for settings and complete-tree
construction before root-first Spawned handlers. The parser accepts named prefab
trees, ordinary component entries, named child blocks and nested prefab roots.
The provisional spelling and limits are in the spec's Prefabs and authored scenes
section; .tprefab files accept composition and namespace/using directives only.

prefab_check.inc expands a tree at each spawn/load site and separately validates
unused declarations. Copied expressions retain their source namespace and nested
instance scope. Named paths support forward references; this denotes the current
entity block. Component/default/placement checks remain ordinary typed checks.
Recursive inclusion, duplicate components/fields/child paths, missing symbols,
nonconstant authored expressions and absent SceneRequired references are errors.
SceneRequired initially applies to direct Entity/LocalEntity component fields,
including nullable ones, without constraining ordinary code-driven construction.

Spawn(Prefab, Component { fields }) and local Spawn return ordinary typed handles;
component literals replace only named root fields, bare components reset their
defaults, and absent root components can be added. Runtime argument expressions
evaluate once in written order before recording the tree. Scene.Load accepts the
same root component arguments, with optional named visibility last. A private
per-placement scene marker distinguishes loaded roots without a wrapper entity
or implicit public prefab component. Existing scene Main startup stays in .tide.

prefab_codegen.inc emits specialized private recorders using existing entity,
queue, text ownership and parenting functions; no runtime API was added. All
handles are reserved, then rows and parents recorded, then root-first ready
commands. Pending spawn commands suppress initialization until those ready
commands; normal spawns retain their ordering. Temporary handle settlement still
uses ordinary spawn/reference/parent machinery. Composition is bounded at compile
time to 1024 entities and 128 nesting levels; no game-controlled tree allocation
or recursive runtime factory is introduced.

Coverage: prefabs e2e checks nested inherited defaults, nullable required paths,
dynamic overrides evaluated once, root settings additions, complete parenting
before initialization, root-first order, match/local isolation, subtree destroy,
scene unload, snapshots/raw packing, and identical split-job versus serial ticks.
Eight negative fixtures cover invalid/removed components, missing/required paths,
recursive inclusion, duplicate children/override fields, runtime expressions and
placement mismatch. Project discovery/editor coverage is in their notes.

Remaining: native scene-editor UI, authored child-name rename/completion, inherited
child removal/replacement, prefab startup/identity conventions, authored local
visual fields on match entities, and live-instance defaults/override reconciliation.
No native editor or tide open command is built. Dedicated portable-wire migration,
prefab queued hot-reload and performance measurements remain untested.

Validation: warning-free complete native/web debug builds and all 2250 native
and 2218 web CTests passed on Windows with Clang 23.1.2/Node 24.16.0, including
OpenGL, WebGL and WebGPU. Final neutral-binding correction for authored local
entity lists and local scene-load coverage passed the 14 affected native checks
and all 9 web prefab checks. Local reference elements retain their declared type
in neutral validation; actual spawns still check every reference's placement.
The correction's valid List<LocalEntity> fixture fails against the earlier binder.
The final editor completion adjustment passes all 225 LSP tests and stdio/pipe
checks. Docs production build, 230 grammar checks, 24 VS Code tests/build and
JetBrains plugin build plus 7 headless adapter/platform tests pass. No CI, other
operating systems, Android or sanitizers were run; opted-in JCEF/client integration
was not run. Source/whitespace review passes.
