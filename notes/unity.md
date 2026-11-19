## iOS players (2026-11-19)

The owner asked for a Unity iOS player of a Tide game, proven as far as it can be without a Mac: "i dont have a mac
i only have an iphone", "feel free to use github actions". Tide on iOS is this and nothing else (no standalone app;
`notes/ios.md` has the engine side and its simulator job). Every pick here is mine (Provisional).

**Where the game is compiled: in the player's Xcode project, from sources.** iOS loads no library of an app's by
name, so the game is linked into the player, and only Xcode's clang with Apple's SDK compiles for iOS, on a Mac.
Weighed: (a) `tide unity --ios` run on a Mac making a static library: needs tide and its platform library for iOS
on a Mac the owner hasn't; (b) the generated C and the engine's sources in Unity's Xcode project, each with its
flags; (c) both. Built: (b) alone. A Windows editor's export is then a project a Mac builds with Xcode and nothing
of Tide's installed, which is what lets a GitHub runner build it. (a) would only save the Mac compiling 140 files
(under a minute of a build that takes many), so it isn't built.

- **`tide unity --ios -o <path>`** (`tide_unity_ios` in `compiler/cli/build.c`; once; on any system, with no
  compiler on it): `<path>/ios/Tide` gets `include/` (the engine's and the platform layer's headers), `engine/`
  (the installed `src/engine`), `game/` (the generated C and a `library.c` giving `tide_host_library`),
  `platform/` (the installed `src/platform`: `platform/src`, `platform/embedded`, stb_truetype and what a build
  generates, `notes/ios.md`), `packages/<name>/` (each listed package's C with its headers) and `own/` (a plain
  folder game's own C). `sources.txt` lists what to compile: `tide sources 1`, then tab-separated lines,
  `framework <name>` (Security, CoreFoundation), `needs microphone` (the game says `Clip.Microphone`) and
  `source <path> <flags>`, `@` in a flag standing for the folder. The game's lines are the compile commands tide
  would run (`queue_game` into a list nothing compiles), cut to what says how code is made: `-std`, `-O`, `-D`,
  the two header folders, C++'s four flags, and `-fno-fast-math -ffp-contract=off -w` on every file. No
  `-flto`, target, sysroot or debugger flags. The pack goes to `<path>/ios/pack`, a file (an iOS app's files are
  files: no `.incbin` as Android's library has), with `unity.json` and `game-hash` as the other players'.
  A game's prebuilt libraries aren't added (tide can't tell an iOS one from a Mac's): each is a warning saying to
  add an iOS build of it to the project. Nothing in the folder is iOS's but the frameworks: a console's project
  would take it too.
- **The package.** `TideBuildProcessor` takes `BuildTarget.iOS` (`tide unity --once --release --ios`), refuses a
  graphics API list with anything but Metal, and fills Player Settings' Microphone Usage Description where it's
  empty, with a warning: Unity builds no iOS player whose code names `Microphone` without it, and `TideAudio`
  does. `Editor/iOS/TideIosBuild.cs` (`Tide.Editor.iOS`, define constraint `UNITY_IOS`, as Android's has its own;
  `[PostProcessBuild]`) copies `ios/Tide` to the project's `Libraries/Tide`, makes a static library target of its
  own, `TideGame`, which UnityFramework depends on and links, adds each listed source to it with its flags as that
  file's Compiler Flags (`-I"$(SRCROOT)/Libraries/Tide/..."`), adds the frameworks to UnityFramework, puts the pack in `Data/Raw/Tide/pack` (`Application.streamingAssetsPath`) and writes
  `NSLocalNetworkUsageDescription` (players on one Wi-Fi reach each other directly, which iOS asks about) and,
  where the game needs it, `NSMicrophoneUsageDescription`, each only where the plist has none. A target of its
  own because every file of UnityFramework is compiled with Unity's `Classes/Prefix.pch` before it (a project-level
  setting), which brings CoreFoundation and Apple's `MacTypes.h`: the first Xcode build failed in `font.c`, whose
  `outline` MacTypes.h has too, and a game's own type names would meet the same. `TideGame` has no prefix header, no
  modules (`CLANG_ENABLE_MODULES NO`) and `SKIP_INSTALL`.
- **`TideLibrary` binds without `dlopen`** where `TIDE_LINKED` is defined (`UNITY_IOS`, `UNITY_TVOS`, or a
  project's own `TIDE_LINKED_GAME`, outside the editor): every embed function and `tide_host_library` is a
  `[DllImport("__Internal")]` of its own name, the ABI is checked as elsewhere, and the pack is read from the
  player's files. The runner gives `--data-dir Application.persistentDataPath` on iOS as on Android. Leaving a
  match when the app goes to the background was already there (`tide_embed_background`, `TARGET_OS_IPHONE`).
- **Found by exporting, and fixed: Tide's shaders didn't compile for Metal.** Unity compiles a player's shaders
  for Metal on any system, and its translator refused 46 of the Sample's (every kernel, and every shader reading
  a storage buffer): "Metal shading language does not support buffer size query from shader". `tidehl_length<i>`
  asked the buffer (`GetDimensions`) to clamp a read as WebGPU does. Under `SHADER_API_METAL` it now reads
  `_TideStorageBytes<i>`, which TideRender sets beside each storage buffer it binds (a draw's and a kernel's);
  other APIs' HLSL is as it was inside the `#else`. The package's own copy kernel (`TideCompute.compute`'s
  TideCopy) takes both buffers' sizes the same way on every API (`_TideCopySizes`). So "the shaders go through
  Unity's Metal compiler as through its Vulkan one" (2026-10-22) was never true until now, on a Mac's editor
  either. The Sample's 21 checked-in test shaders with the helper were patched to what the translator writes, and
  its `com.tide.game` was written again by this tide.
- **The Sample**: `SampleSetup.BuildIos` (`-tideOut <folder>`, `-tideSmoke`, `-tideSimulator`); `PlayerSmoke`
  under the `TIDE_SMOKE` define checks itself as the app starts and, on an iPhone, stays open with what it found
  on the screen, green or red (an app there has no log at hand).

**Building it with no Mac: the iOS app workflow.** `.github/workflows/ios-app.yml` (`workflow_dispatch`: `release`,
`simulator`, `expect`) downloads an Xcode project from a draft release's files (`xcode.tar.gz.*`), builds it with
`xcodebuild` on `macos-latest`, and either packs the app unsigned as `app-unsigned.ipa` (artifact `ios-app`) or,
for a project exported for the simulator, installs it in a booted iPhone simulator, launches it with its console,
waits up to three minutes for a line with `expect` ("Tide smoke:") and fails where that line says FAIL (artifact
`ios-simulator-log`). A failed build uploads `xcodebuild.log`. `unity/ios-app.mjs <folder> [--simulator]` is the
one command around it: tar, split at 1 GiB, `gh release create --draft`, `gh workflow run`, wait, `gh run
download`, delete the draft. A draft release because a workflow can't be handed files, the project is about
1 GB (250 MB packed: Unity's libraries and IL2CPP's output), and a draft is readable by the repository's people
alone. This repository is public, so its runs cost nothing; a private one's Mac minutes are billed.

**What's proven, all on 2026-11-19, none of it on this machine past the export** (Unity 6000.6.0f1 with its iOS
module installed here through the `unity` CLI; Xcode 26.6 on GitHub's `macos-latest`):

- Windows, batch mode: `SampleSetup.BuildIos` exports the Xcode project (973 MB) with 140 Tide sources in
  `TideGame`, each with `-ffp-contract=off`, the pack in `Data/Raw/Tide`, both plist keys, the two frameworks; no
  C# warning; no shader error after the Metal fix (46 before it).
- GitHub's Mac, device build (run 44519221362): `** BUILD SUCCEEDED **` for `iphoneos`, arm64, unsigned;
  `app-unsigned.ipa` is 30 MB with `Data/Raw/Tide/pack` in it. So the sources compile with Xcode's flags around
  them, and IL2CPP's `__Internal` calls link against `TideGame`.
- GitHub's Mac, simulator (runs 44519229642 and 44523229777; a project exported with `-tideSimulator`, arm64):
  built, installed and started in an iPhone simulator, where `PlayerSmoke` said `running True, ball (-2.25, 0.50,
  0.00), mine True, pillar turned True`: the linked game was found and bound, its pack read from the app's files,
  the match ran through the embed, this machine's ball was shown through the Unity prefab on the floor and the
  scene's pillars were turned by Tide. Its drawing check said `not with Tide's renderer (not there; Apple iOS
  simulator GPU, Metal, shader level 45, constant buffers True, compute True, indirect False, storage buffers a
  kernel 31, a frag 31, a vert 31)`: the simulator's GPU has no indirect draws, which `TideRender.Supported`
  asks for, so the package drew with its OnGUI path there, as designed. `PlayerSmoke` passes a simulator on
  the match alone, saying so: run 44544548789, on this work merged with dev's embed ABI 30, ended PASS.
- The same sources outside Unity, in CI's package jobs (`unity_ios_test.mjs`, `notes/cli-and-packages.md`; run
  44530030202): the Sample's match through a stand-in for the player says tick 119's world is `b22f206cd96c179c`
  in the iOS simulator under Xcode's clang, as on Linux, Windows and the Mac under LLVM.
- The engine under Xcode's clang in the simulator, with the pinned cross-platform bits: `notes/ios.md`.

**What only the iPhone shows:** Tide's renderer on a real device's Metal (the smoke's `drew yes`), touch input,
the local-network prompt and a match between the phone and another machine (a room through the relay, a LAN
host), sound through Unity's audio, the microphone, the app going to the background and leaving its match.

**For the owner, with the iPhone** (about a quarter of an hour, Apple ID sign-in aside; the app is built already):

1. The Sample's app is `app-unsigned.ipa` from run 44519221362 (kept as `Downloads\TideSample-unsigned.ipa` on
   this machine). To build it again, or another project: export with
   `Unity.exe -batchmode -buildTarget iOS -projectPath unity/Sample -executeMethod SampleSetup.BuildIos -tideOut
   <folder> -tideSmoke -quit` (or File > Build Profiles > iOS > Build), then `node unity/ios-app.mjs <folder>`.
2. Install it from Windows with Sideloadly (sideloadly.io; it needs iTunes or Apple's device support installed):
   plug the iPhone in, drop the .ipa on it, sign in with an Apple ID, Start. A free Apple ID signs for seven
   days; nothing is published anywhere. On the phone: Settings > General > VPN & Device Management > trust the
   Apple ID, and Settings > Privacy & Security > Developer Mode on (iOS 16 and later; the phone restarts).
3. Open the app. Within a few seconds the top of the screen shows the smoke's line, green for PASS or red for
   FAIL. `drew yes (Metal)` is Tide's renderer on the phone's Metal: the orange stripe on the plate and the cube
   over the ball are Tide's drawing, the ball and pillars Unity's.
4. If it fails, or the app closes at once: a screenshot of the line is enough for the first; for a crash, the
   phone's Settings > Privacy & Security > Analytics & Improvements > Analytics Data has a file named `Sample-…`
   to share.

Whether to sign with a paid developer account in the workflow (TestFlight, no seven days) is the owner's: it
needs the account's certificate and profile as the repository's secrets, and nothing here assumes it.

**Metal on a Mac's editor** (the Sample's drawing tests, for whoever has a Mac with Unity 6000.6; about ten
minutes; never run): from the repository, with tide on PATH,
`/Applications/Unity/Hub/Editor/6000.6.0f1/Unity.app/Contents/MacOS/Unity -batchmode -noAudio -projectPath
unity/Sample -runTests -testPlatform PlayMode -testFilter "BounceTests|ComputeReplayTests|ScaleTests"
-testResults results.xml -logFile unity.log` (no `-nographics`: a Mac's batch mode has Metal; `TIDE_HIDDEN=1`).
Send back `results.xml` and `unity.log`. Running that on GitHub's Macs needs a Unity licence activated there
with the owner's account, which is the owner's to give.

**Not done, and why:** tvOS (the define is there, nothing was exported); a game's prebuilt iOS libraries;
`--distribution`'s cuts for the sources (the flags are `--release`'s: Xcode strips); a second export into the
same folder (Unity's Append: the target is found again by its name and its files listed again, not tried);
the packages' `unity.json` sources and a game with C in a package (Box3D) through the Xcode project (the list
handles them, and `unity_ios_test.mjs` compiles whatever is listed, but the Sample has none); the URP Sample (its
player is the built-in pipeline's, as checked in).
## Unity's meshes, materials and sounds as types of their own: embed ABI 29 (2026-11-19, later)

**Owner**, on the report of the section below, which proposed copying a Unity mesh into a `Mesh<V>` and a clip into
Tide's mixer: "the reference seems more relevant maybe we introduce a Unity mesh type directly instead of forcing
current Mesh to behave correctly, I suppose similar for others", and to the shape I then described, "go with
recommendations". So: separate types that name Unity's objects. Everything past that is my pick (Provisional), and
one differs from what I described: the draw is `Draw.UnityMesh`, not an overload of `Draw.Mesh` (below).

**What a user has.** `UnityEngine.Mesh`, `UnityEngine.Material` and `UnityEngine.AudioClip`: opaque, local fields
only (the match can't hold one: they count as rendering resources), set in Unity's inspector on a component, by a
script on a singleton, exactly as a local `Texture` is. `Draw.UnityMesh(mesh, material, transform)` and
`Audio.Play(clip, volume)`. Spec: Assets, Provisional, "Unity's meshes, materials and sounds are types of their
own"; README, Run; docs/tools/cli.md, Unity.

**How it goes through:**

- **The types** (`ast.h`, `types.c`): three kinds, `TY_UNITY_MESH`, `_MATERIAL`, `_CLIP`, named in the builtin table
  by their dotted names, which the type resolver takes whole (so no `using` shortens them and Tide's `Mesh<V>` keeps
  its name); 8 bytes, `tide_external_ref` in C, the number the other program knows the object by. They're in
  `holds_render_resource` (local code only), field-able, array-able, not pure, not for workers. The schema says a
  field's type by the same dotted name.
- **`Draw.UnityMesh`** (`builtins.c`, a plain table entry) is `tide_draw_external_mesh` (`draw.c`): a
  `TIDE_DRAW_EXTERNAL` command with the two numbers and a matrix of the list's (the 3D transform that's set worked
  in). **Why not `Draw.Mesh(mesh, material, transform)`:** `Draw.Mesh`, `Instanced` and `SkinnedMesh` never reach the
  table; `check_mesh_draw` takes them whole and works out vertices, indices, shaders and `lod:` by what each
  argument is. An overload there means telling a Unity mesh from the rest before that function has checked its
  arguments, in the checker's most special-cased call. A name of its own is one table line and can't change what a
  Tide mesh draw does. The owner can ask for the overload; the runtime under it wouldn't change.
- **The renderer** (`platform.c`): `STEP_EXTERNAL`, made only for the window (a target of Tide's gets none) and only
  where the backend has `external_draw` (`gpu.h`), run in the window's pass in order with the other steps, and in a
  scene view's picture too (`window_3d_step`). The recorder writes `TIDE_EMBED_RENDER_EXTERNAL_DRAW` (mesh,
  material, 16 floats).
- **The replay** (`TideRender.cs`): `Kind.External` in the camera's recording, `CommandBuffer.DrawMesh(mesh, matrix,
  material, submesh, 0)` for each submesh, pass 0 (the forward pass of Unity's own lit and unlit shaders in both
  pipelines). The runner's table says what a number is (`Objects`); one that isn't a Mesh or a Material, or that
  Unity destroyed, draws nothing. Nothing of Tide's draw state is set for it: the camera's matrices are Unity's.
- **`Audio.Play(UnityEngine.AudioClip, volume)`** is `tide_audio_play_external` (`audio.c`): a one-shot in the
  frame's audio with `tide_clip.external` set and no samples, so Tide's mixer plays nothing of it.
  `tide_embed_sounds(e, &count)` gives the frame's such plays once (clip, volume, pitch 1), and the runner plays
  each with `PlayOneShot` on an AudioSource of its own ("Tide Sounds", through the runner's Audio Output group).
  Silent under `TIDE_HIDDEN`, as Tide's sound is, with `TideRunner.UnitySoundsPlayed` counting for tests. No pitch
  or position: `PlayOneShot` has neither, and a source a play would need a pool this didn't build.
- **Handing over** is the texture's path with another size: `tide_embed_local_object(e, type, offset, birth, lineage,
  object)`, the generated `set_local` taking a field by its offset and size (48 for a Texture, 8 for these), the
  generated C# `public global::UnityEngine.Mesh x;` with `assets.Object(offset, x, ref tideGiven_x)`, and one table
  of objects in the runner, numbered from 1, kinds mixed (what a number is, is Unity's to say, where it's used).
  Web: `tide_unity_local_object`, `tide_unity_sounds`/`_sound_count`, `TideWebLocalObject`, `TideWebSounds`.
  `TIDE_EMBED_ABI` and `TideLibrary.Abi` are 29.

**What isn't there:**

- **Lighting and shadows** are what a command-buffer draw gets. Unlit materials are exact (tested). A lit one in
  the built-in pipeline is drawn after the forward passes with whatever light constants were last set, and casts
  and receives no shadows; in URP it's drawn in Tide's pass after transparents, lit by the main light as set, with
  no shadow casting. Drawing in the pipeline's own queues (a real renderer instead of a command) is another design.
- **Unity's camera only, the screen only.** A view's own `Draw.Camera` doesn't move it, and a `RenderTarget` gets
  none of it.
- **One material for every submesh**, pass 0 alone (no multi-pass shaders, no per-submesh materials: a list of
  materials would need the type in a list handed over, which `set_local` doesn't take).
- **Sounds**: one-shots only; no pitch, position, loop, stop, or Tide bus. On the web the calls exist and nothing
  was run.
- The first-frame lag is as for textures (below). Time travel's other ticks were a gap and aren't (closed the same
  day, at the end of this section).
- `target.UnityMesh` exists as a method (a render target has Draw's methods) and draws nothing.

**Tests.** `tide_platform_embed`'s `embed_local_objects_are_handed_back`: the fixture's `Counter` has the three
fields and `Faces` draws and plays them; a mesh with no material records nothing; with both, one EXTERNAL_DRAW with
the two numbers and the view's matrix (the identity moved by (1, 2, 3)); the sound once, at 0.5, then none until the
next frame's play; a field takes only its own size (an object refused where a Texture is and the other way round),
an offset inside the field, and no entity; unset, nothing. The Sample: `Ball.badge` (Unity's Cube), `badgeLook`
(`Assets/Sample/Badge.mat`, Unlit/Color magenta) and `chime` (bonk.wav as Unity imported it) on `Roller.prefab`;
`Emblem` draws the cube two units over the first player's ball and `Heard` plays the chime; the frame tests read
magenta where Unity's camera sees that point, and the sound test counts a play.

**Runs** (as the section below: Unity 6000.6.0f1, hidden batch, this checkout's Sample and tide): PlayMode on
Direct3D 11 68 of 68; the frame tests and ComputeReplayTests on Vulkan 40 of 40; the frame tests on OpenGL Core 3 of
3 (built-in pipeline, after a hot reload, URP: each reads the magenta cube); EditMode 50 of 52, the same two as
before. Native debug: `tide_platform_embed` 35 of 35, `tide_cli_tests`, `tidels_tests`, the HLSL checks. tidec says
the existing errors for a match use of such a field and for the two arguments the wrong way round (by hand).
**Not run:** sanitize, web, a lit material (the Sample's is unlit, so lighting isn't pinned), a mesh of several
submeshes, a Linear project, a scene view's picture of it, Direct3D 12, players, and the sound by ear (hidden runs
are silent; the play is counted).

**A tick shown again has what was given** (the owner, away: "I need you to be independent here with your
decisions"; so the gaps that were bounded and testable were closed rather than left). While time travel keeps
anything (`keep_seconds` above 0), `embed.c` remembers what `tide_embed_local_texture` and `_object` gave, the
latest for each field of each entity (`embed_given`, `remember_given`; of the running build's layout, at most 16,384,
the oldest going first), gives it all to the separate local state as `show_frame` makes it (`give_again`), and gives
that state whatever is given while it's there. So a scrubbed tick, the background mode and a recording draw with
Unity's textures and meshes as the match's own frames do. Nothing is remembered with time travel off (a player by
default), so a shipped game pays nothing. No ABI change: the same calls do more. Left: an entity that only an older
tick has (gone from the match since) is given nothing new, as Unity shows no GameObject to take it from; what it was
given while it lived is still remembered. Test: `embed_shown_ticks_have_what_was_given` (a texture given, then a
pause and an earlier tick shown: its size read and no draw of a mesh; a mesh and material given while it shows,
drawn the next frame; both there as the match goes on). Runs: at the end of the Runs paragraph above this one's
tests, and below.

**Runs after that change:** native debug `tide_platform_embed` 36 of 36 and `tide_platform_travel`, and both again
under Windows ASan (`sanitize`), which the work above hadn't run. Unity, as before: PlayMode `TravelTests` and
`BounceTests` on Direct3D 11, 26 of 26; the frame tests on Direct3D 12, 3 of 3, and with the Sample's color space
Linear for the run, 3 of 3 (the Unity mesh's magenta and the texture's colors there too). Still not run: web, a lit
material, a mesh of several submeshes, players, and a scrubbed tick's picture in Unity itself (pinned natively).

## Unity's own textures in Tide's views: embed ABI 28 (2026-11-19)

The owner: "can we use assets from unity in views inside tide? if not is it possible?", then asked for it built.
Until now only a path declaration drew in Unity (`asset Texture x = "x.png";`, Tide's own import of the file, which
stays as it is), and an inspector reference to an asset had nowhere to go: a match component can't hold a rendering
resource. Textures are built; meshes, sounds and materials are proposals at the end. **Every pick here is mine
(Provisional)**: the owner asked for the feature and ruled on none of its details. No syntax is new.

**What a user has.** `local Texture face;` on a match component is, in its generated Unity component, a serialized
`public UnityEngine.Texture face;` (a Texture2D or a RenderTexture), set in the inspector on a prefab or in a scene.
Tide's views draw with it as with any `Texture`, and the image on the GPU is Unity's own imported object, bound by
the replay: no second import, no pixel upload, no copy. A match singleton's local `Texture` is the same field on its
C# class, which a script sets (a singleton has no inspector). Spec: Assets, Provisional, "A local `Texture` field
takes one of Unity's textures"; package README, Run; docs/tools/cli.md, Unity.

**How it goes through, layer by layer:**

- **The reference** (`engine/include/tide/draw.h`). A `Texture` is a `tide_texture_ref`; a fourth kind beside C's
  pixels, a rendered image and a pack's name: no pixels, no target, `image.height == TIDE_TEXTURE_EXTERNAL` (-3),
  and in its name (the low 60 bits of `image.version`, under the sampling bits every reference has) the number the
  other program knows the image by (28 bits, from 1) and its width and height (16 bits each).
  `tide_texture_external(id, width, height, sampling)` makes one, `tide_texture_is_external` tells one.
  The size is in the value because each game library has its own engine copy: `tide_texture_size` (`mesh.c`) reads
  it with no table to reach. To every reader that doesn't know the kind it's a pack name nothing has: no texture.
  A hot reload's carry-over keeps it (it zeroes references with pixels or dead targets alone).
- **The draw list** (`engine/src/draw.c`). `TIDE_PIXELS_EXTERNAL`: a texture with no pixels, its `id` the name,
  with `TIDE_PIXELS_EXTERNAL_RAW` (bit 60) for a `[Raw]` read (`tide_draw_texture_source_raw`), since a read of its
  channels as numbers is another read for the other program (no alpha worked in). A `[Linear]` read is the list's
  usual twin (`linear`). Appending one list to another re-names it (`append_texture_plain`). The run-of-sprites
  shortcut (`tide_draw_same_source`) leaves external references out: it matches pack names.
- **The renderer** (`platform/src/platform.c`, `texture_for`): asks the backend's new optional
  `gpu.h external(external, width, height, flags)` (`TIDE_GPU_EXTERNAL_LINEAR`, `_RAW`) and uploads nothing; its
  smaller images are taken as there (`mipped`), so no `mipmaps` call is made. A backend that draws for itself has no
  `external` (Vulkan, WebGPU, none): the texture is 0 and reads get the usual fallback. So such a field draws only
  where the recorder is the backend, which is by nature: Unity's prefabs exist in a Unity project alone.
- **The recorder** (`gpu_record.c`): `TIDE_EMBED_RENDER_EXTERNAL` (id, image, width, height, flags), after
  DEPTH_BIAS. `TEXTURE_FREE` of that id lets go of the number alone. Nothing else is recorded of it.
- **The embed** (`tide/embed.h`): `tide_embed_local_texture(e, type, offset, birth, lineage, image, width, height,
  filter, wrap)`: `type` the schema's type of the local fields (a component's, with its entity, or a singleton's,
  with zeros), `offset` the field's. `image` 0 gives the field no texture. False for anything that's no Texture
  field of the game's local state. It goes through the game's generated `tide_embed_game.set_local` (new, last
  member; `embed_codegen.inc`), a switch over the local parts with a `Texture` field of their own, by exact offset:
  a component's through the existing `tide_get_<part>`/`tide_set_<part>` (a row made where the entity has none,
  born unknown, which the frame's own sweep lets go of with the entity), a singleton's in place. A `Texture` inside
  a struct, list or map takes none. `embed_exports` and the web program (`tide_unity_local_texture`,
  `TideWebLocalTexture` in the jslib: ten numbers, no pointers) have it. `TIDE_EMBED_ABI` and `TideLibrary.Abi` are 28 (27 is the view packet's reads, below, merged the same day).
- **The generated C#** (`unity_code.c`, `put_local_fields`): a local part's field whose schema type is `Texture`
  (with `[Raw] ` or `[Linear] ` before it) is `public global::UnityEngine.Texture <name>;` (serialized, with a
  tooltip on a component) beside a `[NonSerialized] tideGiven_<name>`, and the class overrides
  `WriteAssets(TideAssets assets)` with a line each, `assets.Texture(<offset>u, name, ref tideGiven_name)`. Every
  other local field is `[NonSerialized]` and read live, as before. `WriteAssets` joined the member names a Tide field
  keeps clear of (`unity_names.c`, and `analysis.c`'s copy of the list).
- **The bake** needed nothing: `components.json` lists match fields alone, and the converter already leaves out a
  serialized key its component hasn't. (The "not assets yet" error is for an asset in a TideEntity's `references`,
  which an entity field's `@n` names; it stands.) The value never enters a prefab's Tide text, which can't name a
  local field anyway (spec, Local fields).
- **The runner** (`TideRunner.cs`, `TideComponent.cs`): `LiveOf` calls a component's `WriteAssets` after `ReadLive`
  where its class overrides it (`LiveComponent.assets`, found once a schema by reflection), with a `TideAssets`
  saying the local type and the entity. So it's handed for a component that's read (the section below, merged the
  same day): one enabled on a GameObject, which a prefab's or a scene's is; a disabled one hands nothing, and a
  singleton's is handed once a script asked for it (`Get<T>()`, which is how a script sets its field anyway). `TideAssets.Texture` hands over a texture that's another than the one
  handed last (a reference compare: nothing is made, and nothing is called for a texture that hasn't changed),
  everything again for another entity or another game (`AssetsRun`, `AssetsOf`: a pooled GameObject's next entity,
  a schema bound again, an embed started again), none when a script takes it away, and nothing for a field Unity
  leaves empty, so a Tide default (`local Texture face = logo;`) stays. `Give` numbers each distinct texture from 1
  (`externals`, kept while the embed lives, as a field keeps its number) and passes its size and how it samples:
  Point as `Filter.Point`; a texture with smaller images as `Filter.Trilinear`, `Anisotropic` where its Aniso Level
  is above 1 (Unity's Bilinear reads the nearest smaller image and Tide's reads none, so this is the nearer look);
  its Wrap Mode. A texture that isn't 2D is refused with an error naming it, once: the field is left as it was and
  the texture counts as handed. `ReadSingleton` does the same for a singleton's.
- **The replay** (`TideRender.cs`): `Op.External` makes an `Image` that wraps the runner's texture
  (`Externals`), never owned, written, renamed or given smaller images (`external`, `authored`); one that's been
  destroyed, or isn't 2D, reads as a missing image does. Two things make its reads what Tide's own textures give,
  both in the shader, since the image itself isn't touched:
  - `_TideForeign`, 2 bits a texture (`tidehl_foreign`, `hlsl.c`): 1 multiplies the color by its alpha after the
    sampler (Tide stores each color times its alpha, Unity beside it); 2 first makes linear light display colors (a
    Linear project's sRGB texture, read by an unmarked field); 3 first makes display colors linear light (a
    `[Linear]` field in a Gamma project, or of a texture no hardware decodes). A `[Raw]` read is 0.
  - `_TideTurned`, a bit a texture (`tidehl_turned`, `_by`, `_turned_row`): Unity keeps an imported image's rows
    from the bottom and Tide's uv counts from the top, so `v`, its derivatives and a loaded pixel's row are turned.
  Both ride in the sampler mode `tidehl_mode` already builds per module texture (bits 9 to 11), so every generated
  shader's and kernel's text gains the prelude lines and nothing else; keys don't change. On OpenGL Core, whose
  textures carry their sampling, the image's own filter and wrap are reported as they are (`OwnOf`) and never
  changed, so a draw that asks otherwise is emulated from its pixels, as for any texture read two ways.
- **Players** keep the texture because the prefab or scene references it, as any Unity object field's value: the
  registry of "Runtime binding" keeps the prefabs, and they keep it. No name lookup.

**What isn't there, and what to do about each:**

- **The first frame.** Unity shows an entity through a GameObject after the frame's views ran, so the field has no
  texture in the views' first frame of the entity, and for as long as a scene of Unity's is still loading. A view
  asks `if (x.face.size.x == 0) return;`. Removing it means either the bake carrying the value (a prefab's Tide
  text can't set a local field, and what a late joiner or a migrated host learns of the entity is its
  `UnityEngine.Source` alone, so it would be a table by asset and node that generated local code applies at spawn:
  a compiler feature) or a frame split in two so C# binds between ticks and views. Neither is small.
- **Time travel's other ticks** (closed the same day: the section above, "A tick shown again has what was given").
  `show_frame` runs the views on a local state of its own (`travel.local`), which nothing was handed: a scrubbed
  tick's or a recording's frames drew without Unity's textures.
- **A `Texture` parameter of a function a frag calls** carries no mode in the HLSL (as it carries no own-sampler
  mode), so a read of an external through one gets its rows and channels as Unity stores them. A loaded pixel
  (`textureLoad`) of a module's 2D float image has its row turned, not its alpha worked in.
- **Float and HDR textures in a Linear project** are taken as display colors where they aren't an sRGB format.
- **Sprites, cubemaps, arrays, volumes.** A `Sprite` needs its rectangle beside its texture (a `Draw.Sprite` part),
  which wants a value type of its own or two fields; the others need their reference kinds made external the same
  way (`tide_cubemap_ref`, `tide_texture_array_ref`).
- **The inspector in Play** shows the field grayed with the rest (`TideComponentEditor` disables the default
  inspector there); a script sets it.
- **WebGL** players draw none of Tide's views (no compute); a WebGPU player would replay this like the rest. The
  export and the jslib are built and compiled, not run in a browser.
- The Sample's hand-made test shaders (`Assets/Tests/Resources/TideShaders`) weren't written again with the new
  prelude: they bind no external image, and an undeclared `_TideForeign` reads as nothing set.

**Tests.** `tide_platform_embed`'s `embed_local_textures_are_the_other_programs` (the fixture's `Counter` has
`local Texture face`, `[Raw] local Texture bumps`, and `Tally` a `local Texture logo`; `Faces` draws them): sizes
as the game reads them, the three EXTERNAL packets with their numbers, sizes and flags (2 for the `[Raw]` read),
each bound by an IMAGE and never written or mipmapped, nothing named again the next frame, all named again after a
reset, an image of 0 freeing the name, every refusal (another field, an offset inside the field, a match type, no
entity, a side of 0 or 65,536, an unknown type, no embed), and the game's hash unchanged.
`unity_code_writes_the_samples_package` covers the generated field and `WriteAssets` (the Sample's `Ball.mark`).
The Sample: `Assets/Sample/Mark.png` (4 by 4: red, green, a violet of middling channels (128, 64, 191), half-clear
white; Point, uncompressed, sRGB), set on `Roller.prefab`'s `Ball`, drawn by Bounce.tide's `Marks` view over a black
square for the first player's ball; the frame tests' `Check` reads red at its top left, green at its top right, the
violet below (which a color-space mistake would darken, where pure channels read the same either way), and gray
where the half-clear white is over black (the color times its alpha: 0.5 in a Gamma project, 0.735 in a Linear
one, which blends light), so rows, channels, alpha and color space are all pinned, in the built-in pipeline, after
a hot reload and in URP.

**Runs** (Unity 6000.6.0f1, Windows, hidden batch with the GPU, `-noAudio`, in this checkout's `unity/Sample`, a
tide installed from this checkout's mingw-release and debug trees given by `-tide-command`, the game built with
`tide unity Assets -o Library/Tide --assets Assets --once`): all PlayMode tests on Direct3D 11, 60 of 60 (ScaleTests
quiet with the new per-frame `WriteAssets`); the three frame tests and ComputeReplayTests on Vulkan and OpenGL Core,
40 of 40 each; the frame tests on Direct3D 12, 3 of 3; the frame tests with the Sample's Player Color Space set to
Linear for the run (and put back) on Direct3D 11 and Vulkan, 3 of 3 each, which is `_TideForeign`'s mode 2;
EditMode on Direct3D 11, 45 of 47 (one ignored as before, the launch-argument test failing only for
`-tide-command`), ShaderTests compiling every shader with the new prelude with nothing said. Native debug:
`tide_platform_embed` 33 of 33, `tide_platform_reload`, `_reload_reads`, `_travel`, `_embed_shards`,
`tide_cli_tests`, and every `tidec_hlsl_*`, `wgsl_hlsl_*` and `tide_hlsl_shaders_valid` (fxc, warnings as errors,
OpenGL Core's defines too). The docs build. **After merging origin/dev** (the reads, the shapes, the orthographic
camera, which tide built a library, time travel's gaps; ABI 28 taken): PlayMode on Direct3D 11 68 of 68, the frame
tests and ComputeReplayTests on Vulkan 40 of 40, EditMode 50 of 52 (the same two), and natively the embed, reload,
travel, shards, CLI, made-by and HLSL tests again. `draw.c`, `mesh.c`, `embed.c`, `gpu_record.c` and `platform.c`
compile for wasm32-wasip1-threads under the warnings (syntax alone; no web tree was built).
**Not run:** sanitize (the tests above ran in the debug tree), a web suite (the engine's change is three branches
on a reference kind no web host makes; `tide unity --web`'s new export and the jslib were not built into a page), a Unity WebGPU or Android player, Metal, `[Linear]` and `[Raw]` fields in
Unity itself (the recorder's flags for them are pinned natively; modes 0, 1 and 2 ran, mode 3 didn't), a singleton's
field from a script, and a player build's stripping (the reference is the prefab's own, as any serialized field's).

**The three proposals below were answered the same day** (the owner chose references, as types of their own, over
copies into `Mesh<V>` and `Clip`): built, in the section above this one. They stay as what was weighed.

**Meshes: not built, and why (a language decision for the owner).** A Tide mesh is two CPU lists of the game's own
vertex struct (`Mesh<V>`: an asset's ID or a made mesh's lists), which any code reads (`mesh.vertices`), and which
the draw list copies, batches, culls and levels by. A Unity `Mesh` is GPU buffers in Unity's layout, with CPU
arrays only where Read/Write is on. Two ways, each with a cost that's the owner's to weigh:

1. **Copy it in.** C# reads the mesh (`vertices`, `normals`, `uv`, `colors`, `tangents`, `triangles`) and fills a
   `local Mesh<V>` field's lists by `V`'s field names, as tidec's importers fill a model's (`position`, `normal`,
   `uv`, `uv2`, `color`, `tangent`; other fields their defaults). Everything of Tide's then works (`Draw.Mesh`, a
   custom `vert`, instancing, `lod:`, code reading the lists). Costs: a second copy of the geometry in memory and on
   the GPU, a player needs Read/Write Enabled on the mesh (the build processor could set or check it), and Unity's
   submeshes become one mesh. It needs `set_local` to take lists (heap ownership in local state) and C# that packs
   `V` from the schema. This is the one I'd build.
2. **Bind Unity's buffers.** The replay already reads vertices from raw buffers by a per-pipeline table of slot,
   offset and format, so a Unity vertex buffer could be bound with a table made from `Mesh.GetVertexAttribute*`,
   matched to `V` by name. Costs: `Mesh<V>` gains a kind whose lists no code can read (a language change), the draw
   list and renderer gain a draw that bypasses batching, culling and levels, Unity must be told to keep the buffer
   raw-bindable (`vertexBufferTarget |= Raw` before upload), formats Tide's vertex pull hasn't (half floats,
   normalized shorts, which mesh compression makes) need adding, and an attribute the mesh lacks has nowhere to get
   `V`'s default from.

**Sounds: not built, and why.** A `Clip` is a name (`tide_clip_ref`, 8 bytes) that generated code resolves to
samples inside the game library (`tide_clip_of`), which Tide's mixer then plays on the audio thread. Two ways:

1. **Through Tide's mixer:** C# hands the clip's samples over once (`AudioClip.GetData`), a `local Clip` field
   names them, and everything of `Sound` works (volume, pitch, buses, effects, `audio` declarations). Costs: the
   samples are copied (a clip must be Decompress On Load; a streaming one has no samples to give), and the table of
   handed clips has to live where `tide_clip_of` runs, so a new hook into each game library's engine copy that
   survives hot reloads.
2. **Through Unity's:** `Audio.Play(clip)` of an external clip becomes a record the runner plays with
   `AudioSource.PlayOneShot`. Nothing is copied and Unity's import is used whole, but Tide's `Sound` controls, buses
   and effects don't apply, and the two mixers' voices are counted apart.

I'd build the first, for the same reason as for meshes: Tide's features keep working on it.

**Materials: a proposal only.** A Tide draw with a Unity `Material` can't go through Tide's renderer as it is: the
replay draws procedurally from raw buffers with Tide's generated shaders, and a Unity material's shader expects
Unity's vertex streams, per-object constants and passes. What could work is a draw the replay hands to Unity
whole: a new draw-list command "this mesh, this external material, this matrix", recorded as a packet the replay
turns into `CommandBuffer.DrawMesh(mesh, matrix, material, submesh, pass)` at its place in the frame's order, with
Unity's own mesh (so it wants the meshes decision first, and way 2's external mesh at that) and Unity's lighting
as far as a command buffer draw gets it (in URP, a pass drawn after the pipeline's own lights are set up; no
shadows cast unless drawn in the shadow passes too). The language side would be a `Material` resource type that
only a local field holds and only `Draw.Mesh` takes, with no fields Tide code reads. Until then the supported way
to use a Unity material is what the package does already: the entity's GameObject wears it, and Unity draws it.

## The packet carries what Unity reads; a part's body by a generated accessor (2026-11-19)

The owner asked for the Unity integration's known gaps to be closed; these are the two performance ones left on
2026-11-10 (Made for production, "Not done, and why"): the view packet carrying every component of every shown
entity, read or not, and `TideParts.BodyOf` finding a shape's body by reflection each frame. Phase 1's decision 4
("every match component of shown entities and singletons") is replaced by what's below. Every pick is mine
(Provisional); the one that changes what a game's C# must write is marked.

**What's read.** The runner keeps a set of the schema's types Unity reads and tells the embed
(`tide_embed_view_want`, embed ABI 27; notes/standard-host.md, The view packet by what the other program reads); the
game's frame writes the entities and those types alone:

- **Tide's `Transform` and `UnityEngine.Source`**, always: the runner places and finds GameObjects by them. A view
  rule's component too.
- **A component**, once one of it is enabled on a GameObject (a prefab's, a scene's, one a script added), and from
  then on, GameObjects of it coming and going or not: `TideComponent` counts its own `Awake`, `OnEnable`,
  `OnDisable` and `OnDestroy` by type (`Said`: enabled on active GameObjects, disabled by their own checkbox, ever
  enabled; "ever" is a runner's, which starts over from what's enabled as it starts, `StartOver`). While it's read, every shown entity that has it has its component, added by the runner where the
  GameObject hasn't one and taken away where the entity hasn't, exactly as before.
- **A singleton**, once `TideRunner.Get<T>()` or `TryGetSingleton` asked for it. Before, every singleton with a class
  was read into its object each frame, asked for or not.
- **An event**, while something hears it (`TideRunner.On`/`OnVerified`, a `TideEntity`'s; counted in
  `TideHearing.Listen`, and taken away with a runner or a `TideEntity` destroyed with handlers still on it,
  `TideHearing.Forget`). One nothing hears isn't kept by its tap at all, where its record was written and skipped.
- **Whatever a script asks for**: `TideRunner.Read<T>()` and `Read(name)`, a `TideEntity`'s `Has`, `TryGet` and
  `Get` of a `Value`, `RowOf`. Asked once, read from then on; the names are kept across a new build's schema.
- **Everything**, with the runner's `readEverything` ("Read Everything", off by default): what the packet carried
  before.

**Asked for the first time, it's there in the same frame.** `tide_embed_view_again` writes the frame's packet again
with what's read now (the same worlds, entities in the same places, the same events), and the runner reads it
(`Reread`): so `Get<T>()` gives the singleton, `Has` answers and `Read<T>()` puts the component on every GameObject
whose entity has it, at the first call, not a frame later. The same when a GameObject made for an entity brings
components nothing read till then (its prefab's): `Show` asks once its GameObjects are made, before their components
are written, so a prefab's first instance has its entity's values in its first frame. One packet written twice, in
the frame a type is first read; where the embed can't (a request or a time travel call since the frame), the next
frame's has it.

**Disabling says "not read here".** A disabled Tide component stays on its GameObject and its entity keeps the
component in the match (tide never read `m_Enabled`). While the only ones of a type there are disabled ones, the
type isn't read, and the runner neither writes nor takes away components of a type the packet doesn't carry
(`TideEntity.DropStale` skips them; a GameObject's count of them, `LiveUnread`, keeps the every-frame check a
compare). Enabling one reads the type again. A disabled one beside enabled ones is still written, as the type is
read.

**What a game's C# sees.** One that reads everything through GameObjects' components sees what it did: authored
components are live from the entity's first frame, singletons come from `Get<T>()`, events from `On`. Changed
(the owner's since, below): **a component the game gives an entity as it runs, which no prefab, scene or
script has put on a GameObject, isn't added to its GameObject until something asks** (`runner.Read<T>()` once, or
`Has`/`TryGet`/`Get`); the Sample's own case is `Owner` on a player's ball. Before, the runner added every component
of every entity, so `GetComponent<Owner>()` found it. Weighed: reading every type that has a C# class by default
(nothing changes, and nothing is saved for a game that reads through components, as every class is one), and reading
only where a GameObject has one enabled right now (a type flickering in and out with its last GameObject, a packet
written twice at each return). `readEverything` is the old behaviour whole. Also changed: `TideRunner.Packet` holds
what's read, so code reading sections from it directly finds only those (`Reads(name)` says which); the `.tide`
file inspector says "not read in Unity" for such a component rather than asking (asking would add components while
it's looked at), and asks for a singleton it shows. A `TideEntity`'s inspector in Play lists the game's components
that aren't read ("Not read in Unity"), each with a Read button (`TideRunner.Read`), since an entity that has one
shows none of it there.

**Built.**

- Engine and generated C: `tide_packet.wanted`, `wanted_types`, `again` and `tide_packet_wants`; a test around each
  component's and singleton's block of the packet writer; taps that keep only what's heard and keep it until the
  next frame (notes/language.md, The packet carries what's read; notes/runtime-ownership.md). The packet's format and
  version (5) are as they were: a section is there or it isn't.
- `tide/embed.h` (`platform/src/embed.c`): `tide_embed_view_want`, `tide_embed_view_again`; `TIDE_EMBED_ABI` and
  `TideLibrary.Abi` 27; both in `embed_exports`; the web program's `tide_unity_view_want` and `_again`
  (`unity_web_read_exports`, a string of its own: one literal holds 4,095 characters) and `Tide.jslib`'s
  `TideWebViewWant`/`TideWebViewAgain`.
- `TideRunner`: `SendReads` (the set worked out when something moved: the components' and the hearing's versions,
  an ask, the setting, a new schema; said before the frame and compared with what the packet in hand has),
  `TakeSent`, `Reread`, `Ask`, `Carries`, `Read`, `Reads`, `readEverything`; `LiveOf` and `ReadSingleton` (a
  binding's rows at once, for the same-frame answers); `packetSerial` in place of the frame number for the row
  tables, as a frame can have two packets. `TidePacket.Size`.
- **A part's body** (task 2). tide writes `global::Tide.ITidePart` on a component whose package's unity.json says
  `"part"`, with `TidePartOf => <its entity field>` (`unity_code.c`, where the field is a GameObject's; the
  `[TidePart]` attribute stays). `TideEntity` keeps its GameObject's part as it collects its Tide components
  (`Part`, with each component's `Beside`), so `TideParts.BodyOf` is two field reads for a component the runner
  writes; any other (outside Play, a test's) asks its GameObject once (`TryGetComponent<ITidePart>`). The reflection
  (`GetComponents` into a list, an attribute and a `FieldInfo` by type, `GetValue`) is gone. A hand-written part
  implements `ITidePart` (the Sample's `GeometryPart`).

**Measured** (the 1,000-entity case of 2026-11-10: the Sample's `Crowd` of 1,000 balls nobody rolls, 1,003 entities
shown, Unity 6000.6.0f1 editor (Mono), Direct3D 11, hidden batch, a debug build of the game; the new
`ScaleTests.WhatUnityDoesNotReadIsNotInItsFrames` measures three ways of reading in one match, medians of 240 frames,
two runs, on a machine other sessions were building on):

| | everything (as before) | what the scene reads | the balls' `Ball` disabled |
|---|---|---|---|
| The packet | 173,576 bytes | 156,688 | 88,560 |
| The runner's frame (the game's own frame, the packet, the render) | 1.23 and 1.42 ms | 1.28 ms (and 2.00 in the run another build disturbed) | 1.01 and 1.10 ms |
| Showing the packet in Unity | 0.25 and 0.27 ms | 0.19 and 0.28 ms | 0.135 and 0.14 ms |

"Everything" is `readEverything` on the same build. Against the commit before this one, built and installed as its
own tide with its own package, the same day, the same way (`ABusyMatchsFramesMakeNothingNew`, which asks for both
singletons and whose balls have their `Ball`, so it reads all it has, two runs each): the runner's frame 1.30 and
1.31 ms before, 1.25 and 1.31 after; showing 0.262 and 0.266 ms before, 0.257 and 0.291 after. A game that reads
everything costs what it did. Two more runs of the three ways of reading, taken with those: 1.26 and 1.29 ms
(showing 0.257 and 0.259), 1.17 and 1.15 (0.179 and 0.175), 0.99 and 1.03 (0.134 and 0.134). What the scene reads by itself leaves out the singletons (Extras's list of the 1,000
balls among them: nothing in that test asks for one), `Owner` and the scenes' tags; the balls' `Ball`, which their
prefab has, is still read, so this game's frame is about what it was, as it should be for a game that reads what it
has. With the balls' `Ball` unread the frame is about 0.2 ms shorter and showing takes half the time. The frame's
times move by more than that from run to run here (the 1.3 of 1.6 ms measured on 2026-11-10 is 1.2 to 1.4 now, the
match's own ticks over 1,000 balls being most of it), so the bytes are the firmer number. At the Sample's usual 300
balls, in the whole PlayMode run: 53,176, 47,488 and 26,960 bytes; 0.47, 0.44 and 0.41 ms; showing 0.076, 0.054 and
0.042 ms. `ABusyMatchsFramesMakeNothingNew` still finds no object made in 120 of 120 frames of showing.

**Tests.** Native: `embed_view_carries_what_is_read` (notes/standard-host.md), and
`unity_code_writes_a_physics_packages_rules` checks Box3D's `Part` is an `ITidePart` giving `of`. The Sample's
PlayMode `ReadTests`: a prefab's and the scene's components read and `Owner` not, with no `Owner` on the ball, until
`Read<Owner>()` puts it there in the same frame with its value; an entity asked for `Owner.Value` answering at once;
a singleton not read until `Get<Room>()`, there at once and the same object after, `TryGetSingleton` reading too, an
event read once heard; a ball's `Ball` disabled leaving the packet smaller and the component in place, read again
once enabled. `ScaleTests.WhatUnityDoesNotReadIsNotInItsFrames` asserts each packet smaller than the one before.

**Runs** (after merging origin/dev, which brought time travel's heard ticks and the Scene view's shapes; Windows,
Unity 6000.6.0f1 in this checkout's `unity/Sample`, hidden batch, BelowNormal, `-noAudio`, a tide installed from this
checkout's mingw-release and debug trees with `-tide-command`): PlayMode 68 of 68 on Direct3D 11; EditMode 49 of 52,
two skipped (the shader compile under `-nographics`, the ABI-5 fixture) and the launch-argument test failing only for
`-tide-command`, as before. Native debug: `tide_platform_embed` 33/33, `tide_platform_travel` 11/11,
`tide_platform_embed_shards`, `tide_cli_tests`' unity cases 13/13, and all four under Windows ASan after the merges
(33, 12, 1, 13). Web debug: `tide_platform_embed` builds for wasm (it isn't run there). With the later changes
(the hearing's counts, the inspector's list, the part's test): EditMode 50 of 53, the new
`GeometryTests.AShapesBodyComesFromThePartItsEntityKeeps` among them (a shape's body from the part its `TideEntity`
keeps, none for a part of its own GameObject or a part destroyed), and PlayMode 68 of 68 again.

**A WebGL player** (the web bridge's side, run): `tide unity --web --once` builds the Sample's program with
`tide_unity_view_want` and `_again` (a web-release platform library installed beside the others),
`SampleSetup.BuildWeb` builds the player, and `web-smoke.mjs` runs it in headless Chrome (muted): the match runs,
this machine's ball is found by its `Ball` component, placed by the match (so the prefab's component was read
through `TideWebViewWant`, and its first frame's packet written again through `TideWebViewAgain`), and the pillars
turn. The smoke's last check, that Tide's renderer drew, had failed there since 2026-11-05, when Unity's WebGL 2
stopped taking the renderer (Unity's OpenGL: Core stays, ES 3 and WebGL go): `PlayerSmoke` now passes where
`TideRender.Supported` says the GPU can't, saying so in its line.

**On the tree as merged** (origin/dev at f20db14d6, where other work took embed ABI 28 to 30 and gave the
Sample's `Ball` local fields of Unity's, so its packets are larger than the tables above: 76,048, 70,360 and 26,960
bytes at 300 balls): native debug `tide_platform_embed` 36/36, `tide_platform_travel` 13/13,
`tide_platform_embed_shards` 2/2, the CLI's unity cases 13/13; EditMode 52 of 55 (the same two skipped and one
failing for `-tide-command`); PlayMode 70 of 70 on Direct3D 11; the WebGL player built again and its smoke PASS.

Not run: IL2CPP and Android players, Vulkan, Direct3D 12 and OpenGL Core (no drawing code changed), a physics
package's parts in a running Unity game (no Sample game has a `Part`; the accessor is generated, unit-tested, and
exercised by the test above), and the two inspectors' new lines in a window (batch runs have none).

**Owner (2026-11-19), asked about what was still open: "i want you to go with your recommendations".** So these
stand as decided, no longer picks to confirm:

- **Reading by need is the default.** A component the game gives an entity as it runs, which no GameObject has, is
  read once a script asks (`Read<T>()`, `Has`, `TryGet`, `Get`); `readEverything` stays off by default, as the
  switch for a project that wants every component on every GameObject.
- **Whole types, not rows.** A component is read for every shown entity that has it or for none, which is what lets
  the rule be "while it's read, every entity that has it has its component". A game that reads `Health` of one boss
  among a thousand entities pays for all of them. Reading by entity would have the runner say entities, which
  change every frame, where types change a few times a match, and would make which GameObjects have a component
  depend on which were asked about: not built, and not planned unless a game's frame shows the need.
- **Whole components, not fields.** A component's row is tens of bytes; what costs is the rows, and a container a
  field holds, which comes with its component. Not built.

**Left.**

- The inspector's "Not read in Unity" list is of the game's components, not of the selected entity's: which
  entities have a component that isn't read isn't known in Unity, which is the point of not reading it.

## Physics shapes in the Scene view: drawn as Unity's colliders are, and checked against them (2026-11-19)

Phase 4's "gizmos for Tide physics shapes" and the authoring gaps' "gizmos". The wireframes were built on
2026-10-22 and 2026-10-25 (Packages' components in Unity) for the selected GameObject alone and never checked
against anything. All picks mine (Provisional).

- **When a shape is drawn** (`TideShapes.Draw`, `Drawn`): while its GameObject **or one above it** is selected
  (`GizmoType.InSelectionHierarchy`), as Unity draws the colliders under a selected Rigidbody: selecting a body
  shows its parts' shapes, which it didn't. Unselected ones are drawn only with **Always Show Tide Shapes** on
  (Project Settings > Tide; `TideShapes.Always`, EditorPrefs `Tide.AlwaysShowShapes`, this machine's, off at first),
  as Physics 2D's Always Show Colliders does Unity's, and fainter (`Faint`, alpha 0.35 against 0.9). Unity's 3D
  colliders have no such switch outside the Physics Debugger; one switch for both of Tide's is simpler than two.
  Tide's blue is kept rather than Unity's green, so the two can be told apart where a GameObject has both.
- **With it on, Unity asks for every Tide component each repaint**: `Shaped(type)` answers from a table whether a
  type has any of the five attributes, before any reflection on the component.
- **Edit mode and Play.** Outside Play the fields are the authored values. In Play they're the match's: the
  generated `ReadLive` writes each frame's values into the same fields (a size divided by the scale again, a part's
  offset or points brought back from its body's frame: `TideScale.Unscale`, `TideParts.Placed`/`Points`), and the
  GameObject is where the match has the entity, so the same drawing shows the live shape. Nothing separate draws
  in Play.
- **A primitive's wires are plain lines** (`TideShapes.Wires`, from `PrimitiveOf`'s matrix, centre, size, radius
  and height): a box's twelve edges, a rect's four, a sphere's three rings, a circle's one, a capsule's two rings,
  four sides and four half rings, a ring 64 lines. The gizmo hands them to `Handles.DrawLines` as they are, where
  it called `DrawWireCube`, `DrawWireDisc` and `DrawWireArc`, so what the tests read is what's drawn, and nothing of
  Tide's is between them and Unity's line drawing. `WorldBounds` is the box in the world that holds the shape,
  Unity's `Collider.bounds` of the same one.
- **Tests** (EditMode `ShapeTests`, fixtures `Assets/Tests/Geometry/GeometryBox`, `Sphere`, `Capsule`, `Rect`,
  `Circle` with the attributes tide writes for Box3D's and Box2D's):
  - a Tide box, sphere and capsule against a `BoxCollider`, `SphereCollider` and `CapsuleCollider` of the same
    fields on the same GameObject, at five placements (plain; scaled per axis; turned and scaled; mirrored; under a
    moved, turned, scaled parent): the bounds agree to 0.4 mm;
  - a rect and a circle against `BoxCollider2D` and `CircleCollider2D`, turned about z, scaled and mirrored; in
    each of these, every point of the wires is inside Unity's bounds;
  - the wires themselves: a turned, scaled box's are twelve edges, each between two of its eight corners along one
    axis; a rect's four sides; every point of a sphere's rings on the sphere, the rings joined and closed; every
    point of a capsule's at its radius from its middle line, its height end to end, and one shorter than it's wide a
    sphere, as Box3D makes it (notes/box3d.md);
  - a part's box in Play: its offset baked as tide bakes it, shown back by `TideParts.Placed` as authored, drawn
    where Unity's collider is, and again after its body moved;
  - when a shape is drawn: selected, under a selection, unselected with and without the switch, the switch kept in
    EditorPrefs, which types have shapes, and the gizmo's own `DrawGizmo` flags.
- **Read against the packages' notes** (notes/box2d.md and notes/box3d.md, whole, and the shapes' declarations):
  sizes are whole sizes about the offset, a capsule stands along its body's y with its height end to end, as drawn;
  no shape, joint or gizmo gap of Unity's is listed there.
- **Runs:** Unity 6000.6.0f1, hidden batch, Direct3D 11, this checkout's Sample: ShapeTests 5 of 5 and GeometryTests
  5 of 5. No C# warnings.
- **Can't be seen from here:** the Scene view itself. Agent runs open no window, and Unity draws gizmos only inside
  a window's repaint: `Handles` read the current GUI event, which can't be set outside one (tried: a repaint event
  set by hand stays null, and `Handles.SetCamera` throws), so a batch run can't draw one into a texture either. What
  is left unseen is Unity's own: that `Handles.DrawLines` draws these lines in the colour given, the Project
  Settings toggle's row, and Edit Shape's handles (Unity's `BoxBoundsHandle` and the like, unchanged by this).
- **By choice, not left:** outlines, meshes, terrains and joints have no Unity collider of the same fields to be
  held against (their geometry is `GeometryTests`'); a sensor or an asleep body draws as any other, as Unity's 3D
  colliders do.

## An orthographic Unity camera (2026-11-19)

"Not done: an orthographic Unity camera" (Unity's camera is the views', 2026-10-21) was left because the embed's
camera then was a position, a rotation and a field of view, and Tide's 3D cameras were perspective ones. Both
stopped being true a day later: `tide_embed_camera` takes Unity's two matrices as they are (ABI 4, 2026-10-22), and
Tide has `Draw.Camera(transform, projection)` with `float4x4.Ortho`. So nothing needed building, no language or
draw API change is proposed, and nothing is the owner's to decide: what was missing was any run of it in Unity.

- **Run now** (the Sample's PlayMode `BounceTests`, the Sample's own camera made orthographic):
  - `TideDrawsIntoAnOrthographicCamerasFrame`: the whole frame check of `TideDrawsIntoUnitysFrame` through an
    orthographic camera (size 7): the game's frag plates where the camera sees them, the plate under Unity's floor
    hidden by the camera's depth (so Tide's depth is Unity's orthographic depth, near and far planes included), the
    cube over the ball at `WorldToScreenPoint`, the ring around it, and the screen-space Hud, target, stencil and
    depth view as they were. The ring's pixels are matched by 0.5 there rather than 0.35: it's a pixel wide and
    smooth, and where its middle falls between two pixels each is half floor (10 of 16 angles at 0.35, a matter of
    the ball's place on the screen, not of the camera; the floor alone is 0.6 away).
  - `TheMatchShowsItsBall`: the ring's place in the draw packet (`Draw.WorldToScreen`) within 1.5 pixels of
    `Camera.main.WorldToScreenPoint` after the camera turns orthographic, and at another place than the perspective
    camera had it. Runs without a GPU too.
  - `SceneViewsShowTidesThreeD`: a scene camera made orthographic (the Scene view's Iso) shows the cube where it
    sees it (`tide_embed_render_view`), and not a unit beside it.
  - `TideDrawsIntoAnOrthographicCamerasFrameInURP`: the same frame check through URP's pass.
- **Read against the renderer's notes** (notes/rendering.md and notes/drawing.md, whole): nothing of Tide's that
  takes something from the 3D camera needs a perspective one. Culling planes and `Draw.Bounds` come from the matrix
  and are tested with orthographic ones; picking rays unproject through it; the mirror test is a determinant's
  sign, and `Camera.projectionMatrix` is OpenGL's convention as a perspective one's; levels of detail go by `w`,
  which doesn't change with distance there, as an object's size on the screen doesn't; occlusion and GPU culling
  never run under the recorder. One thing that isn't about the projection, and older than this: a scene view draws
  the frame's list again, which was culled and given its levels of detail for the game's camera, so what that
  camera culled is missing from a scene view looking elsewhere (drawing.md, "Culling happens during recording").
- **Runs:** Unity 6000.6.0f1, hidden batch with the GPU, `-noAudio`, this checkout's Sample with a scratch tide of
  this checkout: BounceTests 20 of 20 on each of Direct3D 11, Direct3D 12, Vulkan and OpenGL Core, the built-in
  pipeline's and URP's orthographic frames among them. The Direct3D 11 frame was also saved (`TIDE_UNITY_DUMP`) and
  looked at: the box from above without perspective, the ring about the ball, the cube over it, the plate's stripes.
- **Not run:** Metal (no Mac), and an oblique or off-centre projection set by hand (`Camera.projectionMatrix`:
  the same path, any matrix; Tide's own tests cover arbitrary ones, drawing.md).

## A build says which tide made it, and the host loads only its own (2026-11-19)

Closes "Found on the way, not changed" of 2026-10-25 (Tide's drawing after a hot reload): a host took any
`game-<n>` that loaded. The shape check of 2026-11-10 (Made for production) refuses tables of another size; two
tides that lay the tables out alike, or whose engine copies differ, still loaded each other's builds. All picks
mine (Provisional).

- **What a build carries.** `tide unity` works out which tide it is as each build starts (`made_by` in
  `compiler/cli/build.c`): `<version> (<16 hex digits>)`, the digits a hash of tide's own executable (as it was
  when the process first asked: a tide replaced on disk under a running one is still the old program), its compiler,
  the installation's headers, the engine's sources and the platform layer's library, by their stamps (path, size,
  time).
  It writes that text into the game's library (`tide_host_library_made_by`, in `library.c`) and into the host Unity
  loads (`embed_host.c`: a constructor calling `tide_embed_made_by`, so the host says it as it's loaded and C# calls
  nothing new). Two tides of one version (a development tide built again) are two; so is one whose files changed.
- **What the host does** (`reload_library.inc`'s `load`, after the shape check): a library that says another text,
  or has no such function, isn't loaded. The Console gets an error through `tide_embed_log`, as for any build that
  doesn't load: "tide: .../game-7.dll was built by tide 0.8.0-dev (36d2...240), and what runs it by tide 0.8.0-dev
  (36d2...24f), so it isn't loaded: tide was installed again, or another one took its place, since the game started.
  Stop the game and start it again once tide has built it (in Unity: leave Play, wait for tide's build, and enter
  Play again), so that one tide built both". The match goes on with the build it has, and that build isn't tried
  again. Where it's the first build (`tide_embed_create`), there's no embed and the same error.
- **The installation changing under a running editor**, which "could do the same (not tried)":
  - tide's loop (`unity_build`) sees that the text changed since it built the host, stamps the headers again, and
    builds the host again before the game. So the host on disk and the newest game are always one tide's, and
    entering Play again is enough: nothing is restarted by hand. A host that fails to build is tried again with
    the next build (`made_by` is forgotten), rather than left the old tide's beside new games.
  - A Play that's running keeps the host it loaded (a copy), which refuses the new build with the message above.
  - `TideBuildService.FollowTide` (once a second, from `Drain`): where the tide executable the service started was
    replaced (another size or time, as it is for two seconds: `Replaced`), it starts tide again. The running one is
    the old program, which would go on compiling with the new installation's files.
- **No embed ABI or packet change.** `tide_embed_made_by` is the host library's own business, not an export, and
  `TideLibrary.Abi` stays.
- **`tide run` too** (the same day, after a review against notes/cli-and-packages.md and notes/hot-reload.md: its
  watching host and its libraries can be two tides' as Unity's can, and its headers are stamped once a run, so a
  build after an install mixed old objects with new headers). Its host says its tide (`tide_run_desc.made_by`, in
  the generated `host.c`) and its libraries export the text (`write_library`). The host can't be built again while
  it runs, so tide doesn't build at all once the installation is another tide (`run_build`): "tide's installation
  changed since this run started (it was tide ..., and is tide ... now), so the game isn't built again ... stop
  this run and start `tide run` again". The game goes on with the build it has.
- **Not checked, as before:** a player's game (linked by the package from one build), Android's whole library and a
  page's program (the game and the host are one link). Not in the number: the installation's `mingw/` and
  `lib/clang/` (thousands of files to stamp each build; they change only with tide itself, which is in it) and
  macOS's MoltenVK (the host's alone). A reinstall of the very same version is another tide to a
  Play that's running (its files' times changed), which costs a Play restart and never a wrong load. Stamps, not
  bytes: two installations whose files are the same bytes at the same paths and times are one.
- **Tests:**
  - `tide_platform_reload` (native, ASan): a foreign build published while the game runs is refused, both tides
    named and the fix said, and the build running goes on; a host that says nothing loads it; a host of a third tide
    loads neither (notes/hot-reload.md).
  - `tide_cli_unity_made_by` (`compiler/cli/tests/unity_made_by_test.mjs`, a made-up installation and the controlled
    compiler): the installation changed under a watching `tide unity`, part by part, gives another text in the
    library and the host, the host compiled again; a change of the game alone leaves the host; and `tide run`'s
    `host.c` and `library.c` say the same tide (notes/cli-and-packages.md).
  - The Sample's PlayMode `BounceTests.ABuildAnotherTideMadeIsntLoaded`: the running build's library with one digit
    of its text changed, published as the next `game-<n>` in Play. The Console's error is expected by its text
    (`LogAssert`), no `loaded-<n>` copy stays, the match's ticks go on, and the game ended and started again runs.
    This is the 2026-10-25 crash's case with the tables the same size, which the shape check let through.
  - PlayMode `BounceTests.TideInstalledAgainWhilePlayRunsIsSaidAndPlayAgainWorks`, the real thing end to end with
    the editor's own build service and tide: in Play, the installation's platform library gets another time and
    the service is told of a change; tide builds the host again as another tide, and the game; the running Play
    says the Console's error, keeps its build and its ticks go on; the game ended and started again runs the new
    host with the new game; then the installation is put back and tide builds both once more. It runs only with
    automatic builds on and `-tide-command` (a development tide: an installed tide's files aren't a test's to
    touch), and is ignored otherwise.
  - EditMode `ReplacedTests`: what counts as tide replaced (changing, settled, the same bytes at another time, gone,
    put back), and `TheRunningTideIsStartedAgainWhenItsFileIsReplaced`, live: the running tide's executable moved
    aside and a new file put in its place, as an installer replaces a running program (Windows refuses a write of
    one, its time included), and the service's tide is another process within seconds, once. Under the same
    conditions as the test above.
- **Runs:** Windows ASan (`sanitize`): `tide_platform_reload`, `tide_platform_embed`, `tide_platform_travel`,
  `tide_cli_unity_made_by`, `tide_cli_generation`, `tide_cli_build_overlap`, `tide_cli_build_report` pass. Web debug:
  `tide_platform_embed` builds and links. Unity 6000.6.0f1, hidden batch, `-noAudio`, this checkout's Sample with a
  scratch tide of this checkout's mingw-release tree (`-tide-command`), the editor's build service on: PlayMode whole,
  65 of 65 on Direct3D 11, and BounceTests 20 of 20 on each of Direct3D 11, Direct3D 12, Vulkan and OpenGL Core (the
  end-to-end test above on all four); EditMode on Direct3D 11, 52 of 54: one skipped as before (the ABI-5 host
  fixture), and the launch-argument test failing only because these runs pass `-tide-command` (it checks a launch
  without one, which here would be the owner's installed tide). No C# warnings. Again after the day's last merge with dev (time
  travel's gaps, the view packet and Unity assets in views): PlayMode whole 70 of 70 on Direct3D 11, EditMode 52 of 54
  as before.
- **Not run:** `tide upgrade` itself or the Versions window's switch while Play runs (what they do to the files is
  what the two live tests do); a watching `tide run` end to end, which needs a window (its host's check is
  `tide_platform_reload`'s, its texts the CLI test's, and `run_build`'s refusal is read, not run); Linux and macOS
  (CI's).

## Time travel's gaps (2026-11-19)

The owner asked for the Unity integration's known gaps to be closed; these are the ones "Time travel (2026-10-26)"
ended with, in its order. Every pick here is mine (Provisional).

### 1. The ticks shown are heard

Frames that show another tick than the match's latest (scrubbed back, the background mode, a recording) run the
views on a local state of their own, which heard nothing: no local handler ran there and the packet's EVENTS were
empty, so C#'s `On<E>` said nothing of a tick shown. Now:

- **`tide_replay_heard`** (`tide/session.h`, `session.c`; notes/netcode.md): `tide_replay_check` with an event log,
  which the tick is run with as the server's own machine runs its ticks for its player's local handlers
  (`tick_events`, or a projection game's `tick_client_events` with its `all`; a scene's preparation's events too).
  The world comes out the same.
- **`tide_timeline_heard(t, tick, log)`** (`tide/timeline.h`): what the tick that made the world after `tick` ticks
  sent, by running that tick again on a copy of the world before it (one it keeps; else the one a seek made its
  world from, `seek_before`, a page-sharing copy taken at a seek's last step, so stepping through ticks before the
  ring doesn't seek again from the start; else made by `tide_timeline_world`). Nothing for its first tick. One more
  run of a tick for each tick heard: a debugger's cost.
- **The embed** (`embed.c`): `travel_gather`, before the world of a frame's tick is asked for, puts into
  `travel.events` what the frames come to hear, and `show_frame` hands it to the separate local state's handlers
  (`game->events`) before its views, as `tide_run_frame` does a session's. The taps `tidec --embed` adds are among
  those handlers, so the frame's packet has them as EVENTS and C# hears them as it hears the match's. As heard:
  each event once as predicted and once as confirmed (a tick of a recorded match is the server's own). What's heard
  when (`TRAVEL_HEARD_MOST`, 64):
  - going on from the tick last shown (a step, a recording playing, a scrub forward of at most 64 ticks): every
    tick come past, in order;
  - any other tick come to (the first one shown, one before the last shown, one further on than 64 ticks): what the
    tick that made its world sent, alone, so the tick a debugger lands on says what it did;
  - a tick shown again: nothing again.
  Handlers read the match as their event's tick left it (the log's own rows, as always) and count in the separate
  local state, which starts fresh as before: the match's own local state hears none of it and is as it was when it
  goes on. Its sounds play (`tide_platform_audio` after the frame) unless the match's own frame ran unseen in the
  same frame (background mode), whose sound the frame's is.
- **Fixed on the way:** in background mode the match's own frame ran unseen and its taps kept what they heard until
  the next packet, which was the shown tick's: C# heard the live match's events on an old tick's picture. The
  unseen frame's packet is written and dropped now (`background_frame`'s `write_view`).
- No embed ABI or packet change: EVENTS are as they were, and the package's C# is untouched.
- **Tests:** `tide_platform_travel`'s `travel_shown_ticks_are_heard` (travel.tide's new `Bumped` event, sent by
  the Bump command's handler, and its local handler counting in a local singleton): a tick before the ring shown
  hears its Bump once with the match as that tick left it, counted as its local state's first while the match's
  had counted three, in the packet once as predicted and once as confirmed with the tick's number; shown again,
  nothing; the tick before it nothing, a step on from it the Bump again; steps on each heard as come to; several
  ticks on at once all heard; back, the tick landed on alone; the match's own next Bump its fourth; and a
  recording played, its four in order, in the packets too. `travel_never_stops_a_match_for_other_machines`: a
  command behind the tick shown is heard by the match's own handlers and is in none of the shown tick's packets.
  The Sample's PlayMode `TravelTests.ShownTicksAreHeard` (the ball's `Bounced`: `On` and `OnVerified` at the tick
  shown, the ball's local `heard` counted from 1 there, a step to it, a recording played hearing every bounce, the
  match's own count as it was after).
- **Runs:** native `sanitize` (Windows ASan): `tide_platform_travel` 11/11, `tide_platform_embed` 32/32. Web debug:
  the platform layer and engine compile (`tide_platform`); no web test runs the embed's travel. Unity 6000.6.0f1,
  this checkout's Sample, batch, `-nographics -noAudio`, `TIDE_HIDDEN`, `-tide-command` a tide installed from this
  checkout's mingw-release and debug trees: PlayMode `TravelTests` 6/6 (the five there were in one run, the new one
  alone in the next, after its test's own fixes). The Sample's `com.tide.game` as that tide wrote it for the runs
  isn't committed (its hash is the build's). Not run: EditMode (no editor code changed), a GPU run, Unity on the web.
- **Left:** the standard host's own pause (`tide/host.h`'s dev menu, notes/standard-host.md) shows earlier ticks
  through the match's own local state and still hears nothing of them: it's another host's design (its local
  state isn't separate), not Unity's. `Shown`/`Hidden` availability events aren't made for a tick come to by a
  jump (the separate local state hears what ticks sent, not what differs between two worlds shown).

### 2. A match another machine runs is followed

A client had no time travel at all ("the match is another machine's"): the journal is its server's. Its confirmed
worlds are kept now, which is what the section's note said could be. No journal is made on a client, so nothing of
it replays: it's a window over the ticks this machine saw confirmed, not a recording.

- **The session** (`session.c`, `tide/session.h`; notes/netcode.md): `tide_session_recorder.verified(user, tick,
  world, heard)`, optional. After the client's update (`record_client`), each tick it has had confirmed since the
  last one told is told once, from the worlds the client's ring still has of them (a confirmed tick's world isn't
  made again; the ring holds the ticks up to the one it's ahead at), with what the session's log has of the tick
  that made it as confirmed (`as` VERIFIED, copied for both kinds of handler). A world that came whole
  (`first_tick` moved), ticks confirmed faster than the ring keeps, or the first told, start over at the latest:
  the recorder sees a tick that isn't the one after its last. Not on the machine that runs the match, nor in a
  sharded one. The events are there only where the session's host takes them (`tide_session_events`), and only
  those with a confirmed handler: every event of the game's own in a `--embed` build, whose taps are both kinds.
- **The timeline** (`tide/timeline.h`): `tide_timeline_follow(t, tick, world, heard)` keeps the world in the ring
  and the tick's events beside it (`kept.heard`), with no record: `first` is the first tick kept, `last` the last
  told, `tide_timeline_world` gives kept worlds alone, `tide_timeline_heard` what it was told, and
  `tide_timeline_cut` and `_save` refuse. A tick out of order starts it over (`tide_timeline_begin`).
  `tide_timeline_follows`. `tide_timeline_recorder` has the third callback; `tide_timeline_set_game` drops the kept
  events (laid out as the build that heard them has them).
- **The embed**: `travel_why` lets a connected client pause; the timeline's recorder goes to the session with
  `travel_verified`, which follows only while the frames don't hold a tick (not paused, no recording shown): a
  pause **holds** the ticks kept then, so the tick shown doesn't fall out of the ring while the match goes on, and
  going on follows again from the match's latest (the ticks it ran meanwhile aren't kept). `travel_background` is
  true on a client, so a pause is always the background mode: the match's own frame runs unseen with nothing held
  (this machine's player as if it let go of everything, as on a host with others in), and `show_frame` shows the
  tick held. Step goes through the ticks held and is refused at the last; `tide_embed_continue` is refused by
  `tide_session_rewind`, `tide_embed_recording` gives none. A recording still opens there (the match held behind
  it). A machine that takes the match over (host migration), or the other way, starts over with what the session
  says next (`travel_follow`: a timeline that follows on a server, or records on a client, is dropped).
  `tide_embed_travel.followed` (the pad byte; the struct's size is as it was) says so.
- **No embed ABI bump** (mine): the byte was padding, zero from an older library and ignored by an older package;
  nothing reads otherwise. Taking a number is for a layout or meaning that changes.
- **The package**: `TideTravel.followed`; the window says "Following another machine's match: the ticks confirmed
  here are kept, 61 to 90" and, paused, that the match goes on behind what's shown, and greys Continue From Here,
  Save Recording and Replay This Match there.
- **Picks:** a pause holds the kept ticks rather than following on behind (else the tick looked at is gone after
  Keep Seconds, with no start to make it again from); the worlds are this machine's own confirmed ones, so a game
  that hides state shows what this player may have, never more; no client-side journal.
- **Tests:** `tide_platform_travel`'s `travel_follows_another_machines_match` (the embed joins a plain session's
  match through a game's own network over loopback): followed and live with the last 30 ticks kept and none
  before; a Bump it sends taken by the other machine's match; paused, the match goes on (20 ticks and more) while
  first, last and shown stay; back through the held ticks each world has the hash the server's had, and the
  Bump's tick is heard once where it's landed on, as predicted and confirmed in the packet; nothing before the
  first shows; a step on from the tick before the Bump hears it; no step past the last; no going back, no
  recording; going on, live and followed again from the latest, still connected. EditMode `TravelWindowTests`
  (what the window says of a followed match, live and held).
- **Runs:** native `sanitize`: `tide_platform_travel` 12/12, `tide_platform_embed` 32/32. Web debug: `tide_platform`
  compiles. The docs build. Unity as for gap 1, the tide installed again from this checkout: PlayMode `TravelTests`
  6/6, EditMode `TravelWindowTests` 2/2.
- **Not built, not run:** a recording made on a client (its frames carry what a journal record does in a game that
  hides nothing, so a client-side journal is possible; not asked for). Unity with a second machine: the Sample's
  PlayMode tests run one editor, so the followed case runs natively alone.

### 3. A recording replays across a layout change

A recording was refused by any build whose layout hash wasn't its own, as it said nothing of how its worlds were
laid out. It says now, and a build laid out otherwise carries its worlds over by name with the hot reload's own
migration (`tide/migrate.h`, as a checkpoint is loaded by another layout's build: `platform/src/checkpoint.c`).

- **The file** (`timeline.c`, `TIDETIME` version 2): after the head, the layout it was recorded with
  (`tide_layout_pack`: its size, 0 for none, and bytes); and each world (the first, and each one gone back to) is
  followed by itself as its layout has it (`tide_game.pack_world`; size 0 without a layout), since migration reads
  those bytes and a delta pack is only the recording build's to read. Version 1 files still load, into a build of
  their layout. The cost is the layout's description and a second copy of the first world and of each jump's.
- **`tide_timeline_desc.layout`** (the build's own, `tide_host_game.layout`): with it a saved recording says its
  layout, and `tide_timeline_load` takes one of another layout: each world through `tide_migrate_world` and this
  build's `unpack_world`, the records as they are. `tide_timeline_about` says `other_layout`, `fields_reset`
  (`tide_layout_fields_reset`), `entities_dropped`, `tasks_dropped`. `tide_timeline_set_layout` for a hot reload of
  the same layout (the old build's description goes with its code).
- **Nothing is checked there** (`unchecked`): a record's hash is of the recording build's world, laid out the old
  way, so a carried-over replay reports no `differs` and its `checked` stays at its first tick, rather than every
  tick "differing". Its ticks are this build's code run on what went into them. A tick whose input or commands this
  build lays out otherwise doesn't read, and is where it stops (`unreadable`, as before). Such a timeline isn't saved
  again (`tide_timeline_save` gives none): it would be a recording of this layout with the other's hashes.
- **The embed** hands the timelines its game's layout (live and loaded, and the new build's at a same-layout
  reload) and says `other_build` 2 for a recording carried over (1 was, and is, another build's code of the same
  layout). No ABI or struct change. The package's window says so in place of its "Every tick to N made the
  recording's world" line.
- **Picks:** carried over by name with the reload's rules rather than a migration of its own; no check rather than
  a hash of some layout-free form of the world (there is none); refusing to save a carried-over timeline; refused
  still where the recording has no layout (version 1, or a game built without one) or this build has none to say.
- **Tests:** `tide_platform_travel_layout` (new: `platform/tests/travel_layout.tide`, travel.tide's game with its
  components' fields in another order, new fields with defaults among them and a new singleton, built as a second
  program; ctest runs it after `tide_platform_travel`, which leaves its recordings where `TIDE_TRAVEL_RECORDING`
  says, a fixture of the two tests): the other build's recording, with a going-back in it, replays through the
  embed as `other_build` 2, the views drawing at its first tick, at the tick gone back to and at its last what the
  recording's build drew there (steps, the walker's x to the bit, jumps, the score), the new fields at their
  defaults, nothing unreadable, nothing checked, and played at its rate to its end; the timeline's own account
  (`other_layout`, nothing reset or dropped), no save of it, a build with no layout to say refused, bytes cut
  short inside the layout refused; and a recording that says no layout refused, saying so. `tide_platform_travel`:
  a recording with its layout hash flipped is carried over (to the same fields) and replayed unchecked, where it
  was refused; `record_in` reads the new file. `tide_platform_dev_menu` (the standard host's recordings, saved
  and replayed through the same file) passes as it was. The Sample's PlayMode
  `ARecordingReplaysFromAFileBitForBit`: the flipped hash carried over, the ball where the match had it, nothing
  checked; a cut-short file refused. EditMode `TravelWindowTests` (what the window says of one).
- **Runs:** native `sanitize`: `tide_platform_travel` 12/12, `tide_platform_travel_layout` 2/2, `tide_platform_embed`
  32/32, `tide_platform_dev_menu`. Web debug: `tide_platform` compiles. Unity as before, the tide installed again
  from this checkout: PlayMode `TravelTests` 6/6 (the Sample's own layout carried over to itself), EditMode
  `TravelWindowTests` 2/2.
- **Left:** the standard host's dev menu makes its timelines without a layout, so its recordings say none and it
  refuses another layout's as before (one field, `tide_timeline_desc.layout`, in `tide/host.h`'s two calls, when
  it's wanted there). The counts of what didn't carry (`fields_reset`, entities and tasks dropped) are in
  `tide_timeline_about` and not in `tide_embed_travel`, which has no room for them without an ABI number. Input
  and command changes aren't carried: their packed bytes are the recording build's alone to read. A real layout
  change in Unity (the Sample's game changed, built and reloaded mid-test) isn't a PlayMode test; the two-program
  native test is that case.

### 4. Sharded matches: a proposal for the owner

(Since, the same day: a sharded match is **followed**, which is this proposal's step 1 and its last, on A1 and B1,
built another way than sketched below: by what the player's clients confirmed, not the servers' worlds. Section 5.
Recordings, ticks before those kept and Continue From Here are still as proposed here, and C is still the owner's.)

Stopped at a proposal, as asked where a real trade-off shows: three do (A to C below). As first written, nothing
of it was built: a sharded match said "a sharded match isn't recorded" (`travel_why`), the session's recorder was
told nothing of one, and `tide_session_rewind` refuses it (it still does).

**What a sharded match is to a debugger** (notes/sharding.md). On this machine it's a `tide_shards` inside the
session: a root and regions, each a server with its own world, tick count and journal, and a coordinator that
carries their messages between updates, follows players, splits and merges regions and makes checkpoints. This
machine's player is a shard client: a client for each shard it follows, one of them its home; a frame's views draw
the home's world and the others' beside it (`frame_shards`), and the embed's packet has them all. So there's no
single world to keep, no single tick to name, and no single journal to replay.

**What's cheap, and safe to build on my picks once A and B are answered** (it only reads the match):

1. *Showing kept ticks, pausing and stepping.* After each `tide_shards_update`, keep a page-sharing copy of every
   shard's world on this machine, with which shard was the player's home and which it followed
   (`tide_session_views`). A shown frame hands the home's world to the views and the others' to `frame_shards`,
   as `write_view` does live. With nobody else in the match, a pause is the orchestrator not updated and a step is
   one update (every server's tick, then one round of the coordinator); with others in, the background mode as it
   is. Each followed shard's own events are heard as gap 1's are.
2. *Ticks before those kept, and recordings.* A shard's journal record already carries the shard messages its tick
   took, so a shard replays alone from its first world, with no orchestrator (`tide_replay`). A sharded recording is
   then a timeline for each shard that ever was (its first tick and world, its records), and for each update the
   player's home and followed shards. A split or merge starts or ends a shard's timeline. It needs a new file form
   (TIDETIME 3, a container of today's), and memory by the shard.

**The decisions:**

- **A. What a tick is.** Each shard counts its own ticks (a split's shard starts at the root's count; the servers
  tick within one update). (A1) The unit is the orchestrator's update, named by the root's tick: one slider, the
  whole match at one moment. (A2) A slider a shard. *Recommend A1*: a match's moment is what a debugger scrubs, and
  shards change hands of entities between updates, so two shards at different moments show an entity twice or not
  at all.
- **B. Which worlds a tick shown draws.** (B1) Those this player followed then, its home's first: what the game
  showed live, which is what a game's views are written for (`Shards.Owns`, a home). (B2) Every shard this
  machine runs: the whole world, which a debugger may want and a game's views never saw. *Recommend B1*, with the
  window listing every shard's region and tick; B2 after it if wanted.
- **C. Continue From Here.** Taking the match back needs every shard's world at one moment and the coordinator's
  own state (the directory of entities away from home, index ranges, routes, what's acknowledged). The coordinator
  captures itself only for a checkpoint, when the match is drained and idle (in `shards_test`'s busy world 86% of
  updates are quiet, the longest stretch without one 16 updates).
  - (C1) Only from quiet updates: each is kept whole (the worlds, and the coordinator's capture), and going on
    from a tick shown goes back to the last quiet update at or before it, the window saying which. It needs the
    orchestrator to capture and restore itself in memory beside worlds it shares pages with, where
    `tide_shards_save`/`_load` pack everything to bytes today: new orchestrator surface.
  - (C2) From any update, by the crash recovery's path: the last checkpoint, each shard's records up to the
    update, the coordinator rebuilt from first reports (`tide_shards_recover`). It needs checkpoints on in the
    editor's sharded matches, and with checkpoints on a split or merge waits for its checkpoint before it's told:
    **time travel being on would change when a match reshapes**, so a match with the debugger kept on goes
    another way than one without. That's a debugger changing what it debugs.
  - (C3) Not for sharded matches.
  *Recommend C1.* C2 is the larger feature and the one with a cost outside the debugger.

**After those:** a sharded world another machine runs, followed as gap 2 follows a match: each followed shard's
client's confirmed worlds, joined at the home's tick (the other shards' at their latest confirmed tick at or before
it, as their clients confirm apart). And costs to measure before any of it ships: a world a shard a tick (shared
pages, so what changed), up to the 512 shards a session may run here, and the timeline's 64 MiB of records, which
is a match's and would be a shard's.

**Order proposed:** 1, then 2, on A1 and B1; C1 once the owner agrees to the orchestrator's new calls; the followed
sharded world last. 1 and 2 touch the session (a recorder callback after each orchestrator update, with the shards'
worlds, ticks and the player's follows), the timeline (a set of them), the embed's `show_frame` and the window; no
generated code, layout, hash or wire format.

### 5. What those left, closed (2026-11-19, later)

The owner, on the report of 1 to 4: "try to close all gaps that make sense please". Taken as leave to go by my
recommendations where a gap had one; every pick stays mine (Provisional). What made sense, in the order built:

- **The dev menu's recordings say their layout** (`tide/host.h`): its timelines are made and loaded with
  `tide_host_game.layout` and handed the new build's at a same-layout reload, so a recording it saves replays in a
  build laid out otherwise, as Unity's do; its line says "recorded by a build laid out otherwise: carried over by
  name, its ticks unchecked". `tide_platform_dev_menu` passes as it was.
- **What didn't carry over is said (embed ABI 30).** `tide_embed_travel` has `fields_reset`, `entities_dropped`
  and `tasks_dropped` after its texts (16 bytes more: `TideTravel.Size` 336, `TideLibrary.Abi` 30, the next
  number at the merge: 28 and 29 went to other work meanwhile). The window adds "Not carried over: ..." under a carried-over recording when
  any is more than none. `travel_layout.tide`'s `Score.drift` is an int where the recording's was a float, which
  doesn't convert: one field reset, checked through the embed and the timeline.
- **Another machine's match is recorded here, where the game hides nothing** (2's "not built"). A client is sent
  all that goes into a tick (the frame a journal record is made of), so:
  - `session.c`: a client confirming a tick from its frame hands its session a journal record of it
    (`client_journal`: the tick, the frame, the downloads it names, as `journal_tick` writes them) with the worlds
    before and after; `client_journaled` tells the recorder `begin` (from the world before, at the first, at a new
    client, or where a tick isn't the one after the last: a world came whole) and `tick`. `journal_client`, before
    each update, turns it on where the recorder takes records, the match is another machine's, isn't sharded and
    the game has no projections (`client_state`); such a client isn't followed too.
  - So the embed's timeline of it is a recording like its own match's: every tick since this machine came in, ticks
    before the ring made again, replay checks, the ticks' events by running them again (gap 1's way), and
    **Save Recording works on a client** (a tester's machine in someone else's match). It never stops for this
    machine (the background mode, recorded on behind the tick shown, so nothing is held) and isn't this machine's
    to take back. `tide_embed_travel.followed` is 3 for it; the window says "Recording another machine's match:
    tick N, from tick M, where this machine came in" and greys Continue From Here alone.
  - Following (`followed` 1) is what's left for a game that hides state, where a client's frames aren't the whole
    tick, and for a recorder with no `tick`.
  - Tests: `travel_records_another_machines_match` (was `travel_follows_...`): `followed` 3, the first tick where
    it came in and staying there, a pause showing a tick while the match is recorded on behind it, every tick back
    to the first the server's world by hash (those before the ring made again), the Bump heard where landed on and
    in the packet, no step at the latest, no taking back, and its recording saved there replaying bit for bit,
    every hash the server's machine's, all checked. `timeline_follows_a_client_that_keeps_no_records`: following
    itself, at the session and timeline (a recorder without `tick` on a client of three machines over the lossy
    loopback): the last 20 confirmed ticks, each the server's world, the Bump's event at its tick, nothing before,
    no save, no cut.
- **Sharded matches are followed** (4's first step and its last, on A1 and B1). A sharded match, on this machine or
  another, reaches the recorder as its player has it:
  - `session.c`: `record_client` follows the home shard's client through the home moving from shard to shard
    (shards count ticks alike, so it goes on while the shard it's at has the next tick confirmed), and
    `tide_session_recorder.beside`, right after each `verified`, has the other followed shards' worlds: each as its
    client had it confirmed at that tick, or at its latest before (`record_beside`, by
    `tide_shard_client_session`). `tide_session_remote_players` counts a sharded match's root's players but this
    machine's.
  - `timeline.c`: `tide_timeline_follow_beside` keeps them with the tick's slot (page-sharing copies, their
    shards' numbers), `tide_timeline_beside` gives them back.
  - `embed.c`: no refusal for a sharded game; `hand_beside` gives a shown tick's other worlds to the game's
    `frame_shards` before its views and again before its packet (and before `tide_embed_view_again`'s), as
    `write_view` does a live frame's. A sharded match this machine runs with nobody else in it **really pauses**
    (no shard ticks) and **steps** (every shard a tick, kept once the player's clients confirmed it): it's followed
    through such a pause. With others in it, or on another machine, a pause holds the ticks kept, as 2's.
    `followed` 2; the window says so.
  - Not for it: Continue From Here and recordings (4's decision C and step 2, still the owner's: below).
  - Test: `tide_platform_embed_shards`' `embed_shows_a_sharded_matchs_kept_ticks` (its rocks drift now, a fiftieth
    a tick, so one crosses the border): `followed` 2 and no background; a pause runs no tick in a second of
    frames; a step is one tick; back through 300 kept ticks every rock is in the packet once at every tick after
    this machine followed the regions (its first few ticks are its home's world alone, the root's, which has no
    rock), each exactly one tick's drift on from the tick before, through rock 2's crossing from one shard to the
    other; no taking back, no recording; going on, followed again.
- **Runs:** native `sanitize`: `tide_platform_travel` 13/13, `tide_platform_travel_layout` 2/2,
  `tide_platform_embed`, `tide_platform_embed_shards` 2/2, `tide_platform_dev_menu`, again on the merge that made
  the ABI 30. Web debug: `tide_platform` compiles. The docs build. Unity as before, the tide installed from the
  merged tree: PlayMode `TravelTests` 6/6, EditMode `TravelWindowTests` 2/2.
- **Still not built, and why:**
  - *A sharded match's recording, ticks before those kept, and Continue From Here* (4's steps 2 and 3): each needs
    what following doesn't have (every shard's journal on this machine, and for going back the coordinator's own
    state), and decision C is a real trade-off that stays the owner's. Following needs neither and works for a
    sharded world on another machine too, so it came first. 4's text stands for those.
  - *The standard host's own pause hearing earlier ticks:* it shows them through the match's own local state, so
    handlers hearing them again would count twice in the state the match goes on with. It would need the separate
    local state the embed has, which is that host's design to change.
  - *Input and command changes across a layout change:* their packed bytes are the recording build's alone to read.
  - *Unity tests of the followed, recorded-client and sharded cases:* the Sample's PlayMode tests are one editor
    with one game that isn't sharded; those run natively through the same embed.
  - *The embed's travel on the web:* its tests are native (`if(NOT TIDE_WEB)`), as the embed's are; the code is the
    same C, compiled for the web each time.

## The replay of 2026-11-14's GPU work, Gamma [Linear] images, multisampled mipmaps, triangle sides (2026-11-15)

Everything the day's readback, number-image, checker-format and capability work records is now replayed and run
in Unity (6000.6.0f1, Windows, hidden batch with the GPU, `-noAudio`), and the review's Unity findings are closed.

**Built in `TideRender.cs`:**

- **[Linear] images in Gamma projects** (review finding 28; spec, Provisional picks for `[Linear]`). Unity strips
  sRGB formats in Gamma, so a marked image keeps its bytes UNorm in `Image.encoded` and is read through a
  linear-light RGBA16F copy (`Decoded`), drawn by `Runtime/Resources/TideSrgbDecode.shader` (a texel load, sRGB
  decoded, alpha kept) for each written texture, array, array part, cube face and sRGB BC1/BC3 or RGBA8 level
  (authored levels each decoded; missing ones made from the copy with GenerateMips, so in linear light). A copy a
  camera draw of the frame read is renamed first (`DecodedAgain`). Color-10 attachments (the renderer's [Linear]
  sampling copies) are RGBA16F in Gamma. The refusal (`srgbRefused`, `SrgbAvailable`, its error) is gone, and the
  three sRGB witnesses run in both color spaces (`GammaProjectsExplainMarkedImagesAndKeepUnmarkedImages` removed).
  Cost: Gamma projects only, twice an RGBA8 image's memory, more for BC.
- **A multisampled color's smaller images** (findings 12/18, the Unity half; WebGPU's and Vulkan's were built).
  A blendable color of a multisampled target is marked `drawn`; MIPMAPS of its id resolves the samples into a
  one-sample copy with a chain (`mipped`, `ResolveAntiAliasedSurface`, with the drawn image bound first because
  OpenGL Core's Unity resolves only its bound target and asserts otherwise), GenerateMips, and binds the copy for
  reads until a pass draws into the target again (`Shown`). A copy a camera draw read this frame is renamed.
- **A cube's faces share one 2D color image** (finding 17): `Image.faceColor`, made with the first face; a pass
  that doesn't clear copies its face in first. Five of the six extra images a drawn cube had are gone. Not done:
  counting the one left in the engine's held GPU memory (`made_face` counts the face's depth alone): gpu.h has no
  way for a backend to say it holds an image of its own per cube, and adding one changes the runtime's interface.
- **Stand-ins of each read's own dimension** (`EmptyRead`): a far depth (kinds 1, 2, 7), a dark cube, a white
  array, zero RGBA32F and R32I arrays (13, 14), zero number images and volumes. Only reached where no image is bound.
- **Smaller images only for blendable colors**: drawn arrays and cubes of R32F, RGBA32F or R32I, and ARRAY_OF images
  of formats 5-7, get no chain (they're loaded, never filtered; Vulkan and tide.js do the same).
- **Integer images are render textures.** Unity makes no Texture2D or Texture2DArray of R32_SInt or
  R32G32B32A32_SInt ("not supported for Sample usage"): a Grid2<int> texture and the renderer's zero 32-bit int
  layers (D's `layers_fallback`, which relied on `MakeArrayOf`'s zeros) failed to be made at all. `NewIntegers`
  makes an R32_SInt/RGBA32_SInt render texture with random writes, and `IntegersInto` copies a box of pixels into
  it from a raw buffer (`TideCompute.compute`'s TideIntsTo2D/Array/3D) in the frame's order, a write of a part
  after `Written`'s rename. Integer volumes upload the same way (their Texture3D upload of an integer format hit
  the same refusal); a device without load/store for the format gets the volume message.
- **A triangle's side** (findings 29, 45). Run, `this.front` was wrong two ways. Unity turns the rasterizer's
  sides round (culling and SV_IsFrontFace together) whenever a render texture is bound on Direct3D, Vulkan and
  Metal, for the projection it flips there, so in a target's pass, which Tide doesn't flip, fronts read as backs
  and Cull.Back culled fronts, with or without a camera drawing. And in the camera's own pass `tidehl_facing`
  turned SV_IsFrontFace round a second time for `_ProjectionParams.x < 0`, though culling there was right. Now a
  target's pass on those APIs sets `SetInvertCulling(true)` until its End, and `tidehl_facing`
  (compiler/src/hlsl.c) turns the side only for Tide's own flip (`_TideFlip < 0`, OpenGL Core's targets).
- **OpenGL Core's blends of several targets** (found by the new tests' order; older than them, as dev's renderer
  fails the same way). A blend said for all targets at once (`Blend [..]` with no index) is set by Unity's OpenGL
  Core for every draw buffer, but a later draw's `Blend 1 One Zero`, which it believes is set already, is skipped:
  after a depth-alone draw (Blend Zero One), the number image at index 1 of a later several-image draw kept its
  old pixels, while the int at index 2 (never blended) was written. `tide_hlsl_unity_shader` now says each of the
  four targets' blends by its index (`Blend i [_TideSrcColor] ...`, numbers' `Blend i One Zero`). Every generated
  shader's text changes; keys don't. Unity's own draws (URP's) still set blends for all targets at once between
  Tide's passes; no test shows a difference from that.

**Checked-in assets.** The Sample's read kernels match what `tide unity` writes byte for byte. 00000000c0de0066
(ReadTargetLayerNumbers) was stale: it sampled where the WGSL now loads; ComputeReplayTests passes read kind 13 and
no sampler for it. Added: 69 Float2, 6a FaceNumbers, 6b LayerInts, 6c-6f the volume kernels, and hand-written test
WGSL with HLSL from `tide_wgsl_translate --hlsl`: triangle_side (c0de0016), flat_int (17), depth_plane (18, a sloped
plane), write_depth (19), compare_layers (1a, a depth array's compare), sample_level (1b, a kernel's SampleLevel).
Every test .shader was written again for the blend and facing changes (c0de0012 and c0de0013 were stale, their
prelude older than the reversed source depth; c0de0002 has no WGSL source and was edited to the same header). The
Sample's `com.tide.game/Resources/TideShaders` is what this tide writes for it now (the rest of com.tide.game, whose
hash is the build's, is left as it was).

**Replay tests added** (ComputeReplayTests): ReadKernelsReadPartsBandsAndPairs, ReadKernelsReadFacesAndLayersOfNumbers
(an R16F cube face, R32F/RGBA32F/R32I layers, the zero 32-bit layer stand-ins), ReadKernelsReadVolumesWholeAndInBoxes,
ReadKernelsReadMultisampledAndDepthAloneDepths, NumberGridTexturesKeepTheirNumbers, IntegerImagesTakeWritesOfParts,
MultisampledTargetsHaveSmallerImagesOfWhatTheyResolveTo, TrianglesFrontIsWhereTheirCornersGoClockwise (a target's
pass, the camera's own, a target's pass with no camera), FlatIntsComeFromTheFirstCorner (OpenGL Core: the last),
SlopeBiasPushesAwayWherePositive, FragsWriteTheDepthTheySay, DepthArraysCompareTheirLayers,
HalfFloatVolumesOfFourBlendEveryAxis, SrgbImagesKeepEveryLevelAndLayerAsStored. One expectation corrected: a cleared
RGBA32F layer's alpha is 1, as Vulkan's and WebGPU's clears give. `platform/tests/embed.c`
`embed_render_target_packets_name_formats_by_number` (finding 48) pins TARGET packets' format words as literals
(0x9671, 0x40005, 10, 0), their color and depth ids (depth 0 when multisampled) and `target_samples` giving 1 for
R32F, RGBA32F and R32I, so a renumbered `tide_gpu_color` can't pass both suites while Unity reads other formats.

**Runs** (a scratch prefix from this checkout's mingw-release `tide_cli` and `tide_platform`; a Gamma Sample and a
Linear copy, each built with `tide unity Assets -o Library/Tide --assets Assets --once`): all PlayMode tests in the Gamma Sample on Direct3D 11 (`-force-d3d11`),
Direct3D 12, Vulkan and OpenGL Core with `-tide-command`: 59 of 59 each, nothing skipped (the sRGB witnesses run in
Gamma now), ScaleTests quiet (none in 120 of 120 frames showing, the whole frame none in 116 to 117).
ComputeReplayTests in the Linear copy on the same four: 36 of 36 each. IntegerImagesTakeWritesOfParts, added after
those runs, alone on the four: passes. EditMode once (`-nographics`): 44 of 47, two ignored as before and the
launch-argument test failing only for `-tide-command`. Before the fixes, on Direct3D 11 the integer textures and
layers failed to be made and the triangle sides were wrong in both passes; on OpenGL Core the multisampled
resolve asserted and ReadKernelsReturnEveryImageOfATarget lost its number image after the depth-alone and
banded tests ran before it. Not run: Metal, a Unity WebGPU player, Android, and the scratch port of
`platform/tests/readback.tide` with the real recorder (the replay tests above hand the replay the recorder's packets
by kind, `embed.c` pins what the recorder writes).

## What the replay owes this day's exact reads and depth cubes (2026-11-14, not run in Unity)

render-targets.md (top) built 32-bit array layers and cube faces, typed array loads, typed volume blends and
`Cubemap(size, Format.Depth)` without touching TideRender.cs (the owner's later pass). A cube of depth alone or of
32-bit numbers is recorded as an ordinary array (6 layers, layer targets), and its reads are the existing
`Texture2DArray` loads and depth compares, so no new packet arrives. The replay owes: the two new image read kinds
13 and 14 (`TIDE_GPU_READ_NUMBER_ARRAY`, `_INT_ARRAY`: loaded arrays of floats and of ints) bound as the recorded
`Texture2DArray` they are, and `EmptyRead` giving a zero RGBA32F / R32_SInt Tex2DArray for them (it gives `white`
now; the engine binds its own recorded fallbacks, so it isn't reached); the catalogue's new ReadTargetLayerInts and
changed ReadTargetLayerNumbers (a load now), which `tide unity` writes. Their HLSL compiles under fxc.
Corrected 2026-11-15 (review): the fallback the engine binds for those kinds was a drawn-into ARRAY packet, which
`MakeArray` makes as a `RenderTexture` nothing clears, so a typed 32-bit array or cube with no image read undefined
values on Unity. It's an `ARRAY_OF` of RGBA32F or R32I now, 1x1x1, which `MakeArrayOf` makes of zeros it writes
(platform.c `layers_fallback`; pinned by embed.c `embed_render_number_layers_fallback_is_zeros`). The shader also
holds a 32-bit cube's face to the layers bound (`min(face, textureNumLayers - 1)`), so a fallback of one layer is
never read past on Metal, where an out-of-range load is undefined.

## Banded reads, parts and volumes: embed ABI 26 (2026-11-14, not run in Unity)

A read of a target or a Texture3D now runs its kernel over a band of rows at a time, each band a storage buffer of
its own bound where the kernel's list's pool goes, read back on its own (render-targets.md, top section). It records
as before: buffers, a compute pipeline, uniforms, images, storage, a 2D dispatch and buffer reads, so no new packet.
Every read kernel's uniforms changed (the part's width, the band's rows and first, the part's first pixel, the
image's size, then the table: 48 bytes) and four kernels were added (ReadVolumeColor/Float/Float4/Int), so `tide
unity` writes 21 computes. The shared read table grew (`from`, `size`, `depth`): embed ABI and `TideLibrary.Abi` are
26 (25 was the grids of numbers', below, made the same day). The Sample's hand-made read kernels (`00000000c0de0014`, `c0de0061` to `c0de0068`.compute) were written again from
the new `read_pixels_wgsl.h` with `tide_wgsl_translate --hlsl` (the old WGSL gave the checked-in files byte for byte
but for c0de0014, which was already stale), and ComputeReplayTests' `ReadKernel` and
`PixelReadKernelReturnsStraightHdrThroughBufferReads` record the new layout. Neither the C# nor the assets were
compiled or replayed in an editor here (the owner's Unity is off limits to this work): the next Unity run checks them.

## For the next Unity pass: a multisampled target's smaller images (2026-11-14)

Built 2026-11-15 (top section).


Vulkan and now WebGPU make a chain of a multisampled target's resolved colors when a read needs one
(render-targets.md, Kernels read every kind of image). `TideRender.cs` was left alone in that work; Unity's replay
still reads the resolved image alone there, as `MakeTarget` gives a target colors with mips only where `samples ==
1` and `Mipmaps()` returns for an RT without them. What the recorder hands it is already all it needs:
`TIDE_EMBED_RENDER_TARGET` carries the target's format with its samples (bit 16 on) and names each resolved color
id, and `TIDE_EMBED_RENDER_MIPMAPS` names such an id whenever a read (a frag, a vert or now a kernel) needs its
chain after more was drawn, never for an R32f/Rgba32f/R32i image (`embed_render_multisampled_target_mipmaps` in
`platform/tests/embed.c` pins both). The replay's part: give each blendable color of a multisampled target a
one-sample companion RT with `useMipMap` (made once), and on Mipmaps of its id resolve the MSAA surface into it
(`CommandBuffer.ResolveAntiAliasedSurface` or a blit), `GenerateMips`, and bind the companion for that id's reads.
No ABI change is needed for it.

## Read kernels, number images and [Linear] run in Unity: three replay fixes (2026-11-14)

What landed on dev the same day without a Unity run (the ten read kernels, number images' filtering, mips,
layers and faces, the [Linear] review fixes) was run on Direct3D 11, Direct3D 12, Vulkan and OpenGL Core.
Three things were wrong, none in the new work's own code, all older and never run where they show:

- **A cube face's depth was never written or tested** (`TideRender.cs`, `MakeCubeFace` and `Record`'s Begin;
  since cube faces were first replayed). Unity binds no depth beside a cube's face when the depth is a 2D
  texture, and says nothing: draws into a face ignored depth, and the face's depth image stayed at its clear.
  A face's passes now draw into a 2D image of the face's own (`Target.faceColor`) with the 2D depth, copied
  into the cube's face as each pass ends (`CopyTexture`). The same rows as before on every API (the face-read
  test checks three faces' top left). A 2D image more a face target; a copy a face pass.
- **A depth-alone array's depth wasn't found** (`MakeArray`; since ABI 15). The recorder gives such an array
  a depth array id of its own (`array_depth`), which is the id draws and kernels read; the replay kept the
  array's id alone, so a `TextureArray(..., Format.Depth)` (a shadow array) read the white 2D stand-in, and a
  kernel refused to run ("mismatching texture dimension"). Both ids name the depth now.
- **On OpenGL Core a frag with fewer outputs than its target has images wrote its color into the others**
  (`compiler/src/hlsl.c`, `tide_hlsl_unity_shader`). ShaderLab's first pass now says `ColorMask 0 i` for each
  image the fragment stage has no `SV_Target<i>` for (none where it writes depth alone, as before). Direct3D
  and Vulkan left them alone already. Every generated shader's text gains those lines; keys are the SPIR-V's,
  unchanged. `00000000c0de0011.shader` (the tests' image draw) was written again with it.

Left as it is: `EmptyRead` gives the white 2D image for a missing cube, array or depth array, which Unity
refuses with the dimension message rather than reading zeros (corrected 2026-11-15: a stand-in of each read kind's
own dimension now, top section). Nothing reaches it now but a refused sRGB image
or an image that couldn't be made.

**Replay tests added** (`ComputeReplayTests`, hand-made commands as the recorder writes them; the kernels are
`platform/src/read_pixels_wgsl.h`'s WGSL through `tide_wgsl_translate --hlsl`, keys `c0de0061` to `c0de0068`:
Float4, Int, Depth, Face, Layer, LayerNumbers, Float, LayerDepth):

- `ReadKernelsReturnEveryImageOfATarget`: a target of RGBA16F, RGBA32F and R32I: the color through the Color
  kernel, the second image's four floats and the third's int bit for bit, after a color-only draw that must
  leave both alone; depth as 1 where cleared and 0.25 in a clipped corner, forward on every API.
- `ReadKernelsReturnFacesAndLayersFromTheTopLeft`: faces 0, 2 and 5 of a cube and a layer of an array, green
  clipped into the top left of red; then a face's depth, a layer's, and a depth-alone array's layer by its
  depth id (0.25 in the corner, 1 elsewhere).
- `NumberImagesBlendHaveSmallerImagesAndAreLayers`: R16F with 1.5 in its left half: 0.75 blended at the
  middle and 1.5 nearest; three levels, the smallest 0.75; read exactly by the Float kernel; and an R16F
  array's layer by LayerNumbers.

**The native fixture in Unity** (scratch only, not checked in): `platform/tests/readback.tide` added to a
scratch Sample's game with a view saying each of `readback.c`'s checks through Debug.Log, so the real
recorder, `tide unity`'s own compute assets and all eleven kernels run: colors, each image of a four-image
target, R8/Rg8 bytes, R16f/Rgba32f/Rgba16f numbers, depth (a frag's 0.25, a multisampled target's, depth
alone 0.75), faces, a face's depth (0.125), layers, layers' depth (0.5, and a depth array's 0.625), number
faces and layers, and the two reads that must fail. All true on the four APIs after the fixes; before them
the face's depth read 1, the depth array's read never ran, and OpenGL Core had the target's number images
overwritten in its right half.

**[Linear]** in a second scratch copy set to Linear color space (the scratch copy's PlayerSettings alone):
the three sRGB witnesses that the Gamma Sample skips pass on the four APIs (ComputeReplayTests 24 of 25 there,
the Gamma-only witness skipped). In the same scratch game: `[Linear] asset Texture` and its unmarked twin out
of the pack (`TIDE_PACK_LINEAR`) read 0.2158 and 0.5020 for the byte 128; a render target cleared to 0.5 read
through a `[Linear] Texture` field 0.2130 and unmarked 0.5; after the clear changes to 0.75, 0.5210: the copy
is made again when its source changes. The same numbers on the four APIs.

**Runs** (Unity 6000.6.0f1, Windows, hidden batch with a GPU, `-noAudio`, tide installed from this checkout's
trees): all PlayMode tests in the Gamma Sample on Direct3D 11 (`-force-d3d11`), Direct3D 12, Vulkan and OpenGL
Core, 44 of 47 each with the three sRGB witnesses skipped; ScaleTests quiet (116 to 120 of 120 whole frames).
EditMode once, 44 of 47: two ignored as before, and the launch-argument test failing only for
`-tide-command`. Native debug: the hlsl, wgsl, unity and `tide_cli` tests (123) pass. Not run: Metal, a Unity
WebGPU player, and the kernels for float2 and for faces of numbers as hand-made replay tests (the scratch
fixture runs both).

## ScaleTests' two objects a frame, clip rects in a target, depth bias's sign (2026-11-14)

**ScaleTests' two whole-frame allocations were a regression** from 8354e85fb (compute images and Texture3D,
2026-11-14), which the test's way of running shows and a game can hit too. Since that commit a target's pass
runs as the frame is read while the camera's own draws wait, so `Begin` on a target calls `Written` for each of
its images: one a kept draw of this Unity frame read (`image.used == Time.frameCount`) is renamed, and the
rename made `new RenderTexture(texture.descriptor)` every time. ScaleTests calls the runner's `Update` itself in
the frame Unity already called it in, so the second read of the frame finds the Sample's two sampled target
images (64x64) read by the first and renames both: two objects, two GPU textures and two copies a frame, in all
120 measured frames (`showing` made none, as the depth thread saw). A probe counted exactly two renames a frame,
starting at the test's first own frame. A game does the same with one read a frame whenever it draws into a
target, or a kernel writes an image, after a camera draw of that frame sampled it. Before 8354e85fb target
passes waited in the camera's commands with the draws, and nothing was renamed.

- **Fix** (`TideRender.cs`): images a rename lets go of are spares, as buffers have been since 2026-11-10.
  `Written` dooms the old image marked `again`; `Collect` moves it to `spareImages` once no camera's commands
  read it (three frames), and `Renamed(RenderTexture)` takes a spare of the same descriptor (size, depth,
  samples, dimension, both formats, mip count and creation flags, compared a member at a time: a struct's
  `Equals` boxes) before making one. At most 32 spares, one unused for 600 frames released, all released with
  the renderer. A frame's renames go round about four images each, which is how many were alive before, each
  made anew. No assertion changed. The runner's median frame in the test went from 1.47 ms to 0.66 ms (D3D12).
- **Also found by reading:** a pass on a target of several colors made a `RenderTargetIdentifier[]` each time
  it began (older than the 2026-11-10 pass, which missed it; no Sample target has several colors). Kept with
  the target now (`Target.colorIds`).
- What's left in the whole frame is what the test allows: about once a second the Input Manager's joystick
  names and the editor's look at `unity.json` (4 of 120 frames on some runs, about 30 objects each).

**A clip rect in a target's pass was upside down** on every API (found making the read-back check below
asymmetric; older than the rendering work, as no test clipped a target by rows). `Record` turned every clip
rect to rows from the bottom, which is right for the camera's own target alone. A target's rows are counted as
stored, from Tide's top everywhere (OpenGL's passes are drawn upside down for that), so its rect's y is Tide's.
Tide's 2D clips drawn into a render target in Unity were mirrored top to bottom until now.

**Checked, and right as they were:**

- **Read-back row order.** `PixelReadKernelReturnsStraightHdrThroughBufferReads` now reads an image with a
  green triangle placed at the top left by its corners alone (clip space) and blue clipped into the bottom
  right 16 by 16; every pixel but the triangle's slanted edge is checked at its place from the top left.
  The kernel's rows and geometry agreed on all four APIs; only the clip rect was wrong.
- **Depth bias's sign.** `DepthBiasPushesAwayWherePositive`: two draws on one plane, depth tested and written,
  in four columns (second +8: the first stays; second -8: it wins; first -8 then none: the first stays; none:
  the second passes, being equal), in a target's pass and in the camera's own. `SetGlobalDepthBias` takes
  Tide's sign as it is on all four APIs, Unity turning it for its own depth. Constant bias only: the fixture's
  shader has no slope (the slope part is run since, 2026-11-15: `SlopeBiasPushesAwayWherePositive`).
- `ClipRectsCountRowsFromTheTop` pins both kinds of pass.

**Runs:** Unity 6000.6.0f1, Windows, hidden batch mode with `-noAudio` and a GPU, a scratch Sample pointing at
this checkout's package, tide installed from this checkout's mingw-release and debug trees. All PlayMode tests
on Direct3D 11, Direct3D 12, Vulkan and OpenGL Core: 41 pass of 44 on each, the three sRGB witnesses skipped
in the Sample's Gamma color space as before. Before the fix ScaleTests failed (0 quiet frames of 120). Not
run: EditMode (no editor code changed), Metal, a Unity WebGPU player. Note for the next run: without a
`-force-` flag this machine's editor picks Direct3D 12, not 11.

## Grids of numbers: embed ABI 25 (2026-11-14)

A Grid2<float2>'s texture is the recorder's pixel format 9, `TIDE_GPU_RG16F` (two halves a pixel), which
TideRender's `TextureFormat` makes `R16G16_SFloat` (`PixelBytes` 4, as before for unknown numbers); a Grid2<float4>'s is
the existing Rgba16f (4) and a Grid2<int>'s or Grid2<byte>'s the existing R32i (7), now also 2D textures of the
TEXTURE packet. `NewTexture` gives integer images (3 and 7) no mip chain, as nothing reads one between levels.
Packets keep their shape; TIDE_EMBED_ABI and TideLibrary.Abi go from 24 to 25 together, so an older replay can't
take format 9 for RGBA8. `embed_render_number_grid_textures` pins the packets (formats, bytes, a part's rows). Not
run in Unity: there is no Unity on this machine for this work.

## [Linear] texture replay: embed ABI 24 (2026-11-14)

The owner approved opt-in sRGB sampling on 2026-11-14. Static compiler roles and importer storage are in
textures.md. Recorder packets retain their shape: pixel format 8 is SRGBA8, internal attachment color 10 is
sRGB, and bit 0x100 of an immutable block format selects its sRGB variant. Block support/strides use the base
format; BC1/BC3/RGBA8 payload bytes and authored levels are retained exactly. TideRender creates
R8G8B8A8_SRGB, RGBA_DXT1_SRGB and RGBA_DXT5_SRGB resources. Internal sRGB targets are sampling copies;
no public Format member or ordinary render target, fragment output or presentation behavior changes.
TIDE_EMBED_ABI and TideLibrary.Abi advance together from 23 to 24 so older replays cannot silently treat the
new format values as ordinary bytes. The C recorder's embed_render_srgb_images_retain_encoding_formats
pins unmodified upload bytes, host BC support, marked block descriptors and RGBA-array fallback descriptors.

**Provisional Unity prerequisite** (withdrawn 2026-11-15: Gamma projects decode marked images into a linear-light
copy instead, top section): Player Color Space must be Linear for [Linear] resources. This is a real
Unity constraint: in a Gamma scratch Sample, explicit GraphicsFormat SRGB Texture2D and Cubemap
constructors returned UNorm, and marked gray 128 sampled as 128 rather than linear 55. The replay
refuses marked images/arrays/cubes/internal targets and reports once, naming Project Settings > Player >
Other Settings > Color Space > Linear. It never changes the project's setting, and unmarked Gamma resources
continue normally. Refused internal targets and their faces/layers cannot fall through into camera drawing.
The package README and language/user docs state this prerequisite.

**Mips:** even in a Linear project, Unity Texture.Apply(true) and RenderTexture GenerateMips averaged sRGB
bytes (gray 128 + white 255 produced 191 rather than the correct encoded linear mean 205). The replay therefore
uses Resources/TideSrgbMip.shader to sample the previous level through its sRGB format, draw into a separate
sRGB attachment, and copy the encoded result back. One 2D scratch render texture is retained while its shape
matches; a shape change defers the old one with other renderer resources, and reset releases scratch/material.
Arrays are processed by layer, cubes by face. CPU upload mirrors retain the base as stored; GPU mips are
regenerated after changed uploads. Immutable prepared block chains bypass this operation and keep their levels.
There is no per-pixel CPU conversion in a frame's mip path. On Unity OpenGL Core, texture-owned sampler
state can still be Point from an earlier draw. The mip shader uses that texture's named sampler, reads exact
texel centers and interpolates explicitly there, without changing the earlier draw's state. Other APIs use
one bilinear SampleLevel. R32F, RGBA32F and R32I attachments stay unmipped (R8 to RG16F have a chain, below); private sRGB color 10 retains its chain.

**Actual Unity checks:** hidden/noAudio Unity 6000.6.0f1 scratch Sample, automatic Tide builds disabled.
After merging numeric image work, Linear D3D11, D3D12, Vulkan and OpenGL Core ComputeReplayTests each
pass 18/18 applicable (Gamma-only witness skipped): known marked/unmarked point and midpoint pixels; GPU
mip bytes for 2D, both array layers and all six cube faces; sRGB block formats and retained authored levels;
production linear_texture_wgsl replay converting soft-alpha premultiplied target pixels while retaining the
original byte-for-byte, including the private target's mip chain; all existing and incoming numeric/compute/
image/order cases. ShaderTests passes 1/1 on each API, explicitly compiling every pass of TideSrgbMip, all
numeric shader passes, the production-copy fixture and existing renderer/game shaders with no warnings.
The source of 00000000c0de0015.shader is production linear_texture_wgsl from linear_wgsl.h, translated by
this checkout's tide_wgsl_translate. Integrated Gamma D3D11 passes 16/16 applicable, including actionable
refusal and unchanged unmarked/numeric behavior; the three supported-only sRGB witnesses are skipped.
Metal and a Unity WebGPU player require other machines/build environments and were not run here.

## Numeric images and outputs (2026-11-14)

Numeric attachment formats, typed nearest reads and the three portable numeric storage formats now replay
through the existing target, pipeline, image and volume packets. No recorder opcode or embed ABI bump is
needed. HLSL retains the WGSL numeric-output mask and bypasses the Color conversion for a numeric float4 at
location zero. ShaderLab uses literal `Blend i One Zero` for each numeric output; the other outputs retain
Tide's selected blend. R8/RG8/R16F/RG16F single-sample targets have a mip chain since the blended typed reads (render-targets.md, top; replayed
in an editor since: the top section here); R32F/RGBA32F/R32I have none, and their typed reads use the base level.
R8/RG8/R16F/RG16F retain mean MSAA resolve; R32F/RGBA32F/R32I remain single-sample.

For numeric shaders, ShaderLab has a second pass with literal `ColorMask 0 i` for every attachment. Replay
selects it for depth-only draws, including the renderer's reset-depth-without-color clear. A dynamic global
ColorMask left later MRT attachments writable on Unity Vulkan; per-target dynamic ColorMask is not valid
ShaderLab. Two passes preserve the fragment's discards and depth while keeping every numeric image intact.
The ShaderTests fixture now compiles every pass. Games without numeric outputs still produce the earlier
single-pass shader text.

Missing typed numeric images bind all-zero float or integer resources of the correct dimension. Integer zero
textures are render textures, initialized by clear or the existing integer volume kernel, because Unity's
filterable Sample capability check rejects an integer Texture2D on D3D11 even though integer loads work.
Numeric volume allocation checks LoadStore instead. These are nearest integer reads on every replay API.

ComputeReplayTests pins exact scalar/pair/vector/signed outputs in every new attachment format and RGBA16F,
a free fourth channel, replacement under Alpha, mixed float/integer clears past one with clipping, depth-only
preservation, and R32F/RGBA32F/R32I storage volumes. Integer readback uses an integer textureLoad kernel and
the existing buffer-read callback: OpenGL Core does not expose raw integer texture readback through Unity's
AsyncGPUReadback texture conversion. The kernel checks every integer word without converting it to float.
The eleven checked-in numeric WGSL fixtures pass spirv-val for Vulkan 1.3 and FXC SM5 `/WX`.

Validation: the complete 14 ComputeReplayTests, a focused mixed Color/float4/int MRT blend test, and the
shader compilation EditMode test pass on Windows
D3D11, D3D12, Vulkan and OpenGL Core, Unity 6000.6.0f1, hidden batch mode with no audio, using a scratch Sample
whose package points at this checkout. The shader compilation run also includes all sixteen actual renderer
clear shader variants. Metal and a Unity WebGPU player require machines/build environments unavailable here.

## Depth-write descriptor flag: ABI 23 (2026-11-14)

The follow-up writes_depth flag keeps explicit Draw.Bounds from re-enabling geometric occlusion testing for
frags that replace depth. Generated C and the renderer rebuild together; TideLibrary.Abi and embed.h are 23.
No recorder opcode, shader text or replay behavior changed. Details and focused tests: shaders.md.

## Pixel readback kernels for every image (2026-11-14)

`await target.Read<T>(image)` and `ReadDepth()` (render-targets.md, Readback of every image) add ten renderer
kernels beside ReadTargetPixels: number images by `textureLoad` (float, float2, float4, int), depth, a cube's face,
an array's layer and its depth, and a face's and a layer's numbers. They record as the first did: a compute pipeline, a sampled image, the pools and
a buffer read, so there is no new packet and no ABI change. `tide unity` writes a compute asset for each from
`platform/src/read_pixels_wgsl.h`, which is now generated (`compiler/tests/read_target_builtin.mjs --write`) and
compared byte for byte with what tidec writes: the first copy was only checked as a substring and had gone stale
when kernels gained the reversed-depth preamble. Their HLSL compiles with fxc as cs_5_0 under TideWarnings
(`tide_hlsl_shaders_valid`), and `unity_shaders_written` counts 17 computes. Run in Unity since, with replay
tests and the native fixture: the top section, Read kernels, number images and [Linear] run in Unity.

## Pixel readback kernel (2026-11-14)

Embed ABI 22 extends the host-shared read table, with no new GPU command. The engine's pixel-read kernel uses
ordinary sampled images, compute storage and asynchronous buffer reads. CLI shader output includes its Unity
compute asset; the copied WGSL is checked against the Tide source by tidec_read_target_builtin. The Sample's
PixelReadKernelReturnsStraightHdrThroughBufferReads replays an RGBA16f target draw, samples it with the actual
kernel and checks every float word returned by the existing buffer-read callback. ComputeReplayTests pass
11/11 each on Windows D3D11, D3D12, Vulkan and OpenGL Core, including after reversed-depth integration
(6000.6.0f1, hidden batch, no audio). Metal needs a Mac.

## Reversed-depth replay and float attachments (2026-11-14)

The replay prefers D32_SFloat_S8_UInt attachments, with D24 where the device cannot render D32. Unity retains
its native depth convention and ShaderLab comparison/clear mapping. HLSL recognizes the new reversed WGSL
source convention, so vertex output, fragment depth, reads, texture loads and comparison references do not
reverse twice. Sample-zero resolves remain raw copies. Existing forward hand-written fixtures still translate
with their original convention. Regenerated game/built-in shaders are keyed from their new SPIR-V as before.

Depth storage adds no replay packet or embed ABI layout change. The merged DepthBias packet uses ABI 21. Bit 31 of tide_embed_render_formats now reports
float depth storage for the recorder's 8-versus-4-byte memory estimate; existing compression bits retain their
meaning. embed_render_counts_the_hosts_depth_format pins that accounting selection and restores defaults.

Validation uses a scratch Sample, this checkout's CLI and MinGW platform/runtime, hidden Unity 6000.6.0f1 batch
mode with noAudio. All ten ComputeReplayTests pass on D3D11, D3D12, Vulkan and OpenGL Core. Generated shader
compilation and generated-doc XML EditMode checks pass. An encountered pre-existing Unity analyzer warning in
TypeBoundaryTests was fixed using Assembly.GetLoadedAssemblyPath rather than Assembly.Location.
The broader PlayMode attempt passed 31/33; the live-build case passes when rerun with this checkout's running
build service. ScaleTests still reports two whole-frame allocations (showing itself allocates zero); this is
outside the depth replay witnesses and no allocation assertion was loosened. Metal remains external coverage.
See depth-precision-production.md for the complete native/web validation and measurement.

## Integral handed fields and provoking corners (2026-11-14)

Generated HLSL carries integer varyings with nointerpolation; bool handed fields
use an integer wire representation and enums i32. No C# or replay packet changes.
Direct3D 11/12 and Vulkan take the first corner, as Metal's flat interpolation
does. OpenGL Core uses the last by default: the permitted exception in the spec.
The replay's CommandBuffer.DrawProcedural path exposes no provoking-vertex switch;
it does not issue glProvokingVertex or install a native graphics plugin to do so.
Do not silently rotate triangle indices: that would change which corner is first
for the other APIs. For portable per-triangle data in this one exception, assign
the same integral value to all three corners.

References: [OpenGL provoking vertex](https://wikis.khronos.org/opengl/Provoking_Vertex),
[Apple's render command guide](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html).
This is a source/API convention audit; no new Unity graphics replay run is claimed. (Run since, 2026-11-15:
`FlatIntsComeFromTheFirstCorner` passes on the four APIs, OpenGL Core taking the last corner; top section.)
Metal runtime validation requires a Mac.

## Triangle side (2026-11-14)

`this.front` uses the translator's existing front_facing / SV_IsFrontFace path;
`tidehl_facing` corrects projection flips. The generated vertex stage carries the
model mirror sign; the renderer supplies camera mirroring in screen.z's sign.
No replay packet change. Runtime replay on Unity graphics APIs is not yet verified
for this member; FXC validates the generated HLSL on this machine. Corrected 2026-11-15: run, it was the wrong way
round in the camera's own pass and in a target's on Direct3D and Vulkan; fixed (top section).

# Unity as a front end

## Compute images and volumes: embed ABI 20 (2026-11-14)

The owner's image writes and `Texture3D` decision (2026-11-13, "these make sense, lets do all of these") now has a
recorder and Unity replay path. Details here are Provisional; the public rules are in the spec's Views and drawing
and Compute on the GPU sections.

- **Packets.** `VOLUME` (id, `tide_gpu_format`, width, height, depth), `VOLUME_WRITE` (id, x/y/z, width/height/depth,
  then the box's rows and consecutive slices, padded to four bytes), and `PIPELINE_IMAGES` (pipeline id, writable
  image-slot bits, image count, each image's storage format) follow `ARRAY_BLOCKS`. The last packet is emitted only
  for a pipeline that writes images. Images stay at their ordinary `IMAGE` slots, HLSL `_TideTexture<i>`; those
  marked writable are storage images with no sampler. Existing packets keep their shape. `TIDE_EMBED_ABI` and
  `TideLibrary.Abi` are 20; hosts and packages rebuild together.
- **Volumes.** A GPU-written volume is a `RenderTexture` of `Tex3D`, `enableRandomWrite`, without mipmaps. A
  half-float grid uses the same internal box-upload path where Unity supports stores to R16F; that avoids sending
  the entire volume for every changed box. A device without that internal store capability uses a `Texture3D`
  with a CPU pixel mirror; no public kernel gains a nonportable R16F binding. `TideCompute.compute`'s internal float
  and integer kernels zero GPU volumes and upload just a box from a staging volume, preserving other cells even
  after kernel writes. The recorder checks positive dimensions, its portable 2048 side limit, a 128 MiB byte
  bound, and every box before cutting rows out of the full source volume. Unity checks its actual 3D limit and
  bounded byte arithmetic before allocating; a volume that cannot be made gives no image and says so once.
- **Order, including aliases.** Offscreen target passes now execute once while `Prepare` reads the frame, in the
  same `work` command buffer as compute, uploads, mipmaps and depth resolves. A kernel therefore sees target draws
  before it in this frame. Camera-own draws remain deferred, preserving Unity's scene depth. A write after such
  a draw sampled an image makes a new image and copies its contents in work order; the draw keeps the old image.
  Renaming updates every alias of the actual texture and the targets attaching it, including ordinary, resolved
  and array depth: pending camera reads retain their earlier depth too. Cube faces and array layers snapshot
  their owner's image, since their targets deliberately have no color image id of their own. This applies to kernel writes
  and later target passes. A dispatch whose writable and sampled slots alias, or whose actual format does not
  match its storage declaration, is left out. A second camera never repeats the offscreen writes or compute.
  Chosen over dispatching with each camera (which repeats writes) or retaining last-frame target reads (which
  violates recording order). GPU volumes go with the recorder's other objects at a reset or reload.
- **Formats.** Single-sample target images of RGBA8, RGBA16F, R32F, RGBA32F and R32I can be bound for storage;
  the other new attachment formats stay ordinary render targets. The format check uses their actual catalogue
  values, separately from the pixel-upload format numbers.
- **Fixtures.** `embed_render_volume_written_in_parts` pins box strides across rows and slices and refuses
  negative/overflowing extents and enormous volumes; `embed_render_pipeline_writes_images` pins the extension
  packet. Both pass in the native sanitize `tide_platform_embed`. The Sample's `ComputeReplayTests` now includes
  byte-exact target draw/kernel/draw/target draw ordering, kernel-filled volume sampling between cells on each
  axis, a GPU volume's partial upload with all other cells checked exactly, and the half-float grid-volume path.
  It also checks point and linear sampling together, repeat and mirror on each axis of the same volume, HDR
  RGBA16F values written without a camera, and a pending camera draw's target depth before a later pass changes it.
  Filtered half-byte results allow one byte of rounding; the color ordering, wrapping, point and uploaded-cell
  witnesses are exact. A final baseline fixture also pins earlier/later camera reads around cube-face and array-layer
  writes, and that an array's owned depth is released with the renderer (the reset path now mirrors texture-free's
  ownership cleanup). The four new shaders are translated from the WGSL text fixtures beside them; SPIR-V passes
  `spirv-val --target-env vulkan1.3`, and the kernel, draw stages and four internal volume kernels compile with
  FXC SM5 `/WX` (the draw uses the existing `TideWarnings.hlsl`, as the ordinary shader harness does).
- **Unity validation:** 6000.6.0f1, Windows, hidden batch mode with `-noAudio`, a scratch project pointing at this
  checkout's package, automatic Tide builds disabled. The nine compute/image/volume/depth cases pass on Direct3D
  11, Direct3D 12, Vulkan and OpenGL Core (focused follow-ups cover cases added after each initial run). The tenth
  cube/array ordering and cleanup case passes on Direct3D 11. The package and fixtures compile without C# or
  shader warnings. The mixed-filter/wrap volume case first failed on OpenGL Core: the new 3D HLSL helper now uses
  the texture's own sampler or emulates point/eight-tap filtering and per-axis wrap, as the existing 2D path does.
  Not run here: Unity's Metal backend or a Unity WebGPU player. Native/WebGPU backend validation is recorded in
  compute.md and textures.md with the combined implementation.


## A game laid out otherwise in a player: its C# for both (2026-11-10)

The editor's game is built with DEBUG and a player's without (`--release`), so a game whose data differs with it (a
component's field inside `#if DEBUG`) has another layout, and another hash, in a player. Its C# in
`Packages/com.tide.game` was the editor's game's alone, so a player's scripts read nothing, silently; the hash
check of the note below then made that a build error that nothing could fix. The owner: "can we fix that debug
thing?" Built (mine, Provisional):

- **tide writes the C# of both games** where they differ, each file's text for the editor inside
  `#if UNITY_EDITOR` and the player's after `#else` (a file only one has, inside `#if UNITY_EDITOR` or
  `#if !UNITY_EDITOR`), so Unity compiles the right one for each, as it does a script's own `#if UNITY_EDITOR`
  fields. A file that's the same for both is written as before, so only the types that differ are doubled.
  Scripts' metas and `components.json` are the editor's: authoring is the editor's game's, and the bake leaves a
  field the player's game hasn't out, as for any field a component no longer has. A script that reads a
  debug-only field doesn't compile for the player, as with Unity's own.
- **How tide knows** (`find_player_schema` in `build.c`, from `write_unity_schema`, on each of the editor's
  builds): nothing more where no file the game compiled, nor `tide.packages`, says `DEBUG`. Else the game is
  generated again without DEBUG (tidec alone, into `<-o>/player/gen`) and the two hashes compared; only where
  they differ is that game built (`<-o>/player/game-1`, never run) for its schema. Then `write_unity_package`
  writes each game's C# into `Library/Tide.editor-code` and `Library/Tide.player-code` (as deep in the project as
  the package, which is how the writer finds the project) and `merge_code` puts both into the package, taking
  away component scripts neither has. A game that doesn't build without DEBUG gets a warning and the editor's C#
  alone: its player build says the errors.
- **A player's build writes no C#** (`write_unity_package` returns for `--release`): it had been writing the
  release game's alone over the project's on desktop. The package's check (below) compares `game-hash` with the
  last `Hash = "` in `TideGame.cs`, which is the player's where there are two.
- **Checked** on a scratch Sample with `int debugTurns` on `Spinner` inside `#if DEBUG`: `Spinner.cs` and
  `TideGame.cs` hold both (Value sizes 12 and 8, two hashes), `Ball.cs` is as before; the editor's BounceTests
  and ScaleTests pass 17/17 (D3D11); a player builds and its smoke passes with "drew yes", which reads `Ball`'s
  live color, so the player compiled its own half. tide's run of it under ASan: clean (the first version freed a
  string builder's arena text, which the debug heap caught). Not tested: a component only one game has, and the
  WebGL and Android players of such a game (their check is the same hash).
- Cost: a game that mentions DEBUG pays one more tidec run an editor build; one whose layouts differ, a second
  build of the game.

## A player build while tide isn't ready (2026-11-10)

The owner asked how a player build behaves while tide is downloading, building or not installed, "making sure we
don't run into basic unity usage errors". A player build never uses the editor's background build: it finds or
installs tide (`TideToolchain.Find(install, wait)`), runs `tide unity --once --release` itself into
`Temp/TideBuild` and waits. Three things were wrong with that, fixed (mine, Provisional):

- **Nothing showed while tide built for the player.** `TideBuildProcessor` follows tide's lines and shows its
  step and `[n/m]` as a progress bar (not in batch mode). Its progress lines no longer fill a failure's message
  or a warning's: what it has to say alone is kept.
- **An install already on its way was downloaded a second time.** `TideVersions.Ensure(wait)` follows the
  install `Begin` started (`following`, its `Watch` step, which the editor's updates can't call while a build
  waits) to its end, with its bar.
- **The game's C# could be of another build than the player's game.** The C# in `Packages/com.tide.game` is
  compiled into the player and reads the match by the layout it was written for; where tide in the editor hadn't
  built the last change yet (or source control's C# was older than the game), the player's scripts read nothing,
  with nothing said. Every `tide unity` player build (desktop, `--web`, `--android`) now writes
  `<-o>/game-hash` (`write_game_hash`: `tide_game.hash` from the generated `game.c`), and the build fails where
  it isn't the hash `TideGame.cs` had as the build began, saying to let the editor's build and Unity's compile
  finish and build again (and that a game whose data differs between debug and release can't match: the
  editor's C# is the debug game's). On desktop tide has by then written the right C#, so the next build passes.
- Checked in batch mode on a scratch Sample: a player builds; with `TideGame.cs`'s hash changed by hand the
  build fails with that message; the build after it succeeds. VersionTests 12 pass (one ignored, and the
  launch-argument one failing under `-tide-command` as before). Not seen: the two progress bars (no window), and
  following an install in flight (needs a download under way as a build starts).

## An Android player's match on translated ARM (2026-11-19)

The owner has no phone that can be plugged in, and asked whether one could be emulated. It can, for the CPU: Google's
`google_apis` x86-64 images run ARM64 apps through `libndk_translation` (`ro.product.cpu.abilist=x86_64,arm64-v8a`),
which the `default` image of the `tide` AVD doesn't.

- **The device:** `x86_64-34_r14.zip` (Android 14, `sys-img/google_apis`, SHA-1 checked against `sys-img2-3.xml`)
  unpacked to `%LOCALAPPDATA%\Android\Sdk\system-images\android-34\google_apis\x86_64`, and an AVD `tide-arm`
  written by hand beside `tide` (its `config.ini` with that image and `tag.id=google_apis`). Booted with
  `-no-window -no-audio -no-boot-anim -no-snapshot-save`.
- **Ran:** `SampleSetup.BuildAndroid` (IL2CPP, ARM64, this checkout's tide through `-tide-command`), installed, and
  started with `am start -n dev.tide.unity.bounce/com.unity3d.player.UnityPlayerGameActivity -e unity -tide-smoke`
  on `-gpu host -feature -Vulkan`: `Tide smoke: running True, ball (-2.25, 0.50, 0.00), mine True, pillar turned
  True, drew not with Tide's renderer (OpenGLES3, ...): FAIL`. So `libtide_game.so` loads by name from the app, the
  pack inside it is read, the match runs over loopback in Unity's process and the C# reads it, all as ARM64 code. The
  FAIL is the drawing alone: Unity took OpenGL ES there, which Tide's renderer leaves out by design.
- **Not shown: Tide's renderer on Unity's Android Vulkan.** With `-gpu host` (Vulkan 1.3; `cmd gpu vkjson` answers)
  the emulator's own process dies (exit 139) as Unity makes its second `VkInstance`, twice of twice; nothing of the
  app's is at fault that the log shows. `-gpu swiftshader_indirect` gives the app no Vulkan and OpenGL ES 3.0 only,
  which Unity refuses, and an Android player doesn't take `-nographics`. Translated ARM also says nothing of a real
  phone's float results or GPU driver. A phone is still what shows these.
- **adb:** Unity's editors run their own adb (36.0.0), the SDK's is 37.0.1, and each kills the other's server, so
  with any Unity editor open on the machine adb commands fail midway. `ANDROID_ADB_SERVER_PORT=5038` gives a run
  a server of its own.
- **Found:** the Sample's committed `Packages/com.tide.game` was of another build than dev's tide makes (its schema
  hash), which the player build refuses with its message; `tide unity Assets -o Library/Tide --assets Assets --once`
  wrote it again. Not committed from here, as the threads changing the Sample each write it again.

## Android players, and IL2CPP run (2026-11-10)

The owner, on which players come first: "just keep going with your recommendations" (Android, then IL2CPP on
desktop, then iOS). Unity's Android and Windows IL2CPP modules were installed for 6000.6.0f1 on this machine for it
(the `unity` CLI's `install-modules`). All picks mine (Provisional).

- **`tide unity --android -o <path>`** (`tide_unity_android` in `compiler/cli/build.c`, once, as `--web`): for each
  of Android's CPUs, `<path>/android/<abi>/libtide_game.so`, **one library**: the game, the engine and
  `tide/embed.h`'s host linked together, as a page's program is (one engine copy, so no table handed between two),
  with the libraries the game's own C links beside it, and `<path>/unity.json`. `link_android` takes `unity`: the
  embed's functions kept (`-Wl,-u`), no `ANativeActivity_onCreate` and no `--wrap=exit` (the process, its activity
  and its exit are Unity's). The platform layer's `activity.c` comes with its window functions and names a program's
  `main`, so the generated `library.c` has a hidden one nothing calls.
- **The pack is inside the library**: `library.c` has it by `.incbin` (`tide_host_library_pack(&size)`), read where
  the system maps the library, so there's no file to find inside an APK (an Android player's StreamingAssets aren't
  files) and no JNI. The pack's stamp is in the text, so a changed pack is compiled in again.
- **The package**: `TideLibrary.OpenWhole` (Android: `dlopen("libtide_game.so")` by its name, the embed's functions,
  `tide_host_library` and the pack all from it; `NativeLoader` has Android's `libdl`). The runner gives the embed
  `--data-dir Application.persistentDataPath` there (without an activity the platform layer has no folder of its
  own to offer). `TideBuildProcessor` takes `BuildTarget.Android` (`tide unity --android --once --release`) and
  refuses a player for a CPU tide doesn't build for, saying which setting. `Editor/Android/TideAndroidBuild.cs`
  (`IPostGenerateGradleAndroidProject`, in `Tide.Editor.Android`, an assembly with the define constraint
  `UNITY_ANDROID`: Unity's Android editor code exists only with its module) copies each CPU's libraries into the
  Gradle project's `src/main/jniLibs/<abi>` and adds `android.permission.INTERNET` to its manifest where Unity
  didn't (it adds it for C# that uses the network; Tide's is native).
- **A use after free, fixed**: the runner freed the embed's arguments right after `tide_embed_create`, which keeps
  them (`tide_run_settings.argv`, and `--data-dir`'s value as the save folder's path). They're kept until the
  embed is destroyed (`TideRunner.FreeCreated`).
- **Unity 6000.6 builds no x86-64 Android player** ("Target architecture x86_64 is no longer supported and has been
  removed"), so its players are ARM64 alone. The package still copies x86-64 for the Unity versions that have it
  (by the enum's number: its name is obsolete in 6000.6, and warnings are errors).
- **Checked:** `tide unity --android --once --release` builds the Sample's library for both CPUs (NDK r30), with
  `tide_embed_*`, `tide_host_library`, `tide_host_library_shape` and `tide_host_library_pack` exported.
  `SampleSetup.BuildAndroid` (IL2CPP, ARM64) builds an APK with `lib/arm64-v8a/libtide_game.so` in it, with no C#
  warning. **Not run:** this machine's emulator is x86-64 with no ARM translation and there's no phone, so no Unity
  Android player has started a match. What only a device shows: the library loading by name from the app, the
  network from inside Unity's process, Tide's renderer on a phone's Vulkan.
- **IL2CPP runs.** `SampleSetup.BuildPlayer -tideIl2cpp` builds a Windows IL2CPP player (GameAssembly.dll); with
  `-batchmode -tide-smoke` it passes with no GPU and on Direct3D 12 ("drew yes"): function pointers, the packet's
  unsafe reads, the generated C# and the renderer's native uploads, compiled ahead of time.
- **Left:** iOS (a static library linked with `__Internal`, and a Mac to build on; built 2026-11-19: iOS players,
  top), another desktop system than
  the editor's, consoles. An Android App Bundle whose assets are split out is untouched by this: the pack is in the
  library, which counts against the base module's size.

## Made for production: the frame's cost, the Console, another tide's builds (2026-11-10)

The owner: "overhaul the unity integration. Make sure we can use it for production games." An audit of the package
found what a shipped game would feel first in the runner's frame, in what a developer can't see, and in one crash.
What was built is below; what's left is at the end, with the platforms a decision of the owner's. Every pick here is
mine (Provisional).

**A frame makes nothing new, and costs by what's shown.** Measured with the Sample's new `Crowd` command (balls
nobody rolls) and `ScaleTests`, 1,003 entities shown, Unity 6000.6.0f1 editor (Mono), Direct3D 11, the old package
(HEAD's, with HEAD's generator) against the new on one machine:

| | old | new |
|---|---|---|
| Showing the packet in Unity (`Show`: GameObjects, Transforms, components, singletons, events) | 0.72 ms | 0.25 ms |
| Objects made a frame by that | 34 | 0 |
| Objects made a frame by the runner's whole frame | 43 (about 68 KB) | 0 |

What made them, and what replaced it:

- **The packet's sections** were found by walking every section for each row read (`TidePacket.Find`): a table by
  kind and type now, made as the packet is read, and `Rows` gives a type's rows at once (count, stride, the rows'
  entities, the first value).
- **Names looked up each frame**: Tide's `Transform` and its three fields by name for every entity placed, each
  component's and singleton's class by its Tide name. `TideRunner.Bind` finds them once a schema (and again when
  `TideTypes.Version` moves): `liveComponents`, `liveSingletons`, `liveEvents`, the Transform's offsets.
- **A GameObject's Tide components** were looked for with `GetComponent` per component and `GetComponents` per
  entity, every frame. `TideEntity` keeps them (`CollectLive` as it starts showing an entity, `Live(kind)`,
  `DropStale`), and only a GameObject holding more than its entity has is looked through.
- **A Transform is written only where it isn't there already** (`GetLocalPositionAndRotation`, compared exactly):
  an entity that stays put leaves its Transform, and what Unity keeps by it, untouched. What a script moved is still
  put back, as before.
- **Lists, maps, sets, grids and text were made again every frame** for every component and singleton holding one
  (a singleton's grid copied into new arrays 60 times a second). The generated C# passes the field itself to
  `TideRead` (`put_conv`'s `into`): `List`, `Set`, `Map`, `Grid2/3` and `Array` fill what the field has, a grid
  keeps each chunk's array (cells laid out as C# has them are copied whole), `Texts` keeps each line of a
  `List<string>` that's as it was, `TideText.Read(packet, had)` gives back the same string while the bytes are
  that string's (`TidePacket.Text(handle, had)`, compared without decoding where it's ASCII), and a struct's
  `From(v, packet, runner, had)` keeps its own. So what a script reads there is this frame's, and a list it keeps
  changes with the match. An event is still an object of its own.
- **Events** went through `Activator.CreateInstance` and `Delegate.DynamicInvoke`. `TideTypes.Register` takes a
  maker (`() => new T()`, which tide writes for singletons and events), handlers are typed lists
  (`TideHearing<T>`, copied as one's added or taken away, so a handler can do either while it's told), and an
  event nothing hears isn't made.
- **Commands** allocated a writer, an array a text and one for the bytes: the runner keeps one
  `TideCommandWriter`. `Room` and `Message` are the same string until their bytes change.
- **Input**: the Input Manager path asked Unity for the joysticks' names every frame (a new array each time; once a
  second now), the Input System path made an array of the mouse's buttons and looked the pad's up by name.
- **TideAudio** looked through the whole scene for an AudioListener every frame: the one found is kept while it's
  on, and with none the scene is looked through four times a second.
- **TideRender**: `GraphicsBuffer.SetData` of a managed array makes garbage at every call, so what's written each
  frame is native (`NativeArray`: the uniforms' bytes, a buffer's write, an index view, a dispatch's groups); the
  uniforms' array is its buffer's size and grows with it; `Collect` had two closures a frame; a draw's storage
  buffers are in its recording's list; the resolved depth's image is kept; and a buffer written after a draw of the
  frame read it took a new GPU buffer every frame, where `Renamed` now takes one a rename let go of (`spares`, at
  most 64, released after 600 frames unused).
- **A physics package's shapes** (`TideParts.BodyOf`, asked of every shape each frame) made an array of the
  GameObject's components, and `Points` a new list.
- `Tide.Frame`, `Tide.Render` and `Tide.Show` are ProfilerMarkers.

**The game's Debug.Log is in Unity's Console (embed ABI 19).** It went to the process's stderr, which nobody sees
in the editor, as did what the host said when a build didn't load ("see the Console for what tide said" pointed at
nothing). The approved phase 1 draft had a `log` callback that was never built. Built as a call the other program
makes, not a callback into it (no native thread calls into Mono or IL2CPP, nothing to unhook at a domain reload)
and without taking the process's stderr (the standard host's `log.c` does that for a window's `Player.log`, which
inside Unity would take Unity's own):

- `tide_debug_log_to(heard)` (`tide/debug.h`): where `tide_debug_log` also goes. Each library has its own engine
  copy, so `tide_host_game.debug_log_to` (filled by `tide/run.h`) lets the host set a loaded game's
  (`reload_library.inc`'s `library_heard`, and a linked game's in `tide_embed_create`).
- `tide_embed_log(e, &size)`: records since the last call, each a kind and a length (`uint32_t`s) and the text, to
  4 bytes; `TIDE_EMBED_LOG_GAME` for Debug.Log, `TIDE_EMBED_LOG_PROBLEM` for the host's (a library that didn't
  load, by `library_say`; an argument left out; no build in the folder). The process's, so `e` may be NULL: that's
  how the runner shows why `tide_embed_create` gave none. Said on any thread (a spin lock, `atomic_flag`); more
  than 256 KiB between two calls is left out with a record saying so. A web program keeps none (its page's console
  has it), and `TideLibrary.Log` gives nothing there.
- `TideRunner.Said`, after each frame and after `Create`: `Debug.LogFormat` with `LogOption.NoStacktrace`, a log
  for the game's and an error for the host's. Assertions aren't in it: a failed one ends the program.
- `TIDE_EMBED_ABI` and `TideLibrary.Abi` are 19; `tide_embed_log` is in `embed_exports` (whose `.def` text has
  room for 2,048 bytes now: the names had come to within a line of 1,024).

**A build another tide made isn't loaded.** The crash found on 2026-10-25 (Tide's drawing after a hot reload,
"Found on the way") is closed: `tide/host.h`'s `TIDE_HOST_SHAPE` is the sizes of the tables a host reads a game
through (`tide_host_game`, `tide_game`, `tide_io`, `tide_layout`), each library exports it as
`tide_host_library_shape` (`build.c`'s two `library.c` texts), and `reload_library.inc`'s `load` refuses one that
says another number, or has none, saying it "was built by another tide than the one running it" (an error in the
Console, through the log above). The game goes on with the build it has. It follows the tables by itself (adding
`debug_log_to` moved it), and doesn't see members reordered at one size. A player's game library, linked by the
package, isn't checked: both come from one build.

**Run In Background** (`TideRunner.runInBackground`, on): sets `Application.runInBackground`, as a Tide window
keeps running behind another. Unity's default stops a desktop player that isn't in front, and a match that machine
runs with it.

**The Sample**: `Crowd { count }` and `Extras.crowd`/`lines`, `Say` logging what's said; `ScaleTests` (PlayMode:
`TIDE_UNITY_CROWD` balls, 300 by default; frames that make nothing, for showing and for the whole frame; the same
list, set, grid, map and string as the frame before; what changes read as it changes); `BounceTests` expects the
Console's line; `SampleSetup.BuildPlayer` builds this machine's player, and `PlayerSmoke` reads its frame from a
texture in batch mode (it hung there: no screen to read). Its `com.tide.game` is written again by this tide.

**Tests and runs** (Windows, Unity 6000.6.0f1, batch, no window, `TIDE_HIDDEN`, a scratch copy of the Sample with
this checkout's package and a tide installed from this checkout's mingw-release and debug trees):
PlayMode 27/27 on Direct3D 11 (a GPU), and the drawing, compute-replay and scale tests on Vulkan, OpenGL Core and
Direct3D 12 (9/9 each before the Console's log, Vulkan 8/8 after it); EditMode 44 of 47, 2 ignored as before and
`VersionTests.TheActualLaunchArgumentSelectsTheDevelopmentCompilerOnlyWhenPresent` failing only because these runs
pass `-tide-command` (it checks a launch without one). A Windows player (Mono) built by `SampleSetup.BuildPlayer`
and run with `-batchmode -tide-smoke`: PASS with no GPU, and "drew yes" on Direct3D 12 and 11, which is the
renderer's native uploads in a player.
Native debug, and again under Windows ASan (`sanitize`): `tide_platform_embed` 23/23 with `embed_log_is_the_other_programs` (a Debug.Log record once, a bad
argument's and a missing folder's problems, asked with and without an embed), `tide_platform_reload` with a
library of another shape published while the game runs (refused, the build running goes on),
`tide_platform_reload_reads`, `tide_platform_travel`, `tide_platform_embed_shards`, `tide_cli_tests` (the
Sample's package as tide writes it), `tide_cli_unity_physics`, `tide_tests`. Web debug: `embed.c` builds (its log
kept out) and `tide_tests` passes. The docs build.

**Not done, and why:**

- **Players for other systems** (Android came the same day: Android players, above)**.** A player builds for
  the editor's own desktop system and WebGL alone (`TideBuildProcessor`): tide builds the game's library with this machine's toolchain. Android, iOS, consoles and
  another desktop system each need tide to build the embed and the game for that target (iOS and consoles as
  static libraries linked with `__Internal`, IL2CPP's flags as "IL2CPP and Tide's C" below) and a device or
  module to run them on: this machine's Unity has Windows (Mono) and WebGL modules only. It's the largest thing
  between the package and a shipped game, and which targets come first is the owner's.
- **IL2CPP** players weren't run (no module here; run the same day, above). The package calls through function pointers and takes no
  native callback, which IL2CPP has.
- **A Unity web player**: Tide's renderer needs WebGPU there since the GL backends went, and no WebGPU player was
  run.
- **Not seen in a window** (agent runs open none): the Console's lines themselves (read back by `LogAssert`), the
  Profiler's markers.
- The view packet still carries every component of every shown entity each frame, read or not (decision 4 of
  phase 1). At 1,000 entities the game's own frame was 1.3 ms of the runner's 1.6 in a debug build; a packet of
  what C# asked for is a design of its own. (Built 2026-11-19: The packet carries what Unity reads, top.)
- A physics shape still finds its body by reflection each frame (`TideParts.BodyOf`): no garbage now, but a
  generated accessor would be cheaper. (Built 2026-11-19, the same section.)

## Play waits for the install and the first build (2026-11-10)

The owner updated the package (`#dev`), said yes to installing its tide, and
pressed Play while it downloaded: the pre-Play host check loaded the old
`tide_embed.dll` and said the ABI error (17 against 18). Owner: "i would
expect it to maybe show a progress bar waiting for the download before
actually entering play mode".

`TideBuildService.RefusePlayWithErrors` now holds Play (`held`, cancelled and
entered again, as `Sync` does) while `PlayWaitsFor` names something:

- the package's tide being installed (`TideVersions.Installing`, with
  `InstallProgress`/`InstallStatus`, which `Begin` keeps beside its status-bar
  task);
- tide's first build since the service started it (`starting`: from `Start`
  to `tide: building the game for Unity ...`, tide ending or `Stop`), which is
  the build that writes the embed's library; `loading` covers the moment
  between a script reload and the service's delayed `Start`;
- once held, Unity compiling the C# that build wrote.

`Held` (each `Drain`) shows `EditorUtility.DisplayCancelableProgressBar`
("Tide: Play starts once the game is built", the install's status and
fraction over 0 to 0.8, then 0.9 for the build and compile); Cancel gives
Play up and the install goes on. When nothing is left it refreshes assets; if
that starts a compile it stays held, else it clears the bar and sets
`isPlaying`, which goes through the hook's usual `Sync`, errors and host
check. `held` is in `SessionState` (`Tide.PlayHeld`), so the script reload the
new C# brings keeps Play waiting through the service's restart. An install
that failed (Console) with no tide running gives Play up rather than checking
the stale host. Batch runs hold nothing, as with `Sync`.

Picks (mine, Provisional): a modal cancelable bar for every hold, also the
short one of Play pressed in tide's first second after a script reload
(rather than a bar for installs alone: one path, always cancelable); Play is
also held for a first build when the host on disk would have loaded (it ran
the last build and swapped before).

Also: `Begin`'s end restarts the build service only with Build while editing
on (Project Settings' Install started building with it off).

Validation: Unity 6000.6.0f1, scratch Sample with this checkout's package
(`file:`), TIDE_HIDDEN/batchmode/nographics/noAudio, automatic builds off:
the package compiles with no C# warnings; VersionTests 13/13 enabled (the
ABI-5 fixture ignored), with the new `PlayWaitsForAnInstallOnItsWay` (a
`file://` release begun with `Begin`: `PlayWaitsFor` names it under 0.9,
then nothing once it's in place), and ConsoleTests 3/3. Not run: the hold
itself, its bar, Cancel and the resume through a script reload, which only an
interactive editor does (batch runs skip it); UnsavedTests need a running
tide (2 fail in that scratch for that alone).

## Logical Entity keys: packet 5 and embed ABI 18 (2026-11-06)

Owner-approved Entity128 changes the C/native command boundary to two uint64
words (`birth`, `lineage`), and the generated embed callback matches it. The web
export/jslib/PInvoke path passes four uint32 halves unchanged; no JavaScript
Number combines a 64-bit identity. The C wrapper reconstructs each word with
uint64 shifts. `TideEntityId` is 16 bytes, two ulongs with full-key equality/hash;
view/scene dictionaries retain that value directly. Packet entity rows are 32 bytes
(key 16, flags/prefab/parent/presentation-born 16); event headers are 40 bytes,
including the complete target key. Both-zero is null; a zero birth alone is not.

The packet builder's former global-index lookup is an exact logical-key hash
map reused between frames. This matters for sharded views: separate worlds can
put distinct keys in private slot 0. Null and NONE registrations are ignored;
registering an existing full key replaces only its place, retaining the existing
selected authoritative/ghost view behavior. Allocation arithmetic widens before
growth; begin clears retained entries and free releases the map. Generated
component/parent/event references use it instead of physical indices.

The checked-in Sample and physics schema fixtures were relaid out for 16-byte
Entity fields, moving later fields/default bytes without changing their values.
Their old schema hash is retained until the next real game build, which writes
its actual new hash. Sample C# was regenerated with this checkout's writer.
Native ASan CLI unity_code 6/6 and packet-map 1/1 pass; generated embed game C
compiles strictly. The checked-in JS fakeinterop test forwards all four high-bit
halves exactly and passes. In build/unity-identity-check, this checkout's Sample
and package compile under Unity 6000.6.0f1, hidden batchmode/nographics/noAudio,
with automatic Tide builds disabled. New EntityPacketTests 2/2 pass: layout/offset,
complete key dictionary identity, null behavior, packet 5/event targets and all
packet truncations. Native/web platform embed validation is recorded with the
coordinated identity change. Historical index/generation packet entries below
are superseded.


## One Unity entity component (2026-11-06)

Owner decision: combine `TideEntityView` into `TideEntity`. `TideEntity` retains
its script GUID and serialized `components`/`references`, and now also exposes
`Runner`, `Entity`, `Mine`, `Showing`, `Has<T>`, `TryGet<T>`, `Get<T>`, `Text`,
and entity event subscriptions. There is no separate view component or `View`
property. Scripts can find an authored entity and subscribe before binding;
`Showing` is false until it represents an entity in the current view packet.
The runner reuses the component on authored objects and ensures it on visual
prefab instances. Runtime-added components do not modify source assets, so
view rules still accept ordinary visual prefabs without authoring entities.
Binding, release, pooling, source identity and event dispatch retain their
existing behavior. The type-binding attribute has its own `TideTypeAttribute.cs`.
Editor gizmos, sample scripts/tests, generated sample documentation and public
CLI/package docs use the combined component. Historical mentions below describe
the earlier split.

Validation: Unity 6000.6.0f1 in a scratch Sample (automatic builds disabled,
TIDE_HIDDEN, batchmode/nographics/noAudio) compiles the package and Sample
without C# warnings and passes EntityBindingTests 2/2 and EntityEditorTests 3/3.
The Sample's checked-in schema was staged into Library/Tide for the inspector
fixtures. The new tests cover pre-binding reads, authored serialization, release
retaining the same component, and subscriptions made before binding. The docs
production build passes. Native/web game execution and player builds were not
run; no native embed, wire, simulation, generated-C or web bridge code changed.

## GPU lists in pools: Unity's replay (2026-11-05)

No packet changed and the embed ABI stays 12 (`notes/compute.md`, GPU lists in pools): the recorder records the same
calls, with fewer storage buffers a pipeline (the two pools and a kernel's run record). `TideRender.cs`'s `MaxStorage`
is 4 (gpu.h's `TIDE_GPU_MAX_STORAGE`), and `Supported` asks for 4 storage buffers in compute and fragment stages and 7
in vertex stages, where it asked for 7 and 10. The recorder has no `storage_most`, so its pools grow to 128 MiB each.
Gap: a kernel writing a pool after a kept draw read it renames the whole pool (`Written`), where it renamed one list's
buffer; the Sample's `ComputeReplayTests` weren't run (their commands are hand-made and unchanged).

## Occlusion culling in Unity (2026-11-05)

`Draw.Occlusion` (drawing.md, Occlusion culling from a depth pyramid) culls only on the GPU, and the recorder never
does (`cull_plan`): a game's draws with it on are recorded and replayed whole, as with it off, the same pixels.
`gpu.h depth_far` (since `depth_now`) is NULL in the recorder; nothing new is recorded and the embed ABI is unchanged.
The renderer's pyramid kernel (`hiz_one`, `hiz_samples`, shaders.h) is in the renderer's shaders `tide unity` writes as
HLSL, though the replay never runs it.

## Unity's OpenGL: Core stays, ES 3 and WebGL go (2026-11-05)

With the GL backends removed (compute is guaranteed on every backend), Tide's
drawing replays in Unity only on graphics APIs that compute. Tide's shaders for
Unity have one SubShader (target 4.5, `exclude_renderers gles3`): the OpenGL ES
3.0/WebGL 2 one, which read vertices from textures (`TIDEHL_TEXTURE_BUFFERS`),
is gone, and so are TideRender's `textureBuffers` path (word textures,
`VertexWords`, the shadow arrays). `TideRender.Supported` keeps the replay's
requirements (compute, indirect arguments, level 4.5, the buffer slots below)
and refuses `OpenGLES3`, so those players and Unity's WebGL export get the
OnGUI path (`TideDraw.cs`) only.

**Unity's OpenGL Core stays supported** (Claude's pick, Provisional). The
owner's rule is about backends that can't compute; Unity's OpenGL Core is 4.5,
which computes, and the replay's `ComputeReplayTests` passed on it (below).
The GL removal had dropped it with the rest of GL, together with the
per-texture samplers it needs (Unity's OpenGL has no samplers apart from
textures); the merge brought those back for Core alone: `TIDEHL_OWN_SAMPLERS`
under `SHADER_API_GLCORE`, `sampler_TideTexture<i>` beside each module
texture, the `own` parameter of the prelude's sampling helpers, and
TideRender's `ownSamplers` (set for `OpenGLCore` only), which sets each
texture's filter and wrap as the draw asks. The Sample's checked-in
`Assets/Tests/Resources/TideShaders` files were written again with
`tide_wgsl_translate --hlsl` (the shader's name set back to its key). Not run
in Unity after the merge: the sampler path is the one the replay was checked
with on OpenGL Core, now under one define instead of four.

## Texture arrays: embed ABI 15 (2026-11-05)

`TIDE_EMBED_RENDER_ARRAY` (id, format, width, height, layers, given, its depth array's id, then each layer's pixels)
and `TIDE_EMBED_RENDER_ARRAY_LAYER` (target, array, layer), after SAMPLER. TideRender's `MakeArray` makes a
`Texture2DArray` (mips made by `Apply(true)`) for an array of pixels, or a `RenderTexture` of `Tex2DArray` with
`volumeDepth` and a depth one beside it (or as it, for depth alone), and `MakeArrayLayer` a target that draws into that
layer of both (`RenderTargetIdentifier(rt, 0, CubemapFace.Unknown, layer)`); freeing the array frees its depth. The HLSL
reads `Texture2DArray<float4>`/`<float>` with `float3(uv, layer)` (notes/shaders.md). `TIDE_EMBED_ABI` and
`TideLibrary.Abi` are 15. Not run in Unity.

ABI 16 (the same day, a `Grid3` as an array): `TIDE_EMBED_RENDER_ARRAY_OF` (id, format, width, height, layers: zeros)
and `TIDE_EMBED_RENDER_ARRAY_WRITE` (id, layer, x, y, width, height, then that part's rows packed, padded to 4 bytes).
TideRender's `MakeArrayOf` makes a `Texture2DArray` of the format (RGBA32 or RHalf, `TextureFormat`) with a mip chain
and keeps its pixels (`Image.layers`); `WriteArray` copies the part into them and sets that layer's
(`SetPixelData(pixels, 0, layer, offset)`), or makes a new array when a draw of this frame already reads it, as
`WriteTexture` does; `Mipmaps` applies a `Texture2DArray`'s. Compressed pack layers reach Unity decoded, through
`ARRAY`. Compiled, not run in Unity.

ABI 17 (the same day, the owner's "why are we stopping with things still open?"):
- **Compressed images.** `tide_embed_render_formats(e, formats)` says which block formats the other program samples
  (1 << 1 BC1, 1 << 3 BC3) and remakes everything (`tide_embed_render_reset`); the recorder
  (`tide_gpu_record_block_formats`) then implements `texture_blocks` and `array_blocks`: `TIDE_EMBED_RENDER_TEXTURE_BLOCKS`
  (id, width, height, format, levels, bytes, the chain) and `_ARRAY_BLOCKS` (id, width, height, layers, format,
  levels, bytes a layer, the chains), for BC where said and for RGBA8 chains always, so a device without BC gets the
  build's mips decoded rather than ones Unity makes. TideRunner says `TideRender.BlockFormats`
  (`SystemInfo.SupportsTextureFormat` DXT1/DXT5) when it starts the renderer; WebGL through `TideWebRenderFormats`
  and the page's `tide_unity_render_formats` (`compiler/cli/build.c`). TideRender's `MakeBlocks` makes a `Texture2D`
  or `Texture2DArray` of `RGBA_DXT1_UNorm`/`RGBA_DXT5_UNorm`/`R8G8B8A8_UNorm` with those levels (`SetPixelData` per
  level, `Apply(false, true)`: no CPU copy). Test: `embed_render_blocks_where_the_other_program_samples_them`.
- **OpenGL Core reading a texture two ways.** Unity's GL keeps sampling on the texture and applies it when the frame
  runs, so the last state set won for every draw. Now TideRender's `Own` sets a texture's filter and wrap at its
  frame's first read only (`Image.own`, kept while `used` is this frame) and passes each texture's own mode in
  `_TideOwn`/`_TideOwnHigh` (4 bits a texture, 15 unknown: white, depth); under `TIDEHL_OWN_SAMPLERS`, `tidehl_mode`
  adds it (bits 4 to 7, bit 8), and `tidehl_sample_grad`/`_level` emulate a read whose mode differs
  (`tidehl_emulates`; a cube compares filters only): texels fetched at their centers with the texture's own sampler,
  which any filter gives exactly, wrapped (`tidehl_wrapped`, clamp/repeat/mirror without integer `%`, which fxc warns
  about) and filtered (1, 4 or 8 taps; trilinear's level from the derivatives, `tidehl_lod_of`). A cube's taps stay on
  the face the direction points at (`tidehl_cube_uv`/`_direction`), so its edges differ from a seamless cube's.
  Reads of a texture a function was given carry no own mode and sample as before. `expect_hlsl.cmake` compiles every
  shader for `SHADER_API_GLCORE` again (dropped with GL's removal), so fxc checks this path.
- Compiled, not run in Unity.

## One image a texture: embed ABI 14 (2026-11-05)

Textures and samplers are counted apart (notes/rendering.md, One image a texture). The recording's PIPELINE packet
carries, after its texture reads, `sampler_count`, each sampler's read and each texture's sampler bits; SAMPLE became
IMAGE (slot, texture: the same op number) and SAMPLER (slot, filter, wrap: a new op after
DRAW_INDEXED_INDIRECT_COUNT). TideRender keeps `sampled` by texture and `sampling` by sampler, packs
`_TideSampling`/`_TideSamplingHigh` by sampler (`Modes`), and sets `_TideUnmipped`, bit i for texture i without its
smaller images, which `tidehl_mode` reads (the blended fallback was TideRender's per slot). OpenGL Core, whose
textures carry their sampling, samples a texture as its first sampler says (`OwnMode`). `TIDE_EMBED_ABI` and
`TideLibrary.Abi` are 14; `ComputeReplayTests`' hand-made pipeline gained its sampler count. Not run in Unity (no
Unity run was made; the Sample's checked-in shaders weren't written again).

## Sixteen textures: embed ABI 13 (2026-11-05)

A pipeline samples 16 textures (notes/rendering.md, Sixteen textures a draw), so TideRender's `MaxTextures` is 16,
with `_TideTexture12` to `_TideTexture15`; textures 8 to 15 take `_TideSamplingHigh`'s bits as 8 to 11 did. Storage
buffer i is at WGSL's binding 33 + i (`hlsl.c`'s `storage_slot`). The packets are as they were (a pipeline's reads
are counted), but a library with 16 slots and a package with 12 would drop four, so `TIDE_EMBED_ABI` and
`TideLibrary.Abi` are 13. A texture the shader reads through another's sampler is still sampled by its own slot's
bits (`texture_call`): the two say the same but for TideRender's blended fallback for a texture without mips, which
stays the texture's. The Sample's `ComputeReplayTests` WGSL moved its storage to 33. Not run in Unity: the Sample's
checked-in shaders weren't written again, and no Unity run was made for this change.

## Counted indirect draws: embed ABI 12 (2026-11-05)

`TIDE_EMBED_RENDER_DRAW_INDEXED_INDIRECT_COUNT` (buffer, offset, most, counts, count_offset), appended after
`BUFFER_READ`: gpu.h's `draw_indexed_indirect_count` (notes/drawing.md, Culled runs drawn by a count, joined).
`TIDE_EMBED_ABI` and `TideLibrary.Abi` are 12. TideRender replays it as `most` (at most 65,536) DrawIndexedIndirect
ops at 20-byte steps: records past the count are of no instances, so they draw nothing. The recorder's `indirect()`
is 0: Unity's instance ids count from 0 whatever a record's first instance, so the renderer gives it none but 0 (and
doesn't cull on the GPU under the recorder anyway). Test: the Sample's `ComputeReplayTests.CountedDrawsDrawTheirRecords`
(a record of three corners drawn, records of none not), `embed_render_compute` (the op's words).

## Meshes over GPU lists in Unity (2026-11-05)

Nothing new crosses the embed (ABI stays 11): a pulled-vertex shader is a pipeline with a vertex slot of stride 0 and
no vertex attributes, reading storage in its vertex stage (as `ComputeReplayTests`' draws do), drawn with the CPU's
indices (`DrawProcedural` with an index view) or, for GPU indices, `DRAW_INDIRECT` without indices
(`DrawProceduralIndirect`). Their HLSL compiles with fxc (`tidec_hlsl_gpu_meshes`). `TideWarnings.hlsl` now also turns
off X3584 (one value every invocation writes to shared memory, as X3583 for storage); X3695 (differing values) can't
be turned off, so tidec warns (notes/compute.md). Not run in Unity: the Sample has no game with these draws, and no
Unity run was made for this change.

## Compute in Unity: embed ABI 11 (2026-11-05)

The recorder (`gpu_record.c`) writes gpu.h's compute calls as
`tide_embed_render` ops after the existing ones, whose packets are unchanged:
`PIPELINE_STORAGE` (id, compute, storage_count, storage_writes bits), right
after the `PIPELINE` it's of; `STORAGE` (slot, buffer); `COMPUTE_BEGIN`,
`COMPUTE_END`; `DISPATCH` (x, y, z); `DISPATCH_INDIRECT`, `DRAW_INDIRECT`,
`DRAW_INDEXED_INDIRECT` (buffer, offset); `BUFFER_COPY` (from, from_offset, to,
to_offset, size); and, new with the replay, `BUFFER_READ` (token, buffer,
offset, size). A `BUFFER` of kind 2 is a storage buffer, zeroed when given
nothing. `tide unity` writes each kernel's `.compute` (`tide_hlsl_unity_compute`,
by the same key as shaders; `notes/compute.md` has the HLSL contract).

**Reads back (embed ABI 11).** `buffer_read` gives a token (a table of 128 in
`gpu_record.c`) and writes `BUFFER_READ`; the other program hands the bytes over
with the new `tide_embed_render_read(e, token, data, size)`, NULL for a read it
couldn't make, and `buffer_read_done` gives them once (1), 0 till then, -1 for a
failed, unknown, answered or wrongly sized one. `tide_embed_render_reset` and
the embed's end drop every waiting read (the renderer fails them itself, its
device epoch moving). `gpu.h` has `tide_gpu_record_read`. `TIDE_EMBED_ABI` and
`TideLibrary.Abi` are 11; `tide_embed_render_read` is in the embed's exports
(`build.c`), and on the web `tide_unity_render_read` (the generated program) and
`TideWebRenderRead` (Tide.jslib, which copies the bytes in through
`tide_unity_buffer`). `TideRunner.RenderRead` passes TideRender's answers on.

**The replay (`TideRender.cs`).** Compute runs once a frame, however many cameras
draw it: as `Prepare` reads the commands, into a CommandBuffer of its own
(`work`) that `Flush` runs with `Graphics.ExecuteCommandBuffer` before anything
done at once (a buffer or texture written, mipmaps) and at the commands' end,
the recording's uniform buffer written first so kernels have their values. Draws
stay deferred to the camera, so:

- A storage buffer is a raw `GraphicsBuffer` (Raw, CopySource, CopyDestination),
  as vertex buffers are. Unity ignores Raw beside IndirectArguments ("Invalid
  combination ... Ignoring Raw"), and its CopyBuffer copies whole buffers of one
  size alone, so indirect calls' numbers go through `Resources/TideCompute.compute`
  (`TideCopy`, words between raw buffers, bounds-checked): copied where the call
  is in the frame into a pooled raw buffer, then with CopyBuffer into a pooled
  IndirectArguments buffer (and, for a dispatch, a pooled `Constant |
  CopyDestination` buffer bound as `TideGroups`). A direct dispatch's TideGroups
  is its own pooled buffer, written at once. Pools are taken in turn and reset
  at each frame's `Replay`. `BUFFER_COPY` is the same kernel (through a buffer
  between when both are one).
- A buffer a kernel or copy writes after a kept draw of the frame read it is
  renamed (`Written`): a new buffer, the old one's bytes copied first in the
  work's order, the draw keeping the old one, as CPU writes already were.
- Draws bind storage with `SetGlobalBuffer(_TideStorage<i>)`; indirect ones are
  `DrawProceduralIndirect` (with the bound index buffer for an indexed one),
  their first vertex or index in the numbers, `_TideFirst` 0.
- Kernels: the pipeline's `.compute` (`Loaded<ComputeShader>`, looked for again
  each frame till imported, one Console error two seconds on as for shaders),
  kernel `tidehl_compute`; `SetComputeBufferParam` for its storage,
  `SetComputeConstantBufferParam` for `TideUniforms` and `TideGroups`,
  `SetComputeTextureParam` and `_TideSampling(High)` for its textures.
- Reads are `RequestAsyncReadback` in the work's order; the answer goes to
  `TideRunner.RenderRead` (or `ReadsTo`, a test's). One begun before
  `ForgetAll` (a reset) is dropped: the embed forgot it. A read of nothing, or on
  a GPU without async readback, fails at once.
- `Replay` with no camera still prepares, so compute and reads go on.
- `Supported` now asks for compute shaders, indirect arguments, shader level 4.5,
  seven storage buffers in compute and fragment stages and ten (three vertex
  buffers more) in vertex stages (four and seven since GPU lists in pools, above), and not OpenGL ES 3: **Unity's OpenGL ES 3.0
  and WebGL 2 targets don't take Tide's renderer any more** (owner's rule:
  compute is on every render backend), and draw the views' 2D with the OnGUI
  path (`TideDraw.cs`) as GPUs without constant buffers did. WebGPU, Metal,
  Vulkan, Direct3D 11/12 and desktop OpenGL do. The GL ES path itself went with
  the GL backends' removal (above).

**Picks (mine, Provisional):** compute in a CommandBuffer of its own run as the
commands are read, rather than in the camera's (a camera can draw a frame twice,
or not at all, where compute must run once); a kernel samples a render target as
the camera's commands left it (the last frame's passes), not as this frame's
passes before it, which run later; copies through a kernel of the package's.

**Tests:** `tide_platform_embed`'s `embed_render_compute` (every compute op's
words, a read answered once, another size or NULL failing, a reset dropping
waiting reads); `tide_cli_tests`, `tide_cli_unity_physics`,
`tidec_unity_folder_names`, `tide_platform_embed_shards`, `tide_editor_embed`
pass (sanitize). The Sample's PlayMode `ComputeReplayTests` replays commands it
makes itself through a kernel and a draw shader tidec's translator wrote
(`Assets/Tests/Resources/TideShaders/00000000c0de000{1,2}.*`, from the WGSL in
the test's comment), with `ReadsTo` taking the reads: a direct and an indirect
dispatch (their storage, values and TideGroups), a copy, a write after the work
landing after it, a read of nothing failing; storage starting as zeros and
staying from frame to frame; and two indirect draws reading storage in both
stages, the first's colors kept as they were though a kernel between them wrote
them. Run in Unity 6000.6.0f1 in batch mode on a GPU (TIDE_HIDDEN, no window,
`-noAudio`, automatic builds off) in a scratch copy of the Sample with this
checkout's package by `file:`: ComputeReplayTests 3/3 on Direct3D 11 and 12,
Vulkan and OpenGL Core, and EditMode `ShaderTests` (the fixture shader too) with
nothing said; without the rename the indirect-draw test fails (checked). The
package and test assemblies compile with no C# warnings. The web debug build of
the embed compiles; the docs build passes. Not run: the Sample's other suites
with a tide built from this checkout (ABI 11 needs one; no Sample game runs a
kernel), a real game's kernels in Unity, Metal, WebGPU (a Unity web player),
URP's pass with compute, and the `tide unity --web` program's new export.

## Embed ABI 10: the game asks to quit (2026-11-04)

`tide_embed_status.quit` (a former pad byte; size 376 unchanged) is set once
the game calls `Application.Quit`, until the embed ends. `TideNative.cs` has
`quit` and `pad[2]`, `TideLibrary.Abi` is 10. `TideRunner.FollowQuit` acts
once per embed: a `QuitAsked` handler decides; without one, the editor leaves
play mode (`EditorApplication.isPlaying = false`) and a player calls
`UnityEngine.Application.Quit()`. The sample's generated `DisconnectReason`
gains `Banned = 7`. Not run in Unity locally (no Unity tests here); the
native embed still builds and its tests pass (`tide_platform_embed`).
Correction the same day: `TideGame.cs` had been edited alone, and CI's
`tide_cli_tests` (`unity_code_writes_the_samples_package`) regenerates it from
the Sample's `schema.json`; that and `components.json` gained `Banned` (7) too,
so the three agree.

## Protect cached compilers from expiry (2026-11-03)

The CLI now removes exact cached versions after 30 days without use
(notes/cli-and-packages.md, Moving project channels and cache expiry). Executable
selection takes its shared `.VERSION.use` lock outside the installation and
refreshes `.VERSION.last-used`. File sharing on Windows and flock on POSIX match
the CLI protocol. The editor retains selected compilers until assembly reload or
quit; process termination releases the locks after a crash. Selection rechecks
the complete installation under the lock. Package/commit selection remains exact;
standalone release channels do not replace Unity's package-selected compiler.
VersionTests covers lock exclusion/release and stamps as well as existing selection
and download behavior. Unity 6000.6.0f1 in a scratch Sample passes 12/12 enabled
VersionTests (one existing ABI-5-host check ignored without its separate fixture),
under TIDE_HIDDEN/batchmode/nographics/noAudio with automatic game builds disabled.
It checks selection prevents exclusive access, release permits it, and ending a
session refreshes last use. Native Windows ASan CLI/version tests also pass;
Linux/macOS flock interop and interactive editor UI remain untested here.
No embed ABI, generated C# or runtime behavior changed.


## Embed ABI 9: a pipeline's alpha to coverage (2026-10-31)

`TIDE_EMBED_RENDER_PIPELINE` has one word more, `alpha_to_coverage`, after
`outputs` (a cut-out frag into a target with samples: notes/render-targets.md,
Cut-outs), so `TIDE_EMBED_ABI` and `TideLibrary.Abi` are 9. `TideRender.cs`
reads it into `Pipeline.alphaToCoverage` and sets `_TideAlphaToMask` on the
material, which the ShaderLab tide unity writes takes as
`AlphaToMask [_TideAlphaToMask]` (0 by default). Not run in Unity here.

## Versions window and unpublished dev packages (2026-10-31)

The owner's TideUnity project downloaded 0.6.0-nightly.6 (embed ABI 7) for
its dev package (ABI 8). The compiler selection and download were working;
calling an ancestor release the package's matching version was wrong. Owner
requested an editor window for updating and switching versions.

Window > Tide > Versions, also Manage Tide versions in Project Settings >
Tide, shows the package revision/commit, required ABI, selected compiler
version/path, stable/nightly releases, and Check for updates. Use version
installs that release's compiler with the existing checksum/version checks
before asking UPM to switch the package to the same tag. A download failure
leaves the project pin unchanged; a UPM failure is shown and restores building
with the previous selection. The build service stops during a switch and its
IDE cache is cleared. UPM owns manifest/lock changes and script reloads.
Automatic building resumes when enabled. Update / switch to dev re-adds its
Git URL to refresh the branch. No local package or launch override is replaced.
Switching is disabled during Play, compilation and another switch. Choosing
an old release also restores that release's UI (which may predate this window).

Git commit resolution now requires the site's exact flag; an ancestor is only
a suggestion for choosing a release in this window. Untagged dev ahead of a
published compiler cannot build with that older compiler. The old unversioned
ancestor cache is ignored; successful format-2 lookups expire after ten minutes,
failed lookups retry after one minute, and Check for updates clears both.
Release tags still select their own version directly; local and -tide-command
development selections retain their existing behavior. The endpoint addition
and deployment boundary are recorded in releases-and-ci.md. No engine ABI,
runtime, compiler semantics or generated game code changes.

Validation: Unity 6000.6.0f1 EditMode VersionTests 12/12, including an actual
checkout-built ABI-5 host, legacy ancestor cache invalidation, refusing ancestor
and missing-proof responses, refreshing after publication/expiration, release
listing/availability, URL preservation, version ordering and the menu. Existing
download/checksum/version/launch/cache checks remain; a failed compiler download
through the window leaves the manifest unchanged and clears the switching guard.
Node 24.22.1 release tests
11/11 and docs production build pass (the existing unstaged game-page 404s).
The window's actual Change action also passes in a scratch editor: a local Git
fixture tagged v9.9.9-test.1 and a file-based release site exercise checksum
installation, UPM Client.Add, package registration/script reload, both manifest
and lock pins and the final pinned compiler selection. Its compiler fixture is
discovery-only, never executed; automatic builds are disabled. All Unity runs
use TIDE_HIDDEN, batchmode, nographics and noAudio. No owner project is changed.
The window's pixels, other operating systems and native/web game execution are
not exercised; this changes editor/version selection only.


## Embed ABI 8: 8-byte handle slots (2026-10-30)

Text, list and grid fields are 8 bytes in the game's C and a map or a set 32
(notes/ecs.md, 64-bit handles), so the view packet's component bytes, which
follow the game's layout, changed: `TIDE_EMBED_ABI` and `TideLibrary.Abi` are
8. `TideText`, `TideList` and `TideGrid` keep their `uint handle` (the
packet's place) in the first 4 of 8 bytes, the rest an unused zero word, and
`TideMap` is 32 bytes; `unity_code.c` sizes them 8 and 32. A command's text or
list field from C# is refused when its high word isn't zero
(`tide_embed_command_text`, `_list`). The Sample's `com.tide.game/schema.json`
and `compiler/cli/tests/unity_rules_physics/schema.json` were laid out again for
the new sizes by a script (notes/ecs.md, 64-bit handles: built), keeping their
old `hash`, and the Sample's `TideGame.cs` written again from it; its first
real `tide unity` build writes both as the build has them.

## Development overrides belong to the editor launch (2026-10-26)

Owner decision, replacing the persistent override introduced below: normal use
must follow the project pin; engine iteration opts in when launching Unity.
`-tide-command <executable>` selects a development compiler for that editor
process only. Missing/repeated values and missing executables fail explicitly.
Every lookup reads the launch arguments, so domain reloads retain the selection.
Project Settings > Tide displays it and warns, but edits no path. The
TideUserSettings class, saved tidePath field, migration and path-picker UI are
removed; old ProjectSettings/UserSettings paths are ignored, not migrated again.
Without the flag, existing git-package version resolution/install behavior and
local-package PATH/installed fallback are unchanged. ABI diagnostics now say to
relaunch without the argument or rebuild the development compiler.

The IDE cache is format 2. A development selection carries the editor PID and
UTC process-start epoch seconds, as well as the existing lock hash/commit.
Normal selections have sessionPid 0 and the resolved pinned version; IDEs check
the installation's VERSION before reusing them, so a global upgrade cannot
silently change a project pin. A normal quit deletes
only this process's development selection; IDEs validate process identity too,
so a crash or PID reuse cannot revive it. Old unscoped caches are ignored.
An unavailable/unresolved pin is reported, never replaced by a stale override.
The editor/frontend implementations and exit observation are in editors.md.

Validation: Unity 6000.6.0f1 VersionTests 8/8 on a normal batch launch and 8/8
on a second launch with the real -tide-command argument (path includes spaces).
They cover actual argument selection, ignoring a saved legacy path, malformed
arguments, session metadata/quit cleanup, missing executable, normal cache kept,
and rejecting the checkout-built ABI-5 host with the appropriate fix. The
development launch's cache was absent after its process exited. Builds were
disabled in this test project; no stand-in compiler was executed. Existing
version/download/checksum checks remain included. No runtime C/ABI changed;
native/web game suites and other operating systems weren't needed/run.

## IDE toolchain selection and an old host after a package update (2026-10-26)

The owner reported Play loading embed ABI 5 after updating to an ABI-7 package.
Read-only inspection of the Playground found its migrated UserSettings tidePath
still naming TideDev/playground-tide/bin/tide.exe. That intentional-override path
takes precedence over package pinning; neither the override nor the running
owner's project was changed by this task.

Project Settings > Tide now explicitly warns that Your tide command overrides
package updates and offers Use package version (clear the local override and
restart the build service). The runtime's existing ABI rejection appends an
editor-supplied explanation naming that command and this fix. With no override,
it names the selected compiler and explains that #dev can precede the next
published compiler. Play's editor hook checks an existing host through the same
loader before entering Play, keeping the error in Edit Mode. No embed ABI or C
runtime interface changed; explicit custom compilers remain supported.

TideToolchain.Find publishes the selected command to Library/Tide/toolchain.json,
with its package commit and SHA-256 of Packages/packages-lock.json, for the IDEs
(notes/editors.md). The lock digest also identifies local packages. Writes are
atomic, only on content changes; a failed selection deletes the old cache.
The machine path stays out of source control. Existing version.json and release
resolution/download behavior remain unchanged.

Validation: Unity 6000.6.0f1, batch/nographics/noAudio under TIDE_HIDDEN, six
VersionTests pass, including a custom command publishing the IDE cache, a failed
selection removing it, and a tiny checkout-built native host exporting ABI 5
being refused by the pre-Play helper with the override path and its fix. The
existing release-tag, commit-cache, side-by-side install, wrong-version and
checksum tests pass too. Final atomic-write change is included in that run.
IDE checks and docs build are recorded in editors.md. No rendering/runtime/game
change requires native/web suites; interactive Play cancellation and the new
settings button's pixels were not exercised in a window. User docs are updated.

Integration with origin/dev 665bf9217 keeps the incoming retired-converter
cleanup below and the README's current component workflow. Only this note's
adjacent new sections conflicted; both are retained. The merged package compiles
and passes VersionTests plus EntityEditorTests, 9/9, with the same batch flags
and ABI-5 fixture. Unrelated IDE checks and docs builds need no repetition.

## Retired list conversion tool (2026-10-26)

Owner: no pre-1.0 compatibility tooling is needed for the old TideEntity lists.
Removed the project-wide list-to-component converter, its Tools menu and batch
entry point, its inspector prompt, and its migration-only tests and metas.
The inspector's generated-component lookup now belongs to `TideEditorSchema`,
with only the component names and script GUIDs it needs from `components.json`.
Add Tide Component still adds the generated Unity component where available.
The existing listed authoring format and CLI asset reader are unchanged by
this menu/tool removal. The package README describes adding the game's own
components through Unity's Add Component menu.

Validation: Unity 6000.6.0f1 compiled the package in a scratch copy of the
Sample and passed `EntityEditorTests` 3/3 (EditMode, batch, `-nographics`,
`TIDE_HIDDEN=1`, automatic Tide builds disabled). No C# warnings or errors;
no remaining conversion-tool references in `unity/`. Inspector drawing in a
window was not exercised; no runtime or web code changed.

## Debugging the game in the editor (2026-10-26)

Each `game-<n>` library `tide unity` builds is a debug build whose `.pdb`
carries the game's natvis, with `Library/Tide/game.natvis` and `game_lldb.py`
beside it. A native debugger attached to the Unity editor (VS Code's Attach
to Unity (Tide), CLion's Run > Attach to Unity (Tide), Visual Studio's Attach
to Process) stops at `.tide` breakpoints in the `loaded-<n>` copy the embed
host loads, and on each new library a test or a reload loads. Checked in
Unity 6000.6.0f1 itself (batch PlayMode tests under Windows' dbgeng) and with a
host loading `tide_embed.dll` as `TideRunner` does. `notes/debugging.md`;
package README, Debug.

## Time travel (2026-10-26)

The owner asked for a time-travel debugger in Unity: pause a match in Play, step it a tick at a time, scrub back
through recent ticks with Unity's GameObjects, inspectors and Tide's drawing showing the tick, and record and replay a
session bit for bit (a tester's file too), with the first tick that differs when the build changed.

**Owner's decisions** (asked during the work, 2026-10-26):

- With other machines in the match, pausing never blocks them: "continue the main simulation in the background such
  that everything plays as normal and when I unpause I jump to the present ... if we don't have other players
  connected we can pause the simulation". Built: with nobody else in it (`tide_session_remote_players` 0) Pause is a
  real pause (no tick runs, no time passes for the match); with others in it, or one joining during a pause, the match
  runs behind the tick shown and Resume shows its latest. This machine's player sends input as if it let go of
  everything meanwhile (a zeroed `tide_embed_input` to the unseen frame): **Provisional (mine)**.
- "Continue from here" was asked for, then "should work for multiplayer with clients too". Built for both: the
  match goes back to the tick shown, the ticks after it gone, other machines' clients included.

**The boundary.** Real play has no engine-imposed pause and still hasn't: time travel is a debugger the embed offers
when the other program asks (`tide_embed_travel_keep`), the runner's Time Travel setting deciding where
(`TideTimeTravel.Editor` by default: Play in the editor; `EditorAndPlayers` lets a game save testers' recordings;
`Off`). Only a match this machine runs is recorded (the server's journal is the source); a client of another
machine's match, and a sharded match, say why not (`tide_embed_travel.why`). (Since 2026-11-19 a client's match is
followed, its confirmed ticks kept, and a recording of another layout is carried over rather than refused: "Time
travel's gaps", above. A sharded match still says why not.)

What's built:

- **The session's recorder** (`engine/src/session.c`, `tide/session.h`; notes/netcode.md): `tide_session_record` takes
  `begin(world, tick)` and `tick(tick, record, size, world)`, fed by the server's journal (`tide_server_desc.journal`,
  the shards' recovery records), `begin` as the recorder is set on a running match, as a match starts, as this machine
  takes one over, and as a layout reload migrates it. `tide_replay_check` is `tide_replay` saying same, different
  (with both hashes) or unreadable. `tide_session_rewind(world)` and `tide_session_remote_players`.
- **The timeline** (`platform/include/tide/timeline.h`, `platform/src/timeline.c`): every record since the start
  (64 MiB of them at most, then it starts again from the first world kept), the start world, a ring of the latest
  `keep` worlds (COW copies, so the cost is the pages that changed), a world made again from the start for a tick
  before the ring (`seek`, kept for the next tick on), and replay checks (the first tick that differs, its hashes,
  the first unreadable). Tick n is the world after n ticks. `tide_timeline_cut(tick, world)` is Continue From Here's:
  records after the tick go, the next record is whichever server tick comes, and `world` (the rewound world with the
  match's command state, below) is kept as that tick's, recorded as a **jump** when it isn't what replaying makes, so
  a replay goes there too. Saved as bytes (`TIDETIME`, version 1, little-endian): layout hash and simulation ID, tick
  rate, the recording machine's player, the first tick, worlds packed as `pack_delta` against nothing (portable
  between native and wasm; raw packs only for a game without deltas), the records, the jumps. Loading refuses another
  layout hash ("laid out otherwise ... replay it with a build of that layout"); another simulation ID loads and says
  `other_build`.
- **The embed** (`tide/embed.h`, embed ABI 7): `tide_embed_travel_of` (state, first/kept/last/shown, tick rate,
  replay checks, `background`, the shown world's hash, `why` and `refused`), `_travel_keep(seconds)`, `_pause`,
  `_step`, `_show`, `_continue`, `_recording`, `_replay`, `_replay_end`.
  - Paused at the latest tick with nobody else in the match, frames are `tide_run_frame` with no time, through
    `tide_host.h`'s `tide_run_unblended` (views and the packet show the latest tick as it is, not blended into the one
    before). Local code runs; its commands and requests go to the session and wait for the next tick; devices latch, so
    a key pressed while paused is in the step's tick. A step is one frame of `tide_session_until_tick` plus 1 µs, with
    the commands local code (or C#) sent handed to the session first (`tide_run_send_commands`, the loop
    `tide_run_frame` ends with, factored out of it), so a command sent while paused is in the step's tick rather than
    the one after (the Sample's scrubbing test found it).
  - Showing another tick (scrubbed back, background mode, or a recording) is `show_frame`: the views run on that world
    with a local state of their own (`travel.local`), with no session; its devices are consumed and its commands and
    requests dropped, so nothing of an older tick's picture reaches the match, and the match's local state is as it
    was when it goes on. The packet's player is this machine's, or a recording's player.
  - Background mode: each frame runs the match's own frame unseen (zeroed input), then `show_frame` of the tick shown.
  - A recording shown pauses a match this machine runs (refused for one it can't pause) and plays at its tick rate
    (`playing`, at most a quarter second of ticks a frame).
  - Hot reload: `reload_library.inc`'s `reload_before(old, next)`, which only the embed sets, runs before a swap or
    full reload while the old build's code is there: the separate local state goes; a build of the same layout keeps
    the timelines (`tide_timeline_set_game`); another layout or a full reload drops them (the session's next `begin`
    starts the recording again from the migrated world), keeping the pause. While paused the embed still checks for
    builds with the frames' real time.
  - Web: `tide unity --web`'s program exports `tide_unity_travel` and the rest, `Tide.jslib` has `TideWebTravel` and
    the rest, `TideLibrary` calls them.
- **Continue From Here** (`tide_embed_continue`): `tide_session_rewind` copies the kept world into the server's,
  then the generated `tide_game.keep_commands` (codegen.c, new) copies the live world's `tide_command_state` over it,
  so each player's command sequences and unordered windows stay where the match had them: a command applied after the
  tick doesn't run again, and one on its way (a client's pending, numbered on from the live world) goes in as it would
  have. This machine's player takes the server's world at once (`take_server_world`); other machines' clients find
  their next tick's hash isn't theirs and repair or resync as on any divergence. The world the match goes on from is
  therefore the kept tick plus the command state, which the timeline records as a jump. No wire protocol change; the
  generated header's Session ABI marker gained `rewind (1)` for the new `tide_game` member.
- **The package**: `TideTravel` and `TideTravelState` (TideNative.cs), TideLibrary's bindings (ABI 7), and
  TideRunner's `timeTravel`, `keepSeconds`, `Travel`, `Pause`, `Resume`, `Step`, `ShowTick`, `ContinueFromHere`,
  `Recording`, `SaveRecording`, `Replay`, `ReplayFile`, `EndReplay`. `Editor/TideTimeTravelWindow.cs` (Window >
  Tide > Time Travel, IMGUI, repainting in Play): what's going on (`Describe`), Pause/Resume, Step, Continue From
  Here, a Tick slider, Save Recording / Replay This Match / Open Recording (`.tiderec`), and in a replay Play/Stop,
  Step and Back to the Match with the check's result.

**Picks (mine, Provisional):** Resume from a scrubbed tick goes to the latest (Continue From Here is the other);
the separate local state for other ticks' frames (local handlers don't hear replayed or scrubbed ticks' events, and
views' accumulated local state starts fresh there); a replay needs the same data layout rather than migrating its
worlds; 10 seconds kept by default; 64 MiB of records; the `.tiderec` extension; a step's 1 µs past the tick.

**Tests:**

- `tide_platform_travel` (`platform/tests/travel.c`, `travel.tide`; native): off until kept; pause and step one tick
  (no tick in a second of paused frames, the latest tick unblended, input and a press while paused in the step's
  tick, no burst after resuming); kept ticks shown again (hashes and what the views drew equal, ticks before the ring
  made again from the start, a B pressed on an old tick's picture sending nothing, steps from a shown tick); record
  and replay bit for bit with input, commands and a view's command (played at the tick rate, every tick checked, and
  replayed again in another embed in no match); a flipped record hash reported at its tick with both hashes, another
  layout and a cut-short file refused; continuing from a tick (a Bump after it gone for good, a later one once, the
  recording with its jump replaying bit for bit); a match never stopping for another machine (the game's own network,
  a loopback, opened to a second session: a real pause becomes the background mode as it joins, the guest stays
  connected and current, Continue From Here takes the guest's verified world back too). Session-level, three machines
  over a lossy loopback: three players' input and commands replayed bit for bit (and before a ring of 8, from the
  start); a build whose tick changes at its 50th tick (same layout, another simulation ID) differing at tick 50 with
  both hashes and `other_build`; a rewind with two clients in the match, a command on its way going in once, and the
  recording replaying bit for bit.
- The Sample's PlayMode `TravelTests`: pause holds the match and a step runs one tick; scrubbing shows a kept tick
  with its GameObjects (the tossed ball gone before its toss, the player's ball where it was, the live `Ball`
  component's values and the world hash as they were); a recording saved to a file replays bit for bit (every hash,
  the ball where the match had it, the recording's `Say`), and another layout is refused; Continue From Here takes the
  match back (the toss gone for good, a later one in); a hot reload while paused keeps the pause and its ticks, and
  the new build steps. EditMode `TravelWindowTests`: what the window says, and its menu item.

**Runs:** native debug 2801/2801 and web debug 2696/2696 (both without `private-prototype`; one disabled test
each, MicroDuck's turn as before), `tide_platform_travel` 10/10; the Sample's EditMode 30/30 and PlayMode 22/22
(D3D11, a GPU, batch), with a tide installed from this checkout's mingw-release and debug builds and the Sample's
`com.tide.game` written again by it (the schema hash moved with the Session ABI marker). After merging the type boundary's work (`unity_code.c`, the Sample's package): the native unity, CLI, LSP, outline, tidec, embed, reload and travel tests 2573/2573, the Sample's package as tide writes it unchanged, EditMode 33/33 and PlayMode 22/22. Not run: Unity on the web
(the jslib and exports are built and compiled, not played), the window's drawing in a window, Linux and macOS.

**Not done / gaps** (worked through since: "Time travel's gaps (2026-11-19)", at the top)**:** local handlers and the packet's EVENTS for replayed or scrubbed ticks (the separate local state
hears nothing); a client of another machine's match can't time travel (it has no journal; its verified worlds could be
kept, not built); sharded matches; replays across a layout change (the old layout's description isn't in the
recording); the window's drawing seen in a window (batch runs have none).

## Twelve texture slots and embed ABI 6 (2026-10-25)

The coordinated internal capacity completion raises TideRender's sampled arrays
and texture property IDs to twelve. Slots 0..7 keep _TideSampling's original
four-bit modes; slots 8..11 use _TideSamplingHigh, supplied separately per draw.
HLSL reads that high word only for high slots. The recorded pipeline contains
one read entry per actual texture, replacing eight fixed entries. Native and
managed embed ABI constants are both 6; stale hosts are rejected and the whole
host/library/generated-shader set must rebuild. The separate draw packet stays
at its existing version. No new runtime function, device baseline or vertex
buffer transport is introduced. Twelve game textures plus the three GLES/WebGL
vertex-buffer textures fit the existing sixteen vertex-texture baseline.
The native zero-through-twelve packet fixture and strict FXC compilation of the
capacity fixture passed. Unity player execution remains unvalidated locally.

Integration audit against b4655dfd (2026-10-25): the merged hot-reload fix below
keeps only vertex layout arrays in each pipeline's MaterialPropertyBlock.
Both procedural draw paths retain that block; each command separately captures
both sampling words and sets both globals immediately before drawing. The
twelve-slot arrays, actual-count pipeline parsing and both ABI 6 constants
survived the merge. Disable/enable discards pipelines and recordings; Fresh
requests a render reset, and every shaded draw supplies its actual sample slots.
The Sample's twelve checked-in shaders still declare only the low sampling word
and reference slot zero; they do not exercise high slots and need regeneration
with the complete rebuilt toolchain. This was a source audit only: no Unity,
compiler or build execution was performed for the merge.

Proposed and built 2026-10-20, early: the owner asked for every phase "most of
the way there" to test and then list tweaks. What's built is in the "as built"
sections below; the proposal's text above them is the design they follow.
Decisions the owner made in the discussion are marked **Owner**; everything
else is a pick to confirm. One pick the owner hasn't seen and should:
`Packages/com.tide.game`, a generated folder in the project (Authoring as
built, "Generated types and Unity's build cache").

Tide stays the simulation, networking and determinism: the match code, its
session, prediction, rollback, hashing and transports run as they do today.
Unity renders, plays audio, and is where scenes and prefabs are authored. The
shape is Photon Quantum's, with Tide's language in place of Quantum's C# DSL.
C# never runs match code, so determinism is unaffected by Mono or IL2CPP.
Dedicated and managed servers stay Tide-only builds of the same match code;
Unity clients join them unchanged.

## What a user has

**Owner:** as few generated files as possible. The project holds `.tide`
sources, Unity scenes and prefabs, and ordinary C# MonoBehaviours for views.
The schema, the native library, compiled authoring data and shaders live in
Unity's `Library/` (an uncommitted, rebuildable cache) or as sub-assets of the
`.tide` files' importer, never as files a user maintains or commits.

## C# bindings

**Owner:** generated by a Roslyn source generator, with no `.cs` files on disk.
**Superseded (owner, 2026-10-22):** tide writes the C# as files in
`Packages/com.tide.game`, so components are real Unity components (Components of
their own, below); the generator is gone.
tidec emits a schema beside the native build: components (fields, offsets,
sizes, local parts), singletons, events, commands, settings and prefab/scene
names. A Unity package ships the generator, which turns that schema into typed
C# at compile time:

- `entity.Get<Health>()` and its `Verified` counterpart, local fields and local
  state, read as blittable structs. Generated types already have explicit
  padding and size assertions, so they map to `[StructLayout]` structs.
- `Tide.On<Hit>(...)` for what the game's local handlers hear (the event log).
- `Tide.Send(new BuyItem { ... })` for commands.

Readers work on a base pointer plus offsets: the world's own memory on desktop
and mobile (no copy), a per-frame copy on the web (below), with one C# path.

## Authoring

**Owner:** Unity assets are the source of truth, and we own the authoring
component and its serialization rather than relying on Unity's mechanisms.

Source-generated MonoBehaviours have no script asset and can't be attached to
GameObjects, so the package has one hand-written `TideEntity` MonoBehaviour per
entity GameObject (superseded 2026-10-22: the game's components are Unity
components of their own, Components of their own below; what follows is the
listed form, which tide still reads):

- Authored values are Tide syntax, one string per component, holding only the
  fields the author set (`Health { value = 100 }`).
- **Owner (pick accepted):** overrides merge field by field. A variant or
  prefab instance stores a patch of the fields it changed; tidec layers it over
  the base, so later edits to other base fields still flow through. Unity marks
  the whole string overridden; our inspector shows the merged values and marks
  overridden fields itself.
- Entity references and asset references live in a separate list of real Unity
  object references, which the text names by index (`muzzle = @0`), so Unity
  keeps them correct through hierarchy moves and renames.
- Nothing is dropped silently: an unknown component or field stays in the text,
  shows red in the inspector and fails the bake with a diagnostic. **Owner
  (2026-10-21):** not for a field taken out of a component: the bake leaves its
  value out, saying nothing, and the inspector doesn't show it ("we should error
  when we are using data from views for example that ain't there anymore.
  inspector tweaking should just hide it and move on"): code still using the
  field is what fails. An unknown component still fails the bake.
- The inspector has "Add Tide Component", one foldout per component styled like
  a Unity component, Tide defaults shown greyed out, and `[SceneRequired]`.
- Plain Unity components (renderers, animators, VFX) sit on the same objects or
  their children. GameObjects without `TideEntity` are visual-only.

### Baking at tide time

**Owner:** no intermediate files. tidec reads Unity's text YAML itself, as it
reads `.tprefab`: a game's sources include `Assets/**/*.prefab` and `*.unity`,
and objects with `TideEntity` become Tide prefabs (named after the file) and
scenes. tidec resolves nested prefab instances and variants through `.meta`
GUIDs and applies their modifications, turns Unity Transforms into Tide
`Transform` and the hierarchy into parenting, and reports errors at their place
(`Assets/Enemy.prefab: Enemy/Weapon: Aim.muzzle: no entity`). The package shows
them in the Console; clicking one selects the GameObject.

Assets must use Force Text serialization (Unity's default); binary assets get a
clear error. Unity's YAML isn't formally documented, but has been stable for
years; the parser needs only objects, components, references and modifications.

Tide renames (tidels) rewrite the Tide text inside Unity assets, as they do
`.tprefab` files and asset paths. Unity Transforms need no conversion: Tide is
left-handed with +y up and +z forward, applies Euler angles Z, X, Y, puts a
triangle's front on its clockwise side and stores quaternions x, y, z, w, as
Unity does (spec; `engine/include/tide/math.h`). Authored floats are baked by
their bits.

This is a second authoring front end beside `.tprefab` and `tools/tide-editor`;
neither is replaced.

### Assets

**Owner:** asset fields use Unity GUIDs and are set in the inspector like any
Unity object field. At bake tidec maps each GUID to its file through `.meta`:

- Asset types Tide has (Texture, Mesh, SkinnedMesh, Animations, Sound) are
  imported by Tide's own importers (PNG, glTF, OBJ, FBX), only when Tide code
  reads their contents, such as mesh colliders or Tide drawing.
- Unity-only objects (materials, visual-only prefabs, VFX, animator
  controllers) stay in Unity. **Owner:** Tide gets no handle type for them; a
  reference only needs to exist in Tide if the simulation uses it. Views attach
  them as ordinary Unity components. Revisit if a game's match state has to
  choose among Unity-only assets.

## Runtime binding

- When Tide spawns `Enemy`, Unity instantiates `Enemy.prefab` and binds child
  entities to child GameObjects by path. The editor finds prefabs through
  AssetDatabase.
- Players include everything Tide can reach because it is referenced, never by
  name lookup: authored references are real Unity object references (scenes and
  prefabs keep them), and a scene build processor (`IProcessSceneWithReport`)
  adds a registry object to the built copy of the first scene referencing every
  Tide prefab, scene and `.tide` shader sub-asset. Unity's stripping then keeps
  their assets, shaders and engine modules, with no committed file and no
  "always included" lists.
- Loading a Tide scene loads its Unity scene; its `TideEntity` objects bind to
  the entities Tide spawns for it.
- Views see predicted state blended between ticks (the existing `previous` and
  `alpha`), with confirmed state on request. Shown/Hidden and Spawned/Destroyed
  drive GameObject lifetime, so a rolled-back predicted spawn is destroyed.
- Unity's Input System fills `Devices` on every platform, the web included.
  Unity UI marks what it consumed.
- **Owner:** simulation stays threaded; views and everything touching Unity run
  on Unity's main thread, during the frame step.
- Tide's audio plays through Unity's (2026-10-22, asked for by the owner:
  "Audio and the editor's workflow" below).

## Native boundary

It touches the runtime, so its contents are agreed with the owner before either
side depends on them (`notes/runtime-ownership.md`). Draft below.

**Owner:** in the editor the package loads and unloads the library itself
through function pointers (`DllImport` would lock it until Unity restarts). A
rebuild packs the world and local state, swaps the library and unpacks them, as
Tide's hot reload already does (`notes/hot-reload.md`). Users never see this.

### Phase 1 interface (owner approved the draft and all five picks, 2026-10-20)

The standard host (`tide/host.h`) already runs everything a front end needs
except the window: the session, rooms, dedicated-server admission, host
migration, local requests, commands, local handlers, files and hot reloading of
game libraries (`platform/src/reload.c`, `tide_host_game`). So the boundary is
that host with the window, GPU and audio device taken out, called once per
frame by another program. The name is engine-neutral, as nothing in it is
Unity's: `platform/include/tide/embed.h`, agent-maintained like the rest of
`platform/`.

```c
typedef struct tide_embed tide_embed;

typedef struct tide_embed_desc {
    const tide_host_game *game; // A game linked in (players, IL2CPP static builds), or NULL with:
    const char *library_dir;    // ...the folder tide writes game-<n> libraries to: the editor, hot reloading
    int tick_rate;              // 0 for the game's tickRate
    int argc;                   // tide/run.h's --host, --join, --connect, --seed, and a sharded game's --shards and --shard-width
    char **argv;
    const char *files_dir;      // Where the game's files live (Application.persistentDataPath)
    const char *relay;          // NULL for the default relay
    void (*log)(void *user, int error, const char *text); // stdout/stderr lines, for Debug.Log
    void *user;
} tide_embed_desc;

tide_embed *tide_embed_create(const tide_embed_desc *desc);
void tide_embed_destroy(tide_embed *e);

// One frame of tide/host.h's loop: the session's update, connection events, local handlers, local
// requests, then the views, recorded with the frame's view packet (below). `devices` is this
// machine's input as Unity read it. Returns seconds until the match next needs a frame.
double tide_embed_frame(tide_embed *e, float seconds, const tide_devices *devices, tide_float2 screen_size,
                        float screen_scale);

// The view packet the last frame wrote: valid until the next frame. Bytes, never pointers.
const uint8_t *tide_embed_view(const tide_embed *e, uint32_t *size);

// The session as tide_session_status says it, with the room code: what Unity's own UI shows.
void tide_embed_status(const tide_embed *e, tide_embed_status *out);

// What local code's Session calls do (tide_session_request): start a scene, join a code, connect to
// an address, leave, open, close, end, kick. False when it can't be asked now.
bool tide_embed_request(tide_embed *e, tide_session_request_kind kind, const char *argument);

// A typed command from Unity's UI, as local code's Send: `type` and `value` as the schema lays
// them out; packed and sent as Tide's own commands are. False for an unknown type or bad size.
bool tide_embed_command(tide_embed *e, uint32_t type, const void *value, uint32_t size);

// The app went to the background (true) or came back. Phones leave the match there, with host
// migration where enabled, as Tide's mobile hosts do; desktop changes nothing.
void tide_embed_background(tide_embed *e, bool background);

// The game's schema (JSON): what the Roslyn generator reads, and its hash, which every view packet
// carries, so stale bindings refuse to read rather than misread.
const char *tide_embed_schema(const tide_embed *e, uint32_t *size);
```

The view packet is the main new piece. Each frame, generated code writes what C#
reads into one flat buffer, laid out by the schema:

- a header: schema hash, frame, tick, verified tick, alpha;
- the shown entities: ID and generation, prefab, parent, owner and whether it's
  this machine's, Shown/Spawned flags;
- for each component, its entities and their values, floats blended exactly as
  views see them, and singletons;
- text and lists copied into the packet's own area, named by offset and length;
- the events this machine's local handlers heard this frame, for `Tide.On<T>`.

Its benefits: C# never depends on ECS storage (archetypes, lanes, pages, heap),
which stays free to change; C# memory safety is a bounds check against one size
and a schema hash; the web copies one buffer a frame (option B), desktop reads
it in place; C# does no blending. Its cost is writing it each frame, which is
the same work views reading those values do now.

Generated (tidec, per game), as new `tide_host_game` members: the view packet
writer, the command packer and the schema. The web build is the same host
compiled into the Tide program `tide.js` runs: `pageFrames` calls its frame,
the `.jslib` copies devices in and the packet out.

Decisions, all approved by the owner as picked:

1. The engine-neutral name `tide/embed.h` (pick), or Unity-specific naming.
2. The view packet (pick), or C# reading world storage directly.
3. Which prefab an entity came from must be known to late joiners, so it has to
   be match state: a compiler-generated prefab ID on entities spawned from
   prefabs or scenes, snapshotted and hashed (pick). Nothing records it today.
4. Packet contents for the first version: every match component of shown
   entities and singletons, blended; local fields; heard events. Confirmed
   (verified) values only for components C# asks for, as they double the size.
5. Unity's UI writing local state directly (not just commands and requests):
   left out of the first version.

Left for later phases: Tide's draw list for Unity's renderer (phase 5), audio,
Tide GUI input claims.

### Phase 1 as built (2026-10-20)

The owner asked for it "most of the way there", to test and then list tweaks,
as a git URL package with no hard-coded paths. Built:

- **`tide/embed.h`** (`platform/src/embed.c`): the standard host in another
  program's frame, with a linked game or, on desktop, `library_dir`: the newest
  `game-<n>` there, each newer one swapped in as `tide run`'s host does
  (`platform/src/reload_library.inc`, shared with `reload.c`). Frames take raw
  `tide_embed_input` (a change from the draft's `tide_devices`, so presses latch
  in the platform's own poll); status is a flat, size-asserted struct;
  `tide_embed_view`, `tide_embed_schema`, `tide_embed_command` (with a target
  entity: a completion of the approved call), START by scene name, and
  `tide_embed_abi`. Contracts and tests: `notes/standard-host.md`.
- **The view packet** is laid out in `engine/include/tide/embed_game.h`:
  header, entities (index, generation, owner flags, prefab, parent), then
  8-aligned sections of components (rows, blended values), singletons, local
  fields and STRINGS (text and plain-element lists, named by handle). Prefab is
  always none: entity prefab identity (owner decision 3) waits for authoring.
- **`tidec --embed`** (`compiler/src/embed_codegen.inc`, `tide_add_game(EMBED)`)
  writes `tide_game_embed`, in the `.c` only: the header, and so the game's hash,
  is byte-identical with and without it, so Unity builds play with every other
  build of the same game. The packet writer reuses views' `tide_prev_<C>`/
  `tide_blend_<C>`. Schema JSON comes from the game's own layout (C offsets)
  through the engine's `tide_schema_json`. Each match component's type there
  has `defaults`, its value with its declared defaults as hex bytes (text and
  lists empty), from the generated `tide_embed_defaults` (the
  `tide_embed_game.defaults` hook, 2026-10-20). A scene's tag (a `scene`
  declaration, the game's own or a Unity scene's `UnityEngine.Scenes.<Name>`) is
  a component there with `"scene":true` (`tide_embed_game.component_scenes`,
  2026-10-21), distinct from `scenes`, which lists only the scenes C# can start.
  Every match component the game declares is listed as one, with its defaults
  (`embed_component_listed` in `embed_codegen.inc`, 2026-10-21): it was only
  those an archetype stored (`embed_component_kept`), so a new component nothing
  spawned yet was a "struct" and Add Tide Component didn't offer it, though giving
  it to a prefab's TideEntity is what gives an entity it (owner: "should i not see
  this as a component in unity?"). The view packet's sections are still the stored
  ones'. Test: `embed_schema` (`Unplaced`, which no entity has, is a component with
  its defaults).
  Details: `notes/language.md`.
- **`tide unity [folder] -o <path> [--once]`** (`compiler/cli/build.c`): the
  embed host library (`tide_embed`, the installed platform library and engine,
  exporting the ten `tide_embed_*` functions), then `game-<n>`/`pack-<n>`
  numbered on from the folder, and `schema.json`, read from each new build
  through its exported `tide_unity_schema`. Ends when its input closes; a
  `restart` line starts the running game over. `notes/cli-and-packages.md`.
- **The package** `unity/com.tide.unity` (Unity 6000.0+): `Tide.Runtime`
  (`TideRunner`: loads a copy of `Library/Tide/tide_embed` through function
  pointers, frames with Unity's input from the Input System or the Input
  Manager, reads the packet, shows entities by view rules (Tide prefab name or
  component), places them by Tide's `Transform` under their parents, sends
  commands and requests; `TideEntityView.Get<T>`), `Tide.Editor` (Project
  Settings > Tide; `TideToolchain` finds tide by setting, PATH or the
  installer's folder; `TideBuildService` runs `tide unity` while the editor is
  open; `TideSchemaWatcher` forces a clean script compile when the schema hash
  changes, as Unity's build cache doesn't see the file), and `Tide.Game`,
  which the source generator (`unity/generator`, built to
  `Game/Tide.Generator.dll`, labelled RoslynAnalyzer in its meta) filled with a
  struct for every schema type (until 2026-10-22: tide writes it now). The generator reads
  `Library/Tide/schema.json` from the folder Unity compiles in: Unity forwards
  `.additionalfile`s only from `Assets`. The game folder defaulted to
  `Assets/Tide`, as tide compiles every `.c` in a game's folder; since
  2026-10-21 the game is all of Assets (The game is Assets, below). Every
  package file has its `.meta`, which a git package can't make.
- **`unity/Sample`**: Bounce (balls in a box, WASD/stick, Space), its scene and
  ball prefab (`Assets/Editor/SampleSetup.cs` makes them), `BallTint` reading
  the generated `Tide.Game.Ball`, and a play mode test.

Tests: `tide_platform_embed` (eight cases: frames, input, requests, flags, bad
frames, packet bytes incl. text/list/owner flags and a blended singleton,
schema, commands and named scenes); every one of the repo's 324 games built
with `--embed` compiles under `-Wall -Wextra -Wpedantic -Werror`, and their
headers match plain builds; the sample's play mode test, headless with a scratch
install: `tide unity` builds it, Tide runs Main in Unity's frames, the ball is
this machine's, shown, coloured through the generated type and dropped onto the
floor by the match.

Gaps known then: Tide drawing in Unity (phase 5), authoring and prefab identity
(phase 3, built below), the web path's package side (option B is proven, not
packaged), player builds (built below, desktop), commands holding text or
lists, scenes C# can start being those the game's code starts,
`Clipboard.Copy`/paste, and tide's own log lines from the embed host reaching
Unity's Console (the build service's do).

### Authoring as built (2026-10-20)

**The component.** `TideEntity` (`Runtime/TideEntity.cs`, its meta's GUID
fixed: `4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e`, which tide finds it by) holds
`components` (each a Tide type name and its values as a Tide literal's fields,
`"speed = 1.5, color = Color(1, 0, 0)"`) and `references` (GameObjects; an
entity field's value `@n` names `references[n]`'s entity, `this` its own). The
entity handle struct the generator maps entity fields to was renamed
`TideEntityId` for it. The inspector (`Editor/TideEntityEditor.cs`) lists the
schema's components (Add Tide Component; not `Transform`, which is Unity's,
`UnityEngine.*`, nor scenes' tags: membership is the Unity scene the object is in,
and `Main` was offered until 2026-10-21), draws each field by its schema type (numbers, vectors, `Color`,
`quaternion` as Euler angles, `bool`, `string`, enums, entities as object
fields; anything else as its literal), shows an unset field with its declared
default from the schema's `defaults` (a text field's default shows empty), editable
like a set one (it used to be faded, which read as read-only, and showed zero),
and a right click on a field has Reset to Default (and Revert to Prefab in an
instance). Each component's header is a bold foldout with a ⋮ menu (or a right
click): Reset (every field), Revert to Prefab (an instance's component the
prefab has) and Remove Component. The toolbar bar, the Remove and `×` buttons,
the raw "As tide reads it" lists and the Play-mode label went on 2026-10-21
(owner: simpler; the graying is enough). In a prefab instance it writes only
the fields the instance changes (`Inherited` merges the chain of sources, nested
prefabs and variants included), as tide merges. Reset there leaves out a field
nothing under it sets (or sets to the default) and writes the default over one
the prefab sets to something else (a text field it can't, its default not being
in the schema's bytes); Revert to Prefab leaves the field out, and an instance
left setting nothing has its override reverted (`prefabOverride = false` in
the one `ApplyModifiedProperties`, as a field's edit is: `RevertPropertyOverride`,
first, saved a variant asset itself, and the owner's Console then showed Quick
Search's indexer warning "Build asset version error ... modification time",
its import racing a second save; Unity rescans and recovers, and batch mode
can't show the interactive editor's saves, so whether the change silences it
is unconfirmed). `Assets/Tests/Editor/EntityEditorTests.cs` (the Sample's
EditMode tests, through `InternalsVisibleTo`) pins both on a prefab inside a
prefab with overrides at each level, on a prefab asset itself, and on a
variant asset reverted and saved. In
Play it's grayed out: the match has its entities already, and an edit there
would reach neither the match nor the scene (owner, 2026-10-20: what can't be
changed at runtime is grayed out).
Showing the match's live values there instead is a possible next step.

**The converter** (`compiler/cli/unity_assets.c`, owner decision: Unity assets
are the source of truth, baked at tide time with no files in the project):
`.meta` files give GUID → asset; prefabs and scenes holding the TideEntity GUID,
themselves or through prefabs they instance, are read (binary serialization is
an error naming the setting). Expansion follows Unity: GameObjects, Transforms
(TRS, `m_Children` order), TideEntity MonoBehaviours; each PrefabInstance
expands its source (32 levels at most), applies `m_Modifications` (`m_Name`,
`m_IsActive`, local TRS, `components.Array.size`/`data[i].type`/`.values`, the
values merged field by field, `references.Array.size`/`data[i]`),
`m_RemovedComponents` and `m_RemovedGameObjects`; objects inside an instance
get Unity's IDs in the outer file, `(instance ^ id) & 0x7FFF...`, so outer
files reach objects nested deeper; stripped documents, added GameObjects and
components, and `m_TransformParent` attach what the file adds. Roots follow
SceneRoots, else file order. Emission: a prefab per asset, named after its file
(two of one name are an error); a scene's roots go under an entity of its own,
which has no Transform. Inactive GameObjects are left out with what's under
them; a GameObject without TideEntity passes its placement on (composed TRS).
Entity names are the GameObject's, sanitized (letters, digits, `_`; keywords
and leading digits get `_`), a second sibling of one name `_2`. Each entity
gets `Transform` (Unity's, with a TideEntity `Transform` component's fields
over it) and `UnityEngine.Source { asset, node }`. References resolve to entity
paths, `this`, or an error (an object without TideEntity, an asset, the root
from below).

**Prefab identity** (owner decision 3): `UnityEngine.Source` is ordinary match state
declared by the converter's own source, so snapshots, hashes, late joiners and
other builds of the same sources have it with no compiler change; the packet's
prefab column stays unused. The map `unity.json` gives each asset's GUID, path,
whether it's a scene, and each node's GameObject path (names from the root,
`Name#k` for the kth of a name among siblings).

**Binding** (`TideRunner`): an entity with `UnityEngine.Source` shows through its
own GameObject: a scene's (found by path in the open scene of that path), or for
a prefab's root, an instance of the Unity prefab (by GUID: the runner's
`prefabs` links in a player, `AssetDatabase` in the editor), whose descendants
show the prefab's other entities (found up the packet's parents). Authored
GameObjects stay where Unity's hierarchy has them: their placement is Tide's
under the parent entity's GameObject, through what's between; gone, they're
deactivated rather than destroyed. Others use the view rules as before. The
map is reloaded with each schema and when the file changes.

**Identity and pooling (2026-10-21, owner: "lets deal with that gap and lets
allow pooling").** Views were keyed by handle alone, and a rollback can give a
handle to another entity (a predicted spawn where the server had another:
notes/ecs.md, born), which kept the first entity's GameObject, maybe of
another prefab, for the second. Now a `--embed` build always keeps what each
entity was born as (`entities_born` is true for it: a hash per spawn, beside
the world, not in the hash or the header, so the game's hash is unchanged), the
view packet's entity row carries it (`born`, 24-byte rows, packet version 2),
and `TideEntityView` keeps the born and `UnityEngine.Source` it was made for:
`TideRunner.Same` remakes the view when either changes (born only where both
are known; 0 is a world another machine sent). A game with visibility state
borns from tick and handle, which doesn't tell two spawns of one tick and
handle apart; the Source check still tells their prefabs apart. Releasing goes
through `Release`: a scene's or an instance's GameObject is deactivated; one
made from a prefab (`MadeFrom`, a view rule's or a Unity prefab's root) goes to
`TideRunner.Spawner` (`ITideSpawner.Make/Release`, a game's own pool) if set,
else to the pool when `pool` is on (inactive under "Tide pool", the runner's
child, by prefab; taken back out to the active scene, reset by the game in
`OnDisable`/`OnEnable`), else Destroy. `OnDisable` hands shown views to the
spawner and destroys the pool, and no longer destroys scenes' GameObjects.
Tests: `tide_platform_embed` (the row's born is known and stays), the sample's
`Toss` command (an unowned Roller in and out) with
`ThePoolKeepsAGameObjectForTheNextEntity` and
`ASpawnerMakesAndLetsGoOfGameObjects`. Not exercised end to end: a real
rollback handing a handle over, which needs two machines in the sample's test.

**Generated types and Unity's build cache** (the generator described here was
replaced on 2026-10-22 by C# tide writes: Components of their own, below). Unity 6 caches compiles in a
shared cache keyed by an assembly's inputs (`Csc ... [CacheHit ...]` in the
log), and a file in `Library` isn't one: deleting `Library/Bee` still restored
a stale `Tide.Game.dll`, and neither a package `csc.rsp` nor `.additionalfile`s
in `Assets` reach a package assembly's compile. So `Tide.Game` moved out of
the package into `Packages/com.tide.game`, an embedded package tide writes
(asmdef, `schema.json`, a stub naming the hash); the generator DLL moved to
`Generator/` (no asmdef: it runs for every assembly and emits into `Tide.Game`
alone) and reads the schema there first. Checked: adding and removing a
component each recompiled (`CacheMiss`). The watcher now only refreshes the
asset database when that schema changes. **Provisional (my pick, for the
owner):** one generated folder in the project, meant for source control so a
fresh clone compiles before tide runs; without it, scripts using the game's
types fail to compile on a fresh clone, and Unity's Safe Mode would keep the
package from ever building the game.

**Diagnostics.** The build service gives tide's messages the asset they name
as their Console context (a `.tide` file, or a prefab or scene, whose line is
of `Library/Tide/prefabs/<asset>.tprefab`).
A diagnostic's lines (source line, caret, notes: indented) are one Console
entry. The last build's errors persist as Unity's compiler errors do, until a
build succeeds (`SessionState`, so a script reload keeps them): logged again
when the Console's count drops (cleared, by hand or on Play or recompile;
Unity's internal `LogEntries.GetCount`, through reflection, and nothing if it
isn't there), and Play is refused while they last, with a Scene view
notification (`TideBuildService.HasErrors`). Errors coming after a result line
start the list anew, as tide's stdout and stderr can interleave. Checked by hand
in batch mode (a broken file, the Console cleared, Play attempted).

**Opening the file** (owner, 2026-10-21: a double click opened
`TideBuildService.cs`, the C# that logged it). A diagnostic naming a file and
line goes in through Unity's internal `Debug.LogCompilerMessage` (reflection;
in 6000.6 `(message, file, line, column, forEditor, isError, identifier,
EntityId)`), as the C# compiler's do: the entry has the file (the project's
path: `Assets/Tide/X.tide`, or for a prefab or scene the `.tprefab` text tide
read) and line, and a double click opens it there in the external editor, with
no C# stack. Ours carry the identifier `0x74696465`; a build's first diagnostic
(`said`) and a build with nothing to say take the last build's away
(`Debug.RemoveLogEntriesByIdentifier`), so the "the game's errors are fixed"
line is left only for errors that went in as ordinary logs (no location, or a
Unity without those functions, which then logs as before). Re-logging on a
cleared Console or refused Play removes ours first, so none is there twice.
`Assets/Tests/Editor/ConsoleTests.cs` logs one and reads the entry back
(`LogEntries.GetEntryInternal`): its file and line, and its removal. In the
owner's open editor (through the Unity CLI's Pipeline package), two restarts of
tide's build each logged the error again with the Console's count unchanged,
and Clear left exactly one (the stacking the owner saw was the package before
this, whose ordinary logs nothing removed). What the double click opens for a
`.tide` is the external editor's choice; not seen.

Correction (2026-10-21, owner: a two-prefabs-of-one-name error came with a C#
stack). A message naming a file but no line (`Assets/X.prefab: error: ...`)
didn't match `located`, so it went in as an ordinary log, which Unity gives the
package's own stack. The pattern takes the line and column as optional now; one
about a whole prefab or scene goes in located at the asset itself (line 0), so a
double click opens it and the Console selects it. Ordinary logs, for a message
naming nothing, are `Debug.LogFormat(..., LogOption.NoStacktrace, ...)`. Test:
`ConsoleTests.AnErrorAboutAnAssetOpensItAndNoneHasACSharpStack`.

Correction (2026-10-21, owner: opening a `.tide` file didn't use the default
IDE). Unity gives a file to the external code editor's package
(`CodeEditor.CurrentEditor.OpenProject`), and Rider's and Visual Studio's
packages open only their own extensions and those of Editor Settings'
"Additional extensions to include" (`EditorSettings.projectGenerationUserExtensions`,
`m_ProjectGenerationIncludedExtensions`), returning false for the rest, so
Unity fell back. `Editor/TideCodeEditor.cs` adds `tide` and `packages` there
once, at the first editor update after a script load, and syncs the editor's
project, so `.tide` files and `tide.packages` open there from the Project
window, the inspector's Open and the Console, and are in Rider's solution. It
changes a project setting (`ProjectSettings/EditorSettings.asset`), as a user
would in Editor Settings. Test: `IconTests.TideFilesOpenInTheCodeEditor`
(listed once each, the rest kept). The owner's editor is Rider (2025.1).

**The Console is the user's** (owner, 2026-10-20): logging intrudes and needs a
good reason. tide's errors and warnings go there; its progress ("building ...
whenever its files change", "built game-N") doesn't, which Project Settings >
Tide's status shows instead, nor a player build's output unless it warns. The
one other line is "the game's errors are fixed", as the errors before it stay
in the Console. Unity's internal sticky entries (`Debug.LogSticky`,
`RemoveLogEntriesByEntityId`, keyed by `EntityId` in 6000.6) could take fixed
errors away instead; not tried. (Since 2026-10-25 builds are a status-bar task,
and errors naming no file go as compiler messages too, so the fixed line is
gone: Closing the gaps, below.)

Tests: the sample's ball is `Assets/Sample/Roller.prefab` (TideEntity with
`Ball`; since 2026-10-22 a `Ball` component of its own), spawned by `Spawn(Roller, ...)`; `Bounce.unity` has two pillars named
Pillar under a plain `Pillars` GameObject, each a `Spinner`, loaded by
`Scene.Load(Bounce)`. The play mode test checks the ball as before, and that
both pillars show their entities with the scene's speeds, at Unity's
positions, turned by Tide.

**Fields the component no longer has (2026-10-21).** Owner: removing a field
from a component gave each prefab's and scene's error twice (said once now:
`notes/language.md`, A diagnostic is said once). The owner's decision, after a
red inspector row with Remove and then a warning were tried the same day: it's
no error and no warning. `check_literal` (`compiler/src/check.c`) takes a Unity
prefab's or scene's value of a field its component hasn't (`unity_authored`:
its source is named `.prefab` or `.unity`) out of the literal and says nothing,
as Unity leaves out a removed C# field's value; the inspector draws only the
component's fields, so it isn't shown. The value stays in the asset, so adding
the field back brings it back. Code that still uses the field fails as any
unknown member does. `.tide` and `.tprefab` text keeps the error: written by
hand, an unknown field is a mistake. Test: `tidec_prefab_error_once` (a
`.prefab` builds saying nothing of it; the same text as a `.tprefab` fails with
the error once).

Gaps: unsaved scene and prefab edits (tide reads saved files), `Name#k` paths
when names include `#`, and gizmos (physics shapes' since: Packages' components in Unity, and 2026-11-19, top). (Asset fields aren't one: a match component
can't hold a rendering resource, "rendering resources belong to this machine,
so the match can't use them", and a `Mesh` can't be a field, so nothing of a
component is a Unity asset; tidels renames reach Unity assets, Tooling below.)

### Components of their own (2026-10-22)

Owner: "could we get a unity component instead? how reliable would baking
actual files be? we can keep them in the package you create", accepting that
renames and deletions are handled by refactoring tools as in Unity itself; then
"if the components are their own components now why can't I access them via a
reference directly and use their values?" and "yep that's what I imagine is the
best" to live values. Defaults: "Unity's way" (a component's serialized values
are what's baked; a declared default changing later doesn't reach a component
already added, as a C# initializer's doesn't).

- **tide writes the C#** (`compiler/cli/unity_code.c`, called by
  `write_unity_package` in `build.c` after `schema.json`; the Roslyn generator,
  `unity/generator` and `Generator/Tide.Generator.dll`, is deleted, and the old
  hash stub `TideGameSchema.cs` is removed from projects). Into
  `Packages/com.tide.game`, each file only where its bytes change:
  `Components/<namespaces>/<Name>.cs` per authored match component (not a
  scene's tag, not `UnityEngine.*`, not `Transform`), a file each as Unity finds
  a MonoBehaviour's script by its file name, with a `.meta` whose GUID is the
  first 16 bytes of SHA-256 of `"tide component " + name` (`unity_code_guid`),
  so prefabs keep it across machines and builds; `TideGame.cs` (`TideGameSchema`
  with the hash, game, tick rate, scenes and prefabs;
  `TideGameTypes.Register()` (`TideGameComponents` until the same day, below) at `SubsystemRegistration`; the game's enums;
  a layout struct for every other type); and `components.json`, how each
  component's fields are serialized, which the converter reads. Stale component
  scripts go (`sweep_entry`). Generating in tide rather than in the editor
  avoids a chicken-and-egg: a project whose scripts use the game's types
  doesn't compile until they're there, and batch mode exits on compile errors.
- **A component** is `[AddComponentMenu("Tide/<name>")]`,
  `[DisallowMultipleComponent]`, `sealed partial class X :
  Tide.TideComponent` (`Runtime/TideComponent.cs`: `[RequireComponent(typeof(TideEntity))]`,
  `TideName`, `ReadLive`), its fields with their declared defaults (from the
  schema's default bytes; a text field's from `print_outline`'s new `text`, as
  written between its quotes), and a nested `Value` struct laid out as the C
  is. Field forms (`plan_of`): numbers, `bool`, vectors as `Vector2/3/4`,
  `Vector2Int/3Int`, `Quaternion`, `Color`, text as `string`, entities as
  `GameObject`, enums (size 4) as the generated enum, lists of those plain
  types as `List<T>`, and anything else a string holding a Tide literal
  (`literal`, with a tooltip naming its type; empty is its default). Names that
  clash with `Value`, `TideName`, `ReadLive` or the class get `_`; C# keywords
  `@`, which Unity's YAML key leaves out.
- **Live values** (`TideRunner.Live`, after `Show`): for each packet component
  with a registered type, each shown entity's GameObject gets it (added where
  it hasn't) and `ReadLive` writes its fields from the packet; components the
  entity no longer has are destroyed. Only while `TideTypes.Hash` (`TideComponents` then) is the
  packet's schema hash, so stale C# never misreads. Writes in a script stay
  Unity's; commands reach the match. `TideComponentEditor` grays them in Play
  and repaints. `TideRunner.GameObjectOf(entity)` gives entity fields.
- **The converter** (`unity_assets.c`) loads `components.json` beside the
  project's Assets (`read_components`; its stamp is the game's) and reads each
  MonoBehaviour whose `m_Script` GUID is a component's (`read_component`):
  fields flattened into property paths (`velocity.x`, `marks.Array.data[2]`,
  Unity's hex for a list of numbers decoded by `elementType`'s size), entity
  fields as references of the component's own, resolved with the TideEntity's.
  A node's own components are kept apart from its TideEntity list (`owned`,
  slots never move, so pending references keep their index). An instance's
  `m_Modifications` set paths on them (`ident.what` 3), `m_RemovedComponents`
  takes one away. Emission writes each field Unity serialized as Tide text
  (`put_form`: numbers as written or `number`, `Color(r, g, b, a)`, enum
  members by value, `this`/paths/`default` for entities, lists in `[]`); a field
  with nothing serialized keeps its default. A script GUID the game hasn't, and
  a key its component hasn't, are left out silently (owner's rule above).
- **Converting (retired 2026-10-26):** the one-time editor tool converted the
  original TideEntity lists to generated Unity components. It and its tests
  were removed at the owner's request; see Retired list conversion tool above.
  The entity inspector's Add Tide Component still adds the component itself
  where tide made one (`TideEditorSchema.ComponentType`).
- **Renames** (tidels, `unity_renames` in `compiler/lsp/analysis.c`): a
  component's rename gives its objects the new name's GUID and
  `m_EditorClassIdentifier`; a field's renames its key in those objects and an
  instance change's `propertyPath`, the target found through the prefabs
  instanced (`unity_ids`: each asset's component object IDs, instances' as
  `(instance ^ id)`).

Tests: `unity_components_as_tide_components` (`compiler/cli/tests/unity_components`,
hand-written as Unity writes: every form, a hex list, a removed component and a
removed field left out, a scene instance changing a list element, a color
channel, an entity to the scene's own and removing a component);
`unity_code_writes_the_samples_package` (tide writes the Sample's checked-in
package byte for byte, line endings aside, and sweeps a stale script) and
`unity_code_guid_is_the_names`; `lsp_rename_reaches_unity_assets` (the GUID,
class identifier, a key, an instance's `propertyPath`, not another component's
field of that name); the now-retired conversion tests covered a prefab and a
prefab holding it, and PlayMode tests read Ball and Spinner through
their components, live. The Sample was converted by the command (4 components).
Gaps then, bridged the same day (Every type in C#, below): asset fields, `char` lists and structs as literals, a
field whose C# name had `_` added for a clash not renamed by tidels, the owner's project not converted.

### Events, commands and singletons in C# (2026-10-22)

Owner, after components of their own: "this should make views trivial right? what about events and commands"; my
recommendation (C# hears what local handlers hear, as predicted and as verified; singletons live; text commands;
a GameObject as a command's target): "yeah I think that makes sense yes".

- **Events.** tidec `--embed` taps every match event of the game's own with a predicted and a verified local
  handler (`notes/language.md`, Events, text commands and command defaults), and the view packet's EVENTS section
  carries what they heard. tide writes each event as `sealed partial class E : Tide.TideEvent` (fields read live:
  text as `string`, entities as `GameObject`, plain lists as `List<T>`, a struct field as its value; maps and grids
  left out) with `Tick`, `Verified`, `Target` (the GameObject the entity it was sent to shows, or null) and
  `TargetEntity`. `TideRunner.On<E>(Action<E>)` hears each as soon as this machine has it (a correction can turn it
  out not to have happened), `OnVerified<E>` once the server confirmed its tick, `Off`/`OffVerified` stop; a
  `TideEntityView`'s `On`/`OnVerified` hear those sent to its entity. The runner tells them after the frame's
  components are live (`Hear`, after `Live`), before scripts' Update; a handler's exception is logged and the rest
  run.
- **Singletons** are `sealed partial class S : Tide.TideSingleton`, live like components (`LiveSingletons`), their
  local fields too: `TideRunner.Get<S>()` gives the runner's, null outside a match. `TryGetSingleton` reads the
  nested `S.Value` as before. Internal ones (`tide_*`) stay structs.
- **Local fields** of a component or singleton are its class's `[NonSerialized]` fields, read from the packet's
  local sections; the inspector shows them in Play under "Local fields" (`TideComponentEditor`, by reflection).
  The converter never sees them: `components.json` lists the match fields alone.
- **Commands** are `sealed partial class C : Tide.TideCommand`, starting at their declared defaults (the schema's
  `defaults` for commands now), `Pack` writing their `Value` through `TideCommandWriter` (`Text`, `List`, `Entity`
  of a GameObject); `TideRunner.Send(C, GameObject target = null)` and `Send(C, Component target)` send them to the
  match or the entity the target shows, as local code's `Send` and `entity.Send`. `Send<T>(T value)` of a raw
  `Value` stays for plain commands. False outside a match, for text that isn't UTF-8, or a command too big.
- **The registry** is `Tide.TideTypes` (was `TideComponents`): components, singletons and events by Tide name,
  registered by `TideGameTypes` in `TideGame.cs`, which holds every non-component class; the packet reader takes
  version 3 and lists EVENTS (`TidePacket.HeardCount`, `Heard`, `HeardValue`).
- **The Sample**: `event Bounced { float speed; }` sent to a ball as it bounces, `local int heard` on `Ball` that a
  local handler counts, `command Say { string text; }` kept in `Extras.said`, and `command Paint { Color color; }`
  handled per ball. PlayMode tests: `EventsAreHeardAsLocalHandlersHearThem` (heard, verified and the ball's view's
  counts equal, the target is the ball, the local field equals them) and `CommandsCarryTextAndGoToGameObjects`
  (Room live, a non-ASCII `Say` comes back in `Extras`, `Paint` sent to the ball's GameObject colors that ball).

Gaps then, bridged the same day (below): events' and singletons' map, grid and heap-holding list fields left out
of the classes; a component's struct fields Tide literals in Play. (Commands held lists of bytes alone then: Tide's rule. Since 2026-11-04 they hold lists of any plain value,
`notes/netcode.md`, Commands holding lists of values; `tide_embed_command_list` takes them, untested from C#.)

### Every type in C# (2026-10-22)

Owner: "lets bridge any gaps".

- **The packet carries every container** (`notes/language.md`, Containers in the view packet): lists of anything,
  maps, sets and grids, each an entry of STRINGS with what it holds the packet's handles too (packet version 4, every
  entry 8-aligned). A map's or set's field keeps the handle in its first word (`Tide.TideMap`, 24 bytes), a grid's
  as `Tide.TideGrid`.
- **C# has every type** (`unity_code.c`, a type tree parsed from the schema's type strings, `parse_ctype`): scalars
  and vectors as before, text as `string`, an entity as its `GameObject` (as a key, its `TideEntityId`, as
  GameObjects come and go), an enum as itself, a struct as its own `[Serializable]` C# struct (`write_struct`: its
  fields, `From` of its laid-out `Value`, `Pack` for a command), `List<T>`, `Tide.TideSet<T>` (insertion order,
  serializable), `Dictionary<K, V>`, `Tide.TideGrid2<T>`/`TideGrid3<T>` (the chunks the grid has, indexers, `Size`)
  and arrays as `T[]`. `TideRead` (Runtime) turns a packet's entries into them; the generated conversions are
  lambdas that take nothing from around them, so C# makes each once.
- **Authored in Unity**: a component's struct fields, lists of anything but containers, sets of scalars, text or
  enums, arrays and `char`s are serialized and baked; a map or a grid (no map literal but `[]` in Tide) and a
  container of containers are `[NonSerialized]`, live in Play only. `components.json` describes each field's
  shape (`"shapes"`, by Tide type: its form, a struct's fields, an element's shape, an array's length), which the
  converter reads recursively (`flatten_shaped`, `put_form`): a struct as `Hold { mass = 12, owner = Pilot }`, a set
  from its `items`, an array of exactly its length, a `char` as `'A'` (`put_char`: Tide's escapes, `char(n)` for a
  control character), an entity anywhere in it a reference of the component's own, and an instance's change of any
  of them by its property path (`shape_at_path`). A component's struct field's declared default is its C# initializer
  (`new Trail { length = 0f, ... }`).
- **Renames** (tidels): a struct's field is renamed wherever an authored component holds the struct, through
  structs, lists and arrays, in YAML keys (followed by indent, `- ` items as "[]") and instance changes'
  `propertyPath`s (`unity_struct_paths`, `unity_path`); keys are computed as unity_code.c names fields (`unity_key`:
  `Value`, `TideName` and the other members, or the type's own name, get `_`).
- **Assets**: none to bridge; see the Authoring gaps above.
- **The owner's project** was converted by the command in batch mode (6 components in 4 prefabs and scenes, none
  left listed, after a copy of its Assets, Packages and ProjectSettings), and tide bakes the same values from it.
- **The Sample**: `Trail trail` on `Ball`, `char badge = 'S'` on `Spinner`, `Set<PlayerID> speakers` and
  `Grid2<byte> board` in `Extras` (filled by `Say`). Tests: PlayMode checks the set's count, the grid's size and the
  cell `Say` counted, a cell nothing set, the room's `Dictionary` of balls whose value is the ball's GameObject, the
  pillars' `badge`; the retired conversion tests also covered struct literals and chars.
  `unity_components_as_tide_components` covers every shape, nested entities and instance changes of them;
  `lsp_rename_reaches_unity_assets` a struct's field in a struct, a list's element and an instance's change, and a
  `Value` field's `Value_` key. Every one of the repo's 330 games built with `--embed` compiles under `-Werror`
  (which found unused command helpers in four games with text or byte-list commands: `TIDE_HELPER` now).

### Tide types and their C# twins (2026-10-26)

The owner asked for the seams between Tide's types and their C# twins closed, Tide staying the source of truth (every
twin still written into `Packages/com.tide.game` by `unity_code.c`): renames reaching C# scripts, a twin leading back
to its Tide declaration, and C# errors that explain a Tide rename. **Owner** (2026-10-26, hub card `mvp14nl24360`):
"File only, line looked up live (your pick)": the generated C# names the declaration's file, not its line, so a line
moving above a declaration rewrites no C# (a recompile and domain reload, in Play too, on most saves); the line comes
from `Library/Tide/outline.json` when Unity opens the declaration. Everything else here is my pick (Provisional).

- **One naming, shared** (`compiler/lsp/unity_names.c`, compiled into tide and tidels as `json.c` is): the C# name of
  every type (namespace parts after `Tide.Game`, an identifier C# takes, `Local` after a local part, `_` after a
  second of one name in one namespace, in schema order), field (`_` after the type's own name and, for a class or a
  struct of Unity code's, `Value`, `TideName`, `ReadLive`, `Pack`, `From`, `Tick`, `Verified`, `Target`,
  `TargetEntity`; a class's local fields after its own, among them) and enum member (a second of one identifier left
  out), from the schema's names. `unity_code.c` takes every name from it (its own `identifier`, `taken` and `reserve`
  are gone); the Sample's package came out byte for byte the same.
- **Renames reach C#** (`compiler/lsp/csharp.c`, from `unity_renames` in `analysis.c`, for a type, a field or an enum
  member of a component, singleton, event, command, struct, input or enum, and a namespace): reads the project's
  `Packages/com.tide.game/schema.json`, names everything as the C# has it and again with the rename (`unity_cs_assign`
  over the changed Tide names), and edits what changed in every `.cs` of `Assets` (not in folders ending `~` or
  hidden), of embedded packages (`Packages/*`, but the one whose package.json is com.tide.game) and of local ones (the
  manifest's `file:` dependencies, `..` folded so the URIs are the editor's). The edits join the rename's
  WorkspaceEdit, so the editor previews and undoes them with the rest. The C# is lexed (comments, strings, characters
  and preprocessor lines skipped; an interpolated string's holes lexed as code, up to a format or an alignment) and
  read conservatively, with no compiler, in three passes over the project's files (its types' names; their members;
  the edits):
  - a type where a dotted name resolves to `Tide.Game.<twin>` as written, through `global::`, an alias's target, or a
    namespace the file opens (its usings, its namespaces and those above them); not where the first name is a type
    the project declares, a variable in scope there, or assigned (`Mode = ...`); a single name the project also
    declares as a variable or member anywhere only where only a type can be (after `new`, `is`, `as`, `<`, `typeof(`,
    or before a declared name). Only a name written as the type's own is edited, not an alias's. A namespace (Tide's
    `Combat` is C#'s `Tide.Game.Combat`) likewise, in a using, an alias's target, a qualified name or one under a
    namespace the file opens (`Combat.Blade` after `using Tide.Game;`); not in a `namespace` declaration, which is the
    project's own. A namespace's rename moves each component in it in the assets too (`unity_move`: the new name's
    script GUID, `m_EditorClassIdentifier` from where the old and new class names part, a TideEntity list's
    `- type:`), as a component's own rename always did; `unity_rename` now carries a list of moved components.
  - a member where its value's type is known: a variable, field, property or parameter declared of a twin (in scope: a
    type's body, the rest of a block, a method's or lambda's body, a foreach statement; two of one name in one scope
    of different types are neither), `var` given `new`, a generic call or another known value, `out var` in a generic
    call, a lambda's parameter in a generic call (`On<Hit>(hit => ...)`), a generic call's type argument
    (`GetComponent<T>()`, `runner.Get<T>()`; an indexer or a foreach over it its element), `new T { a = ... }`'s
    initializer, a cast (`((Health)x).hp`, `var h = (Health)x;`), `as` (`(x as Health).hp`), a pattern's variable
    (`x is Health h && h.hp > 0`, in scope through the if's block), an enum's member after its enum, and on through the
    twins' fields, lists and arrays (`h.trail.length`). A class's members are its fields and its local fields; its
    `Value`'s, its fields.
  - the project's own classes and structs (`user_class`: by their names, so partial ones across files are one, and
    classes of one name in two namespaces too, a member two of them type differently left ambiguous): each field's,
    property's and method's type, so a member reaches through `state.Current.hp`, `Find().hp`, `state.Pick().hp`,
    `GameState.Instance.Current.hp` (static, by the class's name), and a bare or `this.` name in a class finds its
    members declared in another file of a partial class.
  - a doc comment's `cref` (`/// <see cref="Health.hp"/>`, `T:` and `global::` read): its namespace, type and member,
    as code naming them; other comment text never.
  - Test: `lsp_rename_reaches_unity_scripts` (scripts in Assets, an embedded and a local package, and com.tide.game's
    own script, never edited): a field through a variable, `var` of `GetComponent`, an interpolated string's hole, a
    foreach over `GetComponents`, a method's parameter, a qualified field; not Shield's or another class's `hp`, a
    comment's, a string's, nor `Samples~`'s; the type in a using alias, a field, generic arguments, `Tide.Game.` and
    `global::` names, not the alias's uses; a local field; `Value_` renamed to a plain name and a plain one to
    `Value_`; a struct's field through a component's field, not the project's own `Trail`'s, and the struct itself only
    where qualified, as the project has a `Trail`; a singleton's field after `runner.Get<Extras>()`, an event's in a
    lambda, a command's in an initializer (not a variable named `text`); an enum and its member; a namespaced
    component and its field by the namespace's using; a field through casts, `as`, a pattern's variable, a method's
    result, a static field and a partial class's field from its other file, and in a cref; the type in a cast, `as`,
    `is`, a method's result, a partial class's field and a cref; the namespace in a using, a qualified name and a
    cref (not bare `Blade`), and in a prefab its component's script GUID, class identifier and TideEntity list.
- **Docs and where** (`unity_code.c`, from the outline): `print_outline` (`compiler/src/outline.c`) gives a
  declaration's `doc` (the `//` lines just above it, attributes allowed between, a blank line or code ending them;
  slashes and one space dropped), each field's `line` and `doc` (above it, else a comment after it on its line), and
  an enum's `memberDocs`. The C# has a `/// <summary>` (XML escaped) on each class, struct and enum of the game's,
  its last paragraph "Tide's component Ball, declared in Assets/Tide/Bounce.tide." (`where_declared`: a game file
  from Assets, a package's as "the Box3D package's body.tide", none for tide's own in Library/Tide, which get no docs),
  and on each field and enum member with a doc. Tests: `unity_code_writes_tides_docs_and_where_each_type_is` (fixture
  `compiler/cli/tests/unity_names`), `tidec_outline_game` (docs above and after, two lines, a member's).
- **Opening the declaration** (`Editor/TideDeclarations.cs`): `[OnOpenAsset(0)]` (an `EntityId` from 6000.2, as
  com.unity.timeline's), for a script opened with no line that's a `TideComponent` whose `Value` has `[TideType(name)]`:
  the outline's component of that name, opened by `InternalEditorUtility.OpenFileAtLineExternal` at its line and column
  (`Opener`, which tests replace). A double click on the Script field, Edit Script and the Project window all come
  through `AssetDatabase.OpenAsset`. A script opened at a line (a C# error in it) opens as itself, as does one before
  the game has built. `TideOutline` reads `doc` and fields' `line` and `doc`.
- **Errors that explain a rename:** `unity_code_record` (tide, after each C# write, from `write_unity_package`) diffs
  the names of the schema the C# had (the com.tide.game schema.json it replaces) with the new one's: a type by name,
  else the one gone and the one come of its kind with the same fields, else the only ones of their kind; a field by
  name, else in its place with its type, else the only one gone and come of its type; local fields through their
  parts; an enum by name, else the only one; members by name, else in their place. Into `Library/Tide/renamed.json`
  (`{"renamed": [{"was": "Combat.Health.hp", "now": "Combat.Health.health", "file": "Assets/Tide/combat.tide",
  "line": 8}, {"was": "Shield", "removed": true}]}`, names after `Tide.Game.`): a new entry replaces one of its name,
  one whose `now` is renamed again follows it, one whose name the C# has again goes, the newest 100 stay, and a build
  of the declarations alone records no removals (it leaves some out). Written only when it changes; Library is never
  in source control. `Editor/TideRenames.cs`: at `CompilationPipeline.assemblyCompilationFinished`, each error saying
  a member (CS0117, CS1061), a type (CS0246, CS0103), a type in a namespace (CS0234) or in a type (CS0426) isn't there
  is looked up (`Tide.Game.` and `global::` dropped, `.Value.` as the class's field, else an entry ending in it, for a
  namespaced type written short), and a note goes in as a compiler message of its own identifier (`0x7469646e`), an
  error one, so it shows beside the errors it explains when warnings are filtered out: "tide: `Ball.speed` is now
  `Ball.velocity` (Assets/Tide/Bounce.tide:124)", located at the new declaration, or "tide: `Paddle` was removed from
  the game's Tide code" at the script's line; each once, all gone at `compilationStarted`. As the package loads
  (`delayCall`) it also explains the C# compile errors the Console has (`ConsoleErrors`, read from Unity's internal
  entries by `file(line,col): error CS`), which the editor's first compile at startup leaves before it could hear
  them. `TideBuildService.LogLocated`/`RemoveLocated` take the identifier. (A first probe in batch mode found no
  warning of any kind added to the Console, which looked like tide's own located warnings going nowhere: it was this
  machine's Console with its warning filter off (`consoleFlags` 1666, without `LogLevelWarning`), and
  `LogEntries.GetCount` counting what the filter shows. With the filter on, `LogCompilerMessage`'s warnings and
  `Debug.LogWarning` are counted. Nothing was wrong.) Test:
  `unity_code_records_what_became_of_names` (a field renamed in place, a local field, a struct by its fields, an enum
  member, a field whose type changed too removed; renamed again, back again, declarations alone).
- **Sample tests** (EditMode `TypeBoundaryTests`): Ball's script opens `Assets/Tide/Bounce.tide` at `component Ball`
  through `AssetDatabase.OpenAsset` (BallTint's isn't the game's); the regenerated `Ball.cs` has Ball's doc, its file,
  and `velocity`'s and `heard`'s docs; `GeneratedDocsAreWellFormedXml` compiles all of com.tide.game with `/doc` (its
  files' `#pragma warning disable` hides doc warnings, so the XML C# writes is read instead): no "Badly formed XML",
  and the summaries of Ball, `Ball.velocity`, Trail and Extras where they belong, as Rider and Visual Studio read them;
  a probe script of renamed and removed names compiled apart gets the three notes from Roslyn's own messages, each once
  in the Console, and none after `Clear`; a compile error already in the Console gets its note from `ExplainConsole`.
  Both compile with the Roslyn `csc.dll` Unity ships (`DotNetSdk`, a response file), synchronously: `AssemblyBuilder`
  is obsolete (CS0618) and reported after the test's wait once. The Sample's Ball and its `velocity` have doc comments
  to pin. `unity_code_writes_the_samples_package` passes the Sample's outline
  (`compiler/cli/tests/unity_sample/outline.json`, cut to Bounce.tide's types, from the Sample's
  `Library/Tide/outline.json`): copy it again with the Sample's package when Bounce.tide's docs change; the test
  says so when the package differs.
- **Gaps** (after a second round, 2026-10-26, the owner: "deal with what is left"): what C# itself can't tell without
  a compiler stays out: overloads and extension methods (a method's result is its name's, across the class's
  overloads, and none where they differ), inheritance (a member of a base class isn't followed to a derived one),
  generic type parameters, `dynamic` and reflection. A project type of a twin's name keeps bare uses of the twin
  unrenamed everywhere (qualified ones are), by design. Not seen in a window, as agent runs open none: Rider's and
  Visual Studio's hover itself (the XML they read is tested), the external editor opening at the line (the call to
  `OpenFileAtLineExternal` is), and the notes in an interactive Console (Unity's own entries are read back).
- **Runs:** native debug 2805/2805 and web debug 2710/2710 (without `private-prototype`), then the CLI, language-server,
  outline and Unity CTests again after the last `unity_code.c` change (21/21); docs build. The Sample with a scratch
  tide (this checkout's mingw-release, then debug, installed): EditMode 31/31 (three runs, the first regenerating
  com.tide.game with the docs), PlayMode 17/17 on D3D11. Second round (C#'s casts, patterns, own classes, crefs and
  namespaces; Console errors at load; the XML test): native debug 2805/2805, tidels 261/261, tide_cli_tests 67/67, the
  docs build, Sample EditMode 32/32. Web and PlayMode weren't run again (the owner: test what's necessary, CI catches
  the rest): tidels is native only, and the package change is editor code.

### Scenes as built (2026-10-20)

**Owner:** the match decides which Unity scenes are loaded, as a dedicated
server outside Unity must have them too; Build Settings' first scene mirrors
Unity's start; after it, the match's code loads scenes; in the editor, the
editor tells the match which scenes it has open, editor only.

- **The converter** (`unity_declarations`) adds to `Unity.tide`, when the
  project has scenes of Tide entities: `singleton UnityEngine.LoadedScenes { Map<int,
  Entity> loaded; PlayerID opener; bool heard; bool opened; }`;
  `event(Spawned) Begin(with Main, ...)` loading Build Settings' first enabled
  scene (`ProjectSettings/EditorBuildSettings.asset`, whose stamp is the
  game's too), unless the editor's command came first; `command OpenScene {
  int asset; bool only; }`, taken only from the match's first player (set by its
  `PlayerJoined`, or by the command when it comes first in the first tick):
  `only` unloads the scenes it loaded but that one, then it loads the asset's
  scene by a `switch` over the scenes. Order-independent: Begin then Open, or
  Open then Begin, end with the editor's scenes alone. Test:
  `unity_prefabs_and_scenes_as_tide_prefabs` (the fixture's Build Settings
  names Level first, after a scene that's off).
- **`tide build`/`tide run --assets`**: a server, or any build, of a game made
  in Unity has the same prefabs and this code (the prefab texts are written
  for `tide unity` alone, under its `-o`). Checked: `tide run --frames 60` and
  `tide build --server` of the sample.
- **The runner** follows the match: a scene asset's root entity in the packet
  loads that scene additively unless it's open
  (`EditorSceneManager.LoadSceneAsyncInPlayMode` in the editor, any scene;
  `SceneManager.LoadSceneAsync` in a player, Build Settings' scenes), and its
  going unloads it, waiting for a load the match let go of mid-way. Entities
  of a scene still loading show once it has. The runner keeps itself across
  loads (`DontDestroyOnLoad`, a root one), and a scene's own runner loaded
  while one runs removes itself quietly. In the editor, as this machine runs
  the match (`status.server`), it sends Open for each scene of Tide entities
  open as it started, the first with `only`; and Play from a scene of Tide
  entities with no runner makes one. The build processor warns of a scene of
  Tide entities missing from Build Settings.
- **The sample**: `Bonus.unity` (a Totem Spinner), loaded and unloaded by the
  `ShowBonus` command; Bounce.tide no longer loads Bounce itself. Play mode
  tests: the match loads and unloads Bonus with Unity; the editor's open
  Bounce and Bonus are the match's; Bonus open alone is the match's and Bounce,
  loaded as Main started, goes. Players checked as before.

**Unity scenes are Tide scenes** (owner, 2026-10-20): a scene asset `N` is
`scene N { }` in `Unity.tide`, its authored tree the prefab
`UnityEngine.SceneContents.N` (`namespace UnityEngine.SceneContents;` at the
top of its text), and `UnityEngine.Fill<N>` (`event(Spawned) ... (with
UnityEngine.Scenes.N)`) spawns that prefab into each loaded N, so every entity
of it is N's for `with scene N` (`notes/language.md`, Scene filters). The
sample's `Spinning` is `without scene Bonus`, and the play mode test checks
Bonus's totem stays still.

**Everything from Unity is under `UnityEngine`** (owner, 2026-10-20, so the
link is clear): scenes are `UnityEngine.Scenes.N`, prefabs
`UnityEngine.Prefabs.N`, a scene's tree `UnityEngine.SceneContents.N`, and the
converter's own declarations `UnityEngine.Source`, `UnityEngine.LoadedScenes`
and `UnityEngine.OpenScene`. Game code writes `using UnityEngine.Scenes;` and
`using UnityEngine.Prefabs;` for the short names. The entity editor's menu
leaves out `UnityEngine.` names.

**Named by their folders, short while unique (2026-10-21).** Owner: "i cant
have 2 prefabs with the same name, we should likely do something about this"
(the converter refused two of one file name). The owner picked folder names,
with the short name while it's unique, over folder names alone and over
references set in the inspector. The converter declares each prefab, scene
tree and scene in its folders' namespace (`asset_folders` in
`compiler/cli/unity_assets.c`: each folder under `Assets` a name as an
entity's is, so `Assets/Enemies/Goblin.prefab` is
`UnityEngine.Prefabs.Enemies.Goblin`, the Sample's `Assets/Sample/Bonus.unity`
`UnityEngine.Scenes.Sample.Bonus`; one in `Assets` itself keeps the short
name); a scene's Fill handler is named after the whole name, dots as `_`. The
checker's `find_in_folders` (`check.c`, called when `find_in` misses) makes a
name `UnityEngine.Prefabs`, `UnityEngine.Scenes` or `UnityEngine.SceneContents`
hasn't the one declaration of that name in their sub-namespaces, and nothing
for none or two: no copy is declared, so a prefab's errors are said once and a
scene is one scene. With two, the unknown-name error's new note
(`other_namespaces_note`, any unknown name) lists the declarations of that
name in other namespaces: "there's 'UnityEngine.Prefabs.Bosses.Goblin' and
'UnityEngine.Prefabs.Enemies.Goblin': write the one you mean". Only two of
one folder whose names sanitize alike (`My Level`, `My_Level`) are still
refused. Relative qualified names (`Enemies.Goblin` after `using
UnityEngine.Prefabs;`) aren't in the language. Tests:
`tidec_unity_folder_names` (`compiler/tests/unity_folder_names.cmake`: the one
Goblin by its short name and by `UnityEngine.Prefabs.Goblin`, a full name
beside another of its name, and the short name of two refused with both
named), `unity_prefabs_and_scenes_as_tide_prefabs` (the fixture's
`Assets/Fixture` names), and the Sample's play mode test, whose code uses the
short names.

**Scenes loaded by hand are local (owner, 2026-10-22):** "scenes that are
matches should be loaded via Tide; unity scenes being loaded manually should be
left alone and considered local/disconnected". A scene the match loads twice is
two of Unity's (2026-10-22, "Audio and the editor's workflow").

### Web as built (2026-10-20): option B, packaged

`tide unity --web -o <dir>` (once; `compiler/cli/build.c`, `tide_unity_web`)
builds the game, `--embed`, as a WASI program whose `main` is
`unity_web_main`: `tide_embed_create` with the linked game and the page's pack,
then `tide_platform_run`, its frame `tide_embed_frame` with the input the page
last copied in (a frame with none, from tide.js's worker while the page is
hidden, gets zeroed input), and `tide_unity_*` exports: `ready`, `input`,
`frame_info` (the screen; frames are timed by the page's clock), `status`,
`view`/`view_size`, `schema`/`schema_size`, `draw`/`draw_size`, `buffer` (where
the page puts a request's text or a command's value), `request`, `command`,
`background`. It writes `tide-game.js`: `Tide = {wait, pageFrames, embedded,
canvas: a detached one}`, `TIDE_PROGRAM`/`TIDE_PACK` as base64 (gzip in
release) and tide.js, and `unity.json`.

`tide.js` (`platform/web/tide.js`, release runtime regenerated): with
`pageFrames`, frames while the page is shown come only from `Tide.frame()`;
hidden, from the worker as before. `Tide.exports()` and `Tide.memory()` give
the page the program's exports and memory.

The package: `Runtime/Plugins/WebGL/Tide.jslib` starts the program with the
runner's arguments (`Tide.arguments`, `Tide.relay`, `Tide.start()`), and each
Unity frame copies Unity's input into the program's memory, runs
`Tide.frame()` and copies the packet, schema, status and draw packet into
buffers of Unity's heap it keeps (`_malloc`), which `TideLibrary`'s methods
return as the desktop calls do (TideRunner calls methods now, function pointers
on desktop, `DllImport("__Internal")` on WebGL). A web player can't read
`StreamingAssets` files, so players carry the map in the scene
(`TideRunner.map`, set as the scene builds). The build processor runs `tide
unity --web --once --release` for a WebGL target and puts `tide-game.js` in the
build with a `<script>` before `</head>` of Unity's `index.html`: no template in
`Assets`. Tide's threads need the page served cross-origin isolated (COOP
same-origin, COEP require-corp), as any Tide page.

Checked: `unity/Sample/web-smoke.mjs` serves a WebGL build of the sample
(`SampleSetup.BuildWeb`, Brotli as Unity builds it, served with its encoding)
isolated and opens `index.html?tide-smoke` in headless Chrome (muted, a window
of its own): PlayerSmoke saw the match running, this machine's ball through
the Unity prefab on the floor, and Tide turning the scene's pillars: PASS.

### Tide drawing in Unity as built (2026-10-20): the 2D first slice

`tide_embed_draw` (`notes/standard-host.md`) gives the frame's draw list;
`Runtime/TideDraw.cs`, which TideRunner adds while `drawTide` (on by default),
draws it over Unity's frame from `OnGUI` with GL (`Hidden/Internal-Colored`),
so in any render pipeline: rects, circles, their wires, lines, smooth shapes
(boxes and ellipses with their top-to-bottom fill and outline; arcs as part of
a ring; round corners, soft edges and turns not yet), 2D meshes' triangles
with their corners' colors times the tint, and text in Unity's font at the
anchor; under Tide's 2D cameras (a size is half the height seen; 0 a pixel a
unit, y up, the camera's center at the screen's) and in screen pixels after
`Draw.Screen`; alpha and additive blending. Skipped: clears (Unity's frame is below), render targets,
clips, stencil, textures, 3D meshes, shaded runs and blits. The sample's Hud
view draws its text, which the play mode test finds in the packet. The HLSL
target and ScriptedImporter for custom frags/verts, Tide's own font, and 3D
meshes through Unity's renderer remain.

**Unity's camera is the views' (2026-10-21).** Owner: a view drawing at
`Draw.WorldToScreen(worldPos)` drew everything in the bottom left corner. Three
causes: Tide's 3D camera was its own first one (at the origin facing +z), so
points about z = 0 had no projection and `WorldToScreen` gave (0, 0); TideDraw
put the 2D origin with no camera at the screen's bottom left, where Tide's first
2D camera centers it; and the view drew the screen position as a world one
(it's for `Draw.Screen() { ... }`). Now TideRunner sends its `viewCamera`, else
`Camera.main`, each frame before the frame call (`ViewCamera`: position,
rotation, vertical field of view; an orthographic one or none sends 0, Tide's
own), through `tide_embed_camera` (`notes/standard-host.md`; ABI 3, the
package's `TideLibrary.Abi` with it; on the web the `tide_unity_camera` export
and `TideWebCamera` in the jslib). Unity and Tide share axes and handedness
and both give the vertical field of view, so `WorldToScreen` lands where
Unity's camera sees the point. TideDraw's default 2D origin is the screen's
center. The sample's Hud used the old corner (`float2(16, Screen.height - 30)`
in world units), so it moved into `Draw.Screen()` at (16, 30), and a `Ring`
view draws a wire circle at each ball's `WorldToScreen`; the play mode test
finds the ring within 1.5 pixels of `Camera.main.WorldToScreenPoint` of the
ball (y flipped). Not done: an orthographic Unity camera (Tide's 3D cameras are
perspective ones), and Unity's near and far planes for a projected depth. (Both done a day later by the matrices of
Tide's renderer in Unity, below; an orthographic camera first run in Unity on 2026-11-19, top.)

**Stats and the runner's Arguments (2026-10-21).** The owner put `--stats` in
TideRunner's Arguments and saw nothing: the embed dropped every flag it didn't
take, and drew no stats. `--stats` now shows Tide's stats drawer, open, over
the game (`notes/standard-host.md`), as does the game's `stats` setting; TideDraw
draws it as any of Tide's drawing (its panel, text and lines). A word the embedded
game doesn't take gets a Console warning naming it and listing those it takes
(`TideRunner.UnknownArguments`; Sample's `ArgumentsTests`). Unity's own Stats
window has the rendering; Tide's drawer has the frame's work, ping, delay,
network, entities, threads and the slowest systems.

### Tide's renderer in Unity (2026-10-22): 3D, targets and shaders

Owner: "let's do the 3d drawing and shaders", then "lets deal with the not done
yet parts". Tide's own renderer (`platform.c`) draws the frame into Unity's
camera through `platform/src/gpu_record.c` (`notes/rendering.md`, Recording),
and the package replays its GPU calls. All of Tide's drawing comes through: 2D
shapes, smooth shapes, text in Tide's font, sprites, meshes, 3D primitives and
meshes, instancing, render targets with several colors, depth, cubes and
samples, stencil, clips, blits, and the game's frags and verts. It is the same
pixels the renderer draws for its own window. It works in the built-in render
pipeline and URP; on Direct3D 11 and 12, Vulkan, OpenGL and WebGL 2; in Gamma
and Linear projects; and the editor's scene views show the frame's 3D. The
OnGUI path (`TideDraw.cs`) remains only for a GPU `TideRender.Supported` refuses
(no constant buffers, or neither compute-class buffers nor OpenGL ES 3).
Correction 2026-11-05 (Compute in Unity, above): OpenGL ES 3 and WebGL 2 are
refused too now, compute being on every render backend.

- **Shaders:** `tide unity` (`compiler/cli/unity_shaders.c`, with `--assets`)
  writes a Unity shader per pipeline WGSL: the renderer's 10 (`TIDE_RENDERER_WGSL`)
  and each game shader (`codegen_options.shader_out` collects them as the game
  generates). Each is its HLSL (`notes/shaders.md`, HLSL) in ShaderLab, at
  `Packages/com.tide.game/Resources/TideShaders/<key>.shader` named
  `Hidden/Tide/<key>`. The key is FNV-1a 64 of the shader's SPIR-V, which the
  recording backend sends with each pipeline. A wasm program has no SPIR-V and
  sends its WGSL's FNV-1a instead; `web-keys.txt` beside the shaders maps each
  WGSL key to its file's, which TideRender reads. Files are written only when
  they change, and those of shaders gone are removed with their metas.
  Resources get them into player builds. Render state is material properties
  (blend factors, ZTest/ZWrite, Cull, Stencil Ref/Comp/Pass). Each shader has two
  SubShaders: target 4.5 without `gles3`, and target 3.5 for `gles3` alone, whose
  HLSL reads vertices from textures. Each includes
  `Packages/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl`: Unity passes a
  standard `#pragma warning` to fxc only from an included file (a probe showed
  one inline in the shader ignored).
- **TideRender.cs:** added by TideRunner with `drawTide` where `Supported`. After
  each frame the runner calls `tide_embed_render`, and `Replay` reads the commands
  (`Prepare`): objects are made and written at once, and the draws are kept as
  `Command`s, each with the buffers, textures, material and uniform range it
  reads, in a `Recording`. In the built-in pipeline the recording is written into
  a CommandBuffer on the view camera at `CameraEvent.AfterForwardAlpha`, so Tide's
  3D depth-tests against the scene and draws before image effects. A scriptable
  pipeline's pass calls `Prepare(camera)` as the camera begins and
  `Record(camera, commands, color, depth)` as it runs.
  - Buffers are raw `GraphicsBuffer`s written at once (`SetData`). One written
    after a kept draw of this frame read it is renamed: a new buffer, the old one's
    bytes copied first (`Graphics.CopyBuffer`), so the earlier draws read what
    they did and a second picture in the frame (a scene view's) can't change the
    first. Textures written after use are renamed the same way. Everything freed
    is destroyed three frames later.
  - Index buffers keep their bytes. Unity draws through an index `GraphicsBuffer`
    of 16 or 32 bits made from them (`DrawProcedural` with an index buffer, so the
    GPU's vertex cache works), made again only when the bytes changed.
  - Textures are `Texture2D`s (linear, a mip chain where the format allows, and a
    CPU copy for part writes), `Cubemap`s, or cube `RenderTexture`s.
  - Targets are linear color `RenderTexture`s with mips, plus a separate D24S8
    depth `RenderTexture` sampled as `RenderTextureSubElement.Depth`. A
    multisampled target's depth binds as a `Texture2DMS` (`bindTextureMS`), and
    RESOLVE_DEPTH draws a sample of each pixel into a depth texture of one sample
    a pixel with `Runtime/Resources/TideResolveDepth.shader` (SV_Depth), the
    texture Tide reads from there.
  - A pipeline is its shader, with a `Material` for each (flip, stencil value) it
    draws with. A shader Unity hasn't imported yet (a new build's) is looked for
    again each frame. It's one Console error, two seconds of frames on.
  - Each draw sets the globals `hlsl.h` names and the recording's uniform
    `GraphicsBuffer` at its aligned offset (`SetGlobalConstantBuffer`), then calls
    `DrawProcedural`.
  - Target 0 is the camera's own (`BuiltinRenderTextureType.CameraTarget`, or a
    pass's color and depth), never cleared. `_TideFlip` 0 has
    `_ProjectionParams.x` turn it as Unity's own draws are turned.
  - Tide targets are cleared to black as gpu.h says, and flipped only where
    Unity's texture rows go from the bottom (`!SystemInfo.graphicsUVStartsAtTop`).
    Cull is swapped there.
  - OpenGL and OpenGL ES (and WebGL): a texture's own sampler (`ownSamplers`), its
    `filterMode`/`wrapMode` set as each draw asks. Unity's OpenGL has no samplers
    apart from textures, so a texture sampled two ways in one frame is sampled the
    last way by all.
  - OpenGL ES 3 and WebGL 2 (`textureBuffers`): a buffer is an RGBA8 texture of
    4,096 pixels a row, a word each pixel's bytes, written whole as it changes.
  - Scissor rects go to Unity's bottom-left rows.
  - A pipeline Tide doesn't draw in (HDRP, or one of a project's own) is one
    Console warning.
  - `Fresh` (nothing made yet) has the runner call `tide_embed_render_reset` first.
    Disabling the runner disables it, and the next embed's numbers start over.
- **Scene views:** the built-in pipeline's scene cameras get a CommandBuffer each.
  As one is about to render (`Camera.onPreCull`, `CameraType.SceneView`),
  TideRender asks for `tide_embed_render_view` (`notes/standard-host.md`) with its
  matrices and size, reads it as a recording of its own and records it. In URP the
  pass does the same through `Prepare`. Only the window's 3D is drawn, through the
  scene camera; the frame's 2D, clips and targets stay out.
- **URP:** `Runtime/URP/Tide.Runtime.URP.asmdef` compiles where the project has
  `com.unity.render-pipelines.universal` (17.0+; `TIDE_URP` from its version
  define). `TideUniversal` hooks `RenderPipelineManager.beginCameraRendering` and,
  for each camera TideRender draws into, enqueues a `ScriptableRenderPass` at
  `AfterRenderingTransparents`: no renderer feature has to be added to the
  project. Its `RecordRenderGraph` adds an unsafe pass using the camera's active
  color and depth (`UniversalResourceData`) and records into its native
  CommandBuffer. URP 17.6 has RenderGraph alone (no compatibility mode).
- **The camera:** `tide_embed_camera` takes Unity's matrices (ABI 4): the camera
  transform unscaled, and `Camera.projectionMatrix`. Tide's 3D is in Unity's depth
  with its near and far planes, and orthographic cameras work. The frame's screen
  is the view camera's pixels while TideRender draws. On the web,
  `tide_unity_camera(given)` reads 32 floats that the jslib copies to
  `tide_unity_camera_matrices()`.
- **The web:** the Tide program beside Unity's exports `tide_unity_render`,
  `_render_size` and `_render_reset`. The jslib copies the commands into Unity's
  heap (`TideWebRender`), and TideRender replays them as on desktop with
  `textureBuffers` and own samplers. No resolve of multisampled depth on WebGL 2:
  `TideResolveDepth` isn't supported there, so that depth reads as far.
- **Tests:** in BounceTests (PlayMode; ignored with `-nographics`):
  - `TideDrawsIntoUnitysFrame` and `...InURP`. The latter makes the Sample's URP
    assets once (`Assets/Sample/URP`, plus URP's own global settings and volume
    profile, committed) and sets the pipeline for the while. It puts URP Lit
    materials in place of the Sample's built-in ones for the while, so the scene
    is lit, not the error magenta.
  - Each renders the main camera into a 640x480 texture and checks:
    - the stripes' exact colors, and the plate under the floor hidden;
    - the cube over the ball in its color, at Unity's projection of where it is;
    - the ring and the Hud's text;
    - the render target's orientation;
    - stencil (a cyan square inside its circle alone);
    - the multisampled target's resolved depth (view `DepthView`: a cube's depth
      darker than the far limit at its corners).
  - `SceneViewsShowTidesThreeD` renders a camera made a scene view's
    (`cameraType = SceneView`) from elsewhere: the cube where it sees the ball,
    and no Hud text.

  Runs: D3D11 all 11; Vulkan, D3D12 and OpenGL Core (`-force-vulkan`,
  `-force-d3d12`, `-force-glcore`) the three drawing tests; and a Linear copy of
  the Sample (`m_ActiveColorSpace: 1`) both frame tests. EditMode 15/15 includes
  `ShaderTests.TideShadersCompileWithNothingSaid` (every Tide shader through
  `ShaderUtil.CompilePass`, no `GetShaderMessages`), which passes on all four
  desktop APIs. On the web, `PlayerSmoke` now also reads the frame on the screen
  (`ScreenCapture`): stripe and cube colors. `web-smoke.mjs` (headless Chrome,
  SwiftShader WebGL 2, `TIDE_SMOKE_SHOT=<png>` saves the page) PASSes on a WebGL
  build. That's the back buffer path on GL ES. Batch mode has no
  `WaitForEndOfFrame`, so the editor tests call `camera.Render()`.
  `TIDE_UNITY_DUMP`/`TIDE_UNITY_SCENE_DUMP=<png>` save their frames. (A first
  version of the web check rendered the camera by script into a RenderTexture,
  which on WebGL read back the clear color alone, Unity's scene missing as much as
  Tide's: the screen it reads now is what a player sees.)
- **Picks (Provisional, mine):**
  - The built-in pipeline first, then URP.
  - The renderer's HLSL is translated, not hand-written.
  - Shaders are written by `tide unity` into com.tide.game, not by an importer
    making sub-assets of `.tide` files.
  - URP's pass is enqueued per camera, not a renderer feature the project adds.
  - Scene views draw the frame's 3D alone.
- **Not done:**
  - HDRP, which the owner's design takes only on request.
  - A Metal run: no Mac here. The shaders go through Unity's Metal compiler as
    through its Vulkan one, which runs them.
  - No Tide API reads pixels in an embedded game: `target_read` serves headless
    captures and the platform tests, so it reads nothing in Unity, by design.

### Audio and the editor's workflow (2026-10-22)

The owner asked for "audio working and deal with those editor workflow issues":
Tide's sound in Unity, and four gaps listed after the renderer: unsaved edits,
scenes opened during Play, a scene loaded twice, and `#` in names. All the
picks below are mine (Provisional).

- **Audio.** An embedded frame hands its audio to the mixer as a window's does
  (`tide/host.h` calls `tide_platform_audio` embedded too; `platform.c`'s
  embedded branch submits and goes no further natively). Embed ABI 5:
  - `tide_embed_audio(e, out, frames, rate, channels)` is `tide_audio_out_fill`,
    the pull every device backend uses, called from the other program's audio
    thread. It says `tide_mixer_rendered_elsewhere(true)`, so a reload's forget
    waits for that thread. NULL with 0 frames says the thread stopped. Silence
    with no embed, nothing for channels outside 1 to 8.
  - `tide_embed_microphone(e, samples, frames, rate, channels)` is
    `tide_audio_in_push`, from the frame's thread. `tide_embed_status.microphone`
    (one of the old pad bytes, same size) says a frame names a microphone voice.
  - `tide_embed_destroy` forgets the mixer first, as its clips go with the game.
  - Both are in `embed_exports` (`compiler/cli/build.c`), which the embed DLL
    exports. A function missing there links into nothing: `Bind` then fails.
  - **C#:** `TideAudio` (`Runtime/TideAudio.cs`) is made by the runner on a child
    GameObject, "Tide Audio": an AudioSource of its own (so a user's AudioSource
    on the runner is never filtered), a silent looping clip so the filter runs,
    `OnAudioFilterRead` writing the mixer. The runner's `audioOutput` mixer
    group is the source's. The audio thread reads the embed under a lock;
    `Detach` (before `library.Destroy`) clears it under the lock and then tells
    Tide the thread stopped, so destroy's forget never waits on a thread that's
    waiting on the lock. Under `TIDE_HIDDEN` the block is cleared after it's
    read: silent, while `TakeLoudest`/`Reads` (public, a level meter) still see
    the mix. `Listen` opens Unity's `Microphone` (48 kHz asked, its own rate
    used) while the status says so, never under `TIDE_HIDDEN`, asking Android's
    and iOS's permission first, tried once a second.
  - **Web:** Unity's WebGL has no `OnAudioFilterRead` or `Microphone`, so no
    `TideAudio` is made there. Tide's program on the page opens its own
    AudioWorklet at its first audio frame (`embedded_heard` in `platform.c`)
    and the page's microphone through the usual path. tide.js resumes the
    context on the document's first press, which Unity's canvas gets.
  - **Sample:** `Assets/Tide/bonk.wav` (made by a script: a falling sine, 0.18 s)
    plays from the ball's local `Heard` handler, louder for a faster landing.
- **Unsaved scenes.** At Play, `TideBuildService.Sync` saves a copy of each
  loaded, dirty scene that has a path (`EditorSceneManager.SaveScene(scene,
  "Library/Tide/unsaved/<path>", true)`), deletes copies of scenes that aren't
  dirty, says `changed` when a copy was made, changed or deleted, then
  `sync <n>`. Play is cancelled and entered again once tide says
  `tide: synced <n>` for the last n asked (the `tide unity` loop prints it when
  no build is waiting). A batch run skips all of this. Each Play with a change
  pending waits for that build, and for the loop's 250 ms ticks.
  - **Syncs are numbered** because tide takes one stdin line per tick
    (`sys_typed_line` holds one line at a time): a second `sync` can arrive
    after the build that answered the first, so tide answers it again. Without
    numbers, the editor took that stale answer for the next question's and read
    the build before it happened.
  - **The copy is saved to `Temp/TideUnsaved.unity` and then copied** into
    `Library/Tide/unsaved`, as the bytes are compared with the last copy anyway.
  - **`TideBuildService` does nothing in an asset import worker**
    (`AssetDatabase.IsAssetImportWorkerProcess`). A worker is a Unity process
    of its own that runs `[InitializeOnLoad]` constructors with its own
    `SessionState`: it emptied the unsaved folder as a "new session" in the
    middle of a test, and its `delayCall` could start another `tide unity`.
  - tide reads `<project>/Library/Tide/unsaved/<asset>` in an asset's place
    where it exists (`read_asset` in `unity_assets.c`), and the copies are in
    `unity_assets_stamp`. A file was picked over stdin commands because tide
    restarts at every domain reload (entering Play among them): a restarted
    tide has the copies with no telling.
  - A copy goes when its scene is saved, closed or opened again (not while Play
    comes or goes, when Unity reopens scenes), and the folder is emptied once
    per editor session (`SessionState`), in case an editor quit with copies.
  - A never-saved scene and Prefab Mode: "What was not covered" below.
- **Scenes during Play.** First built as sent to the match (OpenScene, a
  `CloseScene` command); the owner then decided a scene loaded by hand is local
  ("What was not covered" below), and both went.
- **A scene loaded twice.** Each scene root entity (node 0 of a scene asset)
  gets its own Unity scene instance (`sceneOf`, by entity id): an open, loaded
  instance of that path no root has, else a new additive load claimed when its
  `sceneLoaded` comes (`sceneLoads`, in order). `MakeView` finds an entity's
  scene through its parents to its root. The fallback is the first open
  instance. A root whose instance someone else unloaded isn't loaded again
  (`scenesClosed`) until it goes. Unloads the runner does are kept by `Scene`,
  so their `sceneUnloaded` sends nothing. Unity 6.6's `Scene.handle` is a
  `SceneHandle`, whose conversion to int is an error, so scenes are compared
  as `Scene`s.
- **Names with `#`, `/` or `\`.** A node path escapes them with `\`
  (`game_object_path`); `TideUnityMap.Parts` splits at an unescaped `/`, and
  `Split` takes the last unescaped `#` followed by digits as the sibling's
  number, then unescapes the name.
- **Tests:**
  - `tide_platform_embed`'s `embed_audio`: silence, a square wave from an
    `audio` declaration at 48 kHz stereo and 44.1 kHz mono, channels 0 refused,
    the microphone flag, a constant pushed at 44.1 kHz stereo heard at about
    0.5, and silence after destroy.
  - `tide_cli_tests`: `unity_unsaved_scene_read_from_the_editor_copy` (a copy
    of the fixture in the build folder; the stamp changes and goes back),
    and the fixture's `M#1/k` GameObject (entity `M_1_k`,
    node path `M\#1\/k`).
  - EditMode `UnsavedTests`: a Spinner's speed changed through a
    `SerializedObject` and not saved is in the built `.tprefab` after `Sync`,
    and the scene opened again takes it back out.
  - PlayMode: `TheGamesSoundPlaysThroughUnitys` (the bonk read on Unity's audio
    thread in a batch run, silent under `TIDE_HIDDEN`), `ASceneLoadedTwiceIsTwo`
    (`ShowBonus { twice }`: two Bonus scenes, each Totem its own entity), and
    `ScenesLoadedByHandAreLocal` (below).
  - Not run: Play's cancel and resume (batch runs skip it), Unity's
    microphone, a phone, and audio in a WebGL player by ear.
  - Runs: native debug 2512/2512, web debug 2413/2413; Unity EditMode 16/16
    and PlayMode 14/14 on D3D11, PlayMode 11 with 3 GPU tests skipped under
    `-nographics` (Unity's audio thread runs in batch mode); the Sample's WebGL
    build with `web-smoke.mjs` PASS.

### What was "not covered" (2026-10-22)

The owner: "can we try to look into the 'Not covered'". All picks here are
mine (Provisional).

- **Voice runs embedded.** `tide/host.h` called a game's voice provider only
  outside an embed, as its audio went nowhere there; it goes to Unity now, so
  the guard is gone (`notes/voice-package.md`). `embed_voice_provider`
  (`tide_platform_embed`) registers a fake provider: its frame runs each embedded
  frame, its constant voice is in `tide_embed_audio`'s samples, its stop runs as
  the embed ends.
- **An embed's end starts the audio over.** `tide_embed_destroy` also flushes the
  mixer's remainder and resets the pull (`tide_audio_out_reset`, new in
  `audio_out.h`) and the microphone's resampler: the next runner played up to a
  block of the last one's sound (found by `embed_voice_provider` after
  `embed_audio`).
- **The editor's untitled scene.** The first loaded scene with no path is copied
  at Play to `Library/Tide/unsaved/Untitled.unity` (`UNITY_UNTITLED_SCENE`),
  dirty or not. tide adds it as an asset of the all-zero GUID, named
  `UnityEngine.Scenes.Untitled` (`SceneContents.Untitled`), its map path `""`,
  Unity's for a scene with no file. A saved `Assets/Untitled.unity` clashes with
  an error saying to save the untitled scene under another name. Its copy goes
  as the last untitled scene closes or is saved under a name. The runner finds it
  as the loaded scene with no path (`Unclaimed("")`, `SceneOf`'s fallback), and
  `LoadScene` loads nothing for `""`. A second untitled scene isn't the game's.
- **Prefab Mode with Auto Save off isn't a gap:** Unity's prefab instances,
  Unity's own Play included, are the saved prefab until Prefab Mode saves it, so
  tide reading the saved file matches Unity.
- **Play refreshes assets before it resumes** (`AssetDatabase.Refresh`): a sync
  can rewrite the game's C# (a scene coming or going changes the schema and its
  hash, which the C# must match), which is then compiled before Play.
- **TideAudio plays its source only while an AudioListener is active**, looked
  for each frame, keeping its own playing flag (a source's `isPlaying` reads false
  a while after `Play`, which restarted it every frame). With none Unity printed
  "There are no audio listeners in the scene" each block. The Sample's camera had
  no listener: it has one now (`SampleSetup` and `Bounce.unity`), and its sound
  test passed before only because Unity runs filters with nothing listening.
- **Unity, after a script changes in Play, calls the OnAudioFilterRead of no
  component made later in that Play** (batch runs; its source plays, its time
  advances, its filter never runs). The game's C# changes in Play when its schema
  does: a runner started after that is silent until Play restarts; one already
  running keeps its sound. The untitled PlayMode test changes the schema, so it's
  its own fixture, `UntitledTests`, which runs after `BounceTests`.
- **Scenes loaded by hand are local** (owner: "scenes that are matches should be
  loaded via Tide; unity scenes being loaded manually should be left alone and
  considered local/disconnected"). The runner sends nothing for a scene it didn't
  load (no `CloseScene`; Open only for the editor's scenes as Play starts) and
  claims for the match's roots only scenes open as it started (`scenesStarted`:
  the editor's open scenes, a player's first scene) or ones it loaded; an
  entity's scene falls back only to the match's instances (`SceneOf`). A match
  scene something else unloads is dropped from `sceneOf` and loaded again. So a
  client editor, which sends no Open, has the server's scenes, and whatever it
  loads itself is its own. PlayMode `ScenesLoadedByHandAreLocal`: Bonus loaded by
  a script shows no entity; the match's Bonus loads beside it with its Totem
  showing; unloading the local one leaves the match's, and the match letting its
  Bonus go leaves a local one.
- **Tests:** `embed_voice_provider`; `unity_unsaved_scene_read_from_the_editor_copy`
  (an untitled copy read as the scene `Untitled` with path `""`, and the clash's
  error); EditMode `AnUntitledSceneIsTheGames` (a new scene with a Spinner built
  as `Untitled`, gone as Bounce opens); PlayMode
  `UntitledTests.TheEditorsUntitledSceneIsTheMatchs` (a copy of Bonus built as the
  untitled scene, a runtime scene with no path holding Bonus's GameObjects, and
  its Totem showing its entity with Bonus not loaded).
- **Runs:** native debug 2515/2515, web debug 2416/2416, docs build; Unity
  EditMode 17/17, PlayMode 15/15 on D3D11 and 12 with 3 GPU tests skipped under
  `-nographics`. Not run: Play's cancel and resume with an untitled scene (batch
  runs skip it), and the refresh before Play resumes.

### A first game: Coin Rush (2026-10-22)

The owner, away: "try making a small game and seeing what issues you hit. I
imagine we might need to allow built-in components? for things like colliders
and physics and what not?"

Coin Rush is in a scratch project, `%LOCALAPPDATA%\TideDev\scratch\coin-rush`
(not in the repository). It's a fresh Unity 6000.6 project with
`com.tide.unity` from this checkout and `box3d` in `Assets/tide.packages`. Each
player's ball (a `Box3D.Body` and `Box3D.Sphere` on a Unity sphere) rolls by
impulses from a `Move` input, in an arena of static boxes. Five coins are sensor
spheres on static bodies; `Touched` scores and moves the coin. A kinematic bar
sweeps the middle. A `Scores` singleton feeds a Tide HUD, and the pickup has a
`Clip` chime. `Assets/Editor/CoinSetup.cs` builds the prefabs and scene. It
plays: PlayMode tests roll a ball into a coin (scored in 0.9 s, the chime heard)
and into a wall (held at x = 5.505, the wall's face less the radius). The
dedicated server (`tide build --server --assets .`) builds and runs, and the
WebGL player passes `web-smoke.mjs` with its own smoke script (the ball scores
in headless Chrome, Box3D in the page's wasm).

What I hit, in order:

1. **Every physics game needs two systems of boilerplate.** The converter gives
   each entity Unity's placement as Tide's `Transform`, but `Box3D.Body` has its
   own `position` and `rotation`, which start at zero. So every body sat at the
   origin until a `Spawned` handler copied `Transform` into `Body`, and Unity
   showed nothing move until an `[After(PhysicsStages.Step)]` system copied
   `Body` back into `Transform`.
2. **Sizes say the same thing twice.** A Unity cube scaled (12, 1, 12) needs
   `Box3D.Box { size = (12, 1, 12) }` by hand, and the box goes wrong when the
   scale changes. A sphere's radius likewise.
3. **Unity's own colliders are silently nothing.** `CreatePrimitive` puts a
   BoxCollider or SphereCollider on each, which a Unity user expects to be the
   collider. Tide ignores them, and Unity's physics keeps them as static
   colliders that move every frame (the ball's). They also draw Unity's collider
   gizmos, which then don't match Box3D's shapes.
4. **The package's own fields are authored.** `Box3D.Body`'s inspector shows `id`
   and every `step*` field, the package's bookkeeping, and they're baked:
   `stepType = Dynamic` on every static body. `BoxMade`, `SphereMade` and the
   other markers are in Add Component > Tide.
5. **A new project can't bootstrap without a detour.** The game's C# is written
   only once it builds, and code that spawns a Unity prefab doesn't build until
   the prefab exists, which needs the C# components. I commented the spawn out,
   built, made the prefab, then restored it.
6. **`using Tide.Game;` beside `using UnityEngine;` is ambiguous.** Tide's own
   `Transform`, `Time` singleton and `Font` enum get C# types in `Tide.Game`, so
   `Transform` and `Time.deltaTime` don't compile in such a script. I used an
   alias.
7. **C# had no way to give Tide input** but Unity's real devices, which a test
   can't press (and which on-screen sticks, bots and replays need).
   Fixed: `TideRunner.EditInput` (below).
8. **Smaller ones:** `PlayerID` is opaque, so naming players needs a seat
   counter, as the Sample keeps (by design). Box3D's water density makes a
   0.5 m ball weigh 523 kg, so impulses tuned by feel are 40 times too small
   (documented, but a surprise). The C# comments named a `TideKeys` class that
   doesn't exist (fixed). The spec still said voice had no Unity audio (fixed).

**Built (small, Provisional):** `TideRunner.EditInput`, an event of `ref
TideEmbedInput`, raised each frame after Unity's devices are read, and
`TideEmbedInput.Hold(key, held = true)` by the Input System's key names
(`TideInputReader.Keys`; false for a name Tide's keyboard hasn't). Works on the
web too, before the jslib copies the input over. Test: Sample PlayMode
`TheGamesOwnInputRollsTheBall` (D held through it rolls the ball right; an
unknown key refused).

**Proposal, the owner's to decide: Unity's physics components as Box3D's.** (Built for what's one to one, then
removed by the owner for packages' own components with a workflow of their own, which are the smaller fixes listed
at the end: "Packages' components in Unity" below.)
Items 1 to 4 go away if the converter reads Unity's own physics components on
TideEntity GameObjects as Box3D's, where the game has the box3d package:

| Unity | Box3D |
|---|---|
| `Rigidbody` | `Body`: `isKinematic` → Kinematic, else Dynamic; `useGravity` → `gravityScale` 1 or 0; drags → dampings; `interpolation` ignored; `mass` → each shape's density, as mass over the shapes' volume |
| a collider with no `Rigidbody` up its parents | a static `Body` |
| `BoxCollider` | `Box`: `size` × the lossy scale, `center` × scale → `offset`, `isTrigger` → `sensor` |
| `SphereCollider` | `Sphere`: radius × the largest scale axis |
| `CapsuleCollider` (Y) | `Capsule`; X or Z an error that says so (Box3D's capsules stand along y) |
| `MeshCollider` | `MeshCollider` on a static body, once Unity writes the mesh's triangles where tide reads them (C can't read an FBX) |
| `PhysicsMaterial` | `friction` (dynamic friction) and `bounce` (bounciness); combine modes are Box3D's |
| colliders on children | `Part`s of the body |

`Body.position` and `rotation` are baked from the GameObject's world placement.
The runner (or the package) places a body's GameObject from `Body` rather than
`Transform`, so neither system is needed. Unity then draws and edits the
colliders with its own gizmos. Open questions:

- Is the converter knowing box3d acceptable, or should a package say how it maps
  Unity's components (a mapping file in the package)?
- Does a body spawned from a prefab at a `Transform` take that position, so
  `Spawn(Roller, Transform { ... })` keeps working?
- What about Unity's joints, and 2D (Rigidbody2D and the 2D colliders as Box2D's)?

Without that, smaller fixes for items 1, 2 and 4:

- Box3D's `Body` takes the entity's `Transform` when it starts at zero.
- A way for a package to keep fields out of authoring, an attribute (a language
  change).
- Editor gizmos for Box3D's shapes.

For 6: the engine's own types in another namespace (`Tide.Game.Engine`), or no
C# for the ones Unity has (`Transform`, `Time`, `Font`). For 5: tide could write
the C# of the game's components from a build that fails only at Unity names it
doesn't have yet.

### Packages' components in Unity (2026-10-22)

The translation of Unity's physics components (a `Rigidbody` as a Box3D `Body`, a `BoxCollider` as a `Box`) was
built and merged, then removed: the owner, "well maybe we just make it our own components so we dont force a
workflow, we design one". Asked which parts: sizes follow the scale, the package's own fields hidden, Scene-view
gizmos. Flagging Unity's colliders on a TideEntity wasn't picked, so they're silently nothing, as before.

- **`unity.json` in a package's folder** (`UNITY_MAPPING_FILE`, the format in `unity_mapping.h`, the docs in
  docs/packages/using-and-writing.md, "Unity components"). Per component of the package, by its Tide name:
  - `hidden`: `true` (a component the package adds itself: `[AddComponentMenu("")]`, never baked), or a list of
    fields (`[HideInInspector]`, never baked, so stale serialized values such as a body's `id` don't reach the match);
  - `scaled`: field to scale name, one of scale, absScale, largestScale, largestScaleXY, largestScaleXZ, scaleX/Y/Z
    (`unity_scale`; all but `scale` without the sign). Baked times the GameObject's lossy scale through its parents,
    per axis for float2/float3; in Play the generated `ReadLive` divides the entity's value again
    (`TideScale.Unscale`, a zero axis left alone), so the Inspector shows what was authored;
  - `shape`: `{box|sphere|capsule|rect|circle: sizeField, center, height}` (a capsule needs `height`), generated as
    `[TideShape(kind, size, center, height)]`.

  `source` is Tide lines a Unity project alone compiles, as a source of the package's own
  (`Library/Tide/Unity.<package>.tide`, written beside `Unity.tide` for messages and editors): as the package's, its
  systems don't warn when nothing matches them. `tide unity` (and `tide build`/`run --assets`, the web build too)
  reads the unity.json of each package the game lists: `unity_assets_read` and `unity_code_write` take their folders
  and names (`unity_package`), and a `unity_source` says the package it's of. A unity.json with rules tide doesn't
  read is an error naming the component. A package the game drops leaves its last `Unity.<package>.tide` in
  Library/Tide, compiled by nothing.
- **Box3D's** (`packages/box3d/unity.json`, as of 2026-10-25 below): `Body` hides position, rotation and the step
  fields; `Box` (size by absScale, offset by scale, a box shape), `Sphere` (radius by largestScale), `Capsule` (radius
  by largestScaleXZ, height by scaleY), `MeshCollider` and `HullCollider` (scale and offset by scale, a mesh),
  `HeightFieldCollider` (scale by absScale), `Part` (a part of a `Body`), `Joint` (force and torque read back). Its
  `source`: `StartAtTransform` (stage offset 0: a body with id 0 takes the entity's `transform.position`
  and `localRotation`) and `FollowTransform` (offset 19, after the history systems). In the package itself those
  systems would have put an empty Transform table into every Box3D game's world: every pinned hash and `order.txt` of
  the package tests changed, though nothing moved differently. **Box2D's** likewise (rect and circle shapes in XY,
  placement by x, y and the turn about z: 2·atan2 of z and w, the short way, following it keeping z).
- **Editor** (com.tide.unity): `TideShape` and `TideScale` (Runtime/TideShape.cs); Editor/TideShapes.cs draws a
  selected shaped component's wireframe (`DrawGizmo` on `TideComponent`) in the GameObject's matrix the way Unity's
  colliders are (a box with the scale, a round shape with its radius times the scale's factor), and `TideShapeTool`,
  an `EditorTool` for `TideComponent` available when the component has a shape, edits size, radius, height and
  centre with `BoxBoundsHandle`/`SphereBoundsHandle`/`CapsuleBoundsHandle`, with Undo and prefab overrides recorded.
  The component's inspector has its Edit Shape button (`EditorToolbarForTarget`).
- **Tests:** `unity_box3d_components_by_its_rules` (tests/unity_physics: Box3D's components on a floor scaled
  (10, 1, 10) with a serialized `id` and `stepType`, a ball scaled (2, 3, 1), a pillar's capsule, a mirrored box and a
  `BoxMade`; Box3D's real unity.json and its `source`; and without the package every field as serialized).
  `unity_code_writes_a_packages_rules` (tests/unity_rules, rules for the Sample's own components: the shape, tooltip,
  HideInInspector, Unscale and the empty menu; a capsule without its height refused). Coin Rush with Box3D's
  components on scaled primitives (their Unity colliders removed): PlayMode 2/2 (the ball holds at 5.5 against walls
  of size 1 scaled 0.5 wide) and the WebGL smoke.
- **Not verified:** the gizmos and handles were compiled, not looked at (no editor window from agent runs).
- **Frictions:** Box3D's `Collider` type makes `Collider` ambiguous in a script `using Tide.Game.Box3D` with
  UnityEngine (Coin Rush's setup needed `UnityEngine.Collider`). A shape's size by the scale ignores a parent's
  rotation, as Unity's lossy scale does.

**The rest of the physics (2026-10-25).** The owner: "lets close the gaps". The gaps listed under the translation
(Box2D's untested in Unity, physics materials, compound colliders, mesh colliders, joints) are re-read for packages'
own components: materials are the shapes' own fields (friction, bounce, density), so nothing to do. The rest, all my
picks (Provisional):

- **Rules name the package's.** `unity_code_write` checks every component and field a unity.json names against the
  game's schema (`rules_name_the_game`, `unity_rules_fields`): one the game hasn't is an error naming the file, the
  component and the field. Box3D's unity.json had rules for `BoxMade` and the other `*Made` markers, gone from the
  package since, which nothing said. Each rule keeps its file (`unity_component_rules.file`).
- **`[Native]` fields are hidden by intent.** `print_outline` marks a field `"native": true` (`compiler/src/outline.c`,
  the `field.native` the checker sets), and `unity_code.c` (`native_field`, from the outline it's given) writes no C#
  field for one of a component or singleton: nothing in Unity shows, authors, bakes or reads it. `Value` keeps it, as
  the layout. So the unity.json files don't list ids and handles: Box3D's `Body.id`, `settingsApplied`, each shape's
  `cache`, `Part`'s handles, `Joint.id`; Box2D's `Body.id`, `Part`'s handles, `Joint.id` and `configuration`.
- **Parts (`"part": {"of": field, "body": component}`).** A component that makes its entity a part of another's body
  (Box3D's and Box2D's `Part`). The body is the GameObject the entity field names, else the nearest above with the
  body's component. Tide prefab text can't name a prefab's root from a child (paths are from the root, `this` is the
  block's own), so a part's empty `of` is filled at run time instead: each package's `source` has
  `PartOfTheBodyAbove` (stage offset 0, before parts are prepared, `world.Parent` up to the first with a `Body`). The
  converter (`body_of`) finds the same body and bakes each of the part's placed fields (`"placed"`, else the shape's
  `center`: a shape's offset) in the body's frame (`put_placed`): Unity's value times the scale (the field's `scaled`
  rule), turned and moved by the part's world placement, then into the body's frame by the body's inverse (`world_of`
  composes through every parent, as `compose` does). A part turned against its body (relative turn's |w| below
  0.9999999, about a twentieth of a degree) whose shape isn't a sphere or circle is a warning: "Fin is turned against
  Cart, the body its Box3D.Box is a part of, which a shape can't be: it's baked as if it weren't turned. Turn the
  GameObject as its body is, or make it a body of its own and join them". A part of nothing (no `of`, no body above) is
  a warning too, as the package destroys it. In Play, the generated `ReadLive` shows a placed field through
  `TideParts.Placed` (Runtime/TideShape.cs): the part's `[TidePart(of)]` component's live `of` GameObject gives the
  body's frame, and the value goes back into the part's own space, divided by its scale; without a body it's
  `TideScale.Unscale` as before. So the Scene-view wireframe, drawn in the part's GameObject from the field, is where
  the match has the shape, outside and in Play. Box2D's capsules, polygons, segments and chains have points, not an
  offset: placed since by `"points"` (Physics in Unity's last gaps, below).
- **Meshes (`"mesh": {"vertices", "indices"}`).** `[TideMesh(vertices, indices)]` on the component (indices empty for
  a hull). Editor/TideMeshes.cs: `ObjectFactory.componentWasAdded` fills an empty one from the GameObject's
  MeshFilter (Add Component), and the inspector (`TideComponentEditor.MeshFields`) has a Mesh field (the filter's at
  first), its counts and Copy, with Undo and prefab overrides. Unity's `vertices` and every triangle submesh's
  indices go in unchanged: Box3D's "counterclockwise from the front" is in its right-handed terms, and the front it
  means is the direction of cross(b - a, c - a), which for Unity's (and Tide's) clockwise-front triangles also points
  out of the front (Unity's quad: its triangle 0, 3, 1 gives (0, 0, -1), its normal); Box3D's own mesh test feeds a
  floor whose triangles' cross points up. Vertices are the mesh's own, unscaled: the component's `scale` field takes
  the GameObject's (`scaled`). In the editor `Mesh.vertices` reads any mesh; it isn't read in players. Box3D's
  `MeshCollider` and `HullCollider` have it. A `HeightFieldCollider` from a Terrain: built since, copied by the
  inspector as meshes are (below).
- **Joints** need nothing new: `a` and `b` are entity fields, GameObject references baked as paths. `"readBack"`
  fields (Box3D's and Box2D's `Joint.force`, `torque`) are `[NonSerialized, TideReadBack]` live fields, never in
  components.json, shown in Play under "From its package" (`TideComponentEditor.Section`, before "Local fields").
  Anchors aren't by the scale; their Scene-view handles came since (below).
- **Box2D's** unity.json: `Capsule` and `Segment` points by `scale`, a capsule's radius by `largestScaleXY`; parts;
  joints read back; `BoxMade`/`CircleMade` still hidden (Box2D still has them).
- **`tide schedule --assets`** reads a Unity project's prefabs, scenes and its packages' `source` as `tide unity`
  does, so a game made in Unity is checked without building it (help and docs/tools/cli.md).
- **Tests:** `unity_physics_components_by_their_rules` (was `unity_box3d_components_by_its_rules`; tests/unity_physics
  with Box3D's and Box2D's real unity.json: the earlier objects, a cart turned half a turn about z with a scaled wheel
  part naming no body and a turned fin naming the cart, a hinge joint naming the cart and the ball, a ramp's mesh on a
  scaled GameObject, a 2D kite turned half a turn with a tail part, a part of nothing; the fixture's components.json
  as tide writes it now, without `Body.id`). `unity_code_writes_a_physics_packages_rules` (tests/unity_rules_physics:
  a schema and outline as tide writes them; a native field with no C# field and not in components.json, a part's
  attribute, placed fields through `TideParts.Placed`, the mesh attribute, a read-back field; four stale or wrong
  rules refused). `tide_cli_unity_physics` (`unity_physics_test.cmake`: the fixture as a Unity project listing the
  real packages, `tide schedule --assets` compiles it with both packages' `source`, and says the two warnings).
  `tidec_outline_game` (a `[Native]` field marked).
- **Runs:** native debug 2760 of 2763 (one disabled; `tide_render3d_pixels_opengl` segfaulted while Unity held the
  GPU and passed alone; the private prototype `cracked_tests` fails its pinned match hash, `TIDE_EXTERNAL_DIRS`'s
  checkout against this dev, nothing this touches); web debug: every engine test passed, the run ending in the
  private prototypes' play tests, which weren't run again. `tide unity --once` on a copy of Coin Rush (Box3D listed)
  writes Box3D's components with the real rules and no error; its PlayMode 2/2. The Sample's EditMode 18/18 and
  PlayMode 16/16 (D3D11), its com.tide.game written again by tide unity from this dev (schema hash, renderer shaders).
- **Not verified:** the mesh inspector, the parts' wireframes in Play and joints in a running Unity game were compiled
  and unit-tested, not looked at or played (no editor window from agent runs).

**Physics in Unity's last gaps (2026-10-25).** The owner, on the list left: "lets tackle the todos" (apart from
other platforms' tests). All picks mine (Provisional).

- **Points (`"points": [fields]`).** Fields that are a shape's points in the GameObject's own space, a float2 or
  float3 or a list of them (`unity_field_point`; another type is an error, as only those have C# to read them back).
  The converter (`put_points`, `place_point`) bakes each times the lossy scale, each axis with its sign (a float2's z
  scale is 1), and on a part turned and moved into its body's frame, which points can be: unlike an offset, a turned
  part is no warning, but a flat shape's part tilted other than about z is (`tilted_against`: the part's z in the
  body's frame), its points baked where they fall on the plane. A list is baked in the other order where the scale
  mirrors it (x times y negative for float2s, all three for float3s), so a one-sided chain keeps its solid side. In
  Play `TideParts.Points` (four overloads) reads them back: from the body's frame, divided by the scale, a mirrored
  list put back in order. Box2D's `Capsule` (`a`, `b`), `Segment` (`a`, `b`), `Polygon` (`points`) and `Chain`
  (`points`) have it (Box2D's `FixtureOwner` already put them on a part's body); a capsule's and polygon's radius by
  `largestScaleXY`.
- **Outlines (`"outline": {"points", "radius", "closed", "hull"}`).** `[TideOutline(points[], radius, closed, loops,
  hull)]`. Editor/TideGeometry.cs (`Outline`, `Hull`: Andrew's monotone chain, counterclockwise) gives the points in
  order and TideShapes.cs draws them through the GameObject's transform: a plain line, or with a radius (times the
  largest scale, of x and y for a flat one) each side of an open one with a disc at each end, and the outside of a
  closed one (each edge's side away from the points' middle) with arcs between edges. Edit Shape (`TideShapeTool`, now
  available for outlines and joints too, `Editable`) has a `FreeMoveHandle` on each point, moved back into the
  GameObject's space (`InverseTransformPoint`, so the scale is the field's own; a Vector2 drops z). Box2D's capsule
  (`a`, `b` rounded), segment, polygon (closed, a hull, rounded) and chain (closed while `loop`).
- **Joints (`"joint": {"a", "b", "anchorA", "anchorB", "rotationA", "rotationB"}`).** `[TideJoint(...)]`.
  `TideGeometry.JointOf`: each anchor in the world from its body's GameObject's position and turn (not its scale, as a
  body has none); a body not set stands in as the joint's own GameObject. Drawn as a line between the anchors, dotted
  from each body to its anchor. Edit Shape: a `PositionHandle` on each anchor (in its frame's turn) put back into the
  body's frame (`AnchorIn`), and a turn: `RotationHandle` for a quaternion, a `Disc` about the body's z for an angle
  (`TurnIn`: radians, -pi to pi). Box3D's `Joint` (`rotationA`/`B`) and Box2D's (`angleA`/`B`).
- **Meshes drawn.** `"mesh"` takes `"scale"`; `[TideMesh(vertices, indices, scale, offset)]` (offset: the placed
  field). `TideGeometry.MeshOf`: each triangle's three edges, its vertices times the scale and moved by the offset in
  the GameObject's matrix, at most 20,000 triangles (spread over the mesh beyond), or a hull's points as dots.
- **Terrains (`"terrain": {"heights", "holes", "columns", "rows", "scale", "minimum", "maximum"}`).**
  `[TideTerrain(...)]`; Editor/TideTerrains.cs fills one from a `TerrainData` (Add Component on a Terrain's
  GameObject, or the inspector's Terrain field and Copy, with Undo and prefab overrides), as meshes are copied rather
  than read by the converter (TerrainData is a binary asset). `TideGeometry.Sample`: Box3D takes 65,536 samples at
  most, 4,096 a side (`live_height`), so every `step`th sample, a power of two, the fewest that fit (Unity's 513 is 129
  by 129; its 257 too, as 257 by 257 is 66,049); heights 0 to 1 (`GetHeights`, [z, x], into x + z * columns), scale a
  cell's size (`size / (count - 1)` in x and z) and `size.y`, bounds 0 and 1 (Box3D clamps to them and keeps 16 bits
  between, as Unity's heightmap does). Holes: a coarse cell is a hole where every one of the terrain's in it is
  (`GetHoles`, true for ground), none listed where there's none. Box3D's height field starts at its body's origin, as
  a Terrain does at its GameObject's (`b3CreateHeightField`'s bounds from 0). Drawn as its grid, at most 64 lines each
  way. The `scale` field stays by `absScale` (a Terrain's GameObject isn't scaled by Unity, so 1).
- **The converter's flats are indexed.** A component's serialized paths were found one by one, so a list of n
  numbers baked in n squared string compares (a 16,641-sample terrain, or a mesh of thousands of vertices: seconds).
  `owned.index` (open addressing by FNV-1a, made at 32 paths, at most half full) serves `set_flat` and `get_flat`.
- **Tests:** `unity_physics_components_by_their_rules` adds a Fan, a part 3 up the kite, scaled (2, 1) and turned half
  a turn, with a polygon and a capsule (their points turned and moved into the kite's frame, radii doubled), and a
  Ridge mirrored in x with a chain (points mirrored and reversed); its components.json has Box2D's `Capsule`,
  `Polygon`, `Chain` and a `List<float2>` shape. `unity_code_writes_a_physics_packages_rules` now reads the real
  `packages/box3d` and `packages/box2d` unity.json over tests/unity_rules_physics, its schema and outline the ones
  `tide unity` writes for a game listing both, cut to the components the rules name (its own unity.json is gone):
  the joint, terrain, mesh and outline attributes, `TideParts.Points` reads, and three more wrong rules refused. The
  Sample's EditMode `GeometryTests` (components of the rules' attributes in Assets/Tests/Geometry, the runtime test
  assembly, as Unity adds none from an Editor folder; the editor tests reference it, and allow unsafe code for
  `ReadLive`): a hull without its inside point and a chain closed as it loops; a joint's anchors in a turned, scaled
  body's frame and back; a mesh's edges by scale and offset, the triangles spread at a limit, a hull's points; a 33
  terrain sampled whole and a 513 one every fourth with a coarse hole only where all sixteen are, filled into a
  component, its grid's lines; the Fan's and Ridge's points shown back as authored. By hand: a scratch project listing
  both packages compiles every generated component in Unity (batch, no errors).
- **Runs:** native debug 2777/2777 and web debug 2680/2680 (both without the `private-prototype` label), the docs
  build; the Sample's EditMode 24/24 and PlayMode 16/16.
- **Not verified:** what's drawn and the handles, in a window (the geometry is tested, not its pixels); joints and
  terrains in a running Unity game.

### The game is Assets (2026-10-21)

**Owner:** "anything inside `Assets` is valid", rather than a game folder;
scoping is like assembly definitions; and in a Unity project C is allowed only
in packages ("so we can avoid a lot of these issues": Unity's plugins keep C in
`Assets`). Built on tide's own packages rather than a new file:

- **A Unity project's Assets folder is the game** (`packages_unity_assets` in
  `compiler/lsp/packages.c`: a folder named `Assets` beside
  `ProjectSettings/ProjectVersion.txt`; shared by tide and tidels). Every
  `.tide` in it is the game's, but those in folders Unity leaves out (hidden,
  or a name ending in `~`; `packages_unity_left_out`, applied in `tide_files`).
  Its `tide.packages` is `Assets/tide.packages`. Project Settings > Tide lost
  its Game folder (`TideSettings.GameFolderPath` is `Application.dataPath`), so
  `tide unity` runs in Assets and messages' paths are from it
  (`Tide/Playground.tide:1:7`).
- **Packages are found like assembly definitions:** `resolve` (packages.c)
  adds a local line, `./Folder`, for each folder under Assets whose
  `tide.packages` says `package Name` (not inside another package's folder,
  which is that package's business; not in folders Unity leaves out; skipped
  where the game's file lists it already). The lines exist only in the
  resolved game (`line` 0), never in the file, so `tide add`/`update` don't
  write them; what such a package needs, the game's file lists, as for any
  package (`tide add` adds it). A folder under Assets with a game's
  `tide.packages` (no `package` line) is an error at it: "the game is the whole
  of Assets ... move it to the Assets folder, or start it with `package
  Name`". `stamp_folder` also stamps every `tide.packages` under Assets, so a
  new package folder rebuilds.
- **C only in packages:** `own_files_of` gives nothing for the Assets folder
  itself, so the game's own C, headers and libraries (Unity's managed DLLs
  among them) are never built or linked; a package's folder's are, as in any
  game. `warn_unity_c` warns at each `.c`/`.cpp` beside the game's `.tide`
  files (`<path>:1:1: warning: tide doesn't build this: in a Unity project, a
  game's C goes in a package`, located so Unity's Console opens it); other C in
  Assets is plugins' and says nothing.
- **Commands from any folder of Assets:** `tide add`, `update`, `eject`,
  `fetch` and `publish` work on the Assets folder when run in a folder of it
  that isn't a package's own (`packages_unity_game`, `command_folder` in
  `compiler/cli/main.c`). `run`, `build` and `unity` take the folder they're
  given, as before.
- **The editor knows the generated names:** `tide unity` writes its own
  declarations (`UnityEngine.Source`, `LoadedScenes`, `OpenScene`, scenes) to
  `<-o>/Unity.tide`, where messages already said they were, beside the
  prefabs' texts in `<-o>/prefabs`. tidels takes a Unity project's Assets as a
  game (`game_folder`, walking up from a file, stops there as at a
  `tide.packages`), drops files in left-out folders, and adds the project's
  `Library/Tide/prefabs/**` and `Library/Tide/Unity.tide` after the game's own
  files, as tide compiles them (`add_unity_game` in `compiler/lsp/server.c`).
  Until the editor has built once, those names are unknown in the IDE. A folder
  opened below Assets (Assets/Tide as the root) is still a game of its own
  there, as before.

Tests: `cli_a_unity_projects_assets_is_a_game_whose_packages_are_found`
(packages: Assets found, a folder of it gives Assets but a package's own
doesn't, left-out folders, Physics and Tools found with no lines and Inner
inside Physics not, a listed one once, a misplaced game file's error) and
`lsp_unity_game_is_its_assets_with_what_tide_unity_wrote` (a game across two
folders of Assets using `UnityEngine.Prefabs.Roller` from Library/Tide has no
diagnostics, a broken `Samples~` file is left out, and without the prefab text
Roller is unknown). Gaps: the walks over all of Assets (`.tide`, `.tprefab`,
`tide.packages`, and the converter's own) run each time tide checks for
changes, which a very large project may feel (answered the same day by
--notified, below); older com.tide.unity packages still run `tide unity` in
`Assets/Tide`, which builds as a plain folder game.

**Told of changes rather than looking (2026-10-21, owner: "unity handles assets
and lets us know when they get changed").** `tide unity` polled every 250 ms by
walking all of Assets several times (`stamp_folder`'s `.tide`, `.tprefab` and
`tide.packages`, and `unity_assets_stamp`'s every `.prefab`, `.unity` and
`.meta`). The package now runs it `--notified`: `game_stamp` then stats only
the files the last generate compiled (`build.sources`) and `Assets/
tide.packages`, plus local and built-in packages' folders as before (small), and
a `changed` line on its input starts the settle-then-build any change does.
`TideChanges` (an AssetPostprocessor in `TideBuildService.cs`) writes it when
Unity imports, moves or deletes a `.tide`, `.packages`, `.prefab` or `.unity`
under Assets, or C, headers or libraries in a package's folder; Build
Settings' scene list changing (`EditorBuildSettings.sceneListChanged`) does
too. Edits to known files still build while Unity is in the background, since
Unity imports only on focus; a new file waits for Unity to import it. Declared
asset files keep their own cheap stat (`asset_stamp`). A build itself still
walks Assets once (finding the game's files and reading prefabs and scenes).
Checked by a scratch driver (an edit untold rebuilt, a new file alone didn't,
`changed` did and the new component was in the schema) and the Sample's
`ChangesTests` (what counts: the game's files, a package's C, not a plugin's).

### Icons (2026-10-21)

Owner: "we should add icons for these, the packages, the .tide files etc".
`Editor/TideIcons.cs`: a `.tide` file imports (`TideSourceImporter`, a
ScriptedImporter for `tide`, in a file of its own name) as a TextAsset of its
text, so the inspector shows the source, with `Editor/Icons/TideFile.png` as
its icon; a `.packages` file (`TidePackagesImporter`, in its own file too) the
same, with `TidePackages.png` when it's named
`tide.packages` and none otherwise; and a folder whose `tide.packages` says
`package Name` before any other line gets `TidePackageBadge.png` drawn over the
bottom right of its folder icon (`projectWindowItemOnGUI`, in list and grid;
answers cached until `projectChanged`). The PNGs are 64 px, drawn from the
favicon's three amber waves (`#f59e0b`) by a throwaway coverage-sampling
script (no image tool on this machine); their metas clamp, keep alpha as
transparency and leave them uncompressed. Test: the Sample's
`Assets/Tests/Editor/IconTests.cs` (the importers' type, text and an icon
other than plain text's, and which folder is a package's). Not seen in a
window: how the badge sits on the folder icon at each zoom.

Script icons (2026-10-22, owner, of the Inspector's `#` on a Tide component:
"these should have a custom icon"): `TideComponent.png` (dark waves cut into an
amber rounded square, as Unity's component icons are solid) on every Tide
component, `TideEntity.png` (amber waves on a dark square) on `TideEntity` and
`TideEntityView`, and the plain waves on `TideRunner`, by Unity's `[Icon]`
with the paths in `Tide.TideIcon` (Runtime/TideComponent.cs). tide writes
`[Icon(global::Tide.TideIcon.Component)]` on each generated component, rather
than an icon in its `.meta`, which stays two lines. Two waves, not the logo's
three: at the Inspector's 16 px three ran together (checked by box-filtering
the 64 px PNGs to 16 px on Unity's dark and light backgrounds). Test: the
Sample's `IconTests.TideScriptsHaveTideIcons` (the icon Unity gives a Ball, a
TideEntity and a TideRunner). Not seen in a window.

Correction (2026-10-21): the importers were first in `TideIcons.cs`, but Unity
finds a ScriptedImporter's script by its file's name, so the owner's inspector
showed "Failed To Load Script Object" over the text and
`AssetImporter.GetAtPath` gave nothing. A file imported before the importers
existed also keeps a `.meta` of `DefaultImporter`, which Unity reads into the
ScriptedImporter and warns of ("contains a <unknown> object at version 1, below
the supported minimum (2)") at each import of the file, so at each save:
`TideMetas` (an AssetPostprocessor beside `TideSourceImporter`) writes such a
`.meta` once, as the importer's, at the next editor update (`SetDirty`, then
`SaveAndReimport`), keeping its GUID. Both importers are version 2, so a
project updating the package imports its files again and has its `.meta`s
written. Each importer has an editor with no settings and no Apply/Revert,
so the inspector is the text, as a C# script's is. Tests: `IconTests` checks
each importer's `m_Script` is its class and that an old `.meta` is written
again with its GUID.

### File inspectors (2026-10-21)

Owner, on the importers' plain inspectors: "they could likely use some custom
inspector to depict what they do or visualize some debug or whatever". My
picks, for the owner to tweak. tide unity writes `Library/Tide/outline.json`
beside the schema (`notes/cli-and-packages.md`), from `tidec --outline`'s
`print_outline` (`compiler/src/outline.c`): each declaration of the game's
files, the converter's and the prefabs' texts (kind, qualified name with an
overload's parameters, file, line and column, local/async/extern), a type's
fields with their types (a match component's local fields marked), an enum's
members, an event's or command's handlers and local handlers, and for systems,
views and handlers what they read and write (parameters, then World and query
uses marked `every`), their `with`/`without` filters (scenes and ownership
included), and in the tick their stage, waits with the schedule's reason, and
what runs alongside. `Editor/TideOutline.cs` reads it when it changes and names
its files as the project does (`AssetOf`: absolute under the project, else from
Assets; `Library/Tide/Unity.tide` as is).

`Editor/TideFileInspectors.cs` replaced the importers' empty editors:

- **A `.tide` file**, above its text: whose it is (the game's, or the package's
  whose folder holds it); the last finished build's errors and warnings in it
  (`TideBuildService.LastMessages`: a build's messages gather until tide says
  how it went, `Locate` finds each one's file), each opening its line; then its
  declarations grouped by kind, each with a foldout (open at first in a file of
  six or fewer) and a `line N` link. A component's or singleton's fields show
  their declared defaults (the schema's bytes through
  `TideEntityEditor.Default`); in Play a component shows how many shown
  entities have it (the packet's section count) and a singleton its values
  (its packet bytes, text read from the packet's strings, an entity by index),
  repainting constantly. A system says when it runs ("each tick, for each
  entity it matches, at stage 1"), Reads, Writes, Reads any/Writes any (World
  and queries), With, Without, Waits for (name and reason) and Alongside; a
  view, handler and local handler say when they run.
- **`Assets/tide.packages`**: each resolved package with where it comes from
  (comes with tide; a git source, `@ref`, at its commit's first 8 digits;
  a folder; or "this project's Assets/X", found by its `package` line), Show
  for a folder in the project, Update for git (`tide update <name>`) and
  Remove for a listed one (takes its line out of the file, keeping its line
  endings); then Add a package, a text field with a menu of the packages that
  come with tide (those listed greyed), running `tide add "<source>"` in
  Assets. tide runs off the editor's thread (`TideToolchain.Run`, polled from
  `EditorApplication.update`); a failure's output shows under the field, and
  the file is imported again either way, so the build hears of it.
- **A package's own `tide.packages`**: its name, whether the game builds it,
  and how many `.tide` and C files its folder has; a `tide.packages` in a
  folder of Assets without a `package` line gets the warning tide's error
  gives.

Tests: `compiler/tests/outline/game.tide` (every kind, a local field, filters,
handlers, and a system waiting for two others with each reason; tidec run in
its folder, `RELATIVE` in `expect_schedule.cmake`, so its paths are the file's
name); the Sample's `OutlineTests` (the JSON's files and folders as the project
names them, a system's access, waits and filters, and a message located in
its file). The Sample's PlayMode run wrote a real `outline.json`. Not tested:
the inspectors' drawing (IMGUI needs a window, which batch runs have none
of), Add, Update and Remove against a real project.

### Tooling as built (2026-10-20)

- Console context for tide's messages (Authoring, above).
- `tidels` renames reach a Unity project (`compiler/lsp/analysis.c`,
  `unity_renames`): with the game's folder inside a project's `Assets` (found
  up from it by `ProjectSettings/ProjectVersion.txt`), renaming a component
  edits its `- type:` lines in every prefab and scene holding TideEntity
  objects and a prefab instance's change of a `.type`, and renaming a field
  edits its key in that component's `values` (plain or single-quoted).
  Instances' changes of listed values aren't renamed: their YAML doesn't say
  whose they are. The game's own components are renamed too (Components of
  their own, above). Test: `lsp_rename_reaches_unity_assets`.
- Gizmos (`Editor/TideGizmos.cs`): a selected TideEntity's components as a
  Scene view label; in Play mode, each shown entity's index, generation and
  whether it's this machine's (unselected ones within 40 units of the Scene
  view's camera).

### Player builds as built (2026-10-20)

`Editor/TideBuildProcessor.cs`: before a player build, `tide unity --once
--release` into `Temp/TideBuild`; each built scene's TideRunner gets
`prefabs` links to every Unity prefab in the map (so Unity builds them in);
after it, `tide_embed`, the newest `game-<n>` as `game`, `pack-<n>` as
`pack`, `unity.json`, `schema.json` and other libraries tide put there go to
the player's `StreamingAssets/Tide` (`<name>_Data`, or the app's
`Contents/Resources/Data`). In a player `TideLibrary.BuildFolder` is that
folder; it loads the embed in place, then the game's library itself, takes
its `tide_host_library()` and the pack's bytes, and passes them as the
description's `game` and `pack` (no `library_dir`, so nothing is written
beside the player). Only the editor's own desktop platform builds for now.
Checked on Windows: a 64-bit player of the sample, run with `-batchmode
-nographics -tide-smoke` (`Assets/Scripts/PlayerSmoke.cs`), ran the match,
showed its own ball on the floor and turned the pillars. Not checked: Linux
(where the game's library may need the embed's symbols visible) and macOS.

### Closing the gaps (2026-10-25)

The owner, after a review of what was left: "yes do both" (a progress indicator outside Play, and icons on the
generated components and TideEntity), "also lets close the gaps (apart from the tests in other platforms)", and on
C# names Tide and Unity share: "for ambiguous names we should have to explicitly type their namespace such that we
all agree what it is".

- **The earlier session's last work reached dev with this round.** Packages' components in Unity (above) and the
  script icons (Icons, above) were built on 2026-10-22 and left uncommitted in that session's worktree; they were
  carried over as they were, then brought up to date with the physics packages (Packages' components in Unity).
- **A build is a task in the status bar** (`TideBuildService.Working`/`Idle`, Unity's `Progress` API, indefinite,
  named `Tide`), never a Console line: from tide starting to its first build's end (`tide: building the game for
  Unity ...`), from each `tide: building game-<n>` (`notes/cli-and-packages.md`) to `built` or `keeps running its
  last build`, and while Play waits for tide to build the scenes as they are, from the `sync` to its answer. It goes
  as tide stops, and a task a script reload left is removed as the service loads. Project Settings > Tide says
  "building the game" meanwhile. Unity's own task for compiling the C# tide wrote follows it. My pick: removed as it
  ends, failed or not, rather than left finished (a failed build's errors are in the Console already).
- **Errors that name no file go as the rest do.** They went in as ordinary logs, which stayed, with "the game's
  errors are fixed" after them; now they're compiler messages too, with no file (`LogLocated` with `""`), which the
  next build takes away (`RemoveLocated`), as is "the game has errors: fix them before Play". The fixed line is left
  for a Unity without compiler messages alone.
- **The game's C# comes while its code doesn't build** (Coin Rush's item 5): after a failed build tide builds the
  game's declarations alone and writes `Packages/com.tide.game` from them (`notes/cli-and-packages.md`). So a new
  component is in Unity before the code spawning the prefab that needs it builds: make the component, the prefab,
  and the game builds. The editor's game keeps its last build; the new C#'s hash isn't its schema's, so live values
  wait for a build that works. My pick: every type the game declares, rather than writing C# only for a build failing
  at Unity names alone, so a component also comes while a system is half written.
- **Names Tide and Unity share are written whole (owner).** Tide's `Transform`, `Time` and `Font`, and a package's
  types such as Box3D's `Collider`, keep their names in `Tide.Game`; a script using both namespaces gets C#'s error
  for an ambiguous name and writes `Tide.Game.Transform` or `UnityEngine.Transform`. Nothing is renamed, hidden or
  moved to another namespace (the two proposals in Coin Rush's item 6 are withdrawn). The generated C# and the
  package already write every name from `global::`. Tide's own names follow the same rule (Unity prefabs of one name
  in two folders: "write the one you mean"). User docs: the package's README and `docs/tools/cli.md`, Unity.
- **Already closed:** the walks over Assets were answered by `--notified` (The game is Assets).
- **Tests:** `tidec_declarations_only`; the Sample's `ConsoleTests.ABuildIsATaskInTheStatusBarAndNoneOfTheConsoles`
  (a `[UnityTest]`: the service's own tide stopped for the while, lines given through `TideBuildService.Hear`, the
  task looked for in `Progress.EnumerateItems` a frame later, as Unity starts and takes away tasks at its next
  update) and `AnErrorAboutAnAssetOpensItAndNoneHasACSharpStack` (an error naming no file is an entry with none, and
  goes with `RemoveLocated`); `IconTests.TideScriptsHaveTideIcons`, run for the first time (the editor tests'
  assembly references `Tide.Game` for it). By hand on a copy of the Sample: a `Crate` component and a handler
  spawning `UnityEngine.Prefabs.CrateBox`, which isn't there: `tide unity --once` says that one error, and
  `Crate.cs` is written with `Owner.cs` kept. Sample EditMode 19/19 and PlayMode 16/16 (D3D11, a GPU, batch).
  Not seen in a window: the status-bar task itself.
- **Runs** (this round with the physics work merged in): native debug 2761/2762 before the merge, the one failure
  `cracked_tests` (tide-prototypes' pinned match hash, a private prototype the environment's `TIDE_EXTERNAL_DIRS`
  adds; no simulation code changed here); web debug passed through test 2658, the same `cracked_tests` failing,
  stopped before the private prototypes' browser play tests; after the merge `tidec_*` 2509/2509, the CLI and Unity
  tests 19/19, Sample EditMode 19/19 and PlayMode 16/16, and the docs build (a link's anchor fixed:
  `shards.md#a-c-library-s-state`).

### Tide's drawing after a hot reload (2026-10-25)

The owner: "last time i tried it (before this work) hot reloading broke screen space rendering?". Their playground's
only Tide drawing is the rooms package's menu, in screen space.

- **Cause:** each pipeline's material held its vertex layout as vector arrays (`_TideAttributes`, `_TideSlots`,
  `Material.SetVectorArray`). A material keeps no array its shader doesn't declare as a property (ShaderLab can't
  declare arrays), and Unity empties them when it reloads its materials: an asset import in Play does, as
  `TideSchemaWatcher`'s refresh does when a build of another layout writes new C#, and so does entering Play with
  Enter Play Mode Options skipping the domain and scene reload (the owner's project: `m_EnterPlayModeOptions: 3`).
  With them empty every vertex reads nothing, so all of Tide's drawing went, while Tide's own command stream, the
  draw packet, every material, shader, buffer and texture stayed as they were (traced: identical streams before and
  after). A reload of the same layout writes no C#, refreshes nothing, and kept drawing.
- **Fix:** the arrays are each pipeline's `MaterialPropertyBlock` (`Pipeline.properties`, made in `MakePipeline`),
  which each draw takes (`Command.properties`, `CommandBuffer.DrawProcedural`'s last argument). A material keeps
  only what its shader declares (blend, depth, cull and stencil), which survive.
- **Test:** BounceTests `TideDrawsIntoUnitysFrameAfterAHotReload`: the frame's checks of `TideDrawsIntoUnitysFrame`
  (now `Check`), then the newest `game-<n>` copied to the next number, the embed's newer `loaded-<n>` awaited (it
  deletes the one it ran), a forced import of `Bounce.tide` and a refresh in Play, and the checks again. It fails
  without the fix (the plates and everything else of Tide's gone) and passes with it. Sample PlayMode 17/17 and
  EditMode 24/24 on D3D11; the drawing tests also on Vulkan, OpenGL Core and D3D12. Reproduced first on a scratch
  copy of the owner's playground (its rooms menu: gone after a layout reload, and a frame after the first under its
  Enter Play Mode Options; shown in both with the fix).
- **Found on the way, not changed** (closed since: the tables' sizes on 2026-11-10, Made for production, and which
  tide built it on 2026-11-19, top): a host takes any `game-<n>` that loads, with no check that the same tide made
  it. A copy of the playground whose ProjectSettings named another tide (`tidePath`) loaded an embed of one tide and a
  build of the other, and Unity crashed in `tide_run_bind_io` (a pointer of the game's table, laid out otherwise).
  The same could come of tide's installation changing under a running editor, as each build reads the installed
  sources and libraries while the embed Unity loaded stays (not tried).

### One tide for a project (2026-10-25)

The owner: "what if my buddy has another tide version installed ... we need to be able to use a specific version tied
to that branch", then "we should be able to have maybe parallel tide installations? that way a unity project can
install it if it isnt and use it", "that should also handle CI? imagine GameCI", and on how: "for unity we can maybe
use the commit hash" (Unity's packages-lock records the package's commit) "otherwise for tide itself pinning it in the
tide.packages", with only Unity switching versions (the CLI's pin: `notes/cli-and-packages.md`, A game pins its tide).

- **The package's version** (`Editor/TideVersions.cs`): `PackageInfo.FindForAssembly` gives the package's source; for
  git its locked commit (`git.hash`) and what it follows (`git.revision`). A revision that's a release tag
  (`OfTag`: `v0.5.0-nightly.12`) is that version; any other commit asks the site (`/releases/for/<commit>`,
  `notes/releases-and-ci.md`), and the answer is kept in `Library/Tide/version.json` by commit; a lookup that failed is
  asked again a minute on, not at each repaint. A package that isn't from git (file:, embedded: the Sample, the owner's
  playground) has none, and builds as before (PATH, the installer's folder).
- **Side by side:** `TIDE_VERSIONS`, else the `versions` folder of where the installer puts tide
  (`%LOCALAPPDATA%\Tide\versions\<version>`, `~/.tide/versions/<version>`; `tide upgrade` replaces only the entries
  its package has, so the folder stays). `Executable` takes `<version>/bin/tide` whose `VERSION` is the version, or
  the installer's tide when it's that version. `Install`: `/releases/v<version>.json` from the site
  (`TIDE_RELEASES_SITE`, default tide-engine.dev; `file://` for tests), this platform's package (`PackageName`, as the
  installers name them) and `SHA256SUMS`, checked; unpacked (zip by `ZipFile`, tar.gz by `tar`, which keeps what runs
  runnable) into `.<version>-<guid>`, whose `VERSION` must be the version, then moved into its place last; another
  editor's install meanwhile is taken. Everything from the site comes through `UnityWebRequest` (the package depends on
  `com.unity.modules.unitywebrequest`): .NET's `HttpClient` in the editor timed out on every address on this machine,
  the site, api.github.com and google.com alike, where `UnityWebRequest` took 0.2-1.7 s, so the first version, built on
  `HttpClient` and tested only with `file://`, never reached the site. `UnityWebRequest` belongs to the editor's
  thread: a batch run or a player build waits on it there (`Install`); the editor's install (`Begin`) follows the
  download (`DownloadHandlerFile`, into the work folder) each update, then checks and unpacks it off the thread
  (`Settle`).
- **When:** `TideToolchain.Find(out where, install, wait)`: your own command first (`TideUserSettings`, below), then
  the package's version: in a batch run installed at once (a build server: GameCI needs nothing beforehand); in the
  editor asked once a session (a dialog), then installed off the editor's thread with a `Progress` task, the build
  service restarting as it's in place; a player build (`wait`) installs it with a progress bar. Project Settings >
  Tide says which tide and has Install. With the package's version (`Pinned`), tide's runs get `TIDE_NO_UPDATE_CHECK`.
  A game's `tide =` pin the package's tide isn't is that tide's error (packages.c), its note saying to pin that tide
  or move the package.
- **Your own tide command** (`TideUserSettings`, `UserSettings/TideSettings.asset`, which source control leaves out)
  replaced the project's `tidePath`, which put one machine's path in source control (the owner's playground had
  `%LOCALAPPDATA%\TideDev\playground-tide`). It's moved from the project's asset once (`Loaded`), and takes the place of
  the package's version, as for a build of tide being worked on.
- **Picks (mine, Provisional):** the site answers a commit with the highest release at or before it (so `#dev`
  works); a package newer than every release then builds with the newest release, whose embed ABI the package checks
  (`TideLibrary.Abi`): following a tag is the advice for teams and CI. Installing asks first in the editor.
- **Tests:** the Sample's EditMode `VersionTests` (a site and versions folder in a scratch folder: a tag is its
  version with nothing asked; a commit the site's answer, kept; an unknown commit's reason; a version installed beside
  others, once, leaving no work folder; a damaged checksum and a package of another version installing nothing);
  `docs/tests/releases.test.mjs`. Sample EditMode 28/28, PlayMode 17/17. Against the live site, in batch mode (a
  scratch copy of the Sample, `TIDE_VERSIONS` an empty folder, the installer's tide 0.5.0-nightly.24): a git package
  at commit 6f3c781e found it's 0.5.0-nightly.25 (the site, 0.4 s) and stopped there under `HttpClient`; with
  `UnityWebRequest`, that commit's version installed in 11 s (`tide --version` saying it) and a second `Install` took
  it as it was. Not run: macOS's and Linux's tar path, the dialog.

## Web

**Owner:** the web is very important and goes first. Option B is tried first:
a Unity WebGL page template loads `tide.js` and the game's Tide wasm beside
Unity's module. Tide keeps its toolchain (`wasm32-wasip1-threads`), worker pool,
WebRTC and relay, so its cross-platform results stay the ones our tests pin.
A `.jslib` bridge carries devices, commands and events, and copies the frame's
view-visible state (and Tide's draw list) into Unity's heap once per frame.
Threads need cross-origin isolation, which Tide pages already require.

Option A, compiling Tide with Unity's pinned Emscripten for zero-copy reads, is
the fallback if B fails on something fundamental. It brings another clang, flags
Unity controls, Unity's thread setting, a port of Tide's web platform layer, and
determinism to prove again.

### Spike step 1 (2026-10-20): B works

Local branch `unity-spike` (not on `dev`) holds a scratch game
(`unity/Spike/game`: bodies bouncing, one per player), a Unity 6000.6.0f1
project with a WebGL template, a `.jslib` and a driver (`drive.mjs`). Nothing in
the engine changed: the template wraps `WebAssembly.Instance` before `tide.js`
loads to keep the Tide program's instance (`tide-capture.js`), and the scratch
game's views hand positions to its C, which Unity copies from Tide's memory each
frame. An edit to `tide.js` itself trips the release runtime's freshness check
at configure time, so later steps that need page hooks regenerate it.

Measured in headless Chrome (SwiftShader, `--mute-audio`) with two Unity WebGL
pages and a plain Tide page in one relay room, 30 s:

- Every page cross-origin isolated, Tide's memory shared (its threads on) while
  Unity's own threads stay off. Unity needs no thread setting for Tide's.
- All three in one room through the real relay; clients' verified ticks kept up
  and resyncs stayed 0. Unity pages interoperate with plain Tide pages.
- Unity and Tide both ran about 62 frames a second in each page.
- The per-frame copy (1 KB here) cost about 2 µs. A sumo-sized measurement is
  still to do.
- A hidden page is fine: Unity's frames throttle to one a second, while
  `tide.js` keeps the match going on its tick-rate worker heartbeat (measured:
  Tide at exactly 30 frames a second, the tick rate, in background tabs). Only
  measuring view frame rates needs a window per player in headless runs.
- The spike made its sphere and material in code, which Unity's build stripping
  can't see (the Standard shader, a primitive's collider class). The design
  avoids this by reference (Runtime binding), not by workarounds.

Spike step 2 (2026-10-20), on the same branch:

- Unity drives Tide's frame. `tide.js` gained `pageFrames` (spike branch only,
  with the release runtime regenerated): while the page is shown, its loop
  schedules nothing and the page calls `Tide.frame()`; hidden, the worker
  heartbeat runs frames as for any page. Unity's `Update` calls it before
  copying, so Unity and Tide frames matched exactly (2,930 each in 30 s) with no
  trailing frame. Tide's whole frame there (session update and views) averaged
  0.22 ms.
- A hidden Unity page hosting the match: Unity fell to one frame a second while
  Tide kept serving at exactly its 30 Hz tick rate on the heartbeat; the other
  players stayed verified with 0 resyncs.
- Copying from Tide's shared memory into Unity's heap (`HEAPU8.set`, the
  `.jslib`'s operation) ran at about 19 GB/s: 16 KB 1.2-1.5 µs, 256 KB 13 µs,
  1 MB 54-61 µs, 4 MB 205-231 µs a frame. A sumo-sized view state (transforms
  of a few hundred bodies) is tens of KB, a few µs; even a whole 4 MB world would
  be about 1% of a 60 Hz frame.

Unity drives Tide's frame only while the page is visible; hidden, `tide.js`'s
heartbeat keeps driving it, so a throttled Unity never slows the match.

Spike step 3 (2026-10-20): a native player joined the room a Unity WebGL page
hosted. `game/spike_native.c` is a windowless HOST program on the real clock: a
session over the generated `tide_game_api`, joined with
`tide_platform_room_join` (desktop WebRTC through the relay). Two Unity pages,
a plain Tide page and the native player played 20 s in one match, all four
bodies in every world, resyncs 0 everywhere, native included (verified 582,
predicted 584 at its end).

Still open for phase 0: mobile browsers (needs devices).

### IL2CPP and Tide's C

Proposed by the owner (2026-10-20): IL2CPP player builds could compile Tide's
generated C and runtime with the player, rather than linking a library built
by tide. Checked on WebGL (Unity 6000.6.0f1, its Emscripten clang):
`PlayerSettings.SetAdditionalIl2CppArgs("--compiler-flags=...")` reaches C
sources in `Assets/` (a probe with `#error` unless a define was given failed
without the argument and built with it). Flags are global: they apply to all
IL2CPP C++ too. Unity's own flags for a plugin's C include `-O3`,
`-fno-strict-overflow`, `-msimd128` and no `-ffp-contract`, so Tide's
`-ffp-contract=off -fno-fast-math` must be added; generated C can also carry
`#pragma clang fp contract(off)` so a file doesn't depend on build flags.

Open: Windows and Xbox IL2CPP compile with MSVC, which Tide's clang-only C may
not build; results under each platform's compiler must match the pinned
cross-platform hashes (the per-tick hash would show a mismatch as a desync);
FP environment (denormals) on Unity's threads. Alternative: a static library
tide builds with its own clang per platform, linked with `__Internal`, with
sources only where tide can't build (console SDKs).

## Tide drawing in Unity

**Owner:** both C# views and Tide drawing are wanted. Tide's draw list goes to
Unity on every platform and is drawn inside the render pipeline by a URP
renderer feature, sharing depth and post-processing with the scene:

- Built-in drawing (shapes, text, sprites, meshes, GUI) uses hand-written HLSL
  shipped with the package. The rooms and lobby menus, Tide's GUI and debug
  drawing then work in Unity games unchanged.
- **Owner (liked):** custom `frag`/`vert` get an HLSL target in tidec beside
  GLSL, WGSL and SPIR-V. A Unity importer for `.tide` files creates the shaders
  as sub-assets of the source file, so Unity compiles them for every graphics
  API and builds include them. The same importer triggers tidec.
- URP first; HDRP and the Built-in pipeline only on request.

Built 2026-10-22 (Tide's renderer in Unity, above) with Tide's renderer replayed rather than
a renderer of Unity's, the renderer's HLSL translated rather than hand-written, `tide unity`
writing the shaders into com.tide.game rather than an importer, and the built-in pipeline
first, then URP: my picks, Provisional.

## Phases

0. Web spike (B): a Unity WebGL build and a native Tide build of one game in
   the same room through the relay, hashes agreeing every tick, threads on,
   desktop and mobile browsers, per-frame copy cost measured on a sumo-sized
   game. Throwaway branch; nothing merged until it passes.
1. Native ABI and runtime package: loading/unloading, frame loop, input,
   entity binding, editor hot reload, with hand-written bindings for sumo.
2. Schema and the Roslyn generator, replacing the hand-written bindings.
3. Authoring: `TideEntity`, its inspector, tidec's Unity front end (prefabs,
   then scenes, then variants and nesting).
4. Tooling: renames across Unity assets, diagnostics that select GameObjects,
   gizmos for Tide physics shapes.
5. Tide drawing: built-in drawing and GUI, then custom shaders through HLSL.

## Open

- Where the Unity package and its sample project live in the repository, and
  how the package is distributed (with `tide`, or a UPM Git URL).
- Unity version floor. This machine has 6000.5 and 6000.6 with WebGL support.
- Spike results decide whether B stays or A is needed.
