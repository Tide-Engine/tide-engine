# C hosts

A game needs no C: `tide` runs it in a window. This page is for working inside Tide's own repo, where CMake builds games, and for C programs that drive a game themselves: tests, tools, custom hosts. See [AGENTS.md](https://github.com/BlenMiner/tide-engine/blob/dev/AGENTS.md) for building the repo.

## tide_add_game

```cmake
tide_add_game(<target> [SOURCES <file.tide|file.c|file.cpp>...] [HOST <file.c>...] [NAME <name>] [TITLE <title>] [STATS] [LAYOUT] [WARNINGS <text>...])
```

- The game is every `.tide` file in the current source folder and its subfolders. `SOURCES` lists the files instead.
- The game's C, which defines its `extern` functions (see [Calling C](./functions.md)), is every `.c` file there but the `HOST` ones, or the `.c` files listed in `SOURCES`, and its `.cpp` files the same way, built as `tide` builds them (see [C++](./functions.md#c)). Unlike `tide`, CMake doesn't pick up prebuilt libraries: link them to the target yourself.
- Without `HOST`, the game is the whole program: a generated `main` runs it in a window, titled `TITLE`, or the game's `title` setting, or `<target>`. `STATS` shows the [stats](../tools/cli.md#stats) in a corner, as `tide run --stats` and the game's `stats` setting do. On the web, it's `<target>.html`.
- With `HOST`, those C files are the program. They include `<name>.h`, the generated header, where `NAME` defaults to `<target>`. `LAYOUT` also describes the game's data layout, as `tide run` does for hot reloading (`tide_game_layout`, in `tide/layout.h`).
- Warnings are errors in the repo's build: a warning from tidec fails it, unless part of its text is listed after `WARNINGS`, and each of those has to be there. Tests use it for programs tidec warns about on purpose.
- `<target>_schedule` is a build target that prints the game's [schedule](../game/schedule.md).

## The standard host

`tide/run.h` is what a game without `HOST` uses:

```c
#include "game.h"
#include "tide/run.h"

int main(int argc, char **argv)
{
    tide_run(&(tide_run_desc){.title = "My game", .argc = argc, .argv = argv});
}
```

