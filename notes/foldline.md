# FOLDLINE prototype

Added on 2026-10-12 at the owner's request: an original 3D, first-person,
Portal 2 cooperative puzzle inspiration with freedom to add a twist and an
emphasis on appearance. The owner explicitly required portals to render the
space inside them. Other repository games' source was not used as a reference;
implementation follows Tide's public documentation and subsystem contracts.

## Box3D rooms and shot folds (2026-10-13)

The owner, after playing the first chambers ("currently it makes no sense"):
Box3D for physics "so we can launch the box", folds shot onto any wall that
fail visibly where they can't go, as Portal's do, and resizable folds
("dope lets try it"). The decisions put to the owner were left to Claude's
recommendations: fixed sizes that apply to operators and the cube, walls that
refuse a fold, floors and ceilings after the chambers, and a fold that can't
be moved while something is in its mouth (the owner's own first option, over
cutting or killing what's in it). Order agreed: (1) this section, (2) sizes,
(3) five or six real chambers, (4) floor and ceiling folds. (1) to (3) are
built; (4) is not. Where Match and controls below differs, this section is
current.

- **The room is Box3D's.** `Wall` is a box of the room (center, size, the
  renderer's paint, whether it takes folds, a kind for planters, ceilings and
  crates), and its colliders are `Piece` entities with a static `Body` and
  `Box`. Operators are dynamic capsules (0.26 m radius, 1.8 m high, friction
  0, kept upright by writing their rotation each tick) moved by writing
  `Body.velocity` before the step; `Operator.position` is the feet, copied
  after it. The cube is a dynamic box of 48 kg. The gate and the third
  chamber's bridge are kinematic bodies moved by position. `Arena.chamber`
  counts chambers; anything of an older one destroys itself.
- **A shot** (`Shoot`) is a raycast from the eye that passes operators and the
  cube. It needs an upright face of a wall that `takes`, at least 0.6 m
  thick. The opening is nudged to fit: inside the face, above the floor and
  under the ceiling found by rays from a point 0.2 m off the wall, and clear
  of what stands within 3 m either side at that height; within 0.8 m of the
  floor it stands on it. It fizzles (`Fold.fizzle*`, drawn as shards) on
  anything else, where it would overlap the other end, where a box 0.3 m
  deep before the opening overlaps a piece of the room, and while
  `Fold.busy` says something was in a mouth within three ticks.
- **A hole is real, and only while both ends are open.** `Carve` rebuilds a
  wall's pieces when `Wall.version` changes: the face is cut into a grid by
  the openings' edges, and a cell inside one is only what the hole leaves at
  the back (the hole is `min(thickness - 0.1, 0.95 * size)` deep). `Uncarve`
  destroys pieces of an older version. So a body walks into the wall, its
  middle crosses the face, and `CrossOperators`/`CrossCube` move it after the
  step (`Traverse`), with its speed turned; a cube's rotation and spin turn
  too. Nothing filters collisions: Box3D's package has no filter.
- **Carrying:** the cube is pulled to a point before its holder by its
  velocity, with no gravity, so it collides; through a fold when the mapped
  point is nearer it. E lets go, F throws at 11 m/s; out of reach (4 m) it
  drops. `Portal.size` and `Operator.scale` are threaded through the mapping
  and are always 1 until step (2).
- **The renderer** takes its boxes from the match's walls (`DressWall`, again
  when the chamber or their count changes) and the folds from `Fold.amber`
  and `Fold.cyan`; one end alone is drawn as a dark sheet on the wall.
- **Tests:** `test.c` was rewritten: standing and walking on Box3D's floor, a
  shot that fizzles on the ceiling, one that places and is nudged to the
  floor, one end alone staying solid, the busy-mouth and overlap refusals,
  walking through and coming out turned with speed and never behind a face,
  a thrown cube through a fold, carrying and throwing, chambers and the
  command, replay and pack. `render_test.c` sets the two ends directly. 2/2
  native and 3/3 web pass; the replay hash is `3005331974280731993` natively
  and under WASI. The full suites were not rerun (no engine or package file
  changed). Not checked: two players through one fold at once, the cube
  wedged in a hole when a fold is moved (its wall closes on it), cost on a
  slow machine.
