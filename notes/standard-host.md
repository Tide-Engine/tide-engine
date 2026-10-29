# The standard host: runs with no window, the stats, debug views

## Dynamic agent play (2026-10-29)

The owner requested agents that can inspect rendered games and queue input
while the same game stays alive. `--interactive <replay.txt>` selects the
existing desktop hidden host. It draws frame 1 normally, then blocks on stdin
between explicit `step <n>` requests. No session/simulation clock advances
while waiting. Keyboard/mouse/clicks/movement/scroll/text/touch/gamepad and
player leave/drop/rejoin use script.c's existing parser and device path.
No generated C, match behavior, embed ABI or runtime function changed.

The provisional protocol is documented in docs/tools/cli.md, Interactive play:
absolute future-frame script lines queue actions (stable order for equal
frames, sorted insertion across frames); step advances 1..1,000,000 frames;
capture reads the current frame, optionally for one player; status observes;
clear frees pending actions without releasing held input; quit/EOF ends at
the current frame. Flushed `tide: interactive ready/ok/error` lines report
completion and permit recovery from malformed requests. Lines are limited to
4,094 bytes, pending events to 65,536, frames to 1,000,000,000. --frames can
cap the session, --capture/--capture-audio write final outputs; --script is
mutually exclusive. Dynamic audio-capture lines are refused because audio
not requested ahead of time can already have been discarded.

Only player 1 reads stdin and writes the replay. players.c broadcasts control
commands on the existing private coordinator connections, and acknowledges
completion/error from each process, separate from frame datagram exchanges.
Every process steps together, including while disconnected from its match.
An observation targeting another player's capture is acknowledged after the
file is written, and a failure there reaches the controller. External peers
and services do not stop with this process; --players keeps a whole local
match under frame control.

Executed events alone are flushed to the replay in frame/arrival order,
including events targeting other players. Current-frame observations append
only after successful acknowledgement. Consumed events are freed; abandoned
future plans do not enter the replay. Clean exits append the last frame's
quit. Replay uses the normal --script path and requires matching runtime
settings and starting saves/external state. Relative captures/recordings are
beside the replay file, matching ordinary scripts.

Validation: Windows Clang 23.1.2/Node 24.22.1 strict native builds and the web
platform archive build pass. tide_platform_interactive passes visual
observe/click/step decisions, held/released movement, text, no advancement
between observations, out-of-order queues, clear, error recovery/long lines,
two synchronized players, targeted input/capture, a second player's capture
failure, leave, EOF, a --frames cap, and byte-identical normal script replays.
All ten existing capture/refusal, multiplayer/refusal/network and CLI
compatibility CTests pass. The final interactive, no-GPU three-process control
acknowledgement/error test and CLI forwarding test also pass (3/3).
Docs production build passes (the existing unstaged-example 404s remain).
No browser was launched, deterministic hash changed, or extra CI dispatched.
Other desktop OS execution, Android and sanitizers remain untested locally;
the interactive command itself is desktop-only.


## A slow network to play on (2026-10-27)

The owner's ask: tune how a match feels online (Parry's blocks, struck through a guard that looked up in time) without
two machines on the internet, from `tide run`, scripts and `--capture` included.

- **Options** (`tide_run_configure`, `tide_run_condition` in `tide/host.h`): `--latency <ms>` each way (0 to 10,000),
  `--jitter <ms>` (up to that much more each way, at random, so datagrams also reorder), `--loss <percent>` (0 to 100,
  each way). `<player>:<value>` (`--latency 3:150`) is one player's of a `--players` run and wins over everyone's,
  whatever the order; a player the run doesn't have, a player's value without `--players`, and player 0 are refused with
  exit code 2 before anything opens. `tide run` passes them on (repeatable: `game_args` grew to 128 in
  `compiler/cli/main.c`), `--server` refuses them, and `tide run --web` puts them in the page's arguments. On the web the
  address's `latency=`, `jitter=` and `loss=` are the same words (`tide.js`, beside `stats`; the page's own
  `Tide.arguments` win), so a page takes them as a desktop command line does.
