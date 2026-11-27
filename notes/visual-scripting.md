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

An empty file, as the editor makes one: `{"tidegraph": 1, "nextId": 1, "decls": []}`. Which modifiers an
editor offers for each kind of declaration is the schema's (`graph_schema.json`); tidec takes any of them, and the
parser says where one doesn't go.

`text` and `code` read their inputs by name. tidec reads the text as Tide tokens and puts each input, in
parentheses, where its name stands as a name (not after a `.`). `code`'s locals are its own: what comes after
reads them only through the text.

### Diagnostics

A graph's errors name the element they're about: `Assets/Enemy.tidegraph:@17: error: ...`, where 17 is the id
of the innermost node or element whose text the error is in. After it tidec shows the line of the Tide it wrote,
which is the same Tide a `.tide` file would hold. The Unity package and the language server read the id.

**An input with no wire is a hole, and the rest is still read** (2026-11-25; my pick, Provisional). A graph being
made is nearly always half wired, and until this a single unwired input stopped the whole file's lowering, so an
editor lost its types, completion and members until everything was connected. Now `put_hole` writes
`_graphHole` (`GRAPH_HOLE`) where the wire would be and keeps the error (`node 12's 'target' isn't connected`) to
say once the text is written, at the hole's place in it, which is its node's span. The checker's `check_name`
gives `_graphHole` the error type with no message of its own when the file is a graph that has holes
(`graph_map.holes`), so a hole is said once and what's around it is checked as usual (an unknown call, a wrong
type elsewhere in the same body). tidec still fails: a hole is an error. Holes are a single input (`input`), a
call's argument, a text's value, a list's item and a literal's field; other wrong shapes (no kind, a wire to no
node, a missing type or name) still stop the lowering. While a hole's error is said, `graph_hole_port` gives its
key and its place among the key's items, which tidels puts in the diagnostic: `"data":{"node":12,"port":"target"}`,
`{"node":15,"port":"args","index":1}`. Editors mark the pin from that, not from the message's words.

## Implementation (2026-11-24)

- **Reading** (`compiler/src/graph.h`, `graph.c`): its own JSON reader (arena memory, each value's place in the
  file), then `graph_lower`, which writes the Tide a graph stands for and records each element's stretch of it
  (`graph_map.spans`, in the order each is finished: an inner one before the one it's in) and each element's place in the JSON (`places`). `compile.c` lowers a
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
    methods, a namespace's or a type's members), and `type`, the value's type (or null), read by an analysis
    before with the node written and no `.`. The game is analysed again with the graph's Tide a `.` after the
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
  them), an `exec` output each and an `element` name for one of them (`case`, `arm`, `init`), a `map` of named
  inputs, `absentWhen` (an input that isn't there while a flag key is set: a `member`'s `object` with `shorthand`), its exec `outputs` besides next, `ends` for a statement
  whose next is optional, and what local it `declares`). tidec's own lowering reads its nodes' wires from it
  (`graph_data_wires`, `graph_exec_wires`, `exec_kind`), so the schema and what tidec reads can't differ. It's in the
  compiler's identity sources.
- **Layout** (`graph_layout.c`): the converter's and `tide/graphLayout`'s, one algorithm. A file's board
  (`graph_layout_board`, 2026-11-25): every card and frame is measured first (a frame as the page draws it: its
  entry, then its nodes' extent, left of 0 too; a card by its rows, with its note, its attributes and a prefab's
  entities), then the frames go in rows like a page's words, in the file's order, in a shape about 16:10 and no
  narrower than the widest frame, and the cards in columns to their left, a column as tall as the frames are
  together (1200 at least). Before, everything was one column: sumo's arena was 55,000 px tall and "Fit" was 2%.
