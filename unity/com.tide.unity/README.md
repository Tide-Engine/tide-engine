# Tide for Unity

Runs a Tide game's simulation and networking inside Unity. Tide simulates,
predicts and rolls back, every machine bit for bit the same; Unity draws, plays
audio and is where you author.

Early: the owner is testing it. What's built and what isn't is in the Tide
repository's `notes/unity.md`.

Tide `[Linear]` textures work in either **Project Settings > Player > Other Settings > Color Space**. In Linear they
use Unity's sRGB image formats. Unity's Gamma mode has none, so there a marked image keeps its bytes as stored and is
read through a copy decoded to linear light in half floats, which costs twice an RGBA8 image's memory (more for
compressed images). Unmarked images keep their existing behavior in either color space. Tide never changes this
project setting.

## Install

1. In Unity, Window > Package Manager > + > Add package from git URL. A
   release tag keeps everyone on the project, and its build server, on one
   version:

   ```
   https://github.com/Tide-Engine/tide-engine.git?path=/unity/com.tide.unity#v0.5.0-nightly.12
   ```

   or `#dev` for engine development (it may be ahead of published compilers).
2. The package builds with the `tide` of its own commit, which Unity locks in
   `Packages/packages-lock.json`: a release tag is that version, any other
   commit requires a release built at exactly that commit. The first time, it asks to
   install that version, beside any other `tide`, in
   `%LOCALAPPDATA%\Tide\versions` or `~/.tide/versions` (or the folder
   `TIDE_VERSIONS` names). Play pressed while it installs, or while `tide`
   makes its first build, waits with a progress bar (Cancel gives Play up) and
   starts once the game is built. A build server (Unity in batch mode, as GameCI runs
   it) installs it with no question and nothing installed beforehand: keep
   `TIDE_VERSIONS` in its cache to download it once.

   Open **Window > Tide > Versions** (also **Manage Tide versions** in Project
   Settings > Tide) to see the project's package, compiler and available stable
   and nightly releases. **Use version** installs that release's compiler and
   switches the Unity package to its matching Git tag. Unity reloads scripts and
   resumes automatic builds when enabled. Commit the changed package manifest
   and lock file so teammates use the same version. A failed download leaves
   the package pin unchanged.

   **Update / switch to dev** refreshes the development package; it does not
   substitute an older compiler if that commit has no published release.
   **Check for updates** retries release resolution after publication. An
   unpublished commit reports the gap and directs you to this window to choose
   a release or use a matching development compiler. Local folder packages stay
   under your control; this window switches Git packages only.

   To test a local Tide compiler/runtime, launch Unity with
   `-tide-command "<local Tide>/bin/tide"` (`tide.exe` on Windows). This overrides
   the package pin for that editor process only, including its script reloads.
   Project Settings > Tide shows the active command and the override warning;
   it stores no compiler path. Restart Unity without the argument to follow the
   pin again. Old saved compiler overrides are ignored. A missing executable or
   malformed argument reports an error instead of substituting another compiler.
   Tide checks the existing host's ABI before entering Play; a development build
   must match the package, or Play explains how to return to the pin.

   Updated VS Code and JetBrains Tide plugins use the same selected installation
   for their language server and runs. Unity writes its selection
   to `Library/Tide/toolchain.json`, tied to the package lock; keep it out of
   source control. A development selection identifies the running editor process:
   closing it (or a crash) returns the IDE to the pin. Changing the package lock
   makes the IDE select again.

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
with. An entity with Tide's `Transform` is placed by it. `TideEntity` on
that GameObject reads the entity's components as Tide's views see them this
frame: `entity.Get<Tide.Game.Health>()`. The same `TideEntity` holds its authored
components and live binding; `Showing` says whether it is currently bound. Scripts
can find it and subscribe to its events before the match starts. `TideRunner.Current.Send(command)`
sends a command, as local code's `Send`.

