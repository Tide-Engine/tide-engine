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
- **One way down (2026-10-13).** The owner: "make puzzles harder, i dont
  really feel challenged... make level transitions be walking vs a button...
  some backrooms like in-between level where here we can't use our guns and
  maybe we have monsters we need to evade. i know this is vague but im just
  trying to find the fun in this game which right now its lacking." Claude
  proposed, and the owner took ("go with your recommendation"): no level
  button; stretches between rooms where the device is dead; one thing in
  them that moves only while nobody sees it (over a chase to run from);
  caught there, both start that stretch over; and harder rooms made for two
  folds, the five cut to two that teach and three real ones. Built in that
  order, for the owner to play each step. **Steps 1 and 2 are built; the
  rooms are the old five still.**
  - *Lifts.* The lobby past the gate is a lift. With both in it the gate
    shuts whatever holds it, and after `RIDE` (1.3 s, `Arena.ride`) the next
    place is built (`Arena.depth`, `LevelAt`: room, between, room, room,
    between, room, room, between, and round again) and they're in the lift
    they came by: a box in the south-east, through a gap in the south wall
    at x 3.5 to 9.5, which is the lift they left by turned half about. An
    operator keeps its place and facing through that turn (`Arena.rode`,
    in `MoveOperators`), so nothing is seen to move but the lamps failing
    and the eye shaking. Its door (`Door`) opens 0.7 s after arriving.
    Everyone starts in it, in every room. `Arena.complete` is never set and
    the screen with its button is gone; the menu's six squares (the last is
    the stretch between) stay for getting about.
  - *Between* (`BETWEEN`, level 5): the hall cut into 8 by 10 cells by
    concrete walls (`MazeEast`, `MazeNorth`: one maze, with loops, made by a
    script once), a fifth of the lamps' light, no shot (nothing happens on
    a click), and the gate open.
  - *What's in it* (`Stalker`, `Stalk`): it's seen while it's within 60
    degrees of where an operator looks and no wall is between (a ray). Not
    seen, after four seconds' grace, it goes at 3.3 m/s to the nearest
    operator who isn't in a lift, by the cells (a breadth-first count from
    their cell each tick). Within 0.95 m it has them: `Arena.caught`, the
    picture reddens and goes out, and after `CAUGHT` the stretch is built
    again with both in the lift. It's drawn as the watcher is, in one
    frozen pose, through `Watcher.at`; `Haunt` does nothing there. It's
    heard only while it moves (`scrape.wav`).
  - `test.c`: both in the lift ride to the stretch and stand as they stood,
    turned; no shot there; it stays put while both are in the lift, comes
    for someone with their back to it without leaving the maze, catches
    them and starts the stretch over; it stands while looked at down an
    open way and moves when looked away from. Replay hash
    `8822463228466978914` natively and under WASI.
  - Not done or checked: the rooms; a second maze (every stretch is the
    same one); whether 3.3 m/s against a run of about 5 is frightening or
    only slow; it going through an operator's other body; the thing seen in
    a capture (the tests see it, Claude's captures didn't find it).
