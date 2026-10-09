# Dead Signal private cooperative shooter

Built at the owner's 2026-10-09 request: design and start building a private,
fast Doom-like cooperative shooter with PSX visuals, at least two players and
30–60 minutes of intended gameplay. The working title, fiction and tuning are
prototype choices. The complete human session duration remains unmeasured.
`prototypes/dead-signal/DESIGN.md` explains the experience and playtest questions;
its `README.md` gives build/play instructions and implementation boundaries.

## Build and sharing

The repository was verified private. `TIDE_PRIVATE_PROTOTYPES`, OFF by default,
opts into the game and its test hosts via `prototypes/CMakeLists.txt`. The game
needs the platform layer; simulation tests also work without it. Native and web
use the same six game sources and the existing Rooms package. At the owner's
follow-up request, the admin prototype grid links Dead Signal with a combat
screenshot and the existing availability check. The owner's later public-sharing
request makes the game and thumbnail public without admin access.
`dead_signal_page` stages its self-contained browser page under
`docs/static/prototypes/dead-signal/index.html`, served as
`https://tide-engine.dev/prototypes/dead-signal/`; `private_games` depends
on that target (or the native game). The build remains opt-in and is not
installed in the Tide package. No CI, hosting or release was dispatched.
Any external game assets belong beside the page in its public directory.
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

The graphics pass replaces cube figures and weapons with original faceted
meshes. `models.tide` builds the operator, clawed ribbed hunter, respirator
trooper, asymmetric shield bulwark and crowned Warden (572–836 triangles each).
Split face normals, tapered limbs and sloped armor preserve their silhouettes.
Per-vertex tags drive local GPU stride/attack animation; only the operator's
accent panels inherit its seat color. Contact shadows ground the figures.

`weapons.tide` builds five distinct first-person guns, hollow polygonal barrels,
material regions, shaped gloves and forearms. A separate transparent 270-line
weapon target supplies fresh depth before alpha compositing onto the scene,
so hands/receivers self-occlude without clipping against nearby world walls.
`environment.tide` bakes continuous industrial panels, bevelled ribs, sloped
crowns, pipes, louvers, terminals and caches. Sector materials use restrained
wear and grain; floors no longer have the checkerboard. Decorative geometry
does not change the shared map or collision. Sparks/markers use small faceted
glints. `render.tide` coordinates those meshes, point-filtered targets, snapped
vertices, fog and quantized color. None of these changes writes match state.
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

The graphics host renders thirteen real game states twice, including every
weapon and a controlled gallery of the complete cast. It checks varied pixel
readback and requires the world, actor fragment/vertex, shadow, weapon, overlay
and final presentation shader paths on native OpenGL, WebGL and WebGPU.
Its optional native `--capture-directory <existing-folder>` saves each fixture
for visual review, using a counted headless window; ordinary tests save nothing.
The standard host's hidden two-player script captures lobby, intake, combat and
map screens; those captures were visually inspected. Before integration the
existing full suites passed 2,009 native and 1,981 web CTests. Post-merge scoped
private builds and checks passed 5/5 native and 9/9 web CTests across Dead Signal,
Cabin Nine and The Last Light.

The graphics pass's own native and web simulation/render tests pass, retaining
the same live-combat world hash. Captures of the two-player opening and the
full arsenal/cast were reviewed. A scratch audit of the built model buffers
also found valid indices, nondegenerate triangles, unit normals matching their
winding, consistent per-triangle animation tags and finite bounded vertices.

The original catalog integration was checked by building `private_games` on the web and
the native game, checking the site types/build, and running the admin unit and
production HTTP regressions. They cover the catalog link, protected screenshot
and compiled HTML, raw/nested assets, encoded paths, expired sessions and
retired public aliases. Those authentication rules are superseded by the owner's
public-sharing request; current serving validation is in `notes/docs-site.md`.
Dead Signal remains excluded from public Samples, search and sitemap;
the site build checks its assets stay under the public prototype root.
The existing manual docs
workflow already enables the opt-in build and stages `private_games`.
The original muted headless Chrome check covered the fourth card at 1280×900 and 390×844, loaded
its screenshot without horizontal overflow and opened the actual staged game
in a separate authenticated tab. The game rendered the minimum-two-player
lobby without runtime/console errors. No deployment was made.

Human pacing, enjoyment, later-sector balance and two-machine end-to-end room
play still need a real pair's playtest. There is no procedural map generation,
permanent progression, save system, built-in voice, stairs or jumping.
