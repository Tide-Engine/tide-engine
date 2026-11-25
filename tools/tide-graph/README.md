# Tide's graph editor

The editor of `.tidegraph` files (docs/tools/visual-scripting.md, notes/visual-scripting.md) that VS Code
(`tide-vscode/src/graph.js`, a custom text editor), JetBrains IDEs (`tide-jetbrains`, `TideGraphEditor`, in
JCEF) and the docs' graph playground show. One page, written once: plain classic scripts that add to
`globalThis.TideGraph`, loaded in the order `files.json` gives, with no build step and no dependencies (JetBrains'
Gradle build has no Node).

A file is one board (notes/visual-scripting.md, "The experience: one board"): a card for each declaration of data,
edited where it's drawn; a frame for each piece of code, with its entry (what it takes, each with a pin) and its
nodes, as Unreal's Blueprint has them (notes/visual-scripting.md, "Round four: full Blueprint"). What never changes
and one input reads (a literal, a capitalised name and a path of members from one, a minus over a number, a vector or
a colour made of numbers: the schema's `inline`) is shown and changed in that input's row, with no node and no wire;
what a Set changes, read by it alone, is in its title (`inline.titled`); both by the same rule as tidec's layout
(`graph_layout_fold`), so the layout's sizes are the board's. A variable is a Get node, a knot on a wire a reroute,
and what's worked out a node wired in. The file is the same either way.

Everything it says and offers comes from the schema tidels serves (`tide/graphSchema`, the compiler's
`compiler/src/graph_schema.json`, which each host also packages as the schema to start with): the kinds and their
groups, summaries and sentences, what each kind can have, the labels of keys, options, parameter modes, flags and
inputs (`portInfo`), what Set's operators do (`assignInfo`), the rows that add (`addRow`), which control edits
which type (`valueControls`), the type picker's groups and the types to offer first. What's valid at a place of the
graph's Tide is asked of tidels there: the types of a field, what a system can take, what a value or a name has
(`tide/graphMembers`, by a node, by a type, or by a name for an enum's choices and a namespace's functions), a
call's parameters (`tide/graphCalls`), which names are the game's own (`tide/graphNames`), and which operators a type takes
(`tide/graphOperators`; every one when it answers null).

- `src/json.js`: reading, and writing as tidec does (`print_jv`), byte for byte.
- `src/schema.js`: node ports, wires, key orders and the wording, from the schema.
- `src/doc.js`, `src/editor.js`: the document and its edits, the wiring rules, copy and paste, undo; the board's
  items (cards, frames, namespace blocks), a frame's pins, which values are shown in inputs (`analyse`), a value
  put in an input (`setInput`, `makePath`, `makeLiteral`) and the sizes tidec lays nodes out by (`sizes`).
- `src/lang.js`: tidels's answers (text and spans, types, calls, names, diagnostics with the input a hole is,
  hover, completion, members) and what an input is called (`inputLabel`).
- `src/ui.js`: fields edited in place, popups, the picker (a list with a search box, which can open an entry to
  what it has), tooltips, toasts.
- `src/values.js`: a value by its type's control (a number, a tick box, a vector, a colour, an enum's choices).
- `src/pickers.js`: the Add menu, the type picker, what a frame needs, the value picker of an input, the node
  search, jump-to.
- `src/cards.js`: a card: its name (renamed by a double click), its "+" on the title, its rows.
- `src/frames.js`: a frame: its entry, nodes (Gets, knots, a Set titled by what it changes), inputs with their
  values, pins (always shown) and wires, the operators a Set or an operator can have (`tide/graphOperators`), what a
  Set can change, a free place for a new node and for the Gets a pick makes, Keep it as a local; a value's small
  graph.
- `src/menus.js`: the one menu of every card, frame, node, row and pin, in the same sections and order (notes/
  visual-scripting.md, "Round three: like Blueprint").
- `src/panels.js`: My game (the Variables of the code in hand, dragged out as Gets and Sets; the file by group;
  click, double-click to rename, drag out) and Details (what's
  selected: its options as ticks with their help, Advanced, its note; the file's own with nothing selected).
- `src/board.js`: the board: places, pan and zoom, what's drawn (frames off screen are empty, far ones are
  blocks), the pointer (anything is dragged to move it, a box selects, a right or middle drag looks around, a pin
  starts a wire and lights where it can go), problems on their elements (an input with nothing in it in the page's
  own words, by the diagnostic's `data.port`), and the reads of a name lit together.
- `src/app.js`: the page, its toolbar, its commands and its messages with the host.

## The host's side

`TideGraph.start(host)` makes the page; `host.post(message)` sends to the host, which calls
`TideGraph.receive(message)`. The messages are listed at the top of `src/app.js`: the host sends the document
(`load`, then `text` when it changes elsewhere) and tidels's diagnostics; the page sends each edit as the whole
text (`edit`), its requests (`request`: `tide/graph*` for tidels, `host/clipboardRead`), and `clipboardWrite`,
`save` and `showTide` (View as Tide). A host handles the page's messages in order, so an edit is in its document
before a request sent after it reaches tidels. The page is its board at any size: a narrow one (a phone, a slim
split) has its lists open over the board.

## Working on it

`index.html` is the page with no host: open it in a browser with `?schema=<url>&graph=<url>`, or pick the schema
and a graph with its file input. Nothing is checked or saved there.

Tests (Node 24.22.1 or later; `ctest -R tide_graph_editor` runs them all with this build's tidec and tidels):

```sh
node --test tools/tide-graph/test/model.test.mjs     # the model, with tidec (TIDE_TIDEC) and tidels (TIDE_TIDELS)
node --test tools/tide-graph/test/browser.test.mjs   # the page in headless, muted Chrome or Edge (TIDE_BROWSER)
```

The browser test drives the page with real pointer and key events over the DevTools protocol
(`test/browser.mjs`). `TIDE_GRAPH_SHOTS=<folder>` saves a screenshot there when a wait times out.
