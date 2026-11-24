---
description: "Edit a Tide game made with graphs in your browser: nodes and wires checked by Tide's language server as you edit, compiled and hot reloaded into the running game."
---

# Graph playground

This game is made with graphs only: a [visual scripting](../tools/visual-scripting.md) file, `game.tidegraph`,
with no text code. Edit its nodes and wires, and the running game picks up the change. Everything runs in your
browser, as in the [text playground](./playground.md): nothing is installed and your graph is not sent anywhere.

<div data-tide-graph-playground></div>

## Try a change

The graph opens on `Fall`, the system that moves every star down and checks whether the basket caught it.

- On the left, select `FALL` and change its `90` to `200`: the stars fall faster.
- Select `WIDTH` and change `40` to `80`: the basket is twice as wide.
- In `Fall`, the `1` wired into the `+=` after `caught` is what a catch counts: make it `10`.
- In `Baskets`, change the `TEAL` node's name to `GOLD`.

The score and the stars carry over. **Restart** starts the last successful build from the beginning, and
**Reset graph** brings back the original graph and starts it again. Your graph is kept in this browser, so it's
still here when you come back.

**Auto reload** builds the graph a moment after each change. With it off, press **▶ Play**. If the graph has an
error, the node it's about is marked, the Tide panel lists it, and the last successful game keeps running.

## The editor

This is the graph editor of the [editor extensions](../tools/editors.md), checked by the same language server:

- **On the left**, the file's declarations; **in the middle**, the selected one's code as nodes; **on the right**,
  what's selected. Space, a right click or a double click on the canvas adds a node, and a wire dropped on nothing
  offers what can go there: a value's fields and methods first.
- Drag from a port to a port to wire them. Delete removes, Ctrl+Z and Ctrl+Y undo and redo, and **Layout** lays the
  graph out.
- The **Tide** panel shows the Tide the graph stands for and what's wrong with it; click a problem to go to its node.
  **Show as Tide** shows the whole file as text, and opens it in the text playground.

[Visual scripting](../tools/visual-scripting.md) describes the nodes and the editor in full. Drag the divider between
the graph and the game to give either more room, or press **Expand** to fill the window.

## What runs here

The graph is compiled as any Tide file is: tidec reads the `.tidegraph` as the Tide it stands for, then the game is
compiled to WebAssembly and loaded into the running preview. As in the text playground, the first build downloads
about 25 MiB of compiler tools, and the language server is a separate download of a few MiB, so the graph is checked
before the first build.

To make a game with graphs, [install Tide](./install.md): graphs are edited in Unity, VS Code and JetBrains IDEs, and
`tide graph` turns any `.tide` file into one.
