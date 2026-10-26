# Tide for Unity

Runs a Tide game's simulation and networking inside Unity. Tide simulates,
predicts and rolls back, every machine bit for bit the same; Unity draws, plays
audio and is where you author.

Early: the owner is testing it. What's built and what isn't is in the Tide
repository's `notes/unity.md`.

## Install

1. In Unity, Window > Package Manager > + > Add package from git URL. A
   release tag keeps everyone on the project, and its build server, on one
   version:

   ```
   https://github.com/Tide-Engine/tide-engine.git?path=/unity/com.tide.unity#v0.5.0-nightly.12
   ```

   or `#dev` to follow the newest.
2. The package builds with the `tide` of its own commit, which Unity locks in
   `Packages/packages-lock.json`: a release tag is that version, any other
   commit the newest release at it or before it. The first time, it asks to
   install that version, beside any other `tide`, in
   `%LOCALAPPDATA%\Tide\versions` or `~/.tide/versions` (or the folder
   `TIDE_VERSIONS` names). A build server (Unity in batch mode, as GameCI runs
   it) installs it with no question and nothing installed beforehand: keep
   `TIDE_VERSIONS` in its cache to download it once. Project Settings > Tide
   can name your own `tide` instead, kept for you alone, as for a build of tide
   you're working on.

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
While the game's code doesn't build, its components still come from what it
declares, so a component a new prefab needs is there before the code that
spawns that prefab builds. A build in progress shows as a Tide task in the
editor's status bar, not in the Console.

Some of Tide's types share a name with Unity's: Tide's `Transform`, `Time` and
`Font`, or a package's, like Box3D's `Collider`. Neither is renamed or hidden.
A script with `using Tide.Game;` beside `using UnityEngine;` writes the
namespace of the one it means, `UnityEngine.Transform` or
`Tide.Game.Transform`, as C# asks when a name is ambiguous, so everyone reading
it knows which it is. Tide code does the same for its own names.

Tide is where the game's types are declared; their C# follows them:

- **Renames reach your scripts.** Renaming a component, a field, a singleton, an
  event, a command, a struct, an enum, an enum's member or a namespace in the code editor
  (tidels's rename, F2 in VS Code or Shift+F6 in Rider) also renames it in the
  project's C#: the scripts in `Assets` and in its embedded and local packages,
  never `Packages/com.tide.game`, which tide writes again. It's one edit you can
  preview and undo, with the prefabs' and scenes' changes. References are found
  carefully, with no C# compiler: a member where what it's a member of is known
  to be the game's type (a field, variable, parameter or lambda parameter of it,
  `GetComponent<Health>()`, `new` and its initializer, `foreach`, a cast, `as`,
  `is Health h`, or a field, property or method of your own classes that gives
  it, partial ones across files too), a type or a namespace where the file's
  usings make the name the game's, and a doc comment's `<see cref="..."/>`.
  Another type's member of the same name, other text in strings and comments,
  and a name a type of your own also has are left alone. A namespace's rename
  also gives its components' prefabs and scenes their new script GUIDs.
- **From a type back to its declaration.** Each generated type carries its Tide
  declaration's doc, the comment lines just above it (a field's, above it or
  after it on its line), and the file it's in, so a hover in Rider or Visual
  Studio shows them. Opening a component's script (a double click on its Script
  field, Edit Script, or the script in the Project window) opens its `.tide`
  file at the declaration in the external code editor.
- **Errors that explain a rename.** When a script no longer compiles because
  Tide renamed or removed a name it uses, the Console says so next to Unity's
  error: ``tide: `Health.hp` is now `Health.health` (Assets/Tide/game.tide:12)``,
  which opens the new declaration, or that it was removed, from the first
  compile as the editor opens on. tide keeps what
  became of the C#'s names in `Library/Tide/renamed.json`, on this machine
  alone.

## Author

Add the game's components with Add Component > Tide; Unity also adds the
`TideEntity` that makes the GameObject an entity. Each field starts at its
declared Tide default and is authored in its component's inspector. An entity
field names another TideEntity's GameObject. The GameObject's Transform is the
entity's, and its parent the entity's parent (through GameObjects that aren't
entities).

