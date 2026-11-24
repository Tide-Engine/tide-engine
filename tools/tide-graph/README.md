# Tide's graph editor for IDEs

The editor of `.tidegraph` files (docs/tools/visual-scripting.md, notes/visual-scripting.md) that VS Code
(`tide-vscode/src/graph.js`, a custom text editor) and JetBrains IDEs (`tide-jetbrains`, `TideGraphEditor`, in
JCEF) show as the file's editor. One page, written once: plain classic scripts that add to `globalThis.TideGraph`,
loaded in the order `files.json` gives, with no build step and no dependencies (JetBrains' Gradle build has no
Node). Everything it knows of the format comes from the schema tidels serves (`tide/graphSchema`, the compiler's
`compiler/src/graph_schema.json`, which each host also packages as the schema to start with).

- `src/json.js`: reading, and writing as tidec does (`print_jv`), byte for byte.
- `src/schema.js`: node ports, wires and key orders from the schema.
- `src/doc.js`, `src/editor.js`: the document, its edits, the wiring rules, copy and paste, undo.
- `src/lang.js`: tidels's answers (text and spans, types, diagnostics, hover, completion, members).
- `src/ui.js`, `canvas.js`, `inspector.js`, `outline.js`, `search.js`, `app.js`: the page.

## The host's side

`TideGraph.start(host)` makes the page; `host.post(message)` sends to the host, which calls
`TideGraph.receive(message)`. The messages are listed at the top of `src/app.js`: the host sends the document
(`load`, then `text` when it changes elsewhere) and tidels's diagnostics; the page sends each edit as the whole
text (`edit`), its requests (`request`: `tide/graph*` for tidels, `host/clipboardRead`), and `clipboardWrite`,
`save` and `showTide`. A host handles the page's messages in order, so an edit is in its document before a request
sent after it reaches tidels.

## Working on it

`index.html` is the page with no host: open it in a browser with `?schema=<url>&graph=<url>`, or pick the schema
and a graph with its file input. Nothing is checked or saved there.

Tests (Node 24.22.1 or later; `ctest -R tide_graph_editor` runs them all with this build's tidec and tidels):

```sh
node --test tools/tide-graph/test/model.test.mjs     # the model, with tidec (TIDE_TIDEC) and tidels (TIDE_TIDELS)
node --test tools/tide-graph/test/browser.test.mjs   # the page in headless, muted Chrome or Edge (TIDE_BROWSER)
```
