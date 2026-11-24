## Graphs in the IDEs (2026-11-24)

The `.tidegraph` editor for VS Code and JetBrains is `tools/tide-graph/`, one page both host: see
notes/visual-scripting.md, "The IDE editor".

## Generics in editors (2026-11-24)

The language's side is in notes/language.md (Generics closed, top section). tidels (`compiler/lsp/analysis.c`):

- **One name across instantiations.** Each instantiation reads the generic's code again, so its names are its own declarations at the generic's locations. `written_generic` maps an instantiation, a method of a generic struct's instantiation (by its index in `methods` or `generic_methods`) and a type parameter's binding (`generic_of`, `type_param_index`) to the generic's own declaration, or its `param_decls` alias; `same_symbol` compares through it, and fields, parameters and locals declared at the same location (`declared_together`), since an instantiation whose `has` dropped a branch hasn't got that branch's names. Occurrences keep the instantiation they were found in, so hovers know the bound types.
- **The walk.** A `DECL_GENERIC` or `DECL_GENERIC_STRUCT` adds its name, its type parameters (declarations, of `param_decls`) and the parameters its `where` names (`walk_generic`); its own unchecked tree isn't walked. Instantiations are walked as the declarations they are, and probes (`decl.probe`, `list_probes` into `A.probes`) after the written declarations, so the inside of a generic nothing instantiates has hovers, definitions and renames. `type_tree` gives `Pair<int>` its instantiation and each `<>` argument its bound type; a `Pair<int> { ... }` literal goes through its `type_arg_ref`.
- **Hover.** `format_data_decl` writes a generic struct with `<T>` and `where` (`format_type_params`, `format_where`), an instantiation's fields with their bound types, and generic methods; `format_routine` writes a generic with its parameters and `where`, and an instantiation or a method of one with its types bound (`format_bound`), `int Max<int>(int a, int b)`. `describe_type_param` says what a type parameter is of, its rules and up to four types the program instantiates it with.
- **Outline.** Document and workspace symbols leave instantiations out (`generic_of`): a generic struct is one struct symbol, with its generic methods among its children; an instantiation's declaration occurrence is as long as the generic's name, not `Pair<int>`.
- **Completion and formatting.** `has_name` is false for an instantiation, so none is offered by itself; a generic struct completes as `Pair<${1:T}>` (`generic_snippet`), a generic function as a call. `generic_type` counts the game's generics (`game_generic`), so the formatter keeps their `<>` tight, and `(` after a generic's `>` too.
- **Grammar** (`tools/tide-syntax`, copied to `tools/tide-jetbrains/textmate`): type parameters after a struct's or a function's name are `entity.name.type.parameter`; `where` is `keyword.other.where` with `support.type.constraint` rules (`#where`); a capitalized name with types in `<>` not before a `(` is a type (`entity.name.type`), and `Name<types>(` a call; a method's return type can have several types in its `<>`, `Map<K, V> Build(`.
- Tests: `lsp_generics` (edits counted, so none is said twice or overlaps; the outline), and the grammar test "generics scope their type parameters, where rules and instantiations as types". Vim and Emacs color every capitalized name as a type, type parameters with them, and now have `where` among their keywords (`tide-vim/syntax/tide.vim`, `tide-lang-mode.el`); neither editor's tests were run for it.

## Commands as one declaration (2026-11-21)

The language's change is in notes/language.md (top section). The language server and the grammars follow it.

- **One symbol.** A command is its type's occurrence (`OCC_TYPE` on the `DECL_EVENT`); its code's name, an override's
  name and its calls map onto it. Document and workspace symbols list it once, detail "command", kind Event, with its
  values as children.
- **Navigation and rename** cover the name at its declaration, at calls (`Rename(...)`, `entity.Rename(...)`) and in
  an override, and the values at the declaration, in the code, as named arguments and in an override's first
  parentheses. A command's new name is checked against types and systems; a value's against the code's parameters
  and locals, and theirs against the values. Unity's C# rename is unchanged: the generated type has the command's name.
- **Hover** on the name shows the header as written, `command(float amount, int times = 1) Bump(Owner owner, mut
  Body body, PlayerID sender)`, then how it's called; on a value, that it's what the command is called with.
- **Completion:** value types in the first parentheses (constants after `=`, field attributes in `[ ]`); a handler's
  parameters in the second, with `PlayerID sender` until one is there; after `entity.`, the commands whose code runs
  for an entity, in place of `Send(command)`; commands as callables in expressions; named arguments; one `command`
  snippet and overrides' in the new form. `tide_payload` is never offered. Commands the file can't name unqualified
  aren't offered as calls, and none inside `[Before]`/`[After]`, as only handlers of one event are ordered.
- **Signature help and inlay hints** work at command calls. **Semantic tokens:** the name a function, its values
  read-only parameters. **Entry points:** a command's code is a root, and a call of it is not a call edge.
- **Formatter:** a name on the line under `command(...)` stays indented one level, stable when formatted twice; the
  fold starts at the `command` line.
- **Grammar** (`tools/tide-syntax`, and its JetBrains copy, byte-identical again: it had lacked the `prepare` rule):
  one begin/end rule for a command, scope names unchanged; `probe.tide` and `grammar.test.mjs` cover the one-line and
  two-line forms, attributes on values, overrides, `dev` and calls.
- Tests: `tidels_tests` 289 (`lsp_commands` rewritten, `lsp_command_override` new), grammar tests 428.
- **Gap:** a call written with its namespace, `Shop.Buy(1)`, compiles (the compiler keeps the namespace in
  `command_namespace`), and the server doesn't follow it there yet: hover, definition and rename see the unqualified
  calls. Earlier sections that say "event/command handlers" and "command submissions" (Entry-point navigation,
  2026-10-14; Shared language-server updates, 2026-10-18) mean commands and their calls now.

## .tweaks files in editors (2026-11-16, later)

