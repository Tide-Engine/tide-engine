# Visual scripting: `.tidegraph` files

The owner asked for visual scripting on 2026-11-24 ("the tide language might scare some people"). Owner's
decisions: graphs are Tide, never C# in the match; a graph file is source that tidec reads with no file baked from
it; everyone is the audience and whole games are made with graphs ("all our prototypes should be able to be made
with just visual scripting"); graphs and Tide source use each other; a full first version, no shortcuts. From my
recommendations the owner picked: an editor of our own in Unity on UI Toolkit (Graph Toolkit keeps its own file
format, and on 6000.6 a node can't have UI of its own), Blueprint-style graphs with exec and data wires that match
Tide's syntax form for form, and optional text nodes. All else here is my pick (Provisional).

## What a graph is

A `.tidegraph` file is a Tide source file stored as a graph. tidec turns it into Tide text in memory
(`graph_lower`, `compiler/src/graph.c`) and parses and checks that as it would a `.tide` file. Wherever a game's
`.tide` files go (a game's folder, a package, Unity's Assets), `.tidegraph` files go too, in the same order of
paths. Declarations in one are seen by the other, so graphs call Tide code and Tide code calls graphs.

Every Tide form has a graph form, and tidec converts a `.tide` file into a graph and back
(`tide graph`, below). The test of that is the round trip: every `.tide` file in the repository and in
tide-prototypes, turned into a graph and back into text, compiles to the same C (`graph_roundtrip` test).

The graph forms follow the syntax, not the checker. A call node is `name(args)`: whether it spawns, sends, draws
or calls a function is the checker's to say, and the editor shows what tidec reports for it (its signature, the
kind of call, the type of every value). New built-ins need nothing new in graphs.

## The file

JSON in UTF-8. tide writes it with LF line endings, two-space indentation, keys in the order given here and one
node per line, so that diffs and merges stay readable; it reads any JSON. Unknown keys are kept by the editor and
ignored by tidec, so a newer editor's extras don't break an older tidec.

```json
{
  "tidegraph": 1,
  "nextId": 42,
  "namespace": "Game.Combat",
  "using": ["Physics"],
  "decls": [ ... ]
}
```

- `tidegraph`: the format's version, 1.
- `nextId`: the editor's next free id. Every element that can be pointed at has an `id`, an integer unique in its
  file: declarations, members, fields, parameters, nodes. Diagnostics name them, wires and local reads use them.
- `namespace`, `using`: the file's `namespace X;` and `using Y;` lines.
- `decls`: the declarations, in order. Order matters as it does in text: it's the default order of systems.

### Comments

Any element can have `"comment": "text"`, written as `//` lines before what it's on (several lines for a text
with newlines). A body can have `"notes": [{"id", "x", "y", "w", "h", "text"}]`: boxes the editor draws behind
nodes, which tidec ignores.

### Declarations

Every declaration has `kind`, `id` and, except where noted, `name`. Common keys:

- `attributes`: `[{"id", "name", "args"}]`, `[Stage(Phase.Move, 2)]` as `{"name": "Stage", "args": "Phase.Move, 2"}`.
  `args` is Tide text, what's between the parentheses; no `args` key means no parentheses.
- `modifiers`: words before the declaration, from `local`, `dev`, `async`, `worker`, `build`, `cached`,
  `virtual`, `override`, `tweak`; written in that order.
- `overrides`: an override's qualified name, `"Combat.Move"`, when `override` is among its modifiers and the
  name is qualified (`name` is then its last part).

Expression slots hold an expression graph, `{"root": id, "nodes": [...]}`. Code bodies hold a body graph,
`{"entry": id, "nodes": [...]}` (`entry` null for an empty body). Both are described under Nodes.