It opens a window and runs the game in a session (`tide/session.h`): it samples this machine's input once per tick, draws the views and the GUI every frame, drains their [commands](../game/commands.md) into the session, and takes `--host [port]`, `--join code` and `--connect address` from the command line. `--host` opens the match, in a room (see [Matches](../multiplayer/matches.md#rooms)), and a UDP port too, except on the web. `title` and `tick_rate` go over the game's settings (see [Settings](../language/basics.md#settings)): leave them out for those, and `game_name` stands in for a title the game doesn't set. `tide_run_desc` also has `width` and `height` (960 by 540 by default, and `--size 1280 720` on the command line goes over them), `stats`, which shows the stats whether or not the game's `stats` setting does, and `debug_views`, which turns the [debug views](../tools/debug-views.md) on as the game's `debugViews` setting and `--debug-views` on the command line do.

It also takes `--capture`, `--frames`, `--script` and `--scale`, which run the game [with no window](#without-a-window) (see [the tide command](../tools/cli.md#without-a-window) for what they do). So every game `tide_add_game` builds without `HOST` has them, and so does one `tide build` made.

The profiler those show is `tide/profiler.h`, for a host of its own to show too. The host marks what each part of its frame was (`tide_profiler_begin`, `tide_profiler_mark`, `tide_profiler_end`) and adds the panel to its draw list (`tide_profiler_draw`). The game's code says the rest into the profiler's `profile` (`tide/profile.h`), while the host gives it to the GUI (`gui.profile`, which `tide_frame`'s views fill) and to what ticks run on (`tide_jobs.profile`, which `tide_world_tick_on` fills: `tide_platform_jobs_profile` for the platform's pool, or a `tide_jobs` of one thread where there's none). With neither, nothing is measured.

For a custom host, a game defining commands exposes `TIDE_HAS_COMMANDS`, `tide_local_command(local, out, capacity)` and `tide_local_command_sent(local)`. Peek the first packed command with `tide_local_command`, submit it with `tide_session_send_command(session, out, size)`, and call `tide_local_command_sent` only when submission returned true. A full session queue leaves the local item available to try again. `TIDE_COMMAND_BYTES` is the maximum packed size. The generated `tide_game_api` supplies the command validation, sequence and enqueue callbacks; custom hosts never supply a sender ID.

Hosts reach the window, input and drawing through `tide/platform.h`, which includes no system header: the generated header names types after the game's components (`Transform`, `Camera`), and the system's headers define many of the same names.

## The generated header

The generated header is the API between the game and its host. Namespaced declarations have their namespace in their C name: `Combat.Health` is `Combat_Health`.

**The world**

- `tide_world` is the whole match. Its data is in pages it shares with its snapshots, so a world starts zeroed (`{0}`, static or `calloc`), copying the struct isn't a snapshot (`tide_world_copy` is), and `tide_world_free(w)` lets it go.
- `tide_world_init(w, dt)` clears it (what it had goes), sets `Time.dt` (and `Time.tickRate` from it) and the singletons' defaults, and loads `Main` if it's the match's. `tide_world_start(w, dt, start)` starts in another scene. Both start with the match's seed, `Time.seed`, at 0, so a run repeats. A game that has a seed (it reads `Time.seed`, or has a `Random` in the match) also has `tide_world_start_seeded(w, dt, start, seed)`, which starts with the one given. Sessions pick one for each match they start, from the `entropy` function in their desc (`tide_platform_entropy` in the standard host), and seed 0 without one.
- `tide_world_tick(w)` runs every system once, then applies structural changes and events, and the tasks whose time has come go on. `tide_world_tick_on(w, jobs)` does the same on threads, with the same results: `tide_platform_jobs()` gives a pool with a thread per physical core (on the web, only in a cross-origin isolated page: NULL elsewhere), and sessions take it as `jobs` in their desc, as the standard host does. If that leaves no scene loaded, it loads `Main` again when it's the match's (`tide_frame` does the same for a local `Main`).
- `tide_world_ended(w)` says whether the match is over: its last scene unloaded and `Main` is local. A server stops there and tells every player, who go offline with `TIDE_DISCONNECT_ENDED`.
- Components are values: a world keeps each one's fields side by side with the same field of other entities, so there's no pointer to one to give. `tide_has_<Component>(w, entity)` says whether an entity has one, `tide_get_<Component>(w, entity)` gives a copy of it (zeros when it has none), and `tide_set_<Component>(w, entity, value)` puts it back changed (false when the entity has none, and nothing changes):

  ```c
  if (tide_has_Body(w, player)) {
      Body body = tide_get_Body(w, player);
      body.velocity.y = 0.0f;
      tide_set_Body(w, player, body);
  }
  ```

- `TIDE_AT(w, arch0_Body, Body, row)` gives a copy of a row's component in an archetype's storage, and `TIDE_ENTITY_AT(w, arch0_Body, row)` its entity: for tests and tools that go through every entity.
- `tide_world_player_joined(w, player)` and `tide_world_player_left(w, player)` send `PlayerJoined` and `PlayerLeft`, handled at the end of the next tick.
- `tide_world_copy(to, from)` and `tide_world_hash(w)`: snapshots and hashes. A snapshot shares the world's pages until one of them changes a page, so it costs the memory of what's different, and each page keeps its hash until it changes, so hashing reads what changed since the last time.
- `tide_world_pack(w, out, capacity)` and `tide_world_unpack(w, data, size)`: the world as bytes, as hot reloading carries it over.
- `tide_world_pack_delta(w, base, need, need_size, &size)` and `tide_world_unpack_delta(w, base, data, size)`: the world as a delta, as sessions send it: page by page, what differs from `base`, a world the receiver has too, or from the receiver's own world, which lacks the pages `need` says (`tide_world_hash_pages` lists the pages' hashes, and `tide_world_need_pages` says which of them a world lacks), or the whole world, with neither. The bytes are to `free()`. Unpacking checks the world's hash, and is false for bytes or a base that don't make it.
- `tide_world_entity_count(w)` and `tide_world_print(w)`, for debugging.
- Text fields are offsets into the world's heap: read one with `tide_text_read(&w->heap, field)`. A grid's cells are too: `tide_grid_read(&w->heap, w->Field.cells, x, y, 0, &tide_shape_Grid2_int)` gives a cell, or NULL outside the grid or where nothing in its chunk was ever set, which is zero, and the generated header names each grid type's shape.

**Local state**

- `tide_local` is this machine's local state, outside every world. `tide_local_init(local)` clears it and sets its defaults, and `tide_local_free(local)` lets it go. `tide_local_pack(local, out, capacity)` and `tide_local_unpack(local, data, size)` are it as bytes, as hot reloading carries it over. `TIDE_MAIN_IS_LOCAL` is defined when `Main` is local.
- `tide_frame(w, previous, alpha, verified, local, draw, gui)` runs every view once, blending the match between `previous` and `w` by `alpha`, then applies the local changes they made, and local tasks whose time has come go on. Pass `NULL` and 1 to draw `w` as it is, and `NULL` for `w` outside a match. `verified` is the last world the server confirmed, which `tide_session_view` gives with the other two: an entity's [local fields](../game/local-state.md#local-fields) go once it's in neither `w` nor `verified`, so a destroy the client predicted wrong takes nothing. A host with one world passes `NULL`: `w` is all there is.
- A match component's local fields are a struct of their own, `<Component>_local`, which the local state keeps for the entities something was written for: `tide_has_<Component>_local(local, entity)`, `tide_get_<Component>_local(local, entity)` (their defaults when nothing was, with text, lists and grids empty) and `tide_set_<Component>_local(local, entity, value)`. What C sets for an entity that had none is kept by its handle alone, where a view's is kept for [the entity it ran for](../game/local-state.md#after-a-rollback). `tide_get_<Component>` and the component's own struct are the match's part. A singleton's are `local-><Singleton>_local`.
- `tide_local_frame_time(local, seconds)` says how long this frame is: it's what [`Frame.dt`](../game/views.md#a-frame-s-length) reads, local tasks' `Wait.Seconds` counts it down, and presentation correction errors decay once. Call it before the session update that may supply fresh corrections, then draw with `tide_frame`. Without it, `Frame.dt` is 0, local tasks wait forever and correction errors do not decay.

**Input**

- `TIDE_HAS_INPUT` is defined when the game has an input, and `tide_input` names its type.
- `tide_input_sample(devices, local)` runs the input's `Sample` with this machine's devices. Give it a copy of the devices that the GUI's own use is taken out of, so a click on a button isn't the game's too, then mark the real ones read, so a press counts once. `tide_gui_hide` takes out what views claimed in the last frame too (`GUI.ClaimPointer`, `GUI.ClaimKeyboard`), and what was typed, which is only ever a frame's. It goes by where the copy's pointer is against where the last frame drew the GUI, so a press on a button is hidden in the sample it lands in; fill the pointer (`tide_pointer_poll`) before it:

  ```c
  tide_devices sampled = devices;
  tide_gui_hide(&gui, &sampled);
  tide_input input = tide_input_sample(&sampled, &local);
  tide_devices_consume(&devices);
  ```
- `tide_platform_poll(&devices)` fills the devices from the window, once a frame. A host that fills them itself sets buttons with `tide_button_set`, and the mouse's fields; reports fingers with `tide_touches_poll` once a frame, then `tide_touch_event` for each one that touched, moved, lifted or was canceled (`tide/devices.h`), which keeps their slots and ids; and calls `tide_pointer_poll(&devices, mouse_used)` last, which points the pointer, and with it the GUI, at the mouse or the primary touch, whichever was used.
- `tide_world_set_input(w, player, input)` sets a player's input for the next tick, and `tide_world_set_server_input(w, input)` the server's. Both repair the input first: NaN, bounds, then `Sanitize`.

**Sessions**

- `tide_game_api` is the game as a session runs it (`tide_game` in `tide/session.h`), with its settings: `tick_rate` and `title`, 0 and `NULL` where it sets none. A session whose desc leaves `tick_rate` at 0 starts its matches at the game's rate, or 60. The settings are in the generated `.c`, not the header, so they don't change the game's hash.
- With host migration (`tide_game_api.host_migration`), the host that runs a match tells its session the room's code and key every frame (`tide_session_set_room`, with `tide_platform_room_code` and `tide_platform_room_key`). A session whose match lost its server says so (`tide_session_migrating`): the host goes to the room again (`tide_platform_room_migrate`), and once `tide_platform_room_migrated` says whether this machine hosts it now, calls `tide_session_take_over` or `tide_session_join`, or fails the session with `TIDE_DISCONNECT_ENDED` if the match ended there. A match that ends tells its transports (`tide_transport.end`, before they close), which a room passes on to the relay. `tide/host.h` does all of it.
- `tide_local_take_request(local, &request, &start)` takes local code's session calls, like `Session.Start`, in order: call it until it's false. `Clipboard.Copy` comes this way too, as `TIDE_REQUEST_COPY`: put its text on the clipboard with `tide_platform_copy`. `tide_local_set_session`, `tide_local_connected` and `tide_local_disconnected` tell local code where it stands; `tide_local_set_session` takes whether the match is open too (`tide_session_status`'s `open`), and the code of the room the match is in (`tide_platform_room_code`), or `""` while it's closed; `tide_local_disconnected` takes a kick's message (`tide_session_event`'s `message`), or NULL.

### Presentation corrections

The standard host wires [provisional correction smoothing](../game/views.md#provisional-correction-smoothing) automatically. For a custom host, a game with tagged fields defines `TIDE_HAS_VIEW_CORRECTIONS` and exports `tide_local_correct(local, correction)`. Set the session's optional `correct_views` callback to forward its `tide_view_correction` into that function, using the session's `user` for your host state. A host driving a client directly uses `tide_client_desc.correct_views` instead.

After drawing the view returned by `tide_session_view`, call `tide_session_mark_presented(session)`; for a client, call `tide_client_mark_presented(client)`. This records the tick and blend fraction actually drawn without copying worlds. A replay that replaces that presentation supplies the callback with old and corrected endpoint pairs at that same tick and fraction. The worlds belong to the callback only: do not retain their pointers. A context with `reset` clears the retained errors and may have no worlds.

Call `tide_local_frame_time` before updating the session, so existing errors decay before fresh corrections arrive. These private caches are shared across views and excluded from world state and local packing. A host swapping builds must clear them with the old build's callback before replacing its table and refresh the session callback with `tide_session_set_view_corrector`; `tide/host.h` handles this too. Leaving the callback unset adds no correction snapshot copies.

## A frame

A zeroed `tide_draw_list` and `tide_gui` are ready to use. A draw list grows as a frame needs, keeps its memory for the next one, and `tide_draw_free` lets it go. It also keeps a copy of each texture the last frame drew with (see [Textures](../game/views.md#textures)), which `tide_platform_draw` uploads when it changed; a host that swaps the game's code for another build calls `tide_draw_forget`, as `tide/host.h` does at a hot reload.

```c
tide_draw_reset(&draw);
tide_gui_begin(&gui, &devices, tide_platform_screen_size(), tide_platform_measure_text);
tide_local_frame_time(&local, seconds);
tide_frame(w, previous, alpha, verified, &local, &draw, &gui);
tide_gui_end(&gui, &draw);
tide_platform_typing(tide_gui_typing(&gui)); // Phones show their keyboard while the player types, or a view asks
tide_platform_cursor_lock(tide_gui_cursor_lock(&gui)); // The cursor is locked while a view asks (Cursor.Lock)
tide_platform_draw(&draw);
```

Asking for the cursor's lock isn't having it: the system decides, and `tide_platform_poll` says whether the window holds the cursor, as `devices.mouse.locked`, which `Cursor.locked` reads. A host that fills the devices itself sets it.

## Without a window

To see what a game draws without a window on screen, or to run it where nobody watches, a host opens a headless window in `tide_platform_open`'s place. Its frame function stays as it is:

```c
tide_platform_open_headless(&(tide_headless_desc){
    .width = 1280, .height = 720, // In logical pixels
    .scale = 2.0f,                // As on a display scaled to 200%: the frame is 2560 by 1440 pixels
    .frames = 300,                // The run ends once this many drew...
    .script = "play.txt",         // ...or when its script says `quit`
    .capture = "last.png",        // The last frame, as a PNG
});
tide_platform_run(frame, NULL);
```

::: info Provisional
These names are new, and may still change.
:::

- Frames draw into pixels of the platform's own, at the size and scale asked for, whatever the display is. `tide_platform_frame_pixels(&width, &height)` gives the last frame drawn, whole, with the overlay: 4 bytes a pixel (red, green, blue, alpha), rows from the top. `tide_platform_capture(path)` writes it as a PNG.
- Time is made up: `seconds` is a 60th of a second every frame, and frames come as fast as the machine runs them. A session that should go the same on every machine takes no clock (`now_ns` left out of its desc), as the standard host's does in such a run.
- The devices are made up too: `tide_platform_poll` takes what the script says for the frame, and what `tide_platform_input` said, each event as a script's line has it without its frame (see [Scripts](../tools/cli.md#scripts)), never the system's:

  ```c
  tide_platform_input("mouse 640 360");
  tide_platform_input("button left"); // A click, in the devices at the next poll
  ```

- `tide_platform_run` ends the program with exit code 0 after `frames` frames or at the script's `quit`, whichever is first, and with 1 when a picture couldn't be written. `tide_platform_open_headless` ends it with 2, having said why, when a line of the script is wrong or nothing ends the run.

It's for desktops: there's no headless window on the web or Android. `tide_platform_read_pixels`, which tests use to check a few pixels of a draw list, works with any window.

## Memory

Worlds grow as they need, with no limits but memory: entities, rows of each archetype, and structural changes and events in a tick. A world's heap, which keeps its text, lists, maps and grids, is the exception: it holds 1 GiB, in blocks of 128 MiB at most, and filling it counts as running out of memory. Running out of memory ends the program, saying so. So does a chain of events that never ends, once it's `TIDE_MAX_CHAIN` deep in one tick (100,000 by default): each event sent, or entity spawned, by a handler of the one before. The message names the event. An archetype keeps its rows in chunks, and the first starts small, so archetypes with a few entities take little memory.
