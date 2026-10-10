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
from a page whose clock fell behind. Existing deadlines and pass conditions
are unchanged; the failure's cause remains unconfirmed.

Diagnostic validation (2026-10-10): the exact CI Chrome Headless Shell
155.0.8059.39 and Node 24.19 passed the WebGL playtest on web-debug with
unrestricted affinity, two processors and one processor, and on web-release
with one processor. A pending local SDP operation injected into a temporary
runner reproduced peers staying
`new`; its account identified the uncompleted operation, and the game's
existing connection timeout failed the run and closed its browser normally.

Fix validation (2026-10-10): native simulation passed 1/1 (fourteen inner
cases), scoped web-debug passed 3/3, and web-release passed 6/6 with the test
process restricted to one CPU and CTest parallelism four (both play backends,
simulation, demo on both backends and Sumo). Fault-injected browser signal
termination and a hung browser both failed promptly, removed their profiles
and left no browser process. macOS confirmation requires a requested CI run;
no workflow was dispatched for this fix.

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
