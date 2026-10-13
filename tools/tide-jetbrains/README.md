# Tide plugin for JetBrains IDEs

On JetBrains Marketplace as `io.github.blenminer.tide`.
Highlighting from the TextMate grammar in `tools/tide-syntax`, and
everything else from `tidels` through the LSP4IJ plugin, which JetBrains
Marketplace installs with it. It runs in every JetBrains IDE from 2024.2 on.

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

```sh
./gradlew buildPlugin                  # build/distributions/tide-<version>.zip
./gradlew test                         # adapter and isolated IntelliJ platform regressions
./gradlew runIde --args="<game folder>"  # an IDE with the plugin, for trying it
./gradlew verifyPlugin                 # against IntelliJ 2024.2 and the newest CLion
./gradlew verifyPlugin -PverifyIde="C:/Program Files/JetBrains/CLion 2026.2.3"
```

The ordinary tests check settings, console hyperlinks, run configurations,
context-menu targets and game-folder selection in a headless test application.
To exercise the real LSP4IJ client, a three-level child-process tree and JCEF,
pass this checkout's built server, Node 24.16 or newer on Windows, and opt in
to the muted embedded-browser test:

```sh
./gradlew --no-configuration-cache test -PtideTestServer="<built tidels>" -PtideTestNode="<Node executable>" -PtideTestJcef=true
```

These checks use the Gradle test sandbox and its JBR; they do not install a
plugin in, restart, or change settings in an existing IDE. The JCEF check uses
offscreen rendering, executes page JavaScript and reloads the same browser.

The manually requested Build workflow builds and verifies it, and publishes it to Marketplace with
each stable release (see `.github/workflows/build.yml`).
