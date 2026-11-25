# What the graph editors draw

Every graph editor (this page, which VS Code, JetBrains and the docs show, and Unity's window) draws the same graph the
same way: the look and the words are the schema's (`look`, `words` in `compiler/src/graph_schema.json`). These files
say what was drawn for the fixtures, and each editor's tests check what it draws against them:

| File | Graph |
|---|---|
| `spin.json` | `tools/tide-graph/test/spin.tidegraph` |
| `shapes.json` | `tools/tide-graph/test/shapes.tidegraph`: one of each shape (a card, Gets fanned out, a member's Get with its Target, a local's Get, a Set titled by a field, one by a local and one whose target is wired into "what", a method on a Target, a call on a type ("Clamp" over "Math"), a reroute, values in pins, a vector, an enum's choice, `self`, an if and a foreach) |
| `stars.json` | `docs/playground/stars.tidegraph` |

The web editor writes them: `TIDE_GRAPH_DRAWN_WRITE=1 node --test --test-name-pattern=drawn tools/tide-graph/test/browser.test.mjs`.
Without the variable the test checks the page against them. A box may be 4 px off; everything else must be the same.

## The format

Drawn with no language server answering (as the browser test runs the page): a type is known only where the file says it (a parameter's, a local's declared type, a literal's), so other pins and wires take no type colour, and a call's arguments are `argument 1`, `argument 2`... The board as drawn in dark, at zoom 1, in board coordinates (x right, y down; a card's or a frame's `x`, `y` in the
file is its top left), every card and frame drawn near:

```
{"graph": "<the fixture's file name>",
 "frames": [{"id": <declaration id>, "box": [x, y, w, h], "chip": "System", "title": "Spin"}],
 "cards":  [{"id": <declaration id>, "box": [x, y, w, h], "chip": "Component", "title": "Wobble",
             "rows": [{"label": "frequency", "shown": "float"}]}],
 "nodes":  [{"id": <node id>, "frame": <declaration id>, "box": [x, y, w, h], "shape": "node" | "get" | "knot" | "entry",
             "header": "#rrggbb" | null, "title": ["Set phase"],
             "rows": [{"label": "Target" | "", "pin": "in" | "out" | "exec-in" | "exec-out" | "none",
                       "color": "#rrggbb" | null, "shown": "<text shown in it>" | null}]}],
 "wires":  [{"from": [<node or declaration id>, "<port>"], "to": [<node id>, "<port path>"], "color": "#rrggbb"}]}
```

- `frames`, `cards`, `nodes` are sorted by `id`; `wires` by `to`, then `from` (an id as a number, a port as text).
- A frame's `box` is all of it, its title bar too; its `chip` is its kind's title as the schema has it (`System`), its
  `title` its name. A card's `rows` are its fields (or choices, or settings): the name, and the type shown (`null` when
  there's none, as for a choice).
- A node is each node drawn on the board: what's shown in an input (a literal, `Math.PI`, what a Set changes) and a
  call drawn as its statement aren't nodes of it. Its `shape`: `entry` (the frame's first node, its `id` the frame's),
  `get` (a variable: its name and its pin), `knot` (a reroute) or `node`. `header` is its title bar's colour (the
  look's `headers` by its category, or `entry`), `null` for a Get and a knot.
- `title` is its title's lines: the words of the title bar, each part once and joined by a space (`Set phase`,
  `Multiply *`, the entry's sentence, a Get's name), and a second line when a call is on a type or a namespace
  (`["Clamp", "Math"]`). A knot's is empty.
- `rows` are its pins, top to bottom and then left to right as they're seen (pins less than 6 px apart up or down are
  one row): an input's `label` is what the input is called, or the operator shown in its place (a Set's `=`); an
  output's is `""` but for a local's pin (its name). `pin` is its kind; `none` is the entry's row of a filter, which has
  no pin. `color` is its type's colour (the look's `types`, else the hue of the name; `null` while the type isn't
  known, and for an arrow). `shown` is what's in it: a number, text (without its quotes), `true`/`false` for a tick, a chip as
  one word (`Phase.Running`), a vector's numbers joined by spaces, `pick…` for nothing yet; `null` for a wire or an output. The
  entry's rows are what it takes: `label` its name, `shown` its type.
- A data wire goes `from` a node's `value` `to` the input's path (`args.0`, `object`, `target`); one an input's value
  is shown in isn't drawn, nor the wire from a call to the statement it's drawn as. An arrow goes `from` a statement's
  exec output (`next`, `then`, `body`) `to` the statement it runs, port `in`; the entry's is `[frame id, "start"]`.
  `color`: a data wire's value's type's, else its source's category's; an arrow's the look's `exec`.
