# Tide plugin for JetBrains IDEs

Public source: [Tide-Engine/tide-jetbrains](https://github.com/Tide-Engine/tide-jetbrains).
The plugin ID is `io.github.blenminer.tide`. Its initial Stable upload has been
submitted to JetBrains Marketplace. You can also build the ZIP with the
instructions below and use Settings > Plugins > Install Plugin from Disk.

Tide's engine installer includes the matching plugin and adds it to existing
JetBrains profiles (2024.2+), including LSP4IJ when missing. `tide upgrade`
updates those profiles with its bundled version. For a newly installed IDE,
open it once, run `tide editors`, then restart it. Custom IDE configuration or
plugin paths need manual installation. Marketplace updates follow the IDE's
chosen channel independently.

Highlighting comes from the bundled TextMate grammar, and
everything else from `tidels` through the required LSP4IJ plugin, available
from JetBrains Marketplace. It runs in every JetBrains IDE from 2024.2 on.

The plugin runs, in this order: the `tidels` chosen under Settings >
Languages & Frameworks > **Tide**; the project's own `build/tools/tidels` (in
Tide's repo, the server your build made), once you trust the project;
`tidels` on `PATH`; or the one where tide's installers put it. When there's
none, the server stays off and `.tide` files say why, above the code, with
links to install tide, choose `tidels`, or try again, which starts it.

## Running the game

Tide run configurations run `tide run` on a game's folder, in the Run tool
window: natively; on the web, whose page shows in the **Tide Game**
tool window (JCEF; the system's browser in an IDE without it); or on
Android, on the phone or emulator that's connected. A `.tide` file's
context menu offers all three, for the game it's in: the project's folder,
the folder of the game it's in when that has a `tide.packages` of its own
(for a package's file, the project's game when it lists the package), or in
Tide's repo, the game `build/tools/games.txt` lists it in. For a file
`tide run` can't play (one of a game CMake builds from a list of files, or
of a package no game here lists), running says why. Errors and warnings in the Run console link to their
place in the file.

An existing `.tprefab` offers **Open Tide Prefab Editor** in the project/editor
context menu. It requires project trust, saves that prefab and launches installed
Tide's native app in the file's game context. The app is a separate window; save
script changes in the IDE to refresh its compiled preview.

**Edit Tide Prefab in IDE** hosts the same hidden native app in the **Tide Prefab**
tool window. Its Swing canvas scales the fixed 1440×900 native image; no JCEF or
browser server is needed. Mesh picking, Move/Rotate/Scale, validation, save/undo
and recovery stay in the native backend. Closing a content tab/project closes
its process. The standalone app remains available for a resizable window.

## Debugging

In CLion (and IDEs with CLion's native debugger), breakpoints go in `.tide`
files: a game's C maps each line back to its `.tide` line, so the debugger
stops there, steps from one Tide line to the next and shows the stack as the
game's systems and functions. Values show as Tide's from the game's natvis
(Windows) or LLDB views.

- **Run > Debug Tide Game**, also in a `.tide` file's context menu, plays the
  game as its Tide run configuration does (when it isn't running already) and
  attaches the debugger. Saving a file reloads it, still under the debugger.
- **Run > Attach to Unity (Tide)** attaches to the Unity editor running the
  project; with several open, it asks which.

Both attach through the platform's attach API, as **Run > Attach to Process**
does, preferring a native debugger. On Windows, put the **Visual Studio**
toolchain first: its debugger reads the game's `.pdb` and the natvis in it.
The breakpoint support (`TideBreakpoints`, in `tide-cidr.xml`) loads only in an
IDE with that debugger; it compiles against a stub of its extension point
(`src/cidrStubs`), which isn't packaged, and `verifyPlugin` checks it against
CLion. More: [Debugging](https://tide-engine.dev/tools/debugging).

## Colors

JetBrains' dark schemes draw type names like plain text, as in Java, so
Tide's types stay uncolored until you pick colors. The server tells each
kind of type apart, so each can have its own color under Settings > Editor >
Color Scheme > **Language Server**:

- **Struct type**: components
- **Classes > Class reference** and **Class declaration**: singletons
- **Interface**: inputs
- **Type**: built-in types such as `Color`, `Entity` and `Devices`

Built-in value types (`float3`, `int`, `bool`), `Sample` and `Sanitize` use the
keyword color, and attributes (`[After]`, `[Clamp]`) the **Decorator** color.
After a server update, restart it (Language Servers tool window) to see changes.

## Building

It needs a JDK 21 or later to build; Gradle downloads the JDK 21 it compiles
with, and the IntelliJ Platform it compiles against, the first time.
Clone this repository and run the commands from its root (`gradlew.bat` on
Windows). No engine source checkout is required for the ordinary build/tests.
Install [Tide](https://tide-engine.dev/guide/install) separately for `tidels`,
running games and the native prefab editor.

```sh
./gradlew buildPlugin                  # build/distributions/tide-<version>.zip
./gradlew buildCliInstaller            # build/cli/tide-jetbrains-installer.zip (Tide + LSP4IJ)
./gradlew test                         # adapter and isolated IntelliJ platform regressions
./gradlew runIde --args="<game folder>"  # an IDE with the plugin, for trying it
./gradlew verifyPlugin                 # against IntelliJ 2024.2 and the newest CLion
./gradlew verifyPlugin -PverifyIde="C:/Program Files/JetBrains/CLion 2026.2.3"
```

The ordinary tests check settings, console hyperlinks, run configurations,
context-menu targets and game-folder selection in a headless test application.
Plugin verification also rejects internal and override-only API calls, including
tool-window registration, on the minimum IntelliJ platform and newest CLion.
To exercise the real LSP4IJ client, a three-level child-process tree and JCEF,
pass a locally built Tide server, Node 24.16 or newer on Windows, and opt in
to the muted embedded-browser test:

```sh
./gradlew --no-configuration-cache test -PtideTestServer="<built tidels>" -PtideTestNode="<Node executable>" -PtideTestJcef=true
```

These checks use the Gradle test sandbox and its JBR; they do not install a
plugin in, restart, or change settings in an existing IDE. The JCEF check uses
offscreen rendering, executes page JavaScript and reloads the same browser.

Add `-PtideTestEditor="<built tide-editor>"` to check actual native PNG rendering
in the Swing prefab canvas and graceful backend shutdown. That check needs the
native headless OpenGL context; it uses private temporary project files.

## License and maintenance

The Tide plugin source, icons, tests and bundled TextMate grammar are licensed
under [MIT](LICENSE). Select **MIT** for the Marketplace listing and use
https://github.com/Tide-Engine/tide-jetbrains/blob/main/LICENSE as its license URL.
The Gradle wrapper retains its Apache-2.0 license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
The MIT license is also included in the built plugin JAR.

The engine repository keeps the canonical source under `tools/tide-jetbrains`.
Issues and patches are welcome here; accepted changes are applied there before
the next public export. Engine builds use the live `tools/tide-syntax` grammar;
standalone clones use the `textmate` snapshot. Maintainers refresh that snapshot
and the bundled host-process test fixture before exporting.

The engine's manually requested Build workflow builds and verifies the plugin,
then publishes it to the same Tide Marketplace listing after publishing the
engine release: `release` uses Stable (the `default` channel), and `dev` uses
`nightly`. Both use the same plugin ID; neither publishes to the former PurrLang
listing. This public source repository does not start a second release workflow.

To opt into nightly plugin updates, add
`https://plugins.jetbrains.com/plugins/nightly/list` under Settings > Plugins >
Manage Plugin Repositories. Stable users do not need that repository. The plugin
still runs your separately installed `tidels`; choosing its nightly channel does
not switch the engine's update channel. See [JetBrains' channel documentation](https://plugins.jetbrains.com/docs/intellij/publishing-plugin.html#specifying-a-release-channel).
