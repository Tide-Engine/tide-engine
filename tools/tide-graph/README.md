# Tide's graph editor

The editor of `.tidegraph` files (docs/tools/visual-scripting.md, notes/visual-scripting.md) that VS Code
(`tide-vscode/src/graph.js`, a custom text editor), JetBrains IDEs (`tide-jetbrains`, `TideGraphEditor`, in
JCEF) and the docs' graph playground show. One page, written once: plain classic scripts that add to
`globalThis.TideGraph`, loaded in the order `files.json` gives, with no build step and no dependencies (JetBrains'
Gradle build has no Node).

A file is one board (notes/visual-scripting.md, "The experience: one board"): a card for each declaration of data,
edited where it's drawn; a frame for each piece of code, with its entry (what it takes, each with a pin) and its
nodes; reads of parameters and locals as wires from their pins. Everything it says and offers comes from the schema
tidels serves (`tide/graphSchema`, the compiler's `compiler/src/graph_schema.json`, which each host also packages as
the schema to start with): the kinds and their groups, summaries and sentences, what each kind can have, the labels
of keys, options and parameter modes, the types to offer first. What's valid at a place of the graph's Tide (the
types of a field, what a system can take, a value's members) is asked of tidels there.

- `src/json.js`: reading, and writing as tidec does (`print_jv`), byte for byte.
- `src/schema.js`: node ports, wires, key orders and the wording, from the schema.
- `src/doc.js`, `src/editor.js`: the document and its edits, the wiring rules, copy and paste, undo; the board's
  items (cards, frames, namespace blocks), a frame's pins, and which nodes are drawn as wires from a pin.
- `src/lang.js`: tidels's answers (text and spans, types, diagnostics, hover, completion, members).
- `src/ui.js`: fields edited in place, popups, the picker (a list with a search box), tooltips, toasts.
- `src/values.js`: a value by its type's control (a number, a tick box, a vector, a colour, an enum's choices).
- `src/pickers.js`: the Add menu, the type picker, what a frame needs, the node search, jump-to.
- `src/cards.js`: a card, and the "…" menu of a card or a frame (what its kind can have).
- `src/frames.js`: a frame: its entry, nodes, pins and wires; a value's small graph.
- `src/board.js`: the board: places, pan and zoom, what's drawn (frames off screen are empty, far ones are
  blocks), the pointer, problems on their elements.
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
