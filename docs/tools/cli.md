---
description: "Use the tide command to run and build native, web and Android games, hot reload code, manage packages and capture scripted headless runs."
---

# The tide command

`tide` builds and runs games. A game includes the `.tide` scripts and `.tprefab` authored trees in its folder and subfolders, with the `.c` and `.cpp` files and libraries there when it calls C (see [Calling C](../c/functions.md)), and the packages its `tide.packages` lists (see [Packages](../packages/using-and-writing.md)). Commands take the game's folder, or use the current one. Saving a prefab also triggers the normal watched build; existing live instances are not automatically rewritten with new authored values.

```
tide <command> [folder] [options]
```

| Command | What it does |
|---|---|
| `tide run [folder]` | Builds the game, plays it and reloads when you save |
| `tide build [folder]` | Builds the game into `<folder>/build`, named after the folder |
| `tide unity [folder] -o <path>` | Builds the game for the [Tide package in a Unity project](#unity), as its files change |
| `tide schedule [folder]` | Shows which systems can run at the same time, and why the others wait |
| `tide shaders [folder]` | Shows the shaders the game's [frags and verts](../game/shaders.md) compile to, as GLSL and WGSL |
| `tide add <source>` | Adds a package to the game in the current folder: from git, a folder, or one that comes with `tide` |
| `tide update [package]` | Moves the game's packages from git to the newest commit of what they follow |
| `tide eject <package>` | Copies one of the game's packages into the game's folder, to change as the game's own |
| `tide pin [version]` | Pins the version of `tide` the game builds with, in its `tide.packages` ([Packages](../packages/using-and-writing.md)) |
| `tide fetch [folder]` | Downloads the game's packages from git that this machine doesn't have yet, and builds nothing |
| `tide editors` | Installs bundled plugins into detected VS Code family editors and existing JetBrains IDE profiles |
| `tide ide <editor>` | Prints setup for Emacs, Vim or Neovim using the installed Tide files |
| `tide open [file.tprefab \| folder]` | Opens the native [scene editor](editors.md#native-scene-editor), ready to open or create an authored tree |
| `tide upgrade` | Updates tide to the newest version |
| `tide version` | Shows tide's version and channel, where it's installed, and which compilers it found |
| `tide telemetry on\|off\|status` | Turns anonymous [usage data](telemetry.md) on or off, or shows what's sent |
| `tide help [command \| argument]` | Lists commands, or shows details for one command or argument |
| `tide login` / `tide logout` | Authorizes or revokes this CLI's hosting access through the browser |
| `tide deploy [folder]` | Creates or reuses a project and saves its link in `tide.deploy.json`; builds and uploads locally. `--server-only` omits the client, `--bundle-only` only builds |
| `tide cc <options>` | clang, built into tide, for C you build yourself (see [Calling C](../c/functions.md#where-the-c-goes)) |

Project build and package commands automatically use the version pinned with
`tide pin`, asking before downloading a missing release into the shared
version cache. See [Pinning Tide itself](../packages/using-and-writing.md#pinning-tide-itself).

Managed deployment, project selection, prepaid credit and current availability
are described in [Managed hosting](../multiplayer/hosting.md).

`tide` and `tide help` show a short command list. Ask for a topic to see its
usage, options, defaults and limits:

```sh
tide help run
tide help --frames
tide help deploy --project
tide run --help
```

For commands such as `run` and `build`, `tide <command> --help` (or `-h`)
shows the same help as `tide help <command>`. `tide cc --help` shows Clang's
own options in packages with the built-in compiler.
Arguments shared by commands show each meaning; `tide help open --project`
selects the editor's project folder, while `tide help deploy --project`
describes the hosting project. Positional arguments have topics too:
`tide help folder`, `tide help source`, and `tide help package`.
Help works without a game or complete installation and performs no command
actions. Unknown topics or arguments return exit code 2. Bare `tide` keeps
exit code 2; explicit help returns 0.

## ide

```sh
tide ide emacs
tide ide vim
tide ide neovim
tide help ide
```

Copy the printed configuration into your editor's init file, then restart the
editor. Tide bundles the modes and prints their full installed paths, including
the matching `tidels` executable. No source checkout or separate mode download
is needed. The helper prints configuration only; it does not edit files or
launch an editor.

`emacs` targets Emacs 29.1+ with Eglot; `neovim` targets Neovim 0.11+. Their
configurations connect to `tidels` automatically when opening Tide files, so
enable them for projects you trust. `vim` targets Vim 8+ with highlighting and
indentation; configure a separate LSP client with the server command printed
in its comments. See [Emacs setup](editors.md#emacs) and
[Vim and Neovim setup](editors.md#vim-and-neovim) for controls and details.

Missing packaged modes or server files return exit code 1 with reinstall
guidance. An unsupported editor or extra arguments return 2. If you move your
Tide installation, run the helper again and replace the old configuration.
Vim/Neovim setup reports installation paths containing `*`, `?`, `[` or `]`:
their runtime search treats these as wildcards. Use a Tide installation path
without those characters.

## open

```sh
tide open .
tide open scenes/Level.tprefab
tide open scenes/Level.tprefab --project .
tide open scenes/Level.tprefab --check
tide open scenes/Level.tprefab --preview
```

The desktop app edits rooted [prefabs and scenes](../game/scenes.md#authored-prefabs).
With a project folder, or no argument for the current folder, it opens a workspace
where New Scene/New Prefab creates the first authored file. `--check` and preview
flags require an existing `.tprefab` argument.
It uses the nearest enclosing `tide.packages` as the project root, or the
prefab's folder if there is none. Use `--project <folder>` when scripts live
elsewhere in your game's folder. The selected root is visible in the window.
The app uses folder/package discovery; it does not read CMake game
manifests. Packages must already be available locally (`tide fetch` downloads them).

`tide open --check` runs the same authoring validation without opening a window and
returns 1 for project errors. It inspects asset metadata, rather than compiling/importing
the full game. For automated editor UI runs, `--frames`, `--capture` and `--script` use
the normal [hidden input/capture loop](#scripts). The default editor size is
1440 by 900; the editor's `--size W H` accepts 1100..8192 by 650..8192, unlike a
game's `tide run --size`. `TIDE_HIDDEN` requires one of these bounded modes or `--check`.

See [the editor controls and current limits](editors.md#native-scene-editor).
New Scene/New Prefab create rooted authored files in that project; Play builds
the selected tree with the normal game lifecycle. Set startup saves a persistent
project selection separately from Play.

`--preview` starts with compiled views enabled on authored match entities;
`--local-preview` uses local placement. Only `[RunInEditor]` and `[EditorOnly]`
views execute, with no gameplay initialization or systems. The full native build
toolchain is required. Bounded captures/scripts wait for the current preview
build and return 1 if it fails, rather than capturing stale state. `--check`
still performs only front-end validation, even with a preview flag.

## run and build

| Option | What it does |
|---|---|
| `--release` | Optimizes game and package code for testing and profiling, keeping symbols and diagnostics; standalone builds leave out unused code, data and assets |
| `--distribution` | Builds for shipping: `--release` plus symbol/name stripping, shader minification and packaging (smaller web pages, Android app bundles) |
| `-O0`, `-O1`, `-O2`, `-O3` | Overrides engine, game and package C/C++ optimization; deterministic math and the chosen build mode stay unchanged |
| `--web` | A web page (WebGPU, or WebGL 2 where the browser has none), built with clang's WebAssembly target |
| `--android` | An Android app, for phones and the emulator (see [Android](#android)) |
| `--scene <Name>` | Starts in the named prefab or scene for this run/build, over the project's `startScene` setting; use the qualified name for a declaration in a namespace |
| `--define <SYMBOL>` | Defines a symbol for the game's [`#if` lines](../language/namespaces.md#symbols-of-your-own), as many times as there are symbols. `DEBUG` is defined in the default debug mode. `schedule` and `shaders` take it too |
| `--title <title>` | The window's title, over the game's `title` setting (default: that, or the folder's name) |
| `--stats` | Shows the [stats](#stats) in the window's corner, as the game's `stats` setting does. `run` starts them open |
| `--debug-views` | Turns the [debug views](debug-views.md) on, as the game's `debugViews` setting does: F7 shows the profiler, which says where a frame's time and a tick's go |
| `--size <width> <height>` | `run` only: the window's size in pixels, 960 by 540 without it (not on the web or Android) |
| `--capture`, `--record`, `--capture-audio`, `--microphone`, `--frames`, `--script`, `--interactive`, `--scale`, `--players` | `run` only: plays the game [without a window](#without-a-window), to see what it draws and hear what it plays, with [several players](#several-players) if you like |
| `--latency <ms>`, `--jitter <ms>`, `--loss <percent>` | `run` only: plays a match this machine joins as if it were further from its server (see [A slow network](#a-slow-network)) |
| `--seed <n>` | `run` only: the seed of the matches the game starts, [`Random.seed`](../language/math.md#the-match-s-seed), from 0 to 2147483647. Without it, every match gets another one |
| `--data-dir <folder>` | `run` only, native desktop: use this save directory, relative to where the command was invoked. Useful for disposable test saves |
| `--shards <n>`, `--shard-width <units>` | `run` only: a [sharded game](../multiplayer/shards.md)'s match as `n` regions along x, each this wide (16 times its `reach` without it), all on this machine |
| `-o <path>` | `build` only: where the program goes |
| `--single-file` | `build --web` only: one self-contained `.html` with the game inside, which opens from disk, instead of a folder whose page downloads the game (see [The page while it loads](#the-page-while-it-loads)) |
| `--no-open` | `run --web` only: serves the page without opening a browser |

Choose a startup without writing a `Main` scene:

```sh
tide run . --scene Levels.Start --stats
tide build . --scene Levels.Start
```

For a persistent project choice, put `settings { startScene = Levels.Start; }`
in a `.tide` file. The command-line selection overrides that setting for the
build and changes no source. It takes a declaration name, not a file path or
string expression. CLI names have no file's namespace or `using` context, so
write `Levels.Start` for a declaration in `namespace Levels;`. Unknown names
and invalid project settings are compiler errors.

A prefab starts as a match scene with its authored root and complete tree, then
runs normal initialization, systems, game views, camera and input. A selected
ordinary scene keeps its match/local placement. With neither selection, the
existing `scene Main` starts as before; a prefab named Main alone is not an
entry point. See [Scenes](../game/scenes.md#startup-scene).

Watched builds keep the command's `--scene` selection. Hot reload follows the
usual state-retention rules; changing startup or prefab text does not recreate
existing live instances. Restart the run to play from fresh initial state.
`--scene` leaves the output filename unchanged; use `-o` for a separate build
artifact.

By default, the engine is optimized in every build. In the default debug mode, generated game code and the game's and packages' C/C++ stay unoptimized for debugging; engine and game code retain debug symbols and assertions, and `DEBUG` is defined. A native debugger stops at breakpoints in `.tide` files and shows the game's values as Tide's, with `<name>.natvis` (also in the `.pdb` on Windows) and `<name>_lldb.py` beside the program (see [Debugging](debugging.md)). Engine objects are cached, so ordinary game edits reuse them. This applies to native, web and Android builds, including hot reload.

Use `tide run --release --stats` to test a large game at optimized speed. Release retains debug symbols, readable profiling and diagnostic names, C assertions and native debugger visualizers, and leaves out unused code, data and assets as a distribution does. Optimized stepping may skip inlined calls or show locals as unavailable. Both optimized modes leave `DEBUG` undefined. Debug, release and distribution use separate `.tide/<target>-<mode>/` caches.

Choose one optimization override to experiment, for example `tide run --release -O3 --stats` or `tide build --distribution -O1`. `-O0` disables optimization; `-O1`, `-O2` and `-O3` select increasing optimization levels, which are not guaranteed to improve a particular workload. The override applies to engine sources, generated game code and game/package C/C++, including hot reload, native, web, Android, servers and `tide unity`. Prebuilt libraries keep their own optimization. Without an override, debug game code uses `-O0`, debug engine code uses `-O2`, and release/distribution use `-O2` for both.

The override changes no build-mode policy: `tide run -O3` still defines `DEBUG`, and `tide build --release -O0` still keeps release's symbols and diagnostics. Deterministic math remains enforced, including disabled FMA contraction and fast-math. Explicit levels have separate caches, such as `.tide/native-release-O3/`, so switching experiments can reuse their objects. For builds with custom C, the selected level also enters simulation compatibility because C can branch on optimization macros. Unsupported levels and multiple optimization flags are errors.

Standalone `tide build --distribution` removes readable system/view profiling labels, shader labels, event/command diagnostic names, and assertion source lines and paths. Asset loading uses the pack's existing SHA-256 name IDs directly. Assertions still check their conditions and keep messages you wrote; profiles and failures use IDs. Resolve them with `.tide/<target>-distribution/gen/game.names`, a private diagnostic map for that build. Keep that map with your build records, and distribute only the finished game files, never `.tide/`, generated C, debug files or the map. Replacing a native debug output with a distribution removes its old debugger sidecars. Debug, release, watched/hot-reload and Unity builds retain their names.

Shipping shader source also replaces function, type, field, parameter and local identifiers derived from Tide names with short tokens. Renderer bindings and shader entry points stay fixed. The private map records the original identifiers for each shader stage. Expressions, numeric literals and layouts stay the same.

Standalone macOS distribution executables expose no public symbols. Android's shipping `libgame.so` exports only `ANativeActivity_onCreate`, the entry point Android loads. Additional shared libraries supplied by your game keep their own exports; code needing a public dynamic API should provide it in a separate library. Hot-reload and Unity libraries retain their host APIs.

This makes reverse engineering less convenient; it does not encrypt a game or prevent copying a downloaded demo. Game text, lookup keys, model contents and executable shader source remain available where the game needs them. For a custom compiler pipeline, `tidec --strip-names` applies the same metadata policy and writes `<name>.names` beside its generated C. CMake's standard-host `Release`/`MinSizeRel` games enable it automatically; custom hosts, layout/embed targets, `RelWithDebInfo` and explicit debugger-visualizer builds retain names unless their compiler pipeline explicitly requests stripping. `tide_add_game(... STRIP_NAMES)` opts in explicitly, including for custom hosts. Required layout/embed metadata still retains its names. The repository's `release` preset uses `RelWithDebInfo` for profiling; use `tide build --distribution` for a shipping build.

`tide build --distribution --web` makes a folder to put online as it is, `.build/web/`: `index.html`, and the game's program and assets beside it, `index.wasm` and `index.pack` (none for a game without assets). The page downloads them showing how much has come, and the browser keeps them in its cache between visits until a new build changes them: the page asks for each by its hash. Such a page needs a server, even on your own machine: a page opened from disk can't download its files, and says so. With `-o <path>.html`, the files go beside that page, named after it.

`--single-file` makes one self-contained `.html` file instead, `.build/<name>.html`, with the game inside, which opens straight from disk. Web distributions compress the program and assets inside that file, and decoding is included, so playing it needs no extra tools or downloads. Distribution pages of either kind use a smaller page runtime. Debug, release and hot-reload pages keep the readable runtime and original payloads.

### The page while it loads

Until the game first draws, the page shows what it's doing: how much of the game has downloaded, then that it's starting. A game's `icon.png` (in its folder, as for [Android](#android)) shows above that, and is the page's icon in the browser's tab. For a page of your own, put a `shell.html` in the game's folder: it replaces the page around the game, its loader included, under `tide build --web` and `tide run --web`. It's a whole HTML page with `{{{ SCRIPT }}}` at the end of its `<body>`, where the game's script goes. Start from [Tide's own](https://github.com/Tide-Engine/tide-engine/blob/dev/platform/web/shell.html):

- An element with the ID `tide-loading` is the loader: the game takes it away once it has drawn, and fills in the elements in it with the IDs `tide-loading-bar` (a bar, whose first child's width is how much has downloaded), `tide-loading-text` (what's happening, in words) and `tide-loading-logo` (an `<img>` for the icon). Leave it out for no loader.
- `Tide.onProgress`, set in the `var Tide = { ... }` before the game's script, is told each step for a loader of your own: `{ stage, loaded, total, error }`, where `stage` is `download` (`loaded` of `total` bytes), `compile`, `waiting` (for `Tide.start()`, on a page that set `Tide.wait`), `start` (the game is running, until it first draws), `done` (it drew) or `error` (it couldn't start, and `error` says why).

Standalone release and distribution builds remove unreachable native code and data; runtime references and required C or shared-library exports are preserved. Tide-only games also omit [unreferenced assets](../game/assets.md#release-builds). Distribution builds also strip debug symbols. Debug and hot-reload builds keep all code and every declared asset.

`tide run` builds and plays with [hot reload](#hot-reload) by default, on desktop, the web or Android. Runs [without a window](#without-a-window) play once. On desktop, closing the game window ends the command too.

A game's [assets](../game/assets.md) (its `asset` declarations) are packed with it: beside the program as `<name>.pack`, which has to stay next to it; inside the web page; and in the APK. Nothing else in the folder goes in. Under `tide run`, a file an asset names is watched like the game's code, and a saved image is in the running game within a moment, with no rebuild.

A web page draws with WebGPU where the browser has it on a GPU, and with WebGL 2 where it doesn't; games draw the same with either. Adding `?backend=webgl` (or `?backend=webgpu`) to the page's address picks one, for example to check a problem a player reports with the other.

A desktop game draws with Vulkan where the machine has a Vulkan 1.3 driver, and with OpenGL 3.3 where it doesn't; games draw the same with either, and nothing in a game says which. The `TIDE_BACKEND` environment variable picks one for a run: `TIDE_BACKEND=opengl`, or `TIDE_BACKEND=vulkan`, which ends the program with a message where there's no Vulkan rather than falling back.

On a Mac, Vulkan is MoltenVK, Vulkan on Metal: `tide build` puts `libMoltenVK.dylib` beside the game, which uses it where the Mac has no Vulkan loader of its own. Ship it with the game; its license is in the tide package's `licenses/moltenvk`.

An Android game draws with Vulkan where the device has Vulkan 1.3, and with OpenGL ES 3 where it doesn't. An app started with `adb shell am start` takes the same choice as an extra of its intent: `--es tide.backend opengl` or `--es tide.backend vulkan`.

### Stats

`--stats` shows what the game is doing in the window's bottom right corner. So does `stats = true;` in the game's [settings](../language/basics.md#settings), with no option to remember:

```csharp
settings
{
    stats = true;
}
```

They're a drawer. Closed, it's one line: the frame rate, and in a match another machine runs, your ping. A click or a tap on that line opens it, and so does **F8**: the rest is listed above the line, which stays where it is, so a click in the same place closes it again.

Documentation examples, sample games and the playground enable stats by default, including their separate playable pages. A web game's `stats` query option passes `--stats` to the program: `game.html?stats`, or `game.html?backend=webgl&stats` alongside other options. This enables the drawer without changing its configured initial open/closed state; an ordinary standalone page starts with the one line.

- **It starts closed** when the game asks, with its `stats` setting, and when a build does (`tide build --stats`, CMake's `STATS`): what players get is the one line. **`tide run --stats` starts it open**, since whoever typed that wants to read it. Under `tide run`, a hot reload leaves it as you left it.
- **A click or a tap on it is never the game's.** Neither the game's input nor its views read that press, from the frame it lands in. A press that began elsewhere and crosses the drawer stays the game's. The game still sees F8, as it sees every key.
- **Closed, it measures nothing.** The frame rate is a count of frames. What the list shows (a frame's work, each system's time, the memory) is measured while the list is open, so a game can ship with the line.
- **The bottom right corner is its own.** A game that asks for the stats keeps its own buttons out of that corner: the line is about 80 pixels wide and 40 high, and about 190 wide with a ping in it.

The list is two columns, a label on the left and its value on the right, in four groups: what a frame takes, the match, the machine, and the systems that take longest.

| Line | What it is |
|---|---|
| `60 fps` | Frames a second, over the last second. With `ping 34 ms` after it in a match another machine runs: the line that's always there |
| `cpu` | What a frame's work takes on its thread, all of it: the ticks it runs, the views and the drawing, without the wait for the display |
| `render` | Listed under `cpu` as the part of it that prepares and submits the frame's draw list, including its render targets |
| `gpu (22 draw calls)` | GPU drawing time, including render targets, on Vulkan (desktop and Android), desktop OpenGL and WebGPU devices with timestamp queries, with how many draw calls the frame took in its label: a run of shapes, of text or of one mesh's instances is one. Results arrive asynchronously, and it overlaps `cpu`, so the two aren't added. Where the backend has no GPU timer (WebGL 2, OpenGL ES 3), the draw calls have a line of their own, `draw calls` |
| `vertices` | Vertices submitted this frame across all render-target passes, counting each index for each instance at its selected LOD. Includes shapes, text, meshes, blits and clear triangles; excludes the host's overlays. A quad counts as six, even with four shared corners. Clipped or culled geometry still counts; this measures submitted geometry, not how many pixels were visible or how often the GPU ran the vertex shader after caching |
| `ping (delay 3 ticks)` | The network's round trip to the machine running the match, in the list when it's this machine, and in its label the input delay when there is one (see [Input delay](../multiplayer/matches.md#input-delay)) |
| `network` | What goes over the network each second, up and down: `↑2.0  ↓0.5 KB/s` |
| `entities` | How many entities the match has |
| `threads` | The threads ticks run on |
| `memory` | The memory the program has, as the system counts it. On the web it's the program's WebAssembly memory, which only grows |
| `vram` | What the game's drawing has on the GPU, by the engine's own count: every texture, mesh and render target it made, by what each holds. Not the window's own framebuffer, nor what the driver keeps beside them, so it's the same number on every backend |
| `renderer` | What draws: Vulkan, OpenGL 3.3, OpenGL ES 3, WebGPU or WebGL 2 |
| `Move` | The three systems that took longest on average per measured run, over the last half second. A system keeps its row while it stays among the three, and one that joins them takes the row of the one it replaces, so the rows don't shuffle |

Vertex counts use decimal suffixes: `123.5K` for thousands, `97M` for millions and `4.3B` for billions, continuing with `T`, `P` and `E` for larger counts. Counts below 1,000 stay exact; larger counts round to one decimal, with trailing zeroes omitted.

Timings change twice a second; frame rate and network rates change once a second. The panel is as wide as its widest label and value, and while it's open it keeps the widest it has been, so its edge doesn't jump as a long system name comes and goes. The drawer shows once there's a first frame rate, a quarter of a second in. GPU time excludes the drawer and display wait; on WebGPU it sums render-pass durations. WebGL 2 and OpenGL ES 3 have no GPU timer here. Outside a match, the list has the lines that aren't a match's. On a screen too low for the whole list, it keeps the lines there's room for, from its top.

A setting is part of the build, so `tide build` keeps it: take `stats` out before players get the game, unless the frame rate in a corner is what you want them to have.

::: info Provisional
The drawer is new. Its key, what its one line says, and that only `tide run --stats` starts it open may still change.
:::

### Threads on the web

A game's ticks run on every core when the page is cross-origin isolated: served with these two headers, which let it share its memory with the workers its threads run in.

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

`tide run --web --stats` serves the page with them. Where a page is put online, the host has to send them: on itch.io, it's the **SharedArrayBuffer support** option in the game's embed settings. A page without them, or opened from disk, runs the same game on one thread, with the same results, so web players on either kind of page play together. Workers only start once a tick has enough work for them.

## Android

`tide build --android` makes an Android app, `<folder>/.build/<name>.apk`, for phones and tablets (arm64) and Android's emulator (x86-64), on Android 10 and up. With `--distribution`, it also makes `<name>.aab`, for [Google Play](#google-play). `tide run --android` builds it, installs it on the phone or emulator that's connected, starts it, and shows what it prints until it ends. `--host`, `--join` and `--connect` go to it as they do on a computer, and Android players play in the same rooms as everyone else.

To play on your phone, turn on USB debugging once: in **Settings > About phone**, tap **Build number** seven times, then turn on **USB debugging** in **System > Developer options**. Connect the phone, and allow your computer when it asks.

Building for Android needs two things of Google's: the NDK, Android's C library and headers, and adb, which installs apps. `tide` uses the ones Android Studio installs. Without them, the first `tide build --android` asks to download them from Google, under Google's license (the [Android SDK License Agreement](https://developer.android.com/studio/terms)), and keeps what it needs of them next to itself: about 30 MB. Set `TIDE_ACCEPT_ANDROID_LICENSE=1` to accept it without being asked, as a build server would.

- The app's ID is the game's `appId` setting (see [Settings](../language/basics.md#settings)). Without one, it's `dev.tide.<the game's name>`, which is fine for testing, but `--distribution` needs one.
- Its name under its icon is the game's `title` setting, or `--title`, or else the game's name.
- Its icon is `icon.png` in the game's folder: a square PNG, 512 by 512 pixels is plenty. Without one, it's Tide's. It's a web page's icon too, shown while the page loads (see [The page while it loads](#the-page-while-it-loads)).
- The Android libraries in the game's folder (`.so` files) go in the app with the game (see [Calling C](../c/functions.md#android)).
- `tide` signs apps with a key it makes on your computer the first time, `~/.android/tide.pem`. A phone only takes an update to an app signed with the same key, so keep a copy of it somewhere safe, and don't share it: whoever has it can sign apps as you. `tide run --android` replaces an app another computer installed.
- To sign with another key (a build server's, or one you already have), set `TIDE_ANDROID_KEY` to its file: PEM, the private key (RSA, 2048 bits or more) and then its certificate. `openssl pkcs12 -in upload.p12 -nodes -out key.pem` makes one from a keystore; a `.jks` keystore becomes a `.p12` first with `keytool -importkeystore -srckeystore upload.jks -destkeystore upload.p12 -deststoretype PKCS12`.
- Each build's version code, which Android only updates an app to a higher one of, is the minutes since 2020 began, so every build is newer than the last. Where `SOURCE_DATE_EPOCH` is set, it stands in for now. The version people see is the game's `version` setting, like `1.2.0`, or else 1.0.
- The back button comes to the game as Escape, as in Unity.
- With `tide run --android`, saving a `.tide` or C file builds the app again, installs it and starts it over: a phone can't swap code into a running app, so the match starts over too. Type `r` and press Enter to start it over yourself; closing the app ends the run.

### Google Play

`<name>.aab` is an App Bundle, which is what Google Play takes: Play makes the APKs from it, each phone getting only the library for its CPU. To put a game on Play:

1. Make a developer account in the [Play Console](https://play.google.com/console) (Google charges a one-time fee), and create the app there.
2. Give the game its `appId`, and build it with `tide build --android --distribution`.
3. Upload `.build/<name>.aab` to a release: internal testing is the quickest way to try it on your own phone. Play signs the APKs it makes with a key of its own, which it keeps (Play App Signing); `~/.android/tide.pem` is then your upload key, and Play only takes uploads signed with it.
4. For an update, build again and upload the new `.aab`: its version code is higher than the last one's.

Play's other requirements, like the version of Android an app is made for (Android 16) and support for phones with 16 KiB memory pages, `tide` meets itself. A library the game brings has to be linked for those pages too, and `tide` warns when one isn't (see [Calling C](../c/functions.md#android)). If you lose your upload key, Play can take a new one (the app's **App integrity** page asks for its certificate: the part of the key's file from `-----BEGIN CERTIFICATE-----` on).

## Hot reload

While `tide run` plays a game, saving a `.tide` file rebuilds it, and so does saving one of its C or C++ files, headers or libraries. The game carries on with the new code in the same window:

- If you changed only code (systems, views, event handlers, methods), the match and everything local, like menus, carry on where they are.
- If you changed data (components, fields, singletons, the input), the game is carried over to it by name, and goes on from where it was.
- If the new code has errors, `tide` prints them and the game keeps running its last build.

Carrying the game over works like this:

- Every field of the same name keeps its value. An `int` field that became a `float` is converted, and so is an int vector that became a float vector of the same size. An enum field keeps its member by name; a value that's none of its members (see [From a number](../language/basics.md#from-a-number)) gets the field's default.
- A new field gets its declared default. So does a field whose type changed to one it doesn't convert to, like `float2` to `float3`. New text and list fields start empty.
- Renaming a field is removing it and adding a new one: its value is lost.
- An entity keeps its ID. When you remove a component, entities that had it keep their other components. An entity whose remaining components can't exist together in the new code is dropped.
- A changed default doesn't change existing entities, only new ones.
- A waiting [task](../game/tasks.md) carries on when its async function's code is the same, and is dropped when it changed: where it was waiting is gone.
- [Local fields](../game/local-state.md#local-fields) carry over with the rest of the local state, by name: a mesh a view baked stays baked. Change what bakes it and it stays as it was, until the view bakes again or you start the game over.
- [Local handlers](../game/events.md#local-handlers) of the match's events hear nothing twice across a reload. The ticks a client had predicted and not had confirmed by then tell its `verified` handlers nothing, when the new build confirms them as they were.
- If the scene the game is in is gone, the game starts over.

`tide` says what happened after each reload, like `reloaded, and carried the game over to its new data layout; 1 field reset; 2 entities dropped`.

When the game gets into a state you don't want, press **Ctrl+R** in the game, or type `r` and press Enter in `tide`'s terminal, to fully reload it. On the web, Cmd+R does the same on macOS. This starts the match, local state, GUI and the game's C or C++ globals over, without carrying state from the old run. With a match on several windows (`--host` in one, `--connect localhost` in another), each window hot reloads when you save, and players stay in the match. A window that joined another's match joins it again a second after that match ends without it: when the other window starts it over, or is still on a build with another data layout. On the web, a hot reload goes on with the match closed for now: the room closes, and the other players drop out.

`tide run --web --stats` hot reloads too. `tide` serves the game's page at an address on your machine, like `http://127.0.0.1:52407/?stats`, opens it in your browser, and keeps running until you press Ctrl+C. What each hot reload did shows in the browser's console. VS Code and JetBrains IDEs can play it beside your code instead (see [Editors](editors.md)).

Hot reload is for `tide run`. `tide build` makes a plain program, or a page with nothing of it. Runs [without a window](#without-a-window) play once, with no hot reload.

A debugger attached to the game stays attached across reloads: each build is a library with its own debug information, and breakpoints in `.tide` files carry over to it (see [Debugging](debugging.md)).

## Unity

```sh
tide unity Assets -o Library/Tide --assets Assets
```

Unity's Tide package (`unity/com.tide.unity` in Tide's repository, added to a
project from its git URL) runs a game's simulation and networking inside the
Unity editor: Tide simulates, predicts and rolls back, and Unity draws. The
package starts this command itself while the editor is open, so you rarely type
it. It builds into the path it's given: the host Unity loads, then a library of
the game each time its files change, which the running game takes over as
`tide run` does, the game's schema, which the package reads the match by,
and an outline of what each file declares and which packages the
game has, which the package's inspectors show. It ends when its input closes, as the editor's package closes it.
`--once` builds once and ends. Unity and other Tide builds of the same game
play in the same matches. This is early, and its details may change.
Debug and release libraries carry debug information: a native debugger attached to the Unity editor
stops at breakpoints in `.tide` files, with the game's natvis in its `.pdb` and
`game.natvis` and `game_lldb.py` beside it (see [Debugging](debugging.md)).

The package builds with the version of `tide` its own commit is, which Unity
locks in `Packages/packages-lock.json`, so a branch of the project and everyone
on it build with one. A package that follows a release tag
(`...com.tide.unity#v0.5.0-nightly.12`) is that version; any other commit is
the highest release at it or before it, which the site answers
(`https://tide-engine.dev/releases/for/<commit>`, kept in `Library/Tide`).
Versions are installed side by side in the `versions` folder of where the
installer puts `tide` (`%LOCALAPPDATA%\Tide\versions\<version>`,
`~/.tide/versions/<version>`), or in the folder `TIDE_VERSIONS` names. A
version that isn't there yet is downloaded from the site and checked against its
release's checksums: in the editor once you say so (or Project Settings >
Tide's Install), while the editor goes on, and in a batch run, as on a build
server like GameCI, at once, with nothing installed beforehand. Point
`TIDE_VERSIONS` at a folder the server keeps between builds to download each
version once. A package in a folder on this machine has no commit: it builds
with the `tide` on `PATH` or where the installer puts it.

For engine development, launch Unity with `-tide-command "<local Tide>/bin/tide"`
(`tide.exe` on Windows). That editor process uses the local compiler/runtime,
including across script reloads; Project Settings > Tide shows the override.
Restart without the argument to follow the project pin. No override path is
saved, and old saved paths are ignored. A missing executable or malformed argument
fails explicitly. If Play reports an embed ABI mismatch, rebuild the development
compiler to match the package or relaunch normally. Tide checks the existing host
before entering Play. Updated VS Code and JetBrains plugins follow the active
selection and return to the pin when that editor exits, including after a crash.
A development package newer than the latest published compiler needs a matching
development build or release tag.

In a Unity project the game is the whole of `Assets`: every `.tide` file in it,
as every C# script is, but those in folders Unity leaves out (hidden ones, and
those whose names end in `~`). Its `tide.packages` is `Assets/tide.packages`,
and `tide add`, `update` and `eject` work on it from any folder of `Assets`. A
folder in `Assets` whose `tide.packages` says `package Name` is one of the
game's packages, with no line for it, much as an assembly definition makes an
assembly. C is only ever a package's: tide builds the C, headers and libraries
of a package's folder and none of the rest of `Assets`, where Unity's plugins
are, and warns of a `.c` beside the game's `.tide` files.

`--notified` is how the editor's package runs it: rather than going over every
file of `Assets` to see what changed, it builds when a `changed` line comes on
its input, which the package writes as Unity imports, moves or deletes the
game's files, and looks itself only at the files it built, so an edit saved
while Unity is in the background still builds at once. A `sync <n>` line asks
it to say `tide: synced <n>` once it has built everything it was told of,
which Play waits for. Each build starts with a `tide: building game-<n>` line,
which the editor shows as a task in its status bar until the build ends.

The game's components come to Unity as C# in `Packages/com.tide.game`, which
it writes from each build. While the game's code doesn't build, it writes them
from the game's declarations alone, leaving out its systems, views, handlers
and functions (what `--declarations` builds): a component a new prefab needs is
in Unity before the code that spawns the prefab builds. The game itself keeps
running its last build. Each generated type carries its Tide declaration's
doc (its comment lines) and the file it's declared in, as an XML `<summary>`
for C# editors' hovers; the line isn't written, so a line moving above a
declaration changes no C#. Each time it writes the C#, `tide unity` records
what became of the names the C# had before, a rename (with where the new name
is declared) or a removal, in `Library/Tide/renamed.json`, which the package
reads to explain a script's error on one of them. `tidels` renames reach the
project's C# as well; see [Editors](editors.md).

Package functions and shaders remain available for their field types and callback
defaults, including Render3D materials. Package bodies, assets and defaults still
need to check normally. Fields that require game-defined shader types or callback
defaults still need a successful game build; those game declarations are omitted
by this fallback.

`--assets Assets` makes the project's prefabs and scenes part of the game:
each one holding GameObjects with the package's TideEntity component is a Tide
prefab named after its file, read from its saved file (Unity's Force Text
serialization) with its prefab instances and variants expanded. A scene the
editor has unsaved changes to is read from the copy of it the package writes
as Play starts, `Library/Tide/unsaved/<the scene's path>`, while there is
one; the editor's first scene that was never saved is
`Library/Tide/unsaved/Untitled.unity`, the scene `UnityEngine.Scenes.Untitled`. A TideEntity is
an entity of that prefab, Unity's Transform and parent are its own, and its
Tide components are the game's components the GameObject has: each match
component is a Unity component (Add Component > Tide), its fields set in the
inspector as any component's, and a prefab instance's changes go over its
prefab's as Unity's do. In Play each one holds its entity's values in the match,
so a script reads them as fields (`GetComponent<Tide.Game.Health>().hp`), and
the match's writes come through each frame; a script's changes to it don't reach
the match, which a command's do. A component or field the game no longer has is
left out with nothing said, as Unity leaves out a removed C# field's value.
The rest of the game is C# too: `runner.Get<Tide.Game.Score>()` is a singleton
as views see it this frame; `runner.On<Tide.Game.Hit>(hit => ...)` hears the
match's events as the game's local handlers do, as soon as this machine has
them, and `runner.OnVerified` once the server confirms them, with an entity's
own view hearing those sent to it (`GetComponent<TideEntityView>().On`); and
`runner.Send(new Tide.Game.Buy { item = "sword" }, target)` sends a command,
its text included, to the match or to the entity a GameObject shows. Local
fields are on their components, shown in Play. Every Tide type has its C#: a
struct is a C# struct, and `List`, `Set`, `Map` and `Grid2`/`Grid3` are
`List<T>`, `Tide.TideSet<T>`, `Dictionary<K, V>` and `Tide.TideGrid2<T>`. A
component's struct, list, set, array and `char` fields are set in the
inspector and baked; its maps and grids are filled in by game code. Tide's
types keep their names where Unity has one too (Tide's `Transform`, `Time` and
`Font`, Box3D's `Collider`): a script using both namespaces writes the one it
means, `Tide.Game.Transform` or `UnityEngine.Transform`, as C# asks of an
ambiguous name.
Older projects list components on the TideEntity itself, which tide still reads:
Tools > Tide > Convert TideEntity Lists to Components makes them components of
their own. Everything from Unity is under the
`UnityEngine` namespace, named by its folders under `Assets`: a prefab is
`UnityEngine.Prefabs.Enemies.Goblin` for `Assets/Enemies/Goblin.prefab`, which
code spawns as any prefab, and a scene `UnityEngine.Scenes.Levels.Cave`, so
`Scene.Load` loads its entities and `with scene` filters by it. While only one
prefab or scene has a file's name, that name alone is it too:
`Spawn(Goblin)` and `Scene.Load(Cave)` after `using UnityEngine.Prefabs;` and
`using UnityEngine.Scenes;`. With two, the short name is unknown and the error
names both.
Build Settings' first scene loads as Main starts, as a player build starts with
it, and Unity loads and unloads each scene as the match does, a scene the match
loads twice as two. A scene loaded by hand, in the editor or by a script, is
local: nothing of it reaches the match. Each such entity gets a
`UnityEngine.Source` component, by which Unity shows it through its own GameObject.
A message about a prefab gives lines of the Tide text tide made of it, which it
writes to `<path>/prefabs`. With `--assets`, tide also keeps the project's
`Packages/com.tide.game` up to date: the game's schema and the C# tide writes of
it, a Unity component for each match component and a struct for each other type,
each written only when it changes. Keep that folder in source control, so a
fresh clone compiles before tide has built anything. Renaming a component or a
field in your editor (tidels) renames it in the project's prefabs and scenes
too, so they keep their values.

In Play, Window > Tide > Time Travel pauses a match the editor runs, steps it
a tick at a time, shows any tick since it started again (the GameObjects,
inspectors and Tide's drawing following it), goes on from a tick shown, and
saves and replays recordings of matches, each tick checked against the
recording, so a tester's bug report replays bit for bit and a changed build
shows the first tick it changes. With other machines in the match, it never
stops for them: the match goes on behind the tick shown. The package's README
has the details.

Tide's views draw in Unity's frame with Tide's own renderer: 2D, text, 3D
drawn with the camera's depth (so Tide's meshes and Unity's scene hide each
other), render targets, stencil and the game's own frags and verts, as Tide
draws them in its own window. The views' 3D camera is the runner's View Camera,
or the main camera. tide writes a Unity shader for each of the game's shaders,
and for Tide's own, into `Packages/com.tide.game/Resources/TideShaders`, which
Unity compiles for every graphics API: Direct3D 11 and 12, Vulkan, Metal,
OpenGL and WebGL 2. It draws in Unity's built-in render pipeline and in URP,
whose pass the package adds by itself. In Play, the editor's scene view shows
Tide's 3D too, through its own camera. The game's sound plays through Unity's
audio, and `Clip.Microphone` is Unity's microphone; a WebGL player's Tide
program plays through the page, and hears its microphone, itself.

A package's components are authored on GameObjects like the game's own, as
a `unity.json` in its folder says (see
[Packages](../packages/using-and-writing.md#unity-components)): with `box3d`,
a `Box` is sized by the GameObject's scale as a collider is, and drawn and
edited in the Scene view.

`tide build` and `tide run` take `--assets` too, for a dedicated server, or any
build, of a game made in Unity: `tide build --server --assets Assets` has the
same prefabs and scenes as the Unity players it serves. `tide schedule Assets
--assets Assets` checks such a game, its prefabs and scenes and its packages'
Tide for Unity, without building it.

`--web` builds the game once for a Unity WebGL player instead: `tide-game.js`,
which Unity's Tide package puts in the player's page beside Unity's own, and
the map of its prefabs and scenes. The package runs this as a WebGL player
builds. Serve the player cross-origin isolated, as any Tide page.

## Dedicated servers

```sh
tide run --server --anonymous --port 7777 --max-matches 64
tide build --server --release
```

`--server` builds a native console service with no window, rendering, audio,
views or local player. Its cache is separate from ordinary native builds, and
its default output is `<folder>/build/<name>-server` (with `.exe` on Windows),
beside its own `<name>-server.pack` when needed. `run` runs it once without hot
reload. Server executables support:

| Option | Meaning |
|---|---|
| `--anonymous` | Allow anonymous match creation and joining for quick startup |
| `--port <n>` | Listening UDP port, default 7777 |
| `--max-matches <n>` | Concurrent match limit, default 64, maximum 4096 |
| `--max-connections <n>` | Admitted/pending connection limit, default 256, maximum 65536 |
| `--udp-only` | Omit the shared WebRTC room |
| `--shards <n>`, `--shard-width <units>` | A [sharded game](../multiplayer/shards.md)'s world as `n` regions along x, each this wide, each a match; players join the world at match 0 |
| `--shard-most <n>` | Splits a sharded game's region in two when its shard owns more entities than this, and merges two neighbouring regions owning a quarter of it together |
| `--shard-journal <folder>` | Keeps a sharded game's world in a folder: a checkpoint now and then and every shard's ticks since, on the disk before anything a shard sends goes on. Started again with the same folder, the server goes on where the world was |
| `--frames <n>` | Bound a test run to this many service iterations |

The service uses wall-clock time and sleeps between iterations, normally about
one millisecond. `--frames` bounds service iterations; it does not render frames
or make networking deterministic. `TIDE_HIDDEN` requires this bound. Interactive
services stop on Ctrl+C or SIGTERM. Empty matches expire after 60 seconds by
default; a custom host can change that interval.

Clients use `--connect localhost:7777/0` to request a new match, or replace `0`
with its match ID. `--join K7QF2M/0` does the same through the server's WebRTC
room. The admitted client prints its match ID and exposes the complete address
through `Session.room`. Web clients use the room form.

Without anonymous policies, the game supplies
`void tide_server_entry(tide_hosting_desc *host)` in its C sources to configure
authentication, match startup and process callbacks. A local menu `Main` requires
a startup callback selecting a match scene. The C entry point is the first
implementation; a Tide orchestration declaration has no agreed syntax yet.
See [dedicated hosting](../c/network.md#dedicated-hosting).

Closed Tide-only server builds automatically retain only simulation assets.
Games or packages with C/C++ or prebuilt libraries retain all assets because C
can find entries by name. `--strip-server-assets` explicitly enables server
pruning for such projects when their C has no additional asset dependencies.
This option also selects `--server`. Source asset validation still runs for all
declarations. Standalone server links discard unreachable client code in debug
and release. `--web`, `--android` and capture/view options do not apply to a
server executable; browser players still connect through WebRTC.

## Multiplayer

`tide run` can start a match others join, or join one. For `--host`, the selected startup must be a match scene (an authored prefab also starts in the match); without a selection, this is `Main` (see [Scenes](../game/scenes.md)). A game that starts in a local menu does the same from code (see [Matches](../multiplayer/matches.md)).

| Option | What it does |
|---|---|
| `--host [port]` | Opens the match for others to join: in a room, whose code the game can show, and on UDP port 7777 by default (not on the web) |
| `--join <code>` | The match in the room with this code, like `K7QF2M` |
| `--connect <address>` | The match at an address, like `192.168.1.5` or `localhost:7777` (not on the web) |

To check a match with no window on screen, `--players` plays [several players](#several-players) at once and takes a picture from each.

### A slow network

On one machine, or on a fast network, a match feels better than it will online: what another player does shows up at once. To feel and tune it as players far apart will, make the way to the match's server worse:

| Option | What it does |
|---|---|
| `--latency <ms>` | Each datagram takes this many milliseconds more, each way, so the ping is twice that more |
| `--jitter <ms>` | ...and up to this many more, at random, each way, so datagrams also come out of order |
| `--loss <percent>` | This share of datagrams is lost, each way, from 0 to 100 |

```
tide run --connect 192.168.1.5 --latency 80 --jitter 10 --stats   # join a match on the network as if from far away
tide run --players 2 --latency 80 --script duel.txt               # two players with no window, the second 80 ms from the first
```

They're the way from this machine to a match it joins, with `--join`, `--connect`, a game's own `Session.Join` and `Session.Connect`, or a dedicated server's match. A machine that runs the match's server adds nothing: its own player has no way to go, and the way to every other player is that player's. So set them on the machine that joins; the one that runs `--host` needs none. The [stats](#stats) show the ping that makes.

With [several players](#several-players), each player who joins is that far from the match's server. Player 1, who opens it, isn't. `<player>:<value>` says one player's own, which wins over everyone's: `--latency 80 --latency 3:150` puts players 2 and 4 80 ms away and player 3 150 ms. A run like this still repeats exactly: the same datagrams are late and lost every time, so a script and its pictures show the same moment from each player's side, like the joining player's under 80 ms. Each way also has the frame that a packet takes there anyway.

On the web, `?latency=80`, `?jitter=10` and `?loss=5` in the page's address do the same, as does `tide run --web --latency 80`.

::: info Provisional
These options are new: their names, `<player>:<value>`, and that a machine running the server adds nothing may still change.
:::

### In a web page's address

A game's web page takes the same from its address, so a link can say which match it's for:

| In the address | What it does |
|---|---|
| `?host` | As `--host`: the game opens its match, in a room |
| `?join=K7QF2M` | As `--join K7QF2M`: the game joins the match in that room. A game's `Session.invite` is that link to its own page, for a player to send |
| `?latency=80`, `?jitter=10`, `?loss=5` | As `--latency 80`, `--jitter 10` and `--loss 5`: the way to a match the page joins is that much worse (see [A slow network](#a-slow-network)) |
| `?lag=100` | The page's player has a ping of 100 milliseconds, the round trip to the match's server, to see a match as on a slow connection with both players at one desk. What the trip takes already counts: the page holds back what it sends and what it receives by half of what's missing each. A page that hosts the match adds nothing |
| `?embedded` | The game is part of a page that scrolls, in a frame of it, and leaves that page its scrolling: the wheel, and a finger that swipes up or down, until the player taps the game |
| `?wait` | The game loads, and starts when the page around it says so |
| `?background-pipelines=off` | Each new shader is ready before the frame that first draws with it, rather than showing a frame or a few later while the browser compiles it in the background. That's for a test that steps frames and compares them; `Tide.backgroundPipelines = false` does the same |

A page that shows a game in a frame of the same site reads the frame's `Tide.room`, the code of the room the game hosts or joined (`""` for none), and can set its `Tide.lag` while the game runs. Its `Tide.minLag` is the least a lag can make the ping: the ping the game measured while its page added nothing, `undefined` until it has measured a few so. A page that wants it starts with no lag, as these docs do. That's how these docs put two players [side by side](../multiplayer/networking.md#prediction-and-rollback): the first frame's address says `?host`, and the second's `?join=` with the code the first one got.

Such a page starts a game that waits by calling the frame's `Tide.start()`. With `?wait` or without, it gives a promise that's kept once the game has drawn something, so the page can keep the frame out of sight until there's something in it. These docs load an example as you scroll near it, and start it when it's in view.

A page can have the browser download a game before any frame shows it: it loads the game's page once in a frame nobody sees, with `#download` after the address, and removes that frame once it has loaded. Nothing runs in it, and the frame that shows the game later finds its page in the browser's cache. A plain `fetch` of the page doesn't do that: browsers keep a frame's page apart from what a script fetches.

With `?embedded`, the wheel always scrolls the page around the game. On a touchscreen, a swipe that starts on the game scrolls the page too, until the player taps the game: the tap reaches the game, and from then on so does every touch, swipes included, until the player taps the page somewhere else.

## Without a window

Scripted runs use the game's normal saves unless you select a separate directory:

```sh
tide run --frames 300 --data-dir test-saves --capture play.png --stats
```

`--data-dir` overrides `Application.persistentDataPath` and relative file paths.
The directory and its parents are created when the game uses files. Reusing it
keeps those saves; remove it when the test is finished. With several `--players`,
each process uses `player-1`, `player-2`, and so on inside it. Temporary cache
paths retain their normal location. The flag also works in a desktop window and
on a built game's command line; it is unavailable on web, Android and dedicated
servers.

`tide run` can play a game with no window at all, to see what it draws or to check it still runs: from a script, on a build server, or for an agent that shouldn't open windows on your screen. The game runs once, as it was built, and `tide` ends with the game's exit code. Nothing is watched or reloaded.

```
tide run --frames 300 --capture menu.png --stats
```

| Option | What it does |
|---|---|
| `--capture <file.png>` | Writes the run's last frame as a PNG |
| `--record <file.gif>` | Writes the whole run as a GIF that loops: every second frame, so 30 a second, each shown as long as it was. `record` lines in a [script](#scripts) record part of a run |
| `--capture-audio <file.wav>` | Writes everything the run played as a WAV (16-bit stereo at 48 kHz). A game with no sound gives silence, a 60th of a second a frame |
| `--microphone <file>` | A WAV or Ogg Vorbis file the microphone plays in the run, looped, where a window's run would hear the capture device (`Clip.Microphone`). Without it a run's microphone is silent |
| `--frames <n>` | Ends the run after `n` frames |
| `--script <file>` | Input by frame from a file, which also says when to capture and when to quit (see [Scripts](#scripts)) |
| `--interactive <replay.txt>` | Keeps the game alive for [dynamic input and captures](#interactive-play), saving executed actions as a replay script |
| `--players <n>` | Plays the game `n` times at once, as the `n` players of one match (see [Several players](#several-players)) |
| `--scale <s>` | Draws as on a display scaled this much, like `1.5` or `2` (1 without it). Text and edges are fitted to the display's pixels, so they look different there, and [render targets](../game/render-targets.md) at the display's scale have that many more pixels |
| `--size <width> <height>` | The window's size in pixels, 960 by 540 without it |
| `--seed <n>` | The seed of the run's matches, 0 without it |

::: info Provisional
These options' names and the script's format are new, and may still change.
:::

The picture is what the window would show: the views, the GUI over them, and the stats and the profiler when they're on. It's `--size` times `--scale` pixels: `--size 1280 720 --scale 2` is 2560 by 1440. The [stats](#stats) are as a player would have them: one line unless the run asked with `--stats`, and a script's click on that line, or its `key f8`, opens them.

A recording is those pictures, every second one, as a GIF: 30 a second, since players show a GIF's frames no faster than 50 a second, and it lasts as long as the run would have in a window. A frame has 256 colors at most: one with more gets the 255 nearest its own, which smooth gradients show as bands. It's written as the run goes, so a long one needs no memory for it; a second of a game's usual flat colors is a few PNGs' worth.

Nothing of the run depends on the machine it's on, or on what's happening there:

- **Time is made up.** Every frame is a 60th of a second long, however long it took, and frames come as fast as the machine runs them: 300 frames take about a second.
- **Input is made up.** The game gets what the script says and nothing else: never the real keyboard, mouse or gamepad, and it leaves the clipboard alone. The cursor is locked whenever the game asks for it.
- **The window's size and scale are what you asked for,** whatever the display is.
- **The match's seed is 0.** In a window, [`Random.seed`](../language/math.md#the-match-s-seed) is another number every match. In a run like this it's 0 every time, so what the game rolls comes out the same. `--seed 7` picks another seed, to see the game roll otherwise: the same seed is the same run again.

So a run repeats exactly: the match goes the same way on every machine, and the same machine writes the same pictures, byte for byte. Between machines, pixels a GPU blends (the edges of shapes, a shader's gradients) can differ a little. The stats' list and the profiler are the exception, while they're open: they show what this machine's clock measured, which no two runs agree on. The stats' one line doesn't. What the game reads from disk, like its saves, is still this machine's. Made-up time doesn't wait for anyone, so a run like this isn't for a match with another machine, or with another run: neither would wait for the other. A match with [several players](#several-players) is one run, which plays them all.

Something has to end a run nobody can close: `--frames`, the script's `quit`, or an interactive controller's `quit`/closed input. The run's exit code says how it went:

| Exit code | What happened |
|---|---|
| 0 | The run ended at `--frames` or `quit`, and its pictures and recordings were written |
| 1 | A picture or a recording couldn't be written. Folders aren't made for it |
| 2 | An option or a line of the script is wrong, or the run would need a window [where nobody watches](#where-nobody-watches): nothing ran, and the message names what to fix |

A game `tide build` made takes the same options itself, so a build server needs no `tide`: `.build/mygame --frames 300 --capture menu.png`. These runs are for computers: on the web, drive the page with a headless browser instead, and take its screenshots.

### Several players

`--players <n>` plays the game `n` times at once, with no window, as the `n` players of one match, and takes a picture from each:

```
tide run --players 2 --script play.txt --size 640 360 --stats
```

Each player is a copy of the game, a program of its own, as on a machine of its own. The copies go a frame at a time, together, so frame 300 is the same moment for every player. Their network is made up, as the run's time and input are: what one sends in a frame, the others read in the next, in the same order every run. Nothing leaves the machine: no port opens, and no relay is asked. So a run of several repeats exactly, as a run of one does, and goes as fast as the machine plays it.

- **Who's in the match.** If the selected startup is a match scene or prefab, player 1 opens it, as with `--host`, and the others join it before the first frame: there's nothing to script. Without a startup selection, this uses `Main`. A game that starts in a local menu opens and joins from its own code, as it does for its players, at the script's clicks and keys.
- **Rooms.** A match that's open is in a room, and each player's room has a code of its own, six of one letter: `AAAAAA` is player 1's, `BBBBBB` player 2's, `CCCCCC` player 3's, and so on through the letters a code has (no `I` or `O`). [`Session.room`](../multiplayer/matches.md#where-this-machine-stands) reads it, and `Session.Join("AAAAAA")` joins player 1's match, so a script types it where a player would: `260 player 2 text AAAAAA`. A room nobody has open fails at once, with `Disconnected` and `Failed`, as a room that isn't one does.
- **Addresses.** `Session.Connect` reaches the first player whose match is open on the address's port, 7777 unless `Session.Open(port)` picked another. The rest of the address says nothing here, and no name is looked up. A port nobody opened never answers, and the game times out, as on a network.
- **One script for everyone.** A line says whose it is after its frame: `120 player 2 key d down`. Input that names no player is player 1's, so a script for one player plays as it did. `quit` and `--frames` end the run for everyone.
- **Leaving and coming back.** `300 player 2 leave`, `drop` and `rejoin` say whose match changes, which tests what a game does when a player goes and returns: `PlayerLeft`, seats that free up, a player who comes back as the same `PlayerID`. A host that leaves or drops ends its match for the others, as on a network.
- **Pictures from each.** A file gets its player's number before the extension: `capture fight.png` writes `fight-1.png` and `fight-2.png`, and `--capture`, `--record`, `record`, `--capture-audio` and `capture-audio` go the same way. `400 player 2 capture hers.png` takes player 2's alone, as `hers-2.png`.
- **A game that stops ends the run.** When one player's game ends before the run does (it stopped, or couldn't write a picture), the run says which player's and at which frame, the others end with it, and the exit code is that game's (1, when the system stopped it).

```
# Player 1 walks up while player 2 walks right, and each machine takes a picture of the match
120 key w down
120 player 2 key d down
300 capture walk.png
310 quit
```

A packet takes a frame each way, in every run, so a player's [ping](../multiplayer/matches.md#where-this-machine-stands) is a few frames long, and the same each time. `--latency`, `--jitter` and `--loss` put the players who join further away, or one of them (see [A slow network](#a-slow-network)). A run has 32 players at most, and no more than the game's `maxPlayers`. It takes no `--host`, `--join` or `--connect`: who's in the match is the run's to say, or the game's. A match whose host leaves ends for the others: it can't [change hands](../multiplayer/matches.md#host-migration), which goes through the relay.

`--players 1` is a run of one whose network is made up too: its match opens in room `AAAAAA` with nobody to join it, and its pictures have no number.

::: info Provisional
This is new, and its details may still change: the option's name, the rooms' codes, what an address reaches, and how a script and its files say whose they are.
:::

### Where nobody watches

Set the `TIDE_HIDDEN` environment variable where nobody is there to look at a window, or to close one: on a build server, or in the shell of an agent that works on your machine while you do. Any value but nothing or `0` counts. No Tide game shows a window there, however it's started:

- A run with no window goes as ever: `--frames`, `--capture`, `--script` and `--interactive`.
- Any other run is refused, with exit code 2, before anything is built or opened. Only closing its window would end it, and nobody could. The message says what to give the run instead.
- `tide run --web --stats` serves the page and opens no browser.
- Nobody listens there either: a program that opens its window hidden, as a test does, opens no audio device and no microphone.
- A game that stops says why on stderr and shows no dialog. On Windows, a debug build that stops would otherwise show one and wait for a click.

A game `tide build` made follows it too, on its own. Leave the variable out of your own terminal and your editor, where you want the window.

### Interactive play

An agent can inspect a picture, choose its next actions and continue the same
game session. Start a desktop hidden run with a replay file:

```sh
tide run --interactive play.txt --size 1280 720
```

Keep the process's standard input open and read its standard output. The game
draws frame 1, prints `tide: interactive ready 1`, then waits. Waiting advances
neither frames nor the run's made-up time. Send one command per line:

```text
capture menu.png
2 mouse 640 353
3 button left
step 30
capture arena.png
32 key d down
44 key d up
step 13
capture moved.png
quit
```

Read `menu.png` before choosing the click, and `arena.png` before choosing the
movement. The steps finish at frames 31 and 44. A frame number is absolute,
counted from 1, and must be later than the current frame. Queue lines use the
[script grammar](#scripts), including `player <n>`, keyboard, mouse, locked
movement, scroll, text, touch, gamepads, captures, GIF recording and
leave/drop/rejoin. Lines can arrive out of frame order; actions at the same
frame keep their arrival order. Held input stays held until released.

| Command | Result |
|---|---|
| `<frame> <event>` | Queues an action without advancing; at most 65,536 pending actions |
| `step <n>` | Advances every player together by exactly `n` frames, then waits; `n` is 1 to 1,000,000 and the resulting frame is at most 1,000,000,000 |
| `capture <file.png>` | Captures the current frame without advancing; `player 2 capture hers.png` selects one player |
| `status` | Reports the current frame without advancing |
| `clear` | Removes all pending actions; already held keys/buttons stay held |
| `quit`, or closing standard input | Ends the session at its current frame |

After a step, wait for `tide: interactive ready <frame>` before inspecting
results or deciding the next batch. Other accepted commands print
`tide: interactive ok <frame>` after every player has completed them. Invalid
commands print `tide: interactive error <reason>` and leave the session open
for correction. These lines are flushed immediately; ordinary capture output
and game logs can appear alongside them. Blank lines and comments beginning
with `#` are accepted. A command is at most 4,094 UTF-8 bytes, excluding its
newline; terminate each command with a newline.

Capture and recording paths are relative to the replay file's directory,
including when `tide run` was invoked elsewhere. Several players use the same
numbered capture names as scripted runs. Only the first process reads commands;
all players wait together, so an agent can take time to inspect each picture.
External peers and services keep their own clocks; use `--players` for a match
whose participants all wait together.

`play.txt` is written incrementally in executed frame order. A clean exit adds
its final `quit`; future actions discarded with `clear` or left unexecuted are
absent. Replay with `tide run --script play.txt`, using the same size, scale,
seed, player count and other runtime settings. Replay writes the captures and
recordings again. As with ordinary scripts, persistent saves or external
services can change the outcome; use an explicit `--data-dir` for test saves.

`--interactive` runs once without hot reload, on desktop only, and cannot be
combined with `--script`. Optional `--frames` caps the whole session and can
end a step early. `--capture` writes its final frame. Use `--capture-audio` to
retain the run's audio from its start; dynamically queued `capture-audio`
lines are refused because past audio may already have been discarded.
These command names and protocol details are provisional.

### Scripts

A script is a text file. Each line is the frame something happens at, then what happens. Frames count from 1, and lines are in the order of their frames. A blank line, or one that starts with `#`, says nothing.

```
# Into a practice round, then a few steps to the right
30 mouse 640 353
31 button left
120 key d down
200 capture fight.png
260 key d up
300 quit
```

```
tide run --script play.txt --size 1280 720 --stats
```

| Line | What happens |
|---|---|
| `key <name> down`, `key <name> up` | A key goes down or up, by its name in [`Devices`](../game/input.md#devices): `w`, `space`, `enter`, `leftShift`, `upArrow`, `digit1` |
| `key <name>` | A tap: down for this frame, and up at the next |
| `button <name> down`, `up`, or neither | A mouse button, the same way: `left`, `right`, `middle`, `back` or `forward`. `button left` is a click where the mouse is |
| `mouse <x> <y>` | The mouse goes to a place, in pixels from the window's top left, with `y` down, as the GUI's and `Draw.Screen`'s are |
| `move <dx> <dy>` | The mouse moves by so much, across and down: what turns a camera whose [cursor is locked](../game/cameras.md#looking-around) |
| `scroll <x> <y>` | The wheel turns, `y` positive away from you |
| `text <characters>` | Characters typed, to the line's end, as [`devices.keyboard.text`](../game/input.md#typed-text) and the GUI's fields get them: 32 a frame, as a paste comes. No key is pressed for them: Enter and Backspace are `key` lines |
| `touch <finger> down <x> <y>` | A finger touches the screen. `<finger>` is a number you pick, to say which one moves and lifts |
| `touch <finger> move <x> <y>` | It moves |
| `touch <finger> up` | It lifts, where it was, or at an `<x> <y>` after it |
| `pad <button> down`, `up`, or neither | A gamepad's button, by name: `buttonSouth`, `start`, `dpad.up` |
| `pad leftStick <x> <y>`, `pad rightStick <x> <y>` | A stick, from -1 to 1, with `y` up |
| `pad leftTrigger <value>`, `pad rightTrigger <value>` | A trigger, from 0 to 1 |
| `capture <file.png>` | Writes the frame, once it drew, as a PNG. A file a script names is beside the script |
| `capture-audio <file.wav>` | Writes what played since the last such line, or since the run began, as a WAV beside the script |
| `record <file.gif>` | Records from this frame on, every second frame, as a GIF that loops, beside the script. It goes on to `record stop`, another `record` or the run's end. `--record`'s recording is its own, and goes on beside it |
| `record stop` | Ends the recording, with this frame in it if it's one of its frames, and writes it out |
| `quit` | Ends the run once this frame drew |
| `player <n> leave` | In a run of [several players](#several-players), player `n` leaves its match, as `Session.Leave` does: the match hears `PlayerLeft` at once |
| `player <n> drop` | ...or goes silent both ways, as a phone that went away or a lost connection: nobody hears of it until a side times out, after 5 seconds |
| `player <n> rejoin` | ...and joins again what it last joined, as the same player: a match that let it go hears `PlayerJoined` with its `PlayerID`, and one that hadn't noticed it go hears nothing |
| `player <n>` before any of these but `quit` | In a run of [several players](#several-players), the line is player `n`'s: `120 player 2 key d down`. Without it, input is player 1's and a capture is every player's |

What a line does is in the devices at its frame, as if a player had done it since the frame before. So it goes the way a player's input goes: the GUI takes a click on one of its widgets and the game doesn't get it, and a modal keeps the keys from the game.

The mouse starts outside the window, on no widget. A gamepad is connected from the script's first `pad` line, and a touchscreen from its first `touch`.

A line that's wrong stops the run before it starts, and the message says which, and what to write:

```
tide: play.txt:7: no key is named 'shift': did you mean 'leftShift'?
```

## add and update

`tide add` and `tide update` change the `tide.packages` of the game in the current folder (see [Packages](../packages/using-and-writing.md)).

| Command | What it does |
|---|---|
| `tide add github.com/owner/repo` | The newest commit of the repository's default branch |
| `tide add github.com/owner/repo@dev` | The newest commit of the `dev` branch, which `tide update` follows |
| `tide add github.com/owner/repo@v1` | The newest `v1.x.y` tag, which `tide update` follows |
| `tide add github.com/owner/repo//physics` | The package in the repository's `physics` folder |
| `tide add ../shared` | A package in a folder on this machine |
| `tide add box2d` | A package that comes with `tide`, by its name |
| `tide update` | Every package from git, to the newest commit of what it follows |
| `tide update physics` | One, by its name, its repository's name or its source |

Both write only the lines they name: what the game's packages need, the game builds from their own lines (see [The packages a package needs](../packages/using-and-writing.md#the-packages-a-package-needs)). Each then checks that the game's packages resolve, and fails with the error if not.

## eject

`tide eject <package>` copies one of the game's packages, from git or one that comes with `tide`, into `packages/<its folder's name>` in the game's folder, and makes its line in `tide.packages` that folder. The package is the game's own from there: its files change with the game's, and `tide update` and `tide upgrade` no longer move it. It's named as for `tide update`.

```sh
tide eject rooms    # the rooms line becomes ./packages/rooms
```

A package that's a folder already is left where it is, and a folder already at `packages/<name>` is an error, with nothing copied.

`tide run` and `tide build` download the packages from git that aren't on this machine yet. `tide fetch` does only that, each at the commit `tide.packages` pins, what its packages need included, and those any `#if` leaves out of this build: a build after it needs no network, as on a build server that fetches in one step and compiles in another.

## upgrade

| Option | What it does |
|---|---|
| `--nightly` | Follow nightly versions from now on |
| `--stable` | Follow stable versions from now on |
| `--version <v>` | Install exactly this version |

`tide` says, at most once a day, when a newer version is out. Set the `TIDE_NO_UPDATE_CHECK` environment variable to stop it looking, as on a build server.

## telemetry

`tide` sends one small anonymous event for each command: which command, its target, the version, OS and CPU, whether CI runs it, how long it took and whether it worked. Never paths, names, code or messages. [Usage data](telemetry.md) lists every field. `tide telemetry off` turns it off, as do `TIDE_TELEMETRY=0` and `DO_NOT_TRACK=1`; `tide telemetry status` shows what's sent, where, and this installation's ID.

## Files

`tide` keeps its work in a hidden `.tide` folder in the game's folder. You can delete it any time, and git ignores it. Packages from git are kept for every game, once each commit: in `%LOCALAPPDATA%\Tide\packages` on Windows and `~/.tide/packages` elsewhere, or where `TIDE_PACKAGES` says. `tide build` puts what it makes in `.build/`, with the game's `.dll`, `.so` or `.dylib` libraries next to the program, and git ignores that too. Nothing in a hidden folder, one whose name starts with a dot like these two, is part of the game: not its `.tide` files, its C or its libraries.

A game's first build compiles the engine's C with it, and its packages'; the builds after that compile only what changed. `tide` compiles those files as many at once as the machine has processors, and what a build says and makes is the same however many that is. On a machine short of memory, set the `TIDE_JOBS` environment variable to how many to compile at once.
