# SUNDAY SUPPER

`prototypes/sunday-supper/`: a first-person cooperative horror story for one
or two, requested on 2026-10-16. The catalog and staging contract is
`notes/private-prototypes.md`; this note is the game's.

## What the owner asked for

- A co-op horror story in the vein of Fears to Fathom: Scratch Creek (the car
  dies, the travellers stay somewhere creepy whose hosts are planning
  something), without copying it. Scratch Creek is a cult in a town; this is
  one family on one farm.
- The owner leads nothing here by choice: "you be the one sort of guiding that
  aspect". Keep it simple, atmosphere first; get a playable slice in front of
  the owner, who expects clearer ideas once they've played it.
- Keep the two players together most of the time and the experience shared:
  separations short and rare, the escape (later) done together.
- Humans and characters from existing free models (Kenney, OpenGameArt and the
  like), "for the sake that I want them to look good". Blender may be used.
- Rejected on the way, not to be brought back unasked: an SCP basis, players
  seeing different things, the drowned-town story, the ghost-hunting show.

Everything else here is Claude's pick and Provisional: the story, the Hales,
the script, the 1998 setting, the controls, the look.

## The story

Friday: Sam and Jo's car dies on Route 9. The only light is the Hale farm.
Ruth answers the knock; Walt "heard an engine quit" two miles off and calls
Earl the tow man (no dial tone when the travellers try the phone). Earl can't
come till Monday, so they stay. Supper: Walt's grace asks to "keep" the ones
at the table; Dale comes in the back door and stares; the house rules are that
doors are locked at nine and nobody goes past the fence after dark. Ruth shows
them to the guest room and locks them in. In the night Walt walks into the
corn with a lantern, looks up at their window, and goes on to the woods;
later someone tries the guest room's door. Saturday: breakfast ("that old
latch sticks"), then the barn, where Dale has them carry feed sacks; under a
tarp is a car with Ohio plates and a child's backpack, and Dale says "Pa said
not to touch that." The slice ends there.

What the Hales are doing (planned, unwritten in the game): they keep
travellers for Sunday supper, for something in the woods behind the farm that
keeps their harvests and their health. Ambiguous until near the end, then
real. The rest of the weekend, the escape and the endings are to be written.

## How it plays

- First person, walking, running, looking; `E` uses what's under the dot, `F`
  is the phone's light, `Q` held shows the phone (always NO SERVICE; Tab is the
  GUI's focus key, which the game never sees), `1`-`3`
  answer a choice. Either traveller answers; the first to press speaks, and
  the line shows as theirs.
- Dialogue is subtitles; nobody is voiced. A look at something (the hood, the
  mailbox, the photographs, the clock, the drawer) gives that traveller a
  line of their own.
- The script waits for both travellers where it needs them together; alone,
  "both" is the one. Co-op tasks: the sacks rise only with both holding one
  (dropped if they pull more than 2.8 m apart), and the tarp takes both
  pulling within 1.5 s. Alone, one is enough for each.
- No failure state, chases or death in this slice: the horror is the house,
  the Hales and the dark.

## Contracts

