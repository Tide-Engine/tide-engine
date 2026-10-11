# The language server and the editors

## Asset declaration type completion, 2026-10-11

The owner reported that asset types had to be typed in full. The top-level
completion context had no branch for `asset`. `complete_asset_type` now reads
the preceding tokens, so incomplete declarations and partially written types
offer string, Texture, Clip, Grid2, Mesh, SkinnedMesh, AnimationCatalog,
Animation, Cubemap, List and Map without requiring a surviving declaration AST.
Generic insertion snippets fill Grid2<Color>, Mesh/SkinnedMesh<Vertex3>,
List<byte> and Map<string, Texture>, with editable type placeholders where
appropriate. The same shared server serves both editor integrations.

Inside a collection, suggestions follow the checker's current restrictions:
no Cubemap/Animation collections, one collection wrapper, string map keys,
and List<byte> for a file's bytes. Grid elements offer Color; mesh vertex and
optional skinned contract arguments offer structs and namespaces, with ordinary
checker diagnostics still deciding which struct meets the required contract.
Complete types/names, initializers and later declarations retain their contexts.
No asset import, language or runtime contract changed.

`lsp_asset_type_completion` covers empty and partial types, a newline,
attributes, namespace blocks, list/map elements, map keys, grid elements,
byte-file collections, mesh arguments and declaration-boundary regressions.
The Editors page documents the suggestions. Coverage is through the LSP
protocol; interactive editor rendering was not separately exercised.
Validation: warning-free complete native/web debug builds, all 2,188 native
and 2,157 web CTests (including OpenGL/WebGL/WebGPU), and the production docs
build pass with Node 24.16. The server is rebuilt at `build/tools/tidels.exe`.
Other host platforms and CI were not run.

## JSON and text primitives, 2026-10-11

`Json.Read`, `Json.Write` and `string.FromBytes` take and give types of the
game's, so they're in no table of the engine's: `analysis.c` has their forms
and docs (`json_calls`, `FROM_BYTES_FORM`), used for completion after `Json.`
and `string.`, hover, signature help and the parameter labels beside
arguments (`CALL_JSON`). `TryParse`, `ToBytes` and `Split` are in the
signature table, so completion after `int.`, `long.`, `float.` and a text's
dot, hover and signature help come from it; they aren't offered in code the
GPU runs. `Json` is offered where a value starts, and `[Json("key")]` before
a field, with its hover. The type a read names is a use of it for hover,
references and rename, inside a `List<...>` too: the walk hands
`type_place` what the call's `T?` holds. `type_argument_open` finds the `<`
of a type that takes types (`Json.Read<List<Score>>(`, whose last `>` is half
of a `>>`), so signature help works there. The shared grammar has `Json`
among the built-in names; its probe has a struct with `[Json]` and each call.
`lsp_json_and_text_primitives` checks all of it and a diagnostic; the
language-server suite is 219 tests and the grammar's 221.
## AnimationCatalog and TryGet completion, 2026-10-11

The owner-approved catalog name and bool/out lookup are reflected in type and
asset completion/hover, catalog methods, signature help and insertion snippets.
`TryGetAt(index, out result)` and `TryGetByName(name, out result)` report bool
results and Animation or Bone outputs. Old method names are no longer offered.
Incomplete out-argument inference recognizes animation catalogs, bone catalogs
and Skeleton, using the receiver type and the existing ordinary out machinery.
Metadata inspection/cache behavior and checked clip members are unchanged.

`lsp_animation_catalog_try_get` covers method signatures/snippets, positional
and reordered named output completion, incomplete result-member inference,
bone outputs and method hover. Existing asset/catalog tests use the renamed
type and methods; source-position assertions account for the longer name.
Historical API spellings below describe the preceding implementation.
Validation: complete native (2,150) and web (2,121) debug CTest suites and the
docs build pass. Final hover wording also passes the focused server suite;
actual editor host rendering is not separately exercised for this API change.

## Delayed locals and switch expressions, 2026-10-11

The shared checker supplies delayed-local diagnostics and switch-arm types.
`analysis.c` walks the selector, labels and results for navigation, hover,
references and selection ranges, keeping the expression's closing brace as
its end. Formatting treats switch-expression braces as expression braces,
keeps `};` together and indents comma-terminated arms without continuation
indentation. The shared TextMate grammar scopes `=>` as a switch-arm operator
for both editors and docs. `lsp_delayed_locals_and_switch_expressions` checks
accepted code, local/enum hover, references, formatting and idempotence.
The grammar suite includes both arrows in one expression and the new e2e
program. All 219 grammar checks and the production docs build pass; complete
native/web LSP validation is recorded in `notes/language.md`. No editor-host
client code changed.

## Enum count and cast guidance, 2026-10-11

Enum-type completion offers `Count()` with its static, compile-time int contract;
the distinct label preserves a declared member named `Count`. Named-phase and
switch-case completion still offer members only. Hover gives the declaration count and
explains that aliases count separately; signature help describes the zero-argument
call. The retained enum-type receiver supports qualified-name navigation and
rename. Both editor integrations receive the same shared language-server output.
Parser diagnostics for `(int)direction` include `int(direction)` as their note.

`lsp_enum_count_and_cast_diagnostic` covers member/method coexistence, hover,
static-only signature help, qualified navigation/rename, cast diagnostics with parser recovery
and valid parenthesized subtraction, indexing and postfix increments. This is
protocol coverage; the full native language-server suite passes after merging
dev's contextual enum support (215 tests).
The full-suite regression also pins member-only switch-case completion. Actual
editor host rendering was not separately exercised.

## Expected enum completion and shorthand, 2026-10-11

Owner-requested assignment completion ranks and preselects the expected enum
type (`ZombieState` after `zombie.state =`), with qualified members next and the
ordinary expression list still available. This does not exclude other valid
enum-valued variables or functions. `completion_enum` uses preceding tokens
and the surviving scope because incomplete expressions can discard their AST.
It handles identifier/member assignment targets, typed declarations/defaults,
returns, comparisons and enum case labels. Leading-dot completion offers only
the expected enum's members in declaration order; switch cases keep unused
members first. Ordinary function/method
arguments and built-in signature arguments are supported when their candidate
parameter types agree, including named arguments. Candidate completion is
conservative: it does not re-run overload resolution on the other arguments,
and more complex assignment targets may lack the token fallback's type.