- **The room's floor runs on under its walls,** so inside a hole it lay over
  the sill of what's seen through the fold, and hid the foot of a cube
  crossing it (the owner's screenshot). `InFold` now discards the room's
  boxes anywhere in the opening up to 0.9 m past the face.
- **After the owner's first play of it:** the cube is picked up through a
  fold when it's within reach that way (`Reach`); with the cube in hand a
  click throws it and shoots nothing (F still throws, E drops); and plates
  count only while weighted ("whats the point of the box if we can just
  stand on them"): `Arena.solved` is the circuit as it is now, the gate shuts
  at 2.4 a second when a plate is let up, and both operators in the lobby
  complete the chamber however they got there. Nothing latches any more.
- A shot that places an end calls `fold.Snap()`: views blended the end's
  place from where it was, and the owner saw it slide. P (`Control.crisp`,
  this machine's) shows the picture at the window's own rows, without the
  300-row pixels or the corner snapping, for the owner to compare.
- **Sizes (step 2, built the same day).** An end is shot at half, whole or
  one and a half times the usual size (`FoldSize`; 1, 2, 3 or the wheel pick
  it on this machine, `Control.size`, sent as `PilotInput.size`, 0 for
  whole). Double doesn't fit under a 4.4 m ceiling, which is why the largest
  is 1.5. `FoldPoint` scales by the ends' ratio, speed with it, and what
  comes out is that many times its size, kept from 0.5 to 1.5. A Box3D shape
  is made once, so a body that changes size is destroyed and spawned again
  with what it was (`Operator.resize` in `CrossOperators`, which sets
  `Arena.op0`/`op1`; `CrossCube` sets `Arena.cube`); a new chamber makes
  operators whole again the same way. Speed, jump, eye height, reach and the
  hand's place follow `Operator.scale`. Nothing but the hole stops a body
  that's too big for an end: a whole operator doesn't fit a half one. A low
  shot stands on the floor at any size. The renderer takes each end's size
  (`Sight.sizes`, `Aperture.size` and `ratio`) for the opening, the hole,
  the box behind it and what's part of the way through. `test.c`'s `sizes`
  walks in whole and out half, back to whole, is refused at its own size,
  and is made whole by a new chamber. Replay hash `14983226645209546878`.
- Inside a hole the room's boxes are cut behind the opening's whole
  rectangle, its rounded corners too: the floor showed there as two patches
  either side of the sill.
- **Five chambers (step 3, built the same day),** each for one thing the
  folds do, in the order a player meets them. All are one hall, 19.6 m by
  26 m, with the gate (6 m wide) in the north wall at z 13.5 and the lobby
  past z 14.2; one plate each (`PlateCount`, `PlatePosition`; `Arena.plateB`
  is kept for a second), the cube and both operators from `CubeStart` and
  `OperatorStart`, the watcher's two corners from `Pillar`. `BuildChamber`
  spawns the walls; the old bridge is gone.
  1. *A shortcut through space:* a pit across the hall (z -3.5 to 1.5) with
     the plate past it. Fold the south wall to one past the pit and carry
     the cube through.
  2. *Through the window:* the plate is in a cage on the west wall with only
     a slit to see through. A fold goes where the shot lands, so one is shot
     through the slit onto the wall inside.
  3. *Smaller:* the plate is in a room whose only way in is a vent 1 m
     square behind a baffle. An operator who walks out of a half end fits
     it; the cube follows the same way.
  4. *Larger:* a pit (z -1.5 to 3.5) that a whole operator can't jump, with
     concrete on the walls beside and past it. An operator out of a large
     end jumps it, and the slab past the pit takes a fold on its far side
     only, for the cube and the other operator.
  5. *Both:* the pit of 4, and the cube shut in a room behind a vent.
  `test.c`'s `chambers_hold` holds what each relies on: the vent refuses a
  whole operator and passes a half one round the baffle into the room; the
  pit of 4 isn't jumped at size 1 and is at 1.5; its concrete refuses a
  shot and the slab's far side takes one; a shot through the cage's slit
  lands on the wall inside. No test plays a chamber from start to gate, and
  nobody but these tests has played 3 to 5. 2/2 native and 3/3 web; replay
  hash `4283102527126451263` natively and under WASI.
- The number by the gate is drawn in segments 0.12 m thick: at 0.08 the
  corner snapping lost half of them from across the hall.
- **After the owner's play of the five (2026-10-13).** Open, the owner's
  to settle: every chamber can be solved by one operator, the other only
  has to walk through the gate (the owner's observation, and true); and the
  sizes "felt annoying having to find the right size for it and juggle back
  and forth more than it did satisfying", so the mechanic is to be rethought
  or polished. Both are answered by "Co-op and the wall's sizes" below.
- **The pit takes whoever falls in** (the owner: death "more brutal and
  gore... hands from hell grabbing us down", saws later). An operator whose
  middle is 1.5 m under the floor is `taken`: stopped for 0.35 s, dragged
  down at 1.3 m/s whatever it does, the cube let go, and back at the start
  after `TAKEN_FOR` (1.9 s). `Operator.taken` is match state. The renderer
  draws eleven arms of boxes up round the body, one to its face (`Taken`,
  `Arm`), shakes the eye, reddens the picture (`Lens.scare`) and closes it
  to black (`Lens.under`); 22 more arms sway far down in a pit all the
  time. The cube still falls through to -8 and comes back. No sound, no
  blood, and the partner's view of it was not captured. `chambers_hold`
  checks the catch, the hold and the return. Replay hash
  `5183574008336881480` natively and under WASI, 2/2 and 3/3.
- **The pit is full of red** (the owner: "the floor should be like a red
  liquid with hands poking out"): a slab whose top is `PIT_SURFACE`, 0.8 m
  under the floor, with the 22 arms out of it to the wrist or elbow. A body
  is caught chin deep (`TAKEN_UNDER` is -1.05 for its middle), and under the
  surface the picture is red and half dark at once. The red is a box with a
  flat top: it doesn't move but for a 2 cm swell, and nothing splashes.
- **Co-op and the wall's sizes (2026-10-13, the owner: "go on the co-op and
  wall-owned-size recommendations").** These replace what the five chambers
  above say of plates, sizes and the second chamber.
  - *Two bodies:* every gate takes two plates weighted at once
    (`PlateCount` is 2) and there's one cube, so someone stands on the
    other. Past the gate the lobby has a plate of its own (`HOLD_PLATE`,
    `Arena.hold`): the one who went through stands on it and the gate stays
    up for the one who held it open. The lobby's walls take no fold. Alone,
    Tab still does it, with the other body left on a plate.
  - *Sizes are the wall's:* the gun shoots whole ends only. A chamber has
    frames painted on its walls (`Mount`, `MountCount`, `ChamberMount`), and
    a shot that lands in one or within 0.35 m of it goes into it at its
    size, 0.5 or 1.5. `PilotInput.size`, `Control.size`, the 1 to 3 keys,
    the wheel and the ovals under the reticle are gone. The owner had found
    picking sizes "annoying... more than it did satisfying".
  - *Chamber 2 is "Apart":* a grille (`Wall.kind` 4: one solid box to the
    match, bars to the eye) shuts operator A and the cube in the south-west.
    A shot from either side fizzles on it, so each shoots one end. The cage
    and its window are gone.
  - Chamber 3 has a small frame on the east wall by the vent room, with
    plate B inside the room; 4 a large frame on the south wall; 5 a small
    one on the west wall by the cube's room and a large one on the south.
  - `test.c`: one plate alone opens nothing, two do, only while both are
    weighted, and the lobby's plate keeps the gate up; a shot by a frame
    takes its size and place and one clear of it is whole; the grille
    refuses shots from both sides and nobody walks through it, and with an
    end each the one shut in walks out; the last chamber's two frames take.
    Not tested or played: any chamber from start to gate, a large operator
    carrying the cube over the pit, a half one bringing the cube out of the
    room in 5. Replay hash `17728616113333077166`.
- **After the owner's play of that (2026-10-13).**
  - *The red has a surface* ("better texture", and under it "see under
    water... instead of it being a clear plane"): the slab is a sheet 4 cm
    thick of `Fixture.kind` -1, which `Surface` paints from three octaves of
    `Math.Simplex` that drift, with clots, and the ceiling's lamps mirrored
    in it by a normal made from the same noise. Its underside is a dim red
    roof. With the eye under it (`Lens.sunk`) `Develop` bends the picture
    with two sines and lays red murk with drifting cloud over it.
  - *The first link stuttered.* Every way a fold is drawn was first drawn
    when the first pair linked. `Render` now copies the match's `Fold` and,
    for its first three frames (`Optics.warmed`, behind the menu), draws
    two made-up ends and then one, from a camera that sees both. This is
    the likely cause and it wasn't measured: there's no frame timing in a
    scripted run, and nobody has played it since. `render_test.c` sets
    `warmed` past it.
  - *A fizzle's shards slid* from the last fizzle's place when clicks came
    fast: every shot snaps `Fold` now, placed or not.
  - *The owner asked whether Portal 2 shares folds as this does.* It
    doesn't: each player there has a pair of their own. Shared stays for
    now (Claude's pick, the owner's to overrule): "Apart" rests on it, and
    two pairs would double what the fold cameras draw.
  - *The interface was redrawn* ("rework the UI") to go with the picture:
    square, bone on near-black with one red, every letter with a hard
    shadow a big pixel off (`Ink`, `Dot`), a list in place of buttons
    (`InterfaceChoice`), square lamps for plates A, B and the lobby's, and
    a reticle of five squares. It's still drawn at the window's own
    resolution over the picture, not in its big pixels.
  - *Sound* ("add some sounds too"): `sound.tide`, one view, `Sounds`,
    after `Render`, which hears what changed in the match since the frame
    before (`Ears`) and plays it: steps by distance walked, a fold's end
    placed, a fizzle, a crossing, plates, the cube taken up, thrown and
    stopped, the hands, the watcher's lamps and its lunge; and loops for
    the lamps' hum, the building, the gate while it moves, the pit, both
    ends of a linked fold, and the red over one's head. All 23 clips in
    `art/sfx/` are made by `art/make_sfx.py` (numpy, 22,050 Hz mono), which
    is kept to make them again. **Nobody has heard any of it:** Claude's
    runs open no audio device. Levels, pitches and whether any of it is
    good are unchecked.
- **A fold each (2026-10-13).** The owner, of the shared pair: "i dont want
  shared portals, i feel like its limiting and the coop becomes a fight of
  power and taking turns instead of cooperating". This replaces what's said
  above of `Fold.amber` and `Fold.cyan`, of one `busy`, and of "Apart".
  - `Fold.ends` is four `Portal`s: 0 and 1 are operator A's (amber, cyan),
    2 and 3 operator B's (rose, violet), and an end's other is `end ^ 1`.
    An operator's two buttons shoot their own two ends and nobody else's;
    anyone and the cube go through any fold that's whole. `Linked(fold,
    pair)`, `Traverse` (which says the end crossed), `Reach`, `AtMouth`,
    `Carve` and the hold all go over four ends; `Fold.busy` is one count a
    fold, so someone in A's fold doesn't stop B's shots; an end clears
    every other end, whoever's. In C the ends are `world.Fold.ends.at[i]`.
  - The renderer draws four ends: a shadow map a lit end (`Daylight.fold0`
    to `fold3`), a camera an end that's on screen (`Optics.first0` to
    `first3`, now one sample a pixel where two ends had four), and its
    second crossing (`deep0` to `deep3`). `Sight` carries all four
    (`foldAt`, `foldFace`, `foldSize`, `foldOn`) for the holes, the light
    and what's part of the way through one (`Sight.fold` is the end plus
    one). **Through a fold's camera the other fold's ends are light on a
    sheet,** not a view: only a fold's own ends are followed twice.
    Not measured: what four cameras cost on a slow machine or in a browser.
  - "Apart" can't rest on sharing any more. A shot now goes between the
    grille's bars (`Shoot` passes a `Wall.kind` 4), so the one shut in
    shoots an end out and walks out. It's a small puzzle, alone solvable,
    and to be replaced with the rest when the chambers are made harder.
  - A crossing's sound was half a second, and played at both ends for
    each thing that crossed (the owner: "sucks... make it something
    simpler shorter"): it's 0.13 s of soft air, once however many crossed
    together, a quarter of a second apart at least.
  - `test.c`: each operator's shots move their own ends only, one end of
    each fold links nothing, and both walk out by the fold of the one
    shut in. Replay hash `8964366398197912681` natively and under WASI.
  - **Asked for and not built yet:** harder chambers, walking from one to
    the next in place of a button, and a place between them with no
    device and things to keep away from. Proposed to the owner; see the
    conversation of this date.
- **After the owner's play of a fold each (2026-10-13).**
  - *One fold seen through the other* ("we should render all portals
    ideally"): an end's camera now follows each end of the other fold it
    can see (`crossSeen`, `Optics.cross0` to `cross7` at half size, drawn
    before everything else), where it drew light on a sheet. So what's
    followed is: the room, through an end, through one more end of either
    fold. Past that an end is a sheet, and a look into the other fold has
    no lit air of its own (`Aperture.end` -1). Seen in a capture (a rose
    end with its picture inside a cyan one); eight more cameras at worst,
    and their cost isn't measured.
  - *Under the red there's fog* ("i can see a long while through it"):
    `HowFar` keeps each pixel's distance (`Lens.reach`) before the hand's
    device clears the depth, and `Develop` thickens the murk by it, to
    nothing seen past a few meters. The device in the hand is fogged as
    what's behind it is.
  - *What goes into the red splashes:* the renderer sees the cube or an
    operator go down through `PIT_SURFACE` (`Enter`), and draws sixteen
    drops thrown up and a ring running out (`Splashes`, six at once), and
    `Sounds` plays `splash.wav` for each (`Optics.splashed`). This is the
    renderer's and not the match's: a machine that didn't draw the frame
    it happened in sees and hears nothing of it.
- P now goes round three pictures for the owner to compare: 300 rows, 480
  (`SOFT_LINES`) and the window's own (`Control.crisp` 0, 1, 2).
- **Known gaps:** a shot blocked by a planter fizzles where Portal would
  nudge sideways; a lone end gives no light; the gate closing on a body
  under it, two operators through one fold at once and the cube in a hole
  when its fold is moved are unchecked.

## Match and controls

`prototypes/foldline/play.tide` contains a deterministic 60 Hz, two-player match.
Two `Operator` entities always exist, with `Owner` deciding whose input each
reads. The first player owns both until the second joins; solo input selects
one operator and the other remains in place. Leaving restores both bodies to
the remaining player. Built-in rooms/session operations supply online co-op
and host migration; there is no game-specific network or runtime change.

`Arena` stores chamber, pressure circuits, latched solution, gate/bridge and
completion state. `Fold` stores the shared pair of panel indices, loose cube,
carry state and partner ping. Match inputs contain movement, look and actions;
menu selection uses a bounded `SelectChamber` command with sender checks.

A fold's opening is 1.36 m wide and 2.6 m high and stands on the floor (the
owner, 2026-10-13: nearer Portal's, more upright; it was 1.8 by 2.8, which no
puzzle needed). `PortalAperture` lets a body through where its middle is
within 0.68 m less its radius of the panel's middle sideways, and within
1.3 m less its radius of a point 0.2 m under it. The replay hash is unchanged.

Portals are restricted to six vertical receptive panels. Mapping resolves a
point or vector in the entry panel's right/up/normal basis, turns right/normal
by 180 degrees and resolves it into the exit basis. Traversal maps position,
horizontal facing and velocity together. Destination collision checks use
destination coordinates. Short re-entry cooldowns restore solid-wall collision
until traversal is available again. Operators and the cube use bounded wall,
floor, divider and pit collision rather than a general rigid-body library.

The three experiments use one architectural layout with changed circuit
positions and rules: weight A, weight A and B together, then leave the cube
on A to power a bridge while an operator reaches B. Solutions latch. Both
operators must enter the exit to complete. Falling resets the fallen object.
The resonance bridge is the prototype's extension of the shared portal puzzle.

`ui.tide` owns menus, room sharing/joining, chamber choice, solo selection,
pointer lock, a minimal aiming reticle and circuit/objective displays. Menus
gate local input without pausing the match. Completion releases pointer lock
so the next-experiment control can be clicked. The README describes the player
controls and build commands. Gameplay supports keyboard/mouse and gamepad;
touchscreen movement is not implemented.

## The look: a PSX backrooms (2026-10-13)

The owner asked to try models and a horror PSX style, chose a liminal
backrooms theme from the options offered, and said of the first pass "i like
this style" and that its darkness is right for the puzzles. Horror is
atmosphere and "sprinkles": puzzles leave time to think, and spooks come
between and around them. Everything below this heading and above Rendering
describes that pass; the Rendering section is the same pipeline's earlier
description, kept for the parts that still hold (the fold cameras, the hole,
the copies at a fold's other end, the lamps' occlusion, the air pass, the
light kept as light until the last blit). Where the two differ, this one is
current: there's no roof glass, sky, sun, rounded edges, floor reflection,
wall ribbons or robots any more.

**Art** (`prototypes/foldline/art/`, credited in its `CREDITS.md`, all CC0):
Kenney's Animated Characters Retro gives the operators' model
(`character.fbx`), three clip files and the skins. Each clip file holds a
one-frame "Targeting Pose" before its clip, so the catalog's odd entries are
Idle, Run and Jump (`FigurePose`). `kit.png` is a sheet of sixteen 64-texel
squares: twelve textures of Kenney's Retro Urban Kit and four generated for
the game (wallpaper, carpet, ceiling tile, plaster). The kit's models aren't
used: each has several textures, which a mesh import doesn't carry, so doors
and crates are the game's own boxes painted from the sheet (`Clad` by place in
the room, `Faced` by a box's faces).

**The picture** (`Develop`, `Snap`): 300 rows whatever the window's are, each
taking one pixel of the full-size scene; corners snapped to that grid in the
vertex stage (`Snapped`, also in `Sway`); textures read at their nearest
texel; 32 steps a channel with a four by four ordered dither; a yellow-green
grade, grain and a heavy vignette. Rendering really at 300 rows is not built:
the scene targets are still the window's size.

**Light:** the ceiling at 4.4 m has a lamp every 5 m across and 3.25 m along
(`LampCell`, `LampOn`): about two in nine are dead and one in nine stutters.
`Lamps` sums the nine nearest for a surface, each a tight downward beam plus a
quarter as much all around, so floors get pools and walls an arc that's dark
towards the ceiling. This replaced a first version (rectangular pools and an
offset wash up the walls) the owner found "a bit weird with the walls/floor".
The one shadow map looks straight down, so shadows fall under things, and only
on what faces up. `LampWide` adds a little bounce to each bay, `LampAir` is
the cone in the haze the air pass marches, and each operator carries a torch
(`Torch`), unshadowed, which the partner sees. The exit's light is its sign's
green.

**The watcher** (`Watcher` and `Haunt` in `play.tide`; the owner's: something
peeking round a corner that goes back behind it when approached or stared at,
sometimes a jumpscare instead, and rare, "odd, not some companion that annoys
you"): match state, so both players see the same one. The first can come 100
seconds into a chamber and the next 150 to 390 seconds after one left. It
picks one of four pillars nobody is within 5.5 m of, leans out of one side of
it by 0.6 m, and keeps the pillar between itself and the nearest operator as
they move. It goes back behind the pillar, and is gone by the time anyone gets
round, when an operator comes within 5 m, after 2.2 s of being looked at
within 18 m, or after 25 s. The ceiling lamps go out for 0.16 s as it comes
(`Sight.lamps`). One in five is bold: it stays, and at 2.7 m goes for that
operator, in their face for 0.75 s; that player's picture jolts and reddens
(`Lens.scare`). Nothing else happens to them. It's the zombie skin on the
operators' model. No sound is built. An earlier version stood in the open
every 25 to 60 seconds and vanished in a blackout; the owner asked for this.

**Obstacles:** the planters, the crates, the locker and the four pillars are
`Obstacle` boxes in `play.tide` that `Confine` pushes operators and the cube
out of (`Around`), and `Dress` draws the planters and pillars from the same
list. Until then nothing stopped a body walking into a planter, and the camera
inside one read as Z-fighting to the owner.

**Crossing a fold** (2026-10-13, after the owner's play: the camera jumped,
a player was cut in half, the cube stopped drawing near a fold and the floor
had a gap at the sill; "this is the main mechanic it needs to be perfect"):

- `Traverse` crosses when a middle goes from before a wall's face to past it,
  and comes out exactly as far past the other face, with no 0.05 m push and
  no cooldown. The cooldown had made the destination wall solid for 0.18 s,
  which threw the body 0.26 m out of it: the jump. `portalCooldown` and
  `cubeCooldown` are still fields and are unused.
- A carried cube whose place is past a fold's face is at the fold's other end
  (`Fold.cubeThrough`), not inside the wall, where it could be dropped and
  never reached. Alone, the cube can be taken from the body left holding it.
- What's part of the way through is decided for each thing, not each pixel
  (`Crossing`: the cube, each operator): it draws stopped at that wall's face
  (`Sight.fold`) and again at the other end, stopped at that face. The old
  rule cut anything within a box behind either wall, the other room's side of
  the divider included, and only copied pieces within 1.4 m.
- The part at the other end is lit as the part left behind is: `Lit` takes
  its place and normal back through the fold (`Sight.echoed`, `Sight.back`),
  and a fold's own glow is at 0.3 on what's passing through it. Lit where
  each half stood, a body had a seam of light at the wall's face that the
  owner read as the cut still being there. The light changes at once when
  the body's middle crosses.
- A fold's camera discards what's within 2.5 cm before the wall it looks out
  of, so that the wall's own face never shows. For what's part of the way
  through that fold the margin is none: with it, a 2.5 cm slice of a crossing
  body was missing at the wall's face, the gap the owner kept seeing. The
  light seam above was fixed first, on a wrong reading of that picture.
- What's seen through a fold is drawn on the inside of a box behind the hole,
  a back and four sides, so a look down at the sill lands on it.
- The near plane is 0.07 m and the far 90 m (0.035 and 120): lamp frames,
  lintels and jambs a few millimeters from the surfaces behind them flickered,
  and were also moved clear of them.

The replay hash is `2788051018070455625`, natively and under WASI. The
crossing test in `test.c` now checks that a body comes out under 0.06 m past
the destination's face and that turning back never leaves it behind a wall.

Not built: vents, chases, timed puzzles, sound, a HUD and menu in the style.

**Stats:** every catalog game is built with `STATS` since 2026-10-13 (the
owner: prototypes show their frame rate), so the drawer's line is in the
corner of each page and of captures.

## Rendering

`render.tide` owns local geometry, materials, light and render targets. The
scene is original procedural architecture: a stone atrium under a glass roof,
dark machinery, warm and cool luminous accents and planting. The owner asked
on 2026-10-13 for the best look achievable with free rein; the renderer was
rewritten that day. Its choices are Claude's and unconfirmed by the owner.

Portal views use mapped cameras and render the real scene into separate local
targets. The portal surface samples the camera image by screen coordinates,
so the window into the destination has the same projection as the viewer.
Scene fragments clip against the destination portal plane. The renderer uses
a bounded second portal level, drawn from deepest to nearest into distinct
targets, rather than reading a destination target while writing it. Local GPU
resources are outside match state and never affect deterministic simulation.

Light stays light until the last blit:

- The room draws into a 4-sample `Rgba16f` window target. The air pass reads
  that target's depth, which the engine resolves since 2026-10-13
  (`notes/render-targets.md`); until then a second float image held distances.
- First-level portal images are window-sized 4-sample `Rgba8`, second-level
  ones half-size. They hold light folded as `sqrt(x / (1 + x))`; `Through`
  unfolds it, so glow seen through a fold still blooms. `Sight.packed` and
  `Aperture.packed` say which form a pass writes.
- `Daylight` is a 2,048 px depth-only sun map over the whole atrium, redrawn
  each frame from every list except the roof glass, the handheld and dust.
  `Sunlight` takes nine compared looks; `FromSun` steps off the surface along
  its normal. The viewer's own body casts a shadow though it isn't drawn.
- Each fold's lamp has a 768 px depth map of its own, seen from 0.55 m off
  its panel through a 140 degree camera (`FoldLight`), so a fold's light
  stops at the divider, the gate, the cube and the operators. What's beside
  or behind that camera is lit.
- The plates', the cube's, the low wall ribbons' and the exit's lamps are
  stopped by what moves, worked out in the frag with no map (`Unblocked`): the
  cube as one ball and each operator as three, each a soft test of the
  lamp-to-surface segment (`Past`). A ball the lamp is under or inside stops
  nothing, so a plate still glows around what weighs it. The low ribbons are
  also kept off the floor before each planter by where their ray passes the
  planter's front edge. Walls don't stop these lamps: the exit's is only
  applied past the divider, and the high warm ribbons have nothing between
  them and the walls they light.
- `Surface` shades everything solid: sun and shadow, a sky/floor ambient,
  analytic creases for the room's planes, contact shade under the cube and
  both operators, the two folds, pressed plates, the cube, the wall ribbons
  and the exit as unshadowed lamps, a sun highlight, and a reflection of the
  roof (`Mirror`) weighted by gloss and angle. `Fixture.size` gives each box
  rounded edges in the frag and keeps normals right under uneven scale.
  Panel seams widen to the pixel they're seen through and fade as they do.
- The roof glass (`kind` 2) draws `Sky`: a gradient, two octaves of cloud and
  the sun. Leaves (`kind` 1) are two-sided blades that let the sun through.
- `Shafts` marches 20 dithered steps through the sun map at half size, from
  the eye to the scene's depth (`Scatter`, `Unprojected`). The viewer's fold
  openings draw through `ThroughAir`, which marches the same way from the far
  opening to the fold camera's own depth (`Beyond`), so shafts continue
  through a fold. A fold seen by a fold's camera draws through `ThroughFar`,
  the same against the second crossing's depth; the two frags differ only in
  which depths they read, as a camera's image can't be read while it's drawn
  into. The last fold (`LastFold`) has none. `Bright`, `Halve` and
  `Blur` build three sizes of spill. `Develop` adds both, applies a film
  curve, a grade, a vignette, a slight fringe and grain, and writes the window.

A fold is a hole, not a picture on the wall (the owner, 2026-10-13: the
camera's near distance made a fold look solid before going through, and what
passed through wasn't shown at the other end):

- `Opening` is the opening's shape in meters on its panel: a rounded box,
  nearly an oval. The room's pieces (`Sight.cut`) are discarded inside it at
  the wall's face (`InFold`), so the wall and the panel have a real hole.
- The sheet a fold is seen on is set 0.3 m back into the wall and is 5 by
  4.4 m. Its frags find where the eye's ray crosses the wall's face (`OnWall`)
  and take the rim and the cut-off from there, so the picture is the same
  from any angle and the eye reaches the wall's face, and crosses it, with the
  sheet still filling the opening. The image is still read by screen position.
- What moves (`Sight.moving`: the cube, both operators, the plates and gate)
  is discarded past either fold's wall face (`PastFold`), and `Echoes` adds a
  copy of every such piece near a fold at the fold's other end, turned as the
  fold turns the world. A thing part of the way through is so whole from
  either side, lit where each part is. The viewer's own copies are kept in
  lists by fold and left out of the pass whose camera looks out of that end,
  which sits inside them. Copies cast shadows.
- The ducts and low ribbons along the side walls stop either side of each
  receptive panel; they ran across the openings there.

The match still moves a body across in one tick when its middle crosses the
wall's face and puts it 0.05 m out of the far wall, so the view steps by that
much at the crossing. `render_test.c`'s clipped-cube case now puts its cube
3.2 m behind the wall where it was 1.2 m: within 1.4 m it's part of the way
through a fold by this rule, and is drawn at the other end.

`OpeningOnScreen` gives each fold's pixel rectangle. A fold's camera is
skipped when its opening can't be seen and otherwise clipped to that
rectangle; a second-level camera runs only when the first can see an opening
again. The clip is set under `SetScreen` before the 3D camera.

Operators are balls and tubes with a visor that follows pitch and legs that
swing with speed. The handheld is drawn at its own size after the room, over
`scene.ClearDepth()` (`notes/drawing.md`), so it's in front of a wall the
operator stands against; `Sight.held` lights it as the eye is lit, since its
own place may be inside that wall. The air pass's blit is recorded before
that clear, so it reads the room's depth. Behind the menu the view is a
drifting camera of its own and the viewer's body is drawn. The aimed panel's
ribbon brightens, and a partner's ping is a 3D beacon, which nothing drew
before. Leaves stir through the `Sway` vert in every pass and every light's
map. Touchscreen controls are not built.

`ui.tide`'s HUD (2026-10-13) is panes of dark glass: the experiment with its
two circuit lamps and the gate's state, the operator, one line of what to do
and a row of key caps. The menu and the completion screen's layout are as
they were.

## Integration and verification

The opt-in `TIDE_PRIVATE_PROTOTYPES` option includes native `foldline` and
`foldline_test`; web `foldline_page` stages the self-contained page at
`docs/static/prototypes/foldline/index.html`. The private catalog and public
path guards agree on `foldline`. Its thumbnail is taken from actual gameplay.
The C test host is isolated under `test/tide.packages` so `tide run` does not
discover it as game C. No external Tide package or asset download is needed.

Native and web debug game targets build without warnings. Scoped CTest suites
pass 2/2 native and 3/3 web: simulation tests and a real GPU fixture on OpenGL,
WebGL 2 and WebGPU. Simulation tests cover all 30 directed panel pairings,
position/facing/velocity transforms, operators and loose/carried cubes crossing
the divider, cooldown collision, solo/co-op assignment and leaving, menu input
gating, movement/jump/aim/ping/carry/drop, chamber solutions, command sender and
range guards, pit recovery, snapshots, packing and invalid numeric input repair.
The full-game replay hash agrees on native/WASI: `347114137472726277`.

`test/render_test.c` draws eleven scene configurations without intervening
simulation ticks. It checks geometry inside the portal against a solid wall,
live cube/partner motion beyond the exit, camera-side geometry clipped by the
exit plane, and lateral camera parallax against independently projected cube
positions. All assertions passed with their original tolerances, with GPU
validation enabled in both browser fixtures. The test uses the real renderer
and targets, rather than a separate approximation of portal rendering.

Native standard-host scripts check the menu, movement and view changes, and a
two-process co-op run walks operator B through the portal while A watches it
appear on the other side. Repeated co-op captures match byte for byte; the
catalog thumbnail is that actual gameplay frame. Visual inspection corrected
the first-person camera initially drawing its own robot body, which obscured
the lower scene; self geometry now draws only in portal passes. The polished
captures show compact equipment, warmer stone, roof shadows and chamber signs.

The PSX pass, the watcher and `STATS` were validated by this game's suites
and the other catalog games' own, native and web: 6/6 native and 11/11 web debug (the
other games' browser play tests among them, with the drawer's line in their
corner). The full suites were not rerun.

The hole, the copies and the opening's size were validated by this game's
suites, 2/2 native and 3/3 web, with the same replay hash natively and under
WASI, and by frame-by-frame captures of walking through a fold and of a
partner crossing one.

The lamps' occlusion and `ThroughFar` (later on 2026-10-13) were validated by
this game's suites alone, 2/2 native and 3/3 web, and by captures of a
weighted plate, facing folds and the far room. Chamber three's planter row
over the machine bay is gone: it hung in the air there.

After the engine gained the multisampled depth read and `Draw.ClearDepth()`
the same day, the game was moved onto both and the full suites were run:
2,265/2,265 native debug (OpenGL) and 2,235/2,235 web debug (WASI, WebGL 2 and
WebGPU), with this game's render test unchanged. The web runs used Node
24.15.0, under the 24.16 `notes/testing.md` asks for on Windows. Android,
Linux, macOS, MinGW and sanitizers were not run. No CI was dispatched.

The 2026-10-13 renderer passes the same scoped suites with the render test
unchanged and at its original tolerances: 2/2 native (OpenGL) and 3/3 web
(WebGL 2 and WebGPU with validation on). Looks were checked from standard-host
captures: each chamber, the menu, facing folds with a second crossing, and a
two-process co-op run whose frame is the new thumbnail and repeats byte for
byte. Frame cost was not measured on a slow GPU or a browser; 1,200 windowless
1080p frames took about a second on this machine. The full native and web
suites were not rerun, as only this game's files changed.

Docs checks and production/public-route validation are recorded in
`notes/docs-site.md`. The collaborative preview's snapshot operation failed,
so visual inspection used standard-host captures and the automated browser GPU
fixtures. A live Internet room with two human players was not exercised;
co-op validation used the standard host's deterministic two-process transport.
Linux/macOS/MinGW, Android and sanitizers were not run. No CI was dispatched.
