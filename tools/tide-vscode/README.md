# Tide for VS Code

Completion, errors as you type with quick fixes, hover, go to definition, type
definition and implementation, find usages, highlights, rename, the call
hierarchy, formatting, parameter hints, inlay hints, the outline, symbols
across the game, folding, selection ranges, semantic highlighting, moving a
declaration to a file of its own, and each system's schedule above it, for
`.tide` files. It works in
VS Code, Cursor, VSCodium and Windsurf.

tide's installer adds this extension to the editors it finds, and `tide
editors` adds it again, for example after installing a new editor. The
language server is `tidels`, which comes with tide. The extension runs it from
`PATH`, or from where tide is installed. In a folder with a
`build/tools/tidels`, like Tide's own repo once it's built, it runs that one,
once you trust the folder.

In a trusted project, the extension selects both `tide` and `tidels` from the
version pinned in `tide.packages`. Unity projects follow their locked Tide
package: open Unity once to resolve/install it, and the IDE uses Unity's selected
installation. A `-tide-command <path>` development launch is followed only while
that Unity process is alive; closing it or a crash restores the project pin.
Versions share the `TIDE_VERSIONS`
cache; a missing pinned version gives an error instead of using another version.
Package/lock changes restart the server. Each open workspace folder has its own
server, so different projects can use different versions. Explicit path settings
and a trusted engine checkout's built server take precedence. A `tide.path`
override also selects its sibling `tidels`, unless `tide.server.path` overrides it.

Open the folder of your game, the one you'd run `tide run` in: every `.tide`
file in it and its subfolders is one game.

## Graphs

`.tidegraph` files, Tide made as a board of cards (data) and frames of nodes
(code), open in Tide's graph editor, checked by `tidels` as they're edited. A
node's simple values (a number, a name, a field) are in its inputs, picked from
a list of what fits there; what's worked out is a node wired in. The file stays VS Code's text:
saving, undo, hot exit and git's diffs are of its JSON (**Reopen Editor With >
Text Editor** shows it). **Tide: Convert to Tide Graph** makes a graph of a
`.tide` file with `tide graph`; **Tide: Show as Tide** opens the Tide a graph
stands for. More: [Visual scripting](https://tide-engine.dev/tools/visual-scripting).

## Running the game

The play button above a `.tide` file runs its game:

- **Tide: Run** plays it in a window of its own, as `tide run` does.
- **Tide: Run on the Web** plays it in a browser tab beside the code:
  VS Code's integrated browser, or the Simple Browser in editors without it.
  The game gets keys while its tab has focus, and the editor may pause it
  while its tab is hidden, so keep it in a group of its own.
- **Tide: Run on Android** plays it on the Android phone or emulator that's
  connected, as `tide run --android` does.

Each runs `tide run` in a terminal of its own, so saving a file reloads the
game (on Android, builds it again and starts it over), and typing `r` and
Enter there starts it over. Running it again starts the game over.
Each replacement waits for the preceding run's processes to stop. Closing its
terminal stops the run too. The first run on Android may ask to download Android's tools from Google: type
`y` and Enter there to accept.

On the web, each reload says what it did in the page's console. **Tide: Game
Developer Tools** shows it: the integrated browser's developer tools for the
game's tab, or in the Simple Browser (where it's also a button above the
page), the window's.

## Debugging

Breakpoints go in `.tide` files. The **Tide** debug configurations (in
**Run and Debug**, add a configuration) stop at them in a native debugger,
with locals under their Tide names and lists, maps, text, enums and entities
shown as Tide's:

- **Attach to Unity (Tide)** attaches to the Unity editor running the project,
  whose game is `Library/Tide`'s library; with several editors open, it asks
  which.
- **Debug Tide game** builds the game to debug (`tide build`, into its
  `.tide/debugger` folder) and starts it under the debugger. `args` passes it
  arguments (`--host`, `--join <code>`).
- **Attach to Tide game (tide run)** attaches to the game **Tide: Run** plays,
  which goes on reloading as files are saved.

They become a native debugger's configurations: Microsoft's C/C++ extension's
(`cppvsdbg`) on Windows, with the game's natvis; LLVM's LLDB DAP (`lldb-dap`)
or CodeLLDB (`lldb`) on macOS and Linux, importing the game's `game_lldb.py`.
In Restricted Mode they don't start, since debugging runs the game's code.
More: [Debugging](https://tide-engine.dev/tools/debugging).

## Settings

- `tide.server.path`: another language server to run.
- `tide.path`: another `tide` to run the game with.
- `tide.trace.server`: `messages` or `verbose` shows what VS Code and the
  language server say to each other, in the **Tide Trace** output.

In the paths, `~` is your home folder. `${workspaceFolder}` and relative paths
are the folder whose settings set them; set in your user or workspace
settings, they're the first open folder (for `tide.path`, the game's).
`${workspaceFolder:name}` is the open folder of that name.

After a server update, run **Tide: Restart Language Server**.

## Restricted Mode

In a workspace you haven't trusted, nothing from it runs: the language server
is the `tidels` that comes with tide (or one your user settings name outside
the workspace), never the workspace's `build/tools/tidels` or one its settings
name. Servers on `PATH` inside the workspace are skipped too, including paths
through symlinks or junctions. Games don't run or debug, since running one runs its code.
Trusting the workspace restarts the server.

## Testing

From this folder, `npm ci` installs the pinned dependencies and `npm test`
runs the adapter regressions. To test the bundled extension inside a real
VS Code installation, with this checkout's built server:

```sh
node test/run-host.mjs "<Code executable>" ../../build/tools/tidels.exe "<Node executable>"
```

Use Node 24.16 or newer on Windows. The test creates an isolated profile,
extension bundle and workspace, and mutes its browser. It checks live language
features, restarts, real terminal/process-tree cleanup, the embedded web page
and developer-tools command, then verifies cleanup after actual host shutdown.
It leaves logs in the printed temporary folder and changes no installed
extensions or personal settings. This opt-in test needs a desktop editor;
CTest runs the dependency-free adapter suite, `test/extension.test.mjs` and
`test/debug.test.mjs` (the debug configurations).