`walk_expr` skips the implicit type receiver while retaining the member's exact
source occurrence, so hover/navigation/rename/highlighting use the same enum
symbol without inventing a source reference to its type. Formatting spaces
leading dots after assignments, returns and labels while keeping member-access
dots tight. The shared TextMate grammar gives shorthand an enum-value scope;
ordinary member access and leading-point numeric literals keep their scopes.
The Editors page documents completion, and language behavior is recorded in
`notes/language.md` and the spec.

`lsp_enum_assignment_completion_and_shorthand` checks ranking, single
preselection, partial members, incomplete returns/initializers/comparisons/cases,
positional/named/built-in arguments (including List.Add and Texture.Sample),
unused-first case ordering, malformed input, unchanged non-enum completion, valid built-in
checking, member hover/rename, and formatting idempotence. The shared grammar
test covers uppercase/lowercase shorthand and numeric/member regressions.
These are protocol/grammar tests; no interactive editor-host test was added.
Final merged validation: all 2,085 native and 2,057 web debug tests pass,
including the complete tidels suites; docs build and 217 grammar checks pass.

## Sample playback hints, 2026-10-11

`skinned_arguments` now retains the written arguments before appending an
omitted `Playback.Clamp`, using the existing `expr.settled`/`written` contract.
Previously positional and source-ordered named Sample calls exposed the
synthetic integer default to tidels as a literal at the method name, producing
`playback:` before `Sample`. Both Animation.Sample and Pose.Sample now omit
that hint while explicit literals keep their parameter labels and locations.
Reordered named arguments retain their existing source list and evaluation order.
No runtime API, default value or language semantics changed.

`lsp_skinned_sample_hints_only_written_arguments` reproduced the extra playback
labels before the fix and passes afterward. It covers variable/literal seconds,
omitted and explicit default playback, source-ordered and reordered named
arguments, exact hint positions, repeated responses and subsequent Sample hover.
The Editors page documents that parameter labels describe written literals only.
Validation: full Windows debug native and web suites pass 2,079 and 2,050
CTest checks, including native GPU and WebGL/WebGPU coverage, with Node 24.16.
The language-server suite passes all 213 checks; the docs production build passes
with the existing allowed missing-example placeholders. The server is rebuilt
at `build/tools/tidels.exe`; editor rendering was not separately exercised.

## Editor host follow-up, 2026-10-11

Closed the host/process coverage gaps from the review below with repeatable
tests against the real editor applications and SDKs, and fixed the failures
they exposed. Changes remain in the adapters; tidels and language/runtime
contracts are unchanged.

- A real VS Code terminal reproduced starting a replacement before the previous
  three-level process tree exited. Runs now serialize per target and wait for
  both Windows taskkill completion and the original child's close. Terminal
  close before its delayed open prevents a launch; deactivation awaits run
  cleanup as well as server disposal. POSIX Stop uses SIGKILL for the detached
  group so descendants cannot ignore a graceful signal. A five-second deadline
  reports unconfirmed cleanup, including failed tree kills even if the parent
  exits, and prevents starting a replacement. Failure to terminate a process
  remains an OS failure the editor must report, rather than claim success.
- Both adapters recheck the originating run before delayed page opening/loading.
  VS Code also guards its URI/command discovery awaits and last-page/context
  updates, and reports rejected asynchronous browser commands. JetBrains carries
  a live-process predicate through its queued show and tool-window activation.
- Pinned vscode-languageclient 10.1.2 leaves its full-document delivery timer
  scheduled on disposal. Editing then rapidly restarting produced
  `Delivering pending changes failed: TypeError: this.task is not a function`
  in the real host. A small subclass cancels `_pendingChangeDelayer` before
  disposal. This deliberately depends on the pinned client's internal member;
  reconsider it when upgrading that dependency. The next client opens current
  document text. Host tests reject any errors in the Tide client log.
- JetBrains configuration validation and generated names now handle invalid
  path syntax with a configuration error instead of an unchecked exception.
  All Gradle sandboxes receive the shared TextMate bundle, including the test
  sandbox. The ordinary platform tests exercise settings, VFS console links,
  coordinate conversion, cloned run options, validation, game selection and
  all three registered context-menu producers for the actual Tide file type.
- `tools/tide-vscode/test/run-host.mjs` bundles the actual extension and launches
  a supplied editor with a fresh profile/extensions directory and muted browser.
  Its host suite exercises diagnostics and unsaved edits, hover, references,
  rename, a clickable reference lens, rapid server restarts, real terminal
  replacement/close, split-URL embedded page JavaScript, developer tools and
  cleanup after actual extension-host shutdown. A three-level Node CLI stand-in
  supplies observable PIDs; it stays alive during cleanup assertions. Failure
  containment follows the test runner's lifetime, without signalling stale PIDs.
- JetBrains `TideHostTest` runs the real LSP4IJ client with this checkout's tidels,
  tests server stop/restart and hover/lenses, and starts the real run configuration
  and process handler against the same three-level CLI fixture. Its JCEF case
  creates the adapter's tool-window content, activates/loads/reuses its browser,
  executes page JavaScript, and refuses a stopped run's queued address. In a
  headless application, the adapter selects offscreen rendering; the opted-in
  JCEF test enables headless JCEF and passes `--mute-audio`. Desktop rendering
  still uses the ordinary native component. The SDK headless tool window is
  used; physical pointer clicks and desktop focus/rendering are not asserted.

Validation: 24 dependency-free VS Code adapter tests; real minified-extension
host suites on VS Code 1.91.0 (minimum supported) and installed 1.141.0; seven
ordinary JetBrains tests and three opted-in process/LSP4IJ/JCEF tests on the
2024.2.6 platform. Both packages and the docs site build; Plugin Verifier checks
the current plugin against IntelliJ 2024.2.6 and installed CLion 2026.2.3.
The READMEs document the repeatable host
commands and their isolated profiles/sandboxes. User documentation explains
replacement/terminal cleanup. Cross-platform editor-host execution remains CI
or other-machine coverage; these real host checks ran on Windows. Cold model
metadata still uses the existing synchronous, budgeted reader described below;
asynchronous indexing is not a new requirement or implemented feature here.

## Correctness review, 2026-10-11

Reviewed the VS Code adapter's settings/discovery, client lifecycle, lens
arguments, game selection, terminal input, child cleanup and web-page handling;
the JetBrains factory/settings, run producer/configuration, console links and
page handling; and tidels's JSON, framing, document synchronization,
cancellation and navigation/analysis regressions. This is a bounded review,
not a claim that every editor/version or malformed request has been exhausted.

Fixed contracts and evidence:

- Restricted Mode now excludes workspace servers from PATH as well as user
  settings, comparing both lexical and real paths of executables and workspace
  roots. This covers symlinks/junctions and Windows' optional `.exe` spelling.
  Trusted configured/built/installed precedence stays the same. A missing or
  relative LOCALAPPDATA no longer turns the installation fallback into a
  current-directory executable. The README and user Editors page explain the
  PATH rule.
- Both editors wait for whitespace after the complete announced web address;
  previously a stdout chunk ending in the hostname, port or path opened that
  incomplete address. VS Code's Run reader and JetBrains's small
  `TidePageAddress` reader are tested at every split of the announcement;
  the Java reader also receives it one character at a time.
- A disposed VS Code run cannot start from a delayed terminal-open callback or
  act on later stdout page announcements. Disposal releases its event emitters. Repeated Stop
  and terminal-close callbacks send one Windows tree-kill request; taskkill's
  nonzero close now triggers the existing direct-child fallback, as a spawn
  error does. The fallback keeps the original child, runs once and never
  signals one whose exit was already reported. A failed tree kill still cannot
  promise that every descendant exited: the fallback only signals tide itself.
- The game picker deduplicates resolved manifest folders. New adapter tests
  also preserve typed restart/Android answers, serialize rapid language-server
  restarts through completed disposal, recover from a failed start and convert
  clickable reference/schedule lens arguments to VS Code's native types. The
  pinned language-client source confirms its command conversion leaves these
  JSON arguments for our middleware to convert.
- tidels's JSON reader now enforces decimal/fraction/exponent syntax without a
  63-byte truncation, rejects nonfinite results, controls and invalid raw UTF-8,
  and never casts an out-of-range/nonfinite/fractional number to int. String
  scanning stays within the input bounds. Valid surrogate pairs decode together;
  unpaired UTF-16 units become U+FFFD without consuming an unrelated escape.
  Strings and object keys retain embedded NUL bytes when echoed, while C-string
  consumers refuse them and key lookup compares complete counted names.
  Parsing/writing follows [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html)'s number and string rules; finite double
  range remains an implementation limit.
- Shutdown now ignores subsequent notifications and rejects requests with
  InvalidRequest, including a request cancelled in the read-ahead queue; exit
  still ends normally. Positions outside the document avoid signed line-number
  overflow, and UTF-8 stepping cannot pass the document's allocation end.

The original new VS Code fixture reproduced eight failures in ten cases; the
first six JSON fixtures reproduced five failing groups. The final regression
sets are `tools/tide-vscode/test/extension.test.mjs` (15 tests), Java's
`TidePageAddressTest` (2), and `compiler/lsp/tests/json_test.c` (8), plus extended
shutdown/position protocol fixtures. CTest registers the dependency-free Node
adapter suite after `enable_testing`, on native and web hosts; Gradle's `test`
task runs the Java regressions and `buildPlugin` depends on it, including in
manually requested CI builds. VS Code's package also has `npm test`.

Validation: full Windows debug native and web suites passed 2,078 and 2,048
CTest checks, including native GPU and WebGL/WebGPU coverage. The final tidels
suite passed 211/211 normally and under Windows clang AddressSanitizer; its real
stdio/pipe fixtures and the adapter suite pass after the final shutdown change.
Node 24.16 was used for native/web CTest (the machine's default 24.15 predates
the required libuv fix). VSIX packaging, JetBrains `test buildPlugin`, the docs
production build and all 215 shared grammar tests pass.
Plugin Verifier reports compatibility with IntelliJ Community 2024.2.6
(IC-242.26775.15) and installed CLion 2026.2.3 (CL-262.10968.117). Local IDE
paths and `--no-configuration-cache --offline` avoid repeatedly fingerprinting
the entire IDE installation during this check. The 2024.2 TextMate bundle
extension is non-dynamic, so the verifier notes an IDE restart for plugin
enable/disable on that platform; this is an existing platform limitation,
not an API compatibility error. The plugin README's stale every-push CI claim
now correctly describes the manually requested Build workflow.

This first pass used controlled doubles at editor/process boundaries. The
follow-up above supplies actual host, JCEF and process coverage. Neither pass
changed language/runtime design or dispatched CI; tests use isolated editor
profiles and sandboxes rather than the owner's editor configuration.

Media formats (2026-10-10): asset path completion stores a counted suffix array rather than four fixed format fields. Texture/Grid2<Color>/Cubemap offer PNG, PSD, JPG/JPEG, TGA and BMP; Clip offers WAV, Ogg, MP3 and FLAC, including case-insensitive file names and per-format list/map globs. `lsp_asset_path_completion` checks each addition. Build-time import failures come from the same checker helpers as ordinary builds; runtime Decode hovers remain PNG/GLB-only. Unsupported skinned source formats now share export guidance across build/editor while recognized model feature errors retain their reason. The existing OBJ/catalog diagnostic regression also checks DAE. The earlier PNG/PSD-only and four-slot descriptions below describe the previous coverage.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Layout

