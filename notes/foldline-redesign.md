# FOLDLINE redesign implementation plan (2026-10-15)

This is the campaign synthesis and implementation plan recovered from the interrupted
Claude task. The owner's requests are recorded in `notes/foldline.md` and the
temporary redesign brief. Geometry, casting, pacing and exact puzzles below are
agent design choices within that task, not new language/runtime requirements.
Nothing here claims human playtesting or measured solve times.

The existing framework's contracts win: 86/48/24 kg operator/cube/crate at size 1;
20 kg standard plates; momentary wires; per-plate heavy thresholds; the existing
1.6 size pickup limit; four ends total; upright receptive walls only; 1/16 refusal
floor; lift and death reset size. Reject the drafts' changed masses, mass-based
pickup rules, inverse-switch teaching, monster-as-weight puzzles, floor folds and
novel blood immunity. A frame may be used repeatedly. No arbitrary size cap.

## Route

Times are first-play hypotheses, not measurements; target about 60 minutes,
with an expected 45-75 minute range to be checked by people.

| Stop | ID / file suffix | Kind | Space and main idea | Minutes |
| --- | --- | --- | --- | --- |
| 1 | RECEPTION / reception | Puzzle | L-shaped waiting/records suite; pre-opened pair, hold a shutter while partner shoots through bars, cube frees both | 4 |
| 2 | MAILROOM / mailroom | Puzzle | Sorting hall and dogleg parcel duct; half-size scout establishes a whole-size cargo route | 5 |
| 3 | LAMPS_OUT / lamps_out | Lift vignette | A figure gets closer as corridor lamps fail; safely foreshadows pursuit | 0.5 |
| 4 | SHOWROOM / showroom | Passage | Display maze and lookout landings; one watches while the other moves | 5 |
| 5 | WEIGHBRIDGE / weighbridge | Puzzle | Tall freight court and mezzanine; giant climbs and handles grown cube, partner runs the service route | 7 |
| 6 | THE_SLOT / the_slot | Lift vignette | A normal spider beyond a jammed service grille, huge to the coming tiny bodies | 0.5 |
| 7 | CRAWLSPACE / crawlspace | Passage | Quarter-size bodies inside walls; visible web trip-lines call a spider | 5 |
| 8 | LONG_THROW / long_throw | Puzzle | A three-level freight shaft; an upper operator establishes a high launch exit for the lower flyer | 7 |
| 9 | THE_WARD / the_ward | Lift vignette | A falling metal object draws the blind flayed creature, which ignores still observers | 0.5 |
| 10 | ARCHIVE / archive | Passage | Shelving, carpet and metal crossings; lure the listener with physical decoys | 5 |
| 11 | EXCHANGE / exchange | Puzzle | Telephone control galleries, tiny service loop; compounding and reopening a route for full cargo | 7 |
| 12 | RED_STAIR / red_stair | Passage | Broken descending stair wrapped around the hands' blood well; static refuges, moving decks | 4 |
| 13 | ATRIUM / atrium | Puzzle finale | A 24 m atrium, service pipe and central weigh island; tiny scout, fourfold cargo, normal rescue route | 8 |
| 14 | RECEPTION_AGAIN / reception_again | Conclusion | A last upward lift opens on the original waiting room, now with two motionless other operators | 1 |

Bench stays available to tests/development and outside the route. Menu follows the
route. The conclusion holds on a final world image and returns to the menu by the
normal menu input; it must not silently wrap to Bench or restart the campaign.

## Shared construction and verification rules

- All coordinates are metres; y is up, floor tops are stated. Receptive walls
  use WALLPAPER and are at least 0.6 m thick. Every other wall uses another paint.
  The listed wall coordinate is its visible face, not its centre.
- Grilles are kind 4, floor-to-roof, and permit shots but stop bodies and cubes.
  Solid shutters stop shots. Keep floors at least 0.6 m thick and overlap seams.
- Lift doorway is 6 m wide x 4 m tall at `Doorway`. Its shell roof reaches 5.4 m.
  Keep a matching clear opening and never leave gaps beside its jambs.
- Use the existing wire and device indicator APIs. Wires take physical routes.
  Avoid unexplained decorative green lamps beside unrelated devices.
- One full input-only solve from arrival to next lift per puzzle is required;
  include both bodies, held/loose cargo, exact shot origins and final lift access.
  Negative checks target real alternative routes: closed shutters, baffle sight,
  plate mass, stacking and alternate frames. Do not merely place the solved state.
- In passages, idle bodies retain their sight and remain safe on explicit
  landings. No timer demands simultaneous control of both bodies. A catch rebuilds
  the passage for both, as the existing maze does.

## First implementation tranche: exact build specs

### 1. Reception

Builder `BuildReception(Arena, mut Place, mut Fold)`; Puzzle, live device.
A subdued paper/carpet suite with 4.4 m ceiling, plenty of light for the first
interactions. Playable floor is x -12..12, z -8..8. The major divider runs along
x=0: opaque concrete at z 0..8, full-height grille at z -8..0. West is the
waiting lobby; east is records. This makes the room read as two connected arms,
rather than an obstacle dropped in the old rectangular hall.

- Arrival lift (-6,0,-10.1), NORTH, doorway at z=-8; south lobby wall leaves
  x -9..-3 open. Exit lift (6,0,10.1), SOUTH, doorway z=8, opening x 3..9.
- Receptive west outside wall face x=-12, normal +x, z -8..8; receptive east
  wall face x=12, normal -x, z -8..8. All other perimeter walls are concrete.