Replaces the tweaks.json handling below. The owner: "ide support is important".
- **A `.tweaks` file is a file of the game's analysis, though not its code.** `game_tweaks` (server.c) finds them as
  tidec does (deep with a game folder, else the first file's folder alone) and `run_game` adds them to the analysed
  files; analysis.c lexes them (no parse) and hands their text to `prog->given_tweaks`, which `tweaks_read` takes
  instead of reading the disk. So locations, references and diagnostics are ordinary ones: each entry's name is an
  `OCC_CONST` occurrence of its tweak (offset past a namespace prefix), and its value's names are walked
  (`walk_expr(d->code_value)`).
- **Server**: `tweaks_document` (a URI ending `.tweaks`); `select_tweaks` picks the analysis that has the file (the
  current one, a `.tide` of its folder open in the editor, else the last game). Opening or changing one bumps
  `changes` and publishes, exempt from the open-context check, so a wrong value is said as it's typed. Formatting
  answers null (no formatter for the file).
- **Features**: lens over an overridden tweak (`combat.tweaks: <value>`, to its name in the file); hovers both ways;
  definition from the file to the declaration; rename of a tweak edits its `.tweaks` entries; completion in the file:
  tweak names at a line's start (snippet `Name = $0;`), expressions after `=`.
- **Editors**: VS Code's tide language and both TextMate packages take `.tweaks`; JetBrains maps `*.tweaks` to tidels
  as Tide (`fileNamePatternMapping`). `analysis_tweak` went.
- Tests: `tidels_tests lsp_tweaks_file` (lens and place, hover, diagnostics on open, definition, hover in the file,
  null formatting, completion at a line's start and after `=`, rename reaching the file, an error as typed);
  VS Code `npm test` (36) and JetBrains `gradlew test buildPlugin` pass (JDK: CLion's `jbr`).

## Tweaks in editors (2026-11-16)

The owner asked for the editor to show that tweaks.json overrides a tweak and to go between them.
- **Analysis** (`analysis_run`): `tweaks_read` after parsing, from the game's folder or the first file's, so the
  program has each tweak's file value (`decl.tweaked_text`, the value as the file writes it; `tweaked_at`, its name).
  The file isn't one of the analysis's files: its diagnostics aren't published, and places in it are written with
  `write_tweaks_location` (a `file://` URI from `prog->tweaks_path`, byte columns).
- **Lens** (`analysis_code_lenses`): `tweaks.json: <value>` over an overridden tweak, `editor.action.goToLocations` to
  its name in the file. **Hover**: a tweak's says `tweak T Name = <code's value>` and what the file gives.
- **tweaks.json in the server** (`tweaks_document`, `tweaks_request` in server.c): opening or changing it stores the
  text and analyses nothing (it isn't Tide; the game reads it from disk as it's saved). A hover or a definition reads
  the quoted name under the cursor and answers from the game's analysis (`analysis_tweak`), running one for a `.tide`
  file of its folder open in the editor when there's none; semantic tokens are empty and the rest null.
- **Editors**: VS Code's document selector adds `**/tweaks.json` (language json); JetBrains' plugin maps `tweaks.json`
  to tidels with languageId json, so the IDE's own JSON support stays. The JetBrains plugin wasn't built here.
- Tests: `tidels_tests lsp_tweaks_file` (the lens and its place, one lens for one override, the hover, nothing sent
  for opening tweaks.json, its definition and hover, null off a name, empty semantic tokens); the extension's tests.

## `dev` in the grammar and the language server (2026-11-16)

Both TextMate grammars take `dev` among the declaration modifiers (`storage.modifier`), the grammar test has
`dev declarations` (probe.tide's lines), and the language server reads `dev` as a declaration's start
(`starts_declaration`) and a contextual keyword. The grammar test also lets an e2e file that's settings alone pass
(`visibility_repairs_few.tide`, `_wide.tide` failed it since 9f21e4d3d).

## The native scene editor removed (2026-11-16)

The owner removed the native scene editor: "we have been trying to fight the fact that each game deals with its own rendering so there isnt a 'universal' way to draw a scene", and "the game itself should be the one providing tools an our job should mostly focus on making that process easy". Its decision is in `docs/spec.md`, Prefabs and authored scenes. The last commit with the editor is `ab0bc7d4d`; its sections in this file, `notes/cli-and-packages.md` and `notes/language.md` described it and went with it.

What went: `tools/tide-editor` (the `tide-editor` program, its document/play/preview layers and its tests), `tide open`, the private `tide _editor-preview` command and `tide run`/`tide build`'s `--source-overlay` and `--editor-cache`, tidec's `--editor-prefab`, `--editor-local` and `--source-overlay`, the generated preview module (`editor_codegen.inc`, `tide/editor_preview.h`) and every `editor_prefab` branch in codegen, the `[RunInEditor]`/`[EditorOnly]` view attributes (now unknown attributes, like any other name) with their fixtures, completion and hover, the language server's `editor_authoring` (an unused prefab no longer counts as reaching a view's filter or as an entry point, as in a build), VS Code's Open Prefab Editor and Edit Prefab in IDE (`prefab-client.js`, the prefab keybindings and menus), JetBrains' Open/Edit Tide Prefab actions and Tide Prefab tool window, `install.ps1`'s optional Open With registration (`TIDE_ASSOCIATE_PREFABS`), the Package job's `tide_editor` targets, the CLI's `sys_start_tree_logged` (a process tree owned by a Windows job or a POSIX process group, which only the editor's builds and Play used), and `open` from the CLI's usage-data commands (the site's validator still accepts it, for versions before this one).

What stays: `.tprefab` files and prefabs as a language feature (both editors still treat `.tprefab` as Tide, with the language server's prefab analysis and renames), `settings { startScene = ...; }` and `--scene`, and the Unity front end's prefabs and scenes. `scene_startup.cmake` keeps its startup-asset closure checks, now on a file on disk rather than an overlay. An install that used `TIDE_ASSOCIATE_PREFABS` keeps a stale `Tide.Prefab` Open With entry in its user registry until removed by hand: nothing deletes it.

Validation (Windows, sanitize preset): tidec, tide and tidels build without warnings; `tidels_tests`, `tidels_stdio`, `tidels_pipe`, `tide_cli_run_exit`, `tide_cli_help`, `tide_cli_installers`, `tide_cli_telemetry`, `tidec_e2e_prefabs`, `tidec_e2e_scene_startup`, `tidec_e2e_views`, `tidec_e2e_scenes*`, `tidec_diagnostics_frontend_cli` and every `tidec_error_*` fixture pass (2,770 tests), and all 23 `tide_cli_*` tests after the process-tree removal. The VS Code extension's `npm test` (36) and JetBrains' `gradlew test buildPlugin` (15 tests) pass; the real-IDE host suites weren't run. The packaged VSIX target and other platforms are CI's.

## A local worker call's `priority:` (2026-11-15)

`complete_argument_names` offers `priority:` where an argument starts in a call of a `local worker` (not a method's,
and once), inserting `priority: WorkerPriority.`; `WorkerPriority`'s members have what each says in completion and
hover (`priority_member_doc`, through `enum_member_doc`, which Format's go through too); the name is in rename's
reserved list; `walk_expr` goes through a worker call's priority, which the checker keeps off its arguments
(`expr.mesh_lod`), so names in it hover, complete and rename. Both TextMate grammars list `WorkerPriority` with the
built-in types (`tools/tide-syntax`'s grammar test and `probe.tide` have a line of it). Tested by `lsp_worker_priorities`.
Not there: signature help doesn't show `priority:` after a worker's parameters.

## Typed image reads in editor help, and the rendering docs (2026-11-15)

An audit of editor help and docs after the rendering work of 2026-11-14/15 found typed reads shown as Color.
`format_numeric_read` and `numeric_read_doc` (`compiler/lsp/analysis.c`) now give every `[Raw(T)]` read of a
Texture, Texture3D, TextureArray or Cubemap, for `Sample`, `SampleLevel` and `SampleGrad`, the checker's
result type and typed defaults: Point for Sample, Trilinear for the other two, and Clamp (a cube has no wrap).
Hover, signature help and member completion all use them, so they agree. `numeric_read_named` keeps a
volume's SampleLevel and SampleGrad, and a cube's SampleGrad, on the plain forms, which these images
don't have. The text depends on the kind of image. On 2D images and array layers, R8 to Rgba16f blend and
have mipmaps, and R32f and Rgba32f blend with Bilinear from four loads. Cube faces in 32-bit formats are
read at the nearest pixel. Volumes blend with Bilinear and have no mipmaps. Ints are read at the nearest
pixel. This replaces the hover that said "other filters and mipmaps are unsupported". The [Raw] help,
helper-parameter hover, Rgba16f doc and Texture3D constructor doc now say that a volume has no mipmaps. In
GPU code, completion offers `size` and `count` on TextureArray, typed TextureArray and DepthTextureArray,
since frags read them. The frag `this` doc lists `front` and says that `world`, and `front` in 3D, each use
one of the 15 carried values. The handed-struct reads and their int fields were already fixed before this.
Tests: `lsp_numeric_images_and_outputs` checks hover on a typed array Sample and a typed cube SampleLevel
(no `-> Color`), hover and signature help on a typed Texture SampleLevel, hover on a blended typed Sample,
completion on typed arrays (size, count, the four-argument SampleGrad snippet) and typed cubes (no
SampleGrad), signature help on a cube Sample, hover on a helper's typed array parameter, and plain array
completion in a frag. `lsp_texture_arrays` now expects a depth array's `size` and `count` in a frag.
`lsp_shaders_hover` checks the `this` text.

Correction after review (2026-11-15): `numeric_read_doc` first gave every kind one SampleLevel text, which
told a cube that `SampleLevel(direction, 0)` and Filter.Bilinear read its R32f/Rgba32f faces, both refused
at the draw; a cube's SampleLevel now has its own text (name Filter.Point, even at level 0), and its Sample
text says a filter other than Point is the error. The typed volume Sample text named R8, Rg8, R16f and Rg16f,
which a Texture3D can't be made with; it now names Rgba16f and Grid3<float> for the sampler and R32f and
Rgba32f for the eight loads. Every claim was probed with tidec. `lsp_numeric_images_and_outputs` now pins
the 2D level-0 sentence, a 32-bit cube's SampleLevel hover and signature help and its Sample hover, and a
typed volume's Sample hover (no R8/R16f/Rg16f).

Grammars: Vim and Emacs now treat `compute` and `cached` as keywords (Emacs gets `worker` too), as the
TextMate grammars already did. `tools/tide-vim/test/editor.vim` and the Emacs ERT test check `compute`.
DepthCubemap needs no grammar change: like DepthTexture, it's a capitalized type name.

Docs and spec, in the same commit: docs/spec.md has reversed float depth (not 24 bits), 16 textures
and samplers (not 12), four Filter members, `out Color` (not a white start), Draw.Occlusion built in
place of the last-frame Hi-Z item, the numeric-samples diagnostic built, SampleLevel and anisotropy taken
off the mipmap "Not there" list, Grid3 of colors and floats as arrays and volumes, and a frag's `[Linear]
Texture` parameter built: removed from Open, with its rules, which the compiler checks. docs/game: the
typed-cube level and Blit's blend lines are fixed, and so is the claim that a frag can't read `.size`.
In shaders.md and meshes-and-textures.md, the basic texture text now comes first under Textures, and
Volumes moved next to the cube section. DepthBias is in draw-settings.md's table, and 3d.md's decal and
occlusion text covers it, including that biased draws and frags writing their own depth aren't
occlusion tested. Every code block that changed on the rendering pages since 2026-11-14 compiles with
tidec, given the declarations around it.

## Grids as bytes (2026-11-14)

`grid_calls` (`compiler/lsp/analysis.c`) has `ToBytes` and `FromBytes`; `format_method` writes `GridN<T>` as the grid
type it's on. A grid value's completion lists `ToBytes` and leaves out `FromBytes` (the type's), and completion after
`Grid2<...>.` or `Grid3<...>.` offers `FromBytes` alone, outside shaders (`complete_members`, as after `Mesh<...>.`).
`CALL_GRID_BYTES` is an `OCC_METHOD` occurrence, so hover finds its form through the receiver's type (the grid type the
checker gave the type name), and signature help is `CALL_MESH_DECODE`'s case. `lsp_grid_bytes_completes_hovers_and_checks`:
both completions, hover of both calls with no diagnostics, and signature help inside FromBytes' parentheses. No grammar
change: `Grid3<Block>.FromBytes` is names and punctuation the TextMate grammar already colors.

## Enum.Values() (2026-11-14)

Completion after `EnumType.` offers `Values()` beside `Count()` (not after `case`); hover and signature help show
`E.Values() -> E[N]` through `CALL_ENUM_VALUES`, which also makes the call an OCC_METHOD occurrence. The grammar
needs nothing: `Values` is an ordinary method name. `lsp_enum_values_and_order` checks completion, hover,
signature help and a clean foreach that orders with `>` and `<= .South`. Language rules: language.md, same date.

## defined(NAME) and define(NAME, default) in editors (2026-11-14)

The language server checks them as tidec does (`check_symbol_call`, `notes/language.md`): `analysis_run` still
defines only `DEBUG` and the game's packages, so `define(NAME, default)` is its default and `defined(NAME)` what
`#if NAME` is there, a file's own `#define` included (the lexer keeps a file's symbols by `loc.file`, forgotten
when the file is lexed again). Greying out is unchanged: only `#if` leaves lines out, and these calls are code.
The formatter sees ordinary call tokens and leaves them as written. `lsp_directives` checks a document of
`defined`/`define` constants has no diagnostic and formats as it is, and that `define(SIDE)` says what it takes.
`Json.Read`/`Json.Write` hover text names vectors, sets and numbered maps (the JSON note in `notes/language.md`).
No grammar change: both are plain identifiers before `(`.

Review fixes (2026-11-15): both names are in `check_new_name`'s `reserved[]` (rename) (tidec refuses them for types,
functions and constants), completion offers `defined` and `define` where a value goes (`DEFINED_DOC`,
`DEFINE_DOC`, beside `Math`), and `call_forms` gives their signature help. A method of the name hides the
built-in as in tidec (`check_call`), so it has no diagnostic. `lsp_directives` checks all three. `tide.packages`
conditions (`packages.c`) refuse a value compared (`#if STEAM == 480`, `> 400`), `==` beside a symbol the
build gave a value, and `defined(STEAM)`, as the lexer's do; `tide_cli_packages` checks them.

## Linear image annotation (2026-11-14)

Attribute completion offers Linear before asset and field declarations; attribute hover describes sRGB
sampling, unchanged alpha, supported image types/formats and source-preserving GPU helpers. Asset hover
reports its Linear sampling role. Math's ToLinear/ToDisplay signatures expose scalar, float3 and Color
through the existing builtin table. lsp_linear_textures passes: both completion contexts, attribute hover,
valid marked fields and an actionable local-placement diagnostic. No new keyword or grammar token is needed:
Linear is an ordinary attribute identifier, highlighted by the existing brackets rule.

# The language server and the editors

## Reads of any target image (2026-11-14)

A RenderTarget's completion offers `Read` as `Read<T>([int image]) -> Grid2<T> fails IOError` and `ReadDepth() ->
Grid2<float> fails IOError`, each with what it reads, its exactness and its limits; signature help has both, and
`colors`' doc names `colors.count`. Diagnostics are the compiler's. No grammar change: `Read<float>(1)` tokenizes as other generic calls. Test: lsp_render_target_formats_and_depth
(tidels_tests 279/279).

## Numeric images and frag outputs (2026-11-14)

Format completion and hover describe all seven numeric members, accepted output/clear types, storage bytes,
32-bit single-sample limits and portable kernel formats. Frag headers offer Color, float, float2, float4 and
int after out; parameter hover explains the image ordinal and numeric replace/no-tint/no-alpha contract.
Missing-output quick fixes initialize numeric values with 0/float2(0)/float4(0), retaining Color's existing fix.
Ordinary enum-member typo fixes cover the new format names.

Raw attribute completion/signature help describes the four numeric role arguments. Fields and read-only
Texture/Texture3D helper parameters carry their role into Sample hover, member completion and signature help;
they return the numeric type and show Point as the default. Numeric float members offer SampleLevel/SampleGrad, on TextureArray and Cubemap roles too, with snippets that name the level (2026-11-14, render-targets.md), except a field whose constructor assignments fix it to R32f or Rgba32f (field_has_levels, image_target_format of the field: those have no levels; 2026-11-15, corrected: this said only the blendable formats were offered, and every float role was). A helper's parameter or a field with no fixed format may be given either and offers them; the draw's error names the fix. [Raw(type)]'s signature help says arrays and cubes take every role and R32f/Rgba32f blend with Bilinear (it still said float roles and nearest only).
Volume sampling signatures identify Texture3D and float3 coordinates. Clear completion, hover and signature
help describe one to four positional image values and float clears past one, alongside the legacy broadcast.

Tests: lsp_numeric_images_and_outputs, lsp_numeric_output_quick_fixes and lsp_numeric_image_helpers. The shared
TextMate grammar already tokenizes numeric out types and typed Raw fields/parameters; an added grammar probe
pins their existing primitive/function scopes without changing tokenization for other programs.
Validation after the overlapping depth/readback merge: Windows Clang 23 ASan tidels_tests lsp_ passes 278/278;
Node 24.22.1 shared grammar tests pass 383/383. The docs production build passes with the new user pages.

## Texture3D (2026-11-14)

The language server recognizes Texture3D as a local volume. Type hover describes creation, sharing,
GPU sampling/indexing/writes, size, revision, resizing and disposal. GPU completion offers Sample and
size; CPU completion offers Resize, Dispose, size and contentRevision. Chained metadata resolves to
int3/long, and method signature help uses volume_calls. lsp_texture3d passes on sanitize (diagnostics,
hover, members). Both shipped TextMate grammars and Vim's builtin type list include Texture3D; the
syntax probe pins its primitive-type token. Node 24.22.1 runs tools/tide-syntax/test/grammar.test.mjs.

## Pure and cached functions (2026-11-13)

Hover on a function or a read-only method ends with what the checker worked out (`purity_summary`,
compiler/src/purity.h): `Pure: its result comes from its arguments alone, so repeated calls with the same
arguments run once.`, `Pure, and cached: ...`, or `Not pure: it rolls with Random (line 12).` with the first
reason and its line. `cached` shows in a function's signature (`format_routine`, the outline's header). It's a
declaration word: declaration starts take it as they take `async`, the contextual words a rename checks are 16
now, and both grammars have it among the modifiers (`local|async|worker|cached|virtual|override`). The server
reads the tree as written: calls aren't merged for it (`asset_metadata_only`), so hover, references and rename
on a repeated call are what they were. Tests: `lsp_pure_functions`; the grammar test's `cached functions` case
and the probe's three lines (a parameter or local named `cached` is still a name). notes/pure-functions.md.
## `previous` in a query (2026-11-13)

The word before a system's query binding (schedule-and-threads.md, top section) is an occurrence of its own
(`OCC_PREVIOUS`, placed by `walk_param` wherever the parser read it): a keyword's semantic token, a hover
(`describe_previous`: what it reads, that nothing waits, the index; a short form where the checker refused it),
and no rename. The binding's hover ends `As it was when this tick started, whatever has run since.`; a system's
hover and lens say `reads Body as the tick started` (`describe_world`, `put_world_lens`, from
`previous_read_names`), apart from what it reads as it is. Completion offers `previous` at a binding's start in a
system's query (not after `with`, not in a view's, handler's or function's) and the match's components after it.
`format_param` and signatures print it. Both TextMate grammars scope it `storage.modifier.previous.tide` where a
binding starts with it, and leave a name `previous` alone. Tested in `lsp_previous` and the grammar's probe.

The World's `GetPrevious`, `HasPrevious` and `TryGetPrevious` are in `world_calls` (completion with a snippet,
hover, signature help). The checker keeps such a call's name as `Get`, `Has` or `TryGet` with
`expr.world_previous`, so `walk_expr` and the signature lookup put the written name back. A system that reads
the tick's start through its World has the lens `World/query: reads Body as the tick started`, and a match
handler's query is offered `previous` too (not a local handler's).

## What keeps a system on one thread (2026-11-13)

Above a per-entity system whose entities stay on one thread, a lens says what keeps them there, `on one thread: it
changes the singleton Score`, and goes to the place when clicked (a lens with no one place, tasks, says only);
the stage lens of one that splits ends `its entities go across threads`. Hover says both. The text is
`system_one_thread`'s (`compiler/src/parallel.c`), which `tide schedule` prints too
(schedule-and-threads.md, The schedule against measured time). Tested in `lsp_schedule_lenses_and_fixes`.

## Scene preparation editing (2026-11-05)

Scene-owned `prepare(...)` appears as a nested method symbol and outline preparation/access entry; its generated
handler/receiver/private fields are hidden. Own-field references/hover and completion resolve to the scene, explicit
dependencies keep ordinary parameter editing, and scene bodies offer the prepare snippet. Scene.IsPrepared joins
Load/Unload in completion/hover. The shared grammar scopes prepare as a function name. `lsp_scene_preparation` and
the grammar fixture cover these paths; public usage/limits are in docs/game/scenes.md and spec Scenes.

## Texture arrays (2026-11-05)

`analysis.c` knows `TextureArray` and `DepthTextureArray` as it knows `Cubemap` and `DepthTexture`: type docs and
zero-value hovers, method tables (`Sample`, `SampleGrad`, `Resize`, `Dispose`; a depth array's `Sample` and
`Compare`) for hover, signature help and Filter/Wrap completion, members by where the code runs (`Sample`/`SampleGrad`
on the GPU, `layers`, `depth`, `size`, `count`, `Resize`, `Dispose` in views), both type names offered for local
fields only, `TextureArray` as an asset type with image and glob path completion, the four constructor forms' signature
help, and its assets by name in a frag. `lsp_texture_arrays` (`compiler/lsp/tests/lsp_test.c`) covers them;
`docs/tools/editors.md` names it among the asset types paths complete for.

## Meshes over GPU lists (2026-11-05)

The mesh draws' hover (`MESH_SHADER_DOC`) says vertices can be a `GPUList` and indices a `GPUList<int>`; the
checker's forms note (`note_mesh_forms`) says the lists can be GPU lists. No completion or grammar change.

## Kernels and GPU lists (2026-11-05)

`compute` is a declaration word in the language server (snippets in both
declaration completions, `HEADER_SHADER`, the declaration starts, and the
contextual words rename checks, now 15 of them) and in both grammars (with
`GPUList<` scoped as `List<`, and `GPU` as a support class). Completion offers
`GPU` (Run in drawing code, Barrier in a kernel), a GPU list's members by side
(capacity, contentRevision and Set, Clear, SetCount, Resize, Dispose, Read,
ReadCount on the CPU; count, capacity, and in a kernel Add and the atomics on
the GPU), and a kernel's `this` (`Invocation`, read-only, with hovers for its
fields); `GPUList<T>` has a type hover. Tests: `lsp_compute_complete`; the
grammar test's `compute Cull(` case and the probe's kernel line.

## tidels in the docs playground (2026-11-04)

The docs playground runs tidels compiled to a WASI reactor (`tidels_browser`,
`docs/playground/tidels.c`): the same `compiler/lsp` sources without `main.c`'s
stdio transport, one message per call, no `defer`, engine headers at `/include` and
no packages (`packages_set_builtin_folder(NULL)`). Server changes reach it with the
next docs build. Code that needs native-only facilities (processes, file watching,
threads) must keep a path that works under WASI, or `tidels_browser` stops
linking. Its tests are in `docs/tests/playground.test.mjs`. See
notes/docs-site.md, "Playground editor".

## Explicit texture gradients (2026-11-02)

Texture member completion and the common method-form table expose
`SampleGrad(float2 uv, float2 dx, float2 dy, Filter filter = the texture's,
Wrap wrap = the texture's) -> Color`, with a three-argument insertion snippet.
Help describes right/down UV change per destination pixel and the existing
texture filter/wrap defaults. Frags and ordinary helper authoring offer it;
direct verts and CPU views do not. Shared checker rules reject direct or
transitive vertex/CPU calls; grids/depth/cubes retain their existing methods.
`lsp_textures_in_functions` checks helper/frag completion, the snippet, full
signature and active gradient argument, and vertex/view exclusions. Root owns
the central native LSP build/test; no editor adapter or grammar changes.

## Authored DDS asset paths (2026-11-02)

The shared image suffix list includes `.dds` for Texture/Grid2<Color>/Cubemap
path completion and list/map globs, with case-insensitive file matching.
`lsp_asset_path_completion` now checks an uppercase DDS file and DDS glob offers.
The narrow RGBA8 authored-chain importer supplies ordinary checker diagnostics;
no protocol, adapter or syntax change is needed. Central native LSP validation
is pending with the foliage pass; the format contract is in `notes/textures.md`.

## LOD attribute multiplier hover (2026-10-31)

LOD hover now explains `[LOD(2)]`, positive finite constants and multiplication
with the destination's pixel tolerance. Attribute value expressions use the
existing expression traversal for references and hover. The expanded native
ASan `lsp_levels_of_detail` check and full language-server suite pass.
Language and asset contracts are in language.md/assets-and-files.md.

## LOD pixel tolerance completion (2026-10-31)

Ordinary builtin setting registration supplies LODPixels/SetLODPixels on Draw
and render targets, with pixel/default semantics in hover and the usual
block/Set quick fixes. `lsp_levels_of_detail` checks both receivers' completion
and the float/no-argument setter hover. The full native ASan `tidels_tests`
suite passes. Runtime and compiler contracts are in drawing.md/language.md.

## Ordinary mesh handles and members (2026-10-30)

Mesh<V> now canonicalizes to the existing static Asset<Mesh<V>> handle. Member
completion offers vertices/indices directly without exposing identity, and
Value chains retain them. The type-tree walker distinguishes the vertex in
Mesh<V> from the model argument in Asset<Mesh<V>>. Whole-mesh draw signature
help and vertex discovery recognize handles. Mesh help describes persistent ID
storage. Existing LSP mesh/reference/alias checks pass; two stale Clip/Texture
borrow/refusal expectations from the preceding collection task are corrected.
Compiler and combined native/WASI validation: language.md, Static mesh values
keep stable IDs.


## Humanoid retargeting (2026-10-30)

The common language server offers HumanoidRig, HumanoidBone,
HumanoidBoneStatus, RetargetMap and RetargetError in CPU type positions.
HumanoidRig.Auto and RetargetMap.Create have static completion, hover and
signature help, including an unfinished Create call and reordered named
arguments. Rig values expose referencePose and GetStatus; prepared maps
expose rootScale, rootOffset, headingDegrees, spineDistribution and
twistDistribution, with calibration, units and distribution help. Backing
lists and static creation methods do not appear on instances.

Animation.skeleton and Pose.skeleton complete through their bone catalogs.
Pose.Retarget describes its bool result, unchanged output on failure and
retained target skeleton. A rig index and GetStatus argument offer anatomical
roles, including leading-dot shorthand. CPU-only operations stay out of GPU
completion; readonly mutation diagnostics come from the shared checker.
Reference-pose help calls out animation-only exports whose defaults are the
take's first frame. Existing general type and enum syntax covers these names,
so no editor adapter or lexical grammar change is needed.

Protocol regressions:
lsp_humanoid_retargeting_completion_and_calibration_help and
lsp_humanoid_retargeting_roles_signatures_and_restrictions. Both passed on
Windows with native ASan. Compiler/runtime contracts and combined validation
are recorded in notes/assets-and-files.md.

HumanoidRig is also offered as a singular asset type, with .humanoid.json path
completion and ordinary asset file rename edits. Retarget attribute completion
offers profile assets for its first two arguments, then unused named bake
settings; hover and signature help explain build-time baking and ordinary
runtime sampling. The editor indexes the attribute's positional and named
argument expressions, so profile references, namespace qualifiers and option
constants support definition, references and rename. Editor analysis resolves
profile paths without importing their reference models; profile validation and
baking run during the build. An unavailable reference FBX regression pins this
boundary. The focused
lsp_humanoid_assets_bake_completion_and_signatures and
lsp_humanoid_assets_paths_and_bake_references regressions cover these paths;
the existing asset type test also excludes profile collections. Both regressions
and lsp_asset_type_completion passed under Windows native ASan (3/3). The full
native suite also passed, including all language-server tests.

RetargetMap values now offer Bake for either an Animation or AnimationCatalog,
returning the corresponding owned value and failing with IOError. Hover uses
the checked result to show the matching overload; signature help lists both,
selects the checked overload and follows reordered named source/target/sampleRate
arguments. Inlay hints label an explicit positional sampleRate and omit an
implicit default or an already named argument. Help describes the 60 Hz default,
approximate interpolation and ordinary sampling after baking. Bake stays off
static RetargetMap and GPU completion. The focused
lsp_humanoid_runtime_bake_overloads_and_hints regression covers these editor
contracts. All 17 affected native language-server tests passed: five humanoid,
eight skinned, three animation and asset_type_completion. Dynamic Bake compiler
and runtime tests passed on native and web, and the docs build passed.

## Sanitizer test builds keep their own server (2026-10-29)

With TIDE_SANITIZE enabled, tidels stays in that build's bin directory and
does not replace the shared build/tools/tidels editor copy. Routine sanitizer
tests therefore leave the editor's server and runtime selection alone. Ordinary
native builds still refresh the shared copy. The instrumented server and its
stdio/pipe tests remain part of the sanitizer build; configuration and validation
are recorded in notes/testing.md.

## Following a Unity development session (2026-10-26)

The owner replaces Unity's saved compiler override with a launch-only
`-tide-command` (notes/unity.md). Both IDE resolvers accept toolchain.json
format 2; old unscoped selections are ignored. A session selection must have
a live PID whose start epoch seconds match; otherwise normal package/pin
resolution runs. A reusable normal cache has sessionPid 0 and a pinned version
matching the installation's current VERSION; a global upgrade invalidates it.
Nothing changes
the precedence of explicitly configured IDE executables or engine builds.

VS Code resolves launch/session identity asynchronously (also for Debug's
compiler lookup): PowerShell's Process.StartTime on Windows, ps lstart in
UTC/C locale elsewhere. No command line or credentials are queried. While a
development session is selected, a two-second check observes exit even when
the cache file remains after a crash; ordinary pinned projects do no polling
or process queries. The timer is disposed with the extension. JetBrains uses
ProcessHandle.Info.startInstant and its onExit future to restart selection.
Unity's normal quit also deletes the development cache, triggering the
existing filesystem/VFS watches. Missing/unresolved pins remain explicit errors.

Validation: Node 24.22.1, 40 adapter/debug tests, including live/dead/reused PID
and old-cache rejection plus global-upgrade invalidation for Windows/Linux/macOS
resolver branches; extension
bundle build. Real VS Code 1.142.0 with this checkout's tidels passes the prior
multi-project pin/hover/rename/process checks plus a development process killed
without deleting its cache: the language server automatically returns to the
pin and hover works, with no Tide client errors. JDK 21/Gradle 9.8.0 test and
buildPlugin pass all 16 ordinary tests; Plugin Verifier reports Compatible on
IC-242.26775.15 and CL-263.7307.35. Production docs build passes with existing
unstaged-example placeholders. Both Unity launch modes are recorded in unity.md.
Actual Linux/macOS process queries, Rider's UI and a graphical Unity settings
window weren't exercised locally; no engine/web runtime code changed.

## Project-selected Tide versions (2026-10-26)

Owner, after Unity's side-by-side version selection: the IDE should follow it too.
VS Code's `projectProgram` and JetBrains' `TideProjectTools` select `tide` and
`tidels` from a game's `tide = <version>` pin or its Unity package. A release tag
in packages-lock.json supplies a version directly; another git commit uses
Unity's commit-keyed Library/Tide/version.json. VERSION must match before a
side-by-side or normal installation is selected. Missing/invalid pins and
unresolved commits explain the fix, without substituting a different compiler.
Unity owns downloading its version; standalone projects install theirs first.

Unity also publishes Library/Tide/toolchain.json (notes/unity.md), its selected
command including an explicit developer override. The package-lock SHA-256 binds
that cache to the project, including local packages; a cache from another branch
is ignored. These files are local caches, never committed project configuration.
Explicit IDE settings and trusted engine build/tools servers retain precedence.
VS Code's tide.path selects a sibling tidels unless server.path is explicit;
run/debug use the project-selected compiler. JetBrains'
run-configuration command and application-level server overrides remain explicit.

VS Code runs one client per workspace folder with a protocol RelativePattern
(baseUri, not the VS Code RelativePattern class) and workspaceFolder. Each folder
is isolated even when its version differs. Settings/workspace/pin changes settle
for 150 ms before restarting. Narrow watches also see Unity's Library/package
lock above an open Assets folder. JetBrains uses one server per project and VFS
changes restart it, preserving manually disabled servers. Emacs/Vim/Neovim retain
their configured commands; nested independently pinned games within one workspace
folder/JetBrains project aren't separate language clients: open their own roots.

Validation: Node 24.22.1, all 39 adapter/debug checks; bundled extension build;
real VS Code 1.142.0 host with this checkout's freshly built debug tidels, two
pinned installations, automatic pin-file switching, hover/rename/diagnostics,
rapid restart, terminal/process cleanup and the existing muted embedded page.
An initial real-host run exposed the protocol-pattern mistake and event bursts,
fixed before the passing run; its final Tide client log has no errors.
JDK 21/Gradle 9.8.0 test/buildPlugin pass all 15 ordinary tests; Plugin Verifier
reports Compatible on IC-242.26775.15 and CL-263.7307.35. Production docs build
passes with existing unstaged-example placeholders. Unity's version/ABI tests
are recorded in unity.md. No compiler/runtime C, engine suites or web game code
changed; other OSs, Rider's UI and private prototype/service validation weren't run.

## Debugging from the editors (2026-10-26)

VS Code contributes breakpoints for `tide` and a `tide` debug type
(`src/debug.js`): Attach to Unity (Tide), Debug Tide game and Attach to Tide
game (tide run), resolved to `cppvsdbg` or `lldb-dap`/CodeLLDB with the game's
natvis or LLDB views; `test/debug.test.mjs` is in the CTest adapter suite. The
JetBrains plugin adds CLion's `.tide` line breakpoints (`TideBreakpoints`,
optional `tide-cidr.xml`, compiled against `src/cidrStubs`) and Run > Attach
to Unity (Tide) / Debug Tide Game (`TideAttach`). Details, what ran and gaps:
`notes/debugging.md`.

## Renames reaching a Unity project's C# (2026-10-26)

A rename of a type, field, enum member or namespace whose C# twin tide unity writes (a component, singleton, event,
command, struct, input or enum) also edits the Unity project's scripts: `csharp_renames` (`compiler/lsp/csharp.c`) joins its
edits to the WorkspaceEdit after the prefabs' and scenes' (`unity_renames`), as UTF-16 columns. Names come from
`compiler/lsp/unity_names.c`, which tide's `unity_code.c` names the C# with too. What it reads as a reference, the
files it reads, its test `lsp_rename_reaches_unity_scripts` and its gaps: notes/unity.md, Tide types and their C#
twins. A namespace's rename moves its components in the prefabs and scenes too (`unity_move`, a list of moved
components in `unity_rename`). The outline's new `doc`, field `line` and `memberDocs` (`print_outline`) aren't used
by tidels's hovers, which still show no comments.