Tide's devices are what Unity's keyboard, mouse, gamepad and touches say. For
input of the game's own on top (an on-screen stick, a bot, a replay, a test),
`TideRunner.EditInput` is called each frame after Unity's devices are read,
with what Tide's devices are about to say:
`runner.EditInput += (ref TideEmbedInput input) => input.Hold("space");`.

What the game's Tide code says with `Debug.Log` is in Unity's Console, as a
log with no C# stack, from whichever system, view or handler said it; a tick
this machine runs again after a correction logs again, as anywhere, so prefix a
log of match state with `Time.tick`. What goes wrong on the host's side (a
build of the game that doesn't load, an argument it leaves out) is there as an
error. In a WebGL player, both are in the browser's console.

A match waits for nobody, so the runner keeps the game running while its
window isn't the one in front (its "Run In Background", on by default, sets
`Application.runInBackground`): a match this machine runs would otherwise stop
for every player in it whenever its player switched windows.

What the runner does each frame is made to cost little and to make nothing
new for the garbage collector once a match is as it'll stay, however many
entities it shows. A GameObject whose entity stays put has its Transform left
alone. A component's or a singleton's list, set, map or grid is the one it
had the frame before, filled again, and text that hasn't changed is the same
string: what a script reads there is this frame's, and a list it keeps
changes with the match, so copy what should outlast the frame. An event a
handler hears is an object of its own, the handler's to keep, and is made only
when something hears its kind. In Unity's Profiler the runner's frame is
`Tide.Frame` (the game's own frame: its ticks and views), `Tide.Render` (its
drawing read for Unity's cameras) and `Tide.Show` (the entities shown through
their GameObjects, their components and the singletons read, events told).

The game's frame hands Unity what Unity reads of the match, and nothing else:

- **A component** is read once one of it is enabled on a GameObject: a
  prefab's or a scene's, or one a script added. Every shown entity that has
  it then has its component, with its values. One the game gives entities as
  it runs, which no GameObject has, isn't read until a script asks:
  `runner.Read<Tide.Game.Stunned>()` once, or an entity's `Has`, `TryGet` or
  `Get` of its value.
- **A singleton** is read once `runner.Get<T>()` asks for it, and **an event**
  while something hears it.
- What's asked for the first time is there in the same frame.
- Disabling a Tide component doesn't take it from its entity: it says that
  GameObject doesn't read it. While the only ones of a component there are
  disabled ones, it costs the frame nothing, and they stay as they were.
- The runner's **Read Everything** reads every component of every shown
  entity, every singleton and every event each frame, as the package did before
  it read by need.

The game's views draw with Unity's camera: the runner's View Camera, or the
main camera, is the 3D camera Tide's views start with, so
`Draw.WorldToScreen(position)` is where Unity shows that point, in screen
pixels from the top left. The camera can be perspective or orthographic: Tide
takes its placement and projection as they are, its near and far planes too,
and `Draw.CameraTransform()` and `Draw.CameraProjection()` give them. Draw at it inside `Draw.Screen() { ... }`, whose
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
targets, the game's own shaders and its compute kernels (`GPU.Run`, GPU lists
and `await list.Read()`), in Unity's built-in render pipeline and in URP. That
takes a GPU that runs compute shaders, as every renderer of Tide's does:
Direct3D 11 and 12, Vulkan, Metal, desktop OpenGL or WebGPU. On one without
them (OpenGL ES 3, WebGL 2), the views' 2D is drawn with Unity's GL alone.

A Tide view can draw a texture Unity imported. Give a component a local
`Texture` field, and it's a Texture field of its Unity component, set in the
inspector like any other (a Texture2D or a RenderTexture):

```tide
component Card
{
    float2 at;
    local Texture face; // Set on the prefab or in the scene, in Unity's inspector
}

view Cards(Card card)
{
    if (card.face.size.x == 0) return; // Until Unity has shown the card's GameObject
    Draw.Sprite(card.face, card.at, float2(1, 1));
}
```

