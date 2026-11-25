---
description: "Edit a Tide game made with graphs in your browser: a board of cards and frames of nodes, checked by Tide's language server as you edit, compiled and hot reloaded into the running game."
---

# Graph playground

This game is made with graphs only: a [visual scripting](../tools/visual-scripting.md) file, `game.tidegraph`,
with no text code. Change its board, and the running game picks up the change. Everything runs in your
browser, as in the [text playground](./playground.md): nothing is installed and your graph is not sent anywhere.

<div data-tide-graph-playground></div>

## Try a change

The board opens on `Fall`, the system that moves every star down and checks whether the basket caught it. Its
data is in the cards on the left: drag the board, or press **Contents**, to see them.

- On the card `FALL`, change `90` to `200`: the stars fall faster.
- On `WIDTH`, change `40` to `80`: the basket is twice as wide.
- On `GOLD`, click the colour and pick another: the stars change colour.
- In `Fall`, the `1` wired into the `+=` after `caught` is what a catch counts: make it `10`.

The score and the stars carry over. **Restart** starts the last successful build from the beginning, and
**Reset graph** brings back the original graph and starts it again. Your graph is kept in this browser, so it's
still here when you come back.

**Auto reload** builds the graph a moment after each change. With it off, press **▶ Play**. If the graph has a
problem, it's marked on what it's about, the chip at the top right counts it, and the last successful game keeps
running.

## The board

This is the graph editor of the [editor extensions](../tools/editors.md), checked by the same language server:

- **Cards** are the game's data: a row for each field, with its name, its type and its value, each changed where
  it is. **Frames** are its code: nodes joined by wires, starting from the entry on the left, which says when the
  code runs and what it takes.
- **+ Add**, or a right click on the board, adds a card or a frame. A right click inside a frame adds a node, and
  a wire dragged from a pin or an arrow and dropped in the frame offers what can go there.
- A type is picked from a list, never typed. A problem is on the row, pin or node it's about, with what to do
  about it, and goes when it's fixed.
- **Contents** and **Find…** go to a declaration. The board's "…" menu has **View as Tide**, which shows the
  whole file as text and opens it in the text playground.

[Visual scripting](../tools/visual-scripting.md) describes the board in full. Drag the divider between
the graph and the game to give either more room, or press **Expand** to fill the window.

## What runs here

The graph is compiled as any Tide file is: tidec reads the `.tidegraph` as the Tide it stands for, then the game is
compiled to WebAssembly and loaded into the running preview. As in the text playground, the first build downloads
about 25 MiB of compiler tools, and the language server is a separate download of a few MiB, so the graph is checked
before the first build.

To make a game with graphs, [install Tide](./install.md): graphs are edited in Unity, VS Code and JetBrains IDEs, and
`tide graph` turns any `.tide` file into one.