## Checked skinned asset references (2026-10-25)

Asset<SkinnedMesh<V>> uses the approved typed-reference completion. The shared
type-tree walker follows its model argument, including aliases. Reference members
offer Value without exposing identity; the Value chain offers the declared
skinned type's dynamic skeleton/catalog and geometry API, with no inferred-name
forwarding. Asset completion help names both static and skinned model kinds.
lsp_skinned_asset_reference_aliases_and_dynamic_value_members passes in the full
native suite, including after the Changed-trigger merge. No adapter, native
model preview or grammar change is introduced. Compiler contracts and combined
validation are recorded in notes/language.md and notes/render3d.md.

## `Changed<...>` triggers (2026-10-25)

`local event(Changed<Health.value> change)`: the language server places the
component as a type and each field as a member (hover, rename), prints the
trigger in headers, describes the change in a handler's hover, and the
formatter keeps `Changed<...>` tight (`generic_type`). The change's `old` and
`value` have no source location, so renaming them is refused as a built-in
type's field. The TextMate grammar's handler rules accept the trigger
(`#changed-trigger`, both copies). Tests: `lsp_changed_triggers`, grammar
cases in `tools/tide-syntax/test`. The feature itself is in
notes/verified-and-local-handlers.md.

## Merge integration (2026-10-25)

