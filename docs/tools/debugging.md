---
description: "Debug Tide code in Visual Studio, VS Code, CLion and LLDB: breakpoints in .tide files, stepping line by line, and lists, maps, text and entities shown as Tide's, in Unity or a game tide runs."
---

# Debugging

A native debugger stops in your `.tide` files: a breakpoint on a Tide line stops there, stepping goes from one Tide line to the next, the call stack shows your systems and functions at their `.tide` lines, and the variables window shows your locals, components and singletons with their lists, maps, text and entities as Tide's. The match runs as native code, so this works where it runs: in a game `tide run` plays, in a game you build, and in the Unity editor, where the match is a native library.

This is the debugger you'd use for C and C++. It's for your game's own logic; [debug views](debug-views.md) are the engine's in-game profiler.

## What a debug build has

Native `tide run`, `tide build` and `tide unity` builds retain debug information in default debug mode and with `--release`. Each has, beside the program or library:

- its debug information, which maps each line of the C the game becomes back to its `.tide` line. On Windows that's a `.pdb`, with the game's natvis inside it, which Visual Studio, VS Code, CLion and WinDbg read by themselves.
- `<name>.natvis`, how those debuggers show the game's values, and `<name>_lldb.py`, the same for LLDB. A Unity project's are `Library/Tide/game.natvis` and `Library/Tide/game_lldb.py`.

`--release` optimizes game code while retaining debug information; inlining and optimized-away locals can affect stepping. `--distribution` builds have no debug information. Web and Android debug/release builds retain symbols, but do not produce these native debugger visualizers.

## What you see

- **Breakpoints** go on Tide lines: a statement's line, or a function's or system's first line, where it starts.
- **Stepping over** goes to the next Tide line. **Stepping into** a line goes into the Tide function it calls; the generated code around your code isn't stepped through. Stepping can still go into one of the engine's helpers a line uses (`tide_add_i` for `+`, `tide_list_count`): step out to come back. LLDB skips those by itself; [Visual Studio](#visual-studio) can too.
- **The call stack** names Tide's code by its C names: `tide_system_Move` is the system `Move`, `tide_view_Draw` a view, `tide_handler_Hit` an event handler, `tide_function_Heal` a function, `tide_method_Stats_IsDead` a method and `tide_property_...` a property. The frames between them (`tide_rows_...`, `tide_task_...`) are how the engine runs systems.
- **Locals and parameters** have their Tide names. A system's components are its parameters (`player`, a pointer to the `Player` it runs for), and `tide_this` is `this`. A name C reserves gets `_` after it (`int_`). Where a function taking an `Action` runs inside its caller, that function's locals start with `tide_in<n>_`. Other names starting with `tide_` are the engine's.
- **Values** show as Tide's:
  - text: `"ana"`, from wherever the world keeps it.
  - lists: their count and their elements, each as its type, structs with their fields: in locals, parameters and fields alike. A list local's type says which list it is, `tide_local_list3`, the list inside it as `list`. Visual Studio, VS Code and CLion show only the count of a list in async code, in a `mut` parameter, or in a function that takes an `Action`; LLDB shows those too.
  - maps and sets: their count and entries, `["ann"] = 3`.
  - enums: the member's name, in a field (LLDB: everywhere).
  - entities: `Entity(3:1)`, or `none`.
  - `T?`: the value, or `none`; `T fails E` its error; vectors `(1, 2, 3)`; `Color(1, 0, 0, 1)`.

Grids show their handle, and C's own view of anything else.

## Unity

The Unity editor loads the game as a native library, `Library/Tide/game-<n>`, the newest one tide built. Attach a native debugger to the Unity editor itself: its C# debugger can't see it.

- **VS Code**: **Run and Debug**, then **Attach to Unity (Tide)** (add it from the **Tide** configurations). On Windows it needs Microsoft's C/C++ extension; on macOS and Linux, LLVM's **LLDB DAP** or **CodeLLDB**.
- **CLion**: **Run > Attach to Unity (Tide)**. On Windows, CLion's debugger reads the game's `.pdb` with the **Visual Studio** toolchain (**Settings > Build, Execution, Deployment > Toolchains**, first in the list).
- **Visual Studio**: **Debug > Attach to Process**, **Attach to: Native code**, pick `Unity.exe`. See [Visual Studio](#visual-studio).

A breakpoint set before attaching, or before the game's library loads, stops once it's there. Each new build Unity loads is another library with its own debug information, and breakpoints carry over to it.

## A game tide runs

- **VS Code**: **Debug Tide game** builds the game to debug (`tide build`) and starts it under the debugger, so it stops from its first tick. **Attach to Tide game (tide run)** attaches to the game **Tide: Run** plays, which goes on reloading as you save files.
- **CLion**: **Run > Debug Tide Game**, or in a `.tide` file's context menu: it plays the game as the Tide run configuration does, then attaches to it. Saving a file reloads the game, still under the debugger. Code that ran before the debugger attached, like a scene's first `Spawned` handlers, runs again when you press Ctrl+R in the game, which starts it over.
- **Visual Studio**: open the program `tide build` made (**File > Open > Project/Solution**, pick the `.exe`) and start debugging, or attach to the game `tide run` plays: it's the program in the game's `.tide/<configuration>/run/<n>/` folder.

## Visual Studio

Visual Studio reads the game's natvis from its `.pdb`, so values show as Tide's with no setup. To keep **Step Into** out of the engine's helpers, save this as `tide.natstepfilter` in `Documents\Visual Studio 2022\Visualizers`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<StepFilter xmlns="http://schemas.microsoft.com/vstudio/debugger/natstepfilter/2010">
  <Function>
    <Name>tide_(?!(system|view|handler|function|method|property|operator)_).*</Name>
    <Action>NoStepInto</Action>
  </Function>
</StepFilter>
```

## LLDB

LLDB on macOS and Linux shows values as Tide's once it has the game's LLDB views:

```sh
lldb mygame
(lldb) command script import /path/to/game_lldb.py
(lldb) breakpoint set --file Game.tide --line 42
(lldb) run
```

The views also keep **step** out of the engine's helpers. The editors' debug configurations import them for you. On macOS, a program `tide build` makes has a dSYM with the views inside, which LLDB loads with `settings set target.load-script-from-symbol-file true`. Windows' LLDB can't read where a game keeps its text and lists; use a natvis debugger there.

A reload that changes the game's types writes new views: import them again to see the new fields.
