# FOLDLINE prototype

Added on 2026-10-12 at the owner's request: an original 3D, first-person,
Portal 2 cooperative puzzle inspiration with freedom to add a twist and an
emphasis on appearance. The owner explicitly required portals to render the
space inside them. Other repository games' source was not used as a reference;
implementation follows Tide's public documentation and subsystem contracts.

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

**The watcher** (`Watcher` and `Haunt` in `play.tide`, the owner's idea: a
figure that goes away when approached or stared at, and sometimes doesn't): it
is match state, so both players see the same one. After 25 to 60 seconds it
stands at one of six spots nobody is within 7.5 m of, and comes and goes in
the middle of 0.22 seconds with every ceiling lamp out (`Sight.lamps`). It
leaves when an operator comes within 4.6 m, after 2.4 s of being looked at
within 18 m, or after 28 s. One in four is bold: it stays when approached and
at 2.7 m goes for that operator, standing in their face for 0.75 s; that
player's picture jolts and reddens (`Lens.scare`). Nothing else happens to
them. It's the zombie skin on the operators' model. No sound is built.

**Obstacles:** the planters, the crates and the locker are `Obstacle` boxes in
`play.tide` that `Confine` pushes operators and the cube out of (`Around`),
and `Dress` draws the planters from the same list. Until then nothing stopped
a body walking into them, and the camera inside a planter read as Z-fighting
to the owner. With the watcher's state in the world, the replay hash is
`2444141639418552689`, natively and under WASI.

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
