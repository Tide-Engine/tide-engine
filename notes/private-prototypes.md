# Private prototypes

The owner requested a private PSX horror game with board-game mechanics on
2026-10-09, and an admin section that also kept Two Forks private. Later that
day, the owner requested public game URLs so invited players need no admin
access. The Last Light (`prototypes/the-last-light/`) is the original two-player
game built for that request. Its title, fiction, roles, mechanics and tuning are agent design
choices under the owner's free-reign instruction. Its researched design and
primary citations are in the game's `DESIGN.md`; play instructions are in its
`README.md`.

## Match contract

Exactly two players are required. Player IDs map to two persistent seats;
owned Participant entities bind their held input. Rite contains the entire
deterministic state. Serial-numbered Plan/Return/Linger/Restart commands check
the real sender's seat, phase, presence and serial. Input carries the same
serial so a held key cannot cross a turn or interruption. No bots substitute
for a missing partner.

The active Pilgrim carries the personal risk; an unprotected exposed strike
reduces only their nerve, costs six candlelight and loses carried fragments.
The Keeper chooses the communicated sigil and spends three wax for the first
mandatory seal or an exposure shield. Later acknowledgements of safe dodges
cost nothing. A fragment needs 720 channel charge, at least one seal and a
minimum 990 ticks of vigilance. Vigils cap at 40/42/44 seconds at 30 Hz. Well
requires holding through the threat rather than releasing. Final-act Belfry
has paired tells and compensating doubled quiet charge.

Eighteen banked names complete three acts of six. Returning rotates roles and
restores four wax, including with empty hands; up to three offerings per turn
can be risked with Linger. Candle costs three per expedition and two per
linger. Dread shortens quiet approaches but preserves the two-second response
tell. Act changes refill twenty wax and recover one nerve, capped at three.
Both players must channel the final eight-second escape. Win/loss support
restart. A disconnect freezes all progress; resuming a Vigil starts a new
quiet approach, preserving charge and resources but avoiding an immediate
strike from an almost-expired tell.

RitualCue match events drive local one-shot sound exactly once across rollback.
Presentation state and sounds never enter the match hash. Role-specific clues
are concealed by the UI, not against inspecting a client: Tide sends the full
world. Existing rooms/host/network APIs are used without engine changes.

## Presentation and files

`paint.tide` owns procedural cube/rounded/robe meshes, point-sampled physical
board printing, local candle lighting, snapped vertices, ordered dithering
and the 270-line target. Targets resize in place and are guarded before the
first connected frame. `room.tide` builds the table, corridor, wards, paper,
beds and visitor; the escape opens a lit doorway and fades in morning light.
`ui.tide` owns legible keyboard/pointer/touch actions, room roster and story
fragments. Rules are disabled during Vigil/Finale so reading them cannot
remove a required held input mid-threat. Menus are ordinary rooms menus and
are not a shared pause. `heard.tide` plays original WAVs regenerated from
`sfx/make_sounds.py` with standard Python libraries.

`test/tide.packages` is a folder boundary: the CLI must not compile the C
test host as playable game code. CMake compiles the simulation test separately
and stages browser pages at `docs/static/prototypes/<id>/index.html`, served
publicly as `/prototypes/<id>/` without an admin session. HTML, thumbnails and
any future external game assets belong under that game's public directory.
The admin catalog still lists the games; they remain outside the public
Samples gallery, search and sitemap.

`private_games` keeps its name and builds/stages The Last Light and Two Forks;
the public sample build also stages Two Forks at its prototype address.
Cabin Nine arrived through `origin/dev` during integration and joins the same
catalog. When its existing opt-in
`TIDE_PRIVATE_PROTOTYPES` option is enabled, `private_games` stages it under
the public prototype root too; the manual docs workflow explicitly enables this
option. Its independent game contract is in `notes/cabin-nine.md`.
Deadbell uses the same opt-in option and joins this grid through
`deadbell_page` and `private_games`, with its actual combat thumbnail and
self-contained page under `/prototypes/deadbell/`. Its independent
co-op roguelike contract and validation are in `notes/deadbell.md`.

