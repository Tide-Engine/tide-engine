# Tide for Unity

Runs a Tide game's simulation and networking inside Unity. Tide simulates,
predicts and rolls back, every machine bit for bit the same; Unity draws, plays
audio and is where you author.

Early: the owner is testing it. What's built and what isn't is in the Tide
repository's `notes/unity.md`.

## Install

1. Install tide (tide-engine.dev). The package finds it on `PATH` or where the
   installer puts it; Project Settings > Tide can name another.
2. In Unity, Window > Package Manager > + > Add package from git URL:

   ```
   https://github.com/Tide-Engine/tide-engine.git?path=/unity/com.tide.unity#dev
   ```

3. Put the game's `.tide` files (and its C, if any) in `Assets/Tide`, or the
   folder Project Settings > Tide names. Keep other plugins' C sources out of
   that folder: tide compiles every `.c` file in it.
4. Set Edit > Project Settings > Editor > Asset Serialization to Force Text
   (Unity's default): tide reads prefabs and scenes from their files.

While the editor is open, `tide unity` builds the game into `Library/Tide`
whenever its files, or a prefab or scene holding Tide entities, change; its
lines are in the Console, and clicking one selects what it's about. It also
keeps `Packages/com.tide.game`: the game's schema, from which the package
makes C# types of every Tide type (`Tide.Game.Health`). Keep that folder in
source control, so a fresh clone compiles before tide has built anything.

## Author

Add `TideEntity` (Add Component > Tide > Tide Entity) to a GameObject of a
prefab or a scene, and its Tide components with Add Tide Component: each field
is drawn by its Tide type, and one you don't set keeps its default. An entity
field names another TideEntity's GameObject. The GameObject's Transform is the
entity's, and its parent the entity's parent (through GameObjects that aren't
entities).

Each prefab or scene holding TideEntity objects is a Tide prefab named after
its file: `Spawn(Enemy)` spawns `Enemy.prefab`'s entities, and
`Scene.Load(Level)` loads `Level.unity`'s. Prefab instances, nested prefabs and
variants work as in Unity: what an instance changes goes over its prefab's,
field by field. tide reads saved files, so save a prefab or scene to put it in
the game.

## Run

Add a GameObject with a `TideRunner`. In Play mode it runs the game: Main's
match on this machine, or what its arguments say (`--host`, `--join <code>`,
`--connect <address>`). A new build of the game is swapped into the running
match.

An entity of a Unity prefab shows through an instance of that prefab, made
when the entity comes; one of the open scene's shows through the scene's own
GameObject. Any other entity is shown by the first view rule that matches it: a
Tide prefab's name, or a component it has, and the Unity prefab to show it
with. An entity with Tide's `Transform` is placed by it. `TideEntityView` on
that GameObject reads the entity's components as Tide's views see them this
frame: `view.Get<Tide.Game.Health>()`. `TideRunner.Current.Send(command)`
sends a command, as local code's `Send`.

What the game's Tide views draw with `Draw` (2D: shapes, lines, meshes and
text) is drawn over Unity's frame, in any render pipeline, while the runner's
"Draw Tide" is on. 3D meshes, textures and the game's own shaders aren't yet.

## Build

A player build (File > Build Profiles) builds the game with it, optimized, for
the editor's own desktop platform or WebGL. The player runs the match as the
editor does. A WebGL player runs Tide's own WebAssembly beside Unity's, with
its threads: serve it cross-origin isolated (`Cross-Origin-Opener-Policy:
same-origin` and `Cross-Origin-Embedder-Policy: require-corp`), as any Tide
page.

## Requirements

Unity 6000.0 or newer, and the tide version this package was made with.