Each prefab holding TideEntity objects is a Tide prefab named by its folders
and its file, `UnityEngine.Prefabs.Enemies.Goblin` for
`Assets/Enemies/Goblin.prefab`: `Spawn(UnityEngine.Prefabs.Enemies.Goblin)`
spawns its entities. Each such scene is a Tide scene named the same way,
`UnityEngine.Scenes.Levels.Cave`: `Scene.Load` of it loads `Cave.unity`'s
entities, and `with scene` it runs a system for them alone. While only one
prefab or scene has a file's name, that name alone is it too, so
`Spawn(Goblin)` and `Scene.Load(Cave)` work after these; when a second one has
it, code using the short name gets an error naming both, as C# does for an
ambiguous name.

```tide
using UnityEngine.Prefabs;
using UnityEngine.Scenes;
```

Prefab instances, nested prefabs and
variants work as in Unity: what an instance changes goes over its prefab's,
field by field. tide reads saved files, so save a prefab to put it in the game.
A scene you haven't saved is Play's as you have it: Play writes a copy of each
open scene with unsaved changes, which tide reads in its place, and starts
once tide has built them. A new scene you've never saved is the game's too, as
the scene `UnityEngine.Scenes.Untitled`, until you save it under a name. With
Prefab Mode's Auto Save off, Play has the prefab as last saved, as Unity's own
instances of it do. A GameObject's name can be anything, `#` and `/`
included.

The match decides which scenes are loaded, and Unity follows: Build Settings'
first scene loads as Main starts, as a player build starts with it, and the
game's code loads and unloads the others (`Scene.Load(Level)`), which Unity
loads beside the rest and unloads as the match does. In the editor, Play has
the scenes you have open instead, and makes a `TideRunner` if they have none.
The match's scenes are Tide's to load: a scene you load by hand during Play, in
the Hierarchy or from a script, is local, apart from the match, and no entity
shows through its GameObjects. A scene of the match's that something else
unloads is loaded again. A scene the match loads twice is two of Unity's, each
with its own GameObjects. Put every scene the match loads in
Build Settings, for players.

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
`--connect <address>`, `--seed <n>`, `--data-dir <folder>`, and `--stats` for
Tide's stats drawer over the game: the frame's work, ping, network, entities
and the slowest systems; the game's `stats` setting shows it too). An argument
it doesn't take gets a warning in the Console. A new build of the game is swapped into the running
match.

An entity of a Unity prefab shows through an instance of that prefab, made
when the entity comes; one of the open scene's shows through the scene's own
GameObject. Any other entity is shown by the first view rule that matches it: a
Tide prefab's name, or a component it has, and the Unity prefab to show it
with. An entity with Tide's `Transform` is placed by it. `TideEntityView` on
that GameObject reads the entity's components as Tide's views see them this
frame: `view.Get<Tide.Game.Health>()`. `TideRunner.Current.Send(command)`
sends a command, as local code's `Send`.

Tide's devices are what Unity's keyboard, mouse, gamepad and touches say. For
input of the game's own on top (an on-screen stick, a bot, a replay, a test),
`TideRunner.EditInput` is called each frame after Unity's devices are read,
with what Tide's devices are about to say:
`runner.EditInput += (ref TideEmbedInput input) => input.Hold("space");`.

The game's views draw with Unity's camera: the runner's View Camera, or the
main camera, is the 3D camera Tide's views start with, so
`Draw.WorldToScreen(position)` is where Unity shows that point, in screen
pixels from the top left. Draw at it inside `Draw.Screen() { ... }`, whose
calls are in screen pixels; outside it, 2D draws go through Tide's 2D camera,
centered on the screen at a pixel a unit unless the view sets one.

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

What the game's Tide views draw with `Draw` is drawn in Unity's frame while
the runner's "Draw Tide" is on: 2D, text, 3D with the camera's depth, render
targets and the game's own shaders, in Unity's built-in render pipeline and in
URP.

