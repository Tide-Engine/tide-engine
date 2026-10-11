# Underfoot

The owner asked on 2026-10-11 for "another prototype, some mashup of puzzle
games", polished, with free rein over what it is, and a challenge: "we are
pretty good at puzzles". The request came with a board of 25 favourite puzzle
games. Everything below that request is Claude's pick inside it: the game, its
rules, its look, its puzzles and how they were made. None of it is an engine
or language requirement.

## What it is

A puzzle for one or two, in `prototypes/underfoot/`. Two pavers stand on a
yard of stone over nothing, each must reach a lantern, and the floor itself is
what they carry. It borrows one idea each from several of those games:

- Void Stranger: a paver lifts the loose stone ahead and lays it elsewhere.
- Sokoban, by way of Isles of Sea and Sky: a crate pushed over nothing sinks
  and is ground for good.
- Gentoo Rescue: ice carries whatever steps on it until something stops it.
- Portal: a portal sends whatever arrives to its twin. Here the portals are
  loose stones too, so the far end goes wherever someone lays it.
- The Talos Principle: plates under a paver or a crate open gates, and one
  player holds a way open for the other.

Inside Out (`notes/inside-out.md`) needs exactly two players. This one takes
one or two: both pavers are always on the board, a player alone works both and
swaps between them, and two players each work their own.

## Rules

`game.tide` has them, as functions over a `Board` (everything a move changes)
and a `Layout` (what never does). A board is 9 by 7 cells with a border of
wall, so 7 by 5 to play on. An action is one of eight: walk north, east, south
or west, or use hands that way.

- **Hands.** With an empty hand, lift the loose ground ahead: stone, ice, a
  portal or a plate that isn't set, with no paver or crate on it. With a full
  hand, lay it over nothing. One thing in hand at a time.
- **Firm footing.** A paver lifts only while standing on set ground or a
  sunken crate, never from a loose stone. Laying needs no footing. This is the
  rule the game turns on. Without it one paver with two loose stones walks
  anywhere over nothing, leapfrogging them, and the first puzzles the search
  found were forty lifts of exactly that. With it a laid stone can strand, a
  bridge is taken up from the bank it leads to, and a second pair of hands on
  the other bank is worth something.
- **Crates.** Walking into a crate pushes it one cell if nothing is behind it.
  Over nothing it sinks: the cell becomes rubble, which is firm and stays.
- **Ice.** A paver or a crate that arrives on ice goes on the same way until
  the next cell is closed to it. A paver stops short of nothing; a crate goes
  over and sinks. A sliding paver pushes nothing.
- **Portals.** P and Q are one pair. Whatever arrives on one is on the other
  at once, if the other is laid and nothing stands on it. A slide ends there.
- **Plates and gates.** A gate is shut unless a plate of its kind is under a
  paver or a crate. Gates are read once, as a move begins, so a step from a
  plate into its own gate goes through, and whatever is in a gate that shuts
  can leave. Nothing is lifted or laid in a gate's cell.
- **Done** when both pavers stand on lanterns. Nothing moves after.

Moves are commands (`Act`, `Undo`, `Reset`, `Next`, `ChooseLevel`). A `serial`
that counts puzzles opened drops commands sent to a board that is gone. Undo
keeps the last 256 boards; starting over is itself a move, so undo takes it
back. Any player seated may undo, start over or choose a puzzle, whoever's
move it was. A player who leaves keeps the seat, and the one left works both
pavers meanwhile.

## The puzzles, and how they were made

`levels.tide` has eighteen, as text: seven rows of nine cells, two characters
a cell, ground then what stands on it. Six chapters of three: loose stones,
crates, plates and gates, ice, portals, everything at once.

- **Six were made by hand,** to show one piece each: the first two, and the
  openers of the crate, plate, ice and portal chapters, 8 to 20 moves. The
  last chapter has no opener.
- **The other twelve were found by search.** `tools/solve.c forge` climbs from
  random small boards towards long shortest solutions, under caps on loose
  stones, crates, special pieces and empty cells, a floor on how many
  positions a board has (a long way through few positions is a corridor), and
  a count of times each paver must use hands. It then turns each cell to
  something plainer wherever the puzzle is no easier for it. Seventy-one boards
  came out of forty-two runs. The twelve were picked by reading each one's
  solution, told step by step (`solve -t`), for length a person will bear (29
  to 94 moves) and for using its chapter's piece. Each was then solved again
  with that piece turned to plain stone: every chosen board is harder or
  impossible without it, except Thaw, which is one move shorter. Thaw is kept
  because its solution slides twelve times on ice it carries about.
- **Par.** `LevelPar` is the fewest moves that solve each board, found by
  breadth-first search over every position. A solved puzzle shows it beside
  the player's count.
- **Hints** are three steps. The first is a sentence written by hand. The
  second and third are facts of the shortest way: where it ends and how many
  lifts and pushes it takes, then its opening six actions, with cells named as
  the board names them while a hint is up (A to G, 1 to 5). `tools/hints.py`
  writes both from the solver's output, so they are made again whenever a
  board changes.