The view draws with Unity's own image, as Unity imported it (its Max Size,
compression, mipmaps and color space), with nothing imported twice and no
pixels copied; `face.size` is its size, and its Filter Mode and Wrap Mode are
how it's sampled where the draw says nothing. Only local fields take one: a
texture is this machine's, so nothing of it is in the match. Unity hands it
over as it shows the entity's GameObject, a frame after the entity appears, so
the field has no texture in the views' first frame of it: skip the draw until
it has a size, as above. A script can set the field in Play
(`GetComponent<Tide.Game.Card>().face = other;`), which Tide's views draw from
the next frame; a field left empty keeps the default its Tide declaration
gives it. A singleton's local `Texture` field is set the same way from a
script, `runner.Get<Tide.Game.Hud>().logo = texture;`, as a singleton has no
inspector. A player build keeps the textures as it keeps anything a prefab or
a scene references. An image the game declares itself, `asset Texture logo =
"logo.png";` in a `.tide` file, is Tide's own import of that file and draws in
every host, Unity included; one set in Unity's inspector exists in the Unity
project alone.

Unity's meshes, materials and sounds have types of their own in Tide, which
name Unity's objects and read nothing of them: `UnityEngine.Mesh`,
`UnityEngine.Material` and `UnityEngine.AudioClip`, as local fields set the
same way.

```tide
component Statue
{
    local UnityEngine.Mesh shape;
    local UnityEngine.Material look;
    local UnityEngine.AudioClip hum;
}

view Statues(Statue statue, Transform transform)
{
    Draw.UnityMesh(statue.shape, statue.look, float4x4.Translate(transform.localPosition));
}
```

`Draw.UnityMesh` has Unity draw the mesh with the material, every submesh,
where the call is among Tide's own draws: through Unity's camera, into the
screen (not into a Tide render target), lit as Unity lights a mesh drawn from
a command buffer in your pipeline. `Audio.Play(statue.hum, 0.5)` plays the clip
once through Unity's audio, an AudioSource under the runner ("Tide Sounds"),
apart from Tide's mixer: its buses and effects don't touch it. Unset, each
does nothing. A Tide `frag` can't draw a Unity mesh, and a Unity material
can't draw a Tide mesh: for those, the mesh or the shader is Tide's
(`asset Mesh<V>`, `frag`).