- `compiler/lsp/`: `tidels`, the Tide language server (owned by Claude). It reuses tidec's front end, with error recovery, to give editors completion, diagnostics, quick fixes (the checker's "did you mean" suggestions among them, as data: `diag_set_suggestion_sink`), hovers, go to definition, type definition and implementation, find usages, highlights, rename, the call hierarchy, formatting (of a range too), parameter hints, inlay hints, code lenses (each system's stage and waits, and what each local handler of a match event hears), the outline, workspace symbols, folding, selection ranges, semantic highlighting, and moving a declaration to a file of its own. It watches the game's files through the editor (dynamic registration), so files changed outside it are seen. It reads ahead of the message it handles (`compiler/lsp/main.c`), since an editor sends a change at every key and a big game takes a while to analyse: changes that arrived together are analysed once, as the last left the text, when a request needs them or nothing more has arrived (`lsp_settle`), and a request that a later message cancels (`$/cancelRequest`) is answered as cancelled without its work (`lsp_handle_ahead`). Nothing there is timed. Native builds only.
  - **A switch on an enum completes by its members** (2026-10-11; the owner reported that `case` offered nothing useful until `ZombieState.` was typed, and that it was "sometimes annoying" even then; what's offered and its order are Claude's picks). After `case`, directly in the switch's braces, completion is the enum's members written as the file writes them (`ZombieState.Run`, by `name_for`) and nothing else: in declaration order by `sortText`, those no case names yet first with the first of them `preselect`ed, the others after with "which a case has" in their detail. Where a statement can start there, each missing case is offered whole (`case ZombieState.Run:`, `sortText` "case NNNN" so they follow the `case` keyword). The colon of a label now counts as a statement's start (`is_label_colon`): before, the line after `case ZombieState.Idle:` offered no `case`, `break` or `if`, so a stacked label or a case's first statement completed to whatever else matched. `?:` and named-argument colons are unchanged. Everything is read from tokens (`switch_of_brace`, `case_written`), as a half-typed `case` fails the whole switch statement in the recovering parser and leaves no AST. `switch_enum` types the value from its names (`names_type`), from the checked switch where it parses (`switch_in`), or from the return type of a game function called as the whole value. A value of any other shape gets its members only where the switch parses, so not after a bare `case`; a switch that isn't on an enum is as before. No trailing ':' after a member. `lsp_switch_cases_complete_enum_members` checks all of it, with the exact items. Not tried in either editor: VS Code's own fuzzy ranking of `Ru` against `ZombieState.Run` is the client's.
  - **Completion follows the entire member chain** (2026-10-08): `complete_members` counts the preceding identifier/dot chain and allocates its exact token-index list. The old 16-index array dropped a long chain's first names, so supported deeply qualified namespaces and nested value fields lost their completion. `lsp_completion_keeps_complete_member_chains` checks a valid 40-part namespace and a value through 40 nested structs, with no diagnostics and the final declaration/field offered.
  - **Completion keeps deeply nested code in its context** (2026-10-08): its context frames are a request-owned heap array sized to the tokens consumed, freed at the shared completion exit. The old 64-frame stack ignored deeper opens but still popped on their closes, so closing 65 of 70 nested blocks made the remaining code look like file-level declarations. `lsp_completion_keeps_deep_block_context` checks a valid function after those closes: its local, parameter and statement keywords complete, declaration keywords don't, and the local still hovers.
  - **Lens builders release their temporary strings after each response** (2026-10-08): `analysis_code_lenses` brackets its read-only builders with `arena_checkpoint`/`arena_rewind` (`common.c/.h`). The checkpoint follows the completed analysis; stage, wait, alongside, world-access, local-listener and override names/titles are copied immediately into the heap-owned `jbuf`, and no post-checkpoint pointer is retained in the analysis. This preserves the declaration and occurrence pointers used by later requests. Before the fix, 200 unchanged requests on 100 valid systems (201 lenses) grew process private memory from 4.4 to 6.5 MB. After it, the same probe stayed at exactly 4,354,048 private bytes at 0, 50, 100, 150 and 200 requests; all 201 lens result objects remained byte-for-byte equal as JSON, and a subsequent Body hover succeeded with 101 find-usages locations. The schedule, local-event listener and virtual/override protocol tests repeat the complete lens response and then check hovers and references from the retained analysis; the memory probe measures the request lifetime separately.
  - **Read-only response builders release their temporary arena values** (2026-10-08): hover, completion, signature help and inlay hints also checkpoint the completed analysis and rewind after their JSON bytes have been copied into the heap-owned output buffer, including null and special mesh-signature exits. Their descriptions, scopes, parameter spans, completion seen sets and asset-path folder entries stay local to the request; completion clears `A.from` before rewinding. These writers never reset or reanalyze the game, and writers that populate retained caches are outside this scope. On valid unchanged documents, 200 requests previously grew private bytes from 3,305,472 to 12,800,000 for a 600-field component hover, 6,488,064 to 30,769,152 for completion among 1,000 constants, 7,540,736 to 13,885,440 for a 600-parameter signature, and 7,524,352 to 17,072,128 for that call's inlay hints; every result remained identical. Protocol regressions repeat the complete hover, deep local/member and asset-path completion, long positional/named signature and inlay responses, then check retained hovers and exact reference locations/counts. Repeated-request memory is measured separately from these output/lifetime checks.
    - With those scopes in the final binary, the same 200-request probes plateaued: hover stayed at 3,301,376 private bytes at 0/50/100/150/200 requests; completion went from 6,479,872 to 6,500,352 by request 50 and stayed there; signature help went from 6,479,872 to 6,533,120 and stayed there; inlay hints went from 6,475,776 to 6,529,024 and stayed there. The respective 11,443-, 61,604-, 18,936- and 52,549-byte JSON results remained identical on every repetition. Each probe then successfully hovered its retained declaration and returned exactly its declaration and one use as reference locations.
  - **The remaining temporary response builders use the same lifetime** (2026-10-09): document symbols, semantic tokens, folding and selection ranges, all three call-hierarchy writers, code actions and formatting also rewind their local vectors, token/line arrays and string builders after copying the result. The hierarchy's null target and formatter's lexical-error literal return before allocating temporaries; other paths have one scoped exit. Code actions read the analysis's existing fixes and missing-asset candidates and reset their temporary namespace lookup position before rewinding. Allocation-free navigation and writers with retained cache population stay outside these scopes. All nine endpoints reproduced growth on unchanged accepted programs over 200 identical requests, with retained hover/reference checks afterward. Representative baselines: 1,000 constants' semantic tokens grew private bytes from 6,471,680 to 23,351,296; 600 fields' outline from 3,305,472 to 10,706,944; folding 1,000 constants from 6,471,680 to 11,743,232; and formatting those already formatted constants from 6,475,776 to 92,983,296, even though folding and formatting returned empty arrays. Existing protocol fixtures now repeat those complete outputs, hierarchy items/call ranges, nested selection parents and actionable quick fixes, then verify retained declaration hovers and reference counts. Formatting's repeated edits are still applied and checked against the expected document.
    - All nine final-binary probes preserve every JSON result and their later retained-declaration hover/reference checks. Private bytes at 50/100/150/200 requests plateaued at 6,488,064 for semantic tokens, 3,321,856 for the outline, 6,463,488 for folding (also the initial value), 2,416,640 for one deeply nested selection, 6,488,064 for hierarchy preparation, 3,309,568 for incoming calls, 3,317,760 for outgoing calls, 3,301,376 for quick fixes and 6,492,160 for formatting. The empty-output writers now release their token-sized scratch arrays as well.
  - **Rename previews release name-check scratch values** (2026-10-09): `analysis_rename` checkpoints after its allocation-free target check, and rewinds both successful edits and rejected new names. `check_new_name`'s token list, namespace builder and local/out-local search vectors are temporary; every returned error is a static literal or its static conflict-message buffer, so error ownership stays unchanged. The name is restricted to identifier characters before lexing it; no lexer diagnostics, AST mutation or cache publication occur during validation. Repeating a parameter rename in a valid function with 100 used out locals grew private bytes from 3,284,992 to 4,337,664 over 200 requests before this scope. Ordinary-field and out-local protocol fixtures repeat successful edits, the refusal fixture repeats conflict/keyword/builtin/invalid-identifier errors, and follow-up hovers and references still describe the original document before edits are applied.
    - The rebuilt rename probe stayed at exactly 3,284,992 private bytes at 0/50/100/150/200 requests. Every 318-byte rename result remained identical; the original Read declaration still hovered afterward and its references still returned that declaration. The preview never applies edits to the source.
  - **Long signatures keep every parameter** (2026-10-08): `label_split` in `analysis.c` returns growable spans instead of silently keeping only the first 16. Signature help's parameter metadata and named-argument selection use all those spans; inlay hints size their parameter names to the checked call's arguments. The same label parser still handles defaults, nested types and the built-ins. `lsp_many_function_parameters` checks a valid 40-parameter function beside a short overload, its last positional argument and the last parameter passed first by name, and all 40 literal argument hints.
  - **Workspace symbols search every compilation context** (2026-10-08): `search_game` in `server.c` uses the same owned context keys as asset refactoring (`first_game_context`): a folder game by its folder, a manifest's game by its ordered source paths and package names. A consumer isn't skipped merely because its files were already searched for another consumer; a shared package's different active `#if` branches contribute their symbols. `analysis_workspace_symbols` serializes each item through a callback; the server copies it before the next arena reset, sorts pointers to discard exact duplicates and retains the original traversal order. Names at different locations or in different containers stay distinct. `lsp_workspace_symbols_of_shared_package_contexts` checks both workspace root orders, with and without an open package document, both `PACKAGE_EXTRA` branches and shared type/method symbols returned once.
  - **Closing a document refreshes its consumers** (2026-10-08): `unsettle_consumers` in `server.c` queues the remaining open documents whose game's source list includes the closing file, before its unsaved buffer is removed. Their diagnostics then use disk contents again. The existing close barrier still settles edits that arrived before it; the dependent refresh uses `defer` and `lsp_settle` as changes do. The closed URI stays in that pending batch as a marker, so settlement doesn't revive its diagnostics under the server's disk URI spelling; another open URI for the same file still gets diagnostics. Duplicate or unknown closes clear that URI without invalidating other games. `lsp_close_refreshes_dependent_diagnostics` checks discarded edits, immediate/deferred refresh, an unrelated workspace game, repeated opens/closes, encoded URI spelling, another open URI and errors in the closed file's disk contents.
  - **Code lenses are several to a line, and the ones that name a declaration go to it** (2026-10-07; the owner asked for reference counts as C#'s editors show them, and for the names in a system's line to be clickable, kept short; which declarations get a count and what a click does are Claude's picks). `analysis_code_lenses` in `compiler/lsp/analysis.c` writes, above a system, a lens for its stage (with what it reaches through its `World`), one for each system it waits for and one for what runs alongside it, and `write_reference_lenses` an "N references" lens above each declaration of the document, from the occurrences find-usages goes by (`same_symbol`). A system, view or handler, the input, a scene, an operator and an `Interpolate` say nothing at 0, as the engine runs those itself. A lens that goes somewhere names one of VS Code's own commands, `editor.action.goToLocations` or `editor.action.showReferences`, with a document, a position and locations as JSON: LSP4IJ implements both by those names, and the VS Code extension's middleware makes the arguments VS Code's types (`lensCommand` in `tools/tide-vscode/src/extension.js`). Counted for every declaration of the open file at each request, with no `codeLens/resolve`. `write_reference_lenses` now groups declaration-backed uses by occurrence kind and canonical declaration, sorts each group in its existing source order and binary-searches the group for each lens; `same_symbol` remains the final equivalence check, so overloads and virtual/override identities retain their counts. This replaces a whole-game occurrence scan per declaration. `lsp_reference_lenses_keep_symbol_groups_and_order` compares each nonzero lens's locations against find-usages on overloaded functions/methods, split namespace blocks and 1200 constants; `lsp_overrides` makes that comparison for virtual declarations and their overrides too. Measured on Windows with the native debug server over real pipes, five warm requests per fixture: 1000/2000/4000/8000 valid unused constants went from median 6.37/20.15/74.47/295.23 ms to 3.13/5.25/11.19/21.84 ms, including transport and JSON parsing, with the same lens counts. Reference-count titles use a bounded stack buffer: their former arena builder kept 64 bytes per declaration at every request until the next analysis, so 200 requests on the unchanged 8000-constant fixture grew process private memory from 37.2 to 140.6 MB. After the bounded-buffer fix, the same fixture stayed at 36.1 MB at 0, 50, 100, 150 and 200 requests. Existing lens regressions preserve title/count formatting. `lsp_schedule_lenses_and_fixes` checks them; the clicks weren't tried in either editor.
  - **Assets, the editor side** (built 2026-10-06, from `docs/spec.md`, Assets: the Decided rule that there are no GUIDs and no meta files, and the Provisional bullet Finding a moved file). A site is a `DECL_ASSET` whose path is a string literal (`collect_assets` in `compiler/lsp/analysis.c`; a `const string` path has none): the checker reads the files and reports at the path (`asset_files.h`), and the server adds what an editor does with that. What's built, and what code relies on:
    - **A rename in the editor rewrites the paths** (`workspace/willRenameFiles`, `will_rename_files` in `server.c`, `analysis_rename_files`): every `asset` path to the file or folder renamed, in every file of the game and its packages, each relative to its own declaring file, which may move too; a glob is never rewritten. The edit is answered before the rename, so it's at the files' old URIs. `workspace/didRenameFiles` looks at the game again. Which renames the editor asks about: an editor that takes registrations (VS Code's client declares `fileOperations.dynamicRegistration`) gets filters for every file an asset names and the folders above it up to the game's or package's folder, registered after each analysis when the set changed (`register_assets_of`, ids `tide-rename` and `tide-renamed`; globs as `[*]`, so a path means itself); one that takes none (LSP4IJ reads only the initialize result) gets `**/*` there and is asked about every rename. The assets' files are watched too (id `tide-assets`), so a moved file a glob stops matching gets its diagnostic.
    - **Asset notifications override metadata caches** (2026-10-08): watched changes, creations/deletions and completed renames call `analysis_file_changed` before refreshing diagnostics. It marks affected cached asset reads and candidate hashes, including files under a renamed folder and meshes read with an external buffer. The next analysis rereads them even if size and modification time match; other files keep their cached decodes. Invalidation leaves the previous analysis's borrowed values valid until it is replaced. `lsp_asset_notifications_override_unchanged_metadata` checks changed UTF-8 text, a glTF buffer, deletion/recreation and a rename over an already cached destination, with fixed file times.
    - **Registrations cover all open games** (2026-10-09): `register_assets_of` keeps heap-owned path snapshots keyed by the same folder or ordered manifest source/package identity as workspace symbols and asset refactoring. Watchers and rename filters are sorted, deduplicated unions, so analyzing another game no longer unregisters the first game's assets. Active contexts are rediscovered once per `server.changes` generation; a folder's own open source files share one source-tree scan, respecting a more specific open workspace root. Manifests are read once for that generation, and their first matching file/folder entry takes precedence even when an explicit `tide.packages` folder game also includes the document. Shared package documents keep ordinary first-consumer selection. Workspace/refactoring traversal consumes heap copies of these grouped contexts, rather than resolving and rescanning the same game for every open document. The retained analysis also has a copied context key: cached requests, deferred settlement and watch refresh reuse a document's analysis only when that selected context matches, so overlapping manifest/folder contexts keep their own `PACKAGE_` branches. Diagnostics for an open document are published only by that selected context; other files and close markers keep their existing publication rules. Closing documents, removing assets or changing the workspace prunes inactive snapshots, and the last close unregisters the union. Temporary workspace/refactoring analyses never add a closed game's registrations. `lsp_asset_registrations_cover_every_open_game` applies actual capability messages as a client does, checks both root/open orders, shared-package deduplication, retained hover/lens/workspace/rename requests, a watched diagnostic refresh, source/close/root removal and clients without relative glob support. `lsp_asset_registrations_keep_manifest_and_folder_contexts` checks explicit-file and folder manifest subsets beside an explicit folder game, both open orders and immediate/coalesced opens, branch-specific hover and workspace symbols, a valid manifest branch retaining empty diagnostics when the folder branch has an error, and retained subset watches after the nonconsumer closes. `lsp_open_contexts_keep_innermost_workspace_root` checks both nested-root/open orders, isolation from a parent-only name and pruning parent watches. On Windows with the native debug server over real pipes, seven warm workspace-symbol requests on 100 valid open constant files went from a median 1,032.37 ms before grouping to 19.72 ms on the final binary, including transport and JSON parsing. The complete 100-symbol results had the same SHA-256 across every repetition and before/after; the probe retained 100 opening diagnostic publications and two dynamic asset/rename capability registrations. The final mixed-context probes also keep all three asset watches, the manifest branch's Mode=22 hover and empty diagnostics in sequential and coalesced orders; closing the other context leaves just its own asset watch.
    - **A moved file is found by its bytes.** `compiler/src/assets.c` (tidec's library) is the path arithmetic (`asset_path_join`, `asset_path_relative`, Windows ignoring case) and the store: `<game>/.tide/assets.sha256`, a line a file (its SHA-256 as hex, its size, its path from the game's folder), making `.tide/` as tide does; `compiler/src/sha256.c` moved there from `cli/`. The checker writes it once a game's assets are checked (`asset_files_save_hashes`, from `check()`), where its text changed, when it knows the game's folder (`program.game_folder`: tide's `--game`, and tidels's for a game that's a folder, since a manifest's has no `.tide/`), keeping what it knew of a file that's missing. The checker's error for a missing file (`say_missing` in `asset_files.c`) names a file of the game's folder with the same bytes, or lists the files of its name; the server computes the same candidates (`asset_missing`, hashing a file again only when its size or time changed: `file_hash`) and offers them as quick fixes on the checker's diagnostic (`asset_diagnostic` finds it at the path's token; `asset_fix_actions`), the by-bytes one preferred. Hidden folders and the root's `build/` are never looked in (`folder_find_files`).
    - **Completion inside an asset's path** (`complete_asset_path`): the folders and files where the path so far leads, from the declaring file, as items with a `textEdit` over the part being typed; `../` where there's a folder above; the type's files only (`.png` and `.psd` for a `Grid2<Color>`, `Texture` or `Cubemap`, ignoring extension case; `string` and `List<byte>` take any, `.tide` files never); and `*` and `**/*` for each accepted format for a `List`, `Map` or `Cubemap`. PSD suggestions and globs for every accepted format were added 2026-10-08, with `lsp_asset_path_completion` checking both image extensions and a list, map, single texture and cube path; hovers remain about the decoded image rather than its source format. Invalid PSD diagnostics come from the same checker as builds. Runtime `grid.Decode` remains PNG-only.
    - **Mesh assets** (2026-10-08, FBX added 2026-10-10; see `notes/assets-and-files.md`): a `Mesh`'s path completes `.glb`, `.gltf`, `.fbx` and `.obj` files (`asset_site.format` through `format4`), and globs of each format. Its hover says what the file has (vertices, triangles, and whether it has normals, uvs, colors and tangents: `mesh_file`'s flags, from the value the checker resolved), `Mesh<...>`'s own hover is the type's, and the struct in its `<>` is a type reference as a list's element is (`type_tree`). After a Mesh's dot, completion offers `vertices` and `indices` (none in a frag or a vert), whose types `member_type` finds among the program's lists (`list_type`). A mesh draw given a Mesh has a form of its own in signature help (`MESH_WHOLE`: `Draw.Mesh(mesh, transform...)`, `Draw.Instanced(mesh, instances...)`), told by its first argument naming one, and `call_param_names` calls the argument `mesh` (the checker's `expr.mesh_part`). The grammar scopes `Mesh<` as it scopes `List<`, and never `Mesh` alone, which is a game's own name as often as not. `lsp_asset_meshes` checks them, and the checker's diagnostics for a file that's no mesh.
    - **Batch renames** (2026-10-08): `will_rename_files` allocates its paths from the request's file count, so every rename is passed to `analysis_rename_files`; the former fixed array silently left out files after the first 64. Each game is analyzed once, identified by its folder or its ordered source paths and package names for a manifest's game. Each asset edit is serialized and copied through `collect_rename_edit` before the next analysis resets the arena, as are the visited game identities. Edits are merged by document path and identical range/replacement pairs are kept once; a shared package's different active `#if` branches in different games contribute their edits too. Paths are computed against the whole batch. `lsp_asset_rename_large_batch` sends 70 renames together, applies the returned edits and checks every asset's new path. `lsp_asset_rename_multiple_games` checks two games and their shared package in both request orders, including a folder move, paths from the package into each game and assets behind `PACKAGE_EXTRA`'s two branches.
    - **Asset ownership during refactoring** (2026-10-08): `visit_workspace_games`, shared with workspace symbols, discovers every manifest game in the open folders, folder games and open documents' games. `will_rename_files` checks these games' asset declarations before its usual renamed-path lookup for other games. A shared-package-only move therefore edits every consumer's paths; an asset of a game whose CMake manifest lists individual `SOURCES` is found through the game's sources, even though the asset isn't one of those entries. Known games are checked because asset paths may lead outside their own folders. Empty synthetic-file fallback analyses are skipped. Ordinary document analysis still uses `game_of` and selects one consumer for a package's file. `lsp_asset_rename_multiple_games` also checks a shared-package-only rename in both workspace-root orders, with one shared edit; `lsp_asset_rename_manifest_games` checks explicit-file and folder manifest entries, a non-source asset rename and a listed source file's own relative asset paths when it moves.
    - Tests: `lsp_asset_rename_rewrites_paths`, `lsp_asset_rename_large_batch`, `lsp_asset_rename_multiple_games`, `lsp_asset_rename_manifest_games`, `lsp_asset_moved_file_is_found_by_its_bytes`, `lsp_asset_path_completion`, `lsp_game_without_assets_changes_nothing` and `lsp_asset_paths` (`compiler/lsp/tests/lsp_test.c`), on games written to the test's folder.
- `tools/`: editor support. `tide-vscode` is the VS Code extension and `tide-jetbrains` the JetBrains plugin: each is the grammar, a client that runs `tidels`, and running the game with `tide run`, natively, on the web or on Android. `tide-syntax` is the TextMate grammar they both include, which the package also ships as a bundle for other editors. An editor feature goes to every integration that can have it, each in its own editor's way. File renames reach `tidels` from both without code of theirs: `vscode-languageclient` (10.1.2) registers the file-operation features itself, declaring `fileOperations` with dynamic registration and matching the server's filters with minimatch against the path (case-insensitive where the filter says); LSP4IJ (0.21.0) sends `workspace/willRenameFiles` by the server's static capability alone (`FileOperationsManager`), which is why tidels announces it in the initialize result for a client with no dynamic registration.

## Angles and Quaternion Euler overloads

`quaternion.Euler` (2026-10-10) exposes both `float3 radians` and scalar `float x, float y, float z` signatures from the shared built-in declarations. Both use radians and the same Z-X-Y rotation order. `lsp_quaternion_euler_signature_help` checks both labels, selection of the vector and scalar forms, the third positional parameter and reordered named `x`, `y` and `z` arguments. The focused native debug test passed.

Degree literals (2026-10-10) use the shared grammar's float scope across VS Code, JetBrains and docs highlighting: `180deg`, `.5deg`, `1e2deg`, underscore digits and a separate unary minus. Only lowercase `deg` is recognized, with word boundaries and leading-dot context kept distinct from member access; strings, comments and identifiers retain their scopes. The grammar suite checks complete literal tokens and ordinary float regressions. Its e2e walker now skips directories named `.tide`, which a compiler run can create as a cache. `lsp_degree_literals_keep_source_spelling` checks accepted source, constant hover, formatter preservation and idempotence, and the scalar Euler signature with a degree argument.

## Skinned meshes and animation

Built 2026-10-08, after the owner asked for the IDE to respond properly without lag. The language rules are in `docs/spec.md`, **Skinned meshes and animation**; the full importer is described in `notes/assets-and-files.md`.

- **The checker supplies the catalogs.** `analysis_run` sets `program.asset_metadata_only`, and `skinned_catalogs.inc` supplies the same generated catalog declarations that ordinary compilation checks. Completion and hover use `decl.skin_catalog`, including cleaned inferred names, collision aliases and `[Name("source")]` contract aliases. A known asset copied with `var` retains its catalog; a plain `SkinnedMesh<V>` parameter offers the dynamic API, and a `SkinnedMesh<V, Contract>` parameter offers its declared members. The editor does not infer a parameter's names from one call site. An inferred or explicitly aliased member's hover names the original source spelling; an unaliased contract member's hover identifies the required normalized source identifier.
- **Interactive inspection skips model content.** `compiler/src/model_metadata.c` reads a `.gltf` JSON file or a `.glb` header and JSON chunk, extracting original animation, node and skin names, node parent indices and skin joints. It validates JSON, UTF-8, the glTF version and hierarchy without reading geometry, animation samples, side buffers or the binary chunk. FBX (2026-10-10) uses ufbx with geometry, animation curves, skin vertex weights and embedded data ignored, external reads disabled, and the full importer's helper-node, axis, unit and node-depth options. Node and animation-stack order, source names and the unnamed FBX root/helpers agree with the full importer; the canonical runtime decoder subsequently adds its own identity root. `SkinnedMesh<V>` and standalone `Animations` metadata analysis avoids full import, animation baking and full model hashing. An existing URL cache is inspected by its URL SHA-256 path, retaining the original URL as its source-format hint; no request fetches a URL or creates its cache. Ordinary static `Mesh<V>` and other asset kinds retain their existing full-import editor path.
- **The cache survives source analyses.** Local metadata and its READY, UNAVAILABLE or INVALID status are cached by path, file presence, size and modification time. Watched `.gltf`, `.glb`, case-insensitive `.fbx` and `.tide/assets/urls` changes explicitly invalidate the path even when size and time are unchanged; renames invalidate the cache, and saves invalidate it for editors without file watchers. Contract and source changes rebuild checked catalog types from cached raw names. Missing and malformed imports are cached too. `model_metadata_forget` releases this process-owned memory; cache results are borrowed until their path is reloaded.
- **Unavailable metadata is distinct from an absent member.** Invalid model catalog data gives an error at the import path; unavailable metadata gives a standalone informational diagnostic there (`DIAG_INFO`, LSP severity 3). Ordinary missing-file diagnostics still apply. Unknown dot access explains that its catalog cannot be checked, rather than saying the name is known to be absent. `Find` remains usable and a declared contract still supplies its members. Only a successfully inspected catalog yields missing-member diagnostics and suggestions. Full builds remain authoritative for geometry, animation data and contracts.
- **OBJ skinned imports explain the required format** (2026-10-10): a failed metadata inspection of a `.obj` source gives the same import-path error as a full build: `a SkinnedMesh asset requires glTF 2.0 (.gltf or .glb) or FBX (.fbx), not OBJ`, with a note suggesting `Mesh<V>` and `Draw.Mesh` for a static OBJ or a rigged glTF 2.0 or FBX export. The source suffix is case-insensitive and comes from the original URL path before its query or fragment, including when the cached file has an extensionless hash name. Successful glTF/GLB content keeps its existing suffix-independent behavior; malformed `.gltf` JSON keeps its parser error. An uncached OBJ URL still gives unavailable metadata without fetching. `lsp_skinned_obj_diagnostics_explain_the_supported_format` pins the local and cached URL diagnostics, complete path-token ranges, static-mesh guidance, accepted renamed glTF content and uncached URL status/read count; `lsp_skinned_unavailable_catalogs_are_not_missing_members` also pins the malformed glTF JSON message.
- **The ordinary APIs are visible in the editor.** Skeleton and catalog members, `Find`, `At`, `count`, `Animation.name`/`duration`, `RestPose`, `Sample`, `Blend`, `Model`, `BoneTransform` fields and `Playback` have completion and hover. Signature help includes optional Playback parameters, static `SkinnedMesh<V, Contract>.Decode(bytes)` and `Draw.SkinnedMesh(mesh, transform[, pose], ...)`, with shader completion after its two required arguments. The optional Pose is classified by its type, so a texture or shader immediately after the transform keeps its own argument documentation and inlay name. Hidden backing lists and indices are excluded. Skinned model and animation asset paths complete `.glb`, `.gltf` and `.fbx` rather than `.obj`; generic vertex and contract arguments are indexed as type references. CPU catalog APIs are not offered on the GPU, while BoneTransform's numeric fields are. The shared syntax grammar recognizes generic `SkinnedMesh<V, Contract>`; its probe and all 202 grammar tests passed after merging the current `dev`.
- **Rest poses and separate animation assets** (2026-10-10): a model's `defaultPose` completes and hovers as an owned `Pose`, including models with no clips. `asset Animations` uses the same content-skipping metadata cache and checked catalog members directly on the asset; `var` retains its names, and plain `Animations` parameters expose `count`, `Find` and `At`. Asset hover explains importing clips without typed render geometry, path completion accepts glTF/GLB/FBX, and the type is available in CPU completion. `lsp_animation_assets_use_cached_catalogs_and_refresh_names` checks inferred completion and original-name hover, missing/invalid/uncached metadata, no side-buffer reads or repeated parses, same-size/time watched replacement and GPU exclusion. `lsp_skinned_static_decode_and_draw_forms` checks rest-pose completion/hover, drawing with no pose, optional-pose signature help and shaders offered after two arguments. User documentation on the animation, 3D and assets pages explains no-clip drawing, separate catalogs and matching-rig binding.
- **Tests and measurements.** `tidec_model_metadata_tests` checks escaped keys and UTF-8, truncated and spoiled GLBs, invalid JSON/hierarchies, failed-read status, explicit invalidation and existing URL caches. A sparse 128 MiB GLB read only 380 bytes of header and JSON: 4.1 ms cold, 16.7 microseconds per warm lookup over 1,000 lookups on this Windows debug build. These are a synthetic metadata measurement, not a whole-editor latency promise. `lsp_skinned_catalogs_use_cached_json_and_preserve_inference` verifies inferred and contracted completion/hover, aliases/collisions, warm source analyses without reads/parses, ignored external buffers and same-size/time watcher invalidation. `lsp_skinned_unavailable_catalogs_are_not_missing_members` checks path diagnostics, malformed/deleted files, uncached URL status, dynamic lookup and retained contract completion. `lsp_skinned_static_decode_and_draw_forms` checks static versus instance completion and drawing signatures. The full language-server suite passed with 188 tests.
- **Limits.** A cold read and parse remain synchronous, bounded to 16 MiB of glTF JSON or 64 MiB of FBX source. FBX additionally limits each of ufbx's temporary and result allocators to 128 MiB; exhausting that inspection budget is UNAVAILABLE rather than malformed data. Larger models can still be built with the full importer's limits. No asynchronous model indexing, pending-read protocol or request interruption during that cold parse is built. GLB binary chunk size alone does not affect metadata work; FBX inspection must read source bytes even though it skips their geometry and animation arrays. Interactive collection imports use their common type or declared catalog rather than inspecting every glob match for per-item inferred names; an `Animations` collection exposes dynamic lookup and enumeration on its elements.
- **FBX regressions** (2026-10-10): the authored `fbx_metadata_fixture.h` checks source-order animation names and node parents without geometry or curve data. Metadata cache tests cover uppercase source suffixes, query URLs whose cache filenames are hashes, extensionless ASCII FBX behind leading comments, warm reuse, explicit invalidation and refusal of a 64 MiB-plus-one source before parsing. Format sniffing reads at most 4 KiB for unknown non-GLB content; GLB retains its header/JSON-only read. `lsp_skinned_fbx_catalogs_complete_names_paths_and_refresh` checks inferred animation/bone completion, standalone `Animations` catalogs sharing the same metadata cache, asset hover, accepted file/glob formats, case-insensitive watches and same-size/time watcher-driven rereads; `lsp_asset_meshes` now includes FBX and preserves OBJ globs. Raw FBX versus converted GLB metadata parity in `tidec_fbx_files` compares catalog count/order/names and hierarchy across synthetic and real exporter fixtures, so content-skipping options cannot silently renumber checked members. FBX metadata load options must change together with the full importer when helper generation or source ordering changes.
  - Validation: all 10 metadata tests and all 197 language-server tests passed on Windows, including the full native suite's stdio/pipe checks. The metadata suite also passed under Linux ASan/UBSan. The docs production build passed after the FBX import and editor pages were updated.

- **Writable animation names and file-list completion** (2026-10-11): the shared checker exposes exporter names such as `Root|Run` as `rootRun`, retaining original names in hover. Combined `Animations` file lists/globs inspect every source through the existing content-skipping metadata cache, prefix colliding aliases with filenames, and offer no partially inferred members if any source metadata is invalid or unavailable. Completion and hover report source filenames. Every literal list path has its own asset site for completion and file-rename edits; singular Animation paths offer model formats without globs, and `[Name]` completion/hover explains exact clip selection. `lsp_animation_file_lists_clean_names_select_clips_and_refresh_all_sources` covers two-file catalogs, cleanup/collisions, shared warm metadata, both path kinds, member/asset hover, rewriting both list and singular references on file rename, same-size/time second-source refresh and no partial catalog after a malformed source. Existing skinned/catalog tests also pin cleaned keywords and ordinal collisions. Native and web editor suite results are recorded with asset validation.

## Singleton methods and view times (2026-10-11)

Editor semantic analysis recognizes singleton methods for dot/field-scope completion and hover, using the compiler's readonly diagnostics. ViewTime and the reserved enabled-input viewTime field come from the analyzed compiler program; hover describes confirmed interpolation instead of exact/unblended verified views. `lsp_singleton_methods_and_presented_view_time` pins method completion/hover, readonly singleton errors and viewTime.valid completion.
