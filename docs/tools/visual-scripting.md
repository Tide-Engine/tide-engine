---
description: "Write a Tide game's code as graphs: nodes for statements and values, wired together, in Unity's graph editor. A graph is Tide source, built with the game's .tide files, and any .tide file can become one."
---

# Visual scripting

A game's code can be graphs instead of text. A graph is a `.tidegraph` file: its declarations (components, systems, events, functions, everything a `.tide` file has) with their code as nodes joined by wires. It is Tide, compiled as Tide is: the same networking, prediction and determinism, the same speed, on every platform. A game can be all graphs, all text, or both, and each uses what the other declares.

## Making a graph

In Unity, with Tide's package:

- **Assets > Create > Tide > Graph** makes an empty one. Double-click it to open the graph editor.
- **Assets > Convert to Tide Graph** on a `.tide` file makes a graph of it, beside it: the same declarations and code, with its comments, laid out to read.

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

An error in a graph is about a node, and says which: `Assets/Enemy.tidegraph:@17: error: ...`. In Unity, the graph editor marks the node, and a double click on the error in the Console opens it there.
