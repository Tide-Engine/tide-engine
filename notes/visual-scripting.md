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
| `name` | `name` | `speed`, `MAX_HEALTH`, `Combat`, `Math` |
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
| `eval` | `value`, `block` (exec), `stacked` | `value;`, or a call's block, `value { block }` |
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

`eval` with a `block` is a call that takes a block, `GUILayout.Vertical() { ... }`. `stacked` writes it as one of
the calls listed before one block, `Draw.Blend(Blend.Add), Draw.Clip(r) { ... }`, the next of which is its block's
first statement. An `eval` whose value another node reads is a statement whose value is kept: tidec writes
`var <name> = value;`, the name its own (`tide_graph_<id>`), and reads that.

`text` and `code` read their inputs by name. tidec reads the text as Tide tokens and puts each input, in
parentheses, where its name stands as a name (not after a `.`). `code`'s locals are its own: what comes after
reads them only through the text.

### Diagnostics

A graph's errors name the element they're about: `Assets/Enemy.tidegraph:@17: error: ...`, where 17 is the id
of the innermost node or element whose text the error is in. After it tidec shows the line of the Tide it wrote,
which is the same Tide a `.tide` file would hold. The Unity package and the language server read the id.

## Implementation

- `compiler/src/graph.h`, `graph.c`: reading a `.tidegraph` (its own JSON reader, arena memory) and writing its
  Tide (`graph_lower`), with a map from each stretch of the text to the id it came from.
- `compiler/src/graph_convert.c`: a `.tide` file to a graph, from its syntax tree as parsed (`graph_from_tide`),
  with its layout.
- `tide graph <file.tide>` writes `<file>.tidegraph` beside it; `tide graph --text <file.tidegraph>` prints its Tide.