The one-round merge of origin/dev at b4655dfd into feature commit 2b0edc69
keeps both complete sections above. Only this note conflicted. This text
resolution changes no source and makes no new validation claim; root owns
merge completion and the full native/web suite repeats.

## A Play that didn't start on macOS, cause unknown (2026-11-11)

Run `43063474847` at `2d31c7e01` failed macOS release's `tide_editor_play`
with a segfault: `editor_play_start` returned false for the cancel-build
fixture (`play_test.c`, after the failed-build one), and the test went on to
read the Play's directory, which is null then. The log has no reason: `CHECK`
prints the expression only. It passed in the day's other runs on every
platform, and nothing in that push touched the editor. The test's starts now
go through `START`, which prints `p->message` and `errno` when one fails, and
`wait_pid` answers 0 for no directory. The cause is still to find; the next
failure names which of the directory, the draft or the compiler's process it
was.

## Editor CI cleanup and empty-edit fixes (2026-10-25)

Run `40152905815` at `5d5f6172` failed Windows release's Play workspace-removal
assertion and Linux ASan/UBSan's prefab extraction. `edited_slice` passed a
null empty edit vector to qsort, whose base argument must be nonnull even for
zero elements. document.c now sorts only vectors with more than one element,
including the two source-discovery sorts with the same empty-vector risk.
Existing extraction tests exercise the empty-edit path; source bytes, reference
rebasing and undo contracts are unchanged.

Play previously ignored a failed private-workspace deletion and forgot its path.
It now retries removal for at most one second after terminating the process tree.
A persistent failure reports the path and retains ownership for another Stop or
Play attempt; another build cannot start until cleanup succeeds. Close reports
any remaining failure on stderr before releasing its in-memory bookkeeping.
Only the already-owned workspace is removed. Windows tests hold the generated
executable open without delete sharing: a delayed release reproduces the old
cleanup assertion; a persistent lock checks error/path retention, refused restart
and successful fresh Play after release. The original hosted lock holder is not
identified by its log; the controlled regression establishes the failure mode,
not the identity of another process on that runner.

An unchanged-code stress run also exposed the fixture compiler exiting after
its child's PID file appeared but before the complete PID was written. The
compiler now uses the existing newline-aware readiness helper as its parent
already does, retaining the three-second guard. Production process-tree handling
is unchanged.

All seven final Windows debug editor CTests pass (26.14 s), including native
Vulkan validation, inspector and embedded/scripted UI checks. Optimized Play
passes ten consecutive runs (26.24 s); its final expanded restart regression
passes again (3.70 s). Clang 23.1.2 and Node 24.22.1 are used. The production docs
build passes with the existing unstaged-example 404 placeholders; dependency
audit reports zero vulnerabilities. These are tools/docs-only changes: full
engine native/web suites were not repeated for this scope.

Focused Linux validation under WSL also passes both Play and the complete
document/startup/geometry executable with Clang 23.1.2 ASan/UBSan (15.40 s),
leak detection disabled as in the sanitize preset and nonrecovering sanitizer
checks retained. This covers the hosted null-qsort failure without suppressing
its diagnostic. Actual hosted runners, macOS and Android were not rerun locally;
no workflow was manually dispatched or retried.

## GUI rectangle, scroll and focus additions (2026-10-25)

The common builtin catalog supplies Rect-based SDF overloads, GUI/GUILayout.Scroll
block snippets and mut-offset signatures, and GUI.FocusNext/ClearFocus. lsp_gui
pins completion for scroll/focus. GUIControl.cancelled hover also names touch
scroll cancellation. There is no separate editor-only API table. Runtime and
compiler contracts/validation: gui.md. The native scene editor removes its
old Ctrl+A shim that manually reset the GUI buffer, and uses the common
caret/selection state instead. It binds the optional clipboard callback; its
existing document-undo shortcuts already exclude active text editing. The
unchanged inspector/UI scripted regressions exercise that integration.

## Task handle syntax and editor surface (2026-10-25)

Task/LocalTask completion retains full optional/failable result type text.
The type walker follows declared failure annotations, and Task members offer
Cancel/isRunning/isCompleted/isCancelled without exposing hidden storage.
Match Cancel is omitted in local contexts. Hovers and formatting preserve
Task<T fails E>; engine-awaitable lowering retains authored source symbols
for hover/navigation instead of generated wrapper names.

The shared and JetBrains TextMate grammars handle nested task generic regions
and contextual fails. Vim/Emacs include both builtin type names. The focused
LSP regression checks members, inference, failure references, engine-call
hover and idempotent formatting; shared grammar checks pass 290/290.
The older Web Request test now accepts retained/discarded Send handles and
checks handle completion rather than requiring immediate await. Full-suite
validation: language.md.