- **One implementation, the loopback's** (`tide_net_conditioned`, `tide/net.h`): a transport wrapping another, both ways,
  whose queue, seeded randomness and conditions are a `tide_loopback`'s (`queue_put`/`queue_take` in `engine/src/net.c`,
  which the loopback's own send and receive now call: the same draws in the same order, so seeded fixtures are
  unchanged). What's sent waits on the queue as endpoint 1 (the address it's for kept as `from`), and what the inner
  transport brings waits as endpoint 2, from when it arrived (`inner.arrived`, else now). Its time is the inner
  transport's clock, or the clock it's given where there's none (`--players` ends: the host gives `tide_run_now`, the
  run's made-up time). Datagrams move only when it's used, so on a real network they go and arrive at frame granularity.
  Its `clock`/`arrived` are the inner clock and the delayed arrival, so the session's ping counts what's added. Closing it
  sends what was on its way out at once (a goodbye isn't lost), then closes the inner one.
- **Where it applies** (`tide_run_conditioned`, Claude's pick, Provisional in `docs/tools/cli.md`): a match this machine
  joins, every way it joins one: `--join`/`--connect` and `Session.Join`/`Connect` (rooms, UDP, the made-up network, a
  game's own network), a dedicated server's match (`tide_run_hosted_request`) and a migrated match joined again. A machine
  that runs the server adds nothing, as the web's `Tide.lag` already did (notes/web.md): its player has no way to go, and
  each joiner's way is that joiner's. So in `--players 2 --latency 80` player 2 is 80 ms from player 1's server each way,
  and in three, players 2 and 3 are 160 ms from each other through it. One machine's setting delays both ways, so it's
  meant for one side: two machines both set to 80 would be 160 apart.
- **Repeats.** Each process seeds its own (`0x6E6574636F6E6400 ^ tide_platform_player()`), and a `--players` run's time
  is made up, so the same datagrams are lost and late every run: a run with jitter and loss repeats byte for byte.
  Real-network runs use the same seed but real clocks, so they don't repeat.
- **Not the web's `lag`.** `Tide.lag`/`lag=` (agreed 2026-10-05) is a target ping that the page reaches by measuring
  what the trip takes and holding back the rest in `tide.js`; `latency` adds a fixed amount each way in the program. Both
  can be on; they add up.
- Tests: `tests/test_net_conditioned.c` (engine, native and WASI): both ways' delays and arrival time by the inner
  clock, jitter reordering and repeating by seed, loss each way, a close sending what was on its way, and a clockless
  inner transport taking the given clock. `tide_platform_players` (`players.mjs`, label `gpu`): two players at
  `--latency 80 --jitter 10 --loss 5` both see both blocks end where they do, the second's ping is 150 ms or more
  (`players.tide`'s yellow block at 300,140), the first's has none, and the run repeats byte for byte; three at
  `--latency 3:80` give the third a slow ping and the second the made-up network's own. `tide_platform_players_refused`
  has the six refusals. `tide_cli_run_exit` passes the options on (and an option given 48 times is still refused).
  `tide_platform_web_debug_views` has `latency=80&jitter=10&loss=5` in its address and checks the host's conditions.
- Measured: not timed; the added cost is one queue scan a datagram, only where an option is set.
- Not built: changing them while a run goes (a script line such as `player 2 latency 150`, or a key), bandwidth limits,
  and conditions for a machine that runs the server.

## Time travel inside another program (2026-10-26)

`tide/embed.h` gained the debugger's calls (embed ABI 7; notes/unity.md, Time travel, has the design, decisions and
tests): `tide_embed_travel_of`, `_travel_keep`, `_pause`, `_step`, `_show`, `_continue`, `_recording`, `_replay`,
`_replay_end`, on `tide/timeline.h` (platform). The embed library and `tide unity --web`'s program export them
(`embed_exports`, `unity_web_exports` in `compiler/cli/build.c`).

The host's part: `tide_run_unblended` (`tide/host.h`), set by the embed for a paused match's frames, makes
`tide_run_frame`'s views draw the session's latest tick with no previous world and alpha 1; nothing else of the frame
changes, and no other host sets it. The loop handing local code's commands to the session at the end of
`tide_run_frame` is `tide_run_send_commands` now, which the embed also calls before a paused match's step. `reload_library.inc` has `reload_before(old, next)`, called first in `swap` and
`restart` while the old build's code is still loaded; only the embed sets it (time travel's worlds and local state
are the old build's), so `tide run`'s host is unchanged. Frames that show another tick than the latest don't go
through `tide_run_frame`: the embed's `show_frame` runs the views over that world with a local state of its own and
the host's draw list, GUI and devices (consumed after it), and writes the view packet from it.

## Runtime stats argument and documentation pages (2026-10-25)

The standard host now recognizes `--stats` in its runtime command line and
enables `tide_run_settings.stats`. It leaves `stats_open` and the drawer's
configured initial open/closed state unchanged. Ordinary standalone builds
therefore start with the collapsed line; a CLI `tide run --stats` host keeps
its existing generated `stats_open = true` choice. No runtime API, layout,
measurement policy or reload ownership changed. The embed's independent
option parsing retains its existing behavior.

`platform/web/tide.js` maps the presence of `stats` in a page's query to
`--stats`, unless the page-owned `Tide.arguments` already contains it. Repeated
query keys still add one argument; existing host/join/connect selection and
debug-view options keep their behavior. Docs embedded and standalone launch
paths request this query (`notes/docs-site.md`). The root owns regeneration
of the checked-in release web runtime before builds or publication.

`platform/tests/web_stats.c` configures the real host against a copied game
without its stats setting, checking disabled-by-default, runtime enable with
a collapsed drawer, preservation of a preconfigured open drawer and repeated
arguments. The existing browser drawer/input/timing checks remain intact.
`cmake/web_runtime.test.mjs` executes the production argument-assembly block
in a VM, checking exact NUL-terminated WASI argv for no query, unrelated keys,
stats, duplicate query/config requests and concurrent debug/join/host options.

Final integration: the full native and web builds pass. The native full suite
passes 2,773 of 2,774 enabled tests; the sole failure is the unchanged private
CRACKED hash failure reproduced on the pre-existing baseline. Repository web
checks pass, including 11 scenarios on all three backends. Optional private
InsideOut WebGL play has a mismatch under read-only attribution, and remaining
optional private playthroughs are running; final overall web-suite status
remains pending. Node argv/query tests and both WebGL/WebGPU stats tests pass.
Native material-demo runs with
`--frames 240 --stats` at 376 x 400 and 960 x 540 pass, fit the cube above open
controls and show the collapsed stats line at bottom right; the base-demo
capture also shows its collapsed 60 FPS line. Final Svelte check reports zero
errors/warnings, the staged production docs build passes, and fresh combined
material code/docs integration review is clean. Final selected docs browser
runs pass for both demos on WebGL/WebGPU, with scene-pixel witnesses for orbit,
zoom, exact camera Reset restoration, height command, cutoff/opacity sliders
and collapsed zoom above stats, plus displayed-source and stats-URL deduplication
checks. Captures are `build/material-docs-final-webgl` and
`build/material-docs-final-webgpu`; WebGL cutout/blend/collapsed shots were
visually inspected. All three exports/admin HTTP checks and 301 grammar checks
pass; the latest five-test playground rerun is active. Isolated JavaScript
syntax and diff checks pass; this child ran no browser.

## GUI selection clipboard callback (2026-10-25)

The standard host binds tide_gui.copy to tide_platform_copy after begin, allowing
Ctrl+C/X selection editing without including platform headers in the GUI runtime.
The callback is optional for custom hosts; hidden runs retain the platform's
existing refusal to access the system clipboard. GUI layout changes require
hosts/games to rebuild together. Contracts and validation: notes/gui.md.

## Worker limits and profile rows (2026-10-25)

Custom hosts can supply tide_gui.worker_limits; zero fields select the internal
64 pending/64 MiB capture/4 MiB per-frame transfer defaults. The standard host
uses those defaults. These are soft private-job limits, not a process memory
cap; first-class task argument state follows normal allocation rules.
The profiler adds Workers per-job rows (queue/compute/capture/transfer) and
Worker copies frame cost. Callbacks only record timestamps; the frame thread
updates profile structures after safe provider completion. Contracts and
measurements: language.md, schedule-and-threads.md, benchmarks.md.

## Local workers (2026-10-24)

The standard host supplies its existing platform pool in tide_gui.jobs before
each generated frame. Local worker captures/results are private to generated
local state. Missing platform background support keeps the synchronous poll
fallback. Free/rebind drains callbacks before game code unload. Custom hosts
opt in through the same optional GUI field; views keep their serial draw order.
Contracts and coverage: notes/language.md and notes/schedule-and-threads.md.

## Optional voice provider (2026-10-22)

The host table's optional `voice` getter obtains registration from the game's
own engine copy. After views and before audio submission, a non-embedded host
runs its provider's frame callback with the session/audio list/current time.
Teardown forgets audio before calling stop; retained native reload also stops
the old provider before unload. Games with no registered provider allocate or
send nothing for voice. The actual three-process headless capture regression
and all C/API/lifetime contracts are in `notes/voice-package.md`.

## Leave, drop and rejoin in a run of several players (2026-10-21)

The owner approved Claude's proposal after AI trials could not test
`PlayerLeft` or seat reuse: three script lines, each needing `player <n>` and
`--players` of 2 or more (`script.c` refuses either missing, naming the fix).

- `player <n> leave`: that player's host does what `Session.Leave` does
  (`tide_run_request` with `TIDE_REQUEST_LEAVE`). The match hears `PlayerLeft` at once.
- `player <n> drop`: `tide_platform_players_drop` (`players.c`) sets `silent`. From
  that poll on, `end_send` drops whatever this player's ends send, and `deliver`
  drops whatever comes for them, so each side times out (`TIMEOUT`, 5 s: 300
  frames). Routing through the first player's process is untouched: a dropped
  first player still routes the others.
- `player <n> rejoin`: clears `silent` and asks the host to join again what it
  last joined. The host keeps the last Join or Connect it made in
  `tide_run_joined`; Main's automatic join at `tide_run_begin` and a game's own
  `Session.Join`/`Connect` both count. The session keeps its cookie for the same
  server, so the player is the same `PlayerID`. A match that already let it go
  hears `PlayerJoined`; one that hadn't noticed the drop gets the takeover with
  no events. A player that never joined says so on stderr and does nothing.
- Leave and rejoin cross from the platform to the host as
  `tide_platform_players_action` (`tide/platform.h`), a queue of up to 16
  actions taken in the script's order right after the frame's poll, in
  `tide_run_frame`. Drop needs no host. Web and Android compile the queue and
  never fill it.
- A host that leaves or drops is allowed (the proposal had refused player 1):
  in a game that starts in a menu player 1 needn't host, and the others getting
  `ServerLeft` or timing out is what a network does.

Tests: `tide_platform_players_net_drop` (no GPU; every desktop in CI): a drop
silences both ways, the queue returns rejoin then leave in order, and speech
resumes after the rejoin. `tide_platform_players` runs `players_leave.tide`
with three players. A leave is heard at once; a drop only at the timeout, on
both sides; both rejoin as their old seats with no new `PlayerID`; a drop and
rejoin inside the timeout is a takeover; the host leaving gives the others
`ServerLeft`. The run repeats byte for byte. `tide_platform_players_refused`
has the three refusals. User docs: CLI Scripts and Several players, and the AI overview.

## Inside another program's frame (2026-10-20)

`tide/embed.h` (`platform/src/embed.c`) runs the standard host's loop inside
another program's frame loop with no window, GPU or audio device: phase 1 of the
Unity front end the owner approved (`notes/unity.md`, Phase 1 interface).
`tide_embed_create` takes a linked game's `tide_host_game` (`tide/run.h`'s
`tide_host_game_api`), its pack, tick rate, command line and relay, and starts
Main's match or what the command line asks, through `tide_run_begin`. Each
`tide_embed_frame(e, seconds, input, width, height, scale)` is one
`tide_run_frame`; the session's clock is the frames' summed lengths (NaN or
negative add none). `tide_embed_status_of` gives a flat copy of
`tide_session_status` (no compiler padding, size asserted, so C# lays it out
the same on every platform), the room code (`tide_run_room`, what the frame
told local code), a kick's message and what the game asked of the window
(typing, cursor lock); `tide_embed_request` takes Session
requests (Start of Main only for now: named scenes come with the schema);
`tide_embed_background` leaves the match on Android and iOS, desktop nothing.
`tide_embed_camera(e, transform, projection)` is the camera the frame's views
start with, the other program's, as `tide_draw_set_camera_matrices` takes one:
16 floats each, column by column. The transform places the camera, which looks
along its +z (Unity's camera transform, unscaled). The projection is OpenGL's,
looking down -z (`Camera.projectionMatrix`). It took a position, rotation and
field of view from 2026-10-21; matrices came on 2026-10-22 so Tide's 3D shares
Unity's depth, near and far planes, and orthographic cameras.
`tide/host.h` keeps the matrices (`tide_run_view_camera_transform`,
`_projection`) and sets them right after `tide_draw_reset` and before the views,
so `Draw.WorldToScreen`/`ScreenToWorld` of a float3 and 3D draws go through it;
a view's own `Draw.Camera` still wins. NULL for either, a value that isn't
finite, or a matrix with a zero determinant goes back to Tide's first camera.
One at a time: the host's statics are the process's.
`tide_embed_abi()` returns `TIDE_EMBED_ABI`: 5 since `tide_embed_audio`,
`tide_embed_microphone` and the status's `microphone` byte (2026-10-22: an
embedded frame submits its audio, which the other program plays;
`notes/audio.md`, Inside another program), 4 since `tide_embed_render` and the
matrix camera, 3 since the first camera, 2 since `tide_embed_draw`. It changes
with anything another language lays out or calls, so a binding refuses an
embed built by another tide. Test: `embed_camera` (`platform/tests/embed.c`).
`embed.tide`'s Show projects (0, 0, 5) and draws a circle there in screen
pixels. Through Tide's camera it lands at the 800 by 600 screen's middle; through
a camera 1 to the right with a 90-degree perspective projection, 60 pixels left
of it; and in the middle again after a projection of zeros.

`tide_embed_render(e, &size)` (2026-10-22) draws the frame's list with Tide's
renderer through the recording backend (`notes/rendering.md`, Recording) and
gives its commands, which Unity's TideRender replays (`notes/unity.md`, Tide's
renderer in Unity). `tide_embed_render_reset` forgets the objects made, so the
next render makes everything again. `tide_embed_destroy` frees the backend's
memory, and the next embed numbers from 1. The embed host exports both.
`tide_embed_render_view(e, transform, projection, width, height, &size)` draws the
frame's 3D again through another camera (Unity's scene views): the window's 3D alone, through
that camera's world-to-clip matrix (made as `tide_draw_set_camera_matrices` makes one), the
2D, clips and targets left out. NULL for a size outside 1 to 16,384 or a value that isn't
finite. Test: `embed_render` (also: the view's commands draw `embed.tide`'s cube alone,
through the 3D meshes' pipeline, its uniforms that camera's matrix).

