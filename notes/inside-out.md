# Inside Out

The owner requested a public online co-op puzzle prototype for two friends
in separate browsers on 2026-10-11. The initial workshop, fixed player roles,
isometric experiment, switch locks and fixed recursive entrances are
superseded. The current direction is actual 3D with a fixed 2.5D view, harder planning and
interdependent moves, physically nested starting rooms, varied sizes and
offset doors, and movable boxes including self-nesting. The owner chose
spatial constraints instead of locks, and on 2026-10-12 a factory where the
players are test dummies, in a PSX style, replacing the earlier institutional
atmosphere. There are still no threats or jumpscares. These are game design
choices within the prototype request, not engine or language requirements.

## Shared world and recursive boxes

`game.tide` contains one deterministic two-seat match. Both makers start
inside the same small box, itself inside an ordinary containing room box
that can be left. The root is called Outer room. Makers retain amber/teal
identities. Their coordinates use one canonical interior: they block one
another even when they arrived through different copies.

Root and room dimensions are authored independently, bounded to 1..7 on
each axis, with 49-cell storage and actual-width row stride. Each room has
a four-side door mask and per-side offsets; -1 selects the midpoint.
Invalid offsets and destinations block atomically. RoomLayout array elements
require explicit literals to obtain defaults: a zeroed LevelLayout does
not apply field defaults inside its room array.

Limits: four canonical interiors / primary boxes, four movable reference
boxes, eight parcels and eight explicit entrance layers. IDs 0..3 are primary;
IDs 4..7 are references. `LevelLayout.references` stores destinations;
`Puzzle.references` stores live parents and positions. References use their
canonical room's doors and offsets. Small stencils distinguish primary 1
from copy 1′ (and 2..4), with identical materials and opening geometry.

Every instance tries to move first. Walls, boxes, parcels and the partner's
body can be backstops. A failed push tries the facing doorway using unchanged
state. There are no switch locks, modifiers, pulling or push chains. A moving
box carries its interior and occupants without changing interior coordinates.
Objects pushed into another doorway enter its canonical interior. Objects
pushed out emerge beside that room's primary box; makers return through the
instance they entered. This distinction is stated in the guide and README.
Resolution crosses at most one boundary and one gateway per action;
occupied door cells block unbounded gateway chains.