- Pre-place A0 on west wall foot (-12,0,-3), face +x, size 1; A1 on east wall
  foot (12,0,-3), face -x, size 1. Each opening shows the other bay immediately.
  Leave 3 m clear before both, and no furniture across them.
- Cube cupboard inside records: interior x 6..10, z 2..6; sides are solid at
  x=6 and 10, rear receptive face z=6 normal -z. Its front is a full-height
  grille at z=2. Its interior floor is continuous with records. Put cube at
  (8,0.40,4.4). The back panel must have at least x 6.5..9.5 clear.
- An opaque moving shutter just behind the grille closes at (8,2.2,2.35),
  size (4,4.4,0.3), signal 0. It covers every line to the cupboard rear while
  closed, but does not intrude into the rear fold pocket.
- Signal 0 standard plate at (-8,0,4), west of the opaque half of the major
  divider. Wire goes from its east rim toward x=-0.45, up the divider, over
  the cupboard and down to its shutter indicator; use two Wire entities if
  necessary to stay under twelve points. The opaque divider blocks every
  ray from a body on this plate to the cupboard's receptive back.
- Signal 1 standard exit plate (4,0,5.5). A different visible wire runs north
  and up the exit lintel. ExitLift needs 1<<1 only. There is no linger.

Intended interaction: A enters records through their already open pair, sees the
cube behind bars, then returns to the west plate. B crosses that same pair and
can now shoot B0 through the grille onto the cupboard back (8,1.3,6), and B1 onto
the east wall at z=-6. B crosses B1, retrieves the cube and returns through B0,
then puts it on the exit plate. The shutter may close after B's route is made;
the still-open back fold remains a legitimate return. A leaves their plate and
crosses their own pair; both walk to the exit. This uses both bodies for different
jobs and gives the cube the final job. No permanent clamp/release ritual.

Teaching: the pre-opened A pair teaches crossing; B visibly owns separate colours;
the shutter rises/falls with weight and wire light; shots pass bars; the cube
carries through; the exit stays on when both operators leave its plate.

Exploit/QA checks: cube cannot be reached through bars from the doorway (at least
2.5 m from front to resting cube, and carry reach from outside is blocked unless
through a fold); A's plate view cannot hit the rear panel; shutter blocks the
same shot when closed; no shots reach cupboard back through the side walls.
An ordinary cube/head stack cannot bypass full-height grilles. Accept retrieving
the cube by reaching through a properly linked fold: it is a taught capability.

Dress: reception desk and bolted chair rows west, short metal filing cabinets
east; keep routes 2 m wide. One pale figure behind frosted-looking grille at a
sealed desk niche changes pose only once after both enter records. It never
attacks and no unexplained plate state changes accompany it.

### 2. Mailroom

Builder `BuildMailroom(Arena, mut Place, mut Fold)`; Puzzle, live device.
Sorting hall x -14..4, z -10..12; sealed office x 6..14, z 4..12.
Both ceilings y=4.4. There is no full-height corridor joining them.

- Arrival lift (-8,0,-12.1), NORTH; opening x -11..-5 at z=-10.
  Exit lift (10,0,14.1), SOUTH; opening x 7..13 at z=12.
- Hall west wall face x=-14 is receptive. Pre-place B2 at foot
  (-14,0,-5), face +x, size 1; B3 at (-14,0,6), face +x, size 0.5,
  with a visible half-frame there. These ends share a clear looping floor
  route; walking around sorting tables can repeat the shrink for fun.
- The office's east wall face x=14, z 7..11 is receptive, normal -x.
  Every hall-facing office surface is opaque and non-receptive.
- The low parcel duct makes TWO opaque turns. Interior first leg
  x 4..10, z -4..-2; second leg x 8..10, z -4..4; it opens into the office
  at x 8..10,z=4. Clear duct height is 1.02 m, width 2 m.
  The solid baffle x 4..8,z -2..4 runs floor to main ceiling. Roof only the
  actual low duct, y 1.02..1.5; baffle/outer walls enclose it. Half bodies
  (0.9 m) fit with useful margin; whole bodies (1.8 m) do not.
  A full-height header x 4..5,y 1.02..4.4,z -4..-2 seals the entrance above
  the opening, preventing a cube/head boost onto the low duct roof.
  Important sight check: from the entrance plane x=4,z -4..-2, every ray
  to the office's east patch z 7..11 hits the x 4..8,z -2..4 baffle.
- Cube whole at (-5,0.40,1); exit plate (11,0,7), signal 0, ordinary 20 kg.
  Wire runs east then north up the office's east wall to the exit lintel.
  Exit requires 1<<0. No crate, no second exit plate, no hidden latch.

Intended solve: B crosses B2 -> B3 and visibly becomes half-size. B goes east,
follows the low two-turn duct to the office and can test the exit plate (10.75 kg,
not enough). B re-shoots B3 from the office onto its east wallpaper at
(14,1.3,8.5): their pair becomes two ordinary ends. A, still whole, carries the
cube through B2, leaves it on the plate, and both enter the office lift. B may
remain tiny as a reward; the next place makes bodies whole.

The role division is a scout who cannot lift the whole cube and a whole cargo
carrier who cannot fit the duct. Both pairs remain free for alternative solutions.
Two half operators together can press 20 kg temporarily; both must leave for the
lift, so this does not remove the cargo requirement. A repeated-shrink route to
1/16 is harmless; it must not be needed.