Integration retains alias/custom-GUI editor support and synchronizes the
JetBrains grammar snapshot with the combined shared grammar. All 299 grammar
cases, the complete native LSP suite and production docs build pass.
The full native run exposed a PID-file readiness race in the editor Play
fixture: file creation can be observed before its write. The fixture now uses
a newline-terminated PID and waits for the complete line under its unchanged
three-second bound. The rebuilt Play test passes; production process handling
is unchanged.

## Transparent type aliases (2026-10-24)

The shared TextMate grammar distinguishes `using Name = Type;` declarations
from namespace imports, including aliases inside namespace blocks. Alias
names and target types receive type scopes; nested containers, Func modes,
optional types and fixed arrays retain their existing scopes. The all-E2E
grammar check also counts files containing only alias declarations.

tidels indexes DECL_ALIAS, its target tree and the compiler's authored
type_ref.alias/expr.type_alias identity separately from the canonical type.
Hover shows the alias declaration; definitions, references and rename retain
the alias spelling, including constructors, static receivers and qualified
enum backing types. Ordinary enum backing types retain their storage help.
Aliases appear in outline/workspace symbols, semantic type tokens, ordinary
type/namespace completion and unfinished alias targets. Enum and instance
member completion uses the underlying type. A declaration snippet supplies
`using Name = Type;`. Formatting keeps nested generic closers tight.

Validation: the grammar suite passes 290 checks on Node 24.22.1. The rebuilt
native tidels_tests passes all 256 checks, including all five alias protocol
cases, enum backing and attribute navigation, and qualified constructors/World
type arguments. The complete suite passes after the final compiler rebuilding.
Final full native/web and docs integration results follow below.
No editor adapter or protocol change is needed.

Final integration after the GUI/local-worker merge passes the full native/web
debug suites (2716/2611 enabled tests), production docs build and live Render3D
example.
The combined 257 native LSP and 295 shared grammar checks pass. Toolchain/platform
coverage is recorded in language.md and render3d.md.

Review follow-up: qualified primitive/Grid/Random constructors and bare Random
constructors retain their authored alias occurrences. Stage/Before/After/Channel
and asset Filter/Wrap resolve aliases to canonical enums; attribute type
occurrences recover the authored alias through namespace/import lookup while
member occurrences retain the canonical enum. No AST layout change is needed.
`lsp_type_alias_qualified_constructors_and_world` covers constructor references
and World/Spawn component aliases; `lsp_type_alias_enum_attributes` covers
ordering/channel navigation and rename. The existing asset sampling regression
also covers imported and qualified enum aliases. These follow-up cases pass
in the complete rebuilt native language-server suite above.

## Custom GUI composition (2026-10-24)

The common builtin catalog supplies GUI.Draw/Key block snippets, Reserve
overloads and signature help. GUIControl completion/hover includes pointer down,
up, cancellation and GUI-space position. `lsp_gui` checks these additions.
No editor-specific protocol or grammar change. API/contracts/validation:
`notes/gui.md`, Custom control composition.

## Local worker declarations (2026-10-24)

The shared compiler parses/checks local workers. Routine hovers, override
completion headers and document symbols preserve local worker, while outline
JSON records worker:true. lsp_local_workers checks hover, outline and formatting.
Shared/JetBrains TextMate grammars and Vim highlight the contextual modifier;
the probe checks both local/worker orders and worker as a variable. Existing
navigation/diagnostics remain function-based. Public docs link the worker/task
workflow. Semantics and validation: language.md.

The grid input extension preserves CPU completion in worker/grid-helper bodies,
even when those helpers are also GPU-callable. lsp_local_workers includes list
Add completion after a grid-helper call. Existing grid syntax needs no grammar
change. Contract and validation: language.md, Local worker audit.

## Mutable callable signatures (2026-10-24)

Callable type text and hover preserve `mut` argument modes and spell a void
result as void. The formatter's generic-closing scan includes mut, keeping
`Func<mut float, void>` tight and idempotent. The shared grammar gives Func its
own recursive generic region, scopes mut as a modifier, and handles mixed and
nested signatures. `lsp_mut_callable_types_preserve_qualifiers_and_formatting`
and the mutable-signature grammar probe cover these contracts. Validation is
recorded in language.md, Mutable callable arguments.

## Callable functions and configured shaders (2026-10-24)

The compiler retains authored function-reference and invocation metadata while
lowering Func values. Analysis uses those source symbols instead of generated
dispatcher names. Lambda bodies retain their typed parameters, source ranges and
capture-source identities, so captured reads navigate to their original local or
parameter. Named shader binding types and initializers navigate to the shader;
binding slot names use the shader's parameter identity. Func type trees expose
their argument/result types, completion offers Func, and the formatter keeps its
generic brackets tight. The shared TextMate grammar recognizes Func signatures,
serving the editors and docs without adapter changes.

`lsp_callable_bindings_preserve_function_navigation_and_signature_types` covers
diagnostics, function and shader definitions, capture definitions and completion.
The grammar suite includes a Func signature case. Full validation is recorded
with the compiler work in language.md. Material-specific authoring and visual
shader tooling remain outside this foundation.

## Offline installer configuration cache (2026-10-22)

Build run 39790751086 passed plugin compatibility verification but failed while
storing Gradle's configuration cache: buildCliInstaller's doFirst captured the
tasks DSL scope through prepareSandbox. The installer now captures the sandbox
Directory provider, shared with its copy input, and resolves only that value
at execution. The missing-LSP4IJ check, archive contents, dependency ordering,
configuration-cache setting and workflow gates remain in place.

On Windows with JDK 21 and Gradle 9.8.0, the original script reproduces the exact
TaskContainerScope serialization failure. The fixed buildPlugin/buildCliInstaller
passes all nine ordinary Java/platform tests and stores its configuration cache;
deleting just the installer ZIP and repeating rebuilds it successfully from the
reused cache. The exact CI buildPlugin/buildCliInstaller/verifyPlugin command
also passes with caching enabled: Plugin Verifier reports Compatible on
IC-242.26775.15 and CL-263.7031.19. The installer ZIP has the matching VERSION,
Tide plugin, 24 LSP4IJ library files and seven upstream notices, with no duplicate
entries or corrupt members.
Optional real-process/server/native-canvas/JCEF tests are outside this Gradle-only
change. No compiler, runtime or IDE adapter implementation changed.

## Computed properties (2026-10-21)

Properties reuse accessor methods in analysis, with one canonical source symbol
for both accessors. Completion offers one property without call parentheses;
hover shows its type and get/set availability; declaration/workspace symbols
use Property, and semantic tokens use property/readonly. Definitions, references
and rename treat reads and writes as one member. Setter value and contextual
world are implicit and cannot be renamed. Component methods complete contextual
world, and World members include ParentToWorld. Shared grammar recognizes get
and set contextually before accessor bodies. Explicit discard temporaries no
longer create an invented identifier span that masks the expression's hover.

lsp_user_properties covers diagnostics, hover, completion, symbols, cross-accessor
rename, Transform.position and malformed-accessor recovery. An incomplete
property has no accessor yet, which the checker treats as an error type rather
than dereferencing a missing setter. Existing editor adapters need no protocol
change. Full validation is recorded in notes/language.md with the compiler work.

## Prefab hovers, removing unused parameters and Enter in JetBrains (2026-10-21)

Owner, in Rider on a Unity project: a prefab's hover showed `prefab SpinningCube { }`
though its file has content; "why cant i have quick actions here too?" on an
unused `World world`; and Enter between `{}` left the brace beside the caret.

- **Prefab hovers** (`format_prefab` in `compiler/lsp/analysis.c`): a prefab
  went through `format_data_decl`, which writes a struct's fields, and a prefab
  has none. It's now the prefab's own text, from its `prefab` line to its
  closing brace (`decl.end`), dedented by the first line's indentation, CRs
  dropped; past 24 lines, the first 22, a `// ...N more lines` comment and the
  closing brace (`PREFAB_HOVER_LINES`). One tide made of a Unity prefab or scene
  (a file under `.../Tide/prefabs/` ending `.tprefab`: `unity_prefab_asset`)
  adds "From `Assets/X.prefab` in the Unity project, as tide read it."
- **Remove an unused parameter** (`FIX_REMOVE_PARAM`): the checker's "never
  used" warnings whose note says to remove it (a World, a query, a singleton,
  a frag's or vert's input) now carry a fix (`remove_param_fix` in
  `check.c`), as a component's already had `FIX_USE_WITH`. tidels's
  `remove_param_action` removes it from its first token to its name with the
  comma joining it to the next parameter, or to the one before when it's
  last, as `use_this_action` does, preferred and tied to its diagnostic.
  Shared (virtual or override) declarations still get no warning, so no fix.
- **Enter between braces in JetBrains** (`TideEnterBetweenBraces`, an
  `enterHandlerDelegate`, first): the plugin colors Tide with TextMate, whose
  editing support indents on Enter but leaves `}` beside the caret. In a
  `.tide` or `.tprefab` file, with one caret, no selection and only blanks
  between the caret and a `{` before and a `}` after, it replaces the blanks
  with a line indented one level past the opening line (the file's code
  style: tabs or `INDENT_SIZE` spaces) and a line with the opening line's
  indentation, the caret on the first, and stops the default handler. VS Code
  does this itself for the grammar's brackets.

Tests: `lsp_unity_game_is_its_assets_with_what_tide_unity_wrote` (a Unity
prefab's hover with its components and asset; a 34-line one cut after its 19th
entity with "...11 more lines"), `lsp_quick_fixes_remove_unused_parameters` (a
first and a last parameter removed with their commas, the edit applied, and
that warning gone), and JetBrains `TidePlatformTest.testEnterBetweenBracesOpensTheBlock`
(nested in a system, a `.tprefab` on one line, and Enter elsewhere unchanged:
it reproduced the brace beside the caret before the handler). The plugin
reaches the owner's Rider through the Marketplace nightly the manual Build
workflow publishes, or `build/distributions` from `buildPlugin` installed
from disk. The prefab hover and the fix need a tidels with them: the owner's
Rider runs their own installed tide's.

## CLI-managed JetBrains profiles (2026-10-21)

The engine package now carries the matching Tide plugin plus LSP4IJ in a
separate offline installer archive; `tide editors` adds it to standard existing
2024.2+ JetBrains profiles and upgrades update those already containing Tide.
No IDE is launched. New IDEs need one launch before discovery; custom profile
or plugin paths remain manual. Restart after installation/upgrades; locked
Windows plugin files give close-IDE/rerun guidance without removing the old
plugin. Existing LSP4IJ, settings, disabled state and other plugins are preserved.

Gradle buildCliInstaller depends on the tested ordinary buildPlugin and takes
its pluginDirectory plus pinned LSP4IJ from prepareSandbox. Marketplace's ZIP
stays a single plugin; the CLI archive adds VERSION and upstream license/source
notices from cli-licenses, also staged in the engine's licenses/jetbrains. No
Java adapter API or language-server behavior changed. The README documents both
distribution methods; the public standalone repository has not been re-exported.

Windows JDK 21/Gradle 9.8.0 packaging and all eight enabled adapter/platform tests
pass. The real archive's installed files match the build byte-for-byte in an
isolated Rider profile. CLI safety/package fixtures, full native/web suites,
docs validation and remaining platform coverage are recorded in
notes/cli-and-packages.md, Bundled JetBrains installation.

## Stable and nightly Tide Marketplace channels (2026-10-21)

