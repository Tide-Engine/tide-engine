# Inside Out

A cooperative puzzle game for **two friends**, designed for native desktop
play. The browser version is a convenient way to preview the same game in
two tabs without installing a player.
You are two small test robots packed in a small box, itself inside another
room, on a model that sits on a table. Work your way out.
Push rooms with your partner inside, fit them through tight passages, and
move boxes into their own interiors. Repeated views of a room share one
set of batteries, boxes and people.

The 3D rooms keep a fixed, gently tilted view of a painted model: pale
floors, matte painted boxes, soft shadows, marks that glow, and a lens that
keeps your room sharp while the larger rooms around it fall out of focus,
with quiet music and soft wooden, brass and robot sounds.
There are no threats or jumpscares.

Choose **Host a room**, then share **Copy invite** or the six-character room
code. Opening a browser invite joins that room directly; **Join a friend**
also accepts its code. Both players must connect before either can move.
Use a voice call or chat alongside the game to discuss plans. The current
review build runs locally as described below; the public prototype at
<https://tide-engine.dev/prototypes/inside-out/> is the earlier deployed build.

## Playing

The scene fills the window, with the controls laid over it. Each screen
follows its own robot in one continuous scene. Step inside a
room and the camera zooms into its actual doorway. Your whole current room
stays in view, with a wider fixed rim of containing-world context around it.
You can see part of a robot standing beside your box.
The camera keeps that framing when your partner moves or leaves the screen.
Your robot walks and shrinks through the doorway; leaving grows them back
into the surrounding world. Your partner can watch that passage from outside.
The outside is your room's box, wherever it stands: a moved box changes your
surroundings, and a box inside itself puts the same room around itself. Amber and Teal keep their identity colours wherever they
travel. A room shell contains a live miniature of its shared interior;
nested and recursive preview details collapse to markers after a few layers.
Floors, walls, batteries and robots are real 3D geometry. The orthographic view
keeps the grid and movement directions familiar; there is no camera rotation.

| Action | Keyboard |
| --- | --- |
| Walk and push a battery or room | WASD or arrow keys |
| Enter a room | Walk into its doorway when the room cannot be pushed |
| Leave a room | Walk outward through an edge doorway |
| Draw, to show your partner something | Hold the left mouse button and draw |
| Undo the last shared move | Z or Backspace |
| Reset this puzzle | R |
| Open / close the menu | Escape |

On touchscreens, swipe in a direction to take one step, or turn on **Draw**
beside Undo and Restart to draw with your finger instead. Rooms move first; when a wall
or your partner stops a row, walking into a stopped box's open doorway
enters it. Directions match the screen: W is up, D right, S down and A left.
After undo or reset, release a held direction before pressing it again.

Beside a box, the game shows what your next step will do before you take
it: a yellow bar against a box you would push, a green arrow into a doorway
you would enter. There is no directional button overlay.

Drawing appears on both screens, in your robot's colour, on the same spot of
the room you drew on, wherever that room is in your partner's view. Each
segment fades away within half a second, like a short cursor trail. A click
without dragging leaves no mark or sound. Undo leaves the trail; restarting
or changing the puzzle clears it. Drawing never moves anything in the puzzle.

The game teaches through its puzzles; there are no hint panels, guide pages
or lesson cards. Settings include
master, music, effects and interface volumes, mute, reduced motion and
step previews. Settings and
what you have solved are saved on this machine; when you play together, the
room counts every puzzle either of you has solved. Escape opens the in-game
menu; your partner's match keeps running. Reset and **Back to the hall** ask
before clearing the shared moves.

## The hall

The first puzzle starts straight away. Solve it and the view pulls back out
of a model on a post: it was one exhibit in the **hall**, where you choose
what to play next. Each chapter has its own hall. A pair that has solved the
first puzzle before starts in the highest hall they have reached.

- Along the north edge stand the chapter's puzzles, each a model of its
  rooms as the puzzle starts, with its number. A green ring marks a solved
  one. In front of each post is a lane of two pads: when both robots stand
  in it, one on each pad, the pads fill with light and the view closes in on
  the model, into the puzzle. Step off while they fill to change your mind.
  Standing in a lane shows its puzzle's name.