| kind | keys |
|---|---|
| `text` | `text`: Tide text, as written. Any declaration, `#if` line or code the converter left as text. |
| `namespace` | `name`, `decls`: a `namespace Name { ... }` block. |
| `component`, `singleton`, `struct`, `event`, `input`, `scene` | `typeParams` (a struct's, `["T"]`), `where`, `members`; a `scene` with `"members": null` is `scene Arena;`. |
| `enum` | `backing` (a type, or absent), `members`: `[{"id", "name", "value"}]`, `value` an expression or absent. |
| `const` | `type`, `value` (an expression). With `tweak` in `modifiers` it's a tweak. |
| `asset` | `type`, `value`. |
| `alias` | `type`: `using Name = Type;`. |
| `settings` | `entries`: `[{"id", "name", "value"}]`. No `name`. |
| `function` | `returnType`, `typeParams`, `params`, `fails` (a type, or absent), `where`, `body`; `"extern": true` has no body. `mut` before the return type is `"mut": true`. |
| `audio` | `params`, `body`. |
| `system`, `view` | `params`, `body`; `"extern": true` (a system's) has no body. |
| `handler` | `trigger`, `params`, `body`: `event(trigger) Name(params) { body }`. |
| `command` | `payload` (fields), `params`, `body`: `command(payload) Name(params) { body }`. |
| `frag`, `vert`, `compute` | `params`, `body`. |
| `prefab` | `root`: a prefab node, `{"id", "name", "base", "components": [expression], "children": [prefab node]}`. |

`where`: `[{"param": "T", "rules": ["comparable", "hashable"]}]`.

`trigger`: `{"verified": bool, "changed": bool, "type": "Hit", "name": "hit"}`; `changed` makes it
`Changed<type>`, whose `type` is the field's path. A command's sender is one of its `params`, `PlayerID sender`.

Members of a type, in order:

| kind | keys |
|---|---|
| `field` | `local`, `type`, `name`, `attributes`, `default` (an expression or absent). |
| `property` | `type`, `name`, `accessors`: `[{"id", "word": "get" or "set", "body"}]`. |
| `method` | as a `function`, with `modifiers` from `dev`, `virtual`. |
| `operator` | `returnType`, `op` (`"+"`), `params`, `body`. |
| `tweak` | `type`, `name`, `value`. |
| `sample` | `name` (`Sample`, or the input's own name for its constructor), `params` (`type`, `name`), `body`. |
| `sanitize`, `predict` | `body`. |
| `prepare` | `params`, `body`: a scene's `prepare(params) { body }`. |
| `text` | `text`. |

Parameters, of every kind of declaration: `{"id", "mode", "type", "name", ...}` where `mode` is `read`, `mut`,
`in`, `out`, `with`, `without` or `query`, and these keys appear only when they say something:

- `local`, `verified`, `previous`: the words before the type.
- `ownership`: `with ownership` / `without ownership`, which has no `type`.
- `scene`: `with scene Arena`.
- `role`: an image parameter's `Raw`, `Linear` or `Raw(float)` and the like.
- `default`: an expression, a function's `float multiplier = 1`.
- `query`: a `query (...) name`'s parameters.

### Nodes

A node is `{"id", "k", "x", "y", ...}`: its kind, its place in the graph (pixels, x to the right and y down) and
what its kind takes. Inputs are wires, the id of the node whose value goes in, or null for nothing connected
(absent and null are the same). Arrays of inputs are `[id, ...]`.

A body's statements are exec nodes. Each has `next`, the id of the statement after it, and those that hold code
of their own name it by exec outputs (`then`, `body` ...). An exec node is reached from one place only. A body runs
from `entry` along `next`. Exec nodes that nothing reaches, and data nodes that nothing reads, are left out: the
editor shows them dimmed.

Data nodes have one output, their value. A value wired to two inputs is worked out at each, as written twice in
text (Blueprint's pure nodes). A statement's value read later (`eval` below) is worked out once, when it runs.

Reading a local: `{"k": "local", "ref": id}`, where `ref` is the node that declares it (`var`, `foreach`,
`parallel`, an `outVar`, an `is` with a binding, or a `for`'s `var`). It's an error to read one where it isn't
declared. Everything else named (a parameter, a constant, a type, a namespace, a lambda's parameter) is `name`.

#### Data nodes

| k | keys | Tide |
|---|---|---|
| `int`, `float`, `char` | `text` | `12`, `0xFF`, `1.5`, `90deg`, `2.5d`, `'a'`, as written |
| `string` | `text` | `"text"`, without the quotes, escapes as written |
| `bool` | `value` | `true` |
| `this`, `default`, `null` | | the word |
| `name` | `name`, `local` | `speed`, `MAX_HEALTH`, `Combat`, `Math`; `local Scene` with `local` |
| `local` | `ref` | the local's name |
| `member` | `object`, `member`; or `shorthand: true` with no object | `object.member`; `.Title` |
| `call` | `name`, `typeArgs`, `args`, `argNames`, `local` | `local Name<T>(a, crit: b)` |
| `method` | `object`, `name`, `typeArgs`, `args`, `argNames` | `object.Name<T>(args)` |
| `outVar` | `mut`, `type` (absent for `var`), `name` | `out mut var name`, `out Health name` |
| `out` | `target` | `out target` (`out _` is a `name` `_`) |
| `binary` | `op`, `lhs`, `rhs` | `lhs op rhs`, op from `\|\| && \| ^ & == != < <= > >= << >> + - * / %` |
| `unary` | `op`, `value` | `!value`, `-value`, `~value` |
| `step` | `op`, `postfix`, `target` | `++target`, `target--` |
| `conditional` | `cond`, `then`, `else` | `cond ? then : else` |
| `switchExpr` | `value`, `arms`: `[{"pattern", "value"}]` (`pattern` null for `_`) | `value switch { p => v, _ => w }` |
| `interp` | `parts`, `formats`, `values` | `$"a {x:F2} b"`: `parts` one longer than `values` |
| `index` | `object`, `index` (`[id]`, two or three for a grid's `[x, y]`) | `object[i]` |
| `list` | `items` | `[a, b]` |
| `literal` | `type`, `inits`: `[{"name", "value"}]` | `Transform { position = p }` |
| `lambda` | `params`: `[{"mut", "type", "name"}]`, `body` (a data input) | `(float3 p) => body` |
| `coalesce` | `lhs`, `rhs` | `lhs ?? rhs` |
| `is` | `value`, `type`, `binding` (a name, or absent) | `value is Type binding` |
| `has` | `subject`, `type`, `field` | `V has float3 normal` |
| `try`, `defaulted`, `await` | `value` | `try value`, `value!`, `await value` |
| `text` | `text`, `inputs`: `{"name": id}` | the text, each input's name in it standing for the input |

`argNames` is absent when no argument is named, and else has one entry per argument, `""` for an unnamed one.
`typeArgs`, `type` and the like are types as Tide writes them, `List<int>`, `Combat.Health`, `int[4]`, `float?`.

#### Exec nodes

| k | keys | Tide |
|---|---|---|
| `var` | `mut`, `type` (absent for `var`), `name`, `value` (absent: declared with no value), `shared` | `mut var name = value;` |
| `discard` | `value` | `_ = value;` |
| `assign` | `op` (`=`, `+=` ...), `target`, `value` | `target op value;` |
| `eval` | `value`, `block` (exec), `withBlock`, `stacked` | `value;`, or a call's block, `value { block }` |
| `if` | `cond`, `then`, `else` (exec) | `if (cond) { then } else { else }` |
| `while` | `cond`, `body` | `while (cond) { body }` |
| `for` | `init` (one exec node, or null), `cond`, `step` (one exec node, or null), `body` | `for (init; cond; step) { body }` |
| `foreach` | `indexed`, `type`, `name`, `collection`, `body` | `indexed foreach (var name in collection) { body }` |
| `parallel` | `indexed`, `type`, `name`, `grid`, `by`, `offset`, `body` | `parallel (var at in grid by b offset o) { body }` |
| `switch` | `value`, `cases`: `[{"labels": [id or null for default], "body"}]` | `switch (value) { case a: ... }` |
| `return` | `value` | `return value;` |
| `fail` | `value` | `fail value;` |
| `break`, `continue` | | |
| `block` | `body` | `{ body }`: a scope of its own |
| `code` | `text`, `inputs` | statements as Tide text, inputs as `text`'s |

`eval` with a `block` is a call that takes a block, `GUILayout.Vertical() { ... }`; `withBlock` with no `block` is
one whose block is empty, `{ }`. An `eval` with `stacked` is the next of the calls listed before one block,
`Draw.Blend(Blend.Add), Draw.Clip(r) { ... }`: it's the only statement of the block of the call before it, and
written after that call and a comma. An `eval` whose value another node reads is a statement whose value is
kept: tidec writes `var _graph<id> = value;` and reads that.

An empty file, as the editor makes one: `{"tidegraph": 1, "nextId": 1, "decls": []}`. Which modifiers the
editor offers for each kind of declaration is its own (`GraphSchema.cs`); tidec takes any of them, and the parser
says where one doesn't go.

`text` and `code` read their inputs by name. tidec reads the text as Tide tokens and puts each input, in
parentheses, where its name stands as a name (not after a `.`). `code`'s locals are its own: what comes after
reads them only through the text.

### Diagnostics

A graph's errors name the element they're about: `Assets/Enemy.tidegraph:@17: error: ...`, where 17 is the id
of the innermost node or element whose text the error is in. After it tidec shows the line of the Tide it wrote,
which is the same Tide a `.tide` file would hold. The Unity package and the language server read the id.

## Implementation (2026-11-24)

- **Reading** (`compiler/src/graph.h`, `graph.c`): its own JSON reader (arena memory, each value's place in the
  file), then `graph_lower`, which writes the Tide a graph stands for and records each element's stretch of it
  (`graph_map.spans`, inner after outer) and each element's place in the JSON (`places`). `compile.c` lowers a
  source whose path ends in `.tidegraph` right after reading it; the source's `text` is then the Tide, with
  `src->graph` the map and `src->graph_json` the file. Everything after (lexer, parser, checker, preludes'
  `*_wanted` scans of the text) sees ordinary Tide.
  - Expressions are written with the parentheses their places need (parser.c's precedence: lambda and `?:`,
    `??`, the binary operators, `switch` expressions, prefix, postfix), `- -x` never as `--x`. A text node is
    always in parentheses, and each of its inputs too. A `switch` expression's object binds as a prefix
    operand, as the parser reads it.
  - Structural errors (a wire to a node that isn't there, a value wired in a loop, a statement reached twice, a
    local read before what declares it, a kind tidec doesn't know) are said at the element's place in the JSON.
  - `graph_data_wires` lists a node's data inputs by kind; `use_graph` uses it to find the `eval`s whose value
    is read, which are written `var _graph<id> = ...;`.
- **Diagnostics** (`common.c`, `report`): a graph's error prints `path:@id:` (the innermost span's id, or the
  element at that place in the JSON before lowering) and the line of the Tide it's in. Sinks get the place in the
  written Tide as before; the language server works a graph's places out itself. `diag_file_loc` gives a place as
  its file has it (a graph's element's JSON place, anything else as it is), for `#line` (debuggers then show the
  JSON line of the node), the profiler's source table and `--outline`'s lines.
- **Converting** (`graph_convert.c`, `graph_from_tide`): lex, parse (never check), then the syntax tree as a
  graph. It undoes what the parser rewrites: a grid's `cells[x, y]` (the parser's `int2` call at its first
  argument's place), a command's attributes (`[Channel]`, `[Unordered]` moved to its payload type) and sender
  (moved second), a `shared` local's zero value, `i++` as a statement (an assignment with the `++` op), `_ =`.
  Members are put back in the order written by their places. Types, attribute arguments and the like are the
  source's own text (spaces collapsed). Locals resolve by scope (block, loop, lambda parameters hide them).
  Comments go on the next element; one after code on its line goes on the element that line started (the
  comment slot is a shared string the converter appends to). Comments left before a body's end become a note box.
  `#if` between declarations: each directive line a text declaration, the code between converted, a branch's lines left out converted alone (the rest of the file blank, so places stay), as text where they don't parse; `#if` inside a declaration or a block, or a `#define`, keeps the whole file as text. Layout:
  statements left to right along `next`, each one's value trees to its left and below, held code below, a row
  wrapping past 3200 px; nodes nothing reaches (none, from a file) on a row at the bottom.
- **The round trip** (`graph_roundtrip.c`, `tidec --graph-roundtrip [-v] <files> | @list`): the file made a
  graph, the graph lowered, both parsed, and the syntax trees compared field by field with places ignored (a
  block of one statement and the statement the same, `else if` and `else { if }` the same). Files that don't parse
  are left out, quietly. 3,538 of the repository's 3,660 files on 2026-11-24 (the rest are error tests that
  don't parse), and all the prototypes', in 9 s under ASan.
- **CLI**: `tide graph <file.tide>... [--replace]`, `-o`, `--text`; tidec `--to-graph`, `--graph-text`,
  `--graph-roundtrip`. `tide graph` isn't in the usage data's command list (it counts as `other`): adding it
  changes docs/tools/telemetry.md and the site's validation, the owner's call. Graphs are found where `.tide`
  files are: `tide_files` and `stamp_folder` (build.c), `publish.c`, the language server's folder scan and file
  watchers, `tide_add_game` (`Tide.cmake`: a graph's assets are known from tidec's depfile, not the configure
  scan), Unity's `TideChanges`.
- **Language server** (`analysis.c`, `server.c`): a graph document is lowered as tidec does; its `lines` are
  the Tide's, `json_lines` the file's. Its diagnostics go on the JSON line of the element (`data.node` the id).
  Requests on a graph document by position answer nothing (its JSON is no Tide); its own, whose places are byte
  offsets into the Tide (`offset`):
  - `tide/graphText` → `{"text", "spans": [[start, end, id], ...]}`;
  - `tide/graphTypes` → `{"<id>": "<type>"}`, each node's type, the outermost expression at its place first;
  - `tide/graphCompletion`, `tide/graphHover`, `tide/graphSignatureHelp` with `offset`: completion, hover
    and signature help there, as at that place of a `.tide` file.
  A document opened with unsaved JSON is analysed as it stands.
  - `tide/graphMembers` {textDocument, node}: what the node's value has, as completion items (fields,
    methods, a namespace's or a type's members). The game is analysed again with the graph's Tide a `.` after the
    node's first span (`analysis_set_graph_probe`; a value nothing reads is written `_ = value;` at its body's end for
    it, `graph_lower_with`), completion asked just past it, and the next request analyses
    the graph as it is (`s->analyzed = false` both times; nothing is published for the probe).
  - Without a document: `tide/graphSchema` → `compiler/src/graph_schema.json`; `tide/graphLayout` {graph: a
    body's or a value's graph JSON} → {"<id>": [x, y]}.

## What both editors share (owner, 2026-11-24: "ide and unity, both"; shared logic "in tidels")

Graphs are edited in Unity's own window (C#, `unity/com.tide.unity/Editor/Graph/`) and in the IDEs (VS Code and
JetBrains, one web editor). What they'd otherwise each implement is tidec's, served by tidels:

- **The schema**, `compiler/src/graph_schema.json`, embedded in tidec (`graph_schema_text`, `tidec --graph-schema`):
  every declaration and member kind (title, group, keys in order, modifiers that apply, whether it has members,
  parameters, a body), the elements' key orders, parameter modes, operators with titles, built-in type names,
  node categories with colours, and each node kind (title, category, exec, whether it has a value, its own keys in
  order, its single data `inputs`, `lists` of data wires, `items` with wires of their own (`labels[]` an array of
  them) and an `exec` output each, a `map` of named inputs, its exec `outputs` besides next, `ends` for a statement
  whose next is optional, and what local it `declares`). tidec's own lowering reads its nodes' wires from it
  (`graph_data_wires`, `graph_exec_wires`, `exec_kind`), so the schema and what tidec reads can't differ. It's in the
  compiler's identity sources.
- **Layout** (`graph_layout.c`): the converter's and `tide/graphLayout`'s, one algorithm.
- **Members** of a value (`tide/graphMembers`), completion, hover, types and diagnostics, as above.
- **Unity's graph editor with tidels** (`unity/com.tide.unity/Editor/Graph/TideLanguageClient.cs`,
  `TideGraphLanguage.cs`; my picks, Provisional):
  - The client: one tidels for the editor, beside the tide `TideToolchain.Find` gives (or `TidelsOverride`, or
    beside `TidecOverride`, for tests), started when a graph's window first checks. Content-Length JSON-RPC
    (GraphJson's reader, a compact writer), written on a thread of its own, read on another, stderr kept (the last
    6-8 KB) on a third; every answer runs on the editor's update. initialize's workspace folder is the project's
    Assets, which tidels takes as the Unity game (`packages_unity_assets`), so a graph there is checked with the
    game's `.tide` files; a graph in a folder with a `tide.packages` is that folder's game; one elsewhere (Temp)
    is alone. No capabilities are claimed, so tidels registers no watchers; Unity's `TideChanges` sends
    didChangeWatchedFiles for Tide files it imports, moves and deletes. Requests time out after 30 s; a server that
    stops is started again (its documents opened again, its requests failed), at most 4 times a minute, after which
    the client gives up and checks are tidec's. The last window closed sends shutdown and exit (killed after 2 s);
    a domain reload or quit does the same and waits.
  - Operations: a check, a completion, a hover each run alone, queued (`Operate`), since a completion sends a
    probe text. Diagnostics: tidels publishes a change's before it answers any request sent after it, so the
    client holds publications until the next answer and drops them when that request was sent during a probe;
    ones no request claims are taken when nothing waits and no probe is out.
  - The checker (`TideGraphChecker`, the default `TideGraphServices.Checker`): didOpen/didChange (full text, the
    window's unsaved JSON, after the window's 0.6 s debounce), then `tide/graphText` and `tide/graphTypes`; the
    diagnostics are the last published (`data.node` the id, the Tide line of its span as Detail). tidec's checker
    is the fallback when there's no tidels or it fails, and the panel says which (`CheckerSays`).
  - Types: ports take their value's type's colour (`TideGraphTypes`, a few fixed, others a hue of the name), as do
    the wires from them; a port's tooltip and the node's say the type, and the node's is replaced by tidels's
    hover (asked at the node's name: a member's member, a method's name after the object's '.', a call's name)
    when the pointer first enters it after a check.
  - Search (`TidelsGraphSearch`, an `ITideGraphLiveSearch`: its entries come in when tidels answers, the popup
    says "Asking tidels…" meanwhile): a wire from a value dropped on nothing, or a member/method node's
    **Pick member…**, completes after `object.` in a probe, a copy of the file where a member node
    `object.tideGraphProbe` replaces the value's first read (or is a statement after the body's last, or the
    expression's value), at its end less the probe's name; then the real text is sent again. Fields, properties,
    enum members and constants become member nodes, methods (and functions of a namespace) method nodes with an
    input per parameter (named from the completion's detail; their names are only in the entry's title).
    Elsewhere completion is asked at the place a node would go (after the statement a flow comes from, before the
    one it goes to, at the start of the statement a value is for, else after the body's last statement or inside
    its braces): functions as calls (in a statement flow, a call statement, `Statement`), the game's structs,
    components and events as literals, enums, other types, namespaces, constants and names as name nodes, enum
    members as shorthand members; keywords and snippets are left to the catalog, and names the catalog has (the
    graph's locals, the routine's parameters) aren't offered twice. Typed, entries rank by title (whole, start,
    a word's start, inside, else group or keywords), the language server's above the catalog's at a tie.
  - Errors: a Console message of tide's for a graph (`path:@id:`) is logged at the id as its line
    (`TideBuildService.ContextOf`), and `TideGraphImporter`'s OnOpenAsset opens the window on that element
    (`Reveal`); the file inspector's list says "node" for a graph.

## Tests

- `tidec_graph_roundtrip`: the round trip over every `.tide` file in the repository (`graph_roundtrip.cmake`).
- `tidec_graph_errors`: an unknown call in `graph/graph_errors.tidegraph` is said at `@15`, its node.
- `tidec_e2e_graph_nodes`: `graph/graph_nodes.tidegraph`, a game of hand-made nodes the converter doesn't make (a
  kept statement value read twice and run once, text and code nodes with inputs, a value wired twice, else-if,
  unreached nodes), run by `graph_nodes_test.c`.
- `tidec_e2e_graph_<name>`: 26 e2e tests built from their `.tide` made a graph at build time and run with their
  own `_test.c` (`crossplatform`'s pinned bits among them). chars, json and optional_fields aren't among them:
  their tests take hashes from definitions of their own targets.
- `tidels_tests` `lsp_graphs`: diagnostics with the node, and the four requests.
- The Sample's EditMode `GraphLanguageTests` (tidels and tidec of this checkout's build, or `TIDE_TIDELS`,
  `TIDE_TIDEC`): an unknown call's error from tidels on its node with its Tide line, the click selecting it; a
  member node's type (`int`) on it, its output and its object's input, its hover; completion after a wired
  value's `.` offering its field and method (first), the method made from it wired to the value with an input
  for its parameter and lowering to `ball.Bounce(2.5);`; Pick member… retargeting a member node to a method and
  undone; completion at a statement offering the game's function, struct and `Math`, the parameter once, and in a
  statement flow no values; tidels killed and started again with the graph; the last window closed and no tidels
  left; a graph in Assets checked with the Sample's game (its event and command offered) and a Console message's
  node opened through `AssetDatabase.OpenAsset`. `GraphTests` pin tidec's checker: their graphs are the format's,
  not games.
- Every game the sanitize build knows (600 `.sources` lists), compiled from its `.tide` files and from them made
  graphs, by a script (2026-11-24, not a test): 460 the same C, 136 different only in what comes from source
  lines (lambda and `_dropped_<line>_<col>` names, a hash of places, line tables), 4 failing both ways.

## Gaps

- The graph editor's completion is asked at a node's place, not inside a call's arguments (signature help isn't
  used), and a namespace (`Math`) is a name node whose members come from a wire dropped from it. A graph in a
  Unity project's Packages folder is checked alone (the workspace is Assets).
- Debugging steps through the JSON's lines, not nodes in the editor.
- A file whose `#if` is inside a declaration or a namespace block, or that `#define`s, converts as one text declaration. `#if` between declarations converts: its lines are text declarations, the code between them nodes, and the lines a branch leaves out converted on their own (as text where they don't parse alone).
- The converter gives a block of one statement braces, so a braceless `if (x) return;` costs one more in the
  work estimate that splits loops across threads (`stmt_cost`); results are the same.