Facing doorways (owner, 2026-10-12: adjacent boxes "have to match position,
not just have a magical bridge", and the rules must be consistent, after a
parcel crossed a bridge that a player leaving the same doorway did not: the
player pushed the neighbor instead). `DoorsMeet` is true when the leaving
room's doorway on that side and the neighbor's opposite doorway are both
open and at the same place along the shared edge, compared exactly in the
containing cell (`DoorReach` scaled by the other room's longest edge). It
applies only to leaving one box straight into a box in the cell outside:
- Doorways meet: a dummy, parcel or box crosses directly. A dummy walks
  through and does not push the neighbor first. This is my pick for
  "consistent": one passage rule for everything.
- Doorways do not meet: a pushed parcel or box is blocked. A dummy pushes
  the neighbor if it can move and steps out into the freed cell; otherwise
  the move is blocked, because the dummy never stood on the floor outside.
From the open floor nothing changed: rooms move first, a blocked room is
entered through whichever doorway faces the dummy, and a parcel or box
pushed into a box enters that doorway, wherever along the edge it is. The
drawn floor between boxes (`DimensionConnections`) uses the same test, so
it is always straight. Guide and README state the rule.

Step preview (owner, 2026-10-12: "a way to communicate to the player when
they will push vs enter"; built on "add it"). `MovePuzzle` reports a `Step`
(Blocked, Walk, Push, Enter) through an `out` parameter; the handler ignores
it. `WorkshopControls` runs the same function on a copy of the puzzle for
each of the local seat's four directions every frame (`Hands.ahead`), so the
preview cannot disagree with the rule, including partner backstops and
facing doorways. Push means a box moved; a pushed parcel is a Walk, and an
entry that also pushes a parcel is an Enter. The direction buttons draw it
(`StepMark`: block ahead of the arrow, frame around its tip, bar across
it), and `Workbench` draws a yellow bumper or a green arrow on the floor
between this screen's dummy and the box, one frame behind the controls and
hidden during a passage. Only the local dummy's next step is shown. The
shapes and colours are my picks.

Leaving a room, the in-room copy of a dummy now stays opaque until it
stands in the doorway (progress 0.22..0.30) instead of fading from the
first frame: the doorway pose is clipped to the opening while inside, so
the old crossfade left the dummy mostly invisible on its way to the door
(owner: "the tiny guy when I exit seems to mostly vanish").

Self-placement and two-room cycles are allowed without cloning interiors.
`Entry.box` resolves the live entered instance, so a moved occupied box
changes its maker's return location. A mismatching prefix rebuilds from
primary ancestry, stops at the first repeated room, and retains finite
unique-room ancestry instead of inventing an outer anchor. Matching visits
retain their explicit trails. Depth-eight entry blocks unchanged; undo
remains available. Popped slots reset to defaults for canonical bytes.

Movement commits a trial Puzzle after all guards pass. Completion requires
designated primary boxes on numbered docks in the specified parent, every
parcel target occupied, every active parcel on a target and both makers on
distinct player goals. Targets may be emptied during planning. Shared history
retains 128 actions. Reset restores boxes, references and trails exactly;
completion waits for Next. Exactly two real participants are required.
Existing Rooms, Session, sender/sequence validation and host migration remain
without new runtime APIs. Missing seats disable commands; stale serials
cannot cross undo/reset/selection/participant changes. All twelve puzzles
can be selected from the public shelf.

## Presentation, controls and audio

Miniatures and focused rooms share one transform: inner tile scale is
1 / max(width,height). The longest box edge fills its containing cell.
Frames sit inside the rectangle with real doorway gaps. Offset door lips
preserve their tangent rather than projecting radially toward the midpoint.

Camera and maker passage share a 0.55-second clock: approach 0..35%,
doorway shrink 35..70%, interior step 70..100%, reversed on exit. The camera
holds the miniature during approach, then zooms continuously. Aperture
clipping and solid-wall occlusion prevent silhouettes showing through walls.
Same-room recursion and adjacent handoffs use these transforms. Interrupted
travel begins from the rendered pose; input stays responsive. Reduced motion,
level changes and zero-move reset discard stale passages. There is no fade.

Each settled room fits max(width,height)+6 tiles in a fixed centered square,
revealing actual containing-world context. Outer room retains its 0.96
viewport-side fit. Partner presence, position and room never change camera
targets; makers may leave the viewport. Surroundings resolve the live entered
instance, including moved copies and self-containing boxes. Drawing is bounded
to three ancestor contexts, three descendant levels and 48 instances per
context; subpixel previews become markers. No match objects are duplicated.

`geometry.tide` builds normalled, depth-tested cube instances for floors,
low cutaway walls, crates, stencils and maintenance makers. The orthographic
camera has a fixed 65-degree pitch and no yaw; ground coordinates preserve
the existing screen axes and camera framing. Each occurrence has a shared
ground plane; component elevations change world Y only, keeping heads, lids
and trim physically stacked. Interior floors rise by 0.018 containing tiles.
Finite contexts and descendant limits remain unchanged. Geometry, lighting
and render resources are machine-local; layouts, rules and audio are unchanged.

`render.tide` uses a common scene/depth for every visible occurrence: Rgba8
material plus Rgba16f normal/occurrence-scale MRT, a fixed-focus 1024-square
directional shadow map with four comparison taps, and half-resolution SSAO.
Each shadow tap compensates for the receiver plane's light-space depth slope,
so the fixed map footprint does not make miniature floors shadow themselves.
Eight normal-oriented hemisphere samples reconstruct world positions from
hardware depth and inverse camera projection. Metric radii/biases follow
the originating occurrence's tile scale. Two separable bilateral passes
respect normal and depth discontinuities; AO affects ambient light only.
No temporal jitter or geometry-dependent camera fitting is used. Alpha and
doorway ground-column discard rules are shared by all mesh passes. The
composite explicitly clips the board because Blit does not inherit Draw.Clip.
Workbench prepares geometry and SceneLighting owns view-only shader draws.

Square rooms, rounded corners and plain doorways (2026-10-12, later the
same day; this replaces the packing described in the next paragraph's
second bullet). The owner kept the lab look after seeing other styles, said
the packed box was "still off-grid just filled the gaps", expected the
puzzles to need redesigning, asked for the black/yellow lines on the
entrances to go, and suggested rounded or merged corners ("box marching").
- Every room is a square grid in the match itself. `SquareRoom`, run by
  `LoadLevel` on each authored room, packs a room that is longer one way
  out to its longer side with rows or columns of wall, the odd one below or
  to the right (above or to the left for puzzle 9's narrow room, the one
  place it mattered), moves everything in the room with it, and carries a
  doorway on a packed side to the new edge along a corridor of floor. The
  authored calls keep their old sizes; the layout a match holds is square.
  `RoomPadding` and its helpers are gone.
- Solutions: eight of twelve replay unchanged. Puzzles 8, 9 and 11 take the
  old solution with one or two steps repeated where a corridor is crossed
  (43, 35 and 28 moves), and show the same evidence as before. Puzzle 7's
  old solution does not adapt. A breadth-first search through the Move
  handler found a 27-move solution with no partner backstop, and the same
  search on the puzzle as it was before found one of 24 moves: the two
  backstops the old 41-move witness showed were never required. Its
  minimum evidence is now one occupied move, one parent change and one
  cargo transfer, and its lesson and three hints describe the 27-move
  route. "Carried together pins two partner-body backstops" below is no
  longer true of the witness. Puzzles 8, 9 and 11 were past the search's
  four million states, so their witnesses are not known to be shortest.
  The second puzzle's exhaustive count is still 167.
- Wall blocks: `WallTile` draws a cell as one cuboid, or as a cross with
  corner squares when an outer corner is rounded (both sides beyond it are
  open floor, radius 0.30 of a tile). `WallFillets` fills an inner corner,
  where on-grid wall runs along two sides of a floor cell, with a hollow
  curve of radius 0.20. Both curves are two more instanced meshes built once
  in `BeginSculpture` (`RoundMesh`, `FilletMesh`, six steps to the quarter),
  placed by `Corner` with a quarter turn per quadrant, and drawn by the lamp
  and scene passes after the cubes. A wall block is one paint now, shaded by
  the light alone: the darker rim, raised face and darker sides are gone.
  The fillet stands in the corner of a floor cell; nothing in the rules
  knows of it.
- The striped threshold on a doorway's floor is gone. The yellow posts
  either side remain.

Continuous cells and square boxes (2026-10-12). The owner asked for cells
next to each other to be "more continuous" rather than "clearly cells on a
grid", and for a box to always fill its cell as a square: rooms need not be
the same size inside, "but the format should still be a square".
- Wall cells that touch are one block: `WallTile` takes a bit for each side
  with wall beyond it (off the room's grid counts), draws its lid to the
  cell's edge, and extends the raised face to every joined side, so the
  darker rim runs only along a block's open sides. An inside corner has no
  rim. Floor cells meet without a joint, in two shades 0.025 apart so steps
  can still be counted.
- A room that is longer one way is packed out to a square with solid wall on
  its two short sides (`RoomPadding`, from `RoomTrim`): half the difference
  in tiles on each. A doorway on a packed side runs through the packing as
  a strip of floor as wide as the doorway. This is drawing only. The room,
  its doors, `DoorsMeet` and every puzzle are unchanged; the packing is not
  a place anything can be, and a passage crosses it on the way to the
  doorway. A box too small to detail is a square block too. The owner's
  words could also mean making every authored room square, which would
  change the puzzles; that reading was not taken and is theirs to ask for.

Full-window layout (2026-10-12; the owner: "the game should be most of the
screen, right now it's like a weird iframe like thing"). The framed panel
is gone. The scene is drawn over the whole window below the heading
(`SceneTop`: 104 px, 146 px under 1050 wide), and `InPlayfield`, the
sculpture's bounds, the composite's board and the shadow fit all use that
region, so a wide window shows as much of the containing world as fits. The
focused room sits in a square clear of the heading and of the controls
along the bottom (`SceneSide`, `SceneCenter`, `SceneBottom`: 80 px, 200 px
under 720 wide), with two of its tiles to each edge of the square where it
had three, so a tile is about 1.6 times its old size at 1280x800. The outer
room still takes 0.96 of the square. `MapLens.center` and `size` are still
the old panel's rectangle, but now only place the labels: the room, the
move count and the surroundings line are on dark plates (`Chip`) drawn by
`WorkbenchLabels` at stage Board 2, after the scene's composite, since they
lie over the scene. The two top plates are where the panel's label row was,
so the browser fixture's label crops are unchanged; the surroundings plate
sits between the buttons and the direction pad at 1050 wide and over, under
the top plates below that, where it can cover part of the outer room's top
row. Buttons and the help line are drawn over the scene by
`WorkshopControls`, the help line on a plate. The lamp's depth map is 2,048
square (was 1,024) because the shadow fit now spans the window rather than
the old square. The fixture reads what is in frame in the room's square
(`sceneSquare`, mirroring `SceneSide` and `SceneCenter`), not the window.

The players are small test robots (2026-10-12; the owner wanted "more
motion and maybe a different look", unsure about them being human, "maybe
robots or something"; the design is my pick and unconfirmed). `Maker` draws
a body in the seat's paint on two rubber treads, a front bumper, a pale
head with a dark visor and a yellow eye, the yellow/black calibration mark
on the head and an antenna tipped in the seat's paint. `BotPart` places
each cuboid to the bot's right and ahead for one of four facings, since
cuboids are never rotated. Motion is presentation state in `Picture`
(`actorFacing`, `actorStride`, `actorWobble`, `actorIdle`): the bot faces
the step it takes (through a doorway, the passage's side), hops twice a
cell and whips its antenna while its drawn position changes. A tenth of a
second after a step stops, and at once after a doorway passage, it turns
back to the camera, so a scene settles no later than it did before the
bots (a longer look-ahead and antenna decay failed the fixture's exact
undo comparison, which waits 24 ticks). A snapped pose (first frame, level change, reduced motion) is that rest pose.
There is deliberately no idle animation: the browser fixture compares whole
settled scenes byte for byte across time (undo, cycle exit), so a settled
scene has to depend on the puzzle alone. Player-visible text still says
"dummy"; renaming is open.

Current look: a bright crash-test lab (2026-10-12). The owner found the PSX
block filter made small nested boxes lose all their detail, preferred the
earlier clean render while their partner had not, asked for "a mix of
simple yet themed", and took my recommendation. `render.tide` is the 3D
revision's again, byte for byte: full resolution, soft shadows, SSAO, no
blocks, no shade steps. The theme is paint and large shapes only, so it
survives when a box is a few pixels wide: pale flooring whose slab joints
are the taped grid, a mid grey-blue void, light grey hall panels, boxes in
bold flat paints (green, blue, violet, coral red; Box 1's green is still
the browser fixture's `homeMaterial`), a yellow/black striped threshold on
the floor of every doorway between yellow posts (`HazardEdge` with a finer
stripe), warning-yellow parcel targets, and the dummies described below.
Wall panels keep the rim-and-face shape, floors the plain slabs, and the
UI its charcoal and yellow. The two paragraphs that follow describe the
superseded factory/PSX passes; what they say of the dummies, door posts,
hazard edges and UI still holds, what they say of blocks, shades, grain and
container colours does not.

The factory look (2026-10-12; the owner asked for "factory vibe, we are
testing dummies; PSX style", then after the first pass: the dummies "look
almost like parrots", "the art is still meh", "PSX but cleaner"; the
particulars below are unconfirmed picks):
Boxes are painted steel containers (green, oxide red, steel blue, violet;
the outer hall is bare steel). A wall panel is a darker rim around a
slightly raised face, both shades of the same paint. Floors are plain
concrete slabs with dark joints. Doorways stand between warning-yellow
posts, parcel targets are warning-yellow floor paint, and the hall's own
perimeter walls carry yellow/black blocks (`HazardEdge`). Players are
crash-test dummies: overalls in the AMBER/TEAL seat paint, rubber feet,
hands, belt and neck, and a head that is the yellow/black quartered
calibration mark. The first pass had a seat-coloured head with a small
black/white mark, which read as a parrot's eye and beak. The code still
calls them makers. Parcels are unchanged. Panels and buttons are square,
charcoal and steel grey, with warning-yellow accents and a strip of warning
tape along the top of the viewing panel, above the label crops the browser
fixture reads. There are no enemies, threat events, jumpscares or timers.

The PSX treatment lives in `SceneComposite` and changes no pass before it.
The board is read in square blocks of `block` logical pixels, the board's
width / 220 rounded and at least 1 (2 at 1280x800, 4 at 1920x1080, 2 on a
390-wide phone): every pixel of a block samples the G-buffer, depth and AO
at the block's middle. Light is one scalar, capped at 1 so a fully lit top
face sits exactly on a shade, and cut to 14 shades. A 4x4 ordered pattern
over blocks only mixes two shades within 0.2 of a shade of where they meet,
so evenly lit faces are flat and only shadow and occlusion edges are
patterned. AO strength is 0.50 (was 0.70). Because the scalar multiplies
all three channels, a material keeps its exact chroma in every shade, which
is what the browser fixture's `materialMatches` relies on; per-channel
colour quantization would break it. Tried and dropped: a per-texel grain
(noisy; the owner asked for cleaner), carrying it in the Rgba8 material's
alpha (low alpha destroyed the stored material, presumably because colour
targets are premultiplied), and snapping vertices to the block grid for PSX
wobble (thin decals darkened unevenly and half a floor tile went dark; the
cause was not pinned down beyond removing the snap fixing both).

WASD/arrows walk/push, Z/Backspace undo, R reset, H hints and Enter continues.
Touch uses the same directional buttons. Held directions require release
after serial changes. Hints grow/wrap to fit. Ambience, sound and reduced
motion are local options; Rooms volume settings remain. Presentation and
audio never enter the match hash.

`sfx/make_sounds.py` reproduces original assets. The melody is replaced by
`ambience.wav`: a seamless 12-second mono 24 kHz PCM16 ventilation/fluorescent
hum, filtered seeded noise and subtle 50/100 Hz harmonics. Peak -28.754 dBFS,
RMS -36.725 dBFS before 0.27 gain; no clipping and a 14-PCM-unit boundary delta.
There are no stingers or sudden threat cues. SHA256:
`274097dcafdf4c1355d435444dc2a3ae8bc59a6cb77684b320d4448e2498c715`.
The obsolete `workshop.wav` is removed. Seven short cues remain byte-identical.
`enter.wav` remains the 300 ms seeded-noise friction and air puff, without
oscillators, coin ringing or note sweeps: 7,200 mono 24 kHz PCM16 frames,
silent endpoints, no clipping, -12.906 dBFS peak before 0.38 gain. SHA256:
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
Entry/exit/depth changes select kind 2 before push cues, including doorway
travel that pushes a parcel.

## Solvability and verification contract

The authored replacement campaign has these production-command witnesses:

| Puzzle | Moves |
| --- | ---: |
| A room outside | 10 |
| No handle inside | 12 |
| The other opening | 20 |
| A place to stand | 31 |
| Passing place | 39 |
| The same corner | 21 |
| Carried together | 27 |
| Before it moves | 43 |
| Two ways home | 35 |
| Return address | 51 |
| Folded landing | 28 |
| Crossed return | 28 |

Return address requires a copied box's parent to change into its own
canonical interior and then extraction. Folded landing forms and extracts
a primary self-cycle. Crossed return forms and extracts a primary two-room
cycle. Carried together pins two partner-body backstops with counterfactual
production commands: removing that partner makes the box push instead of
admitting the approaching maker.

No handle inside exhausts all 167 reachable production-command states for
either initial maker acting alone with the other body removed, ignoring
player goals. Neither can both dock the home and deliver its parcel. This
establishes a mechanical co-op dependency for that layout; it does not prove
solo impossibility or human difficulty for the entire campaign.

`levels.tide` owns twelve nested-start layouts and three-tier hints. A short
intro teaches leaving two containing boxes; later puzzles develop backstops,
occupied transport, narrow storage, offset exits and movable recursion.
Witnesses in `test/solutions.h` are absent from the public game. Campaign
checks invoke actual generated Move handlers with two joined players: every
action succeeds, both seats act, production completion is true, and undo /
reset-replay restore exact state. Witnesses prove solvability, not shortest
paths or measured human difficulty. Final campaign pins/results follow below.

PuzzleCue carries the accepted direction and source Traveler for travel.
An outward step emits kind 2 even when cycle rebasing leaves room/depth/box
identity unchanged. A paint-owned all-seat listener handles this exit only
when snapshots missed it, validates the live box/endpoint against late states,
starts at the captured border pose and clears pending cues on undo/reset.
Ordinary crossings consume cues without replay. This is event/local data,
not new match state. The test-only `test/cues.tide` primitive/source listener
enables a typed-command cue fixture and is absent from the public game.

Core fixtures cover bounds, offset traversal, canonical collision, occupied
instances, cycles/extraction, atomic blockage, history, snapshots, packing and
deterministic replay. `test/browser.mjs` runs two muted Chrome/Edge windows
through real unordered/unreliable WebRTC using existing bounded process/pipe
and serialized-frame helpers beside it. Checkpoints require fresh verified
ticks and packets; browser/runtime/graphics errors fail. Rendered labels and
colour crops observe production play without wasm inspection or debug exports.
Crops on one page run sequentially to avoid Chrome's temporary viewport race.
Local relay is default; TIDE_INSIDE_OUT_RELAY probes production signalling.
Real devices, NAT and wall-clock network latency remain separate coverage.

Bounded scripts, audio and browser captures live in ignored
`build/inside-out-captures/`; TIDE_HIDDEN=1 and muted browsers prevent owner
audio devices opening. `inside_out_page` / `private_games` stage the opted-in
prototype beside its thumbnail. Public play remains
<https://tide-engine.dev/prototypes/inside-out/>; catalog authentication and
serving/deployment contracts remain in `notes/private-prototypes.md` and
`notes/docs-site.md`.


## Square rooms and rounded corners validation (2026-10-12)

Match layouts, four witnesses, puzzle 7's lesson and hints, presentation
and the browser fixture's numbers for the two rooms it measures. Native and
web debug builds are warning-free. All 33 fixtures pass natively and under
WASI, including the twelve replays, the exchanged jobs and the second
puzzle's exhaustive check. `inside_out_play_webgl` passes in 168.92 s and
`inside_out_play_webgpu` in 123.23 s, on the first run. In the fixture the
carried home is read as five by five with its contents half a tile higher,
the recursive home as six by six with its contents half a tile to the left,
and the self-cycle loop is entered one step later and is one step longer
(`neesee`), as the home's east doorway has a corridor cell.

Native captures of puzzle 12 at 1920x1080 and of puzzle 8 at 1280x800 were
inspected: rounded outer corners, filleted inner ones, the five by five
violet box on its cell, doorways without stripes. The other ten puzzles
were not looked at in the new shapes, and nobody has played the four
changed puzzles. The thumbnail is 166,680 bytes, SHA-256
`810dccc05e3f11edb53538e2aa6e9a68cd5b01a85074b67e1e21b95ac5186926`. The
docs production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Continuous cells, square boxes and the title-crop failure (2026-10-12)

The intermittent "recursive puzzle progression agrees on both screens"
failure is explained. It recurred twice here (WebGL, then WebGPU with a
variant that waited up to 60 more ticks, so it was never a race). The
failure screenshots of the two pages show the same title, "12 / 12 Crossed
return", and differ in that crop by exactly one pixel, at 469,42, inside
the text. The fixture compared PNG bytes. That one comparison between two
pages now allows up to 8 differing pixels (`pixelsApart`); a different
title differs in hundreds. Why one text pixel differs between two pages on
one machine is not established; each page fills its glyph cache in its own
order, which is my guess. Other cross-page label comparisons are still
exact and have not failed.

Native and web debug builds are warning-free; the 33 fixtures pass natively
and under WASI. On the final game code `inside_out_play_webgl` passes in
174.75 s and `inside_out_play_webgpu` in 141.85 s. The WebGL run had
started with the waiting variant of that last check loaded and the WebGPU
run with the pixel tolerance; the tolerance has passed once on WebGPU and
once on WebGL since (179.13 s). Native captures at 1280x800 and 1920x1080 were
inspected: joined wall blocks, a plain floor, and the 5 by 4 box of puzzle
12 filling its cell. The thumbnail is 163,638 bytes, SHA-256
`0d7b85de8684b608ca65fdb3d52cd843a117f16731244c7df5b6399b3540636c`.
Nothing was staged or deployed. No CI was dispatched.

## Full-window layout validation (2026-10-12)

Presentation and the fixture's layout numbers only. Native and web debug
builds are warning-free; the 33 fixtures pass natively and under WASI;
`inside_out_play_webgl` passes in 166.15 s and `inside_out_play_webgpu` in
129.57 s. Both are about 35 s slower than before the layout (WebGL was
124.68 s): the scene now covers the window, with full-window occlusion
passes and a larger lamp map, in headless browsers. Their budget went from
180 s to 300 s, as 166 s left too little room. What the larger scene costs
on real hardware is unmeasured. Native captures at 1280x800, 1920x1080 and
390x844 were inspected. The thumbnail is 201,827 bytes, SHA-256
`963603aa6d45534b6946143134b65d935b50825fe64480efdc3450a934f853de`; the docs
production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Test robots validation (2026-10-12)

Presentation only (`paint.tide`, thumbnail). Native and web debug builds
are warning-free and the 33 fixtures pass natively and under WASI. The
first version failed both two-browser fixtures at "partner undo restores
the exact pre-cycle scene"; with the quicker rest described above
`inside_out_play_webgl` passes in 124.68 s and `inside_out_play_webgpu` in
99.87 s. Native captures at rest, mid-roll and settled were inspected at
1280x800. The thumbnail is 101,034 bytes, SHA-256
`ca171ab46b4a5ed9baf22132480b01ce1a3962f534e2fc429552dc2a293d5275`; the docs
production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Crash-test lab look validation (2026-10-12)

Presentation only: `render.tide` restored to the 3D revision's, new paints
and a doorway threshold in `paint.tide`, one constant in `ui.tide`, README,
catalog text and thumbnail. Native debug and web debug builds are
warning-free; the 33 fixtures pass natively and under WASI;
`inside_out_play_webgl` passes in 127.32 s and `inside_out_play_webgpu` in
100.31 s; the docs production build passes. Bounded native captures at
1280x800, 1920x1080 and 390x844 were inspected, including nested boxes a
few tiles across, which keep their walls, thresholds and dummies. The
thumbnail is the 1920x1080 capture of puzzle 12 halved, 101,371 bytes,
SHA-256 `0d93a81d4c2b393024ab1be88d424391b11516fafbd41f3f1ed3d778fbc070ef`.
Web release pages were not rebuilt or staged and nothing was deployed. No
CI was dispatched.

## Facing doorways and cleaner PSX validation (2026-10-12)

Rules: every authored door pair was not audited; instead all twelve
witnesses were replayed under the new rule. Eleven are unchanged. Passing
place's old witness pushed its parcel across a bridge between doorways that
do not meet (a 4-cell box beside a 3-cell-high one) and is replaced by a
39-move witness found by a breadth-first search through the generated Move
handler (1,587,462 states; shortest by construction; the search was scratch
code and is not in the tree). The layout is unchanged and its minimum
evidence, including one partner backstop, still holds; its third hint
follows the new opening. The two-box cycle fixture now expects a dummy
leaving A to walk into the facing B rather than push it. Layouts whose
solutions never used a mismatched bridge may still play differently where
a dummy used to push a facing neighbor from a doorway; no witness did.

Native debug and web debug builds are warning-free. All 33 fixtures pass
natively and under WASI. On the final code (rules, cleaner look, exit
passage, step preview) `inside_out_play_webgl` passes in 131.69 s and
`inside_out_play_webgpu` in 102.34 s; the two batches before it, without
the preview and without the exit change, passed as well, so the title-crop
failure recorded below did not recur in three further WebGL runs. Its cause
is still unknown. The fixture has no assertion for the step preview or for
a blocked mismatched doorway; the rule is covered by the campaign replays
and the cycle fixture, the preview by inspected captures only.

Bounded native captures at 1280x800 and 1920x1080 were inspected: flat
faces, the yellow/black dummy heads, puzzle 8 without its former bent
bridge, a leaving dummy whole through its doorway, and the green enter
arrow with the matching button icon. A push bumper was not captured. The
thumbnail is the new 1920x1080 capture of puzzle 12 halved, 67,276 bytes,
SHA-256 `d52b7bb9900a7c9e5774b13f4a3ffb1b7a14f91570977a42f64ef3f087c68cbd`.
The docs production build passes. Web release pages were not rebuilt or
staged and nothing was deployed. No CI was dispatched.

## Factory / PSX restyle validation (2026-10-12; first pass, superseded look)

Presentation only: `render.tide`, `geometry.tide`, `paint.tide`, `ui.tide`,
player-visible "maker" wording in hints, guide, README and catalog, and the
thumbnail. No match, layout, witness or audio file changed. Box 1's paint,
AMBER and the parcel colours are unchanged, so the browser fixture's
material constants are too.

Native debug and web debug builds are warning-free. `inside_out_tests`
passes natively and under WASI. `inside_out_play_webgpu` passes (104.89 s).
`inside_out_play_webgl` passed three of four runs (133.71, 135.62, 135.68 s).
Its first run failed the fixture's last progression check, "recursive puzzle
progression agrees on both screens": the 2D level-title crops of the two
pages differed after the final Next. That crop holds no 3D scene pixels and
the fixture saves no picture of it, so the cause is unknown and the fixture
is unchanged; whether it predates this revision is unmeasured. The docs
production build passes with the new catalog text and thumbnail.

Bounded two-player native captures at 1280x800, 1920x1080 and 390x844 were
inspected: nested start, outer hall, puzzles 8 and 12, hint panel and the
waiting panel. A `--players` run joins both seats by itself, so the restyled
title page was only exercised by the browser fixture, not looked at. The
960x540 thumbnail is the 1920x1080 capture of puzzle 12 halved, 133,592
bytes, SHA-256
`ef53445d34150d7e80fa200a782f51c531988557d80c683c4c58d228f1820bc1`.
Web release pages were not rebuilt or staged and nothing was deployed: the
public page still shows the previous look. No CI was dispatched.

## 3D revision validation (2026-10-12)

Native and WASI retain all 33 fixtures, including twelve production-command
solutions, exchanged jobs, exact undo/reset/replay and the exhaustive second
puzzle co-op check. No match, layout, hint or audio file changed. Native,
web debug and web release builds are warning-free. Both two-browser fixtures
pass: WebGL 131.81 s and WebGPU 99.18 s. They retain actual online admission,
three production completions, phone touch, continuous doorway growth/shrink,
nearby/offscreen partner framing, carried interiors, physical self-nesting,
undo and unchanged-room/depth/entrance cycle exits.

Illumination multiplies material RGB uniformly. Camera/interior assertions
therefore compare exact brightness-normalized material masks, with mandatory
feature and boundary presence, rather than treating a moving shadow as a
camera movement. Quantitative zoom growth, maker visibility, narrowing and
travel direction remain checked. Undo and settled-cycle whole-scene RGB
comparisons remain exact. Old uniform wall crops are replaced by real edges.

Bounded 1280x800, 1920x1080 and 390x844 native captures were inspected, including
offset entrances and the unchanged-identity primary self exit. Components
share each occurrence's ground plane so lids, heads and trim stack vertically.
CPU cropping includes a conservative caster guard based on its top above
the minimum containing floor; artificial cut faces cannot shadow the view
under the fixed light and camera. The refreshed 960x540 gameplay thumbnail
is 123,388 bytes, SHA-256
`d644e726fc134a33a6dad017119f1a2eee6b8ce0e54754dd41763ace2688f5e3`.

Controlled full/no-AO/no-shadow native captures prove both effects: removing
AO changes 30,471 initial scene pixels (maximum channel delta 55); removing
cast shadows changes 37,375 (maximum 92). An empty 5,280-pixel floor patch
changes zero pixels with AO removed, and both comparisons change zero pixels
outside the scene. Temporary comparison edits were restored byte-for-byte.
A miniature review caught scale-dependent PCF self-shadowing. Receiver-plane
tap depth correction removes that checker while preserving contact bias and
cast shadows: adjacent luminance variation in the miniature drops 40%, with
no changes to the empty floor or surrounding UI. The original AO blur remains.
Final lighting comparisons and desktop/phone captures use this corrected
renderer. Evidence lives in ignored `3d-final-lighting-*`, `3d-shadow-plane-*`
and `volume-*` captures under `build/inside-out-captures/`.

The production site build, zero-error/warning check, eighteen analytics/admin,
227 grammar and five HTTP/SEO/access tests pass. All 64 release/staged/package
playable pages and four thumbnails agree. Inside Out is 1,386,431 bytes,
SHA-256 `7d7e8d231adfa7aa7844a6e162c8bc0fddcc783e02ddf3d6d439741aa5deeb8b`.
Its pack contains each unchanged current WAV exactly once; removed music,
coin and tonal-doorway assets remain absent. No CI was dispatched. Physical
phone/GPU coverage, audio listening and external NAT conditions remain separate.

Source `482c066b` deployed directly to the existing Fly site as release 264,
image `deployment-01M4PHAKNDKRHZZ2MGVTEA16JV`, with passing health checks.
All four public prototype pages and thumbnails return 200 and match the
production package; Inside Out's page/thumbnail have the final hashes above.
Noindex, COOP same-origin and COEP require-corp remain intact. The downloaded
public 3D page passes the full two-player WebGPU fixture through the production
relay in 99.6 s, including recursive passages, occupied transport, exact undo,
three solutions and phone touch. The collaborative preview reloads the public
title screen without console errors. No CI was dispatched.

## Nested-box revision validation (2026-10-11; 2D presentation superseded)

Final native desktop, 900px and phone captures include the primary self-cycle
exit whose canonical room, depth and entered box remain unchanged. Approach,
middle and settled poses show continuous passage; doorway jambs use square
rectangles. The refreshed 960x540 gameplay thumbnail SHA-256 is
`082d090d130c07b526c6921cea56490a61fc4d23e49dfb58f0e49187d2d9ed78`.
A device-free 15-second production mixer capture verifies the new ambience:
source correlation 0.9999975, gain 0.2699908, peak -40.125 dBFS, identical stereo
channels, no clipping and exact repeated samples across the 12-second loop.
Hardware listening is not claimed.

The native build is warning-free and all 33 fixtures pass: 30 core cases,
all twelve command witnesses with undo/reset/exact replay, all twelve with
exchanged jobs, and the second puzzle's exhaustive mechanical co-op proof.
The WASI aggregate passes as well. Two-browser WebGL and WebGPU checks pass
in 117.9 and 57.4 seconds, respectively, with three production solutions,
phone touch, stationary-partner camera crops, occupied-box context, actual
primary self-nesting/undo/extraction and an unchanged-room/depth/box cycle
exit. The final square-jamb presentation and integrated dependency checks
are recorded below after staging.

Bounded native desktop, 900-pixel and phone captures review nested starts,
offset miniature doorways, self-recursive entrances and the exact primary
self-exit. The final same-room passage starts at its real east border,
crosses continuously and restores aliases without a finite-end pop.

A 15-second device-free production ambience capture is stereo, unclipped,
and repeats its twelve-second period byte-identically. Source correlation is
0.9999975, measured gain 0.2699908 and mixed peak -40.125 dBFS. The seven
retained short WAVs remain unchanged; public pack validation follows staging.
Real phones, hardware audio listening and network/NAT conditions beyond
this machine remain unmeasured.

Integration through `origin/dev` `825c6542` is clean. Shared compiler,
network/host dependencies and current Underfoot were rebuilt. Native and WASI
retain 33/33 passing fixtures without warnings; final square-jamb two-browser
checks pass, WebGL in 113.42 s and WebGPU in 49.89 s. All 64 release pages were
rebuilt/restaged (46 docs examples, 14 gallery games, four prototypes), and
their staged/production-package bytes agree. All four thumbnails agree too.
Inside Out HTML is 1,356,103 bytes with SHA-256
`f305bd41a4733ba85f119337e4bbaf9ee9ddbd96dff31ee71d572d18a82b500e`.
The decoded 784,288-byte pack contains each of the eight current WAVs once,
including the exact ambience/doorway assets above. Removed workshop music,
the original coin cue and the previous tonal doorway cue are absent.
Site check has zero errors/warnings; 227 grammar, 18 analytics/admin and five
production HTTP/SEO/access checks pass. The production build and complete
WebGL example/gallery rendering smoke pass. No CI was dispatched.

Source `7a44440f` deployed to the existing Fly site as release 263, image
`deployment-01M4PF2HHN71TH2ZJ2C4S8M4XF`, with passing machine health checks.
All four anonymous prototype pages and thumbnails return 200 and match the
tested package. Inside Out retains noindex, COOP same-origin and COEP
require-corp; its page and thumbnail have the exact hashes above. The full
two-player WebGPU fixture on downloaded public bytes through the production
relay passes in 47.7 seconds. The collaborative preview displays the updated
title screen without console errors. No CI was dispatched.

## Earlier published build (superseded)

The original isometric twelve-puzzle campaign passed fourteen native and
WASI fixtures plus WebGL/WebGPU co-op checks. Its witnesses took 8 to 59 moves.
The owner found those puzzles too easy; those lengths were never a human
difficulty measurement. That campaign and fixed-role presentation are replaced.

The initial public Fly deployment succeeded on 2026-10-11, machine version252,
image `deployment-01M4NW4CRPNFH1X3P4MRYWDY2B`. Anonymous page/thumbnail requests
returned 200 and the page hash was
`57a970dbf68a347b5502dca6eec59db4249c88b691a0881ee5e077ac2d39f21a`.
Two browsers exercised those downloaded bytes with the production relay in
17.1 seconds. Later independent JSON/text and session changes passed scoped
integration checks. This evidence describes the earlier build only.

## Shared-world revision validation, 2026-10-11

All eighteen game fixtures pass natively and under WASI: sixteen simulation
cases plus the full twelve-puzzle production-command campaign and its
identity-swapped replay. Native debug, web debug and web release game builds
pass without warnings. The final browser fixtures pass on WebGL (32.74 s)
and WebGPU (17.41 s), covering real two-browser admission, Shift pushing,
free entry by both makers into one interior, synchronized undo/reset and
held-input recovery, the first witnessed completion/progression and phone
touch movement. No runtime or graphics errors were reported.

Native captures at 1280 by 800, 900 by 800 and 390 by 844 were inspected.
The final capstone capture shows both makers, a shell physically nested in
Mint and the Mint/Lilac reference cycle. The refreshed catalog thumbnail
comes from that actual gameplay capture. A separate self-door capture shows
the maker entering the same canonical room twice. These are desktop/browser
viewport checks; physical phones, cross-machine NAT and human difficulty
remain unmeasured.

The site check has zero errors/warnings; production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. The
staged page, release page and production site copy have identical SHA-256
`fcd07e6e5347c708a75713e1e6ba8257b37082cade06c8181f1503e289865d7a`.
The new thumbnail SHA-256 is
`0db7607f0c8c8c579514a7c9d735fd46dcd8d989cb81df8557ff518fae959920`.

Integration with `origin/dev` through `f4fd43fe` preserved the newly reduced
prototype catalog and the relocated browser helpers. Only documentation
conflicted; the fixture import paths merged cleanly. Native eighteen-case
validation, WASI and both real browser checks pass again (WebGL 26.32 s,
WebGPU 16.74 s), and all 37 relay/lobby Node cases pass. The site check/build
and 23 HTTP/admin/analytics/SEO/access tests pass with the new catalog. All
three current prototype pages were staged; ignored HTML left by the removed
prototypes was cleared before packaging. The Inside Out page hash is unchanged.

The existing Fly site was updated directly from source `0530c23f` on
2026-10-11, release 255, image `deployment-01M4NZJJYP3NF97YMDSPHKSDDF`.
Its machine reached healthy state. Anonymous game/thumbnail requests return
200 with the exact page and thumbnail hashes above; the page retains noindex,
COOP same-origin and COEP require-corp. Two separate muted browsers exercised
the downloaded published bytes with `wss://relay.tide-engine.dev` on WebGPU
in 16.5 seconds, including join, shared controls, undo/reset, completion and
phone touch. This uses the fixture's controlled frame clock and does not
claim physical NAT or human latency coverage. No CI workflow was dispatched.

## Focused-view revision validation, 2026-10-11

The per-maker camera and material/audio changes leave the campaign and match
rules unchanged. All eighteen simulation/campaign cases pass natively and
under WASI. Native debug, web debug and web release game targets build without
warnings. Inspected native captures at 1280 by 800, 900 by 800 and 390 by 844
show one focused scene, the physical containing world, nested capstone rooms
and the changed containing Mint on self-entry. The new catalog thumbnail comes
from `focus-capstone-desktop-1.png`.

Both real two-browser fixtures pass: WebGL 125.63 s and WebGPU 46.96 s,
with no runtime, browser or graphics errors. They compare independent
Workshop/Mint views, the canonical interior reached through opposite doors,
actual outside geometry before/after an occupied-shell push, and physical
versus self-recursive containing worlds. Entry/exit, partner undo/reset,
held-input recovery, first-puzzle completion/progression, real shelf
selection and phone touch all pass. An initial fixture failure compared a
720 px baseline header with an 800 px return: height-dependent backdrop grain
changed its pixels. Restoring both viewports before the unchanged strict
comparison fixed the test; the intended puzzle selection was already correct.

A bounded, device-free 330-frame native two-player run captured the actual
entry and exit mix with music disabled through the normal guide. Each 5.5 s
48 kHz stereo capture contains two isolated threshold cues. Correlation with
the upsampled source is 0.99999955, measured gain is 0.379992, global peak is
-24.675 dBFS, and neither mix clips. No audio device was opened; this verifies
the asset and mixer path rather than a human listening judgment. The source
`enter.wav` SHA-256 is
`6dd808ceb4bad8a85c56ca231312e1c6608b93e8c8382d3dffc2b9677c115dda`.

The site check has zero errors/warnings; its production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML are identical with SHA-256
`593221e6e4a00a449cbfdefb6508383f07cf35cbf7217b3e37315bd096138f14`.
The refreshed thumbnail SHA-256 is
`adb64454b74621e664244b23931bfee1bcc01c1c21290ab6a18525745cceca3d`.

Source `f8f61e9e` was merged/pushed to `dev` after local validation; `origin/dev`
had no newer changes. The existing `tide-website` Fly app was updated directly,
release 256, image `deployment-01M4P1D8MP14MTP7X08VBF1594`. Deployment checks
passed and the machine reached healthy started state. Anonymous published
game/thumbnail requests return 200 with the exact hashes above, retaining
COOP same-origin, COEP require-corp and noindex. The collaborative preview
points at the public URL. No CI workflow was dispatched.

Two separate muted browsers exercised those downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 44.9 s: admission, independent
dimension cameras, canonical interior parity, moved-shell surroundings,
self-recursive entry/exit, shared undo/reset and held-input recovery,
completion/progression, shelf selection and phone touch all pass with no
runtime/browser/graphics errors. This uses the controlled frame clock;
physical phones, cross-machine NAT and human latency remain unmeasured.

## Spatial zoom and automatic-entry revision validation, 2026-10-11

Walking into a physical room now pushes first and tries its facing doorway
only when that push fails. Shift and the touch push toggle are removed.
Entry with a parcel at its doorway remains atomic; the travel sound takes
priority over the cargo push. A full entrance trail prevents further entry
but still allows a room push.

All twenty-one game fixtures pass natively and under WASI: nineteen simulation
cases and both twelve-puzzle production-command campaign replays, including
exchanged maker identities. The campaign repairs add physical backstops and
adjust latch handoffs for automatic entry. Its twenty-one partner-only latch
push probes retain the physical-shell dependency while allowing valid fallback
entry or one inward doorway parcel push. Undo, reset and deterministic replay
remain exact. Native debug, web debug and web release targets build without
warnings. These witnesses establish solvability, not shortest solutions or
measured human difficulty.

The initial spatial two-browser fixtures pass, WebGL in 84.87 s and WebGPU in 54.97 s,
with no runtime, browser or graphics errors. New pixel-area assertions require
the miniature to grow progressively during entry and shrink during exit;
the settled/initial area differs by more than twofold. Existing strict
canonical-interior, occupied-shell parent-context and recursive-camera
comparisons pass. Admission, shared undo/reset, held-input recovery,
completion/progression, shelf selection and phone touch remain covered.
An initial canonical-interior comparison caught the camera's asymptotic
settlement: tiny residual affine differences changed edge pixels. The camera
now reaches its exact target after the finite transition, and idle cell
interpolation also settles exactly. The strict comparison was retained.

Inspected bounded native captures at 1280 by 800, 900 by 800 and 390 by 844
include early/middle/late entry, exit, self-reference and nested capstone
views. Mint's wood area grows 2,651 to 8,625 to 25,936 to 33,970 pixels on
entry; exit decreases 39,775 to 21,532 to 5,878 to 2,648. The focused maker,
actual containing world and outside partner are visible in the same scene.
The catalog thumbnail comes from `spatial-capstone-desktop-2.png`, showing
Peach inside Mint with Amber outside.

The new doorway effect uses only seeded filtered noise, with no tonal body,
sweep, echo or ringing tail. The 300 ms source has RMS -25.452 dBFS and SHA-256
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
A bounded, device-free two-player capture with music disabled through the
guide confirms both entry and exit use this asset: correlation 0.99999992,
measured gain 0.379988 and cue peak -21.310 dBFS. Both 7.0 s, 48 kHz stereo
mixes remain unclipped. The previous published HTML contained the previous
threshold asset's exact bytes; its remaining pickup similarity plausibly
came from that asset's low sine body. These checks verify asset routing and
mixer output, not a human listening judgment.

The initial spatial site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML match SHA-256
`3d3d048342d10a72476529eedbcfa75f4f5ca49291851e54c507fdf994c68547`.
The packaged asset pack contains the exact new doorway WAV bytes.
Thumbnail SHA-256 is
`2e8c973ae731d9f677d89c779b54b1cee90388bfa87bbc3678121e784940b19b`.

The maker-passage follow-up builds without warnings natively and for web
debug/release. All twenty-one native fixtures remain green; the final WASI
fixtures and two-browser checks pass again, WebGL in 81.12 s and WebGPU in
54.75 s. The new stationary outside-view assertion tracks Amber's actual
body pixels: its footprint shrinks progressively by more than threefold and
its centroid moves toward the doorway across early/middle/late/settled frames.
This independently verifies the maker's passage rather than camera zoom.
Existing camera, canonical-interior, occupied-shell, recursive, shared-command,
progression, shelf and phone-viewport checks remain green.

Bounded native captures also cover physical entry/exit from both views,
recursive alias handoff with a stationary partner, fast inner steps,
immediate reversal/Undo and a rebased exit whose trail depth stays equal.
Desktop and phone-viewport passage captures were inspected. The current
settled capstone capture is byte-identical to the existing thumbnail.
A separate read-only renderer audit found no further critical blockers.

Integration with `origin/dev` through `1e969c61` was clean. Incoming compiler
changes triggered game and public-page rebuilds; the twenty-one native tests
passed again. All 64 packaged pages match their release artifacts: 46 docs
examples, fourteen public gallery games and four prototypes, retaining
Underfoot, Inside Out, CRACKED and Two Forks in the authenticated catalog.
All 225 grammar checks and the full WebGL examples smoke pass, including
the 46 docs examples, two embedded games and fourteen gallery games. The
site check has zero errors/warnings, eighteen analytics/admin tests pass,
and the final production build plus five HTTP/SEO/access checks pass.
Final release, staged and packaged Inside Out HTML has SHA-256
`c112d75234015c1ff4ea5f6fc27e00e78dc82e27d8e1b30439a94021f0d8ad33`.
Decoding the packaged asset pack confirms the exact new doorway WAV and
the absence of the previous doorway WAV bytes. No CI was dispatched.

The final merge through `origin/dev` `7c4e0835` brought unrelated CRACKED
and Underfoot graphics/game changes without Inside Out overlap. Their
web-release pages were rebuilt/restaged; all 64 playable pages and all four
prototype thumbnails match the final packaged bytes. The production docs
build and five HTTP/SEO/access checks pass again. No Inside Out game test
restart was needed for those unrelated incoming changes.

Source `9d25977b` was pushed to `dev` and deployed directly to the existing
`tide-website` Fly app, release 258, image
`deployment-01M4P4XTA0MGC534C21G0PTWBC`. Deployment and machine health checks
passed. Anonymous Inside Out HTML and thumbnail requests return 200 with
the exact final hashes above, retaining noindex, COOP same-origin and COEP
require-corp. The published asset pack contains the exact new doorway WAV
and none of the previous doorway WAV bytes. The collaborative preview
navigates to the public URL; snapshot requests fail inside the preview client.

Two separate muted browsers exercised the downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 63.4 s. Progressive maker motion
from the stationary outside view, spatial entry/exit zoom, canonical interior
parity, occupied-shell context, self-recursion, admission, shared undo/reset,
held-input recovery, completion/progression, shelf selection and phone touch
all pass without runtime, browser or graphics errors. This uses a controlled
frame clock; physical phones, cross-machine NAT and human latency remain
unmeasured. No CI workflow was dispatched.

## Local dimension framing validation, 2026-10-11

The owner chose the whole current room with a one-tile outside rim. The
partner-fit helper, presence mask and camera transition trigger are removed.
Inner rooms use the fixed square scene viewport and seven-cell span used in
this revision; the outer workshop retains its scale. Simulation, puzzle layouts,
doorway passage and audio are unchanged. The README and authenticated catalog
description now describe the local framing, and the thumbnail comes from
`rim-capstone-desktop-1.png`, with Peach nested in Mint and a recursive door.

Native debug and web debug/release builds pass without warnings. All
twenty-one simulation/campaign fixtures pass natively and under WASI.
The final real two-browser checks pass, WebGL in 109.39 s and WebGPU in
66.05 s. The new regression accepts all ten partner moves and compares four
nonblank scenery patches exactly while the partner walks far away, approaches,
enters and exits. Amber's rendered pixels are absent in the outside positions
and present after entering the local room. Existing progressive camera zoom,
maker passage, canonical parity, occupied-shell context, recursion, shared
commands, progression, shelf and phone-touch assertions still pass.

The first browser attempt exposed a fixture error: four simultaneous clipped
screenshots on one page raced Chrome's temporary capture viewport. The saved
page was translated by exactly one crop origin. Capturing those crops
sequentially fixed the failure without changing crop geometry or assertions.
Desktop and 390 by 844 native captures show readable complete rooms, bounded
rims and visible doorway motion, including self-reference. A read-only camera
audit found no critical blocker. Physical-phone/NAT coverage remains separate.

Site check has zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. Release, staged
and packaged HTML match SHA-256
`67de50e12c7e96b3d0e1128b3959a8800b88199156e0ef18056fbf19bb2bbeb9`.
The staged and packaged thumbnail SHA-256 is
`48f24a67cf3d60c14bfbb7559d280ae4604a456029997532f43124fb0aae1707`.
The inline pack retains the exact current doorway WAV. No CI was dispatched.

Integration through `origin/dev` `e377d0a9` was clean. Incoming compiler,
Fetch/platform/web runtime and docs changes triggered dependency rebuilds.
The rebuilt game and twenty-one fixtures pass without warnings natively and
under WASI; both final browser checks pass again, WebGL in 106.74 s and
WebGPU in 63.92 s. All 64 public game/example pages were rebuilt and match
release, static and production-package bytes; all four prototype thumbnails
also match. Current Underfoot changes are retained. Docs check is clean,
all 226 grammar checks pass, and the production build plus five HTTP/SEO/
access checks pass again. The integrated Inside Out HTML SHA-256 is
`761bae9375434b22596e6bd05711a6d077f01cb9a034dfa31340d8f0bf507943`;
the thumbnail hash is unchanged from this revision. The inline pack still
contains the exact current doorway WAV. Source `ebbd0649` was pushed to `dev`.

That source deployed directly to the existing `tide-website` Fly app as
release 259, image `deployment-01M4P66FRKJFQ5BR7SKC3E045M`. Machine and
deployment health checks pass. Anonymous page/thumbnail requests return 200
with the exact integrated HTML and current thumbnail hashes, retaining
noindex, COOP same-origin and COEP require-corp. Decoding the public pack
confirms the current doorway WAV. The collaborative preview was navigated
to the updated public URL.

The downloaded published page passes the complete two-browser WebGPU
fixture with `wss://relay.tide-engine.dev` in 65.2 s, including partner
off-screen movement with exact stationary local framing, smooth maker/camera
doorway travel, occupied-shell context, recursion, shared commands, progression
and phone touch. No runtime/browser/graphics errors or CI dispatch occurred.
Physical phones, cross-machine NAT and human latency remain unmeasured.

## Nearby outside maker and goal icons, 2026-10-11

The owner asked to see part of a maker standing beside the current room box,
and for goal tiles to show the occupant they require. Inner framing now uses
an eleven-cell span, leaving three interior-grid tiles of actual containing
world around the five-cell room. The fixed viewport and camera remain
independent of partner movement. Parcel targets show a box icon, maker goals
show a player icon, and switches show both because either can hold a latch
open. Existing occupied colours remain. Guide, lesson and hint text follows
the icons. No simulation rules, puzzle layouts, witnesses or audio changed.

Native debug and web debug/release builds are warning-free. All twenty-one
fixtures pass natively and under WASI, including both twelve-puzzle campaign
replays. The two-browser checks pass, WebGL in 86.90 s and WebGPU in 63.06 s.
The partner regression observes a clipped nearby maker at the fixed scene
edge, zero maker pixels farther away, and a visible maker after entry. Four
nonblank scenery patches remain byte-identical through all ten accepted
partner moves. Existing progressive maker passage, camera zoom, canonical
parity, occupied-shell context, recursion, commands, progression, shelf and
phone-touch checks remain green. A final stale capstone hint was corrected;
native/web targets and all twenty-one fixtures passed again after that text
change.

Bounded native captures at 1280 by 800 and 390 by 844 show the nearby maker
partly clipped at the bottom edge, readable complete rooms and literal goal
icons. Three scenery regions are byte-identical through the settled near,
far, return, inside and outside states; the full focused scenes at near,
return and outside are identical. The refreshed thumbnail comes directly
from `wide-capstone-desktop-1.png`. Captures and pixel evidence are under
ignored `build/inside-out-captures/`.

Site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. All 64 public
game/example pages and all four prototype thumbnails match release, staged
and packaged bytes. Inside Out HTML SHA-256 is
`56defc3b4b91e9aa97082f679382707ef9adb76b779178dc3b858886d8c82f50`;
thumbnail SHA-256 is
`a41a9c7066f5810fa8dcf89ea87c9b6974716e83d47fb771c3dbeec0ec9344cc`.
Decoding the inline pack confirms the exact current doorway WAV and absence
of the previous WAV bytes. No CI was dispatched.

The clean merge through `origin/dev` `27ddca74` brought shared compiler,
asset-list/session/delta runtime changes and current Underfoot updates.
Affected native and web debug/release targets were rebuilt without warnings;
all twenty-one native/WASI fixtures pass again. Final two-browser checks pass,
WebGL in 93.56 s and WebGPU in 63.20 s. All 64 public playable pages were
rebuilt/restaged and match their packaged copies; all four thumbnails match,
including the incoming Underfoot image. Docs check remains clean, all 227
grammar checks pass, and the final production build and five HTTP/SEO/access
checks pass. Integrated Inside Out HTML SHA-256 is
`c10808dbe0d78fcf976aca136860da2bc6dbe82eacb4295a58495d9f551d0d5f`;
the current thumbnail and doorway WAV hashes above are unchanged. No CI was
dispatched.

Source `0fa243a2` was pushed to `dev` and deployed directly to the existing
Fly site as release 260, image `deployment-01M4P75RDBACPVV6P25S6HRV16`.
Deployment/machine health checks pass. Anonymous Inside Out page and thumbnail
requests return 200 with the exact integrated hashes, retaining noindex,
COOP same-origin and COEP require-corp. The public inline pack contains the
exact current doorway WAV and none of the previous WAV bytes. The collaborative
preview navigates to the updated URL; snapshot inspection still fails inside
the preview client.

The downloaded published bytes pass the complete two-player WebGPU fixture
with `wss://relay.tide-engine.dev` in 62.4 s. Nearby partner clipping and distant
off-screen movement preserve exact local framing; smooth maker/camera travel,
canonical interior parity, moved-shell context, recursion, admission, shared
commands, progression, shelf and phone touch pass without runtime/browser/
graphics errors. The probe uses a controlled frame clock. Physical phones,
cross-machine NAT and human latency remain unmeasured. No CI was dispatched.