Events and text commands (2026-10-22): the view packet is version 3, with an
EVENTS section, the match's events this machine heard since the last packet as
its local handlers hear them, each heard as predicted (flag 1) and again once
confirmed (flag 2), with its tick, target and the target's place among the
packet's entities; and `tide_embed_command` takes a command's text and byte
lists after its value, as STRINGS lays them out, saying false for text that
isn't UTF-8 or a command too big to send. Both are the generated game's
(`notes/language.md`, Events, text commands and command defaults); the embed
host passes them through unchanged, and `TIDE_EMBED_ABI` stays 3 (no function
or struct of `tide/embed.h` changed; the packet says its own version). Tests:
`embed_heard_events`, `embed_commands_and_scenes`. Version 4 the same day: lists, maps, sets and grids, what they
hold included, each STRINGS entry from a multiple of 8 (`embed_containers`).

Stats inside another program (2026-10-21, owner: `--stats` in the runner's
Arguments did nothing). They were left out embedded ("the other program has
stats of its own"), and the embed took no `--stats`. Now `embed_options`
reads `--stats` as `tide run --stats` does (the setting, and the drawer
starting open), and the game's `stats` setting counts too: `tide_run_frame`
keeps the drawer embedded, drawn into `tide_run_draw` after the views and the
GUI, so `tide_embed_draw` has it for the other program to draw, rather than
into `tide_run_over` for a window. The values say `elsewhere`
(`tide_stats_values`, `engine/include/tide/stats.h`), which leaves out the
draw calls, vertices, render and GPU lines (the other program's), and memory
is 0, as the process is the other program's. Tests: `embed_stats` (the
packet has the "N fps" line and `cpu`, and no `draw calls`) and
`stats_drawn_elsewhere_leave_out_the_other_programs_lines` (`tests/test_stats.c`).

- Input is raw, as a window reports it (`tide_embed_input`: held keys as bits,
  cursor, this frame's scroll and locked movement, buttons, one gamepad, typed
  code points, this frame's touch events, all top-left y-down). The approved
  draft named `tide_devices`; raw input replaced it so presses latch and
  movement adds up in the platform's own poll, unchanged.
  `tide_platform_embed_frame` (`platform/src/embedded.h`) makes it the poll's
  source in the window's place, as a headless script is (`key_held`,
  `mouse_state`, `gamepad_state`, `touchscreen`, `take_touch`, `take_char` in
  `platform.c`), and sets the screen; sizes outside 1 to 16,384 and scales
  outside 0.25 to 16 (NaN too) become 1. Embedded, typing and cursor lock are
  kept for the status, the clipboard is left alone and no paste is taken.
- `tide_run_embedded` (`tide/host.h`) keeps the frame off the GPU timers, the
  draw and audio submission and the stats drawer; the draw list is still
  recorded, and `tide_embed_draw` gives it to the other program as bytes (made
  when asked, into a buffer the embed keeps): a 64-byte
  `tide_embed_draw_header` (magic "TIDD", version 1, counts, this build's
  struct sizes, the screen), then commands, shapes, vertices, indices, text,
  matrices and text turns, each from a multiple of 8, zeroed between.
- The host's own option parsing ends a program at a bad flag, so the embed gives
  it none: `tide_run_arguments` still reads `--host`/`--join`/`--connect`, and
  `--seed`/`--data-dir` are parsed apart, a bad one left out with a line.
- `tide_platform_set_relay` (`rooms.c`) picks a relay over `TIDE_RELAY` and the
  default on desktop; a page's is its own (`Tide.relay`), so it does nothing on
  the web.

- A game built with `--embed` (`tide_host_game.embed`, `tide/embed_game.h`;
  `tide/run.h` fills it and `.layout` when `TIDE_HAS_EMBED` is defined) gets
  its view packet written after every frame from `tide_session_view`'s worlds
  (`tide_embed_view`), its schema from the game itself
  (`tide_embed_game.schema`, built again when a new build comes in), commands
  with an optional target (`tide_embed_command`) and START by a scene's name,
  limited to the scenes `tide_start` has: those the game's code starts.
- `desc.library_dir` (desktop): the newest `game-<n>` there and its newest
  `pack-<n>`, loaded and swapped as `tide run`'s host does, through
  `platform/src/reload_library.inc`: the helpers moved out of `reload.c` so
  `embed.c` runs them on its own copy of the host's statics (each file that
  includes `tide/host.h` has one). Embedded, a failed full restart leaves the
  game stopped and saying why rather than exiting the program.

