# The tide command

`tide` builds and runs games. A game is every `.tide` file in a folder and its subfolders, with the `.c` and `.cpp` files and libraries there when it calls C (see [Calling C](../c/functions.md)), and the packages its `tide.packages` lists (see [Packages](packages.md)). Commands take the game's folder, or use the current one.

```
tide <command> [folder] [options]
```

| Command | What it does |
|---|---|
| `tide run [folder]` | Builds the game and plays it |
| `tide build [folder]` | Builds the game into `<folder>/build`, named after the folder |
| `tide schedule [folder]` | Shows which systems can run at the same time, and why the others wait |
| `tide shaders [folder]` | Shows the shaders the game's [frags and verts](../game/shaders.md) compile to, as GLSL and WGSL |
| `tide add <source>` | Adds a package to the game in the current folder: from git, a folder, or one that comes with `tide` |
| `tide update [package]` | Moves the game's packages from git to the newest commit of what they follow |
| `tide fetch [folder]` | Downloads the game's packages from git that this machine doesn't have yet, and builds nothing |
| `tide editors` | Adds Tide to VS Code, Cursor, VSCodium and Windsurf |
| `tide upgrade` | Updates tide to the newest version |
| `tide version` | Shows tide's version and channel, where it's installed, and which compilers it found |
| `tide help` | Lists every option |
| `tide cc <options>` | clang, built into tide, for C you build yourself (see [Calling C](../c/functions.md#where-the-c-goes)) |

## run and build

| Option | What it does |
|---|---|
| `--release` | Optimized, the way players get it |
| `--web` | A web page (WebGPU, or WebGL 2 where the browser has none), built with clang's WebAssembly target |
| `--android` | An Android app, for phones and the emulator (see [Android](#android)) |
| `--title <title>` | The window's title, over the game's `title` setting (default: that, or the folder's name) |
| `--stats` | Shows the [stats](#stats) in the window's corner, as the game's `stats` setting does. `run` starts them open |
| `--debug-views` | Turns the [debug views](debug-views.md) on, as the game's `debugViews` setting does: F7 shows the profiler, which says where a frame's time and a tick's go |
| `--size <width> <height>` | `run` only: the window's size in pixels, 960 by 540 without it (not on the web or Android) |
| `--capture`, `--frames`, `--script`, `--scale` | `run` only: plays the game [without a window](#without-a-window), to see what it draws |
| `--seed <n>` | `run` only: the seed of the matches the game starts, [`Time.seed`](../language/math.md#the-match-s-seed), from 0 to 2147483647. Without it, every match gets another one |
| `-o <path>` | `build` only: where the program goes |
| `--no-open` | `run --web` only: serves the page without opening a browser |

`tide build --release --web` makes one self-contained `.html` file with the game inside. It opens straight from disk, and can be put online as it is.

A game's [assets](../game/assets.md) (its `asset` declarations) are packed with it: beside the program as `<name>.pack`, which has to stay next to it; inside the web page; and in the APK. Nothing else in the folder goes in. Under `tide run`, a file an asset names is watched like the game's code, and a saved image is in the running game within a moment, with no rebuild.

A web page draws with WebGPU where the browser has it on a GPU, and with WebGL 2 where it doesn't; games draw the same with either. Adding `?backend=webgl` (or `?backend=webgpu`) to the page's address picks one, for example to check a problem a player reports with the other.

### Stats

`--stats` shows what the game is doing in the window's bottom right corner. So does `stats = true;` in the game's [settings](../language/basics.md#settings), with no option to remember:

```csharp
settings
{
    stats = true;
}
```

They're a drawer. Closed, it's one line: the frame rate, and in a match another machine runs, your ping. A click or a tap on that line opens it, and so does **F8**: the rest is listed above the line, which stays where it is, so a click in the same place closes it again.

- **It starts closed** when the game asks, with its `stats` setting, and when a build does (`tide build --stats`, CMake's `STATS`): what players get is the one line. **`tide run --stats` starts it open**, since whoever typed that wants to read it. Under `tide run`, a reload leaves it as you left it.
- **A click or a tap on it is never the game's.** Neither the game's input nor its views read that press, from the frame it lands in. A press that began elsewhere and crosses the drawer stays the game's. The game still sees F8, as it sees every key.
- **Closed, it measures nothing.** The frame rate is a count of frames. What the list shows (a frame's work, each system's time, the memory) is measured while the list is open, so a game can ship with the line.
- **The bottom right corner is its own.** A game that asks for the stats keeps its own buttons out of that corner: the line is about 80 pixels wide and 40 high, and about 190 wide with a ping in it.

| Line | What it is |
|---|---|
| `60 fps` | Frames a second, over the last second. With `ping 34 ms` after it in a match another machine runs: the line that's always there |
| `cpu 2.1 ms` | What a frame's work takes on its thread: the ticks it runs, the views and the drawing, without the wait for the display |
| `render cpu 0.42 ms` | The part of `cpu` spent preparing and submitting the frame's draw list, including its render targets |
| `gpu 0.85 ms` | GPU drawing time, including render targets, on desktop OpenGL and WebGPU devices with timestamp queries. Results arrive asynchronously; the line is absent when no measurement is available |
| `ping`, `delay` | The network's round trip to the machine running the match (in the list when it's this machine), and the input delay when there is one (see [Input delay](../multiplayer/matches.md#input-delay)) |
| `up`, `down` | What goes over the network each second |
| `tick`, `entities` | The match's tick, and how many entities it has |
| `threads` | The threads ticks run on |
| `memory 48 MB` | The memory the program has, as the system counts it. On the web it's the program's WebAssembly memory, which only grows |
| `WebGPU`, `12 draw calls` | What draws (WebGPU, WebGL 2 or OpenGL), and how many draw calls the frame took: a run of shapes, of text or of one mesh's instances is one |
| `Move 0.31 ms` | The three systems that took longest on average per measured run, over the last half second |

Timings change twice a second, with the system order held between updates; frame rate and network rates change once a second. The drawer shows once there's a first frame rate, a quarter of a second in. GPU time excludes the drawer and display wait; on WebGPU it sums render-pass durations. CPU and GPU work overlap, so their times aren't added to get a frame's duration. WebGL 2 and OpenGL ES 3 have no GPU timer here. Outside a match, the list has the lines that aren't a match's. On a screen too low for the whole list, it keeps the lines there's room for, from its top.

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

`tide run --web` serves the page with them. Where a page is put online, the host has to send them: on itch.io, it's the **SharedArrayBuffer support** option in the game's embed settings. A page without them, or opened from disk, runs the same game on one thread, with the same results, so web players on either kind of page play together. Workers only start once a tick has enough work for them.

## Android

`tide build --android` makes an Android app, `<folder>/build/<name>.apk`, for phones and tablets (arm64) and Android's emulator (x86-64), on Android 10 and up. With `--release`, it also makes `<name>.aab`, for [Google Play](#google-play). `tide run --android` builds it, installs it on the phone or emulator that's connected, starts it, and shows what it prints until it ends. `--host`, `--join` and `--connect` go to it as they do on a computer, and Android players play in the same rooms as everyone else.

To play on your phone, turn on USB debugging once: in **Settings > About phone**, tap **Build number** seven times, then turn on **USB debugging** in **System > Developer options**. Connect the phone, and allow your computer when it asks.

Building for Android needs two things of Google's: the NDK, Android's C library and headers, and adb, which installs apps. `tide` uses the ones Android Studio installs. Without them, the first `tide build --android` asks to download them from Google, under Google's license (the [Android SDK License Agreement](https://developer.android.com/studio/terms)), and keeps what it needs of them next to itself: about 30 MB. Set `TIDE_ACCEPT_ANDROID_LICENSE=1` to accept it without being asked, as a build server would.

- The app's ID is the game's `appId` setting (see [Settings](../language/basics.md#settings)). Without one, it's `dev.tide.<the game's name>`, which is fine for testing, but `--release` needs one.
- Its name under its icon is the game's `title` setting, or `--title`, or else the game's name.
- Its icon is `icon.png` in the game's folder: a square PNG, 512 by 512 pixels is plenty. Without one, it's Tide's.
- The Android libraries in the game's folder (`.so` files) go in the app with the game (see [Calling C](../c/functions.md#android)).
- `tide` signs apps with a key it makes on your computer the first time, `~/.android/tide.pem`. A phone only takes an update to an app signed with the same key, so keep a copy of it somewhere safe, and don't share it: whoever has it can sign apps as you. `tide run --android` replaces an app another computer installed.
- To sign with another key (a build server's, or one you already have), set `TIDE_ANDROID_KEY` to its file: PEM, the private key (RSA, 2048 bits or more) and then its certificate. `openssl pkcs12 -in upload.p12 -nodes -out key.pem` makes one from a keystore; a `.jks` keystore becomes a `.p12` first with `keytool -importkeystore -srckeystore upload.jks -destkeystore upload.p12 -deststoretype PKCS12`.
- Each build's version code, which Android only updates an app to a higher one of, is the minutes since 2020 began, so every build is newer than the last. Where `SOURCE_DATE_EPOCH` is set, it stands in for now. The version people see is the game's `version` setting, like `1.2.0`, or else 1.0.
- The back button comes to the game as Escape, as in Unity.
- While `tide run --android` runs, saving a `.tide` or C file builds the app again, installs it and starts it over: a phone can't swap code into a running app, so the match starts over too. Type `r` and press Enter to start it over yourself; closing the app ends the run.

### Google Play

`<name>.aab` is an App Bundle, which is what Google Play takes: Play makes the APKs from it, each phone getting only the library for its CPU. To put a game on Play:

1. Make a developer account in the [Play Console](https://play.google.com/console) (Google charges a one-time fee), and create the app there.
2. Give the game its `appId`, and build it with `tide build --android --release`.
3. Upload `build/<name>.aab` to a release: internal testing is the quickest way to try it on your own phone. Play signs the APKs it makes with a key of its own, which it keeps (Play App Signing); `~/.android/tide.pem` is then your upload key, and Play only takes uploads signed with it.
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

When the game gets into a state you don't want, type `r` and press Enter in `tide`'s terminal to start it over. With a match on several windows (`--host` in one, `--connect localhost` in another), each window reloads when you save, and players stay in the match. A window that joined another's match joins it again a second after that match ends without it: when the other window starts it over, or is still on a build with another data layout. On the web, a reload goes on with the match closed for now: the room closes, and the other players drop out.

`tide run --web` reloads too. `tide` serves the game's page at an address on your machine, like `http://127.0.0.1:52407/`, opens it in your browser, and keeps running until you press Ctrl+C. What each reload did shows in the browser's console. VS Code and JetBrains IDEs can play it beside your code instead (see [Editors](editors.md)).

Hot reload is only for `tide run`: `tide build` makes a plain program, or a page with nothing of it.

## Multiplayer

`tide run` can start a match others join, or join one. For `--host`, the game's `Main` scene must be the match's (see [Scenes](../game/scenes.md)); a game that starts in a menu does the same from code (see [Matches](../multiplayer/matches.md)).

| Option | What it does |
|---|---|
| `--host [port]` | Opens the match for others to join: in a room, whose code the game can show, and on UDP port 7777 by default (not on the web) |
| `--join <code>` | The match in the room with this code, like `K7QF2M` |
| `--connect <address>` | The match at an address, like `192.168.1.5` or `localhost:7777` (not on the web) |

### In a web page's address

A game's web page takes the same from its address, so a link can say which match it's for:

| In the address | What it does |
|---|---|
| `?host` | As `--host`: the game opens its match, in a room |
| `?join=K7QF2M` | As `--join K7QF2M`: the game joins the match in that room |
| `?lag=100` | The page's player has a ping of 100 milliseconds, the round trip to the match's server, to see a match as on a slow connection with both players at one desk. What the trip takes already counts: the page holds back what it sends and what it receives by half of what's missing each. A page that hosts the match adds nothing |
| `?embedded` | The game is part of a page that scrolls, in a frame of it, and leaves that page its scrolling: the wheel, and a finger that swipes up or down, until the player taps the game |
| `?wait` | The game loads, and starts when the page around it says so |

A page that shows a game in a frame of the same site reads the frame's `Tide.room`, the code of the room the game hosts or joined (`""` for none), and can set its `Tide.lag` while the game runs. Its `Tide.minLag` is the least a lag can make the ping: the ping the game measured while its page added nothing, `undefined` until it has measured a few so. A page that wants it starts with no lag, as these docs do. That's how these docs put two players [side by side](../multiplayer/networking.md#prediction-and-rollback): the first frame's address says `?host`, and the second's `?join=` with the code the first one got.

Such a page starts a game that waits by calling the frame's `Tide.start()`. With `?wait` or without, it gives a promise that's kept once the game has drawn something, so the page can keep the frame out of sight until there's something in it. These docs load an example as you scroll near it, and start it when it's in view.

A page can have the browser download a game before any frame shows it: it loads the game's page once in a frame nobody sees, with `#download` after the address, and removes that frame once it has loaded. Nothing runs in it, and the frame that shows the game later finds its page in the browser's cache. A plain `fetch` of the page doesn't do that: browsers keep a frame's page apart from what a script fetches.

With `?embedded`, the wheel always scrolls the page around the game. On a touchscreen, a swipe that starts on the game scrolls the page too, until the player taps the game: the tap reaches the game, and from then on so does every touch, swipes included, until the player taps the page somewhere else.

## Without a window

`tide run` can play a game with no window at all, to see what it draws or to check it still runs: from a script, on a build server, or for an agent that shouldn't open windows on your screen. The game runs once, as it was built, and `tide` ends with the game's exit code. Nothing is watched or reloaded.

```
tide run --frames 300 --capture menu.png
```

| Option | What it does |
|---|---|
| `--capture <file.png>` | Writes the run's last frame as a PNG |
| `--capture-audio <file.wav>` | Writes everything the run played as a WAV (16-bit stereo at 48 kHz). A game with no sound gives silence, a 60th of a second a frame |
| `--frames <n>` | Ends the run after `n` frames |
| `--script <file>` | Input by frame from a file, which also says when to capture and when to quit (see [Scripts](#scripts)) |
| `--scale <s>` | Draws as on a display scaled this much, like `1.5` or `2` (1 without it). Text and edges are fitted to the display's pixels, so they look different there, and [render targets](../game/views.md#targets-and-post-processing) at the display's scale have that many more pixels |
| `--size <width> <height>` | The window's size in pixels, 960 by 540 without it |
| `--seed <n>` | The seed of the run's matches, 0 without it |

::: info Provisional
These options' names and the script's format are new, and may still change.
:::

The picture is what the window would show: the views, the GUI over them, and the stats and the profiler when they're on. It's `--size` times `--scale` pixels: `--size 1280 720 --scale 2` is 2560 by 1440. The [stats](#stats) are as a player would have them: one line unless the run asked with `--stats`, and a script's click on that line, or its `key f8`, opens them.

Nothing of the run depends on the machine it's on, or on what's happening there:

- **Time is made up.** Every frame is a 60th of a second long, however long it took, and frames come as fast as the machine runs them: 300 frames take about a second.
- **Input is made up.** The game gets what the script says and nothing else: never the real keyboard, mouse or gamepad, and it leaves the clipboard alone. The cursor is locked whenever the game asks for it.
- **The window's size and scale are what you asked for,** whatever the display is.
- **The match's seed is 0.** In a window, [`Time.seed`](../language/math.md#the-match-s-seed) is another number every match. In a run like this it's 0 every time, so what the game rolls comes out the same. `--seed 7` picks another seed, to see the game roll otherwise: the same seed is the same run again.

So a run repeats exactly: the match goes the same way on every machine, and the same machine writes the same pictures, byte for byte. Between machines, pixels a GPU blends (the edges of shapes, a shader's gradients) can differ a little. The stats' list and the profiler are the exception, while they're open: they show what this machine's clock measured, which no two runs agree on. The stats' one line doesn't. What the game reads from disk, like its saves, is still this machine's. Made-up time doesn't wait for anyone, so this is for a game on its own: a match it starts itself, not one it joins on another machine.

Something has to end a run nobody can close: `--frames`, or the script's `quit`. The run's exit code says how it went:

| Exit code | What happened |
|---|---|
| 0 | The run ended at `--frames` or `quit`, and its pictures were written |
| 1 | A picture couldn't be written. Folders aren't made for it |
| 2 | An option or a line of the script is wrong, or the run would need a window [where nobody watches](#where-nobody-watches): nothing ran, and the message names what to fix |

A game `tide build` made takes the same options itself, so a build server needs no `tide`: `build/mygame --frames 300 --capture menu.png`. These runs are for computers: on the web, drive the page with a headless browser instead, and take its screenshots.

### Where nobody watches

Set the `TIDE_HIDDEN` environment variable where nobody is there to look at a window, or to close one: on a build server, or in the shell of an agent that works on your machine while you do. Any value but nothing or `0` counts. No Tide game shows a window there, however it's started:

- A run with no window goes as ever: `--frames`, `--capture` and `--script`.
- Any other run is refused, with exit code 2, before anything is built or opened. Only closing its window would end it, and nobody could. The message says what to give the run instead.
- `tide run --web` serves the page and opens no browser.
- A game that stops says why on stderr and shows no dialog. On Windows, a debug build that stops would otherwise show one and wait for a click.

A game `tide build` made follows it too, on its own. Leave the variable out of your own terminal and your editor, where you want the window.

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
tide run --script play.txt --size 1280 720
```

| Line | What happens |
|---|---|
| `key <name> down`, `key <name> up` | A key goes down or up, by its name in [`Devices`](../game/input.md#devices): `w`, `space`, `enter`, `leftShift`, `upArrow`, `digit1` |
| `key <name>` | A tap: down for this frame, and up at the next |
| `button <name> down`, `up`, or neither | A mouse button, the same way: `left`, `right`, `middle`, `back` or `forward`. `button left` is a click where the mouse is |
| `mouse <x> <y>` | The mouse goes to a place, in pixels from the window's top left, with `y` down, as the GUI's and `Draw.Screen`'s are |
| `move <dx> <dy>` | The mouse moves by so much, across and down: what turns a camera whose [cursor is locked](../game/views.md#looking-around) |
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
| `quit` | Ends the run once this frame drew |

What a line does is in the devices at its frame, as if a player had done it since the frame before. So it goes the way a player's input goes: the GUI takes a click on one of its widgets and the game doesn't get it, and a modal keeps the keys from the game.

The mouse starts outside the window, on no widget. A gamepad is connected from the script's first `pad` line, and a touchscreen from its first `touch`.

A line that's wrong stops the run before it starts, and the message says which, and what to write:

```
tide: play.txt:7: no key is named 'shift': did you mean 'leftShift'?
```

## add and update

`tide add` and `tide update` change the `tide.packages` of the game in the current folder (see [Packages](packages.md)).

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

Both add the lines of the packages the game's packages need that it doesn't list yet.

`tide run` and `tide build` download the packages from git that aren't on this machine yet. `tide fetch` does only that, each at the commit `tide.packages` pins, and adds the same lines: a build after it needs no network, as on a build server that fetches in one step and compiles in another.

## upgrade

| Option | What it does |
|---|---|
| `--nightly` | Follow nightly versions from now on |
| `--stable` | Follow stable versions from now on |
| `--version <v>` | Install exactly this version |

`tide` says, at most once a day, when a newer version is out. Set the `TIDE_NO_UPDATE_CHECK` environment variable to stop it looking, as on a build server.

## Files

`tide` keeps its work in a hidden `.tide` folder in the game's folder. You can delete it any time, and git ignores it. Packages from git are kept for every game, once each commit: in `%LOCALAPPDATA%\Tide\packages` on Windows and `~/.tide/packages` elsewhere, or where `TIDE_PACKAGES` says. `tide build` puts what it makes in `build/`, with the game's `.dll`, `.so` or `.dylib` libraries next to the program. C files and libraries in hidden folders or in `build/` aren't part of the game.