Dead Signal (`notes/dead-signal.md`) also uses `TIDE_PRIVATE_PROTOTYPES`. At the
owner's follow-up request it joins the admin grid, with a combat screenshot,
2–4-player label and the same availability check as the other games.
`dead_signal_page` and `private_games` stage its self-contained browser page
under `docs/static/prototypes/dead-signal/`, served as
`/prototypes/dead-signal/`. All five games and their thumbnails are public;
the catalog itself remains behind admin sign-in. Existing manual docs workflow
staging covers them with no workflow change or deployment.
Pages are self-contained. The site's admin auth, former game addresses,
static build gate and catalog are in `notes/docs-site.md`.

Afterburn (`prototypes/rocket-league/`) adds a Rocket League inspired car-soccer
duel at the owner's request on 2026-10-09. It uses the same opt-in option and
`private_games` integration: `rocket_league_page` stages the self-contained
page at `/prototypes/rocket-league/`, with its gameplay thumbnail beside it.
Its catalog card supports one player against a bot or two human players.
Its game contract and validation belong to `notes/rocket-league.md`.

Aerial (`prototypes/aerial/`) is a second Rocket League prototype, asked for by
the owner on 2026-10-10 as a better one: the sport's own numbers on a
rigid-body car, for one to six players with bots in every other seat. It uses
the same opt-in option and `private_games`: `aerial_page` stages its page at
`/prototypes/aerial/`, beside a native capture as its thumbnail. Afterburn
stays as it is. Its contract and tests are in `notes/aerial.md`.

## Verification and limits

The 2026-10-09 Build run (37994581083) exposed browser-playtest contention:
Windows timed out in DevTools during startup/capture, and macOS lost the host
after a long gap in match traffic. Both backends passed on the same Windows
machine in isolation; limiting the test process to one CPU while running the
demo and Sumo checks reproduced both startup timeouts. The two Last Light
playtests now use CTest `RUN_SERIAL`, since each already renders two software
players. They keep their 180-second test and 20-second command deadlines.

`test/web-play.mjs` observes completed draws through the existing `Tide.netcode`
callback instead of polling `Runtime.evaluate` behind cold shader compilation.
Reports are limited to ten per second so diagnostics do not delay input and
capture commands on small runners. Navigation waits for the exact new URL;
keyboard/resize/capture steps wait for completed frames. Connected play waits
for verified ticks and fresh packets from both players, replacing wall-clock
sleeps. Runtime/shader errors, host loss and stale traffic still fail the test.
Failures record recent commands, frame/packet timing and browser logs; recovery
captures and browser shutdown have short bounds and force-stop only the test's
own browser tree when necessary.

Build run 38003829989 still failed Windows WebGL while WebGPU passed:
both peers remained `new`, neither reported a packet, and completed-frame
reports fell behind the runner by 19–25 seconds through simultaneous gaps
of about six seconds. It persisted after the earlier contention fix.
The runner now records ICE, gathering and signaling states, the start/end
of local/remote SDP operations, and wall-clock timestamps. Delivery age and
the page's monotonic/wall-clock drift distinguish buffered DevTools reports
from a page whose clock fell behind. That run alone did not confirm the cause;
the diagnostics kept existing deadlines and pass conditions.

Run 38049029745 located the Windows WebGL join failure before signaling:
after the keeper's navigation, neither page completed a frame for about
sixteen seconds. The first peer and local SDP operation appeared only after
the keeper had spent its session's fifteen-second connection deadline.
Delivery ages around a millisecond and clock drift below a millisecond ruled
out stale DevTools reports in that run. The playtest now renders the keeper's
local title first, then uses the actual Rooms keyboard controls to enter and
join the host's code on that initialized page. Cold rendering therefore
finishes before a live join starts, without changing session/test deadlines
or depending on shader caches surviving another navigation. Existing
desktop/phone captures, packet/tick assertions and controls remain covered.

The same run's macOS WebGL gameplay passed, but shutdown failed while waiting
for the child process's `close` event. That event also waits for stdio EOF:
a helper inheriting stderr can keep it pending after the browser process has
exited. `test/browser-process.mjs` now confirms process exit before releasing
the runner's stderr pipe. Graceful and forced shutdown waits stay bounded;
a browser process that remains alive still fails and retains its profile.
The regression reproduces exit with stderr held by an owned helper, and also
checks graceful and unresponsive shutdown. These CPU-only checks run through
CTest on native and web builds and pass on Windows and Linux. The macOS log
did not record process exit separately from stdio closure; run 38051684843
subsequently passed both macOS suites.

