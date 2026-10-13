---
description: "Set up Tide in VS Code or JetBrains IDEs with language server support for completion, errors, quick fixes, navigation and refactoring."
---

# Editors

Tide's language server, `tidels`, comes with `tide`. Open your game's folder, the one you run `tide run` in, and every `.tide` file in it is one game, with its [packages](../packages/using-and-writing.md). Across all of them, editors get:

- Completion, and errors as you type, with quick fixes: a name spelled wrong, a missing `mut`, a missing or stray `out`, an error nothing handles, a name from another namespace, a local that's never read (which is faded).
- Enum assignments prioritize and preselect the expected enum type: after `zombie.state =`, `ZombieState` comes first, followed by its qualified members. With [enum shorthand](../language/basics.md#enums-and-switch), `zombie.state = .` offers just that enum's members. A leading dot also completes members in typed declarations, returns, comparisons, enum switch cases and arguments whose parameter type is unambiguous.
- Hovers, go to definition or to a value's type, find usages, the uses of a name highlighted, and rename.
- Go to implementation, from an event to its handlers, from the input to its `Sample`, `Sanitize` and `Predict` and from a `virtual` declaration to its [override](../language/overrides.md), and the call hierarchy: what calls a function, and what it calls.
- Formatting, of a file or a selection, parameter hints and inlay hints. Parameter hints list every function that shares the name being called, with every parameter and its default, and follow an argument written by name; where an argument starts, completion offers the names the other parameters go by. Inlay parameter hints label written literal arguments; omitted defaults and arguments already written by name have no label.
- The outline, symbols across the workspace's games, folding, growing a selection, and semantic highlighting. A shared package contributes the declarations active in each game that uses it, with identical symbols listed once.
- After `asset`, completion offers the supported asset types as you start typing, with forms such as `Mesh<Vertex3>`, `Grid2<Color>` and `Map<string, Texture>`. Inside a list or map, it offers the supported element types; mesh vertex and contract arguments offer structs. Assets also complete by name, with their type's methods and parameter hints, including `Texture.Sample` in a frag or vert. They highlight as ordinary read-only variables; fields use the editor's property styling. The editor's theme chooses their colors.
- A known [skinned model or animation asset](../game/animation.md) exposes its clip names in completion: `hero.animations.idle` or `movement.run`. Hover shows each clip's original source name, and a model exposes its bone names and `defaultPose` too. The editor reads the glTF JSON or FBX catalog without importing geometry or animation samples; invalid or unavailable metadata is diagnosed separately from an absent clip. Their asset paths complete `.glb`, `.gltf` and `.fbx` files.
- Moving a declaration to a file of its own.
- Enum completion includes `EnumType.Count()`; hover and signature help explain its compile-time member count. A cast written as `(int)value` reports the Tide spelling, `int(value)`.
- [Assets](../game/assets.md) kept in step with their files: renaming a file an `asset` names, or a folder above it, in the editor rewrites every path to it, in every file of the game and its packages, each relative to its own file (a glob isn't rewritten: a file it stops matching gets an error instead). Changes to watched asset files refresh their diagnostics, including external glTF buffers and replacement files that keep the same size and timestamp. A file moved outside the editor is found again by its bytes, which `tidels` and `tide` keep the SHA-256 of in the game's `.tide/`: the error says where it is now, with a quick fix that writes the path, or else lists the files of that name, a fix each. Inside an `asset`'s path, completion offers the folders and files it can lead to, the type's own files (`.png`, `.psd`, `.jpg`, `.jpeg`, `.tga` and `.bmp` for a `Grid2<Color>`, `Texture` or `Cubemap`; `.wav`, `.ogg`, `.mp3` and `.flac` for a `Clip`; `.glb`, `.gltf`, `.fbx` and `.obj` for a `Mesh`; `.glb`, `.gltf` and `.fbx` for a `SkinnedMesh`, `Animation` or `AnimationCatalog`), and `*` and `**/*` with each accepted format for a list, map, cubemap or combined `AnimationCatalog` catalog. Each literal in an animation file list supports the same path completion and rename edits; checked clip completion cleans exporter names and shows original names and source filenames on hover. Hovering a [`Mesh`](../game/assets.md#meshes) asset says what its file has: how many vertices and triangles, and whether it has normals, uvs, colors and tangents for the vertex struct to take.
- Each system's place in [the schedule](../game/schedule.md) above it: its stage, what it reaches through its `World`, each system it waits for and why, and what runs alongside it. A part that names a system goes to it when clicked.
- Above a declaration a package replaced, which package did, and above an [override](../language/overrides.md), what it replaces. A click goes to the other.
- Above a declaration, how many places name it ("3 references"), which a click lists: types, functions, methods, constants, assets, frags and verts, where "0 references" is code nothing uses. A system, view or handler has the line only when a `[Before]` or `[After]` names it.

## Native scene editor

Open an authored tree in a desktop window alongside your code editor:

```sh
tide open scenes/Level.tprefab --project .
```

The hierarchy shows the complete composition, including descendants inherited
from nested prefabs. Select an entity in the hierarchy or click its viewport
marker. The inspector shows its components and effective field values, including
defaults. Select a field, edit its Tide expression, then press Enter or **Apply**.
**Ctrl+A** selects the whole value while typing. Values can name entities,
assets, constants and ordinary struct/list literals, with the normal compiler
checks. **Reset override** restores the component default or inherited value.
Untouched fields are not serialized.
Direct entity references show their composed paths, so a nested prefab's `Lid`
can be overridden as `Crates.Left.Lid`. When editing a more complex inherited
expression, write its names in the current prefab's namespace and entity-path
scope; compiler diagnostics check the resulting value.

**Add component** lists this project's shared component definitions.
**Child** adds an entity beneath the selection, and **Prefab** nests an existing
prefab there. **Delete selected subtree** removes an authored child and its
descendants. **Remove** deletes an authored component entry or override; it
cannot remove a component inherited from the base prefab. Inherited descendants
are read-only here: edit their source prefab. **Next** switches between prefab
declarations in a file. **Source** shows the current text draft.

The viewport draws referenced static models, unit wire cubes and axes, with parenting,
local position, rotation and scale. Use the wheel to zoom, middle drag to pan,
and **XY**/**Isometric** to change projection. Literal TRS, constants and basic
float arithmetic are previewed, including `quaternion.Euler(float3(...))`.
Unsupported expressions and their subtrees are explicitly omitted from the
preview. With **Views: off**, static models use imported colors and editor lighting
without executing game code. There are no drag gizmos yet.

**Views: on** compiles the project and renders its opted-in views, including their
frags, verts and textures. `[RunInEditor]` includes a view in both the game and
the editor; `[EditorOnly]` includes it only in the editor. Unmarked views run only
in the game. These attributes take no arguments and apply only to views:

```csharp
[RunInEditor]
view Models(Model model, World world)
{
    Draw.Mesh(model.model.Value, world.LocalToWorld(this));
}
```

The editor constructs the selected prefab's whole tree from its authored values
and defaults. It does not start `Main`, run `Spawned` or other event handlers,
tick systems, or start a session. A view can prepare its own local visual state
using the usual view permissions; that state belongs to the preview. Systems
cannot opt in. Put values needed for rendering in defaults or authored overrides,
or prepare local rendering resources from the view.

The editor supplies the initial camera and viewport size. Views can set their own
camera; hierarchy markers continue to use the editor camera, so keep a shared
preview view from replacing it when selecting entities. **Match**/**Local** picks
the prefab's placement, with the normal type checks: an `Entity` reference cannot
become a `LocalEntity` reference automatically. Game input is neutral in this
first preview, and match time stays at its initial tick. Audio, networking and
file/request services are not provided.
Opting in executes project code, including helpers and trusted C it calls.

Applied edits, undo/redo, prefab selection and detected source/asset changes
compile a fresh authored world, including edits not yet saved. Builds happen in
the background. A failed build shows diagnostics and keeps the last successful
preview visibly marked stale; it does not change the authored file. Turning views
off releases it. Closing waits for an active compile to finish. Private build
outputs and logs remain under `<project>/.tide/editor-preview/`.

Declare a field such as `Asset<Mesh<Vertex3>> model` in your own component and
add that component to an entity. Select its field and choose **Browse**, select
a matching singular mesh asset, then **Use for selected field**. The prefab
stores the asset name as an ordinary checked override. Nested instances use
their composed position, rotation and scale in the preview.
Named references and constant aliases are previewed. Other valid reference
expressions are counted as omitted in the viewport footer; inspect their text.

**Models** opens the project's static mesh asset browser, including collection
items. Right drag orbits, the wheel zooms, **Frame model** resets the zoom and
**Lit**/**Unlit** switches preview shading. Model files and glTF side buffers
refresh when their filesystem metadata changes. Skinned-model previews and
assigning collection items as persistent references are later work.

**Save** / **Ctrl+S** updates the text file. **Undo** / **Ctrl+Z** and **Redo** /
**Ctrl+Y** keep up to 64 edits. Source edits preserve surrounding comments,
line endings and untouched values. Syntax-invalid edits are rejected so the
inspector remains editable; type/reference errors remain visible and block Save.
The compiler checks the whole project's scripts and prefabs as code changes,
including `[SceneRequired]` references. This is front-end and asset-metadata
validation; normal game builds still validate generated C and complete assets.

Outside changes reload automatically while the document is clean. With unsaved
edits, the draft stays in memory and Save refuses to overwrite changed source.
**Reload** asks before discarding edits. Unsaved applied edits are also kept in
`<project>/.tide/editor/`; opening the file again offers recovery. Recovery retains
the original source baseline, so a newer outside edit still causes a conflict.
These recovery files are local cache files, not authored project content.

The first app opens existing `.tprefab` files and uses the folder/package project
context described under [`tide open`](cli.md#open). It supports Tide expression
fields rather than a full text editor: long expressions and source syntax errors
are edited in your usual code editor. Entity rename/reparent authoring, mesh
picking, transform gizmos and embedded IDE windows are future work.

## VS Code, Cursor, VSCodium and Windsurf

Both editor integrations recognize `.tprefab` files as Tide. They use the same
game/package context as scripts, report invalid authored components and references,
and include prefab files when renaming components, fields and named prefabs.
Authored entity names and paths currently need manual text edits. See
[Authored prefabs](../game/scenes.md#authored-prefabs) for the text format.

**Tide: Open Prefab Editor** in the Explorer context menu or above an open
`.tprefab` launches the native app beside the IDE. It uses `tide.path` when set
and requires a trusted workspace. A dirty active prefab is saved before launch;
save script changes in the IDE for the native app to detect them. The app stays
open independently of the IDE window. This is a native launch, not an embedded
IDE viewport.

The installer adds Tide to the ones it finds, and `tide upgrade` keeps it up to date. Installed one later? Run:

```sh
tide editors
```

An editor window that's open gets a newer extension once it's reloaded: **Developer: Reload Window**.

Renaming a file or a folder in the Explorer asks `tidels` first, so the paths of the assets it holds follow it; the extension asks only about the files assets name, and the folders above them. Moving several files together updates all their asset paths in the same edit, across the games and shared packages affected by the move. A shared package's assets update every consuming game in the workspace. CMake games that list individual files with `SOURCES` work the same way.

The extension runs `tidels` from your `PATH`, or from where `tide` is installed; in a folder with a `build/tools/tidels`, like Tide's own repo once it's built, it runs that one. The `tide.server.path` setting runs another one: in it, `~` is your home folder, and `${workspaceFolder}` and relative paths are the folder whose settings set it. After updating the server, run **Tide: Restart Language Server**. The `tide.trace.server` setting shows what the editor and the server say to each other, in the **Tide Trace** output.

In a folder you haven't trusted (VS Code's Restricted Mode), nothing from it runs: the server is the `tidels` that comes with `tide`, or one your user settings name outside the workspace. Servers on `PATH` inside the workspace are skipped too, including paths through symlinks or junctions. Games don't run, since running one runs its code. Trusting the folder restarts the server.

### Running the game

The play button above a `.tide` file runs its game, which is the folder you opened, or the folder of the game it's in when that has a `tide.packages` of its own; a [package](../packages/using-and-writing.md)'s file runs the open game that lists it:

- **Tide: Run** plays it in a window of its own, as `tide run` does.
- **Tide: Run on the Web** plays it in a browser tab beside your code: VS Code's integrated browser, or the Simple Browser in editors without it. A game only gets keys while its tab has focus, and the editor may pause it while its tab is hidden, so keep it in a group of its own.
- **Tide: Run on Android** plays it on the Android phone or emulator that's connected, as `tide run --android` does (see [Android](cli.md#android)).

Each runs `tide run` in a terminal of its own, so saving a file reloads the game (on Android, builds it again and starts it over), and typing `r` and Enter in that terminal starts it over. Running it again starts the game over. The first run on Android may ask to download Android's tools from Google: type `y` and Enter in that terminal to accept. The `tide.path` setting runs another `tide`.

Starting a replacement waits for the preceding run's processes to stop. Closing its terminal stops the run too. If the editor cannot confirm cleanup, it explains how to stop the remaining processes before running again.

On the web, each reload says what it did in the page's console: **Tide: Game Developer Tools** shows it.

## JetBrains IDEs

CLion, Rider, IntelliJ and the others, 2024.2 or later: install the Tide plugin from **Settings > Plugins > Marketplace**. It brings the LSP4IJ plugin it needs.

An existing `.tprefab` offers **Open Tide Prefab Editor** in its project/editor
context menu. It saves that prefab and launches the installed `tide` in the
file's game context. The project must be trusted. The scene app is a separate
native window; save scripts in the IDE to refresh its preview.

The plugin runs the `tidels` chosen under **Settings > Languages & Frameworks > Tide**, if any; else the project's own `build/tools/tidels`, like Tide's repo once it's built, once you trust the project; else the one on your `PATH`, or where `tide` is installed. When there's none, `.tide` files say so above the code, with links to install `tide`, choose a `tidels`, or try again. The **Language Servers** tool window restarts the server, after an update, and shows what it and the IDE say to each other.

Renaming a file or a folder (Refactor > Rename, or in the Project view) asks `tidels` first, so the paths of the assets it holds follow it. LSP4IJ asks about every file, and `tidels` answers from the game's assets.

JetBrains' dark color schemes draw type names like plain text, so Tide's types stay uncolored until you pick colors. Each kind of type has its own color under **Settings > Editor > Color Scheme > Language Server**:

- **Struct type**: components
- **Classes > Class reference** and **Class declaration**: singletons
- **Interface**: inputs
- **Type**: built-in types such as `Color`, `Entity` and `Devices`

### Running the game

Right-click a `.tide` file and pick **Run**, or press Ctrl+Shift+F10 in it (Ctrl+Shift+R on macOS), to run its game, which is the project's folder, or the folder of the game it's in when that has a `tide.packages` of its own; a [package](../packages/using-and-writing.md)'s file runs the project's game when it lists the package. The run is named after the folder, like `mygame`:

- **mygame** plays it in a window of its own, as `tide run` does.
- **mygame (web)** plays it on the web, in the **Tide Game** tool window. Its **Open DevTools** button shows the page's console, where each reload says what it did. The game only gets keys while the tool window has focus.
- **mygame (Android)** plays it on the Android phone or emulator that's connected, as `tide run --android` does (see [Android](cli.md#android)).

They're Tide run configurations, which you can also make and edit under **Run > Edit Configurations**, where **Play it** picks among the three. They use `tide run`. `tide`'s output is in the Run tool window, where an error's place links to the file: saving a file reloads the game (on Android, builds it again and starts it over), and typing `r` and Enter there starts it over. The first run on Android may ask to download Android's tools from Google: type `y` and Enter there to accept. A run configuration can run another `tide`.

## Other editors

`tide`'s `editors` folder holds the TextMate grammar (`tide-syntax`), a VS Code-style bundle, for highlighting in editors that load those. Any editor with a language server client can run `tidels`, from `tide`'s `bin` folder, for everything else. It talks over stdin and stdout.