Tests: `tide_platform_embed` (`platform/tests/embed.c`, `embed.tide`, built
with EMBED; native only: on the web it builds, but needs a page's imports)
runs Main at 30 Hz from made-up 60 Hz frames, one at a time, a one-frame key
tap reaching one tick, the screen reaching `Screen.width`, requests (leave,
start again, refused kinds), bad flags left out, NaN or negative frames and
screens, the packet's bytes as another language reads them (two entities,
owner flags, a root, blended values, text and a list through STRINGS, a
singleton), the schema's types and kinds, a command reaching its handler and
refusals of a wrong size or type, a scene started by name, and the draw
packet (its header, parts, and the view's rect and text among its commands,
nothing once ended). The library path is covered end to end by
`unity/Sample`'s play mode test.

On the web, `embed.c` runs in the program `tide unity --web` generates
(`compiler/cli/build.c`, `unity_web_main`): `main` creates the embed with the
page's pack (`tide_platform_pack`) and runs `tide_platform_run`, whose frame
calls `tide_embed_frame` with the input Unity's jslib last copied in (zeroed
for a frame without, as the worker's while the page is hidden), and exports
`tide_unity_*` for the jslib (`notes/unity.md`, Web as built).

## Window scale frame metadata (2026-10-20)

The host supplies `tide_platform_screen_scale()` in the draw list's
`screen_scale` before every frame, beside the existing target-pixel callback.
Generated `Screen.scale` is the same read-only frame-local float2 as the
renderer's physical/logical ratios. Zeroed custom-host metadata defaults to one
per axis. This adds no frame pacing, simulation pause or lost-contents event.
Hosts/libraries rebuild for the draw-list layout change. Validation is in
`notes/language.md`; other new Inside Out ideas are saved in the prototype audit.


Texture metadata (2026-10-18): before calling the generated frame the standard
host supplies tide_platform_target_pixels in tide_draw_list.target_pixels.
This borrowed callback crosses the host/game-library boundary explicitly;
the generated frame saves/restores its thread-local binding. Custom hosts that
need render-target Texture.size provide the callback too. See notes/textures.md.

## Explicit save directory for test runs (2026-10-18)

UNDERFOOT's scripted tests wrote player saves. The desktop standard host now
takes `--data-dir <folder>` in both bounded and windowed runs. It overrides
`Application.persistentDataPath` and relative I/O, creating parents when the
game uses files. It is explicit: existing saves remain the default, the selected
folder survives exit/repeated runs, absolute file paths still name those files,
and temporary caches keep their normal paths. It does not select headless mode.
Several `--players` processes use `player-1`, `player-2`, etc. under that folder;
one player uses the folder itself. Web, Android and dedicated server runs reject
the option; the CLI does so before building. Paths forwarded by `tide run` are
made absolute relative to the invoking directory.

The host passes the directory to `tide_platform_io_init(app_id, directory)`;
NULL selects normal storage. The provider resolves native roots to full paths
before use and keys reuse by app ID and selected root. Rebinding the same root
preserves pending flushes; selecting another root forgets old work through the
existing app-switch path. An invalid/unwritable root reports ordinary I/O failure
and never falls back to actual saves. The web C provider requires NULL. Existing
C callers were migrated to NULL; no generated Tide/runtime function changed.

`tide_platform_data_directory` runs the real host with relative/nested/space and
absolute roots, repeated/scripted runs and two players, checking actual files
and the runtime path. It uses only its explicitly created scratch directories.
`tide_platform_io` checks independent same-app roots, pending flush reuse,
unusable-root failure and restoring the default. CLI forwarding, help and
unsupported/missing-argument tests cover dispatch; capture refusal tests cover
the host parser.

Validation: warning-free full Windows native and web debug builds with Clang
23.1.2 and Node 24.19.0. All 2,372 native CTests pass (60.54 s, including 32
GPU tests on Vulkan/OpenGL), and all 2,282 web CTests pass (201.32 s, including
WebGL/WebGPU). The expanded CLI unsupported-target/missing-directory checks
also pass afterward. The docs production build and diff checks pass; docs retain
their existing unstaged-example 404 placeholders. Math/random/world hash pins
are unchanged. Every run used TIDE_HIDDEN; browser suites retain mute flags.
Android, other OSes, sanitizers and private prototype playtests were not run;
no CI was dispatched or deployment made.

Managed hosting (2026-10-15): `managed_run.h` is a separate single-threaded WASI
reactor launcher without rendering, platform devices or a local client. Ordinary
host admission copies `tide_game.simulation` into protocol 2 and reports a clear
update message on mismatch. The session protocol independently checks it too.
Typed native entry starts are reused; unsupported native process hooks fail
closed. Contracts/tests/launch limits: managed-hosting.md.

Dedicated service (2026-10-15): serve.h/server_run.h run authoritative matches
without the graphics capture host or any local player/view/audio. The ordinary
host recognizes Connect("host:port/id") and built-in Join("room/id"), with 0
requesting creation, pumps admission before its session and reports the assigned
endpoint in Session.room. Game networks retain their Join text. Dedicated
creation/authentication, ownership, limits and tests: dedicated-servers.md.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Vertex count in the stats (2026-10-15)

The open drawer now has a `vertices` row after the GPU/draw-call row. The
host passes the current frame's `tide_platform_draw_vertices` through the
64-bit `tide_stats_values.vertices`; the row also shows zero for an empty
frame and exists outside a match. Its scope is the game's submitted geometry
in every pass, excluding the host overlays; see `notes/rendering.md`.
`TIDE_STATS_LINES` is 16 to leave room for the additional row alongside both
render timers and all three slow systems. Existing layout/input tests cover
the added row, small-screen truncation and a value above 32 bits. User-facing
counting rules are in `docs/tools/cli.md`.

Validation: Windows Clang 23.1.2, Node 24.19.0; the full native debug suite
(2,337 tests) and web debug suite (2,250) pass after correcting the new blit
fixture's vertex layout and rerunning the affected draw tests. Native coverage
includes OpenGL and Vulkan with both descriptor paths under validation; web
coverage includes WebGL/WebGPU and exact host-to-drawer vertex totals.
The production docs build and `git diff --check` pass. A 960Ãƒâ€”540 hidden capture
was visually checked: a clear plus one rectangle reports 9 vertices, with the
drawer excluded. Android, other OS builds and sanitizers were not run; no CI
was dispatched. Hardware vertex-shader invocation counts remain outside this
submitted-geometry counter's scope.

## Rendering and platform