- **The script** (`story.tide`, `script.tide`, `direct.tide`) is a list of
  `Step`s in the match (`Script`, built at `Spawned`), run by `Direct` each
  tick: steps that take no time follow each other within the tick (48 at
  most), others hold the script. Marks are labels; `Jump`/`Branch` find them by
  scanning. `Story.used` holds spots used since the current step began, and is
  cleared when a step ends; `Steer` runs after `Direct`, so a use is seen the
  next tick. NPC and walker jumps go through `jumped` flags, which their own
  systems turn into `Snap()` (functions can't snap).
- **The farm** is one deterministic function, `MakePlan()`: the match keeps its
  solids, doors and spots (`Farm`), and each machine calls it again for the
  meshes and props it draws. Change one, and both follow.
- **Collision** is boxes: circles pushed out of solids whose height they meet
  above a step's height, standing on the highest top within `STEP_UP`. A door's
  solid isn't there once it's over 15% open. A spot is in sight if nothing but
  the solid it belongs to is between the eye and it.
- **Inputs**: `ready` (past the menu) is `[Absolute]`; picks and use are held
  bools read with `.down`.
- **Commands**: `Restart` (Rooms' New game / Play) and `Skip` (F2-F6, a
  development shortcut that jumps the script to a label and sets that part's
  lights, sky, doors and positions).

## Art

- People: Quaternius' Ultimate Modular Men and Women (CC0), Blender-prepared
  by `art/people/tools/make_people.py`, recoloured for each part. All share
  one rig (`CharacterArmature`, 62 bones) and clips; `draw.tide`'s `Moves`
  contract names the clips and bones (`[Name]` for `Idle_Neutral` and the
  dotted bone names). Sitting, bowing and looking are bone edits in model
  space on a sampled pose (`TurnBone`): heads follow the travellers, which is
  most of what makes the Hales unsettling. Material colours arrive as glTF
  base colour factors, which the importer keeps as they are (linear), and the
  renderer works in light, so they're used as they come.
- Props: Kenney's Furniture, Nature, Survival and Car kits (CC0), unchanged.
  Each model's foot is found from its vertices at start; scales are per
  placement in the plan. Kenney's bright colours are dulled in `KitLit`, and
  green goes toward straw with `season` (the corn, the trees, the tufts).
- The farm's own walls, floors and roof are boxes with procedural materials in
  `Paint` (wallpaper, planks, siding, shingles, gravel, tile, barn boards...),
  in 1/16 m texels.
- Sounds: `sfx/make_sfx.py` (numpy), 22,050 Hz mono, from noise and tones.

## Rendering

- The picture is drawn into an `Rgba16f` target of about 270 lines, corners
  snapped to its pixels (`Snap`), and shown by `Develop`: light brought under 1,
  30 shades a channel with a 4x4 pattern, grain, a vignette, and the story's
  fade.
- Light: the sky and the sun or moon by `Story.daylight` (dusk 0.24, night
  0.02, morning 0.85), dimmed indoors by `InsideAt`, which the mesh decides per
  face from what the face looks out on; the house's lamps by group
  (`Story.lights`), each lighting only its own room's box, since nothing else
  stops light through walls; the car's hazards; Walt's lantern at his wrist; the
  other traveller's phone as a spotlight; and this machine's phone light, which
  casts shadows from a 512-pixel depth image of everything near.
- Windows are see-through panes; seen from outside, a room whose lamp is lit
  glows through them, less fogged than what they light.
- Kit models are culled by distance per kind (corn 42 m, trees 150 m) and
  behind the eye, every frame. Corn and trees are `[LOD]`.
- Behind the title, before anyone plays, the camera holds a shot of the car.

## Tests and validation

- `sunday_supper_test` (native and WASI): two players play the whole slice
  through the real input and spots: the hood, the knock and Ruth's greeting,
  inside, seating, the locked guest room door, both into bed, the morning,
  three sacks carried together, the tarp pulled together, the end; then the
  match from a snapshot and through its packed bytes hashes the same.
- `sunday_supper_play_webgl` / `_webgpu` (`test/browser.mjs`, label
  `private-prototype`, `webgpu`): the page in headless Chrome with frame
  control and `checkGL`: title, Play, road, porch, supper, the night by phone
  light, the barn, a picture of each, and no runtime, shader or GPU errors.
- 2026-10-16: native debug build and both tests pass; web debug build, the
  WASI test and both browser tests pass. A headless `--players 2` run was
  checked by eye: each machine draws the other traveller and their light.
- Not measured: playtime (the slice is about 15-20 minutes), live co-op over a
  real network, sound balance, how scary any of it is.

## Gaps and engine notes

- tidec generated invalid C for `foreach (var p in kit.placed[model])` where
  `placed` is a `List<List<Placed>>` in a local singleton ("cannot take the
  address of an rvalue of type 'tide_list'"). `placing.tide` indexes instead.
  Not fixed in the compiler yet.
- No proximity voice chat (not built in the engine); players use their own.
- The tarp is a box, the sacks are boxes. No footsteps for the Hales, no
  voice, no lip or eye movement.
- Rooms' title page is the package's own; no title art.
- Interiors in the morning are flat; the yard's trees are Kenney's toy-like
  shapes.
