---
description: "Edit a running Tide game in your browser. Compile real Tide code locally and hot reload while keeping the game's state."
---

# Playground

Change the code and watch the running game pick it up. Compilation happens in
your browser; there is nothing to install and your source is not sent to a
build server.

<div data-tide-playground></div>

## Try a live change

- Change `INK` to `Color(0.18, 0.83, 0.75)` to turn the ball teal.
- Change `SPEED` to `2` to make it move faster, or `0` to hold it still.
- Change `RADIUS` to `30` to make it bigger.

The bounce count and position carry over. **Restart** starts the last successful
build from the beginning. **Reset code** restores the original example and
starts it again.

**Auto reload** compiles after you stop typing. With it off, press
**Compile & reload** or Ctrl+Enter (Cmd+Enter on macOS). If the code has an
error, diagnostics appear below the editor and the last successful game keeps
running. Try removing a semicolon, then putting it back.

## The editor

The editor runs the same language server as the [editor extensions](../tools/editors.md),
so it already knows your code before you compile it:

- Type `.` after a value, or press Ctrl+Space, for completions. Hover a name to see its type and documentation.
  Parameter hints appear as you type a call.
- Mistakes are underlined as you type and listed under the editor; click one to jump to it.
- F12 goes to a definition in this file, F2 renames, and Shift+Alt+F formats the code.
  Ctrl+/ comments lines, and Ctrl+F finds text.

Drag the divider between the code and the game to give either more room, or press
**Expand** to fill the window. Esc or **Exit** brings back the page.

## What runs here

This is the real Tide compiler and engine: Tide is transpiled to C, compiled
to WebAssembly and loaded into the running preview. The first compilation
downloads about 25 MiB of compiler tools, which your browser can cache. Later
edits reuse them. The first build also initializes the tools and can take longer
than later edits; progress appears below the editor. The compiler runs in a
worker so you can keep editing while it builds. The editor's language server is
a separate download of under 2 MiB, so suggestions work before the first build.

This demo edits one `.tide` file with no external assets or packages. The
engine's normal component, system and view features work here, including
procedural drawing and shaders. Files named by an `asset` declaration must
exist in the compiler's virtual filesystem; this page has no file upload or
package manager. Native-only integrations are unavailable. [Build code](../game/build-code.md)
requires the installed tools; use `tide run --web` to build and preview a project
that makes assets or constants during its build.

[Hot reload](../tools/cli.md#hot-reload) carries fields over by name. New fields
use their defaults; changing a default does not overwrite an existing field.
Renaming or removing state can reset it. Web reload currently closes connections
to other players, so this playground demonstrates a solo match.

For a complete project, [install Tide](./install.md) and follow
[Your first game](./first-game.md). Use `tide run --web --stats` for the same browser
preview with local project files and watched rebuilds.
