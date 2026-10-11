# Underfoot

A puzzle for **one or two**. Two hooded keepers cross a crypt floor laid over
a pit, and there is never quite enough floor. Lift a loose slab, the kind with
an eye carved in it, lay it over the pit, and get both keepers to a lantern
each. Something down there watches.

Play at <https://tide-engine.dev/prototypes/underfoot/>. Alone, you work both
keepers and swap between them. With a friend, choose **Host a friend**, send
the invite link or the six-character code, and each of you works your own.

## Playing

| Action | Keyboard |
| --- | --- |
| Walk, and push a coffin | WASD or the arrow keys |
| Lift the loose slab that way, or lay the one you carry | Shift or Space with a direction |
| The same, on whatever you face | E |
| Swap keepers (playing alone) | Tab or Q |
| Undo, as far back as 256 moves | Z or Backspace |
| Start the puzzle over (undo takes it back) | R |
| Hints | H |
| Next puzzle, once this one is lit | Enter |
| Any puzzle | Page Up, Page Down, or **Puzzles** |
| The room's menu | Escape |

On a phone, or once a finger touches the screen, there is a pad of four
arrows; **Use hands** turns the arrows into hands.

## The rules

- A keeper carries one thing at a time, and only lifts while standing on set
  floor (or a wedged coffin): nobody pulls up a slab from a loose one.
- Loose slabs are rounded and paler. Ice, plates and portals can be loose too,
  and then they lift and lay like any slab.
- A coffin pushed over the pit wedges there, and is floor for good.
- Ice carries whatever steps on it until something stops it. A keeper stops at
  an edge; a coffin goes over it and down.
- A portal sends whatever arrives to its twin, if the twin is laid and free.
  A portal in someone's hand leaves the other one plain ground.
- A plate under a keeper or a coffin opens its gates. A move keeps the gates it
  began with, so a step from a plate into its own gate goes through.

## The puzzles

Eighteen, in six chapters of three: loose slabs, coffins, plates and gates,
ice, portals, and everything at once. Six short ones show a piece each: the
first two, and the first of the coffin, plate, ice and portal chapters. The
other twelve were found by search and chosen by hand. Every one is solved by
the solver: the count a solved puzzle shows beside yours is
the fewest moves there are, and the tests hold the game to it. The longest
takes 94.

A hint has three steps. The first is a nudge. The second says where the
shortest way ends and how much work it is. The third gives its opening moves,
naming cells by the letters and numbers the board shows while a hint is up.

What you have lit, your fewest moves and the puzzle you left on are kept on
your machine, so a later sitting starts where the last one stopped.

## Building

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target underfoot underfoot_tests
build/debug/bin/underfoot_tests
build/debug/bin/underfoot
```

The web page is `underfoot_page` in the `web-debug` or `web-release` presets,
which stages it for the site at `docs/static/prototypes/underfoot/`.

`art/make_art.py` makes every picture with numpy and Pillow, and `sfx/make_sounds.py` every sound.

`tools/solve.c` is the solver and the search that found the puzzles, and reads
`levels.tide` as it is; `tools/hints.py` writes each puzzle's second and third
hints from what it finds. Both are for whoever changes a board, and neither is
part of the game. `notes/underfoot.md` has the contracts and what was checked.
