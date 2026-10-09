# Private prototypes

The owner requested a private PSX horror game with board-game mechanics on
2026-10-09, and an admin section that also keeps Two Forks private. The Last
Light (`prototypes/the-last-light/`) is the original two-player game built for
that request. Its title, fiction, roles, mechanics and tuning are agent design
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
and stages browser pages only at `docs/static/admin/prototypes/<id>/index.html`.
`private_games` builds/stages The Last Light and Two Forks; the public sample
build also stages Two Forks privately. Cabin Nine arrived through `origin/dev`
during integration and joins the same catalog. When its existing opt-in
`TIDE_PRIVATE_PROTOTYPES` option is enabled, `private_games` stages it under
the protected root too; the manual docs workflow explicitly enables this
option. Its independent game contract is in `notes/cabin-nine.md`.
Pages are self-contained; any future
external game assets must stay under that protected root. The site's auth,
retired public URLs, static build gate and catalog are in `notes/docs-site.md`.

Dead Signal (`notes/dead-signal.md`) also uses `TIDE_PRIVATE_PROTOTYPES`. At the
owner's follow-up request it joins the admin grid, with a combat screenshot,
2–4-player label and the same availability check as the other games.
`dead_signal_page` and `private_games` stage its self-contained browser page
only beneath the protected `admin/prototypes/dead-signal/` root. Its game and
thumbnail receive the same authentication, no-store/noindex and public-alias
protections; admin unit/production HTTP tests include the new ID. The site build
also rejects unprotected dead-signal/dead_signal assets. Existing manual docs
workflow staging covers it with no workflow change or deployment.

## Verification and limits

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
Docs check/build and private asset HTTP tests cover authenticated raw HTML,
wasm, pack, audio and images, former public aliases, encoded paths, ranges,
conditional requests and no-store/noindex headers. No CI or deployment was
requested or dispatched. Physical phone hardware, a human pair's fear/voice
communication and complete two-machine playthroughs remain untested.
