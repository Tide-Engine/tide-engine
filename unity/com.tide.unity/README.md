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

3. Put the game's `.tide` files anywhere in `Assets`: every one is the game's,
   as every C# script is, but those in folders Unity leaves out (hidden ones,
   and those ending in `~`).
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

Each prefab holding TideEntity objects is a Tide prefab named after its file,
`UnityEngine.Prefabs.Enemy`: `Spawn(Enemy)` spawns `Enemy.prefab`'s entities.
Each such scene is a Tide scene named after its file,
`UnityEngine.Scenes.Level`: `Scene.Load(Level)` loads `Level.unity`'s
entities, and `with scene Level` runs a system for them alone.

```tide
using UnityEngine.Prefabs;
using UnityEngine.Scenes;
```

Prefab instances, nested prefabs and
variants work as in Unity: what an instance changes goes over its prefab's,
field by field. tide reads saved files, so save a prefab or scene to put it in
the game.

The match decides which scenes are loaded, and Unity follows: Build Settings'
first scene loads as Main starts, as a player build starts with it, and the
game's code loads and unloads the others (`Scene.Load(Level)`), which Unity
loads beside the rest and unloads as the match does. In the editor, Play has
the scenes you have open instead, and makes a `TideRunner` if they have none.
Put every scene the match loads in Build Settings, for players.

Packages work as in any Tide game: run `tide add rooms` (or a git URL) in any
folder of `Assets`, or type it in `Assets/tide.packages`' inspector and press
Add, and write `using Rooms;` in the game's files. It writes
`Assets/tide.packages`, which the build in the editor picks up. That inspector
lists the game's packages and where each comes from, with Update for a git
one and Remove.

A folder in `Assets` with a `tide.packages` saying `package Name` is a package
of the game's own, much as an assembly definition makes an assembly: its files
are in the namespace `Name`, it lists the packages it needs, and the game has
it with no line for it. C is only ever a package's: tide builds the `.c` files
of a package's folder, and none elsewhere in `Assets`, where Unity's plugins
are. A `.c` beside the game's `.tide` files gets a warning saying so.

An error or warning tide gives for a line of a `.tide` file opens that file at
that line on a double click in the Console, as a C# error does, and goes once a
build has fixed it. `.tide` files and `tide.packages` open in the external code
editor (Preferences > External Tools): the package lists their extensions in
Editor Settings' Additional extensions to include, which Rider and Visual
Studio open files by.

Select a `.tide` file to see, over its text, the last build's errors and
warnings in it and what it declares: each component's and singleton's fields
with their defaults, what each system reads and writes, what it waits for and
why, and each event's handlers. In Play, each component shows how many shown
entities have it, and each singleton its values. A line number opens the file
there.

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

C# doesn't spawn: the match does, in Tide code, which a command from C# can
ask for (`Spawn(Bullet, ...)` in its handler, `Bullet` being the Unity prefab,
under `UnityEngine.Prefabs`). Unity sees the newest predicted state once a
frame, so a GameObject lasts as long as its entity there: a rollback that
spawns the entity again keeps it, and one that gives its handle to another
entity (a spawn the server didn't make) gets the other entity a GameObject of
its own.

With the runner's "Pool" on, the GameObject of an entity that goes is kept,
inactive, for the next entity of its prefab: reset what Unity alone keeps on it,
an Animator's state or particles, in `OnDisable` or `OnEnable`. For a pool of
your own, set `TideRunner.Spawner` to an `ITideSpawner`, whose `Make(prefab)`
and `Release(prefab, made)` replace Instantiate and Destroy.

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