A physics package's components are on GameObjects like the game's own: with
`box3d`, a `Body` and a `Box`, `Sphere` or `Capsule` (Add Component, Tide/Box3D).
A shape's size is by the GameObject's scale, as a collider's is, a selected one
is drawn in the Scene view, and Edit Shape gives it handles. The body starts
where the GameObject is, and the GameObject follows it. A shape on a child
GameObject with a `Part` is on the body above it, where the child puts it. A
mesh collider fills from the GameObject's MeshFilter, a height field from its
Terrain, and a joint's anchors and a 2D polygon's or chain's points have
handles too. Fields and components the package keeps for itself are hidden.
Unity's own `Rigidbody` and colliders do nothing in the match. See the
[Box3D](https://tide-engine.dev/packages/box3d) and
[Box2D](https://tide-engine.dev/packages/box2d) packages' pages.

The game's sound (`Audio.Play`, `Sound` fields, its `audio` declarations)
plays through Unity's audio: an AudioSource under the runner, "Tide Audio", so
Unity's volume and mute apply, through the runner's Audio Output mixer group.
`Clip.Microphone` is Unity's microphone, which a phone asks the player for the
first time the game listens. In a WebGL player Tide's program plays through the
page itself, and hears the page's microphone.

## Debug

The game's Tide code can be debugged in the editor as Tide: each library tide
builds for the editor has debug information that maps its C back to the
`.tide` lines, with how its values look beside it (`Library/Tide/game.natvis`,
also inside its `.pdb` on Windows, and `game_lldb.py`). Attach a native
debugger to the Unity editor, not Unity's C# debugger:

- VS Code: the **Attach to Unity (Tide)** debug configuration of Tide's
  extension.
- CLion: **Run > Attach to Unity (Tide)**, with Tide's plugin.
- Visual Studio: **Debug > Attach to Process**, native code, `Unity.exe`.

Breakpoints in `.tide` files stop there, stepping goes Tide line by Tide line,
the call stack is the game's systems and functions, and lists, maps, text,
enums and entities show as Tide's. A new build swapped into the running match
keeps the breakpoints. More:
[Debugging](https://tide-engine.dev/tools/debugging).

## Time travel

A match is deterministic, so the editor can keep it: Window > Tide > Time
Travel, in Play. The runner records a match this machine runs from its start
(what went into each tick: players' input, joins and leaves, commands) and
keeps the worlds of its latest ticks (the runner's Keep Seconds, 10 by
default).

- **Pause** stops the match at its latest tick. **Step** runs one tick, with
  what's held and pressed then (a key pressed while paused goes into that tick,
  as do commands sent while paused). **Resume** goes on from there.
- The **Tick** slider shows any tick since the match started: its entities and
  their GameObjects, the Tide components' live values in the inspector,
  `TideEntityView`, singletons and Tide's drawing are that tick's. Ticks
  before those kept are made again from the start as they're shown. What's
  clicked or sent on an older tick's picture never reaches the match, and the
  match's own local state is left as it was.
- **Continue From Here** takes the match back to the tick shown and on from
  there: the ticks after it are gone. Which commands are in the match stays as
  it was, so one already applied after that tick doesn't come again.
- With players of other machines in the match, it never stops for them: Pause
  shows the tick paused at while the match goes on behind it (this machine's
  player as if it let go of everything), and Resume shows its latest tick
  again. Continue From Here takes them back too: they're sent the match's world
  again, as when a client goes wrong.
- **Save Recording** writes the match's recording (a `.tiderec`) and **Open
  Recording** replays one, a tester's from a bug report say, on any machine with
  a build of the same data layout (the game's components, input and commands as
  they were): each tick is made again with this build and its world checked
  against the recording's hash. The window says how far every tick matched, or
  the first tick whose world went otherwise, which after a change to the game's
  code is where that change first matters. **Replay This Match** replays the
  current one.

In C#: `runner.Pause()`, `Step()`, `Resume()`, `ShowTick(n)`,
`ContinueFromHere()`, `Recording()` and `SaveRecording(path)`,
`Replay(bytes)` and `ReplayFile(path)`, `EndReplay()`, with `runner.Travel`
saying where it is. The runner's Time Travel setting keeps matches in the
editor (the default), nowhere, or in player builds too, so a game can save a
tester's recording for a bug report. A hot reload while paused comes in as
any: the match stays paused, its kept ticks with it if the data layout is the
same (another starts the recording over), and the next step runs the new code.
Only a match this machine runs is kept; one joined on another machine, and a
sharded one, aren't.

## Build

A player build (File > Build Profiles) builds the game with it, optimized, for
the editor's own desktop platform or WebGL. The player runs the match as the
editor does. A WebGL player runs Tide's own WebAssembly beside Unity's, with
its threads: serve it cross-origin isolated (`Cross-Origin-Opener-Policy:
same-origin` and `Cross-Origin-Embedder-Policy: require-corp`), as any Tide
page.

## Requirements

Unity 6000.0 or newer, and the tide version this package was made with.