- **Members** of a value (`tide/graphMembers`), completion, hover, types and diagnostics, as above.
  `tide/graphMembers` also takes `{"type": "Transform"}` (2026-11-25): what a value of a type has, for a pin
  nothing reads yet (a wire dragged from an entry's parameter). tidels reads the graph with
  `void _graphProbe(<type> _graphOf) { _ = _graphOf.; }` after it and completes there
  (`analysis_set_graph_type_probe`); a type's text is letters, digits and `_<>,.?[] ` or nothing is asked.
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
  - Operations: a check, a completion, a hover each run alone, queued (`Operate`). Diagnostics: tidels
    publishes a change's before it answers any request sent after it.
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
    **Pick member…**, asks `tide/graphMembers` for the value's node (titled "Members of <type>" by its `type`).
    Fields, properties, enum members and constants become member nodes, methods (and functions of a namespace)
    method nodes with an input per parameter (named from the completion's detail; their names are only in the
    entry's title).
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
- `tidec_graph_errors`: `graph/graph_errors.tidegraph` has an unknown call (said at `@15`, its node) and a
  system with three holes (an assign's target, a call's second argument, an if's condition) and a wrong type
  after them: five errors, each once, in that order (`-DERRORS=5`).
- `tidec_e2e_graph_nodes`: `graph/graph_nodes.tidegraph`, a game of hand-made nodes the converter doesn't make (a
  kept statement value read twice and run once, text and code nodes with inputs, a value wired twice, else-if,
  reroutes, unreached nodes), run by `graph_nodes_test.c`.
- `tidec_e2e_graph_<name>`: 26 e2e tests built from their `.tide` made a graph at build time and run with their
  own `_test.c` (`crossplatform`'s pinned bits among them). chars, json and optional_fields aren't among them:
  their tests take hashes from definitions of their own targets.
- `tidels_tests` `lsp_graphs`: diagnostics with the node, and the four requests; members of a type by name (the
  game's, the engine's, and nothing for text that's more than a type); a graph with holes: each on its node with
  its port (and index), the unknown call beside them still said, no `_graphHole` in any message, its Tide and
  an unread node's members still answered; `tide/graphOperators` (a `float3` by a `float`, a component, an `int`'s
steps and bits, `null` for an unknown type and for text that's more than a type); `tide/graphCalls` for a built-in (`Math.Max`: a, b) and a function of
  the game's (names and types); a layout with a worked-out value left of its statement and a number where its
  reader is.
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

## The Unity editor on the shared schema (2026-11-24, later)

`GraphSchema.cs` keeps its names, and every value is the schema's: ports from each node's `inputs`, `lists`,
`items`, `map`, `outputs`, `ends` (no per-kind switches), wires written in its key orders, locals from `declares`,
node colours from its categories. Its schema is read at the first use after a domain reload from
`Library/Tide/graph-schema.json` when that copy is the same program's (size and time), else from `tide graph
--schema`, which refreshes the copy; when tidels starts, its `tide/graphSchema` replaces it if it differs. With no
schema at all, the window says so with "Try again", and the file waits unchanged. Auto layout is
`tide/graphLayout` (without tidels: a notification, nothing moves); members are `tide/graphMembers`; the C# layout
and the completion probe are gone. `??=` isn't offered (tidec doesn't parse it). GraphSchemaTests: the schema in
use equals `tidec --graph-schema`, `tide graph --schema`, `graph_schema.json`, the Library copy and
`tide/graphSchema`; every kind's ports are the schema's and every wire sets, reads and clears in tidec's order;
layout through tidels (values left of their statement, undoable, the Tide unchanged); graphMembers for a read
and an unread value. 28 of 28 Unity tests pass (GraphTests, GraphLanguageTests, GraphSchemaTests). The add menus
still list kind names rather than the schema's titles and groups.

**Rebuilt as one board (2026-11-25)**, to "The experience: one board" below. `GraphCanvas`, `GraphInspectorView`,
`GraphOutlineView`, `GraphFields` and `GraphNodeFields` are gone; `GraphBoard` (+ `.uss`), `GraphCard`,
`GraphFrame`, `GraphLook`, `GraphPanels`, `GraphPopups`, `GraphValues` are new; the window, the node view, the
search popup and the catalog are rewritten. What a user meets is the web page's list above, in Unity's look: the
start screen and the grouped Add menu (a right click or Space too), cards edited in place with Enter for the
next row, the searched type picker fed by tidels, values by their type's control (a float refuses what isn't a
number and says why), a card's menu with its kind's members and options alone, frames with the entry's
sentence and "+ needs…", problems on their node and row refilled by every check, Contents (Ctrl+F), a far view of
names and silhouettes, "View as Tide" and the rest in the board's "…" menu.

- The error that stayed (the owner's report): the inspector's message was rebuilt on selection only, never on a
  new check. Every check now refills each mark in place, cards' rows too. Two more found by driving a real
  pointer: a list row that came under a still pointer became the current one (Enter took it, not the first
  match); a closing window could reopen its document in tidels and keep the server alive.
- Its picks that differ from the spec then written: rows reorder by a menu (Move up / down), not by dragging;
  attributes, namespace, using, "Replaces" and type-parameter names are short text prompts; a lambda's
  parameters are small name nodes; a value's graph floats beside its card while open (no place in the file); a
  card taller than tidec's estimate pushes what's below it on screen only; declarations copy as `"decls"` in the
  same `tidegraphClip` object as the web page's.
- Pictures: `GraphShots.cs` (the Sample's tests) walks the story and saves a PNG per step when
  `TIDE_GRAPH_SHOTS` names a folder. **A batch-mode Unity with a graphics device shows its EditorWindows on the
  desktop it's started on** (the first picture runs put them on the owner's and took focus for moments, which
  AGENTS.md forbids): a run with a graphics device is started on a Win32 desktop of its own
  (`hidden-desktop.ps1`, kept with the pictures), and tests otherwise run with `-batchmode -nographics`, under
  which Unity lays nothing out (the pointer test is skipped there and passes on the hidden desktop).
- Tests (2026-11-25, `-batchmode -nographics`, the three graph fixtures): 36 passed, 1 skipped (the pointer
  test), 1 failed for a stale `tide.exe` in the build tree (the schema test compares `tide graph --schema`).
- Round two (2026-11-25): "Simple values are in the input" below, built: an input's row is its name and a
  field, a tick, a chip, a pin or "pick…"; the value picker ("What goes in 'amount'?": a typed value, what this
  code has, the game's, "Work it out…", "Nothing"; only what fits, with "Show what doesn't fit too"); a hole
  said as "Pick what goes in 'what'" from `data.port`; titles with their object (`Math.Clamp`); arguments named
  from `tide/graphCalls`; "In this game" from `tide/graphNames`; a new item revealed by panning just enough.
  Wanted types: a call's from `graphCalls`, a Set's value from its target, a condition `bool`, a return the
  result's, a local's and a field's their own. Its picks: Set's value row shows the operator's button where the
  label would be; a value frame's root keeps a literal as a small node (tidec doesn't fold what a root reads);
  removing a parameter leaves its reads (the check says where); fitting is equality and the spec's numeric
  widenings, generics and unknown types fit. 39 tests: 37 passed, the pointer test skipped under `-nographics`,
  and the schema test failed for a stale `tide.exe` (the CLI's target is `tide_cli`; `tide` is a phony that
  didn't relink it). On the hidden desktop: both stories, `SimpleValuesAreInTheInput` and the pointer test pass.
- Not there: valid attributes per kind, the namespaces that exist, what an `override` can replace (text
  prompts); which ways of taking a parameter a kind allows come from completion's keywords, so without tidels
  every way is offered.

## The IDE editor (2026-11-24, later)

One page, `tools/tide-graph/` (its README says how it's laid out and run): classic scripts on
`globalThis.TideGraph` in the order `files.json` lists, no build step and no dependencies. Everything of the
format is the schema's; a packaged copy is used until tidels answers. The host bridge is a few messages (top of
`src/app.js`): the host sends the document and diagnostics, the page sends each edit as the whole text and its
requests, handled in order so an edit is in the document before the next request.

**Rebuilt as one board (2026-11-25)**, to "The experience: one board" below; the outline, the inspector, the
Tide panel and the per-graph canvas are gone (`canvas.js`, `inspector.js`, `outline.js`, `search.js` removed;
`board.js`, `cards.js`, `frames.js`, `pickers.js`, `values.js` new). What a user meets, in order:

- An empty file asks "What do you want to make?" with the schema's `common` kinds and their summaries, and
  "More…". A new card or frame has its name selected; Enter makes the first row; Tab goes to the type, where a
  letter opens the type picker with it and Enter takes the first match; Tab goes to the value, the type's own
  control; Enter makes the next row.
- The type picker: the common types, the file's, the builders ("List of…" asks what it holds with the same
  picker), then the rest; tidels is asked what's valid at that place (a field isn't offered a component). No free
  text.
- A card's "…" menu has its kind's `memberKinds` alone (a `once` member greyed when there), its options by
  label, "Generic over…" only with `typeParams` in its keys, attributes, a note, earlier/later in the file.
- "+ Add" and a right click open the grouped menu (Data, Behaviour, More) with summaries and a search box. A
  frame's entry has the kind's sentence, a row per parameter (its mode in words, clicked to change; its type;
  its name; a pin), filters with no pin, and "+ needs…".
- A wire dragged from a parameter's pin offers the value's fields first; the read is no node, the wire comes
  from the pin. A wire dropped outside its frame does nothing and says why.
- Problems are on their row, pin or node and in the chip's list, and go with the next check. Frames off screen
  are empty, far ones blocks under their names; Contents and Find… (Ctrl+F) jump. "View as Tide", "Tidy up
  the board", namespace and uses are in the board's "…" menu. A value that's more than a literal opens as a
  small graph beside its card. A phone gets the board alone.

**Round two (2026-11-25)**: "Simple values are in the input" below, built as in the Unity window, so the two
read alike. The story is one node ("Set what wobble.phase += wobble.frequency", no wires). An input's row holds
a typed number, tick or text, a chip (a name or path in its type's colour, each part clickable), a vector's
boxes, a colour's swatch, a pin, or "pick…"; pins of inputs show while a wire is dragged. The value picker is
titled "What goes in 'value'? A float." and lists a value to type, what this code has (each opening to its
fields by arrow or a typed `.`), the game's constants and functions, "Work it out…" (which wraps what was in
the input as the new node's first input), then the engine's namespaces; what doesn't fit is left out, with
"Show what doesn't fit too"; "Nothing" empties the input. Nodes have no caps kind labels; a method is titled
with its object; arguments are named from `tide/graphCalls`, and the "+" for another carries the next
parameter's name and goes when all are given. Holes are on their row from `data.port`/`data.index` as "Pick what
goes in 'what'". "In this game" is `tide/graphNames`'; an enum of another file gets its choices
(`tide/graphMembers {"name"}`). The page's own word tables are gone for the schema's. Its fold is checked
against tidec's layout over every converted file by a model test (it fails when `graph_layout_fold` changes
without the page). `docs/playground/stars.tidegraph` is laid out again and lowers to the same Tide.

Not there, or the page's own guess: an engine enum (`Anchor`, `Filter`) has no choice control on a card (it
isn't in `graphNames`); Esc closes the picker where Unity's goes back; "what fits" is the page's approximation
(`valueControls` families and `graphNames`; a function's result is read out of completion's detail text; a
binary operator's other side is only a hint); Return's empty value row is hidden when the frame has no result
type; settings' entry names are free text; a prefab's card is basic; attribute arguments, generic rules,
"Replaces" and a handler's `Changed<>` path are small text forms. Wanted of tidels for these: what a function
gives as a field of its completion item, and whether a type fits a place.

- VS Code (`tide-vscode/src/graph.js`): a CustomTextEditorProvider, so the document is VS Code's (undo, save,
  dirty, hot exit, diffs); requests go through the extension's language client, whose document selector now has
  `**/*.tidegraph`; diagnostics with their node through a `handleDiagnostics` middleware. Commands: Tide: Convert
  to Tide Graph (Replace or Keep Both, `tide graph`), Tide: Show as Tide. `.tidegraph` is in the `json` language
  for its text view. `vsix.cmake` packages the page and the schema in `extension/graph/`.
- JetBrains (`TideGraphEditor`, `TideGraphBridge`, `TidelsServer`, `TideLanguageClient`): a Graph tab before Text,
  in JCEF; the page's edits are write commands on the IDE's document; requests through LSP4IJ; the IDE's
  undo/copy/paste/delete/duplicate/select-all reach the page while it has focus; without JCEF, the text editor
  and a notice. Gradle puts the page and schema in the jar under `tide-graph/`. A standalone clone of the plugin
  builds from the snapshot in `tools/tide-jetbrains/graph/` (the page's files, `fonts/` and `graph_schema.json`), refreshed
  before each public export as `textmate/` is (notes/editors.md).
- Tests: `tide_graph_editor` (ctest; in the root CMakeLists beside `tide_vscode_adapter`, as `tools/` is
  configured alone by `tide_cli_ide`, with no tidec):
  19 model tests (the fold as tidec's over every converted file; byte-for-byte round trip of the converted graphs with their places; every wire tidec writes
  is a port the schema gives; ports and key orders of every kind; wiring rules; paste remapping; undo with host
  changes; a graph built from nodes checked with tidec and compiled; every declaration's place on the board;
  nothing offered that a kind can't have; a read of a parameter or a local a wire and no node), 5 browser tests
  in a headless muted Chrome with real pointer and key events over DevTools (`test/browser.mjs`: the story from
  an empty file to an error that goes; the Add menu's groups and what each card offers; a wire that can't leave
  its frame; sumo's arena, 1,161 nodes, opened, panned until another frame is drawn, fitted, jumped in; a run
  checked by tidels), and the VS Code bridge with fakes. tide-vscode `npm test` 38 pass; JetBrains
  `gradlew test buildPlugin` 18 pass, `-PtideTestJcef=true` both real-JCEF tests pass. The real VS Code host suite
  ran its graph part (open, tidels, an error on its node's JSON line, delete and undo through the page, VS Code's
  clipboard, save, Show as Tide, Convert) twice on a hidden desktop; the suite's old terminal step doesn't start
  there, so the whole suite hasn't run to completion here (CI runs it). A sanitize tidels copied for the pinned-version step has
  no ASan DLL beside it: that step needs a debug or release tidels.

- The docs' graph playground (`/guide/graph-playground`, notes/docs-site.md, "Graph playground") is a third host
  of the page: `docs/playground/graph-host.mjs` over the browser tidels and tidec, with a starter game of graphs
  alone (`docs/playground/stars.tidegraph`). The page's rows are placed by name (`style.css`), so its hidden banner
  leaves its row empty in every host.

## Gaps of the shared schema

- Key types (flag, string list, type, an expression's graph, a body's, an element list) aren't in the schema: the
  IDE page infers them by key name (`schema.js`) and the Unity window has its own inspector by key.
- What a new element starts with (a call's name, an operator, a constant's value) isn't in it: each editor has a
  small table (`doc.js`, `GraphDocument.cs`).
- `items[].label` is a key (a literal's `name`) in one place and a word (`case`, `arm`) in others.
- The add menus of the Unity window list kinds by name, not the schema's titles and groups.

## The experience: one board (owner, 2026-11-24)

The owner tried the first Unity window and called its UX horrid: it showed the file format (a list of 23 kinds, an
inspector of every key with "comment" and "dev" first, "type params", a type field that takes any text, members
a kind can't have, the Tide source always on screen, an error that stayed after its cause went, a canvas that
did nothing while a component was selected). The owner's words for what it must be: "we need to think of this
almost like a story, how do we guide the user"; "friendly, simple, approacheable yet powerful"; "only show me
things when they are relevant, only allow me to do valid stuff"; the UI should guide "the same way i want errors
to be descriptive and guiding"; a type is picked or auto-completed, never typed wrong; the source isn't shown
unless asked for. Asked how a file is laid out, the owner picked **one board**; asked where to get it right,
**both editors together**. What follows is the design both editors build (my picks within those, Provisional).
Its wording and rules are the schema's (`graph_schema.json`), so the two say the same things.

**Rules.** (1) Nothing is offered that isn't valid there: kinds of member per kind of declaration
(`memberKinds`), a member that goes once (`once`), modifiers per kind, types valid at a place, wires that can be
made. (2) The common thing first, the rare behind "More" (`group`, `common`, `advanced` in `keyInfo`), in plain
words (`title`, `summary`, `keyInfo.label`, `modifierInfo.label`, `paramModeInfo.label`, a node's `help`): "Generic
over" for type parameters, "Dev builds only" for `dev`, "changes" for `mut`. (3) No form of keys: a thing is
edited where it's drawn. (4) Every empty place says what goes there. (5) Tide's own errors, on the thing they're
about, gone as soon as their cause is.

**The board.** A file is one canvas. Each declaration is on it, where its `x`, `y` say (tidec's converter
writes them; `tide/graphLayout` with `{"file": <the file's JSON>}` gives them, data in a column on the left
and code to its right; a declaration with none is placed by the editor beside what's there):

- **A card** for data (component, singleton, struct, event, enum, input, scene, const, asset, alias, settings,
  prefab, text): a kind chip in plain words, the name (edited in place: a new card starts with its name
  selected), then a row per field: name, type, starting value, each edited in place; a last row "+ field" (Enter
  in a new field's row makes the next). An enum's rows are its members, settings' its entries. A card's "..." menu
  has what else its kind can have and nothing more: the member kinds of `memberKinds` (a method, a property, an
  operator on a struct alone, Sample on an input alone), its options by their labels (`modifierInfo`), "Generic
  over..." where its keys have `typeParams`, attributes, a note, duplicate, delete.
- **A frame** for code (system, view, handler, command, function, audio, frag, vert, compute, and a type's
  method, operator, accessor, Sample, Sanitize, Predict, prepare, each a frame of its own titled `Type.Name`): a
  titled box that holds its nodes, which are placed from its corner (a node's `x`, `y` are in its frame). Its first
  node is the **entry**: the kind's `sentence` ("Every tick, for each entity with", "When this happens", "Takes")
  and a row per parameter with an output pin: its type (picked), its name, and how it's taken by its label
  ("reads" / "changes"); filters ("only with", "without") are rows with no pin; "+ needs..." adds one by picking
  what it takes (a component, a singleton, a filter, and what tidels offers at that place). A handler's entry
  picks its event; a command's has its payload's rows; a function's has its result's type. The entry's exec
  output is the body's `entry`. Dragging a frame's title moves all of it; it grows around its nodes.
- **Wires for names** (superseded 2026-11-25 by "Simple values are in the input" below, which goes further: no
  wire either). A `name` node that reads the frame's parameter, and a `local` node, aren't drawn as
  nodes: their wire comes straight from the parameter's pin on the entry, or from the pin of the node that
  declares the local (`var`, a loop's variable, an `is`'s name, an `out var`). Dragging from such a pin makes the
  node; renaming the parameter or the local renames them. A `name` of anything else (a constant, a type, a
  namespace) is a small node.
- A namespace block is a tinted region around its declarations, named. The file's own namespace and `using`
  lines are in the board's "..." menu.

**Starting.** An empty board says "What do you want to make?" with the `common` kinds as big choices, each
with its `summary`, and "More...". "+ Add" (and a right click on empty board) opens the same as a menu: Data,
Behaviour, More (`groups`), each kind with its summary, with a search box. A right click or Space inside a
frame, or a wire dropped there, opens the node search for that frame; a wire can't leave its frame (said why).
So no click does nothing.

**Picking a type.** Wherever a type goes: a popup with a search box that completes as it's typed. First the
`typePicker.common` types and the game's own (from tidels, by what's valid there), then its `builders` ("List
of..." asks for what it holds, with the same picker), then the rest. Enter takes the first match. No free text: a
type is always one that was picked, a generic's own parameter among them.

**Values.** A field's starting value and the like are edited by the type's own control (a number, a tick box,
text, a vector's numbers, a colour, an enum's choices), showing the type's default greyed when there's none. A
value that's more than a literal opens as a small graph beside its card.

**Nodes.** A node's own values are on it; what's rare is in its "..." menu. A call in a statement flow is one
node. Unreached nodes are faded. An empty frame says "Drag from the arrow to add what happens".

**Checking.** tidels checks as the board changes; a chip says "Checked" or how many problems. A problem is
on its row, pin or node (red, the compiler's message and its note on hover and in the list), and goes when the
next check doesn't have it: what's shown is always the last check's, never kept. The Tide the file stands for
isn't on screen: "View as Tide" in the board's "..." menu opens it.

**Finding.** A "Contents" list (closed until asked for) and a jump-to search list the file's declarations by
group; picking one pans to it. Frames off screen aren't drawn.

**Simple values are in the input** (2026-11-25, my pick after looking at the first rebuilt page; replaced for
variables by "Round four: full Blueprint" below, the owner's decision; what's here still holds for literals).
With a node per name and per `.member`, one statement was three nodes and four wires (`wobble`'s pin → `phase` →
Set's target), a converted function was a syntax tree, and in a big frame the wires from the entry's pins crossed
all of it. So: **a value read by one input is shown in that input and edited there, and is no node and has no
wire, when it's simple**: a literal (`int`, `float`, `bool`, `string`, `char`, `this`, `default`, `null`), a
`name` or a `local`, a `member` of what is itself shown there (a path, `ball.position.x`; a shorthand `.Run`), a
`-` over a number. Anything worked out (a call, an operator, a list, a literal of a type...) is a node wired in, as
before; a simple value read by two inputs is a node too. The schema says which kinds (`inline`: `always`,
`path`, `negative`, and the rule in words); the file is unchanged, a node per name and member as tidec writes
them. A statement reads as a sentence on one node: **Set** `wobble.phase` `+=` `wobble.frequency`.

- An input's row is its name (`portInfo`), then what's in it: a number's, a tick's, a text's own control; a chip
  with a name or a path in its type's colour; or a pin for a wire. An empty input says "pick…" (red after a check
  says it's a hole, by `data.port`).
- Clicking an input's value, or its "pick…", opens the value picker for that input, a search box over: a value
  of its type to type in (where the type's known: `tide/graphCalls` gives a call's parameters' types,
  `tide/graphTypes` a node's), **what this code has** (the frame's parameters and the locals declared before
  it, each opening to its fields through `tide/graphMembers` by type), the game's constants and functions
  (tidels' completion at that place), then "Work it out…" (operators, calls), which makes a node wired in.
  Each segment of a path can be clicked to change it from there on.
- The entry's pins and a node's value pin are still dragged from: dropped on an input they fill it (a chip for
  a parameter or a local, a wire for a node); dropped on the frame they open the search as before. Hovering a
  chip lights the entry's row or the node that declares what it reads, and the other reads of it; hovering a
  parameter's row lights its reads.
- A method whose object is shown in the input is titled with it, `ball.Bounce`, `Math.Clamp`, and has no "of"
  row; a call's arguments are named by its callee's parameters (`tide/graphCalls`: `params`, `types`, `args`),
  "argument 1" only until the first check answers.
- Layout: tidec gives what's shown in an input no room (`graph_layout_fold`: the node is placed where its reader
  is, the reader is as wide as its widest row). sumo's arena went from 24,000 x 28,000 to 15,000 x 14,000.

Later the same day, after the Unity window's pictures of it (a `float2(16, 14)` was still a node wired into
"position"): **a vector or a colour made of numbers alone is in the input too**, as the type's own control
(`inline.numbers`: a `call` named one of `numbersOf` whose arguments are all numbers shown there). And the
words a Set's operator has are the schema's (`operators.assignInfo`: `+=` "goes up by", `=` "becomes"; `alone` on `++` and `--`, which take no value); a call's
list label is "argument". `tide/graphMembers` takes `{"name": "Math"}` as well: what the name itself has after
a `.`, an enum's choices or a namespace's functions (`{"type"}` is what a *value* of the type has, which for
those is nothing).

**`tide/graphCalls`** (2026-11-25): `{"<call's node>": {"params": ["value", "min", "max"], "types": ["float", "",
""], "args": [0, 1, 2]}}` for every checked call of the document: the callee's parameters' names
(`call_param_names`, as inlay hints have them: the game's function's or command's, or the built-in form the call
took), their types where the game declares them (empty for a built-in's), and the parameter each written argument
goes to. Calls with no names to show (`Spawn(...)`) aren't in it.

**`tide/graphNames`** (2026-11-25): `{"game": {"Ball": "component", "Heal": "function"}, "packages": {...}}`,
what the game itself declares (and its packages), by name as the document writes it (`name_for`), with its
keyword. Completion doesn't say whose a type is, so the pickers' "In this game" listed ViewTime and Random; what's
offered and isn't in this is the engine's. Systems, views, handlers, shaders, methods and generics' instances
aren't in it.

**More of the wording in the schema** (2026-11-25, what the web page had made up, so the Unity window says the
same): `flagInfo` (a node's own flags: label, help, advanced), `portInfo` (what an input or an exec output is
called on its node: `object` "of", `lhs` "a", `cond` "condition", `else` "otherwise", `body` "do"; a key with no
entry is shown as it is), `sentence` and `addRow` on members with code, `accessorInfo` (get "When it is read",
set "When it is set, with"), `addRow` on every kind with rows ("+ field", "+ choice", "+ setting", "+ needs…",
"+ input…"), `valueControls` (which control edits which types' values; an enum's is a choice),
`typePicker.groups` (the picker's group titles) and a builder's `counts` (its slots that are a number). Which
inputs must be wired isn't in it: the check says so (a hole's `data.port`).

**In the schema for it** (2026-11-24): declarations' `group` (Data, Behaviour, More; `groups` has their titles),
`summary`, `common`, `memberKinds`, `sentence`, and keys without what a kind can't have (no `typeParams` on a
component, no `returnType` on a system); members' `summary`, `once`; `keyInfo`, `modifierInfo`, `paramModeInfo`
(label, help, advanced); `typePicker` (common, builders); each node's `help`; `x`, `y` after `id` in the keys of
every declaration but a namespace, of members with code, and of accessors. tidec: `graph_layout_board`
(`graph_layout.c`), which the converter places every card and frame with, and `graph_schema_hold`, which reads the
schema once for many nodes.

**Round three: like Blueprint** (owner, 2026-11-25, after building a system in the Unity window that scales a
transform by a component's `frequency`). What went wrong, in the owner's words and pictures: a node
(`transform . localScale`) sat on top of the Set node, and neither it nor any node could be moved; "they overlap,
i can barely link and its very unclear that I can even link it"; the Set ended as `transform *= frequency`
("operator '*' can't be used with Transform and float") because nothing stopped it; on a component's card "i kept
adding a new field by accident", "it barely has any space to click and drag without doing something; the name
should likely be a double click to rename everywhere, the only place I can drag it is on the top left chip"; the
card's menu ("This machine only", "Work out its starting value…", "Add an attribute…", "Note…", Move up/down,
Duplicate, Remove) "is daunting and im not sure even I understand it all"; "we should take inspiration from
unreals blueprint system"; "right click menus are a mess and have no consistency" (a frame's menu had Add a node,
Tidy up, OPTIONS, MORE, Duplicate, Delete; a card's Remove). The design both editors build (my picks, Provisional):

- **Everything moves by dragging it.** A node, a card or a frame is dragged by any part that isn't a control: its
  title bar, its kind chip, its name, the space around rows. A click selects; a drag on the empty board draws a
  selection box; Shift or Ctrl adds to the selection; a selection moves, copies and deletes together. A move is
  an edit (`x`, `y` written, undone like any other). A node stays in its frame (the frame grows to hold it:
  left and up too, below).
- **Names are renamed by a double click** (or F2, or Rename in the menu): a card's, a frame's, a field's, a
  parameter's, a local's. A single click on a name selects and starts a drag. A new thing still starts with its
  name being typed.
- **Nothing is placed on top of anything.** What the editor places (a new node, a pasted one, one "Work it out…"
  made, a frame or card added) goes into free space near where it was asked for. A node shown in an input is
  never drawn as a node as well.
- **Pins are always there.** Every input has its pin at its left, hollow when empty and filled when something's
  in it (a value, a chip, a wire); every value its pin at its right; exec pins are arrows. A simple value stays in
  the input beside its pin. A pin under the pointer says what it is and "drag to connect". While a wire is
  dragged, the pins it can go to are lit and the others dimmed (by type where known; unknown fits); dropped on
  an input it fills it (replacing what was there); dropped on empty space it opens the node search for what fits
  there, like Blueprint's context-sensitive actions (with "Show everything"). Wires are easy to hit; Alt+click on
  a pin breaks its links.
- **One menu, the same everywhere.** A right click on a thing and its "…" button open the same menu, in the same
  order for every kind of thing, leaving out what doesn't apply: (1) what can be added to it ("Add a field",
  "Add a method"; a frame's "Add a node… Space"), (2) Rename F2, (3) Cut, Copy, Paste, Duplicate, Delete with their
  keys, (4) Note…. The same words everywhere ("Delete", never "Remove"). Options, attributes and a worked-out
  starting value aren't in menus: they're in Details. A right click on an empty board or an empty part of a frame
  opens the add search for that place; on a pin: Break links, Pick a value…, Work it out….
- **Details** (Blueprint's Details panel), docked on the right, for what's selected: its name, type and starting
  value; its kind's options as ticks, each with its help written under it (`modifierInfo`); "Advanced", closed,
  for the rest (packages can replace it, replaces another, written in C, attributes); its note. With nothing
  selected, the file's (namespace, uses). It can be closed and stays as left.
- **My game** (Blueprint's My Blueprint panel), docked on the left, in place of Contents: the file's
  declarations by the schema's groups, a "+" on each group's heading that adds one of its kinds, a click pans to
  it, a double click renames. Dragged out: a component or singleton onto a frame's entry adds it as a need; a
  function into a frame makes its call; a constant onto an input fills it.
- **A card's rows have room**, and its "+" is a button on its title bar ("Add a field"), not a row under the last
  one; Enter in a row still makes the next.
- **Only what's valid in a Set.** "What" offers what can be changed (a parameter taken as "changes", a local, a
  field of one of those), not what's only read; the operator offers only operators valid for the target's type
  and the value's (`tide/graphOperators`, below); "Work it out…"'s operators likewise.

**`tide/graphOperators`** (2026-11-25): `{"type": "float3", "other": "float"}` gives `{"assign": ["=", "+=",
...], "step": ["++", "--"], "binary": ["+", "*", "==", ...], "unary": ["-"]}`: which of the schema's operators the
checker takes for a value of `type`, with a value of `other` (of `type` when it's left out) on the right. tidels
reads the graph with a function after it whose statements each use one operator, and leaves out each one with an
error on its line (`mut var _graphC = _graphA;` then `_graphC op= _graphB;`, `_graphC++;`, `_ = _graphA op _graphB;`,
`_ = op _graphA;`). `null` when the type itself isn't known there, or isn't a type's text (then offer them all).
A Set whose value isn't picked yet asks without `other`; with one, `=` is there only if the value fits (`float3`
with a `float`: `+=`, `-=`, `*=`, `/=`, no `=`). A component (`Score`) gets `=` alone, with a `float` nothing. A graph
that doesn't parse isn't checked, so then everything is said to fit.

**Round three built** (2026-11-25, both editors). As above, with these details and picks:

- Why the owner's `transform.localScale` floated over the Set, unwired: in both editors a node made from a pin's
  drop (or a search) went exactly under the pointer, and a wire dropped on a node's body counted as empty space.
  In Unity the Set's "what" picker also only offered what fitted the type already in it, so once it held
  `transform`, `localScale` (a float3) was hidden behind "Show what doesn't fit too". Now every placement looks
  for free space near where it was asked for (`freeSpot`), a wire dropped on a node goes to its nearest input
  that fits, and a Set's target has no wanted type. The exact clicks that left the owner's path unwired weren't
  reproduced.
- A Set's "What does it change?" lists what's taken as "changes" (`mut`, `out`), locals declared `mut` and their
  fields; what's only read is listed greyed under "Only read here", with how to make it changeable; a read-only
  pin dropped on it is refused with that reason. When a value is picked that the current operator can't take
  and others can (a float into a float3 that "becomes"), the operator menu opens by itself once a check has the
  value's type (at most three checks; never on a check of the file from before the pick).
- The web page renames what the file says of a declaration with it (a field's type, a need, a call); the Unity
  window doesn't yet (the check says where). A problem's words under a still pointer go when the next check
  doesn't have it (web: `markProblems`; Unity: through `TooltipView.Close`).
- The schema says `advanced` on `modifierInfo.virtual` and `override` (Details put them among the main ticks).
- Tests: `tide_graph_editor` 29 (model and browser: moving with undo, rename by double click, nothing placed on
  top, every menu's sections in order with no options, Details, My game, the owner's system with tidels and the
  operator menu opening by itself); the Sample's graph fixtures 47 (46 passed and the pointer test skipped under
  `-nographics`; on the hidden desktop the pointer test passes); tide-vscode 38; JetBrains 18 (`TidelsServer`
  now passes `graphCalls`, `graphNames` and `graphOperators`); the docs playground 8.
- Wanted of the schema, said by both: the menus' words (`menu.<key>.{label, keys}`: rename, cut, copy, paste,
  duplicate, delete, break links, pick a value, work it out, add next/before), an "add" label per member kind,
  the panels' words (My game, Details, Advanced, This file, Uses, What this code can change, Only read here),
  `trigger.changed`'s words, and which parameter modes can be changed (`paramModeInfo.<mode>.changes`); until then
  each editor has these words, the same in both.
- Not there: wires can't be selected in Unity (Alt+click a pin); rows reorder from Details (Earlier/Later), not by
  dragging; Paste is always listed on the web (the clipboard can't be read without asking); My game's drag-out
  has no Unity test.

**Later the same day** (the owner: "it annoys me i cant drag this wherever i want", and of a Set's chips "could we
allow to clear these fields?"):

- A dragged node follows the pointer anywhere. Left of or above the node area, the frame grows that way: its own
  place on the board moves by the overshoot, so nothing else on screen moves, and the entry moves with the
  frame's corner. The nodes' and the frame's places are one "Move" edit. Unity had clamped a node at the area's
  left and top (`GraphBoard.OnMove`); it's `GraphFrame.Follow` now, and neither the board's placing nor a layout
  pass runs mid-drag. The web page pushed the other nodes right instead.
- An input is emptied by the × shown on it under the pointer, by Delete or Backspace with the pointer on it,
  and by "Empty it" in its pin's menu: what was shown in it (a path's nodes, a literal) goes, a node wired in
  stays, unwired; one edit, and the input says "pick…" again. The web page's Break links used to delete a node
  wired in when it was a kind that can be shown in an input; now only what was shown there goes, as in Unity.
- Tests: `tide_graph_editor` (a Set dragged above and left of the entry; a chip emptied by ×, a value by Delete,
  a chip by Backspace and a wired input from the menu, each undone), Unity's `ANodeGoesWhereverItsDragged`,
  `AnInputIsEmptiedByItsXByDeleteAndFromItsMenu` and the pointer test
  `ThePointerDragsANodeAnywhereAndDeleteEmptiesWhatsUnderIt`, both over `tools/tide-graph/test/spin.tidegraph`.
  Unity's graph fixtures 51: all pass on the hidden desktop, 49 and the 2 pointer tests skipped under
  `-nographics`.
- Not there: a node's input row can't be selected, so Delete works on the input under the pointer; "Empty it"
  and the × words are each editor's own until the schema has the menu words.

## Round four: full Blueprint (owner, 2026-11-25)

The owner, of a variable shown in a Set's input: "why is it that a variable is embeded in the node? why not allow
a link such that one variable can be linked to multiple places? is this how blueprints handles it?", then, on
being offered chips kept beside Get nodes or Blueprint's way: "we should go full Blueprint, if anything im ok
with a 'clone'". So variables are nodes, as in Blueprint, and the editors follow Blueprint's conventions where
Tide has the same thing. The file doesn't change: tidec already writes a node per name and per member, and a node
wired to several inputs is worked out at each (Blueprint's pure nodes). What follows are my picks within that
(Provisional).

- **What never changes is in the pin; a variable is a node.** A pin holds what Blueprint's pin defaults hold: a
  literal typed there (number, tick, text, char, `default`, `null`, a `-` number, a vector or colour of numbers)
  and, as a chip, a name in PascalCase or FULL_CASE (a type, a namespace, an enum, a constant: Tide's naming,
  spec) with the members of one (`Math.PI`, `Phase.Running`, `Color.red`, a shorthand `.Run`); `this` is shown
  "self". Every variable read (a parameter, a local, a field, a path of members from one) is a node: the schema's
  `inline` lost `name`, `local` from `always`, and has `named` (a `name` starting with a capital). tidec's layout
  (`graph_layout_fold`) follows the schema, so converted files now give Gets room, left of their readers.
- **Get nodes are compact**: no title bar, the variable's name and its value pin, in its type's colour. A member
  is a compact node too, its field's name between a **Target** pin (what it's a member of, wired in) and its
  value pin. A Get's value pin is wired to any number of inputs. Hovering a Get lights its declaration and its
  variable's other Gets.
- **Set is titled by what it changes**: "Set score"; "Set scale" with a Target pin for a member. What a Set (or a
  step, `++`) changes is drawn in its title when it's a name or a member read by it alone (schema
  `inline.titled`, which tidec's layout folds the same way); the title's name opens the picker of what can be
  changed. The operator stays on the Set (Tide's `+=` and the rest, from `tide/graphOperators`), then the value.
  A target read by other nodes too stays a node, wired into the Set's "what" pin.
- **Calls**: a method's object is its Target pin (`ball.Bounce`: "Bounce", Target wired from a Get of ball); a
  call on a type or namespace keeps it in the title as Blueprint's "Target is Math" line ("Clamp", "Math"
  beneath), as that name is a chip folded there.
- **Reroutes**: double-clicking a data wire puts a knot there (a `reroute` node: one input, one value pin, which
  fans out like any). It's dragged like a node; deleting it joins its input to its readers. tidec lowers a knot
  to what's wired into it (`graph.c`; `tidec_e2e_graph_nodes` has a knot before a `-` over a `-`, one wired to two
  inputs and one from a kept statement value). Exec wires have no knots.
- **Making a Get or a Set, Blueprint's ways**: My game lists the frame's **Variables** (its parameters and its
  locals, each opening to its fields); dragging one onto the board asks "Get <name>" or "Set <name>", Ctrl-drag
  makes a Get and Alt-drag a Set at once, and one dropped on an input is a Get wired to it. Dragging from an
  entry's parameter pin and dropping on an input makes a Get of it beside the input, wired. The value picker's
  "what this code has" makes the Gets (`ball.position.x`: Get ball, position, x) left of the input in free space,
  wired. A wire dragged from a value pin into empty space lists its fields first (Gets with it as Target); one
  dragged from an input lists the variables of the input's type first.
- **Promote to a local**: a worked-out node's value pin's menu has "Keep it as a local" (Blueprint's Promote to
  Variable): a Local declared before the statement that first reads it, its value that node, and Gets of the
  local where the node's wires went, so it's worked out once.

**Round four built** (2026-11-25, both editors and tidec). As above, with these details and picks:

- tidec: `graph_layout_node.target` is what's wired into a node's `target` key; the title fold takes it (not the
  first wired input, which skips holes) and a `name`, `local` or `member` read by it alone ("Set speed" for a
  local). A folded node's wired inputs take room (`place_value`, `value_width`), so a titled member's object Get
  doesn't land on the Set's value. `graph_layout_measure` sizes a Get (`name`, `local`, `member`, `this`) 48 + 8
  per character (at most 200) + 60 for a member's Target pin, by 32, and a knot 34 x 18; `Doc.sizes` and Unity's
  estimate mirror it.
- Words: "Target", "self", "Get <name>" / "Set <name>" (Ctrl and Alt as hints), "Variables" (with the frame's
  name; "Select some code to see its variables" when none is), "Keep it as a local" ("Worked out once, before the
  code that reads it"), "Add a reroute" and "Break this link" on a wire, "What does it change?" from a Set's
  title. The search also offers "Set <name>" as a step for what can be changed; "Set" is greyed, with the reason,
  for what can't.
- Keep it as a local: the Local goes before the first statement that reads the value, in the innermost block
  holding all its reads, and takes that statement's place (what was right of it moves right); its name is then
  typed. It's refused, with the reason, when nothing reads the value, when a statement only runs it for what it
  does, or when it reads a local made later.
- Picking another value for an input removes the Gets only it used; emptying it keeps them, unwired (Unity fades
  an unwired Get, as Blueprint does). "Work it out…" wraps what was in the input: it becomes the new node's first
  input.
- New Gets can go left of the entry: the frame moves its place as a drag there does. `docs/playground/stars.tidegraph`
  and `tools/tide-graph/test/spin.tidegraph` are laid out again by tidels (their Tide byte-identical);
  `docs/tools/visual-scripting.md`, `docs/guide/graph-playground.md` and the editor's README say it.
- Tests: `tide_graph_editor` (model 22: the fold as tidec's, Gets, titled Sets for fields and locals, a reroute,
  Keep it as a local compiled by tidec; browser 10: two new over items above with undo), docs graph playground 8,
  tide-vscode 38, JetBrains 18; Unity's graph fixtures 59, all on the hidden desktop, 57 and the 2 pointer tests
  skipped under `-nographics` (new: `VariablesAreGetsWiredToAnyNumberOfInputs`, `CallsSayWhatTheyreOf`,
  `ARerouteIsAKnotOnAWire`, `AWorkedOutValueIsKeptAsALocal`, `MyGameListsTheVariablesToGetAndSet`,
  `AWireFromAnInputListsItsTypesVariablesFirst`, `BlueprintsWaysAreValidTide` (no overlaps after Tidy up),
  `ALocalsSetIsTitledByIt`, `ConvertedGamesShowOnTheBoard` (sumo and towers: shown in an input or a title
  exactly where tidec's layout puts it at its reader)).
- Not there: a Get can't be pointed at another variable in place (a double click on its name renames what it
  reads, not the variable); Keep it as a local isn't offered on a call drawn as its own statement; no Unity test
  drags a My game row with the real pointer (its drop is tested by calls); files laid out before this round have
  Gets where chips were until "Tidy up its nodes"; the new words are each editor's own until the schema has the
  menu words.

## Round five: one look (owner, 2026-11-25)

The owner, on pictures of the two editors side by side: "why do both views vary so much, they are meant to be the
same system", then "we should do an effort to keep them similar if not identical". They shared the schema and
tidels but each drew its own look and said its own words, and no test compared them. So (my picks, Provisional):

- **The web editor's look is the reference** (closer to Blueprint: compact Gets with "Target", dark headers, the
  dotted board). The Unity window changes to match it.
- **One look, in the schema**: `look` holds the colours (board, grid dots, frame, node body and header per
  category, pins, wires, selection, problems; light and dark), sizes (font sizes, row height, pin size, radii,
  gaps, paddings, a Get's and a knot's size), and the font. Neither editor hard-codes them. Both draw with the same
  font, shipped with each editor (Inter, OFL), so text measures the same.
- **Every word in the schema**: `words` holds the toolbar, the hint line, menus (`menu.<key>`: label and keys),
  panel titles and empty states, pickers' titles, tooltips: what was each editor's own.
- **The same features**: each editor has what the other has (the web's zoom buttons, undo and redo buttons and
  Find in Unity; whatever the Unity window has that the page hasn't).
- **A parity test**: each editor writes what it drew for the same graphs (`tools/tide-graph/test/drawn/`): every
  frame's and node's box on the board, its title lines, its rows (label, pin kind and colour, what's shown in
  it), its header colour, and each wire's ends and colour. The web test writes the expected files (with an
  environment variable) and checks against them; the Unity test checks against the same files. Boxes match
  within a few pixels, text and colours exactly.

**Round five built** (2026-11-25, both editors). As above, with these details and picks:

- Schema: `look` has `font` (Inter regular and bold, its licence, and `code`, a monospace stack for code nodes),
  `colors` (28, each `{dark, light}`), `headers` (a title bar's colour per category, and `entry`), `types` (pin and
  wire colours by type, and `typeHue` for any other: a hue from a hash of the name at saturation 55, lightness
  58), `sizes` (31, px). `words` has every word the editors say (458, `{name}` fill-ins; a menu entry is `{label,
  keys}`, a tooltip `<key>.tip`, "How the board works" `help.1`...). The banners shown before there's a schema
  ("Getting ready…", the two "can't start") stay in each editor's code.
- Web: the board takes only dark or light from the host (VS Code's body class or the theme message), not its
  colours or font. Inter ships in `tools/tide-graph/fonts/` with its OFL licence; JetBrains inlines the fonts as
  data and the docs playground points them at the site's assets (both put the CSS inline). An older tidels'
  schema without `look` or `words` keeps the host copy's.
- Unity: `GraphLook.LookSheet` builds a sheet of `--tg-*` variables for the skin from the schema (with the page's
  `color-mix` and `em` sizes worked out), through Unity's internal `StyleSheetImporterImpl` by reflection, as the
  UI Builder does: there's no public way to make a sheet from text or set a variable on an element.
  `GraphBoard.uss` is layout alone. Without the importer, the window loads `GraphLookDark.uss` (the dark look,
  shipped) and logs one error; `TheLooksSheetIsMadeFromTheSchema` checks both looks are made and that the shipped
  file is the schema's dark (it writes the right one when not). Inter ships in `Editor/Graph/Fonts/`; glyphs Inter
  lacks (⋯, ▾) fall back to Segoe UI Symbol, as Chrome does.
- Each has the other's: Unity got Find… (Ctrl+F, "/"), undo and redo buttons (Unity's Undo, always enabled as its
  stack is the editor's), zoom buttons and + / −, right-drag and wheel to look around and Ctrl+wheel to zoom,
  the problem chip's words and colours, problem counts on title bars and My game, "Deleted X" with Undo, the
  namespace's "Delete with what is in it", drop targets lit while dragging from My game. The page got "How the
  board works" and Save in the board's menu, problem words written out (under a row, at a node's foot, above a
  frame) and the list grouped (Problems, To look at, Notes), typed names cleaned up ("my speed" → mySpeed), a new
  field taking the type of the one before, Alt+click on a row to empty it, My game's hover and search behaviours,
  Details' "Owning it, not having a component" and "Nothing of its own" ticks, "Add get" / "Add set" on a property
  and "Add what the query needs…". Unity lost what the page hadn't: attributes on a card row's own line (Details
  has them), the ▾ on a wired input, a value input on a Return in code that returns nothing.
- Parity: `tools/tide-graph/test/drawn/` (format in its README: rows top to bottom then left to right, pins within
  6 px one row; a chip one word; the port names) has `spin`, `shapes` (`test/shapes.tidegraph`, one of each shape)
  and `stars` (`docs/playground/stars.tidegraph`), drawn with no language server (pins and wires coloured only
  where the file says a type; arguments "argument 1"...). The browser test writes them with
  `TIDE_GRAPH_DRAWN_WRITE=1` and otherwise checks the page against them; Unity's `TheBoardDrawsWhatThePageDraws`
  (`GraphDrawn.cs`) checks the window against the same files, boxes within 4 px and the rest exactly (skipped
  without a graphics device, so on CI's `-nographics` runs; it runs on the hidden desktop). A web bug it found: "+
  argument" wrapped on a node 113 px wide (`white-space: nowrap` now).
- Tests: `tide_graph_editor` (model 23: every word the code asks for is in the schema, every colour has both looks,
  the JetBrains copy is the same; browser 12: the look, the words, the features from Unity, the drawn files),
  docs playground 8, tide-vscode 38, JetBrains 18 (its theme test: dark or light only, fonts inlined); Unity's
  graph fixtures 62 on the hidden desktop, 59 and 3 skipped under `-nographics`.
- Not there: the page has no "not saved" marker in its toolbar or View as Tide as a panel (the host's); Unity's
  undo names and some popup texts have no schema words yet; the drawn comparison doesn't run in CI (no graphics
  device there); the light look is compared by pictures only.

## Rows in columns (owner, 2026-11-25)

The owner: "where we display the types on the definitions or usages we should try to align them so they are
easier to read", then "that applies to almost all rows. if we can, we should align". Built in both editors (my
picks, Provisional):

- **The rows of a box are columns**: each as wide as its widest cell in that box (measured from that box alone, so
  a box of short names stays small), left-aligned; a cell with something in it starts `look.sizes.columnGap` (8
  px) after the column before it; an empty cell takes no room, and a column empty on every row adds nothing; the
  parts of a cell are 5 px apart. A + button, a problem line or a heading spans the box.
- Card members: name (and a `tweak` or `local` tag) | type | value; a property's tag and its get/set links where a
  value goes; a method "Turn" | `method`; enum choices and settings name | value. An entry's and a Takes' rows:
  mode (with `local`, `verified`, `previous`) | type (with `scene`) | name (with a default) | pin; the trigger,
  payload, a setter's value and "result" in the same columns; a query's own rows a box of their own, 18 px in; a
  sample's, with no mode, from the type. A node's inputs and a call's arguments: pin and name (a Set's operator in
  the name's column) | what's in it | ×, +. A node's own named fields: name | control. My game: name (a nested
  row's indent inside it) | kind or type | problems. Details: one label column, as wide as the panel's widest
  label (a closed Advanced's too, so opening it moves nothing), at most 45% of the panel.
- Left as they were: a Local's title tags, a lambda's parameters (one line), the pickers' lists (types already at
  the right edge, and a summary line), a constant's or an asset's card, a prefab's rows.
- Unity lines them up with `GraphColumns.cs` (USS has no grid: a box is measured after layout and its cells'
  widths set).
- The drawn files have `columns` (per aligned box of a card or node, where each visible column starts), compared
  within 4 px; the web's drawn test also fails if a cell is off its column's x (board, My game, Details), and
  Unity's `ThePanelsRowsLineUp` checks My game and Details.

**Nodes never overlap after a layout** (found by the drawn files): tidec's layout started the next statement
after the previous one's node, not after the code it holds, so its values lay on an if's body; and its sizes are
estimates. Now `place_chain` goes on past a statement's bodies, and `tide/graphLayout`'s graph takes `sizes`
(`{"<id>": [w, h]}`, the editor's drawn sizes, over tidec's estimate); both editors' Tidy up send every node's
drawn size (`lsp_graphs` checks both). spin, shapes and stars are laid out again with drawn sizes (Tide
byte-identical), and both drawn tests fail if two nodes of one frame overlap.

Tests: `tide_graph_editor` (model 23, browser 12), docs playground 8, tide-vscode 38, JetBrains 18; tidels_tests
`lsp_graphs`; Unity's graph fixtures 63 on the hidden desktop, 59 and 4 skipped under `-nographics`.

## A package newer than its tide; the browser tests on CI (2026-11-26)

The owner's Unity project (the package from git `#dev`) showed every word as its key ("toolbar.add") with no look:
the package's version lookup gave the newest release, 0.9.0-nightly.7, from before the schema had `words` and
`look`. No release had been published after it: every Build on `dev` since round two failed, on our tests.

- **The package ships the schema** (`unity/com.tide.unity/Editor/Graph/graph_schema.json`, the compiler's: the model
  test checks it is the same, as the JetBrains copy). `GraphSchema` takes the tide's schema for the language and,
  where it has none, the package's `look`, and each word the tide's schema hasn't (`WordOf`). Unity's
  `AnOlderTidesSchemaTakesThePackagesWordsAndLook` adopts a schema without either.
- **macOS:** `tide_graph_editor`'s browser tests ran slower and slower after the first (a test of 0.6 s here took
  189 s, then the suite reached ctest's 600 s): steps that wait for the next frame or a timer (a name's focus, a menu)
  timed out. The headless Chrome was throttled as a covered window. `browser.mjs` launches it with
  `--disable-background-timer-throttling`, `--disable-backgrounding-occluded-windows` and
  `--disable-renderer-backgrounding` (as Puppeteer does); here too, the browser tests took 32 s before and the whole of `tide_graph_editor` 14 s after.
  It also intercepts the browser's own drag and drop (`Input.setInterceptDrags`), which on macOS would hold the
  browser, and fails a test that starts one.
- **Windows** failed now and then on `p.text` read straight after an edit (the host's queue set it after earlier
  messages: now at once) and on a frame's error count, put on with the next frame (waited for now).
- **Docs / Examples:** the stars playground check counted values in inputs; a variable is a Get node since round
  four, so it wants more than 5 (it has 9), not more than 10.
- The undo note ("Deleted …") is 54 px from the bottom in both editors, above the zoom buttons a narrow board has
  under it.
- That run still failed. On Windows a problem count stayed in **My game**: it wasn't drawn again while its search
  had the cursor, and nothing drew it later. Now `MyGame.refresh()` draws it unless something else of it (not its
  search, which is drawn again with the cursor kept) has the cursor, and then once that lets go. macOS still failed
  in later tests at their first click or focus, unthrottled: `clickOn` now fails when what it aims at is under
  something else, and a wait that times out says the page's size, focus, what has the cursor and what's open.
- **Linux** jobs (web shard 3, sanitize, Android) were stopped by the runner: `tidec_graph_roundtrip` ran out of
  memory (on Linux under an 8 GB cap: out of memory in 19 s; 12 MB per file). Every lookup of the schema's `inline`
  and `titled` lists and of a node's ports with it not held parsed the whole embedded schema again, into an arena
  that one tidec run never lets go of; round four's layout looks some up for each node, and round five's `words` and
  `look` doubled it. The schema is now read once into memory kept for the program (`arena_keep` in common.c: blocks
  of its own, which no `arena_reset` or rewind lets go of, so tidels keeps it across analyses). All files in one
  tidec still took 5.5 GB (the compiler lets go of nothing between files, and can't: the lexer's state outlives a
  rewind, as ASan said), so `graph_roundtrip.cmake` gives tidec 200 files at a time (about 1 GB each on Linux).
- The macOS messages then said it: tests weren't apart. A page remembered the test before's panels (My game open
  over the card a test clicked), a test's 2100x760 stayed for those after it when it failed, and the page became
  hidden (no frames drawn, so no focus put). `openEditor` now empties the page's storage, puts back the browser's
  size and brings the page to the front before each page, and the browser has focus emulated. The JetBrains job ran
  out of Gradle's default 512 MB compiling its tests: `org.gradle.jvmargs` gives it 3 GB.
- Then Linux, Android, sanitize and JetBrains passed. Windows' last count was My game's: it counted warnings with
  errors and drew them red, where a title bar (and Unity's My game) says the errors in red, else the warnings in
  amber; now it does the same (`problemsIn(item, warnings)`). macOS passed every browser test but the Add menu's in
  32 s, and then never ended: `tide_graph_editor` runs with `--test-force-exit`, and the browser's output is let go
  of when it's closed. The Add menu test brings a card into view before its menu (`app.reveal`).

## Making an engine's value: float3(…) (2026-11-27)

The owner, on Unity's board: searching `float3` made a name node that only said "unknown name 'float3'", and wiring
it into `quaternion.Euler`'s "argument 1" did nothing. Two causes, both fixed:

- **The search took a built-in type as a name** (Unity, `Name("Types", ...)`) or a struct's literal `float3 { }`
  (the page's `entryOf`), neither of which Tide has. Completion's `built-in type` items are now a call of the type:
  `float3(…)` makes `float3(0, 0, 0)`, a node whose numbers are in its x, y, z inputs (`fill`: 2 to 4 zeros for a
  `float`/`int`/`double` vector, `1.0` x 3 for `Color`), and any other (`float`, `quaternion`, `Rect`) a call with one
  input to pick and "+" for more. The game's structs, components and events stay literals.
- **An engine's call said no parameter types** (`tide/graphCalls` gave `""` but for the game's functions), and none
  at all while an input wasn't wired (the checker doesn't take `quaternion.Euler(_graphHole)`, so it isn't in
  `A.calls`), so its input was "argument 1" of no type and the value picker had no `float3(…)` to offer. Now a checked
  built-in call's types are what it took its arguments as (`arg_want`), else its form's (`float3(float x, ...)`), and
  a call the checker couldn't take is answered from the engine's signatures with as many parameters as it has
  arguments (`unresolved_param_names`: names from the first that fits, a type where all that fit agree), so
  `quaternion.Euler`'s input is "radians", a `float3`, with `float3(…)` first in its picker. `graph_types` walks with a
  `visit` for this.

Tests: `lsp_graphs` (`Math.Max(1, 2)`'s types are `int`), the page's model test (Euler with its input empty and
wired, `float3(1, 2, 3)`'s types, `entryOf` of built-in types), `GraphLanguageTests` (the search's `float3(…)`, no
`float3` name; not run here: it needs a scratch tide and a batch Unity). The JetBrains snapshot of `app.js` and `lang.js`
is refreshed.