The owner has submitted the initial Tide Stable upload and approved a nightly
channel under the same io.github.blenminer.tide ID. The engine's manually
requested Build workflow now publishes dev to nightly and release to Stable
after the matching engine release succeeds. It keeps the existing token and
Gradle channel property; the public source repository has no second workflow.
The README records submission rather than claiming Marketplace approval, and
documents opting into https://plugins.jetbrains.com/plugins/nightly/list without
changing the separately installed Tide/tidels channel. No plugin implementation,
identity or Gradle code changed. Workflow validation and engine-suite results
are recorded in notes/releases-and-ci.md, Tide Marketplace channels.
The README update is public at main efc67411de0ce9c4508d45d629a1379b71d30f1f;
its audited 24-commit history contains only the canonical plugin subdirectory,
and the update fast-forwards the initial public export without engine tags.

## Standalone JetBrains publication (2026-10-21)

Published https://github.com/Tide-Engine/tide-jetbrains, public main at
6862c56c1a37fe6ae80d77fef0f7f9a7bb4917c9, extracted from canonical engine
commit 1bae9b20 after it was merged into dev. GitHub recognizes MIT. The remote
has only main; no engine tags or release workflow were published. All 23
reachable historical trees match their original tools/tide-jetbrains subtree
exactly. The audit checked all 40 historical paths and 88 unique blobs, with
no credential patterns found; there are no engine/compiler/runtime files.
The isolated export was repacked without its engine object alternates and
passes git fsck --full --strict before publication.

An anonymous fresh Git clone of the public URL has the same tree and passes
buildPlugin with all nine enabled tests, including real process-tree cleanup,
using JDK 21, Gradle 9.8.0 and Node 24.19.0. Only upstream product/dependency
metadata was reused to allow offline validation. Plugin Verifier 1.411 reports
Compatible against both IC-242.26775.15 (IntelliJ 2024.2.6) and
CL-262.10968.117 (installed CLion 2026.2.3); upstream IDE layout warnings remain
unrelated to Tide API usage. The ready-for-manual-upload licensed ZIP is at
build/jetbrains-publication/tide-jetbrains-0.4.0.zip in this checkout. Its JAR
contains the MIT text. No GitHub release asset was replaced, Marketplace upload
performed, CI dispatched or engine license changed.

The owner approved a public MIT Tide-Engine/tide-jetbrains repository using the
Emacs/Vim model: tools/tide-jetbrains remains canonical and the engine retains
its manually requested build, verification and stable Marketplace publication.
The plugin's LICENSE covers its own source, icons, tests and bundled grammar;
there is no engine-wide license change. Gradle's wrapper retains Apache-2.0,
with its upstream license/notice preserved separately. Its JAR matches Gradle
v9.8.0 byte for byte (SHA-256
238e777fcddd7e34f9708186085def2abd6e08e658505b38718d79d74c21abd5).