Teaching: watch partner become half-size through the barred office observation
window, see the weight gauge fail, discover that a small person can establish a
normal opening by moving their end off the size frame. A demonstration crate is
optional and must never become extra final weight or block an end. Prefer direct
partner observation over adding a fragile scripted physics demonstration.

Dress: low sorting tables and pigeonholes; an observation grille in the office
wall above the duct roof gives a view of the scout but NOT the east receptive
patch (add an opaque return beside it). Do not accidentally make a straight shot
through the optional observation grille to the office wall. A spider leg retreats
under a far sorting table once; no live threat during this puzzle.

### 3. Lamps Out (lift vignette)

Builder `BuildLampsOut(Arena, mut Place)`; StoppingLift at (0,0,0), NORTH,
stay 11 s, dead device. Corridor floor x -4..4,z 2.6..27, ceiling y=4.4,
concrete walls, dead office doors as props. Full-height grille across z=4.0
keeps anyone who steps to the threshold out of the figure's space.
Keep the actual lift interior empty for offset-preserving transitions.

Lamp grid: one column centred x=0, six rows at z 5,9,13,17,21,25, high 4.4;
each its own group (1..6). Palette near-neutral cold green, light=0.35.
A FIGURE mannequin or FLAYED (use the same model as Showroom's live stalker)
begins at (0,0,24), facing SOUTH. Script time is arena.arrive after the door opens.

At 2.0,3.4,4.8,6.2,7.6,9.0 s, kill groups from far to near. Between steps,
the figure is absolutely still whenever either operator is looking at it.
During a short lamp flicker or while both look away it may approach the next
mark: z 24,20,16,12,8,4.8. Never move it toward or through the grille; never
catch a player here. If everyone stares, lamp failures still complete the
vignette; do not deadlock the lift waiting for a scare. At 10 s its arms rise
toward the grille. At 11 s both riders inside make the lift shut and continue.
Use one Beast and a small per-place match state; no physics attack and no text.

This beat safely establishes anticipation and observation. Showroom's arrival
landing demonstrates the exact live stalker rule before the first dangerous turn.

## Remaining implementation specs

### 4. Showroom

Passage, dead device. The implemented grid is 8 x 8 of 3 m cells, origin(-12,-12),
floor 0, ceiling 3.8. Arrival(-9,0,-17.1) NORTH gives a real 3 m safe landing
outside the maze rectangle; exit(9,0,14.1) SOUTH needs 0. The live FLAYED uses
the existing Stalk pathing, speed 4.5/9 m/s,1.35 m catch range and 6.5/.4 s
blackout timing. Both operators' retained yaw/raycast sight counts.

Internal dividers are physical kind 4 grilles 2.8 m high in BOTH orientations.
They stop bodies and the pathfinder while allowing a partner to watch across
aisles. The outside walls are opaque. Stalk now advances its actual sight ray
past grilles/operators with PastShape far-face bounds and stops at opaque
geometry; this is the same physical distinction players see.

The final masks keep the basic alternating long lanes, with extra north
openings row 2->3,column 7 and row 3->4,column 7 for the right-side loop, and
row 4->5,column 5. The live start is cell 38 (column 6,row 4), one aisle beyond
the first sketch. The former mandatory opaque 3 m left U-turn forced a player
inside the creature's 1.35 m catch during a blackout; it was changed as geometry,
without weakening the creature.

The completed native input solve uses both bodies in a relay through row 2,
row 4 and row 6 lookout positions, then both leave by the exit lift. A separate
interval turns A away while inactive B still freezes the figure. Landing
positions reached by inputs check an opaque obstruction against a grille
window. With both bodies turned away the live creature hunts, catches and
rebuilds the passage for both. Inert mannequins remain atmosphere, silent
and separate from the live threat.

### 5. Weighbridge

Puzzle, live device. Freight court x -14..14,z -14..14, ceiling y 12.6.
Arrival (-8,0,-16.1) NORTH. Exit (8,5.4,16.1) SOUTH at upper floor y 5.4.
The exit floor/gap is at z 14, x 5..11. Orange lamps, bare metal, large truck scale.

The west giant route is a broad stepped loading dock: x -14..-4, first slab
z -3..1 top 1.8, second z 1..5 top 3.6, third z 5..14 top 5.4. Each riser is too high
for a normal 1.07 m jump, comfortable for a giant's 2.14 m jump. The final upper
floor continues across x -14..14,z 10..14. Add floor-to-roof grilles along any
service shortcut; solid dressing must not offer intermediate steps.

Only ground wall patches are receptive until the top:
- West wall x=-14, normal+x,z -13..-4, y 0..5.6. Size-2 frame foot
  (-14,0,-8), painted and pre-place A1 there; A0 normal in the same west patch
  at (-14,0,-11.5), normal+x, clear of the arrival lift.
- Upper rear wall face z=14, normal-z, x -12..-6, y 5.4..8.2 is receptive.
  A fourth riser at z 9.8..10.2,x -14..-4,y 5.4..7.2 hides this short panel
  from ground/service eyes. The giant jumps it to reach the exit loading bay.
  Cover the patch's
  reverse and side faces in concrete: Wall.takes applies to ALL upright faces.

Put the only cube at (-6,0.40,-9). A2-size operator can grow while empty, and
the normal partner can carry/throw the whole cube through A0 to be caught by
the giant at A1; or operator and cube can grow together. Heavy plate on the top
at (-9,5.4,11.5), need 350, signal 1, wire visibly crosses the gallery to the
exit lift. Whole operators+cube weigh 220 and fail. Grown cube weighs 384 and
succeeds. No crate.

Normal partner's service route: a self-driven freight deck at x 8,z -7, 3 x 3 m,
top travelling y 0->3.6 (centres y -0.15 ->3.45), speed 1.8. Upper service floor
x 4..14,z -5..3 at 3.6, ceiling 6.0 (2.4 m clear; a giant cannot enter).
Standard plate (8,3.6,0), signal 0, drives the giant shutter across west stairs
at z 4.8, x -14..-4: closed centre(-9,6.3,4.8), size(10,5.4,0.35).
The wire hugs the east wall, crosses overhead to the shutter, and is visible
from both roles. Its open centre is (-9,13.3,4.8), seven metres above the
closed centre, clearing the giant's actual jump apex and held cube.

Solve: A grows and takes the grown cube up step 1/2; B rides the deck and holds
signal 0. A jumps the last riser through the open shutter, deposits cube on H,
then establishes a normal pair from the upper rear patch to an unframed ground
patch by moving BOTH of their own ends. A remains giant and never needs to
enter the now-normal pair. B leaves the service plate, takes the automatic
deck down, enters A's ground end and arrives whole at the top. Both fit the
4 m exit doorway (giant height 3.6). Nothing living must remain on a plate.

Check the upper patch from all ground/service sample origins, not just arrival.
A grown cube lob that lands on H is an acceptable skilled shortcut; players
still need a giant or the top rescue route to reach the exit. Do not remove
physics freedom to enforce one choreography. Giant-on-giant boosting is allowed
and still demonstrates growth. One CRAWLER crosses a high sealed vent once;
it never attacks or repeats while players think.

### 6. The Slot

Vignette, StoppingLift (0,0,0), NORTH, stay 8, dead device. A short maintenance
landing x -4..4,z 2.6..10, ceiling 2.5 beyond a full-height grille at z 4.
A SPIDER, scale 0.8, appears on the far side. A loose-looking screw/bolt prop
falls onto a luminous web at z 7 after 2.5 s; the spider rushes to that exact
point and stays wrapping/tapping it for 4 s. This is a safe physical demonstration
of the Crawlspace web rule. A leg may reach toward the grille but never enters
the lift. Use a prop screw or tiny Cardboard if unavailable; no new pickup type.
Three thin bright web segments show the triggering surface. The lift closes
normally when both riders are inside; do not implement a new half-open door API.

### 7. Crawlspace

Passage, dead device; four fixed ends remain usable. Normal arrival airlock
x -4..4,z -4..4, roof 4.4, arrival(0,0,-6.1) NORTH. The crawl floor is
x -12..12,z 6..32, roof 1.1; walls are joists, pipes and plaster backings.
At quarter size the 1.1 m space feels more than twice body height. Roof 1.1,
not 0.6, gives the spider model actual clearance.

A0 normal on airlock north wall foot(0,0,4), face-z. A1 quarter on crawlspace
west wall foot(-12,0,8), face+x; prepaint quarter frame. Two full crates sit
in airlock to carry/throw through first, becoming quarter-size usable decoys.
B2 quarter on crawlspace east wall foot(12,0,29), face-x. B3 normal on exit
airlock west wall foot(16,0,29), face+x. Exit airlock x 16..24,z 25..33, roof 4.4,
exit(20,0,35.1) SOUTH. Cover the receptive end walls' reverse faces. The actual
crawlspace walls completely separate the airlocks: no outside walking bypass.

Make an S route of 1.4 m-clear crawl tunnels:
(-10,8)->(-6,8)->(-6,16)->(5,16)->(5,23)->(-5,23)->(-5,29)->(10,29).
At turns provide 1.5 x 1.5 m recessed refuges off the spider's track. The open
section at z 16 is a cable trench traversed by web strands at x=-2 and 3; the
section at z 23 has a strand at x 0. Thin glinting web boxes are triggers, not
invisible collider walls. A body or loose object touching one selects that
strand as the spider's destination. Same trigger cannot retrigger until the
toucher leaves (prevents permanent bait while an object rests).

SPIDER scale 0.45 patrols only its explicit web tracks between those three nodes,
starting(1,0,19). On trigger, go to the latest touched node at 3 m/s; spend
5 s striking/wrapping, then return to hub. Catch quarter body within 0.32 m
on the active track; ignore bodies inside refuge bounds. Player speed 1.3 m/s.
The Slot has just demonstrated a bolt drawing this spider to silk. Crawlspace
adds two spare quarter-sized parcels in the first refuge at(1.35,.1,14.3) and
(2.65,.1,14.3), alongside the two whole parcels at the arrival. A thrown parcel
touching a strand calls the spider; a resting one cannot renew it. A missed
throw remains a physical object. There are no random target changes.

Co-op: one lures from a refuge and the other crosses; then swap. Solo can
throw, switch and cross within the five-second busy interval; the actual
crossing is at most 3 m (2.3 s), not the whole tunnel. Ends restore whole size
before the exit. Death resets both to the arrival and rebuilds the fixed ends.
No noise/material listener rule here: the spider answers web contact only.

### 8. Long Throw

Puzzle, live device. Concrete freight shaft x -12..16,z -12..16, roof 18.
Arrival (-9,0,-14.1) NORTH. Landing platform x 1..14,z 2..10, top 5.4.
Exit corridor x 12..16,z 9..16 at 5.4, then lift(13,5.4,18.1) SOUTH.
The void below the route has the ordinary blood pit, top 0, not a new rule.

Ground near platform x -12..-6,z -12..2, top 0. Ground deck access strip
x -6..2,z -12..-6, top 0. Pad at(-10,0,-.7), velocity(0,7,14), enters a
receptive launch wall face z=0, normal-z,x -11.5..-8.5,y 0..4.4.
Its intended entry foot(-10,0,0), with no size ratio. Actual pad activation
starts before its centre; the short run avoids hitting the source lintel.

High target: receptive tower face x=-6, normal+x,z 4..8,y 8..12.4, foot
(-6,8,6). Its reverse/side faces are concreted. A normal end here ejects the
flyer toward+x, across 7 m to the far landing. The real carried-cube solve
passes its leading edge with feet above 5.6 and lands on top 5.4. The held
cube slows the flyer below the initial 14 m/s; this measured gap preserves
carry physics instead of relying on the initial ideal ballistic estimate.

Upper operator rides a 3 x 3 freight deck at(0,-9), top 0->8, signal 0;
standard control plate(-3,0,-9) holds it up (no cube available at that point:
the only cube begins with the lower flyer). Upper viewing cage x -2..2,
z -12..0, floor 8. Full-height grilles prevent jumping directly to the far bank;
they pass the operator's shots. Its rear inside wall z=-12 is a normal
receptive return panel. A solid sight wall at z=0,x -6..3,y 0..8.5 blocks
ground views of the high target. Only ground x>-6 is z<=-6, so every such
ray crosses the wall below 8.5. Upper eyes 9.55 see over it. Source wall can
be shot diagonally from the upper cage around the sight wall's west edge.

Pair ownership and solve: B holds the deck plate; A rides up. A places A0 on
source wall and A1 on the high target. B leaves the plate (A's cage has fixed
floor), carries cube onto pad, flies through A's pair and lands far side.
The far landing's only receptive return patch is around the L bend:
inside west wall x=12, face+x,z 12..15, y 5.4..9.8, behind opaque wall at z 9,
x 6..12 (opening x 12..16). Its reverse is concrete. B places B0 on that
patch, walks back onto the landing to shoot B1 on the upper cage rear wall.
A walks into B1 and emerges in the far corridor. B's cube stays on the
standard exit plate(14,5.4,13), signal 1; exit needs 1<<1.

First pad demo should use the actual operator rather than a special unowned
fifth fold. The visible upper target and launch arrows/pictograms show the
trajectory. An optional pre-placed A0/A1 may give a safe SHORT demonstration
landing on the near side, but the high target must still be shot by the upper
operator; avoid adding scripts that erase a player's ends without cause.
A slow automatic moving deck below the far balcony is a harmless recovery
catch, not an alternate route to the balcony.

Negative checks: lower body cannot hit high target; upper cage blocks normal
jump (including a cube boost) to far bank; far return patch cannot be hit from
upper cage or any near origin; pad alone collides with source wall; actual
fling works with carried cube and both bodies can reach the final lift.

### 9. The Ward

Vignette, StoppingLift(0,0,0), EAST, stay 9, dead device. Ward x 2.6..18,
z -4..4, high 4.4, grille x 4 protects observers. Three beds, curtains, and a
bedpan prop at(12,0.8,0). At 2.2 s it visibly falls and makes the noise cue.
A FLAYED dark model at(16,0,-2) runs to(12,0,0), lashes there for 4 s then
freezes head cocked, ignoring the still operators. If the actual per-place
listener event API exists, use it; otherwise drive this exact vignette script.
No claim that running in the lift is silent: if the players run, it turns its
head toward them, stopped by the real grille. The next passage repeats the
rule in its safe opening.

### 10. Archive

Passage, dead device. The built Archive is an open 24 x 24 m room, floor 0,
ceiling 4.4; arrival(-8,0,-14.1) NORTH, exit(8,0,14.1) SOUTH, no exit plate.
Shelves sit at the perimeter, leaving an actual open central hunting floor.
Carpet bands are z -12..-2,0..4,6..12; metal strips are z -2..0 and 4..6.
One reusable crate starts at(6,.4,-8), the next at(-4,.4,2).

The dark FLAYED starts(0,0,2). ArchiveHunt stores a fixed target, priority and
timer, prior parcel velocities and per-body metal-step clocks. Unheld cargo
whose prior speed and impact velocity change exceed 3.5 m/s below 1.2 m makes
a 12 s priority decoy target. Moving on metal emits steps at 0.3 s intervals,
making a 4 s target when no priority decoy remains. Carpet movement does not
emit those events. The listener moves at 4.1 m/s straight toward the recorded
point; it does not track a silent body's changing position. Contact within
1.2 m in the crossing region is dangerous; carpet banks/lifts are safe waits.

The Ward already shows a dropped object drawing this blind creature. Here
players throw a parcel toward the opposite side of each strip, cross on the
other side, then use the next carpet bank and its parcel. Solo can throw and
switch bodies; no simultaneous controls or near-frame timing is needed.

The full native input solve takes both bodies over both strips and into the
next lift. A second input scenario crosses without a decoy and verifies a
real catch followed by both-body restart. This is the actual prototype hearing
rule, not the abandoned shelf-aisle navigation graph, generic sound-event API,
mass-based loudness, running/landing detector or scripted arrival demonstration.
The shared Beast only displays match state; no engine/runtime audio API is
needed for simulation hearing.

### 11. Exchange

Puzzle, live device. This is the hard shutter/line-of-sight room with a tiny
maintenance scout, not another symmetric pair of exit weights.
Arrival(-10,0,-14.1) NORTH; exit(13,0,16.1) SOUTH.
Vestibule x -16..-4,z -12..12; machine hall x -4..8,z -12..12;
exit bank x 8..18,z 4..14. Ceiling 4.4. One cube at(-10,0,-4).

Signal 0 plate(-10,0,6) opens a full-height D0 in the wall x=-4,
doorway z -8..-4 (size along z 4). Cube will hold it while both enter the
machine hall. Signal 1 standard plate(1,0,-5) opens the opaque target shutter
D1 at x 8,z 8, width alongz 3, high 4.4. Its role is an optical shutter, not a
normal doorway: a fixed full-height grille remains behind it. D1 is wired
clearly from plate 1; it has one lamp. No inverse circuit or latch.

Pre-place B2 normal on vestibule west wall x=-16,z -8; B3 half on same wall
z 2, both face+x. Repeating the whole->half route twice makes B quarter-size.
There are no other growth/size frames. A whole body holding plate 1 is the
only active weight in the hall; quarter B (1.34 kg) cannot substitute.

The maintenance duct starts in the machine hall at(4,0,0), heading+x:
leg x 4..7,z -1..1, then north x 5..7,z 0..7, then east x 5..8,z 6..8.
Interior clear height 0.55; width 2. A solid baffle x 4..5,z 1..6 and another
x 7..8,z 0..6 block a straight shot from the hall. The scout's last pocket
x 6..8,z 6..8 faces the D1/grille slot at x 8. Its roof ends at x 8, so the
quarter scout eye at x 7.5,y 0.387 can aim upward after leaving the slot.

Only far target panel is east wall face x 18, normal-x,z 7..11. Place it
behind an opaque return wall at x 12,z 4..7 extending to the ceiling, so no
machine-hall doorway sight can reach it. The intended ray from the tiny
pocket (7.5,0.387,7.5) to(18,1.3,9) passes the low slot at x 8,y 0.431
and then crosses the open exit bank. Test that ray exactly. D1 blocks it
until A stands on plate 1. A's plate view, hall floor views, and views through
D0 must all fail. If a line around the return wall exists, extend the return
without blocking the named scout ray.

Solve: B shrinks twice in the vestibule; cube holds D0. A enters machine hall
and holds plate 1. B crawls the duct and shoots B3 off the half-frame onto
the far panel as a normal end, keeping B2 normal in the vestibule.
A can now leave plate 1 and return through D0 to the vestibule before taking
the cube off signal 0. B retraces the duct and D0 while cube still holds it.
A picks up cube (D0 closes), both use B2->B3, cube onto standard exit plate
(14,0,11), signal 2. Both enter lift. Any person left in machine hall when
the cube is removed must be rescued by a deliberate new route, not teleported.

This is an ordering puzzle: leave the cube until EVERYONE has returned;
pre-establish the far end while the second person has the optical shutter;
then sacrifice the shrink route to make a cargo route. One body cannot both
hold plate 1 and see through the tiny pocket. A quarter cube weighs 0.75 kg
and does not press an ordinary plate. Two quarter bodies do not help.
Decorate with CRT racks, taut cables and raised wire trays; one CRAWLER in a
sealed ceiling tray is atmosphere only.

### 12. Red Stair

Passage, dead device. A 16 x 16 m blood well x -8..8,z -8..8, top 0.
Start arrival(-10,8,-12.1) NORTH on a southwest landing at y 8; exit
(10,0,12.1) SOUTH on northeast landing at y 0. The stair wraps around the
well, with safe outer walkway width 3 m; all floors outside the blood footprint.
Use actual 0.4 m rises and 0.65 m treads for descent, not 2 m jumps.
Four broad permanent refuges at heights 8,6,3,0 allow idle bodies to wait.
Black/rust concrete, chains as thin props, red light coming from below.

Two breaks interrupt the route: at height 6 on west side, gap 6 m alongz,
and at height 3 on east side, gap 6 m. Self-driven 3 x 3 m decks cross each gap
at 1.6 m/s, waits 1.3 s at ends; rails prevent lateral falls but leave each
boarding edge. Their whole cycle is visible before boarding. No plates,
rising blood, red-light immunity, or hands reaching an otherwise safe floor.
They only take someone who actually falls into the blood, as already taught.

Opening demonstration: a DROWNED prop suspended over the blood drops at 2 s,
hands rise, body disappears beneath the opaque red in a burst of pieces.
The safe arrival overlooks it. At the last refuge a FLAYED body is seen
climbing an unreachable wall once, then gone. A final deck return leaves the
players briefly stranded on a small but safe landing while hands slap below.
Solo parks one body on the refuge then takes the other across. Every landing
is large enough for both and a deck can carry both; no synchronized jumping.

### 13. Atrium

Puzzle finale, live device; no lethal thinking-room monster. The building core
has a 24 m ceiling, a blood void and a heavy cargo island. Ground entry gallery
x -20..-12,z -18..6, top 0. Island x -8..8,z 0..20, top 0. The blood pit spans
x -12..12,z -18..22, excluding island and actual floors. Arrival
(-16,0,-20.1) NORTH, exit(0,0,22.1) SOUTH; opening z 20,x -3..3.
Keep outer gallery and island individually well lit. All connecting void is
real; only the tiny service bridge crosses it.

Bridge: a covered 0.9 m-wide pipe/rectangular duct fromx -12 to-8 at z 4,
clear height 0.55, connected to entry gallery by a narrow ledge at z 4.
Quarter body height 0.45 fits; half 0.9 and whole 1.8 do not. Outer walls and
roof are bare metal; no shots through bends. A normal cube may not be forced
through by holding it from outside. Side observation grilles let players see
the tiny scout actually on the bridge.

Entry gallery west wall face x=-20 takes folds. Normal patch foot
(-20,0,-14); half frame foot(-20,0,-7). Pre-place A0 normal/A1 half.
A takes two whole->half laps and becomes quarter, then walks the bridge.

Island's central tower wall has receptive NORTH face at z 8,x -4.5..4.5,
y 0..12.0, with size 4 frame foot(0,0,8), face+z. Back it with solid concrete
at z 6.2..7.2 and cover all side faces: the gallery must see only concrete.
A smaller service panel is also north-facing at z 8,x 5..8,y 0..4.4, with
quarter frame foot(6.5,0,8), face+z and concrete rear. Players can walk around
the tower's ends at x ±6.5. Neither receptive north face is shootable from
the southern entry gallery; verify this as geometry, not a place-id exception.

Behind the size 4 opening is a giant cargo catch tray: clear x -3.2..3.2,
z 8..16, floor 0, side walls at x ±3.2, solid catch wall at z 16,y 0..6.
The size 4 end needs its full 5.44 m width clear at its plane; do not put tray
sides inside ±2.72. Heavy plate at(0,0,13.5), half extents(2.3,2.3),
need 2500 kg, signal 0, wire
goes around the tower to the exit. Cube size 4 weighs 3072. The catch wall
stops the grown cube, which settles with centre aroundz 14.5 onto H.
Put only ONE whole cube in the entry gallery at(-16,0,-11); no spare crates.
Whole operators+cube cannot operate H. The tray is visually a weighing chute,
so the cube being caught rather than precisely hand-placed is readable.

Both pairs and a role trade are needed:
1. Quarter A crosses the pipe and moves A1 from the hall half-frame into the
   island's quarter frame. A0 remains ordinary in the entry gallery.
2. Whole B enters A0, emerges quarter at A1 and can now work on the island.
   A enters A1 in reverse and returns WHOLE to the entry gallery: a visible
   reversal of the small scout role, leaving a whole cargo worker near cube.
3. B places B3 in the giant frame. From the island's west side, B shoots B2
   onto an ordinary entry-gallery patch (not A0; usex=-20,z=-3). The gallery
   patch must be visible around the tower's west end.
4. A throws whole cube through B2. It exits B3 at size 4, hits catch wall and
   drops onto H. Since speed also scales 4x, the catch tray is essential;
   validate the trajectory and settling in the real input solve.
5. A enters A0->A1 to arrive quarter beside B. Both walk around the tower
   to the exit. Nobody living stays on H; all four ends remain meaningful.

A may also establish a normal island end after cargo delivery and arrive whole;
the solution does not insist on being tiny at the lift. Growing either person
to 4 is allowed and fun, but they cannot fit the lift until restoring size using
a deliberately arranged pair. Death/R is an available escape, never required.
The 4-frame does not mean an arbitrary cap at 4. A 2.96 m cube really has room
to emerge, fall and rest; no hidden collider or destroy-on-arrival shortcut.

One stretched shadow FIGURE watches from a sealed gallery 20 m above, changes
pose once when the scale-4 cargo lands, then is gone when approached via folds.
No chase during this final reasoning puzzle.

### 14. Reception Again (conclusion)

The implemented coda calls BuildReception with its ending option, placing the
cube on the familiar exit plate. PlaceKind.Ending keeps the old exit gate
closed and suppresses further rides; it does not wrap PlaceAt. The arrival
door opens normally, so the tableau can be approached and observed.

Two Figure Beasts stand in records at(8,0,-4.5) and(8,0,-1.5), one mannequin
and one shadow skin. Their yaw changes EAST->WEST after 4 s. All room lamps
are in group 1, disabled after 7 s. The view remains in the resulting scene;
there is no invented nine-second title cut or new control prompt. Existing
menu input and R remain available. The native test verifies the live weight
signal, lamp failure, indefinite terminal hold and R rebuilding the ending.

## Current implementation status

This is a status record, not a list of additional approved requirements.
The detailed current contracts are in notes/foldline.md's 2026-10-15 section.

- [x] Fourteen route stops, Bench/Landing outside the route, terminal ending.
- [x] All six puzzles have native full input-only solves, including both bodies,
  cargo, the final circuit and lift departure, plus targeted blocked-route checks.
- [x] All four passages have completed native solo two-body traversals, with
  physical waiting/refuge positions. Showroom, Crawlspace and Archive have actual
  input-driven hunt/catch/both-body-reset checks.
- [x] Vignettes are physically bounded and nonlethal; their tests leave the lift,
  wait past its stay and return, proving that leaving is optional.
- [x] Showroom's final grille layout uses both bodies' actual sight, including
  an inactive B freezing the creature while A looks away. Its relay and
  opaque/grille sight checks pass without weakening creature constants.
- [x] Spider contact is edge-triggered; its model fits the crawl roof. Its S
  route has two arrival parcels and two spare quarter parcels at the first refuge.
- [x] Archive uses impact-priority and metal-step targets on an open floor;
  it does not track silent bodies or depend on the proposed aisle graph.
- [x] Heavy/quarter loads use cubic mass; giant cargo settles on Atrium's
  actual weighing surface and both bodies leave after the tested role trade.
- [x] Low-route jumping, target reverse faces, hidden sightlines, closed
  shutters immediately behind grilles and small-body strain have focused checks.
- [x] Imported creature silhouettes, real clips, eyes and fold rendering have
  GPU assertions; the fixture explicitly loads its embedded asset pack.
- [x] Native GPU fixture covers actual lamp/fog changes and fourteen terminal
  lift transitions at its unchanged pixel tolerances.
- [x] README and the current notes describe this route, physical teaching,
  free compounding size and momentary wires; earlier C/D/H/loop prose is history.
- [x] Final integrated native debug 2/2 and web debug 3/3 scoped suites pass.
  Native/WASI replay hash is `4519119950475406368`; the fourteen-stop campaign
  hash is `18258979499744220870`. Native OpenGL GPU coverage also passes.
- [x] Standard-host captures inspected across all fourteen places, along with
  giant/normal openings, creature poses, tiny passages and the final tableau.
  The menu and full stop selector also fit the inspected 960 x 540 web preview.
- [x] Human discovery, live Internet co-op, solve time and audio balance are
  explicitly unmeasured. The 45-75 minute figure remains a design target.

No engine or language work is required. New behaviours are prototype match
systems: silk-triggered spider, deterministic impact/metal-step listener,
small vignette scripts and terminal conclusion state. They read ordinary
match queries and drive shared Beast components. Dedicated negative coverage
for every possible silk/blood contact and exhaustive physics shortcuts is
not claimed; shared catch/reset/respawn paths and the completed routes are tested.

## Implementation corrections and current evidence

The builders and executable tests supersede coordinate sketches above. This
section records corrections found while exercising the actual game; the route
and the owner's mechanic decisions are unchanged.

- Mailroom is built in `place_mailroom.tide`. Its full input-only solve passed
  natively, including the half scout, failed 20 kg plate, a failed whole-cube
  pickup by the small body, a normal cargo route and both riders leaving.
  An exterior shot at the office panel is blocked. A full-height header over
  the low duct entrance closes the cube/head-boost route onto its roof; the
  normal-body walk and jump regressions exercise that entrance.
- Exchange is built in `place_exchange.tide`. Its full input-only solve passed
  natively. The test also attempts the actual tiny-pocket shot while the
  optical shutter is shut, and the same target shot from the plate holder's
  position. The former exposed the game's old fixed-distance grille skip:
  a shot stepped past a shutter immediately behind bars. The integrator fixed
  the shared shot logic to advance past the struck shape's far face, and the
  unchanged shutter geometry now blocks the shot. A normal end is established
  only after the second body opens the shutter.
- Atrium is built by `BuildCargoAtrium` in `place_atrium.tide` (the old main
  spawn handler already used the name BuildAtrium). The complete input-only
  role trade, two shrink laps, tiny bridge, reverse growth, fourfold cube throw,
  heavy circuit and both riders' exit passed natively. The island cargo chute
  uses actual grille colliders, allowing its frame to be shot while stopping
  cargo. Its heavy weighing surface is 4.6 m square, centred (0,0,13.5), rather
  than the default 1.6 m plate: the 2.96 m cargo cube naturally settles about
  1.14 m off centre. The threshold stays 2500 kg; size 4 cube mass is 3072 kg.
  A 24 m high gallery-edge grille with a low .55 m gap and a sealed north end
  blocks jumping onto the low pipe roof. The island-side source-end shot is
  from (-6.5,0,11.5), clear of that north seal and the pipe's roof.
- Weighbridge's ground normal end moved to the west wall to clear the arrival
  lift. A solid 1.8 m rail across the full x -14..14 gallery width and a restricted upper patch remove a service-balcony
  sightline that the first floor-only sketch did not block. Its builder's
  complete input solve and 210 sampled negative rays pass; the giant jumps
  the rail as a fourth riser.
- Long Throw's return patch moved to the inside west face of the L corridor:
  the original rear patch overlapped the exit doorway. Actual pad activation
  begins before the pad centre, and a carried cube can slow the flyer. The
  final pad is (-10,0,-.7), launch(0,7,14); landing edge x 1 makes a 7 m
  flight from the receiving wall x -6. The input solve crosses the edge with
  feet above 5.6 and lands on 5.4. Both operators then use the L-corner rescue
  route and leave with the cube powering the exit.
- The GPU lamp/fog fixture now looks down at a known carpet patch beneath
  known active lamps, clear of the bench's partition. All configured lamps
  are explicitly On, group 1, high 4.4. Turning that group off changes 626/626
  sampled floor points; changing fog density also changes 626/626. Both keep
  the original >100 requirement. The full native GPU fixture passes with
  graphics validation, including beasts, portal clipping/parallax, submerged
  red views, all fourteen lift stops and the terminal conclusion.

The three puzzle tests use ordinary input from their arrival lifts, including
body switching, aiming, shots, carrying and lift travel. They do not place
operators, cargo or fold ends directly. Mailroom/Atrium jump hardening also
passes natively. Final integrated native/web validation is recorded above
and in notes/foldline.md, including the latest audio and silk catch regression.
Human first-time solves, solve time, live co-op over the Internet and sound
balance remain unmeasured.
The campaign's 45-75 minute target is a hypothesis.