- `tide/run.h` is the standard host: `tide_run(&(tide_run_desc){.title = "...", .argc = argc, .argv = argv})` opens a window and runs the game in a session (`tide/session.h`), sampling this machine's input from the devices once per tick it runs, and draws its views and their GUI. The devices keep what happened for the next sample (a key that went down and up between two ticks, how far the mouse moved), and a sample clears them (`tide_devices_consume`). Where no tick can sample, the host clears them itself at the end of each frame (`tide_run_frame`): in a game with no input, and while the session isn't connected (in no match, connecting, or finding a match that changed hands), which is the session's to say, never a count of frames. So a match started from a menu reads what's held as it starts, never what was pressed before it; in a match that runs, a frame with no tick clears nothing. `tide_platform_web_first_tick` (`platform/tests/web_first_tick.c` with `first_tick.tide`) checks both in a page, and `tide_platform_first_tick` (`first_tick.mjs`, label `gpu`) the first in a run with no window. When `Main` is the match's, it starts at once, and opens it with `--host [port]`, or joins another with `--join code` (a room's) or `--connect address`. This machine's player joins before the first tick. A game needs no other C. Its loop is in `tide/host.h`, which runs a game through a table of its functions (`tide_host_game`), so that `tide run` can swap builds (see Hot reloading).
- Seeing what a game draws needs no window (agreed: agents never open one on the owner's screen, and CI wants pictures too; the names are provisional). The standard host runs a game headless when its command line has `--capture <file.png>` (the run's last frame, as a PNG), `--frames <n>` (the run ends after them) or `--script <file>`, with `--size <width> <height>`, `--scale <s>` (as on a display scaled that much) and `--seed <n>` (the seed of its matches, below), so every game `tide_add_game`, `tide run` and `tide build` make has it: `build/debug/bin/duel --script play.txt --size 1280 720`. `tide run` passes them on, and the two of audio's that make a run one with no window too (`--capture-audio`, `--microphone`: `notes/audio.md`), and runs the game once: nothing is watched or reloaded, and tide's exit code is the game's. Which options `run` passes on is a list of its own, which the host's has to be kept with (`notes/cli-and-packages.md`). It's the way to look at a game: no scratch host, no hidden window of one's own.
  - A script (`platform/src/script.c`, `docs/tools/cli.md`) is a line an event, the frame it happens at (from 1, in order) and what: `120 key d down`, `200 key enter` (a tap), `210 mouse 640 360` (logical pixels from the top left), `211 button left`, `220 move 40 -8`, `230 scroll 0 -1`, `260 text K7QF2M`, `300 touch 1 down 100 200`, `330 pad buttonSouth down`, `400 capture fight.png` (beside the script) and `500 quit`. A line that's wrong is named, with what to write, before anything opens.
  - `tide_platform_open_headless` (`platform/src/platform.c`) is what the host opens. Its window is GLFW's, never shown, and only there for its graphics context: frames draw into a target of the platform's own (`gpu_state.frame`), the size asked for times the scale, through the path a window's frames take (not `offscreen`), so text is measured and fitted at that scale, and the stats and the profiler are in the picture. `tide_platform_frame_pixels` reads a frame whole, and `tide_platform_capture` writes it as a PNG (`platform/src/png.c`: ours, from the RFCs, deflate's fixed codes with matches, or stored blocks where that's shorter, so a frame of flat colors is a few kilobytes).
  - Nothing about a run is this machine's, or timed. The frame loop makes time up, a 60th of a second a frame (`step`), frames are counted, and the host's session takes no clock and no entropy (`tide_run_new_session` in `tide/host.h`, which every session the host makes comes from), so its matches start with seed 0 and a game that rolls plays the same every run. `--seed <n>`, from 0 to 2147483647, picks another seed, to see the game roll otherwise: it's the seed of every match the machine starts in that run (`tide_session_desc.seed`, which a session with no entropy starts its matches with). It's the standard host's on every platform and with a window too, where it stands in for the system's entropy, and `tide run` passes it on. The devices are made up too (`script.c` says what `window.h` would, and `tide_platform_poll` takes it the same way, so the GUI's hiding and views' claims go as in play): never the system's, as even a hidden window knows where the real cursor is. The cursor locks whenever the game asks, and the clipboard is left alone. So a run's match is the same on every machine, and its pictures on one machine byte for byte; GPUs differ in what they blend.
  - Exit codes: 0 at `--frames` or `quit`; 1 when a picture can't be written; 2, before anything ran, for a flag or a script's line that's wrong, or a run nothing ends. Desktop only: a web page is driven and screenshotted in headless Chrome, as the web tests do.
  - `tide_platform_png` (`platform/tests/png.mjs`, every platform, no GPU) reads what the writer wrote with Node's zlib; `tide_platform_capture` (`capture.mjs`, label `gpu`) runs `capture.tide` with a script of every kind of input and reads its pictures' pixels, the match's seed and what it rolled from it among what they show, then runs it again for the same bytes, and with `--seed` for the seed it asked for and other rolls; `tide_platform_capture_refused` checks what a run refuses, with no GPU.
  - **Recordings** (the owner's ask, 2026-10-27: they record runs as GIFs a lot, and had stitched `capture` lines together with ffmpeg; the owner picked GIF from an encoder of Tide's own, runs with no window, and `--record` plus script lines, over MP4 and over recording a windowed game). `--record <file.gif>` (`tide_headless_desc.record`) records the whole run; a script's `record <file.gif>` records from its frame and `record stop` ends it, as do another `record` and the run's end (`tide_platform_record`, `_record_stop` in `tide/platform.h`). The two are separate slots in `platform.c` (`run_recording`, `script_recording`), so a script's stretches don't cut the run's. A recording takes every second frame from the one it started at, the frame a `record` or `record stop` line is at among them when it's one of its own: 30 a second, because players show a frame of under 2 hundredths for a tenth, and GIF counts in hundredths, so delays go 3, 4, 3 and a recording lasts as long as the run would in a window. Each frame is read back (`tide_platform_frame_pixels`), encoded and written at once, so a recording holds a frame and the last one's pixels, never the run. Its file is named as a capture's is: beside the script for a line, from where `tide` ran for the flag (`tide run` makes it absolute, in its list of file options), with the player's number in a run of several. `finish` writes out what's still recording when a run ends otherwise; one that can't be written makes the exit code 1, as a picture does.
    - `platform/src/gif.c` is ours, from the GIF89a specification and Netscape's looping block, as `png.c` is from the RFCs. A frame is the rectangle that changed since the last, its unchanged pixels transparent (disposal 1, so the last frame shows through) and the rest with a palette of their own: their exact colors when there are 255 or fewer, and otherwise 255 by median cut over 5-bit bins, each box its pixels' mean (exact colors from the sums, not the bins' centers), each bin the nearest. No dithering, so a run's recording is the same bytes every time, as its pictures are. LZW with a 13-bit open-addressed table, which starts over at code 4095. A frame like the last is one transparent pixel. Measured: 240 frames of `towers` at 960 by 540 recorded as 120 in 447 KB, its last PNG 35 KB, and the debug run took 2 seconds.
    - Tests: `tide_platform_gif` (`platform/tests/gif.c`, `gif.mjs`, every platform, no GPU) decodes made-up recordings with the tests' own reader (`gif_read.mjs`): a panel that moves and stops (exact, the changed part only, a still frame as one pixel), a gradient of more than 256 colors (within 24 of each pixel, 3.25 on average) and noise of many codes (exact, through the table's restart); its delays and that it loops. `tide_platform_capture` records the whole run and a `record`/`record stop` stretch of `capture.tide`, checks their frame counts and lengths (60 frames and 2 seconds; 6 and a fifth), that frame 5 of each is the PNG captured at it, and the same bytes on a second run; `_refused` checks a bare `record` and `--record` without a file. `tide_cli_run_exit` passes `--record` on as a path.
    - Not built: a windowed game's recording (a key or a flag while a player plays), other rates or a cap on size, MP4 or another format, and `tide open`'s editor runs, which weren't tried.
