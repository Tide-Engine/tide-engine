---
description: "Write a Tide game's code as graphs: nodes for statements and values, wired together, in Unity's graph editor or in VS Code and JetBrains IDEs. A graph is Tide source, built with the game's .tide files, and any .tide file can become one."
---

# Visual scripting

A game's code can be graphs instead of text. A graph is a `.tidegraph` file: its declarations (components, systems, events, functions, everything a `.tide` file has) with their code as nodes joined by wires. It is Tide, compiled as Tide is: the same networking, prediction and determinism, the same speed, on every platform. A game can be all graphs, all text, or both, and each uses what the other declares.

## Making a graph

In Unity, with Tide's package:

- **Assets > Create > Tide > Graph** makes an empty one. Double-click it to open the graph editor.
- **Assets > Convert to Tide Graph** on a `.tide` file makes a graph of it, beside it: the same declarations and code, with its comments, laid out to read.

In VS Code and JetBrains IDEs, with Tide's extension or plugin ([Editors](editors.md)):

- A new file named `something.tidegraph` opens empty in the graph editor: add a declaration with **+**.
- **Convert to Tide Graph** on a `.tide` file, in the file tree or the editor's menu, makes a graph of it and opens it. It asks whether the `.tide` file goes.

From the command line, `tide graph` does the same:

```sh
tide graph enemy.tide --replace    # enemy.tide becomes enemy.tidegraph
tide graph --text enemy.tidegraph  # the Tide a graph stands for
```

Without `--replace` the game has both files, which declare the same things twice: remove one.

## The editor

- **On the left**, the file's declarations: a type's fields and methods, a system's or a function's code, an input's `Sample`, a scene's `prepare`. Add, rename, reorder and remove them there.
- **On the right**, what's selected: a declaration's parameters and their types, its attributes and the words before it (`local`, `dev`, `async`...), a field's default value, a node's own values.
- **In the middle**, the selected code as a graph. Space, a right click, or a wire dropped on nothing opens the search for a node to add. Drag from a port to a port to wire them, and a wire's end off its port to take it away.
- **The Tide panel** shows the Tide the graph stands for, and what's wrong with it, on the node it's about. Click an error to go to its node.

The editor checks a graph as it's edited with Tide's language server, `tidels`, which comes with `tide`, together with the rest of the game's code: a graph in Assets with the game Unity builds. So it knows the game's own types and functions:

- **Errors** are the compiler's own, a call of what isn't there or a value of the wrong type, on the node they're about.
- **Ports and wires** take the colour of the type of their value, and a node's tooltip says its type and what it is.
- **The search** offers what's there to use: the game's functions as calls, its structs and components as values of them, the names, constants and namespaces in scope (`Math`, `Draw`, `GUI`, `Random`...). A wire from a value dropped on nothing offers that value's fields and methods first, a method with an input for each of its parameters. **Pick member…** on a field or method node offers its object's.

Without `tidels`, the panel says so and shows the Tide unchecked.

Undo, copy and paste (between graphs too), duplicate (Ctrl+D), frame (F) and save (Ctrl+S) work as elsewhere in Unity.

## In VS Code and JetBrains IDEs

The graph editor in VS Code and in JetBrains IDEs is one editor, a page the IDE shows as the file's editor, and has what Unity's has: the declarations on the left, what's selected on the right, the code in the middle, and the Tide panel below it. The file stays the IDE's: it saves, undoes and shows changes in git as any file does, and its JSON is a tab away (VS Code: **Reopen Editor With > Text Editor**; JetBrains: the **Text** tab).

- **Adding nodes**: **+ Node**, Space, a right click or a double click on the canvas, or a wire dropped on nothing opens the search. A wire from a value offers that value's fields and methods first; from a statement's exec output, statements, and a call there becomes a statement of its own (**Do**). Typing narrows it; Enter adds the first.
- **Wiring**: drag from a port to a port. Drag the end of a wire off an input to take it away, or onto another to move it. A statement runs from one place, so wiring it somewhere else moves it there; a wire that would make a value of itself, or a statement run in a loop, isn't made, and the bottom right of the toolbar says why.
- **Moving around**: the mouse wheel scrolls, Ctrl and the wheel (or a pinch) zooms, a right or middle drag pans, **F** frames the selection or the whole graph, and **Layout** lays the graph out as `tide graph` does.
- **Selecting**: click, Shift or Ctrl and click to add, drag on nothing for a box. Delete removes, Ctrl+D duplicates, Ctrl+C and Ctrl+V copy and paste, between graphs, between IDEs and to and from Unity's editor.
- **On a node**: its own values (a number, a name, an operator) edit in place; the right side has the rest, its comment among them. A right click on a field or method node has **Pick member…**. **+ Note** adds a box to write in, behind the nodes.
- **The Tide panel** (**Tide** in the toolbar) shows the Tide the graph stands for, the selected node's part marked, and the problems above it: click one, or a place in the text, to go to its node. **Show as Tide** opens that text in a tab.
- **Undo** (Ctrl+Z, Ctrl+Y) is the graph's own while it has the focus, step by step through what was done to it; the IDE's own Undo works as well.

Without `tidels`, the toolbar says the graph isn't checked; editing works, and the search offers the kinds of node alone.

## Nodes and wires

A graph has two kinds of node, joined by two kinds of wire:

- **Statements** run one after the other along their white exec wires: a local, an assignment, a call, `if`, the loops, `switch`, `return`. A statement that holds code (an `if`'s branches, a loop's body, a `GUILayout.Vertical()`'s block) starts it from an exec output of its own, and the code after it goes on from its `next`.
- **Values** go in along data wires: a number, a name, a field (`member`), a call, an operator, a value of a type (`Transform { position = ... }`). A value is worked out where it's wired in, as if it were written there: wired to two inputs, it's worked out twice.

A call in the flow of statements is one node, with exec wires through it. Its result, wired into nodes after it, is what it gave when it ran, once.

Locals are nodes too: a `var` statement declares one, and a node that reads it names that `var`, so renaming the local renames every read of it. Reading a local before its `var` has run is an error.

A statement only runs when the exec wires reach it, from the start of the code. Nodes nothing reaches, and values nothing reads, are left out of the game; the editor shows them faded.

## Text in a graph

A **text** node is a value written as Tide, `a * b + 1`, whose inputs are wired in by name: `a` and `b` are the values wired to it. A **code** node is statements written the same way. Use them where text is clearer, as for a long formula, or not at all: everything has a node of its own.

## Errors

An error in a graph is about a node, and says which: `Assets/Enemy.tidegraph:@17: error: ...`. In Unity, the graph editor marks the node, and a double click on the error in the Console opens it there. In VS Code and JetBrains IDEs, the graph editor marks the node and lists the error in its Tide panel; the IDE's problems list has it on the node's line of the JSON.