| | Puzzle | Fewest moves | Positions searched |
| ---: | --- | ---: | ---: |
| 1 | One loose stone | 14 | 286 |
| 2 | Twice over | 20 | 312 |
| 3 | Four hands | 29 | 630 |
| 4 | Ballast | 16 | 699 |
| 5 | Dead weight | 30 | 6,421 |
| 6 | Sunk cost | 42 | 23,066 |
| 7 | A plate is a stone | 16 | 356 |
| 8 | Doorstop | 49 | 2,035 |
| 9 | Turnstile | 79 | 13,112 |
| 10 | Black ice | 8 | 31 |
| 11 | Cold feet | 49 | 9,579 |
| 12 | Thaw | 70 | 31,514 |
| 13 | Carry-on | 12 | 268 |
| 14 | Short cut | 34 | 28,153 |
| 15 | Revolving door | 58 | 18,170 |
| 16 | Forwarding address | 64 | 6,336 |
| 17 | Lost property | 72 | 68,843 |
| 18 | Underfoot | 94 | 59,687 |

Fewest moves is a proof. How hard a puzzle is for a person is not measured:
nobody has played these but the solver. The order is a guess from move counts
and from reading the solutions.

## Presentation

The look is a tiled courtyard over dark water at night, cut from flat colour
(the owner's third word on it, 2026-10-11, after two passes that were
"generic" and "halfway": "make it unique, give it a theme or make it simple").
It is both: one theme, and four rules that every picture keeps, written at the
top of `art/make_art.py`:

- flat colour only, with no gradients, gloss, blur or outlines;
- one hard shadow, straight down, from whatever is loose or stands up;
- what is set in the floor is plain, and what can be carried is decorated: set
  ground is sand paving, a loose stone is a glazed tile with an eight-pointed
  star, loose ice has a snowflake and a portal a dark well;
- a short palette (sand, cream, cobalt, mustard, jade, coral, teal, plum),
  shared by `ui.tide`'s constants.

Where there is no ground there is water, which is why a crate sinks and why a
tile laid there is a way across. The pavers are two tilers in straw hats,
coral and teal, drawn from the front, the back and the side. `art/atlas.png`
holds 24 sprites of 288 pixels around a 192 pixel cell, and `art/lattice.png`
the faint star pattern behind everything; `make_art.py` makes both from
shapes given as distances, with numpy and Pillow, and nothing is drawn by hand
or borrowed. Plates, gates and what turns in a portal are white in the atlas
and coloured as they are drawn. `paint.tide` draws the rest with the same
rules: the courtyard's wall as one flat block, water with two short lines of
light a cell, and the HUD's buttons and panels flat on a hard shadow. The
words players read say tile, floor and water; the code still says stone and
ground.

The rim of wall round a board is a third of a cell thick, so the cells are as
large as the screen allows. Words along the top sit on a plain bar.