- Play a hall's puzzles in any order. The **lift up**, at the south edge's
  right, works once every puzzle of this hall is solved: its lights count
  them. The lift down, on the left, takes you back to an earlier hall.
  Its doors close, show the chapter you are going to, and open on its hall.
- The covered post in the middle of the south edge is the hall's
  **challenge**, uncovered once the hall's puzzles are solved. A challenge is
  a small, hard puzzle with a budget of moves, shown on its post and beside
  the move count; once it is spent, nothing moves until you undo or restart.
  Each budget is the shortest solution there is. Challenges never hold up
  the lift. The first three halls have one each: **Freight** (16 moves),
  **Backstage** (21) and **Overload** (22).
- After a puzzle is solved, its name and what the solve opened are shown
  briefly, and you are back in the hall in front of its post. **Back to the
  hall** in the menu leaves a puzzle without solving it.

For playtesting, `?playtest` on the web page (or `INSIDE_OUT_PLAYTEST=1`
for a native run) opens every chapter and challenge for the room and adds
**Choose puzzle** to the menu, a list of every puzzle by chapter.

If your save predates the campaign rebuild, completion ticks for puzzles
7–13 and 15–18 are cleared. The occupied-shutter update clears earlier
ticks for puzzles 10–12 and 18, whose solutions change with normal
push-following. When **Long way home** moved from puzzle 11 to 14 and the
**Held** chapter arrived, ticks moved with their puzzles. Personal settings
and other completion ticks carry over.

The review build includes five proposed chapter materials. Press **F6** or
choose **Settings → Appearance preview** to compare painted wood, enamel and
aluminium, industrial metal, ceramic, and a finished stone/walnut exhibit
on the current puzzle. **Original appearance** restores the current game.
Each player chooses independently; the choice lasts only for this run.
The browser also accepts `?look=1` through `?look=5`. These are art studies
for review, so chapters still use the original appearance by default.

## The rules

One rule moves everything: robots, batteries and boxes.

- Whatever you walk into is pushed, and whatever is beyond it is pushed too.
  A row of any length moves as one. Nothing can be pulled.
- A wall, a raised shutter, the other robot or a clamped box stops a row. A
  robot is never pushed.
- When a row is stopped, the thing against the stop stays put, and the thing
  behind it goes into it instead, if it is a box with a doorway facing that
  way. So you get into a box only when it cannot be pushed, and so does a
  battery or a box you push at it. The one stop you can put in place and
  take away again is your partner.
- The other way round works too: push a box at a battery or another box that
  cannot move, and if your box has a doorway on that side, the stuck thing
  goes into your box through it while your box moves onto its cell.
  Whatever stands in that doorway is pushed further in. A robot or a clamped
  box is never taken in this way. When both could happen, your box going
  into the other wins.
- A clamp, a steel plate with a jaw at each corner, holds the box standing on
  it: that box never moves again, so you can always walk into it by a
  doorway facing you, and anything pushed at it stops. A box pushed onto a clamp is held
  there for good. A clamped box still goes with the room it stands in, and
  robots and batteries cross a clamp like floor.
- A moving box carries everything inside it.
- A room has one box that is its outside. Walk out through an edge door and
  you come out beside that box; so does anything you push out.
- Every room has exactly one box. A box can stand inside its own room: leave
  that room and you come out beside its box, in the same room. Two boxes can
  stand inside each other: leave one and you come out beside it, in the
  other. A room is one place, so what is in it shows in every view of it.
- Pressure plates and shutters come in two colours, each with its own
  shape: magenta with a ring and lime with a diamond. A plate is pressed
  while anything stands on it: a robot, a battery, or a box on a plate of the
  room it stands in. While any plate of a colour is pressed, in any room or
  on the table, every shutter of that colour, in every room, is open floor.
  A shutter also stays lowered while a robot, battery or room box occupies
  its cell. Push an object off it and you follow onto the slab, keeping it
  lowered. It rises and blocks entry only once its cell is empty and power
  is off. Each step uses power and occupancy from before that move, so you
  can step off a plate and push a battery through its shutter in one move.
  Plates and shutters are not goals.