- **A run of several players** (`--players <n>`, the owner's card of 2026-10-08): a run with no window plays a game `n` times at once, as the players of one match, and takes a picture from each. Before it, two players with no window on screen meant two pages in headless Chrome through the relay: a windowless run plays 1,200 frames in 1.6 seconds, so the session's 5 seconds of patience are 0.4 of a clock's, and a `--host` run was over before a `--connect` one started. Every agent of the 2026-10-06 trial asked for it too.
  - **The design is the owner's pick of three** (2026-10-08). The two turned down: `--realtime`, a windowless run paced to the wall clock so that `--host` and `--connect` processes meet (small, but a run takes real time, nothing repeats, two scripts line up only by a margin, and its test rests on timing); and the players in one process (the same from outside, but the standard host's some 45 statics and the platform layer's devices, script, mixer and frame all become a player's, and a game's C statics would be shared by players as no match shares them). What's picked inside it is Claude's, Provisional in `docs/tools/cli.md`: the names, who's put in the match, the rooms' codes, what an address reaches, the files' numbers.
  - **Each player is a process, and they go a frame at a time together** (`platform/src/players.c`, `players.h`). The first player's process is the one the command line started. In `tide_platform_open_headless`, once the script is read and before it has a window or a thread (a fork takes neither along), it listens on TCP at 127.0.0.1, any port, and starts itself again for each of the others (`start_copy`: `CreateProcess` or `fork` and `execv`, on its own path), with its command line and `--player <number> <port>` after it. The others connect and say which they are. Then `step` (`platform/src/platform.c`) calls `tide_platform_players_step` once a frame drew, unless it was the run's last: each player sends what it said of the frame (a size, then records) and waits for what it's told; the first reads everyone's, routes it (`route`) and answers everyone. So a datagram sent in frame k is read in frame k+1, by whom it's for, in the players' order and then the order it was sent in, the same every run, and no player starts a frame the others haven't finished the one before of. Nothing in it takes a clock: the one wait with a time on it is `gather`'s, a 20th of a second at a time for a connection, between which it asks whether a player that hasn't connected has ended, which only decides how soon that's noticed.
  - **The network is made up below the host's requests.** While `tide_platform_players()` isn't 0, `tide_run_request` (`tide/host.h`) opens, joins and connects through `tide_platform_players_open`, `_join` and `_connect` in the place of `tide_platform_host_open`, `tide_platform_room_join` and UDP, the room's code is `tide_platform_players_code`'s, and a room that failed is `tide_platform_players_failed`'s. No port opens, no name is looked up and the relay is never asked. A process has two ends at most: where its match takes the others, and where it reaches a match it joined. A session closes the end it had after it has the next, so ends are numbered and only the last of its kind is live: a close of an older one does nothing, and a datagram for one is dropped. A match sees who joined it as `{TIDE_ADDRESS_ROOM, host: which of the player's ends, port: the player}`, so a player who joins again is another address, as on a socket of its own; a joiner has its server as a room's address (the code, packed) or as the IPv4 address it was given.
  - **Rooms and addresses.** A player's room has six of the player's own character for a code: `AAAAAA`, `BBBBBB` and on through `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`, which is why a run has 32 players at most. A code that's no player's of the run joins nothing (the request fails as `tide_platform_room_join`'s does). A room nobody has open is refused by the first player's process as it routes (`'R'`), which the joiner's end takes for a failure only while that room never answered it: a match that says goodbye and closes in one frame refuses what was on its way, and the goodbye's reason has to stand. Whose match is open is as the frame left it, before anything sent in the frame arrives, so a match opened in the frame a hello was sent in takes it. An address reaches the first player whose match is open on its port; one nobody has open is dropped, and the session times out as on a network. A match whose host leaves ends for the others, with `ServerLeft`: host migration goes through the relay's rooms, and the host of a made-up one has no code or key to give its server for a handover (`tide_run_migrate` asks the real rooms, which have none).
  - **Who's in the match.** Where `Main` is the match's, `tide_run_begin` has the first player open it, as `--host` would, and the others join the first's room, before the first frame. Where it's local, nothing is automatic: the game's own code opens and joins, at the script's keys and clicks. `--host`, `--join` and `--connect` are refused with `--players` (exit code 2), as is more players than the game's `maxPlayers`, and `--players` in a run with a window, which `tide run` says before it builds.
  - **One script.** Every process reads the whole script, so a line that's wrong is named once, by the first, before it starts anyone. A line says whose it is after its frame (`120 player 2 key d down`, `player_of` in `platform/src/script.c`); input with none is the first player's, a capture with none is everyone's, and `quit` takes none. Each process keeps its own lines. A file a player writes has its number before the extension in a run of more than one (`tide_platform_players_file`: `fight-2.png`), for `--capture`, `--capture-audio` and the script's lines alike.
  - **Ending.** `--frames` and `quit` are every process's, so all end at the same frame, and the last frame has no exchange. The first then hangs up and waits for the others (`tide_platform_players_finish`, from `finish`), and the run's exit code is the first that isn't 0. A run of one frame heard from nobody yet, so the first lets the others connect before it hangs up, or they'd find no run. When a player's process ends before the run does (it stopped, or couldn't write a picture), the first finds its connection closed at that frame's end, says whose game ended and at which frame, hangs up, and ends with that process's exit code, or 1 for one the system stopped; each of the others finds nobody at its own frame's end and ends with 1, saying nothing. So no process is left running, and none waits on a time.
  - **Measured** (2026-10-08, Windows, debug): Tag, two players, 1,200 frames at 640 by 360, in 3.4 seconds, where one player's run took 1.6.
  - Tests: `tide_platform_players_net` and `tide_platform_players_net_ended` (`platform/tests/players_net.c`, no GPU, every desktop's tests) are three processes with no window that send text through the network and check, frame by frame, when it arrives, in what order and from whom, what a room nobody opened says, that an end let go of is nobody's, and that a process that ends at frame 3 ends the run there with its exit code. `tide_platform_players` (`players.mjs`, label `gpu`) runs `players.tide`, whose Main is the match's, with two and three players and one, and `players_menu.tide`, which starts in a menu, where the script has the players open and join by the rooms' codes, an address and one nobody is at: it reads every player's pictures, runs each again for the same bytes, and has a game stop and a picture fail. `tide_platform_players_refused` is what such a run refuses, with no GPU, and `tide_cli_hidden` that `tide run` passes `--players` on and refuses it with a window. CI leaves the `gpu` label out, so `tide_platform_players_net` is what runs the processes and the sockets on Linux and macOS.
  - A network worse than a frame each way: `--latency`, `--jitter` and `--loss` (A slow network to play on, 2026-10-27, above).
  - Not built: several players with windows (two at one desk, in real time); a player whose game ends on purpose while the run goes on (`player 2 quit`; `leave`, `drop` and `rejoin`, above, cover its match); host migration on the made-up network; and players who start apart where `Main` is the match's.
- That nothing an agent runs shows on the owner's screen doesn't rest on the agent remembering: `TIDE_HIDDEN` in the environment, set to anything but nothing or 0, says nobody watches a run (`tide_hidden`, `tide/page.h`), and then no Tide program shows a window or a dialog, or opens an audio device. Desktop only. Inside Unity, whose audio plays an embedded game's, the package's `TideAudio` clears each block it fills and opens no microphone under it (2026-10-22).
  - Where agents' shells get it, and the rules that stand beside it, are in `AGENTS.md`, Rendering and platform.
  - A window that isn't `hidden` has only its closing to end its run, which nobody could do, so the program is refused: it ends with exit code 2 before it opens anything, having said what to give it instead (decided over running hidden for a counted number of frames, which would end with 0 as if something had been checked, and as a run with no window that nothing ends is refused already). `tide_platform_open` refuses any such window (`platform/src/platform.c`), the standard host names its flags first (`tide_run_open` in `tide/host.h`), and `tide run` refuses before it builds (`compiler/cli/main.c`); `tide run --web` serves its page and opens no browser. A window that's `hidden`, as every test's is, and a run with no window go as they do without the variable, but for the window's sound (Nobody listens either, below). Under the refusal, `tide_window_open` shows no window whatever asked (`platform/desktop/glfw.c`).
  - A program that stops shows no dialog either. On Windows a debug build's `abort()`, which every stop of the engine's and of generated code ends in, shows one that waits for a click, a failed `assert` shows one in a program with no console, and the system shows its own for a crash or a library that's missing. With the variable they're off before `main` (`hidden_start` in `engine/src/page.c`, a constructor, there because every program of the engine's links that file: programs with no window stop too, and none has to remember it), and tide turns the system's off for what it starts (`sys_hidden`).
  - **Nobody listens either** (the owner, 2026-10-08, who could hear an agent's test runs and would rather not; asked the same day, that the variable means it, and the microphone with the output, are the owner's). With the variable, a window opens no output device and no microphone: `tide_platform_open` notes it (`unheard` in `platform/src/platform.c`) and leaves `tide_audio_out_open` uncalled, and `tide_platform_audio` opens no capture device for a frame that names the microphone, which is then silent. Nothing is said of it, and nothing else differs: the mixer is handed every frame and nothing renders it, as on a machine with no device (`notes/audio.md`). Until then every window opened the device, a `hidden` one too, so a test's hidden window that played was heard on the machine it ran on; only a run with no window opened none. What code relies on:
    - **The gate is the platform's, above the devices' own functions** (`platform/src/audio_out.h`, `audio_in.h`), which open what they're asked whatever the environment says. So `tide_platform_audio_device`, which calls them itself to run the system's device on purpose, goes as it did with the variable (an agent's run, CI's) and without, and needs nothing taken out of its environment: it plays a voice of no volume, and opens the microphone for a tenth of a second. Picked over a gate in the devices' functions with that one test run without the variable, and over skipping the device there, which would leave WASAPI against a real device to the owner's own runs.
    - Desktop only, as the variable is. A page's sound is its browser's: every browser a test starts is muted (`notes/testing.md`).
    - A program that plays and has to be heard in a test has no way to be, with the variable: none is, and one that checks what it plays reads it from a run with no window (`--capture-audio`).
  - `tide_platform_hidden` (`platform/tests/hidden.mjs`, with `hidden.c`, no GPU) checks what's refused, by a game and by a host of its own, that a stop ends at once, and that nothing changes without the variable; `tide_platform_hidden_runs` (label `gpu`) that a run with no window draws the same picture with it and without, that a window asked to show doesn't, and which audio devices a hidden window asks for (below). `tide_tests_hidden` and `tide_tests_watched` check a program with no window, and `tide_cli_hidden` checks `tide run`. Whoever runs the tests may have the variable, so a test that starts a program says which it's for: set, or taken out of the environment.
  - **`hidden.c`'s audio devices are its own**: it defines the devices' four functions (`tide_audio_out_open` and `_close`, `tide_audio_in_open` and `_close`), which open nothing and, in its `audio` mode, print what they're asked. The platform layer is a static library, so a program that has all four isn't linked with `platform/desktop/audio.c`; a fifth function there that the platform calls has to be given here too, or the link fails on the four, defined twice. So the test knows what a window asked for, by the devices' own word and with nothing timed, and neither of its runs opens the machine's device. `audio` hands a hidden window a voice and, for two frames, the microphone's name: with the variable nothing is asked for, and without it the output opens with the window and closes with it, and the microphone opens at the first frame that names it and closes at the first that doesn't. In both, the mixer has the voice (the test's thread renders a block of it, as no device's does). With the gate taken out, the first of the two fails (tried).
- The stats are a drawer (agreed 2026-10-05: always open, they covered two thirds of a game at the size the docs embed one, and a game can't ask where they are; what's picked is Provisional in `docs/spec.md`, Settings). `tide/stats.h` (`engine/src/stats.c`, in the engine beside the GUI and the profiler, so its tests need no window) is the drawer, and `tide/host.h` drives it:
  - Closed, it's one line, with what's worth a glance: the frame rate, and the ping when another machine runs the match (`tide_stats_values.remote`: this machine's own is the loopback's). Open, the rest is listed above that line, which stays where it is, so a click in the same place closes it again. The list is cut from its end where a screen is too low for it, and a system's name is cut where a line is too wide, never what the system took. It shows outside a match too, with the lines that aren't a match's.
  - **The list reads as two columns, and nothing in it shifts but the digits** (2026-10-07; the owner found it hard to read, "things shift a lot", and `cpu` unclear as a total; the shape is Claude's pick, Provisional in `docs/spec.md`, Settings). Each line is a label on the left and its value on the right (`tide_stats_line.label` and `value`, which `tests/test_stats.c` checks by; `entities 2472`, where it was `2472 entities`), labels dimmer than values, in four groups with a little space between (`gap`): a frame's times, the match's, the machine's, and the systems. `cpu` is the whole of a frame's work, and `render` is indented under it (`indent`) as the part of it that's rendering; `gpu` stands beside them, as the two overlap. What belongs together is one line (the owner, the same day): the frame's draw calls are in the GPU line's label, `gpu (22 draw calls)`, and a line of their own, `draw calls`, only where the backend has no GPU timer; the input delay is in the ping's, `ping (delay 3 ticks)`, and a line of its own only in a remote match, whose ping is in the drawer's one line; up and down are one `network` line, `Ã¢â€ â€˜2.0  Ã¢â€ â€œ0.5 KB/s`, whose arrows the drawer draws itself (`draw_arrow`, a shaft and a head of thick lines, as its chevron is: Tuffy has U+2191 and U+2193, and at 16 pixels they look like a 1); and the tick is gone (`tide_stats_values` has no `tick`), as it said nothing at a glance and changed every frame. A system keeps its row while it stays among the three slowest, and one that joins them takes the row of the one it replaces (`tide_stats_collect`), so the rows don't shuffle twice a second; the first three are slowest first. Open, the panel keeps the widest it has been (`tide_stats.held`), so its edge doesn't jump as a long system name comes and goes, and closing it, or `tide_stats_off`, lets that go; the screen still bounds it, and a label is cut where the values' column leaves it no room.
  - A click or a tap on it opens it and closes it (the left button or a finger that went down on it and is let go on it), and so does F8, beside the profiler's F7: the game reads the key too. `tide_run_drawer_input` asks right after `tide_platform_poll`, before anything reads the devices.
  - It starts closed, and `tide run --stats` starts it open (`tide_run_desc.stats_open`, which tide sets for `run` alone: `opts.stats_open` in `compiler/cli/main.c`), since whoever typed that wants to read it. A game's setting, `tide build --stats` and `STATS` are what players get: the one line.
  - Open or closed is the host's (`tide_run_drawer`), never a build's, so a hot reload keeps it: on desktop the host outlives the game's library, and on the web it goes with what a program leaves the next one (`saved.stats_open` in `platform/src/reload.c`).
  - **A press on it is the host's, as the pointer on a widget is the GUI's,** but by where the press began, not where the pointer is now: a button that goes down with the cursor on the drawer, and a finger that touches it, are the drawer's until they're let go, wherever they go meanwhile, and a press that began elsewhere stays the game's when it crosses the drawer (a brush that paints across the corner isn't cut). A mouse whose cursor is locked is on nothing. `tide_stats_hide` takes the drawer's presses out of a copy of the devices: the copy `tide_run_sample` samples the game's input from, after `tide_gui_hide`, and the copy the frame's GUI and views read (`tide_gui_begin` is given it while a press is on the drawer), so neither sees the press, from the frame it lands in. The drawer keeps a press once it's let go while the devices still say it was down (a button's `pressed`, `down` and `up` stay until a sample consumes them, and a finger's slot until then), or the next tick's sample would take it for a click; in a host that never consumes, it's the drawer's until the button goes down elsewhere. The GUI knows nothing of the drawer: a widget under it still shows the pointer on it, and games keep the corner free (the line's rect is about 84 by 40 logical pixels, and up to about 190 wide with a ping).
  - **Nothing is measured that isn't shown.** Closed, the frame rate is frames counted over made-up or real seconds the host has anyway, and the ping is the session's: no clock is read, ticks get no `tide_profile`, and the memory isn't asked for. Open, a frame's work is timed (`began`), the pool gets the profile (`tide_run_debug_views`), and the memory is read: the list's `cpu` line shows once a frame was timed. The numbers that are rates change once a second, and the first time after a quarter of one: the drawer shows from then (`tide_run_stats_known`), never a frame rate of 0.
  - Timings refresh every half second (2026-10-06, the owner asked for readable system times and GPU timing): the three slowest systems are ranked by their average measured run, with names and rows held between updates (above), rather than by the last tick. Their counters survive the F7 profiler clearing its own totals. The CPU total includes rendering; `render`, under `cpu`, shows preparing and submitting the draw list, and `gpu` shows GPU drawing with its render targets, without the drawer or display wait. Desktop uses core OpenGL elapsed queries after draw preparation; WebGPU sums render-pass timestamps when the optional timestamp-query feature is available. Both read results asynchronously and skip samples when their small queues are full; WebGL 2 and OpenGL ES 3 show no GPU line. Closing or reloading discards old samples. Timers run only while the drawer is open. CPU and GPU overlap, so their times aren't added. `platform/tests/gpu_timing.c` and `gpu_timing_web.mjs` cover delayed results, full queues and cleanup; `tide_platform_gpu_time` checks real devices, natively and on both web backends.
  - It's drawn into a list of the host's own (`tide_run_over`), over the frame (`tide_platform_draw_over`, as `tide_platform_draw_overlay` draws a host's text: neither counts in the frame's draw calls, nor moves its camera). So a run with no window has it in its pictures, and a script's click or `key f8` opens it as a player's would. Closed, it shows nothing this machine measured, so such a run still repeats byte for byte (a frame is a 60th of a second, so the line says 60); open, it shows this machine's clock, and pictures differ there.
  - Tests: `tests/test_stats.c` has the drawer with made-up numbers and devices, each frame in the host's order (closed and open, the key, a click, a tap, a tap shorter than a frame, a second finger, whose each press is, a game that samples nothing, small screens); `tide_platform_stats` (`platform/tests/stats.mjs` with `stats.tide`, label `gpu`) runs the standard host with no window and reads the drawer and what the game read off its pictures; `tide_platform_web_stats` (`platform/tests/web_stats.c`) does it in a page with the DOM's own events, and checks nothing is measured while it's closed, and that it's open as it was left when the setting goes and comes back.
  - Examples that ask for the stats keep their own things out of that corner by their own numbers: Duel (`examples/duel`, `StatsCorner` in `hud.tide`) has its Menu button in the top right corner on every layout, its touch ring above the line, and its row of keys as far left as the line needs.

- **A game's own network** (2026-10-11, `tide/network.h`, `notes/own-network.md`): the host asks the game for the network its C registered (`tide_host_game.network`) and serves `Session.Open` and `Session.Join` from it before the `--players` network and the rooms; a match on one has its address, failure, callbacks and host migration from `tide_network_match_*`, and `platform/src/reload.c` starts the game over at a reload while one is on. `Session.room` is up to 63 bytes there (`tide_local.tide_room` is 64).

## Language

- `Debug.Log(text)` and `Debug.Assert(condition[, text])` (`tide/debug.h`) are output for whoever runs the game, to stderr: they change nothing in the match, so they go anywhere, and codegen passes Assert the Tide source line and position for its message, after which a failed one ends the program. The stats (`tide run --stats`, or the `stats` setting) list the three systems that took longest in the last tick: while their drawer is open, the host gives the pool a `tide_profile` (`tide_platform_jobs_profile`, each frame, before or after the pool is made), which ticks fill with each system's name and time, since the host and a reloaded game's library share data, not statics.
- Debug views are the standard host's panels over a running game, for whoever makes it. The profiler is the first: where a frame's time goes, and a tick's.
  - They're on with the game's `debugViews` setting (`tide_game.debug_views`, which the host reads every frame, so a reload changes it), with `--debug-views` on the game's command line (on the web, `debug-views` in the page's address, which `tide.js` makes the argument), and with `tide run --debug-views` or `tide build --debug-views`, which set `tide_run_desc.debug_views`. F7 then shows the profiler, and hides it again (`tide_run_debug_views` in `tide/host.h`, from the key as the last poll left it); the game sees the key too.
  - Nothing is measured while nothing shows it: no clock is read. What's measured is said by the code that does the work, into a `tide_profile` (`tide/profile.h`) the host hands over as data, since the host and a reloaded game's library share data, not statics. A tick says its systems' times (`tide_run_systems`) and its own (generated `tide_world_tick_on`: its systems, then its changes, events and tasks) through `tide_jobs.profile`: the pool's (`tide_platform_jobs_profile`), or where there's no pool (one core, a page without threads), the host's own `tide_jobs` of one thread (`tide_run_alone`). A frame says its views' times (generated `tide_frame`) through `tide_gui.profile`. Each time is the last one and a total since the host cleared the profile, under the system's or view's name in Tide, copied. A system's is from its first task's start to its last task's end, so systems on several threads add up to more than the tick took.
  - The panel is `tide/profiler.h` (`engine/src/profiler.c`, in the engine beside the GUI, so its tests need no window). The host marks its frame's parts (input, the session's update, the views, drawing), and twice a second the profiler takes stock: averages a frame and a tick, the six slowest views and the ten slowest systems by name, as rows it adds to the frame's draw list after the GUI's, in the top left corner. What the session's update took beside its ticks is one row (Netcode): the session times none of it, and only counts the predicted ticks it ran again (`tide_client_status.again_ticks`). `tests/test_profile.c` checks it with made-up times, never measured ones, and `tide_platform_web_debug_views` (`platform/tests/web_debug_views.c`) runs the host's own loop in a page and presses F7: on by the command line, on by the setting, and nothing without either.
  - The stats are apart, and read the same profile: their line shows whenever they're asked for, and the profile is filled while their drawer is open or the profiler shows.

## Confirmed presentation interval ABI (2026-10-11)

Generated `tide_frame` and standard/custom-host callback signatures now take verified_previous and verified_alpha after verified. The standard host forwards the separate session interval and marks the frame presented, which stamps the next input's confirmed viewTime when maxRewind is enabled. Existing prediction correction snapshot bookkeeping remains independent. Manual single-world callers pass NULL/1 for the additional interval. Repository hosts/tests were migrated together; saved benchmark results were left unchanged. Full native/web coverage exercises the ABI, hidden-run safeguards and existing capture/browser hosts.