A plate's disc brightens when held down and shows round a crate. What is in
a portal turns while it leads somewhere and is dull and still while its twin
is in a hand. Corner marks in the paver's colour show the cell its hands would act
on. A paver on its lantern holds it, lit. Positions ease towards the match's
and pavers hop as they walk; a jump through a portal snaps. A lifted or laid
stone flies between the cell and the paver's hands, and a crate that sinks is
seen going. When hands are refused, a line at the foot of the screen says why
(set stone, something standing on it, loose footing, full hands): `Refusal`
in `ui.tide` works it out from the board, for the player whose paver it was.
The board glides up to leave room for a hint or the solved card instead of
hiding under either. Reduced motion stops the easing
and the decoration. `ui.tide` has the title (Play alone, Host a friend, Join a
friend, over the Rooms package's pages), the roster of who works which paver
and what each carries, the hint panel (the board draws smaller above it), the
solved card, the list of puzzles and the rules. On a narrow screen, or once a
finger touches, there is a pad of four arrows and a switch that makes them
hands. `sound.tide` plays thirteen clips that `sfx/make_sounds.py` makes from
nothing. None of this is in the match or its hash.

`progress.tide` keeps what this machine's player has lit, their fewest moves
and the puzzle they left on, as one line of numbers in
`Application.persistentDataPath` (`underfoot.txt`, with `appId`
`dev.tideengine.underfoot`), written when it changes and flushed for the
browser. The first time a machine plays a match of its own that nothing has
touched, it opens the puzzle that was left. A friend's room is never moved.
A file that is anything but those numbers is no progress.

## Tests

`underfoot_tests` (`test/simulation.c`, `test/campaign.c`), native and WASI:

- Ten cases lay out small boards and say what real `Act` commands must do to
  them, worked out by hand: footing, one thing in hand, sinking, each of ice's
  stops, a portal's free and taken twin and one in hand, a gate read as a move
  begins, lanterns and `Next`, who may work which paver alone and together and
  after leaving, undo through a reset and its 256 boards, and a snapshot,
  packed bytes and a replay giving one hash.
- `underfoot_the_game_and_the_search_model_agree_move_for_move`: the search
  runs on a second statement of the rules in C (`test/model.h`), because a
  Tide function is not something the C host can call. So the two are held
  together: 2,500 random actions on each of the eighteen puzzles and up to
  18,000 on each of five boards crowded with every piece, made in both and compared
  after every one, with resets and undos among them.
- `underfoot_every_puzzle_is_solved_in_exactly_the_moves_it_says`: each
  puzzle is searched from the board the game loads; the fewest moves must be
  `LevelPar`'s, and that way is then played through the commands, undone and
  started over.

`underfoot_play_webgl` and `underfoot_play_webgpu` (`test/browser.mjs`, web
builds with Node and a browser) run the page in headless Chrome or Edge, muted,
frame by frame. One player alone walks both pavers and undoes. Then two
windows join through a local relay and unordered, unreliable WebRTC: each
player's keys move only their own paver, undo and starting over are shared,
the two solve the first puzzle in its fourteen moves and open the next, and a
finger on the pad walks a paver on a phone-sized screen. It reads only what
players read: crops of the puzzle's name and move count, which is why the
stars leave the top of the screen still. The harness (process, DevTools pipe,
frame control) is Inside Out's three helper files, imported from
`prototypes/inside-out/test/`; removing that prototype means moving them.

## What was learned about the engine

Three things got in the way, each worked round in the game:

- **The GUI's focus takes the play keys.** The HUD is `GUI.Control` buttons,
  and the match reads no devices, so the arrows start the GUI's focus, and
  Space or Enter then press whichever button has it. Seen before the fix: a
  click on Undo, an arrow and Space opened the list of puzzles. Underfoot
  calls `GUI.ClaimKeyboard()` while it is played. That hides Escape from the
  Rooms package's `Pages`, so `YardControls` opens the room's menu itself, by
  setting `Lobby.page` and `Lobby.resume` as `Pages` does. Inside Out has
  arrows, Enter and HUD buttons with no claim, and likely has the same fault;
  that was not checked.
- **A run with no window writes real saves.** `--script` and `--capture` runs
  put `underfoot.txt` in the owner's `AppData/LocalLow`, as play would. The
  ones made while building this were deleted by hand each time.
- **The C test host cannot call a Tide function** (they are `TIDE_HELPER`), so
  a search over the game's own rules needs them stated twice and a test to
  hold the two together.

## Validation, 2026-10-11

On this Windows machine, with `TIDE_PRIVATE_PROTOTYPES=ON`:

- Native debug: `underfoot` and `underfoot_tests` build without warnings;
  12 of 12 cases pass.
- Web debug: the same 12 pass under WASI. The browser play test passes on
  WebGL (about 20 s) and WebGPU (about 17 s) with no runtime, shader or
  browser errors.
- Native release: 12 of 12. Web release: the page builds without warnings
  (2.7 MB, self-contained) and passes the browser play test on WebGL.
- Windowless native captures at 1280 by 800 and 960 by 540 and the browser's
  at 1280 by 720 and 390 by 844 were looked at: title, play alone and
  together, hints, the solved card, the phone layout. The catalog thumbnail is
  one of them, the last puzzle a move in.
- Saved progress: a windowless run solved the first puzzle and quit, and the
  next run opened on the second.
- The site: `npm run check` (no errors or warnings), `npm run build`, and the
  analytics and admin (18), production prototype HTTP (1) and SEO (3) tests,
  with the page staged and `underfoot` in the catalog and its three patterns.

Not run here: the full native and web suites (the change adds a game and its
tests, a catalog card and site patterns), Linux, macOS, Android and sanitizers,
real phones, two machines across a real network, and anyone actually playing.
The sounds were made and are played but were never heard: every run here was
muted.

Two polish passes followed on 2026-10-11. The owner first asked that the
visuals not get in the way of playing: the first build drew rock and nothing
in nearly the same dark blue inside a full ring of rock cells, so where a
stone could go was hard to read. That pass separated the three, thinned the
rim, and added the refusal lines, flying stones and room for the solved card
(`PuzzleCue` gained `action`). The owner then said it still looked bad, the
bright loose tiles out of place, and to give it the best that could be done.
The second pass made one paving stone for all ground and replaced flat
shapes with generated, shaded sprites over a painted sky. The owner found that
generic and distracting too, and the third pass is the look described under
Presentation, which replaced it whole.

After the third pass: native debug and web debug build without warnings; the
12 cases pass natively and under WASI; the browser play test passes on WebGL
and WebGPU. It found two things the new look had broken in what it reads, the
celebration's sparks and the slab's shadow drifting into the header, and both
now stay below it; the header's bar is also what makes two machines draw it
alike. Captures at
1280 by 800, 1280 by 720, 960 by 540 and the browser's 390 by 844 were looked
at, and the thumbnail was taken again. Native release was not rebuilt.

The page is public at `/prototypes/underfoot/` once the Docs workflow next
deploys the site; that workflow was not started. Catalog and staging are in
`notes/private-prototypes.md`.