Standalone builds use the checked-in textmate snapshot. Engine builds continue
using the live sibling tools/tide-syntax. The real process-host test now loads
its bundled resource instead of requiring a sibling VS Code checkout. Before
each public export, refresh textmate/package.json, language-configuration.json
and syntaxes/tide.tmLanguage.json from tools/tide-syntax, and
src/test/resources/host-process.cjs from tools/tide-vscode/test, and graph/ from tools/tide-graph (index.html,
style.css, files.json, src/*.js) and compiler/src/graph_schema.json (the graph editor, notes/visual-scripting.md);
commit those snapshots with the source changes. The initial snapshots are byte-identical.
Public issues/patches are ported to the canonical engine copy before exporting,
as for Emacs/Vim. No second release workflow is added to the public repository.

Use git-filter-repo in a disposable clone after merging source into dev. The
first export used this tool, so keep using it for stable public commit IDs:

```sh
git clone --no-local --no-tags --single-branch --branch dev <engine-repository> <new-export>
git -C <new-export> filter-repo --subdirectory-filter tools/tide-jetbrains
git -C <new-export> branch -m main
git -C <new-export> ls-tree -r --name-only main
```

Inspect every reachable historical tree before first publication, not just the
tip. Verify the rewritten tip equals the canonical subdirectory tree and that
later exports fast-forward public main. Push only main explicitly, with
push.followTags=false. Never run filter-repo in the engine checkout, publish an
engine branch/history, or replace existing public history. Public publication
is recorded separately below.

The MIT text is packaged in META-INF/LICENSE inside the plugin JAR. The README
provides the public license URL for Marketplace's MIT selection and accurately
records the first-manual-upload requirement. This does not create a Marketplace
listing, change its plugin ID, or rerun the failed stable workflow.

Local validation: JDK 21/Gradle 9.8.0 buildPlugin passes in the engine directory
and a standalone source copy without either sibling project. All nine enabled
adapter/platform tests pass in both, including real process-tree termination
with Node 24.19.0. ZIP inspection confirms the MIT text, exact shared grammar
and absence of tests from the installed payload. Plugin Verifier 1.411 reports
Compatible on IntelliJ 2024.2.6 (IC-242.26775.15), with the existing restart
restriction for TextMate enable/disable. The standalone offline build uses
cached upstream dependency/product metadata, not engine binaries. CLion
verification and public-clone validation are recorded with publication below.
No compiler/runtime/build-system code changed; full native/web engine suites
are outside this tools/README-only scope. Optional real-server, native prefab
and JCEF cases, other host OSs, CI and deployment were not run for this export.

## Empty scene shorthand (2026-10-20)

The shared parser accepts semicolon-terminated scenes, including local and
namespaced declarations. Existing LSP symbol/occurrence and formatter paths
handle them without adapter changes. lsp_empty_scene_declarations exercises
diagnostics, hover, references, rename, outline, completion and formatting.
TextMate's existing declaration matcher already scopes both forms; new probes
pin that behavior and the all-E2E grammar check skips their nonexistent bodies.
Validation is recorded in notes/language.md.

## Scene filters in the editor (2026-10-20)

`with scene Arena` names the scene as a type does: its parameter's `scene_of` is
placed as the scene's type (`type_place`), so hover and renames reach it, and
signatures print `with scene Arena`. The grammar scopes `scene` right after
with/without and before a name as `keyword.other.scene`. Tests:
`lsp_scene_filter_names_its_scene`, and the grammar test's case.

## Renames reaching Unity assets (2026-10-20)

Renaming a component, or a component's field, of a game whose folder is inside a
Unity project's `Assets` also edits the project's prefabs and scenes holding
com.tide.unity's TideEntity: `analysis_rename` appends `unity_renames`' edits
(`compiler/lsp/analysis.c`) to its changes, as UTF-16 columns. Details, limits
and the test: notes/unity.md, Tooling as built.

## A Unity project's game (2026-10-21)

A file under a Unity project's `Assets` (beside `ProjectSettings/
ProjectVersion.txt`) belongs to the game that folder is, as if it had a
`tide.packages` (`game_folder`), with its packages found as tide finds them;
files in folders Unity leaves out are dropped, and tide unity's
`Library/Tide/prefabs/**` and `Library/Tide/Unity.tide` come after the game's
own, so `using UnityEngine.Prefabs;` and the project's prefabs resolve
(`add_unity_game` in `server.c`). Test:
`lsp_unity_game_is_its_assets_with_what_tide_unity_wrote`; details in
notes/unity.md, The game is Assets.

## Double Position hover (2026-10-20)

Position attribute hover now identifies float/double scalars and vectors and
their own correction precision. The existing double LSP regression opens a
tagged double3 component, checks no diagnostics and verifies attribute hover.
Completion syntax remains [Position]. Validation: notes/language.md.

## Double matrices and math tooling (2026-10-20)

The LSP and Vim/Emacs/TextMate grammars recognize double2x2/double3x3/double4x4.
Matrix column completion/hover retains the double vector type. Constructors,
identity, double.PI/TAU/E, all component-wise Math functions and double matrix
builtins use the ordinary signature/member paths. GPU completion omits the
binary64 matrix types. Tests extend the prior double tooling regression and
grammar probes; final validation: notes/language.md.

## Double numeric tooling (2026-10-20)

The LSP offers double/double2/double3/double4 in CPU value/type positions, gives
binary64 hover and constructor signatures, returns double scalar/vector types
for swizzles and exposes double.TryParse. GPU type completion omits doubles.
Shared TextMate and bundled Vim/Emacs grammars recognize the new primitive
names; number tokens retain d/D suffixes. Existing formatting remains unchanged.
Lexer/LSP/grammar cases exercise these paths and explicit narrowing guidance.

The grammar's all-E2E declaration check now locates the declaration capture,
rather than the first same-named identifier on its line. This repairs the existing
`local event(Shown) Shown(...)` false failure without changing grammar behavior.
The production docs build and all 268 grammar cases pass. Final native/web and
editor harness validation: notes/language.md.

## Float bits and screen scale help (2026-10-20)

Builtin completion/hover/signatures expose Math.AsInt/AsFloat, float Hash overloads
and read-only float2 Screen.scale with its physical/logical-axis contract. The
Screen module description now includes pixel scale. Existing scalar-only vector
comparison rules are unchanged. Editor regressions check math overload help and
Screen's property/hover. Final validation is in notes/language.md.


## Skinned instancing signatures (2026-10-20)

Signature help recognizes a `SkinnedMesh<V>` as the whole-model form of
`Draw.Instanced`, selecting the instance list as argument two. Built-in help and
instance parameter docs explain optional Pose fields, authored-rest omission and
invalid-item skipping. `lsp_skinned_static_decode_and_draw_forms` checks valid
source, model-form signature, active parameter and the new help text. See
`notes/drawing.md` for validation of the compiler integration.


Query local fields (2026-10-19): local view/listener and helper query headers
offer mut, and binding/query hover explains current local fields alongside
read-only match data. Match headers still omit mut. lsp_query_local_fields
checks members, mutable header completion, clean writes and rejected match
writes; existing helper/view query assertions reflect the extended contract.
No editor protocol/package configuration changed. Validation: language.md.

Recorded-owner event filters (2026-10-19): ownership is offered after with and
without in local match-event handler headers, including verified triggers,
and remains absent for match handlers and known local-world events. Hover
explains captured event-turn ownership and availability endpoints. The existing
lsp_ownership case checks completion, clean listener diagnostics and hover.
No editor protocol or extension packaging changed. Full validation is in
notes/verified-and-local-handlers.md.

Mutable World.Set overlap (2026-10-19): the shared checker accepts direct and
transitive helper replacement of active mut component bindings. Access warnings
wait for World effects so a parameter read after self Set does not suggest
removing mut. lsp_world_in_functions covers both direct and helper no-warning
cases; ordinary read-only World/view restrictions remain pinned. No protocol,
completion or public editor-package change is needed. See notes/language.md.

Shaded drawing functions (2026-10-19): mesh/instanced shader and per-draw
value completion is available in ordinary synchronous functions as in views.
Shader component hover describes both view and function inputs.
The shared checker reports missing/ambiguous explicit shader inputs.
lsp_shaded_drawing_helpers checks clean diagnostics, completion and a missing
singleton; the former view-only shader completion assertion now covers a
function with explicit inputs. Contracts and full validation: notes/shaders.md
and notes/language.md.

Resource asset collection borrows (2026-10-18) resolve in ordinary helper
signatures, so list/map member completion and signature/type text use existing
paths. Parameter hover explains read-only synchronous borrowing and no escape;
shader completion excludes helpers that borrow directly or through helper
chains. `lsp_borrowed_asset_collections_in_helpers` checks readonly signatures,
hover and list/map completion, and GPU filtering. Contracts and validation are
in notes/language.md; no editor configuration or protocol changed.

## Standalone editor publication (2026-10-18)

Published public MIT repositories https://github.com/Tide-Engine/tide-emacs
and https://github.com/Tide-Engine/tide-vim, both with main and v0.1.0.
The engine remains private with no root license. Both exports were audited
through every historical tree: only files from their respective tools/
directories are reachable. Their trees match the canonical engine directories.
The Emacs main is 31f1cb13d4d4d0a04ab1284a36d27b5760310147; its initial
v0.1.0 is aba90c6ee3bcacc2149702558417cfbd7008b332. The later commit only
corrects the test's compatibility call. Vim main/v0.1.0 is
015c1b02dd889f52cde4aa341c77b5abe7f3c565. Canonical source changes are
57c19996, 86898a2f and b1612f46 on top of the original two editor commits.

Anonymous fresh Git clones work. Emacs 30.2 package-vc-install fetches the
documented GitHub URL into an isolated profile; a fresh process proves prefab
autoload detection and all six ERT cases, including real local-server hover
and clean shutdown. The library and test byte-compile warning-free. Emacs's
own generated package-vc *-pkg.el emits a missing lexical-binding warning;
this generated file is outside the plugin and does not prevent installation.
Native Vim 9.2.1202 vim-plug installation passes detection, syntax and settings.
Neovim 0.11.5 lazy.nvim installation passes those checks plus automatic tidels
attachment, hover and shutdown using the documented specification. Both editors'
standalone runtime checks pass from a fresh public clone too. All profiles,
package directories and lockfiles are isolated under build/.

MELPA package-build 5.0.3 fetches the public Emacs repository using the prepared
recipe, builds tide-lang-mode-0.1.0.tar and installs it with working prefab
autoloads and the MIT text preserved. This is not MELPA acceptance. The public
repository was created at 2026-10-18T18:11:24Z; the one-month prerequisite
cannot be met before November 18, and continuing public maintenance and the
owner's human review of assisted code remain necessary before submission.
No upstream submission or future task was created. The recipe and checklist
are public under melpa/ in tide-emacs. Vim/Neovim Git managers need no central
registry submission.

The freshly rebuilt tidels suite passes 237/237 after merging origin/dev's
borrowed-input helper work. The merged production docs build passes on Node
24.21.0 with existing allowed unstaged-example 404s. Docs link the public
repositories rather than the private source paths. No full engine native/web
suite is required for these tools/docs changes; no other OS, deployment or
CI dispatch was run. Emacs 29.1 and Vim 8 remain untested minimum versions.

## Package-vc test compilation correction (2026-10-18)

The public Git installation compiles the test directory too. Its real-server
test now selects eglot-path-to-uri when available, retaining the former helper
for the Emacs 29 bundled Eglot without emitting a deprecation warning on Emacs
30. Both the test and library byte-compile with warnings treated as errors;
all six ERT checks pass against the freshly rebuilt local tidels. No mode or
language-server behavior changed. Public download verification is recorded above.

## Editor-only MIT licensing (2026-10-18)

The owner explicitly approved MIT for both standalone editor plugins.
tools/tide-emacs/LICENSE and tools/tide-vim/LICENSE cover those directories
only; there is no engine-wide license change. The Emacs library includes the
MIT boilerplate so an ELPA archive built from just the .el file retains its
license. Its test carries the same SPDX identifier. The package passes current
package-lint, checkdoc and warning-as-error byte compilation after the headers
are added. This resolves the license prerequisite recorded in preparation below.
Public repository creation and remote-install validation are complete above;
MELPA's public-maintenance/human-review requirements still apply.

## Standalone package distribution preparation (2026-10-18)

Prepared tools/tide-emacs and tools/tide-vim as standalone repository roots for
Tide-Engine/tide-emacs and Tide-Engine/tide-vim. They remain canonical in the
engine source tree; public issues/patches can be applied there before another
export. The initial license/publication prerequisites are resolved above. Do
not claim MELPA registration is complete. (Superseded 2026-10-21: the engine is
public under `MIT OR Apache-2.0`; see `notes/repository.md`.)

Emacs has Version 0.1.0, maintainer metadata, AI-assistance attribution and a
MELPA recipe at tools/tide-emacs/melpa/tide-lang-mode. Package lint's warning
about with-eval-after-load in packages is resolved by requiring bundled Eglot
when the mode loads, then registering directly. No server starts just from
loading it. The built-in minimum remains Emacs 29.1. Tests/docs now work from
the standalone root too. Vim tests find their runtime from their own file path
rather than assuming the engine's tools/ layout. No CLI/compiler/runtime or
language contract changed.

Public exports use only these two directories. For later updates, commit and
merge the source first, fetch, then inspect each split's tree before publishing:

```sh
git fetch origin
git subtree split --prefix=tools/tide-emacs -b publish/tide-emacs origin/dev
git subtree split --prefix=tools/tide-vim -b publish/tide-vim origin/dev
git ls-tree -r --name-only publish/tide-emacs
git ls-tree -r --name-only publish/tide-vim
```

The initial exports contain the original two editor-support commits plus
distribution and license commits; Emacs also has the test correction above.
Do not publish an engine branch as either standalone repository. Push each
audited publish branch to main in its matching public repository. Public
changes need porting to the engine
source before exporting, rather than diverging the two maintained copies.

README and public Editors docs include package-vc-install, vim-plug and
lazy.nvim examples, with a separate Tide/tidels installation and a choice of
one installed copy of each mode. No shared grammar or mode semantics changed.
MELPA's current pull-request checklist requires at least one month of public
maintenance and human review of AI-assisted code. Prepare the recipe now, but
do not submit prematurely or mark either requirement complete. Keep its actual
registration status separate from direct Git installation availability.

Local validation on Windows: Emacs 30.2 passes package-lint from current
upstream, checkdoc, warning-as-error byte-compilation and all six ERT tests
against this checkout's freshly built tidels (Clang 23.1.2, platform off).
Upstream MELPA package-build 5.0.3 builds the recipe from a local candidate
with fetching disabled, produces tide-lang-mode-0.1.0.tar, and package-install-file
installs it in an isolated profile with working prefab autoload detection.
This is local recipe validation, not proof of a public GitHub fetch. Vim
9.2.1202 and Neovim 0.11.5 pass engine-root and standalone-root runtime checks;
Neovim also passes the real server hover/shutdown case. The production docs
build passes with Node 24.21.0 and the existing allowed unstaged-example 404s.
No engine suite, other host OS, CI dispatch or deployment ran for this
tools/docs-only preparation. External publication is recorded above.

## Installed terminal editor setup (2026-10-18)

The modes now ship as editors/tide-emacs and editors/tide-vim, excluding tests.
`tide ide emacs`, `tide ide vim` and `tide ide neovim` print configuration using
the installed runtime and matching absolute tidels path. The user copies it into
their chosen init file; the helper changes no files or personal settings.
Emacs/Neovim connect automatically; Vim retains the user's choice of LSP client.
Moving the installation requires generating the paths again. This replaces the
source-checkout-only setup described in the first implementation below.

The real generated snippets are exercised by tide_cli_ide's optional host cases:
set TIDE_TEST_EMACS, TIDE_TEST_VIM and/or TIDE_TEST_NVIM to executable paths.
They use isolated configuration and the actual installed payload from CMake.
The comma-path host regression exposed Neovim's unescaped runtimepath: the
helper now escapes its comma before prepending it. Vim/Neovim glob-character
installation paths fail with an explicit move/install-path fix instead of
silently losing runtime discovery. Plain string quoting remains editor-specific.
The existing mode/LSP/runtime behavior is unchanged. CLI, packaging, tests and
validation are recorded in notes/cli-and-packages.md.

## Emacs, Vim and Neovim (2026-10-18)

`tools/tide-emacs/tide-lang-mode.el` adds .tide/.tprefab detection, basic
lexical highlighting, four-space BSD-style C indentation and comment editing.
It derives from c-mode and registers the explicit language id `tide` with
Eglot when Eglot loads. `tide-lang-server-command` is an executable/argument
list; it defaults to tidels on exec-path. Connecting is opt-in (M-x eglot or
the documented eglot-ensure hook). The distinct mode/package name avoids
colliding with the existing TypeScript tide package. Minimum Emacs is 29.1.

`tools/tide-vim` supplies ftdetect, ftplugin and syntax for both extensions
on Vim 8+ and Neovim. Its local indentation/comment settings have an undo
contract when changing filetypes. Highlighting is basic lexical styling;
interpolated strings retain string styling throughout. These modes do not
consume the TextMate grammar or promise its contextual highlighting precision.
Neovim 0.11+ discovers lsp/tidels.lua through runtimepath; users explicitly
enable it with vim.lsp.enable. Root selection prefers the nearest tide.packages,
then CMake/Git, then the file's folder, including paths with spaces. Plain Vim
uses a separately chosen LSP client and the tide filetype over stdio.

Both are manually loaded from a downloaded tools folder or source checkout.
They are not added to release archives or tide editors automatic installation.
There are no run controls, embedded prefab canvases or adapters for the server's
VS Code-specific code-lens commands. Other LSP features use the client's own
support. The README, public Editors page and each tool README document setup,
server selection, capabilities and these limits. No compiler, language/runtime,
build configuration, existing IDE adapter or shared grammar changed.

Validation on Windows: Emacs 30.2 passes all six ERT cases (detection, syntax,
highlighting, indentation, Eglot registration and a real server connection/hover/
clean shutdown). Its mode byte-compiles with warnings treated as errors.
Vim 9.1 and Neovim 0.11.5/0.12.6 pass the headless runtime checks, including
multiline comments, quoted characters, degree literals and filetype cleanup.
Both Neovim versions pass root/space-path checks and real tidels attachment,
hover and shutdown. Tests use isolated configuration and temporary project files;
set TIDELS to an absolute executable path to include the optional real-server
checks. The server was freshly built in this checkout with Clang 23.1.2 and
TIDE_PLATFORM=OFF, not borrowed from another worktree. The production docs build
passes with Node 24.21.0 and existing allowed unstaged-example 404s. Diff checks
pass. Full engine native/web suites are outside these tools/docs-only changes;
Emacs 29.1, Vim 8, other operating systems and interactive GUI use were not run.
No CI or deployment was dispatched.

## Shared language-server updates (2026-10-18)

Borrowed input parameters (2026-10-18) complete in ordinary readonly synchronous
function/method signatures, excluding mut/out, async and extern contexts. Hover
describes the explicit caller borrow and paired current/previous tick values.
Input bool edge completion also follows nested structs, in helpers and systems.
`lsp_borrowed_input_in_helpers` pins these headers, exclusions, nested edges,
hover and clean diagnostics for a forwarded input call.

Borrowed helper queries (2026-10-18) complete in ordinary synchronous function
and method signatures. Nested headers offer component bindings and filters,
and helper foreach rows offer entity/component members. Hover explains the
call lifetime, structural binding requirements and source timing; existing
recursive signature formatting supplies signature help and declaration text.
lsp_entity_query_in_helpers checks headers, async/extern exclusions, nested
binding/member completion, arguments, diagnostics and hover. Query uses in
helpers count as uses of the caller's bound component data.

Synchronous match event/command queries (2026-10-18) use the existing query
completion and row members. Header completion now includes every synchronous
Tide handler, while tasks and extern systems retain their restrictions. Hover
distinguishes current queue-time match reads from a local listener's recorded
rows. Non-listener handlers now report World/query access in code lenses too.
`lsp_entity_query_in_match_handlers` covers both handler kinds, row completion,
clean diagnostics, hover and access lenses; header tests retain async rejection.

Texture.size (2026-10-18) completes as a read-only int2 in CPU and GPU contexts,
supports chained vector member completion, and hovers with physical-pixel,
missing-image and metadata-only semantics. lsp_textures_in_functions checks
CPU helper diagnostics, view/helper completion and the member hover. Shared
compiler checking also supplies empty-map, component-value and payload-helper
behavior; no editor-specific syntax rule is added.

DepthTexture.size (2026-10-29) uses the same member completion, int2 chaining and
metadata hover. lsp_textures_in_functions covers its CPU helper and view reads,
completion and hover alongside ordinary Texture.size. No sampling capability
was added to CPU views; Sample/Compare keep their existing completion rules.

## Scene visibility uses ordinary groups (2026-10-15)

Scene completion now offers only Load/Unload. Its load signature/hover explains
ordinary root components and their presence before Spawned; Visibility's hover
explains scene-wide inheritance and intersecting scopes. SceneVisibility is no
longer a reserved editor name or a shared TextMate builtin. Compiler occurrences,
signature/inlay routing and GPU rejection no longer retain CALL_SCENE_PLAYER.
lsp_scenes checks the new root Visibility call, hover and absent membership
methods; the builtin-enum navigation regression uses Prediction instead.
All editor clients consume these shared server/grammar changes; no adapter or
IDE host behavior changes. Final validation is in notes/language.md.

## Visibility APIs and availability events (2026-10-15)

`Shown`/`Hidden` complete only in local event triggers (including verified),
with hovers distinguishing availability from real Spawned/Destroyed. Their
names and Audience/Visibility/VisibilityGroup are reserved built-ins. The
shared TextMate grammar scopes all five as built-ins; the probe retains its
custom-local-event examples under the unreserved name Displayed.

Audience completion/hover/signature help covers All/None, Only(PlayerID),
From(List<PlayerID>), and instance Add/Remove/Contains. Visibility supplies
static CreateGroup/Set/DestroyGroup, its public group field, and instance
Set<T>(Audience|VisibilityGroup)/Reset<T>(). Internal word/override fields
never complete. Generic component completion excludes scene and Visibility
policy types. Static group-writing forms stay out of view/input completion.
CALL_VISIBILITY carries owner/receiver occurrences and named-argument hint
metadata; method signatures have both Set overloads rather than relying on
the ordinary builtin signature table. This updates the common language-server
analysis and grammar used by all editor clients, not a client-specific API.

`lsp_visibility_policies` pins member/type completion, hidden internals,
generic targets, pure versus match-only forms, hovers and static/instance
signature help. Existing local-event completion tests pin Shown/Hidden local
and verified forms and their absence in match triggers. The full fresh native `tidels_tests` passes 234/234, including the new
policy test and availability completion assertions. `npm ci`
in docs installed the pinned dependencies; `npm run test:grammar` passes
244/244 with Node 24.21.0. A final fresh run after endpoint reconciliation
passes LSP 234/234, shared grammar 244/244 and VS Code Node adapter tests 28/28
with no IDE-host launch. Docs production build and all three SEO checks pass
under that Node too; final merged-doc rebuild is tracked separately.

## Entry-point navigation (2026-10-14)

Functions and methods now have an `N entry points` lens beside their ordinary
reference lens. Roots are systems, views, event/command handlers (including
startup, verified and local handlers), the input's separate Sample/Sanitize/
Predict bodies, custom Interpolate methods and audio callbacks. Each reachable
root appears once in source order. One destination uses goToLocations/goto;
several use goToLocations/peek, through the existing VS Code argument middleware
and LSP4IJ commands. Zero roots produce a nonclickable `0 entry points` lens;
ordinary unused functions and recursive cycles never become roots themselves.
Root declarations do not get a redundant entry-point lens of their own.

The graph uses the call hierarchy's source-call classification, including
operators and shader selection in draws. It follows helpers across the analyzed
game/packages, resolves active overrides and normalizes private local methods.
Replaced bodies contribute no edges. Event sends and command submissions do
not connect handlers back to senders; tracing stops at each handler. This is
static reachability through indexed source bodies, not proof a branch executes.
Implicit field/default initialization and trusted C's internal calls are not
modeled. Intermediate paths remain available through the existing call hierarchy;
the lens lists root declarations without adding a separate path UI.

Each request builds one temporary graph. Source-sorted bodies associate calls
in one pass; sorted declaration keys look up callees. Iterative traversals reuse
their stack and visit marks, deduplicate diamonds/recursion, and sort only the
found roots. Everything is inside the existing code-lens arena checkpoint;
no temporary graph or output string is retained in the analysis.

Five protocol tests cover direct/transitive roots, duplicate paths, orphan
functions/cycles, recursion, overloads, ordinary and local methods, extern
declarations, event/command/startup boundaries, input hooks, interpolators,
audio callbacks, shader-to-view paths and unsaved source replacement. Existing
override coverage now pins cross-file root ranges from both virtual and override
declarations and excludes replaced callers. Repeated lens responses and later
hover/reference requests preserve the retained analysis. Adapter code is unchanged;
click contracts are checked at the protocol boundary and use existing tested
native-argument conversion. Actual IDE rendering was not separately exercised.
The real Inside Out MovePuzzle source returns exactly two roots: the MoveHand
command handler in game.tide and WorkshopControls in ui.tide. Seven repeated
real-pipe responses were identical, with a 3.01 ms median for all 90 lenses.

Validation: all 2,286 native debug CTests pass, including GPU tests. The final
server suite passes 233/233 checks, plus stdio and real-pipe tests. The production
docs build passes with its existing allowed missing-game/example placeholders.
A warm native-debug real-pipe probe with 1,000/2,000/4,000 helpers, a shared leaf
and one calling system returned 2,003/4,003/8,003 lenses in median
32.58/68.27/130.02 ms over seven requests; every response was identical. Calls
were on separate source lines, and these are local whole-response timings,
including reference lenses, transport and JSON parsing, not universal budgets.
All 2,249 web debug CTests pass, including WebGL/WebGPU. Clang 23.1.2 and
Node 24.21.0 were used. Other operating systems, Android and sanitizers were
not run; no CI was dispatched. Integration with origin/dev has no incoming
changes or conflicts.

## Input Predict and DontPredict (2026-10-13)

Predict's separate body participates in field/local scope, source occurrences,
hover, outline, go-to-implementation, call hierarchy, selection ranges, local
rename scope, semantic highlighting and formatting's input-method header rules.
Its completion excludes Sample parameters, devices, Cursor and Random operations.
DontPredict has field-attribute completion/hover. Both editor integrations and
docs highlighting share the updated TextMate grammar.

lsp_input_prediction checks body/attribute completion, excluded capabilities,
parameterless headers, accepted diagnostics, hover, outline, implementation
navigation and hierarchy. The shared grammar probe covers the attribute and
Predict keyword, and all three new E2E sources. This is shared server/protocol
and grammar coverage; no adapter changes or interactive IDE-host run is needed.

Validation: full native/web suites, grammar and production docs build pass;
counts and remaining platform coverage are in notes/language.md.

## Tasks from functions, and a request's time limit, 2026-10-11

Neither needed code in tidels. Completion always offered async functions
inside a function or a method, so nothing was hidden; the checker's new
diagnostics (a view calling a function that starts a match task, and the rest:
`notes/language.md`, Functions and methods start tasks) reach editors as any
do. `lsp_functions_send_and_spawn` checks the offer, that a function that
starts a task and the system that calls it say nothing, and the diagnostic at
a view's call. `SetTimeout`, `WebRequest.timeout` and `IOError.TimedOut` come
from the prelude's source and the checker's enum, so completion, hover and the
doc comment are the ones any struct's method has: `lsp_web_requests` checks
the three and the hover.

## T? fields, 2026-10-11

A field declared `T?` is kept as a struct of its value and whether there is
one (`decl.optional_of`: `notes/language.md`, T? fields), which editors never
show. The checker gives a member that reads one the `T?` as its type, so
hover (`int? best`), inlay hints for a `var` (`: int?`), references, rename,
the unwrap quick fix and diagnostics came with it. `analysis.c` has two places
that go by a field's declared type instead: `member_type`, what's after a dot
in completion, now gives the `T?` (`read_type`), so nothing is offered after
a `T?` until it's unwrapped (it offered `value` and `ok`); and go to type
definition looks through the kept struct to what it holds. Field completion
shows `int?` as the detail, from the kept struct's name. The shared grammar
already scoped `?` after a type; `tools/tide-syntax/test` has a struct of
`T?` fields now (five lines, 225 checks). `lsp_optional_fields`
(`compiler/lsp/tests/lsp_test.c`) covers hovers where a field is declared,
read, written and in a struct's value, hints, references and rename,
completion after a dot and in a value, nothing after a wrapped one, the quick
fix, and the diagnostics for a `mut` argument, `List<int?>` and a grid.
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

- `compiler/lsp/`: `tidels`, the Tide language server (owned by Claude). It reuses tidec's front end, with error recovery, to give editors completion, diagnostics, quick fixes (the checker's "did you mean" suggestions among them, as data: `diag_set_suggestion_sink`), hovers, go to definition, type definition and implementation, find usages, highlights, rename, the call hierarchy, formatting (of a range too), parameter hints, inlay hints, code lenses (each system's stage, waits and what keeps it on one thread, and what each local handler of a match event hears), the outline, workspace symbols, folding, selection ranges, semantic highlighting, and moving a declaration to a file of its own. It watches the game's files through the editor (dynamic registration), so files changed outside it are seen. It reads ahead of the message it handles (`compiler/lsp/main.c`), since an editor sends a change at every key and a big game takes a while to analyse: changes that arrived together are analysed once, as the last left the text, when a request needs them or nothing more has arrived (`lsp_settle`), and a request that a later message cancels (`$/cancelRequest`) is answered as cancelled without its work (`lsp_handle_ahead`). Nothing there is timed. Native builds only.
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
    - **A moved file is found by its bytes.** `compiler/src/assets.c` (tidec's library) is the path arithmetic (`asset_path_join`, `asset_path_relative`, Windows ignoring case) and the store: `<game>/.tide/assets.sha256`, a line a file (its SHA-256 as hex, its size, its path from the game's folder), making `.tide/` as tide does; `compiler/src/sha256.c` moved there from `cli/`. The checker writes it once a game's assets are checked (`asset_files_save_hashes`, from `check()`), where its text changed, when it knows the game's folder (`program.game_folder`: tide's `--game`, and tidels's for a game that's a folder, since a manifest's has no `.tide/`), keeping what it knew of a file that's missing. The checker's error for a missing file (`say_missing` in `asset_files.c`) names a file of the game's folder with the same bytes, or lists the files of its name; the server computes the same candidates (`asset_missing`, hashing a file again only when its size or time changed: `file_hash`) and offers them as quick fixes on the checker's diagnostic (`asset_diagnostic` finds it at the path's token; `asset_fix_actions`), the by-bytes one preferred. Hidden files and folders, `.tide/` and `.build/` among them, are never looked in (`folder_find_files`).
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

## Shared components and parenting (2026-10-12)

Editor source occurrences normalize private component/method specializations back
to the public declarations, preserving rename and definition references across
match/local access. Hovers/headers retain local access qualifiers. Completion
uses shared component definitions, offers local Spawn in local code, and no
longer suggests local component declarations. Entity methods include SetParent;
World includes Parent and LocalToWorld, with local typed handles in signatures.
When a method is used only locally, its checked local body supplies implicit-this
hover context. The shared grammar highlights local placement/access and keeps
existing local-field and contextual-identifier behavior. The new LSP test checks
local implicit this, parent/transform completion and signatures, and a four-site
rename across both placements. Final validation is recorded in language.md.

## Authored prefab files (2026-10-12)

VS Code and JetBrains recognize .tprefab as Tide, sharing the TextMate grammar,
LSP server and existing game-run selection. JetBrains icon/banner/context-menu
checks accept both extensions. The shared grammar scopes prefab and entity
declarations; the language server watches and discovers .tprefab under the same
manifest/folder/package boundaries as .tide.

Neutral checked prefab compositions are indexed even when unused. Component and
field rename/definition references include authored entries; prefab symbol renames
include Spawn/Load and nested prefab bases. Authored child-name rename/path
completion is not yet implemented. Basic component/field completion works in
authored blocks, with nested-prefab suggestions after a colon. SceneRequired has
field-attribute completion. No native scene UI or model viewer was introduced.

lsp_prefab_project_and_refactors drives folder discovery, script-to-prefab type
and field renames, prefab and nested-base renames, and watched invalid-file
diagnostics. The grammar has a prefab/child declaration probe. Validation is
recorded with the compiler build in notes/language.md.