- Every room is a square grid, so a box always fills the cell it stands in.
- Seat every battery in a socket, park each box on its numbered outline, and
  put both robots on goals. An outline is a pad in its box's paint; a box on
  it hides the pad, and green brackets glow at its corners once it is the
  right box. A seated battery can be moved again while you arrange things.

Shared undo takes back the last successful action, whoever made it, and
retains 128 actions. Reset restores the complete starting arrangement.
After solving, the game closes the view, says what was solved, and returns
both of you to the hall. Going into a puzzle holds its name clearly for three
seconds before it opens. The name appears only during this transition; the
playing header keeps the chapter and puzzle count. Each new arrangement is
revealed before movement begins. Restarting the same puzzle uses a short
reveal without repeating its name. Undo or restart cancels an outstanding
return. Solving the campaign's last puzzle shows its ending before the hall.
Losing a participant stops gameplay and the return; reconnecting restarts an
outstanding hold so both players have time to read it.

## Development

For native desktop play, configure and build from the repository root:

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target inside_out
build/debug/bin/inside_out.exe
```

Two native players can host and join using the same room code. To preview
locally in two browser tabs without deploying the website, run these
commands from the repository root (Node 24.16 or newer on Windows):

```powershell
cmake --preset web-release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-release --target inside_out_page
node prototypes/inside-out/serve.mjs
```

Open <http://127.0.0.1:8790/prototypes/inside-out/> in two tabs. Choose
**Host a room** in one, copy its invite, and open that URL in the other.
You can also choose **Join a friend** and enter the six-character code.
The server runs its own
local relay on port 8791 and serves the page with the isolation headers
needed by Tide. It reads the latest staged page on each reload. Ctrl+C
stops both services; optional HTTP and relay ports are the script's two
positional arguments. The server binds to this machine only.

The prototype uses Tide and the existing Rooms package. Its deterministic
match rules are in `game.tide`; the puzzles, drawn as text maps (their
legend is at the top of the file), with their chapter grouping,
are in `levels.tide`. `hall.tide` has the halls' exhibits, their lanes and
what opens them; `gallery.tide` draws the posts, models and lifts. `geometry.tide` builds the local 3D scene and `render.tide`
handles lighting, shadow mapping and screen-space ambient occlusion.
`ink.tide` has the drawing: its marks are kept in the match so both
players see them, and nothing in the puzzle reads them.
Presentation, controls and original audio are local state.
There are no new engine or runtime APIs.

Enable `TIDE_PRIVATE_PROTOTYPES` when configuring the repository. Build
`inside_out` and `inside_out_tests` with the native and web debug presets.
`inside_out_page` stages the web build into the public prototype directory.
`inside_out_play_webgl` and `inside_out_play_webgpu` test two real browser
players with muted audio. `inside_out_materials_native`,
`inside_out_materials_webgl`, and `inside_out_materials_webgpu` compare all
five material studies, check exact original restoration, and retain captures.
`inside_out_shutters_webgl` and `inside_out_shutters_webgpu` check the physical
slab, occupied push-following, vacancy closure and shared undo in two players.
The campaign test replays checked-in witnesses
through production commands; it establishes solvability, not optimality
or measured human difficulty. `inside_out_solve`, a native tool for drawing
puzzles, moves through the same Move handler: `inside_out_solve 3` finds
puzzle 3's shortest solution, `--print` shows its maps, `--alone <seat>`
asks whether one robot can do it alone, `--play "<moves>"` plays moves, and
`inside_out_solve all` tabulates every puzzle (usage at the top of
`test/solve.c`). Contracts and validation: `notes/inside-out.md`.

For puzzle design, `--no-battery-plates` removes solutions that use a battery
to power a plate, and `--keep-docked` prevents moving a box off its outline
once parked. These probes use the production movement rules. An exhausted
search can establish that the removed strategy is necessary; reaching a
search limit cannot establish impossibility.