A physics package's components are on GameObjects like the game's own: with
`box3d`, a `Body` and a `Box`, `Sphere` or `Capsule` (Add Component, Tide/Box3D).
A shape's size is by the GameObject's scale, as a collider's is. It's drawn in
the Scene view while its GameObject or one above it is selected, as Unity's
colliders are (select a body to see its parts' shapes), and Edit Shape gives it
handles. With Always Show Tide Shapes on (Project Settings > Tide, this
machine's choice), every shape is drawn, the unselected ones fainter. In Play
the shapes are the match's: each is drawn from its component's live values. The body starts
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
  `TideEntity`, singletons and Tide's drawing are that tick's. Ticks
  before those kept are made again from the start as they're shown. What's
  clicked or sent on an older tick's picture never reaches the match, and the
  match's own local state is left as it was.
- A tick shown is heard: the game's local handlers, and `runner.On<E>` and
  `OnVerified<E>` in C#, hear what the tick sent, as the match's own machine
  heard it. Going on from the tick shown (a step, a recording playing, the
  slider moved on by up to 64 ticks) every tick come past is heard in order;
  a tick gone back to, or jumped far on to, says what it sent alone; a tick
  that stays shown says nothing again. Those handlers run on a local state of
  the shown ticks' own, which starts empty, so what they count or keep is
  apart from the match's.
- **Continue From Here** takes the match back to the tick shown and on from
  there: the ticks after it are gone. Which commands are in the match stays as
  it was, so one already applied after that tick doesn't come again.
- With players of other machines in the match, it never stops for them: Pause
  shows the tick paused at while the match goes on behind it (this machine's
  player as if it let go of everything), and Resume shows its latest tick
  again. Continue From Here takes them back too: they're sent the match's world
  again, as when a client goes wrong.
- A match **another machine runs** is recorded here too, from what this
  machine is sent, where the game hides nothing from its players: every tick
  since this machine came in shows again, and Save Recording works, so a
  tester in someone else's match can save what they saw. It never stops for
  this machine (Pause shows a tick while it goes on), and it isn't this
  machine's to take back: Continue From Here is the machine's that runs it.
- In a game that **hides state** from players (visibility), another machine's
  match is followed instead: the last Keep Seconds of ticks this machine had
  confirmed are kept, as its player may have them. Pause holds those to show
  while the match goes on, the Tick slider and Step go through them with what
  each sent, and Resume follows it again from its latest. Nothing before them
  can be made again here, and there's no recording.
- A **sharded match** is followed the same way, on this machine or another,
  as its player has it: each tick is the player's home shard's world with the
  other shards it follows beside it, so a tick shown has what the game drew
  then. One this machine runs with nobody else in it really pauses, and Step
  runs every shard a tick. It isn't recorded or taken back yet.
- **Save Recording** writes the match's recording (a `.tiderec`) and **Open
  Recording** replays one, a tester's from a bug report say, on any machine with
  a build of the same data layout (the game's components, input and commands as
  they were): each tick is made again with this build and its world checked
  against the recording's hash. The window says how far every tick matched, or
  the first tick whose world went otherwise, which after a change to the game's
  code is where that change first matters. **Replay This Match** replays the
  current one.
- A recording also replays in a build whose **data is laid out otherwise** (a
  component gained a field, fields moved, a singleton is new): it says the
  layout it was recorded with, and its worlds are carried over by name as a hot
  reload carries a match (a field of the same name keeps its value, a new one
  has its default). Its ticks then run with this build's code from what went
  into them. Nothing checks them, as the recording's hashes are of worlds laid
  out the old way, and the window says so. A tick whose input or commands
  changed shape can't be run: the window says which tick it stops at.
  Recordings saved before this (and a game's that tide describes no layout
  for) only replay in a build of their own layout.

In C#: `runner.Pause()`, `Step()`, `Resume()`, `ShowTick(n)`,
`ContinueFromHere()`, `Recording()` and `SaveRecording(path)`,
`Replay(bytes)` and `ReplayFile(path)`, `EndReplay()`, with `runner.Travel`
saying where it is. The runner's Time Travel setting keeps matches in the
editor (the default), nowhere, or in player builds too, so a game can save a
tester's recording for a bug report. A hot reload while paused comes in as
any: the match stays paused, its kept ticks with it if the data layout is the
same (another starts the recording over), and the next step runs the new code.
A sharded match's ticks are kept as its player's shards confirmed them (above).

## Build

A player build (File > Build Profiles) builds the game with it, optimized, for
the editor's own desktop platform (Mono or IL2CPP), WebGL, Android or iOS. The player runs the match as the
editor does. A WebGL player runs Tide's own WebAssembly beside Unity's, with
its threads: serve it cross-origin isolated (`Cross-Origin-Opener-Policy:
same-origin` and `Cross-Origin-Embedder-Policy: require-corp`), as any Tide
page.

An Android player is IL2CPP for ARM64 (Player Settings > Other Settings:
Scripting Backend IL2CPP, Target Architectures ARM64; the build says so
otherwise). tide builds the game for it with Android's NDK, which it finds
installed or fetches, as `tide build --android` does, and the package puts it
in the app as `libtide_game.so`, the game's assets inside it, and adds the
permission a match needs to reach the network. The game's saves are in the
app's own files (`Application.persistentDataPath`). It needs a device with
Vulkan and compute shaders for Tide's drawing, as every renderer of Tide's
does. An Android player has been built and packaged, not yet run on a phone.

### iOS

An iOS app links its game in, and only Xcode on a Mac compiles for iOS. So for an iOS player tide compiles nothing:
it writes the game, Tide's engine and its networking as C sources, and the package adds them to the Xcode project
Unity exports (`Libraries/Tide`), each file with its own compiler flags, the game's assets as a file of the app
(`Data/Raw/Tide/pack`). The export works on Windows, Linux and a Mac alike, and the project then builds on any Mac
with Xcode: it needs neither tide nor Unity there. Don't change those files' Compiler Flags in Xcode: they keep the
game's arithmetic the same as on every other machine, and a match would drift apart without them.

- Graphics API: Metal (Unity's default). Tide draws with compute shaders, which iOS has on Metal alone.
- The app's Info.plist says why it looks at the local network (players on one Wi-Fi reach each other directly, and
  iOS asks the player first) and why it may record (Unity asks for Microphone Usage Description, which the package
  fills where it's empty; iOS only asks the player if the game listens to the microphone). Put your own words in
  Player Settings > Microphone Usage Description, and in the project's Info.plist for the local network.
- The game's saves are in the app's own files (`Application.persistentDataPath`). An app that goes to the
  background leaves its match, as every phone's does.
- A library the game's own C links (a `.a` in a package's folder) isn't added: tide says so as it builds. Add one
  built for iOS to the Xcode project yourself.

**Without a Mac.** Tide's repository has a workflow that builds an exported project on GitHub's Macs, and a command
that sends it there and brings back what it made. You need `gh` (GitHub's command, signed in: `gh auth login`),
Node 24, a fork or clone of the repository whose Actions you can run, and Unity's iOS Build Support module.

1. Build the iOS player in Unity (File > Build Profiles > iOS > Build) into a folder, say `Builds/iOS`.
2. `node <tide's repository>/unity/ios-app.mjs Builds/iOS`. It packs the folder (about 250 MB for a small game),
   uploads it as a draft release only the repository's people can read, builds it on a Mac (about 20 minutes),
   downloads `ios-app/app-unsigned.ipa` beside the folder and deletes the draft.
3. The app isn't signed, and an iPhone runs only signed apps. Install it with a tool that signs it with your own
   Apple ID as it installs: [Sideloadly](https://sideloadly.io) or [AltStore](https://altstore.io) on Windows. With
   a free Apple ID the app runs for seven days, then needs installing again; a paid developer account signs for a
   year and can use TestFlight. On the phone, trust your Apple ID once (Settings > General > VPN & Device
   Management), and from iOS 16 turn on Developer Mode (Settings > Privacy & Security).
4. To try a build with no phone at all: in Player Settings set Target SDK to Simulator SDK (and its architecture to
   ARM64), build, and add `--simulator` to the command. The workflow then runs the app in an iPhone simulator and
   brings back what it logged (`ios-app/app.log`).

If the build fails, the command says so and downloads `xcodebuild.log`: the lines with `error:` say which file.
Send that file with a report.

The editor's game is a debug build (`DEBUG` is defined) and a player's isn't.
Where the game's data differs between the two, a field inside `#if DEBUG`
say, `Packages/com.tide.game` holds the game's C# for both, the editor's inside
`#if UNITY_EDITOR`, and Unity compiles the one that's its own; a script that
reads a debug-only field compiles in the editor alone, as with a C# field of
your own inside `#if UNITY_EDITOR`. A player build checks that the C# in the
project is of the game it builds, and says to let the editor's build finish
where it isn't.

Not built yet: players for another desktop system than the editor's and for
consoles. A
dedicated server needs none of them: it's a Tide build of the same game
(`tide build --server --assets Assets`), which Unity's players join.

The editor's host and each build of the game it loads are made by one `tide`,
and each says which: its version and a number of the files it builds with. If
a build comes from another (`tide` installed again or upgraded while the editor
is open, or a development `tide` rebuilt), the Console says which tide built
the game and which the host, and Play goes on with the build it has. Leave
Play, let `tide` finish its build, and enter Play again: `tide` builds the host
again with the next game, so nothing has to be restarted.

## Requirements

Unity 6000.0 or newer, and the tide version this package was made with.