Validation of these fixes (2026-10-10): scoped native debug passed 2/2,
web-debug and web-release each passed 4/4, including both real two-player
backends. Cleanup's three regressions passed on Windows Node 24.19 and Linux
Node 24.21. With CI's Chrome Headless Shell 155.0.8059.39, a temporary runner
that blocked the keeper's first draw for seventeen seconds made the previous
URL join fail before exchanging any packets; the same fault passed with the
initialized-page GUI join. No assertions or deadlines were relaxed, and no
workflow was dispatched.

Run 38051684843 passed JetBrains, Linux and both macOS suites. Windows native
and MinGW each failed only the new stderr-inheritance regression, before it
called the cleanup helper: libuv kills a non-detached child when its Windows
parent exits, so `unref()` alone did not establish the fixture's premise.
The fixture now detaches the owned helper, waits for its actual stderr write
and verifies that its PID remains alive after the parent exits. It still
kills that exact helper in cleanup. All three cleanup cases pass on Windows
and Linux with CI's Node 24.21; the first case passed thirty repetitions on
each. A negative control without detachment fails the new alive assertion.
The combined readiness/parent-exit wait has a five-second deadline so a
stuck parent still reaches cleanup. A temporary stuck-parent control fails
within six seconds on both platforms and confirms its helper PID is gone.
The browser cleanup implementation did not change.

That run's Windows WebGL failure was again before the keeper joined, this
time with DevTools reports buffered by 24–33 seconds and less than a
millisecond of clock drift. Input commands were already slow on the host
alone. The test now paces each page's animation requests: after a completed
callback it leaves at least 1/30 second idle before another frame. Both
visible players still draw, and the game's simulation catches up to real
time. This changes only the injected test script; dimensions, screenshots,
GL checks, controls, packet/tick assertions and deadlines stay the same.
Cancellation and callback timestamps retain their browser behavior.

Local comparisons use CI's Node 24.21 and Chrome Headless Shell 155.0.8059.39
with a Windows job CPU-rate limit covering Node and the whole browser tree.
At a budget equivalent to about half a logical processor, the unpaced
optimized WebGL playtest passed in 160.8 seconds using 68.9 CPU-seconds;
the paced test passed in 119.6 seconds using 53.2 CPU-seconds. This establishes
lower resource use and more deadline margin locally. At a tighter budget
equivalent to 0.36 processors, the unpaced test failed before exchanging host
packets (129.4 seconds); the paced test passed all gameplay checks in 164.0
seconds. The specific hosted DevTools stall has not been reproduced.

Scoped validation of the fixture and pacing fixes (2026-10-10): native debug
passed 2/2; web-debug and web-release each passed 4/4, including real WebGL
and WebGPU multiplayer. CPU-only scheduling probes covered cancellation,
duplicate callbacks, native timestamps/context, recursive slow frames and
callback errors. No workflow was dispatched or rerun.

Hosted run `38057969812` at `6a8c9e24` passed Windows native (1,993/1,993)
and Windows web (1,985/1,985). The paced Last Light WebGL and WebGPU
playtests passed in 37.51 and 59.74 seconds, and the cleanup fixture passed
on both. Linux and macOS suites passed too. Unrelated Windows tool-feed and
JetBrains dependency-resolution failures blocked release; at the owner's
request only the failed jobs and their dependent gates were retried.
MinGW passed 1,993/1,993 on that retry, including the cleanup fixture.
The complete run then passed and published `v0.3.0-nightly.117`.

Windows WebGPU input timeouts returned in runs `38079507404` and
`38080715656`, before any keeper peer/SDP operation. Both pages' DevTools
frame reports arrived 19–30 seconds late while page clock drift stayed below
a millisecond. The existing frame callback counted CPU submission, not GPU
completion: its 33 ms idle interval did not bound pending software GPU work.
`test/frame-pacing.mjs`, serialized into the playtest page by `web-play.mjs`,
now waits for every queue used by the previous game frame through
`onSubmittedWorkDone` before scheduling another. WebGL keeps its original
idle interval. Both visible players still render; dimensions, screenshots,
GL checks, keyboard/touch steps, packet/tick assertions and all deadlines
are unchanged. This affects only the injected test script, not the runtime.
Frame diagnostics now include GPU submissions, completions, pending/peak
frames, longest completion and oldest pending age. Fence rejection is an
explicit console error, which already fails the playtest, and releases its
wait so it cannot hide behind a frame deadline. Cancellation remains effective
while a fence is pending.

