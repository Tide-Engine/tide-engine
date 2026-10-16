# GRAVEWAKE

`prototypes/gravewake/`: a first-person cooperative zombie survival game for
one to four, requested on 2026-10-16. The catalog and staging contract is
`notes/private-prototypes.md`; this note is the game's. How to play, the map
and the files are in the game's `README.md`.

## What the owner asked for

"New prototype, medieval zombie game; I guess on the vibes of Black Ops 2
zombies but swords, bows, crossbows, etc. We should include A LOT of them and
it should be 3D first person. It should be a polished game." Later: software
renderers being slow is fine; the owner tests performance on their machine.

Everything else is Claude's pick and Provisional: the name, the village of
Ashwick and its layout, the economy and every number, the four survivors, the
weapon list, the brews, the art direction and the controls.

## How it plays

Round-based survival in the Black Ops 2 mould, with medieval things in place
of guns and perks:

- **Points.** 10 a hit, 20 a hit to the head, 50 a kill, 100 a kill to the
  head; burning and bleeding give none. Nailing boards back pays 10 a board, 400 a round at most. Going down
  costs 5%. Points buy the doors, rubble and iron bars between Ashwick's seven
  areas (750 to 1250), weapons off the walls (500 to 2000, ammunition half
  that, 4500 for a forged one's), the Pilgrim's Chest (950), the Runeforge
  (5000), six brews (500 to 4000, four at most, only once the keep's beacon is
  lit, Second Wind always), fire pots (250 for four) and grave bells (1000 for
  two).
- **Rounds.** 6, 8, 13, 18 of the dead, then `24 * (0.6 + round / 10)`, each
  times `1 + 0.6` per extra player; at most 24 up at once. Health 150, +100 a
  round to round nine, then ×1.1 a round. A spawn every
  `max(0.35, 2 * 0.95^(round-1)) / (1 + 0.3 per extra player)` seconds, at one
  of the four open spawn points nearest a random standing survivor. Runners
  from round three (12% more a round, 92% at most), sprinters among them from
  round nine. Nine seconds between rounds.
- **The dead** rise out of graves and the ground (1.8 s) or come to a
  window, tear off its six boards (1.1 s each) and climb in (1.5 s). Hunting,
  they walk a flow field to the nearest standing survivor and claw (50 a
  hit, 1.1 s between). Heads take more. Elements: fire burns for 3 s, frost
  slows for 3 s and a second frost blow while slowed freezes solid for 1.2 s,
  lightning stuns for 0.6 s (the Staff of Storms chains), bleeding ticks for
  3 s, a blast bursts round the hit; burning and bleeding pay no points and
  show no hit marker until they kill.
- **Melee, after Dying Light** (the owner, 2026-10-16: "maybe we can get
  inspired by dying light's melee combat, including the kicking and them
  tumbling over"). F kicks (0.55 s, a fifth of the stamina bar): up to three
  of the dead within 1.9 m in front go over backwards (25 damage, thrown back
  at 6 m/s); one already down is stamped on, on the head, for
  `220 + 0.4 × the round's health`. A heavy blow from something blunt, swept
  wide or two-handed also takes them off their feet. One thrown back knocks
  over the next it falls into. Down (`ZombieState.Fallen`), one goes over
  with the knockback clip (0.83 s), lies 1.3 to 2.5 s and gets up with UAL2's
  `LayToIdle` (1.53 s); lying, it takes half as much again from every blow,
  and its head, chest and hips (`Body`) are where the lying body has them, for
  blows and missiles alike. Killed while down it stays down; killed by a
  blow that knocks down, it's thrown over as it dies. One tearing at a window
  is knocked back from it instead, and starts on the board again.
- **Headshots** are their own thing: a gold hit marker (red and bigger for a
  kill), a crack every time, a higher tick to the one who hit, HEADSHOT over
  the crosshair for a kill, and the head comes off (the head bone scaled to
  nothing, a burst of blood and bone, the neck spraying a moment).
- **Down and out.** At 0 health a survivor is downed for 30 s and fights on
  from the ground: with the weapon in hand if it shoots, or a last stand's
  hand crossbow of 12 bolts if it doesn't (`LastStand`, in the third slot,
  put away again by `StandUp`). They're raised by a friend holding use for 3 s.
  Going down with Packmule loses the second weapon (the one not in hand). Alone, down
  is the end unless they drank Second Wind Cider (a self-revive, 500 alone,
  1500 with others). Who bleeds out comes back at the next round with what
  they started with and the points they had. When nobody is left standing,
  the game is over; use (or the menu's New game) starts again from round one.
- **Weapons.** 33 and bare hands (`weapons.tide`): 18 melee (slash, chop,
  thrust, or a wide sweep; a heavy blow on right mouse), four bows (hold to
  draw, let go to loose), four crossbows, a handgonne, three staffs and two
  thrown. A survivor carries one (two with Packmule Stout) and starts with
  the hand crossbow; a weapon bought or taken replaces the one in hand. The
  stab and the kick are always to hand (the owner, 2026-10-16: "maybe we just
  have one weapon and we need to pick between melee or not? we already have a
  stab and the kick so we dont need another melee"). 11 are on walls; the
  chest gives any but the hand crossbow (the rusty sword is its dud). After three uses the chest may show a skull and fly to another of its
  five places. The Runeforge renames a weapon and multiplies its damage (×3
  melee, ×2 staffs, ×2.5 the rest), its magazine (×1.5) and reserve (×1.6),
  and gives it an element. V stabs with a dagger whatever's in hand; G throws
  a fire pot; Q rings a grave bell, which draws the dead within 35 m of path
  for 7 s and then bursts.
- **Power-ups**, at most four a round (2.8% a kill), in a fixed rotation with
  random skips: Full Quivers (ammunition), Death's Touch (one-hit kills,
  30 s), Double Gold (30 s), Holy Fire (every one of the dead, +400 each), the
  carpenter (every board back, +200 each).

## Contracts

- **The match** (`game.tide`, `rounds.tide`, `zombies.tide`, `hands.tide`,
  `missiles.tide`, `shrines.tide`, `ashwick.tide`) is deterministic and
  ordered by `enum Tick { Restart, Survivors, Hands, Missiles, Zombies, Field,
  Rounds, Late }`. `Random` is the match's; views roll their own local
  `Random` (`Fx.random`).
- **Ashwick is one function**, `MakePlan()` (`plan.tide`): the match keeps its
  solids, gates, windows, risers, stalls, areas and starts (`Ashwick`, built
  at `Spawned`), and each machine calls it again for what it draws
  (`Village`, a local singleton, because local handlers can't read the
  match's lists). Change one and both follow.
- **Collision** is boxes in 8 m buckets: circles pushed out, standing on the
  highest top within 0.45 m. A gate's box is gone once it's bought.
- **Navigation**: a 0.5 m grid, 168 × 160 cells from (-42, -38). Every six
  ticks `Wayfinding` spreads a breadth-first field from the standing
  survivors' cells, and another from stuck bells, through open zones only;
  each zombie steps downhill, or straight at its target when the path is
  clear. The soak test checks that every spawn point reaches the survivors
  with every gate open (no one of the dead hunts for two minutes).
- **Seats**: `Party` holds four seats (0 Wat, 1 Maud, 2 Godric, 3 Agnes).
  A joiner takes the first free seat, with `500 + round * 250` points after
  round one; a leaver's survivor is destroyed.
- **Commands**: `Restart` (Rooms' New game, Play, or use on the game-over
  screen) sets `Match.restarting`; the `Tick.Restart` systems clear the dead,
  missiles and drops, re-outfit the survivors and reset the match (command
  handlers can't query). `Cheat` is the development keys (F2 gold, F3 the
  beacon, F4 every gate, F5 the round's end, F6 the areas in turn).
- **Inputs**: `ready` is `[Absolute]`; the rest are held bools read with
  `.down`, look is clamped to half a radian a tick. Mouse look is read only
  while the cursor is locked.
- **What's local**: the camera (`Me`), the plan's copy and gate animations
  (`Village`), the kit's placements (`Kit`), poses (`local Pose pose` on
  survivors and zombies, sampled once a frame), walk clocks, hit flashes,
  voices and drop hums (`local` fields), the feed, effects and sounds.
  Hits, kills, blows, missiles and cues reach views through local handlers.

## Art

- The village is Quaternius' Medieval Village, Fantasy Props and Stylized
  Nature MegaKits (CC0), split by texture into 113 parts of 63 pieces
  (`art/kit/tools/split_kit.py`, heavy pieces decimated) with 14 textures
  shrunk for the web and darkened by the kits' occlusion (`make_textures.py`).
  Each part draws instanced with its texture.
- People: six of the dead and four survivors from Ultimate Modular Men and
  Women, recoloured in Blender (`art/people/tools/make_bodies.py`); clips from
  the men's pack and Universal Animation Library 1 and 2, retargeted onto
  both skeletons (`make_moves.py`, two catalogs, the same clips and timing).
- Weapons and items: Quaternius' Medieval Weapons and Ultimate RPG where they
  have one, the rest modelled from primitives by `make_weapons.py` and
  `make_items.py` in the same flat-coloured style. `weapons.json` holds each
  model's grip, tip and length.
- First-person arms (`art/fp`, `fp.tide`; the owner, 2026-10-16: "the weapons,
  maybe we try to find some proper animated FP weapons? your job is meh"):
  CoinCoin's first-person kit from OpenGameArt (CC BY 4.0, credited in
  `art/CREDITS.md` and on the title screen), its bow, tomahawk and knife files
  joined on one skeleton by `art/fp/tools/make_fp.py`, recoloured flat, with
  clips made on the same rig for what the kit lacks. Every weapon goes
  through it (`Grasp`): one-handed blades, axes, maces and thrown axes on the
  kit's tomahawk clips; two-handed ones on the same paths with the left hand
  added; polearms, crossbows, the handgonne, the hand crossbow, staffs,
  javelins, a pot tossed and a brew drunk on made clips; the kit's own bow
  (mirrored into the left hand), arrow, draw, hold, loose and nock; the
  kit's knife stab for V. A weapon model sits on the rig's `weapon` bone
  (`Seat`). Blows land when the clip's do (`StrikeTime` maps the act's hit
  time onto each clip's impact). Aiming a crossbow raises the arms
  (`AimShift`) instead of a clip. The old procedural fists, forearms and
  swing keyframes are gone. A survey of free first-person packs found nothing
  better that may be redistributed (Fab/Unity/Mixamo licences, itch.io behind
  a challenge); the kit is semi-stylized, a little more detailed than
  Quaternius' style.
- The bow, after the owner's note ("the bow gets in your face too, its hard
  to aim with it since you cant see anything"): the kit's bow is held to the
  left and drawn to the cheek, the crosshair clear at full draw.
- The owner's notes on the arms (2026-10-16, with screenshots: "the sword is way
  up, it should be lower"; "the crossbow is a better example, yet i dont
  understand how we are holding it"; "why did the bow become left handed"):
  - A one-handed weapon is held lower and further right than the kit's
    tomahawk, leaned out (`GraspShift`, `GraspTurn`), and a long blade is
    tipped forward in the fist (`BladeSeat`, 38 degrees; two-handed 28) so it
    reaches away from the eye instead of up across the picture.
  - The bow is no longer mirrored: it's in the right hand, as the kit has it,
    on the right of the screen.
  - Crossbows and the handgonne are held by the stock as a pistol is, the fist
    round the grip, the barrel ahead (`make_fp.py`'s X and H clips), the left
    hand under the barrel. The importer mirrors x, so the weapon bone's ahead
    is its -x (`Seat`); the barrels had pointed back at the eye.
  - Head hits follow the animation (the owner: "headshots dont follow the
    animation"): `Brace` (a match system, `Tick.Zombies` + 1) poses each of
    the dead as the picture does (`ZombiePose`, shared with `PoseZombie`) and
    keeps its head, chest and hips (`Zombie.headAt`, `chestAt`, `hipsAt`), which
    `BodyOf` gives blows and missiles. A hunched walker's head is about 1.4 m
    up and 0.35 m ahead of its feet, where the fixed point had been 1.62 m
    straight up; a fallen one's is on the ground. The pose is sampled from the
    game's assets, so the C test now loads the pack.
- Validation of the arms (2026-10-16): native debug `gravewake_test` and the
  web debug WASI test pass; the web page builds. Every weapon was looked at in
  standard-host captures (idle, mid-attack, aimed or heavy). The browser tests
  weren't run: the owner asked to stop running them for this game.
- Sounds: 144 Ogg Vorbis files from CC0 recordings (Kenney, OpenGameArt) and
  synthesis, cut and mixed by `sfx/tools/make_sfx.py`; sources in
  `sfx/CREDITS.md`. Buses Effects, Music and Ambience under a master limiter
  (`audio Limit`, ceiling 0.86).
- Thumbnail: a two-process `--players 2` run, frame 960, Maud in the square
  with the dead coming through a window (`docs/static/prototypes/gravewake/`).
- Late polish (2026-10-16): the wall torches got their models (the flames had
  burned in mid-air), the chapel sells grave bells (Q's bell existed but
  nothing gave one), and the Runeforge's bleed and a second frost blow's
  freeze do something (bleed did nothing beyond damage; nothing set
  `frozen`).
- The map, after the owner's first play (2026-10-16: "doors are rotated weird
  or sections have holes that make no sense"):
  - The kit's door (`Door_4_Round`) is hinged at its origin and spans 1.1 m
    to its left (the importer mirrors x), so a gate's leaf goes at
    `gate.at + along * 0.55`; it had sat beside its arch, half over the wall,
    and from the far side the arch looked empty. Doors and iron gates turn
    about a hinge as they open (`Put.swing`, `Put.pivot`), into the gate's far
    side; the iron gates had risen 3.3 m and hung in the air.
  - House doorways that aren't gates have a shut door (`ShutDoor`), and every
    window that isn't a barricade has the kit's frame and glass (a quarter of
    them lit warm, `Put.glow` read by `KitGlass`) or closed shutters: they had
    been dark holes into the houses' empty insides.
  - Free-standing walls (`StoneWall`) had their two pieces back to front, so
    both faces showed plaster; both now show stone.
  - Two real holes, on the smithy's north side: (-12..-10, -14) led out of the
    village, and (10..12, -14) round the stable door into the stables. Walls
    close both, and the pocket behind the second.
- F10 (development) puts the next weapon in hand, to look at each in turn.
- The owner's next notes (2026-10-16): an invisible collider in the middle of
  the square (the well had a box and no model: the square now has a modelled
  well, `art/items` `well`), and props you walked through (stalls, the cart,
  barrels, crates, benches, chairs, the anvil and the weapon stand now have
  boxes from their kit footprints, `PlaceSolid`). The test also found the
  tavern's north-east window landing the dead in the bar's barrels, where
  they stuck for good; the barrels moved, and the test now checks every
  window's landing and every grave against the solids.
- Validation of these (2026-10-16): native debug `gravewake_test` and the
  web debug WASI test pass, with the new checks (window landings and graves
  clear of solids; a kick knocks one down, a stamp lands, it gets up; a
  maul's heavy blow knocks one down); `gravewake_play_webgl` (330 s) and
  `_webgpu` (273 s) pass. Looked at in standard-host captures: every gate
  shut and open from both sides, the eight areas in four directions, the
  well, the bell stall, the kick, a two-player fight with knockdowns.

## Rendering

- The scene draws into a multisampled `Rgba16f` window target and `Develop`
  brings it to the window with ACES, bloom (quarter-size bright pass and
  blur), the hurt and downed reds and the game-over fade.
- Light: a moon with a 2048-texel orthographic shadow map following the
  camera (snapped to its texels), a sky dome, fog, and up to 28 point lamps
  nearest the camera: torches on brackets (`WallTorch`), the tavern's
  chandelier, the chapel's candles, the beacon's braziers once lit, the
  Runeforge at work, the chest's beam, fireballs and burning dead.
- Kit placements are culled by distance each frame. The first-person arms
  draw after a depth clear, hung from the eye (`FpEye`: 0.15 m lower for the
  made clips, eased between), with sway, bob, a lowered raise, a sprint carry
  and a shot's recoil on top of the clips.
- Behind the title, the camera circles the square at 6 m round (-1, -1),
  clear of the market's cart (it used to pass through its canopy).

## Tests and validation

- `gravewake_test` (native and WASI, `test/test.c`): two players through the
  real input: round one held with swords (kills, points, torn boards), the
  tavern's door, a wall weapon, the chest, the keep's bars and the beacon,
  Ironhide, the smithy and the Runeforge, a downed friend raised, a crossbow
  bolt into the dead, the chapel's rubble and grave bells (bought, one rung,
  the lure field reaching it), a bleeding wound wearing one down, a forged
  Falchion's first blow slowing and its second freezing, then both holding the
  square with every gate
  open for three rounds (every one of the dead reaches them within two
  minutes), then the match from a snapshot and through its packed bytes
  hashes the same.
- `gravewake_play_webgl` / `_webgpu` (`test/browser.mjs`, label
  `private-prototype`, `webgpu`, run serially): the page in headless Chrome
  with frame control and `checkGL`: the title, Play, a swing, the development
  keys, the crossbow, then a walk through all six other areas, each a
  different picture, with no runtime, shader or GPU errors. Software WebGL is
  slow on this page, so the test's frames have per-frame deadlines and a
  900 s limit.
- The browser test replaces `requestPointerLock` with a promise that never
  settles: while it ran with the real one, the owner's mouse was held twice
  (2026-10-16), which stopped when the test's Chrome was killed.
- Validation (2026-10-16): native debug (configured with
  `TIDE_PRIVATE_PROTOTYPES=ON`) builds `gravewake` and `gravewake_test` with
  no warnings and `gravewake_test` passes; web debug builds `gravewake_page`
  and passes all three (`gravewake_test` under WASI 13 s,
  `gravewake_play_webgl` 398 s on SwiftShader, `gravewake_play_webgpu`
  300 s), with Node 24.21.0. The docs production build, `svelte-check` and
  the 19 analytics/admin/production HTTP checks pass. Looks were checked from
  standard-host captures: every area, the torches, the bell stall, the title's
  whole circle, a two-player run. The full native and web suites were not
  run (only this game, its catalog entry and notes changed). Not run:
  Linux/macOS/MinGW, Android, sanitizers, a live Internet room. No CI.

## Gaps and engine notes

- No touch controls: a phone can open the page but not play.
- Not measured: frame cost on a real GPU in a browser (the owner is testing
  it), live play over the Internet with four, gamepad feel, host migration
  mid-round, balance past round ten.
- What the engine made awkward, worked round in the game:
  - tidec emits an unused `const int32_t` temporary for an int literal given
    to a float field of a frag's values struct (`Skin { eyes = 0 }`), which
    `-Werror` rejects. `people.tide` writes `0.0`.
  - No empty `Map` literal: `mut Map<string, int> pieceOf = default;`.
  - A draw with a frag can't be made in a helper function, so drawing code
    lives in views.
  - Queries can't be passed to functions: systems copy the dead into a
    `List<Body>` each tick for strikes, missiles and bursts.
  - Command handlers can't query, so `Restart` sets a flag that systems act
    on next tick.
  - A system with `mut Survivor` can't change another survivor: reviving
    sends `Helped` to the downed one.
  - `Spawn` takes a component literal, not a local value, so `TakeSeat`
    repeats `Outfitted()`'s fields.
  - A singleton can't be assigned whole: `ResetMatch` resets field by field.
  - Local handlers can't read the match's lists (the `Village` copy) or use
    `with ownership` (they compare seats).
  - Animation catalog members keep the clips' underscores (`idle_Sword`,
    `hitRecieve_2`).
  - No `quaternion.FromToRotation`: `held.tide` has its own `FromTo`.
  - No custom fonts: the title is the built-in bold face.
  - The Rooms package's views draw before the game's `Develop`, so the
    title's words are drawn by the game's own `TitleCard` view on the HUD
    layer instead of in `TitlePage`.
  - The web pack carries each texture raw and block-compressed, and globs
    pack whole folders, so the page is about 21 MB after pruning the kit,
    decimating trees, Ogg sounds and three 256-texel textures.
