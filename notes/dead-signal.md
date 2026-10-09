# Dead Signal private cooperative shooter

Built at the owner's 2026-10-09 request: design and start building a private,
fast Doom-like cooperative shooter with PSX visuals, at least two players and
30–60 minutes of intended gameplay. The working title, fiction and tuning are
prototype choices. The complete human session duration remains unmeasured.
`prototypes/dead-signal/DESIGN.md` explains the experience and playtest questions;
its `README.md` gives build/play instructions and implementation boundaries.

## Build and privacy

The repository was verified private. `TIDE_PRIVATE_PROTOTYPES`, OFF by default,
opts into the game and its test hosts via `prototypes/CMakeLists.txt`. The game
needs the platform layer; simulation tests also work without it. Native and web
use the same three game sources and the existing Rooms package. There is no
catalog page target or dependency from `private_games`, no documentation
staging and no package installation. No CI, hosting or release was dispatched.
The test folder's nested manifest excludes its C hosts and GeometryAudit Tide
fixture from a standalone `tide run` of the game.

## Match contract

Two to four real players occupy owner-bound Marine entities. Readiness checks
every current seat; fewer than two participants freeze combat and expedition
time. A replacement joins at the current sector start. Commands validate sender,
phase, choice and sector; relic choice applies only once per player per draft.

`world.tide` supplies six connected, fixed tile maps, circle/tile sliding
collision and exact wall DDA rays. `game.tide` owns the deterministic match,
weapons, enemy BFS navigation, relays, checkpoint/relic flow and finale. Rendering
and local audio live in `render.tide` and never write match state. No runtime
API or language feature was added. Simulation is planar, with vertical aiming.

The six sectors contain 33 encounter sites, each with three changing attack
beats, three paired relays and two optional shared caches. Intake's first fight
ramps from eight to ten to twelve foes for two players. Four weapons unlock over
the first three sectors; eight relics have stackable, bounded effects. Crossfire
boosts damage and bypasses frontal armor. Hunters telegraph melee for 0.25 seconds
and ranged attacks use visible moving bolts. A downed player crawls/fires a weak
sidearm until a living teammate channels a revive; a wipe restarts the sector
with the crew's relics intact. At least two living players must continuously hold Use
for relays/extraction. The final Warden changes at 70% and 35% health; its death
and supporting enemies' deaths precede joint extraction and victory.

## Presentation and verification

The renderer uses authored industrial geometry, instanced articulated figures,
point-filtered 270-line targets, snapped vertices, fog and quantized color.
Full-resolution text provides vitals, ally health, objective bearing, relics,
retry/victory panels and the held-M map. Tab retains GUI navigation. Mouse and
gamepad camera pitch share the shot direction and eye height. Procedural WAVs
are original and regenerate from `sfx/make_sounds.py`.

Sixteen simulation checks pass on Windows clang and WASI. They cover real-owner
readiness/suspension, command validation, revives/checkpoint retries, continuous
paired objectives, friendly fire, shot occlusion/vertical aim/projectile flight,
independent map/ray/collision checks, snapshot replay and two lossy clients
confirming the authority with zero resyncs. Both pin scripted world hash
`1fe52c6bf4f307fe` from a live encounter with actual AI, shots and kills. A normal
health opening controller clears all 30 foes with zero downs, no relics or
retries; it uses actual input, ammunition and pickups. The
invulnerable full-route controller verifies all six sectors and the ending
(1,647 kills); it establishes progression, not normal difficulty or human timing.

The graphics host renders nine real game states twice, checks varied pixel
readback and requires all four shader paths on native OpenGL, WebGL and WebGPU.
The standard host's hidden two-player script captures lobby, intake, combat and
map screens; those captures were visually inspected. Before integration the
existing full suites passed 2,009 native and 1,981 web CTests. Post-merge scoped
private builds and checks passed 5/5 native and 9/9 web CTests across Dead Signal,
Cabin Nine and The Last Light.

Human pacing, enjoyment, later-sector balance and two-machine end-to-end room
play still need a real pair's playtest. There is no procedural map generation,
permanent progression, save system, built-in voice, stairs or jumping.