A local comparison used the same freshly packaged release page and a Windows
job CPU-rate cap for Node and the complete Chrome Headless Shell 155.0.8059.39
tree, with full processor affinity retained. At a budget equivalent to 0.48
logical processors, the previous pacing passed in 82.31 seconds with 34.89
CPU-seconds and a peak of three pending GPU frames; completion pacing passed
in 75.08 seconds with 32.98 CPU-seconds and a peak of one. GPU completion took
up to 2.45 seconds in both. This confirms bounded GPU work and improved local
margin, but did not reproduce the hosted DevTools backlog and does not
establish that GPU queueing caused those CI timeouts.

Six CPU-only `last_light_frame_pacing` regressions exercise the actual serialized
helper: native callback context/timestamp and WebGL spacing, free timers while
GPU work is pending, cancellation before native delivery and during GPU waits,
rejected completion and frames using several queues. All six pass. The real
release two-player WebGL and WebGPU playtests pass with the helper.

Hosted run `38083005042` still timed out while entering the keeper's room code.
At the last delivered samples, both pages had completed every submitted GPU
frame, with pending counts of zero, peaks of one and longest completions of
0.82/1.31 seconds. Those DevTools reports arrived about 20 seconds late, so
they cannot identify the current renderer state at failure. The completion
gate did not resolve the timeout. The synchronized delays on both page
connections implicate a shared path, but do not locate it within Chrome,
Windows or the transport.

The playtest now uses Chrome's DevTools pipe, as the existing served web and
rooms runners do, instead of separate Node WebSocket clients to the browser's
HTTP debugging server. `test/browser-devtools.mjs` decodes NUL-terminated UTF-8
records, assigns globally unique command IDs, and routes each player's events
through its flattened target session. A closed pipe rejects all commands; a
detached target rejects only that session. Browser close is a browser-level
command, and both pipes are released after bounded process shutdown. Separate
player windows, renderer flags, frame reporting, pacing, all gameplay checks,
screenshots and deadlines remain unchanged. This is fixture code only.

Controlled release-page probes used Chrome Headless Shell 155.0.8059.39,
Node 24.21 and the same 0.48-processor job budget. Scratch-only HTTP frame/input
timestamps and browser heartbeats compared paths independently. The WebSocket
probe passed in 74.08 seconds using 35.89 CPU-seconds, and the pipe probe passed
in 70.51 seconds using 32.75 CPU-seconds; maximum DevTools frame delivery age
was 602/39 ms. Neither reproduced the hosted backlog, so these local results
do not establish its cause or prove the hosted fix. The unmodified WebSocket
playtest also passed separately with Node 24.21 and 24.19 in 75.82/78.93 seconds;
there is no supported reason to pin Node. The sideband and heartbeat probes
are not part of the checked-in runner.

Eight CPU-only `last_light_browser_devtools` cases cover split UTF-8 and
coalesced records, independent target events and reordered replies, protocol
errors, closed pipes, late replies after timeout, isolated target detachment,
malformed records/browser exit/write failure, and disposal with commands pending.
All eight pass on Node 24.21. The real checked-in runner also passes the complete
two-player WebGL and WebGPU checks with the release page and that same browser
and Node version.

Hosted run `38085091225` at `ea509d62` passed all 2,037 Windows web tests,
including the real WebGPU playtest in 53.62 seconds. The Windows installer
check also passed in 13.99 seconds. All three package jobs, Linux native/web/sanitizers
and Android, macOS native/web, MinGW, docs and JetBrains passed too. Windows
native alone failed one of 2,048 tests: `last_light_browser_process`, in its
unresponsive-close forced-shutdown case. That helper and test are byte-unchanged
since `6a8c9e24`; they passed prior CI and the same run's Windows web check
in 4.46 seconds. No nightly was published. The pipe playtest therefore passed
on the previously failing hosted platform, but this single result does not
establish the cause of the earlier buffered reports and input timeouts.

The cleanup failure exposed a missing Windows fallback (2026-10-10):
`stopBrowser` launched `taskkill /T /F` without waiting for that command,
then returned false if the browser had not exited within two seconds.
A local probe replacing only that launch with an owned idle Node process
reproduced the false return with both processes still alive. This establishes
the cleanup defect; the hosted log did not time taskkill and cannot establish
that it stalled there.

