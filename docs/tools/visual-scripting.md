---
description: "Make a Tide game without writing code: a board of cards for the game's data and frames of nodes for what it does, in Unity, VS Code and JetBrains IDEs. A graph is Tide source, built with the game's .tide files, and any .tide file can become one."
---

# Visual scripting

A game's code can be graphs instead of text. A graph is a `.tidegraph` file, and the graph editor shows it as one board: a card for each piece of data (a component, an event, a constant) and a frame of nodes for each piece of code (a system, a function, an event handler). It is Tide, compiled as Tide is: the same networking, prediction and determinism, the same speed, on every platform. A game can be all graphs, all text, or both, and each uses what the other declares.

To try it without installing anything, open the [graph playground](../guide/graph-playground.md): a small game made with graphs only, which you edit and play in your browser.

## Making a graph

In Unity, with Tide's package:

- **Assets > Create > Tide > Graph** makes an empty one. Double-click it to open the graph editor.
- **Assets > Convert to Tide Graph** on a `.tide` file makes a graph of it, beside it: the same declarations and code, with its comments, laid out to read.

In VS Code and JetBrains IDEs, with Tide's extension or plugin ([Editors](editors.md)):

- A new file named `something.tidegraph` opens as an empty board.
- **Convert to Tide Graph** on a `.tide` file, in the file tree or the editor's menu, makes a graph of it and opens it. It asks whether the `.tide` file goes.

From the command line, `tide graph` does the same:

```sh
tide graph enemy.tide --replace    # enemy.tide becomes enemy.tidegraph
tide graph --text enemy.tidegraph  # the Tide a graph stands for
```

Without `--replace` the game has both files, which declare the same things twice: remove one.

## Starting

An empty board asks **What do you want to make?** and offers the usual first things, each with what it is for: a component, an event, a function, a system, an event handler. **More…** has the rest.

Later, **+ Add**, or a right click anywhere on the board, opens the same choices as a menu with a search box: **Data** (what the game keeps), **Behaviour** (what the game does) and **More**. Type a few letters and press Enter to take the first match. What you add appears where you asked, with its name selected, ready to type over.

## Cards: the game's data

A component, a singleton, a struct, an event, an enum, an input, a scene, a constant, an asset and the game's settings are cards. A card is edited where it's drawn:

- **Its name**, at the top. Press Enter to go on to its first field.
- **A row for each field**: its name, its type, the value it starts with. Enter in a row goes to the next one, and makes it after the last; **+ field** does the same. An enum's rows are its choices, the settings' rows the settings.
- **Its "…" menu** has what else its kind can have, and nothing more: a method or a property on a component, an operator on a struct only, `Sample` on an input only. It also has its options in words (**Dev builds only**, **This machine only**), **Generic over…** where a kind can be generic, attributes, a note, **Duplicate** and **Delete**.

A member with code of its own, a method or an input's `Sample`, is a frame on the board named for its type (`Ball.Bounce`); its row on the card goes to it.

### Picking a type

A type is never typed free. Click a type (or type a letter on it) to open the type picker: a list with a search box that narrows as you type, with the common types first, then this file's and the rest of the game's own, then the ways to make a type of others (**List of…** asks what it holds, with the same picker), then the engine's. Enter takes the first match. The picker asks Tide which types go there, so a field isn't offered a component, and a system's needs aren't offered an enum.

### Values

A value is edited by its type's own control: a number, a tick box, text, a vector's numbers, a colour, an enum's choices (the enum can be declared in any of the game's files). An empty control shows the type's default, greyed. To work a value out instead (`GRAVITY * 2`), press the small **ƒ** on its row: the value opens as a small graph beside its card, whose last node is where its value is wired in. **Done** closes it.

## Frames: what the game does

A system, a view, an event handler, a command, a function and a type's method are frames: a titled box that holds the code's nodes and the wires between them. Drag its title to move it with everything in it; it grows around its nodes.

A frame's first node is its **entry**. It says when the code runs, in the kind's own words (**Every tick, for each entity with** for a system, **When this happens** for an event handler, **Takes** for a function), and has a row for each thing the code takes:

- **+ needs…** adds one by picking what: a component of the game, a singleton, a filter (**only with**, **without**).
- How it's taken is a word on its row: **reads** or **changes**. Click it to change it.
- An event handler's first row picks its event; a command's rows are the values a player sends with it; a function's last row is its result's type.
- Each row has a **pin** on its right, and the entry has an **arrow** at its top right.

### Nodes

The code is nodes. A node says what it does (**Set**, **If**, `ball.Bounce`, **Multiply**) and has a row for each of its inputs:

- **Drag from the entry's arrow** and let go inside the frame to add what happens first: the search offers the statements (**Set**, a call, **If**, the loops, **Return**). Each statement has an arrow of its own to drag what happens next from, and those that hold code (an **If**'s branches, a loop's body) have an arrow for each.
- **A simple value is in its input.** A number, a tick or text is typed there, a vector made of numbers has a box for each, and a colour has its swatch. Something the code has (a parameter, a local, a field of one) is a name in its type's colour, like `wobble.phase`. There is no node for it and no wire.
- **Click an input**, or the **pick…** of an empty one, to choose what goes in it. The list has a search box and offers what fits there: a value to type, what this code has, the game's constants and functions, and **Work it out…**. Enter takes the first match. What doesn't fit is left out; **Show what doesn't fit too**, at the end, lists it.
- **What the code has opens to its fields**: click its arrow, press the right arrow key, or type a dot after its name (`wobble.`). In a name already picked, click one of its parts (`phase` in `wobble.phase`) to change it from there.
- **What's worked out is a node, wired in.** **Work it out…** has the operators (**Multiply**, **Less than**) and the calls. The new node goes beside the one that reads it, with a wire from its value, and what was in the input becomes its first input. Its own inputs are picked the same way. A value that two inputs read is a node too.
- **Drag from a pin**, on the entry or on a local, onto an input to put it there, or let go inside the frame to pick what to do with it. With the pointer over a name, the row it comes from lights up, and its other reads. Renaming a parameter renames every read.
- **A local** is a node that names a value. Inputs after it are offered it by its name.
- **Right-click, or press Space, inside a frame** to add any node. Type to search; Enter adds the first.
- **A call in the flow of statements is one node**, with arrows through it, titled with what it's called on (`ball.Bounce`, `Math.Clamp`). Its arguments are named as the function names them.
- Drag the end of a wire off its input to take it away, or onto another to move it. An input takes one value, and a statement runs from one place.

A system that adds to a field each tick is one node. Drag from the entry's arrow and pick **Set**. Click **what** and choose `wobble`, then `phase`. Click the `=` under it and choose **goes up by**, which is `+=`. Click the value beside it and choose `wobble`, then `frequency`. The node reads: Set `wobble.phase` `+=` `wobble.frequency`.

A wire stays inside its frame: code in one frame reaches another's through the game's data (a component, an event) or by calling it. Dropped outside, the wire says so.

Nodes that nothing runs, and values nothing reads, are left out of the game; the editor shows them faded.

Where a frame is on the board says nothing of when its code runs. Systems run in the file's order, which **Contents** shows, unless a stage says otherwise; **Move earlier in the file** and **Move later in the file**, in a frame's or a card's "…" menu, change it.

What's rare on a node is in its "…" menu, with **Copy**, **Duplicate** and **Delete**. The small **+** at the end of a call's last argument adds the next one its function takes; there is none once each has its value. A right click on an input can remove it, or name an argument.

### Text in a graph

A **Text** node is a value written as Tide, `a * b + 1`, whose inputs are wired in by name. A **Code** node is statements written the same way, and a **Tide text** card is declarations. Use them where text is clearer, as for a long formula, or not at all: everything has a node of its own.

## Problems

Tide checks the board as you change it, with the rest of the game's code. The chip at the top right says **Checked**, or how many problems there are; click it for the list, and a problem to go to it.

A problem is shown on what it's about: a field's row, an input, a node. It's red there, and says the compiler's message and what to do about it when the pointer is over it. An input with nothing in it says so by its name (**Pick what goes in 'what'**), and the rest of the file is still checked while it's empty. A problem goes as soon as its cause does.

Without Tide's language server (`tidels`, which comes with `tide`), the chip says **Not checked**: editing works, and mistakes show when the game is built. Building says the same errors with the element they're about: `Assets/Enemy.tidegraph:@17: error: ...`.

## Finding your way

- Drag the board to move it; the wheel scrolls, and Ctrl with the wheel (or a pinch) zooms. **Fit** shows the whole file. Far away, frames are blocks under their names.
- **Contents** lists what the file has, by group. **Find…** (Ctrl+F) jumps to a declaration by its name.
- The Tide a file stands for isn't on screen. **View as Tide**, in the board's "…" menu, opens it. The same menu has the file's namespace, the namespaces it uses, and **Tidy up the board**: data in a column, code beside it. A frame's own menu tidies its nodes.
- Shift and drag selects with a box. Delete removes what's selected, Ctrl+D duplicates it, and Ctrl+C and Ctrl+V copy and paste nodes into a frame, between files and between editors.
- Undo and redo (Ctrl+Z, Ctrl+Y) go through what was done, step by step.

## In Unity

The graph editor is a window of Unity's own. A graph in Assets is checked with the game Unity builds, so its types and functions are offered. A double click on a graph's error in the Console opens the board on what it's about. Saving (Ctrl+S) and Undo are Unity's.

## In VS Code and JetBrains IDEs

The graph editor is the file's editor, and the file stays the IDE's: it saves, undoes and shows changes in git as any file does. Its JSON is a tab away (VS Code: **Reopen Editor With > Text Editor**; JetBrains: the **Text** tab), and the IDE's problems list has a graph's errors on their line of it. **Show as Tide** in the IDE's menus is the board's **View as Tide**. Cards and frames copy and paste too, onto any board.