- **After the owner's play of the way down (2026-10-13):** "that was
  fun-ish". This corrects what "One way down" says of the lift's turn, the
  thing's speed and reach, and adds themes.
  - *The lift is walked through* ("weird that the transition looks like a
    teleport, the doors go away, and new ones open behind us, i was hoping
    for the double doors to open"). It has a door at each end. An operator
    is moved by `ARRIVAL - LIFT` and not turned, so the door it faced, the
    far one, is the one that opens on the next place, and the one it came
    in at is shut behind it (`Dress` draws a shutter on each lift's far
    wall, with a green lamp over the way on and a red one over the way
    back).
  - *What's between is worse to be near* ("wasn't really scary and i got
    close-ish to him without being punished at all"; the owner did like one
    staring while the other moves). It has anyone within 1.35 m, looked at
    or not; goes at 4.5 m/s; and every 6.5 s every lamp and both torches
    fail for 0.4 s (`Stalker.dark`), when nobody sees anything and it goes
    at 9. It's half as tall again as an operator and is drawn caught
    mid-stride, differently each time it has moved (`Stalker.walked`). A
    heart (`heart.wav`) is heard louder and quicker within 11 m. Whether
    this is frightening is the owner's to say: nobody but tests has met it.
  - *Themes* ("as we progress it would be fun changing the environment").
    `Arena.theme` is `ThemeAt(depth)`, another every third lift: 0 paper and
    carpet, 1 a pool (pale tiles, a cool cast, water in the pits where the
    blood was, and the same hands in it), 2 bare concrete. It's the
    renderer's remap of two paints (`ThemeCell`, `ThemeTint`), a cast over
    the picture (`Lens.grade`) and the liquid's color (`Liquid`); the rooms
    are the same rooms.
  - Asked for and not built: other things between the rooms than this one
    ("different monsters or challenges in between levels that are more
    horror driven"), and the harder rooms.
- **A place is its own, not a coat of paint (2026-10-13).** The owner, of
  the themes above: "these look more so like a re-skin; we should make
  props different, instead of the blood with hands make it something else;
  some other hazzard. why are we being so cheap about variation." What a
  place's pit is now goes with the place (`Hazard(level, theme)`), and so
  does what stands in it. This replaces "water in the pits... and the same
  hands in it" above.
  - *Paper and carpet (theme 0):* blood with no bottom and the hands, as
    it was; planters, doors to nowhere, boards.
  - *The pool (1):* a pool to wade. It has a floor 1.5 m down and a slope
    from each side (`Ramp`: two static boxes tilted about x, which the
    renderer draws from the same numbers), water to `POOL_SURFACE`, and
    wading is at 0.45 of a run. In it is `Lurker`: it goes up and down the
    pool, at 5.5 m/s for whoever is in the water, and before them for the
    last thing that splashed, for 7 s. So the cube thrown in is the time to
    cross in, and the cube is then on the pool's floor to be fetched.
    Within about 1.25 m of someone in the water it has them (`taken`, as
    the hands do: held under, back at the start). Drawn as a back of ten
    pieces breaking the water, its head up and eyes lit when it's after
    something. About it: loungers where the planters were (the same
    boxes), lane floats, rails down the slopes, rings on the walls.
  - *The concrete (2):* a trench 1 m deep with slopes, and two drums that
    go its length and back at 6 m/s, half a pass apart (`DrumAt`, a
    function of the tick). Anyone on the trench's floor within 0.95 m of
    one along it is `shredded`: in pieces (`Pieces`), nothing left
    standing, back at the start after `SHRED_FOR`. A drum throws the cube
    out of its way. About it: barriers with warning stripes, ducts under
    the roof, boxes on the walls, the trench's edges marked.
  - The rooms were made for a pit nobody crosses, and two of them now
    have one that can be crossed: room 4 ("Larger") is in the pool and
    room 5 in the concrete, by where they fall in `LevelAt`. Their frames
    and plates are as they were; they're easier, and differently solved,
    until the rooms are remade.
  - `test.c`: the pool waded dry-shod of the lurker while the cube draws
    it off, and not without; the trench crossed with both drums at their
    ends, someone in a drum's way shredded and returned, the cube thrown
    out. Replay hash `15445941946088048879` natively and under WASI. Sounds added and unheard: `drum.wav`,
    `shred.wav`.
  - Still one maze and one thing in it for every stretch between, whatever
    the place.
- **Six rooms, for a demo (2026-10-13).** The owner: "let's add more
  puzzles and make sure we increase difficulty it's a puzzle game after
  all, play with room layouts so they are not all the same. we need a demo
  worthy version of this." And the picture: "i decided the current
  pixelation is what I like so we can keep that and likely downscale the
  render scale to match". This replaces the five rooms above, "Apart" and
  the large frame's room with them, and `LevelAt`.
  - *The way down* is nine places and round again: rooms 1 and 2, a
    stretch between; 3 and 4, a stretch; 5 and 6, a stretch
    (`LevelAt(depth % 9)`, `BETWEEN` is 6). The first three are paper and
    carpet, the next three the pool, the last three concrete.
  - *What a room can be made of now:* a gallery (`GALLERY`, 1.6 m: past a
    jump, with room over it for a fold, which stands on it); plates at any
    height (`Weighs` goes by the plate's own); the side walls each in two,
    this side of z 3.5 and past it, so a room says which of six walls take
    a fold.
  - *1, A shortcut through space* (blood): fold across the pit. As it was.
  - *2, High ground:* a plate on each of two galleries. Shoot the wall
    over one from the floor.
  - *3, The lure* (pool): the cube is up on a gallery this side; nothing
    past the water takes a fold. Wade it, with the cube thrown in or
    someone at the edge to draw the thing off.
  - *4, Smaller:* both plates are in the room behind the vent. Only
    someone small gets in, and has to shoot an end of their own fold
    inside for the cube and the other to follow; then they're small until
    the other's fold has an end in the frame.
  - *5, The drums* (concrete): run the trench. One plate is on a gallery
    walled off from this side: the wall over it is seen only from past the
    drums, and the other end goes on the south wall.
  - *6, All of it:* the cube on a gallery this side, a small frame on the
    south wall, the trench, and past it a cage of bars round one plate
    with a way in a meter square. Someone small crosses the trench at half
    speed.
  - `test.c` holds what each rests on, not a room played through: the
    galleries past a jump and reached by a fold shot over them, the plate
    up there counting only who's up there, the walled-off wall refusing a
    shot from this side and taking one from past the trench, the cage
    stopping a whole operator and passing a half one, and the gate opening
    with them on its plate; the order of places and their themes. Replay
    hash `9879513412636022908` natively and under WASI. **Nobody has
    played a room from its lift to the next.** The large frame is in no
    room now, and nothing tests it.
  - *The picture* is 300 rows for good: P and `Control.crisp` are gone.
    The room is drawn at half the window's pixels (`RENDER`), an end's
    camera at half and its second crossing and looks into the other fold
    at a quarter. Not measured: what that saves.

- **Physics for more than one box, rooms that aren't the hall's shape, and
  a stretch to each place (2026-10-13).** The owner, of the six rooms:
  "the room layouts all seem the same rectangle box with just different
  stuff in it. we don't have to constrain ourselves to just that.. also we
  have a full physics system for just one box", then "use physics".
  - *Loose things.* A room has up to eight: the cube and crates (`Cube.kind`
    1: planks, half the cube's weight), each a `Cube` with its number
    (`index`), where it is (`at`, `turn`, which the renderer reads by a
    query) and who holds it. All are picked up (E takes the nearest within
    reach, straight or through a fold), carried through folds, thrown,
    stood on and stacked, weigh a plate, are thrown about by a drum, and
    draw the pool's thing when they go in. `Fold.holding[2]` is each
    operator's (a number and one), `Fold.spots[8]` where they all are;
    `Fold.cube` and `Arena.cube` are still the first's. One of them at a
    time is drawn again at a fold's other end while part of the way
    through (`cubeParts`); the rest are `cubeRest`.
  - *So a gallery is climbed from on top of the cube* (0.74 m and a jump
    clear 1.6), and two plates are held by two things with nobody standing
    on either. Both are tested. Rooms 2 to 6 are easier by it than they
    were written; that's left as what physics does, for the owner's play
    to judge.
  - *Shapes.* A room fills what it leaves of the hall from floor to roof
    (an L for room 2 and room 5, far corners for 3, a corner each for 4
    and 6) and hangs a beam under the roof in three. It's still one hall,
    19.6 m by 26, with the lifts where they were: the hazards, the lamps'
    grid and the maze are written to its coordinates. A room of another
    size or with its lifts elsewhere is not built.
  - *A stretch to each place.* `BETWEEN` (6) is the maze, in paper and
    carpet. `POOLWAY` (7), the pool's: 18 m of water 1.2 m deep with three
    bars of shallows across it (`ShallowAt`, marked by float lines), where
    the thing has nobody; crates on the bank to throw in. `GAUNTLET` (8),
    the concrete's: a trench 14 m across with four drums, a lane each, at
    four speeds (`DrumCount`, `DrumLane`, `DrumTurn`), and a strip between
    lanes none reaches. A pit's width is `PitHalf(level)` now. No fold in
    any of them; their gate is open.
  - `test.c`: a crate carried to a gallery's plate and the gate opening on
    two things; a gallery climbed off the cube; in the long pool nobody had
    in the shallows with the thing beside them, and had a step off; in the
    works nobody touched between lanes over fifteen seconds, and shredded
    in one. Replay hash `10234515152563608430` natively and under WASI.
    `render_test.c` places the cube by its component.
  - Unplayed, all of it. Not done: joints (nothing hinged or hung), things
    of other sizes or weights, and a crate half-way through a fold while
    another is (the second isn't drawn at the far end).
- **Out of the hall (2026-10-13).** The owner: "yes I want freeing us and
  allow corridors and other rooms and maybe even more verticality. moving
  platforms. we might also need to introduce more mechanics". This is the
  ground for it and one room built on it; the six rooms and three
  stretches are still the hall's.
  - *Lifts are where a place says* (`ArrivalAt(level)`, `LiftAt(level)`,
    the middle of each one's floor, at any height; `HoldPlate`). The gate,
    the door, who's in the lift, where riders stand on arriving
    (`Arena.left` is the lift they left by) and all the renderer draws of a
    lift go by them. A lift's way in is still on its north side and its way
    out on its south: lifts aren't turned.
  - *A room builds itself* where it isn't the hall's: `Fill` (a box by its
    corners), `LiftShell`, and `BuildChamber` hands over to it
    (`BuildShaft`). Nothing in a room so built is a pit of a place's kind
    (`Hazard` is the hall's band), has a watcher, or a maze.
  - *Light isn't the hall's:* the lamps are a grid without end, a storey at
    a time (`STOREY` 5 m: `LampPlane(y)` is 4.4 over the floor of the storey
    a place is in), and their fittings hang where a room says a roof or a
    taller room's rods are (`Fitted`). The floor's and walls' darkening
    towards the hall's own corners is gone, in the hall too. The shadow map
    is 56 m long. A floor that has a room under it is a roof to the light
    (`Wall.kind` 2), so what's on an upper storey can shadow the lower.
  - *What a room moves* (`Mover`, `MoverCount`/`From`/`To`/`Size`/`Plate`):
    a kinematic box moved by its speed, so what's on it is carried. One
    with no plate goes between its two places by itself, waiting 1.3 s at
    each (`MoverAt`, a function of the tick); one with a plate goes to its
    second place while that's weighted and back when it's let up, at
    2.2 m/s. An operator standing on one is given its speed
    (`MoveOperators`, by what's under its feet). A room has up to four
    plates: A and B are its gate's, C and D its machinery's
    (`Arena.plateC`, `plateD`), drawn green.
  - *A slope* is a `Ramp` with its own place, size, tilt and paint, drawn
    from a query; a pit's are still the pit's.
  - *The shaft* (level 9, tenth down, "room 07"): a passage from the lift
    into a room two storeys high, papered to the balcony's height (takes a
    fold) and bare above it (doesn't); a deck that rises to the balcony
    while the green plate is weighted; a passage off the balcony; a room
    whose floor is 2.5 m down between two ledges, with a slope back up,
    crossed on two decks that go from side to side by themselves; and the
    lift out on the upper storey. The menu behind it looks down the far
    room. It's a first room on the new ground, to show it stands, more
    than it's a hard puzzle.
  - `test.c` (`shaft`): arriving at the passage's end and walking in; the
    tall room's wall taking a fold low and none high; the deck up with its
    rider while the cube is on the plate and down when it's off; a rider
    carried on a deck that goes by itself for ten seconds; the floor
    between the ledges and the slope back; both in the upper lift riding
    to the next place's floor. Replay hash `13255449979531731272`.
  - Not built: rooms of the hall's nine remade on this ground; a place's
    pit anywhere but the hall's band; lifts turned; anything hinged.
    Asked for and not chosen yet: what other new mechanics.
- **Two rooms meant to be hard (2026-10-13).** The owner, of the shaft as
  first built: "we don't need to prove it we need to make it hard! I feel
  like you have been slacking here on the puzzle design and the room
  limitations". The way down is twelve places now: the nine, then *Line of
  sight* (level 10, "room 07"), the shaft (9, "room 08"), and the maze.
  - *Line of sight* (`BuildSight`): three rooms in a row. A door between
    each two is a `Mover` that's up only while its plate is weighted, and
    the plate is on the near side (C in the first room, D in the second).
    The first room is papered; nothing in the second takes a fold; the
    third is two storeys and bare but for its far wall above a gallery
    4.4 m up over the way out, where the gate's plates are, one each side
    of a wall across it. One cube. That far wall is seen only from the
    second room's far end through its open door, and (over the gallery's
    edge) from the back of the third room's floor: not from the first
    room through both doors, nor from plate D, which the lintels see to.
    So: the cube on C; one stands on D while the other shoots an end from
    the doorway, then they change places for the other's; both back to
    the first room, the only place their other ends can go; the cube
    comes off C with nobody left in the second room, or they're shut in
    it; each walks in at their own end and is on their own side of the
    gallery; the cube on one plate, someone on the other, the other down
    to the lift to hold it.
  - *The shaft, again:* one cube and no crates. One of the gate's plates
    is in a cage of bars on the far ledge whose only way in is a slot at
    the floor, half a meter high. The tall room has a small frame on its
    west wall: the cube thrown into a whole end comes out of it half its
    size, and goes through the slot. So the cube can't hold the deck's
    plate up and be upstairs: someone stays on the plate, and is fetched
    by a fold whose near end was left downstairs and whose far end goes
    on the passage's walls upstairs, the only ones up there that take one.
  - `test.c` (`sight`) holds every one of those claims: each door shut and
    opening on its plate, the second room refusing a fold, the far wall
    not hit from the first room or from D with both doors open, hit from
    the doorway and from the third room's floor, the two near ends, the
    second room shutting on who's left in it, each operator up on their
    side by their own fold, and the gate. `shaft` adds the cube made small
    by the frame, nobody whole getting into the cage, and the small cube
    on its plate. So the intended way through each is known to work; that
    there's no shorter one isn't, and nobody has played either.
  - The owner agreed the next four: a place's pit anywhere, doors and
    bridges on plates (doors are in, by this), throwing oneself through a
    fold with a fall's speed, and the lamps as something to switch.
- **What was owed, in part (2026-10-13).** The owner: "go ahead and tackle
  the still owed". Thirteen places down now; the twelfth is *Blackout*
  (level 11, "room 09").
  - *Thrown, and keeping it.* A pad (`PadCount`, `PadAt`, `PadThrow`)
    gives whoever or whatever steps on it a speed. `Operator.flung` is the
    time since: while it's above 0 an operator's own speed is left alone
    in the air, where before every tick pulled it towards what the stick
    said, so nothing thrown got anywhere. Coming out of a fold faster than
    6.5 m/s sets it too. This is what there is of "momentum": a fold's
    ends are on walls still, so a fall isn't turned into a throw. Folds
    on floors and ceilings are not built.
  - *Lamps on a plate.* `Sight.dim` is a part of a place and `dimmed` how
    much of its lamps' light there is, for the surfaces' light and the
    fittings' glow there. In Blackout it's the passage, and the green
    plate's.
  - *What moves in the dark* (`Shade`, `Shadow`): it keeps to its side of
    the pit, stands while the passage is lit, and with it dark goes at
    3.6 m/s for the nearest operator, as near as its side lets it. Within
    1.35 m of anyone it has them and the room starts over. Drawn as the
    watcher. It makes no sound.
  - *A pit where a room puts it*, for the blood: `Hazard` says a room's
    own, `PitFrom`/`PitTo` how far along x it runs, and the red and the
    hands keep to that. **The pool and the drums are the hall's still:**
    their floors, slopes, lanes and what's in them are written to its
    width and its floor at 0.
  - *Blackout* (`BuildDark`): a lit room; a passage with the pit across
    it, a pad each side, and the lamps' plate at the pit's south-east
    corner; the thing past the pit, standing where the pad lands; the
    passage turns into a room whose one wall that takes a fold faces away
    from the passage. In the dark it follows the nearest of them along
    the far edge, so someone walks it away to the east and steps on the
    plate there, which stops it where it is; the other is thrown over,
    walks past it and round the corner, and puts an end on that wall, for
    the first to come by with the cube: the plate can't be held and left.
  - `test.c` (`dark`): where it starts; drawn east by someone at the
    corner; stopped by the plate and not moving while the other is thrown
    over, lands past the pit and isn't had; no fold from this side into
    the far room, and one on its wall from inside; dark with someone on
    its side, caught, and the room built again; the pit taking who falls
    in. Replay hash `7217484059584344468` natively and under WASI.
  - Still owed: the pool and the drums off the hall's coordinates; folds
    on floors; the hall's six rooms remade to this difficulty.
- **The rest of what was owed, and what's left (2026-10-13).** The owner:
  "keep going until you don't have more things to do".
  - *The pool and the drums go where a room puts them:* `PitWorks` builds
    a pit's floor and slopes as wide as `PitFrom` to `PitTo`; what's in a
    pool keeps to that width, and so does who counts as in it; a drum's
    reach is the trench's length less its own (`DrumReach`), which in the
    hall is what it was. A pit's floor is still at 0: not on an upper
    storey. No room has drums off the hall yet, so that half is the old
    numbers passing the old tests and nothing more.
  - *Undertow* (level 12, "room 10", fourth down in place of the hall's
    pool room, which is off the way down and still in the menu): a dry
    room with the cube and three crates; a channel 6 m wide and 18 long
    with the thing in it; and a door at its far end that's a `Mover` on a
    plate lying on the channel's floor half-way along, marked by four
    green floats. Nothing past the water takes a fold. What's dropped on
    the plate is the last thing in, so it comes there; a crate thrown in
    at the near end after it draws it off again, and the wader gets out.
    The one left behind has to fetch something back out of the water to
    throw for themselves, and the cube has to come over for the gate.
  - `test.c` (`undertow`): it keeping to the channel's width for ten
    seconds; the door shut; a wader with nothing thrown, had; the cube on
    the plate opening the door and bringing it; a crate thrown at the near
    end after that being what it goes for; and someone wading the far half
    meanwhile, up the slope and through the door. Replay hash unchanged
    (`7217484059584344468`).
  - A pad's throw and the dark passage's thing are heard (`cubeThrow`
    lower, `scrape` lower), unheard by Claude as all of it is.
  - **Not done, and why.** *Folds on floors and ceilings:* every fold
    here turns about the vertical only (`FoldDirection`, `Across`,
    `OnWall`, `InFold`, `OpeningOnScreen`, the box behind an opening, the
    hole `Carve` cuts), in the match and in the renderer, about forty
    places; the owner called the fold "the main mechanic it needs to be
    perfect", and Claude can't see or play what it would break, so it's
    left to be done with the owner looking. *The hall's rooms 2, 4, 5 and
    6:* they stand as the easier first half (stacking and two weights
    shorten them), ahead of rooms 7 to 10; they aren't rebuilt off the
    hall. *A room with drums off the hall, a pit on an upper storey,
    lifts turned, anything hinged:* not built.
- **Known gaps:** a shot blocked by a planter fizzles where Portal would
  nudge sideways; a lone end gives no light; the gate closing on a body
  under it, two operators through one fold at once and the cube in a hole
  when its fold is moved are unchecked.

## Progression and puzzle review (2026-10-13)

The owner requested a review of difficulty, progression, pace and flow, with
permission to polish presentation and add obstacles. This pass changes game
files only. Earlier sections are implementation history; this section is the
current resource, ordering, hint and shot-blocking contract.

The main problem was that the hall's resource budget contradicted its puzzles:
extra crates held both gate plates, and a cube used as a step skipped High
Ground's fold lesson. A separate bug let shots pass through closed kinematic
doors, undermining Line of Sight's plate relay. A regression reproduced that
closed-door escape before the shot fix.

- Shortcut, High Ground, Smaller, The Drums and All of It now have one cube.
  The other operator must supply the second weight, then be released by the
  first operator's lift plate. Undertow and the Long Pool keep their crates:
  throwing, retrieving and distracting are their physical resource puzzles.
- High Ground has 1.3 m maintenance grilles over its gallery edges. They pass
  shots and block bodies, so the cube-as-step bypass no longer works. Gallery
  height, ceiling, light and fold traversal remain as before.
- Smaller's interior has one receptive wall patch, at the back of the vent
  room (east wall, z 8.7 to 12.7), behind the baffle. The earlier east wall and
  north wall are bare. A whole operator cannot just shoot an exit through the
  vent and skip shrinking; a small operator can reach the patch and establish
  a route for the other body and cube.
- `Shoot` stops at movers, gates and arrival doors as well as static room
  pieces. Operators, loose objects and grilles still allow shots past them.
  Closed doors visibly fizzle; an open doorway still allows the intended shot.
- Smaller precedes Undertow. The descent now teaches crossing/carrying, then
  elevation and two pairs, then size, before the longer water relay. The
  remaining puzzle/horror alternation is retained: drums, size plus drums,
  door sightlines, vertical machinery/cargo size, and lighting plus launch.
  The final maze and looping descent remain; no ending was added.
- Later puzzle hints are opt-in with H and reset on chamber changes. The HUD
  gives the objective, rather than the solution, by default. The first room
  and survival passages keep their rules visible. An open gate explains the
  lift handoff. Menu stops follow the actual route in two readable rows; HUD
  and physical room signs use the same sequential numbers, 01 to 09. The old
  Lure room is outside the route and menu, still reachable by `SelectChamber`.
- Replaced the obsolete README (shared folds, three rooms and latched gates)
  with current controls, resource rules and descent order.

Validation: the native game and its two tests build without warnings; 2/2
native debug tests pass. The staged browser page and tests build without
warnings; 3/3 web debug tests pass under Node 24.16.0, including WASI, WebGL 2
and WebGPU. The unchanged portal pixel checks retain their tolerances and GPU
validation. Native/WASI replay hashes both are `4736870446065504779`; changes
reflect the intentional loose-object count and room geometry changes.

`teaching_solutions` now solves rooms 01 and 02 from their arrival lifts using
only player inputs: movement, aiming, shots, carrying/dropping, both bodies,
plate handoffs, and riding to the next place. It never positions a body, cube
or end directly. Existing fixtures continue to check later-room prerequisites
and intended routes. Added regression checks cover a cube step blocked by the
new grille, 42 whole-body vent sightlines failing to establish an interior
exit, the small operator's reachable interior patch, and shots blocked by
both closed doors in Line of Sight (including the reverse escape).

Visual inspection used silent standard-host captures of High Ground, its
optional hint, and the revised menu, plus the existing real portal rendering
fixture. The established PSX treatment is retained. This is mechanical
verification and a design review, not evidence of human solve times: the later
rooms have not had a complete input-only solve added, live human co-op and
audio balance are still unverified, and extreme stacking/body-boosting exploits
have not been exhaustively ruled out. The early rooms deliberately teach;
the final rooms carry the deeper sequencing challenge. No engine/package code,
full repository suites, CI or other-platform validation was needed or run.

### Review of that pass (2026-10-13, Claude)

The owner asked for a review of the pass above and for anything more. It
builds without warnings and its tests pass, 2/2 natively and 3/3 on the web,
with its replay hash. Its fix to `Shoot` is a real one: a shot went through a
shut door, which undid Line of Sight. Two things were added:

- High Ground's grilles go to the roof (2.8 m, where they were 1.3). At 1.3
  their top was 2.9 m up, and a jump off the other operator's head reaches
  2.87: three centimeters short, and with the cube under the other operator,
  well over. `chambers_hold` checks someone 2.5 m up is still stopped.
- H is in the key legend, where the objective line alone named it.

Looked at and left: a shot now stops at the shaft's decks too, which only
ever makes a shot harder; Smaller's one patch means the small operator has
to move the end that's in the frame and not their whole one, which is the
room's point; the old Lure room is reached at the depth Smaller has now.
Still true of every room past the second: its way through is proven a
step at a time by placing bodies, not played from lift to lift by inputs.

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