Cleanup now gives the close request and browser exit one shared grace window,
waits a bounded window for taskkill, stops a stalled taskkill before directly
terminating the owned browser, and confirms both exits. A tree killer that
cannot be stopped is diagnosed, unreferenced and reported as failed cleanup;
the caller retains the profile. Browser stderr is released only after browser
exit. The two-second per-wait bounds and ten/30-second regression/CTest guards
are unchanged. Normal Windows cleanup still requests the whole browser tree;
the direct fallback confirms the owned parent and tree-killer exits, without
claiming that every independently surviving descendant was observed.

Five Windows regressions use real owned processes and intercept only the
taskkill launch: a stalled command, failure to start, browser exit while the
command is stalled, refused direct browser termination, and refused taskkill
termination. The original three portable cases remain. The previous helper
fails the stalled-command and lingering-command negative controls; the fixed
helper passes all eight. Windows Node 24.19 passed all 22 CPU-only cleanup,
pacing and DevTools cases and ten consecutive cleanup-suite runs. Scoped
native debug passed 4/4 and web-debug passed 6/6, including the actual two-player
WebGL and WebGPU checks with Chrome Headless Shell 155.0.8059.39. No CI run or
retry was dispatched.

Diagnostic validation (2026-10-10): the exact CI Chrome Headless Shell
155.0.8059.39 and Node 24.19 passed the WebGL playtest on web-debug with
unrestricted parent affinity and parent affinity limited to two processors
and one processor, and on web-release with parent affinity limited to one.
These affinity runs did not constrain SwiftShader's total CPU use: its Marl
workers explicitly request the system's processors. Later resource comparisons
use a Windows job CPU-rate limit for the complete process tree, keeping the
full processor affinity mask so those workers can start.
A pending local SDP operation injected into a temporary
runner reproduced peers staying
`new`; its account identified the uncompleted operation, and the game's
existing connection timeout failed the run and closed its browser normally.

Fix validation (2026-10-10): native simulation passed 1/1 (fourteen inner
cases), scoped web-debug passed 3/3, and web-release passed 6/6 with the test
parent process restricted to one CPU and CTest parallelism four (both play
backends, simulation, demo on both backends and Sumo). Fault-injected browser signal
termination and a hung browser both failed promptly, removed their profiles
and left no browser process. These parent-affinity runs did not constrain
SwiftShader's workers. Both macOS suites later passed in run 38051684843;
no workflow was dispatched by the agent for this fix.

Public-sharing validation (2026-10-09): the native/web builds and full suites
passed 2,016/2,016 and 1,993/1,993. After integrating the five-game catalog,
scoped prototype checks passed 8/8 native and 13/13 web. All five staged public
pages match their compiled HTML, with none staged under the old admin root.
Site check/build and 24 analytics/admin/production HTTP/SEO/access regressions
pass, including the real game bytes without sign-in or admin configuration.
The current serving contract and remaining manual deployment step are in
`notes/docs-site.md`.

The native suite passed 2,010/2,010 tests and the web suite 1,982/1,982 before
the additional game browser checks were registered. The final scoped web run
passed the simulation, two-player WebGL and two-player WebGPU checks (3/3).
After merging Cabin Nine and adding its protected staging, scoped native
checks passed 3/3 and scoped web checks passed 6/6 for both games.
Fourteen game tests
include authorization and stale-input rejection, exactly two players,
disconnect/reconnect safety, Well and Belfry rules, resource bounds, cargo
loss, both endings, snapshot/pack replay and complete bank and linger runs.
Pinned full-world hashes agree on native and WASI. A two-client ritual with
60 ms latency, 40 ms jitter, loss and reordering stays identical to the server,
without resyncs. Automated successful runs supply about 11–12 minutes of
active ritual; discussion and decisions are not timed by these tests.

Native two-process captures and muted browser WebGL/WebGPU play checks cover
desktop and phone layouts, title/waiting, both roles and keyboard/touch input.
The original docs check/build and private asset HTTP tests covered authenticated
raw HTML, wasm, pack, audio and images, former public aliases, encoded paths,
ranges, conditional requests and no-store/noindex headers. Those access rules
were superseded by the owner's public-sharing request; the current serving
contract and its validation are recorded in `notes/docs-site.md`.
No CI or deployment was requested or dispatched. Physical phone hardware, a human pair's fear/voice
communication and complete two-machine playthroughs remain untested.
