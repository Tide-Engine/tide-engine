# Elsewhere

A cozy first-person journey through impossible gardens, for one or two players.

**Somewhere to get a little lost. Someone to find it with.**

Walk through brass windows into gardens suspended in clouds. Wake sunlight,
stillwater and whispering wind; tend their living seeds; bring them home to
flower a shared conservatory. Your companion can wander in another garden,
visible through its window. When the garden blooms, sit together and stay.

## The playable demo

- Four spaces: a brass conservatory, the Sunwell's monumental solar rings,
  Stillwater's pond and aurora, and an orchard of rosewood pergolas and chimes.
- Real destination cameras and seamless crossings, with a cycle of garden
  connections that cannot occupy ordinary space. Trees and floating terrariums
  repeat at smaller scales.
- Three seeds with a complete restoration and flowering ending. Each source
  stays awake long enough to tend alone; partners can keep it awake together
  or explore different gardens.
- Two-player room hosting/joining, a visible companion, reconnect/leave seed
  recovery, and shared progress using Tide's normal authoritative match path.
- Original procedural art and synthesized score/ambience; moving leaves,
  water reflections, soft shadows, multisampling, restrained bloom, a field
  guide, sound control, photographs and a place to sit.

An efficient route takes about two minutes. A first visit should leave time
for exploring the windows, looking around and being together. There is no
timer, failure screen or forced departure after completion.

## Playing

WASD walks; the mouse looks. Hold Shift to walk faster. E or the left mouse
button uses a nearby source, tends a tree, takes/plants a seed or sits down.
Hold E to grow a seed, then release and press it again to take it. The matching
bed in the conservatory is marked while carrying it.

H opens the field guide. F hides the game interface for photographs, retaining
the prototype's required frame-rate line. Escape opens the menu. Keyboard
menus use Tab and Enter. Arrow keys can look without a locked mouse. A gamepad
uses the sticks, West to tend and the left stick button to walk faster. Touch
has a movement pad, Tend and Menu; drag the right middle of the screen to look.

For co-op, choose **Invite a friend**, then use Escape and **Copy room code**.
The friend enters that code and chooses **Join**. Both copies must be the same
build. **Begin a fresh garden together** resets shared restoration and returns
both visitors home. The menu does not pause the match.

## Building

Configure with `-DTIDE_PRIVATE_PROTOTYPES=ON`. Native targets are `elsewhere`,
`elsewhere_test` and `elsewhere_render_test`. Web targets use the same names;
`elsewhere_page` stages the self-contained page at
`docs/static/prototypes/elsewhere/index.html`. `private_games` collects it.
Serve `docs/static` locally, then open `/prototypes/elsewhere/`.
Alternatively, from the repository root:

```
node prototypes/elsewhere/serve-demo.mjs docs/static/prototypes/elsewhere
```

Open `http://localhost:4179/`. The portable review bundle puts `index.html`,
`serve-demo.mjs` and screenshots in one folder: run `node serve-demo.mjs` there.
The page embeds the program, original sound assets and browser runtime;
co-op room discovery needs an Internet connection.

`tide run` in this folder also builds the game. The nested test manifest keeps
the C fixtures out of CLI game discovery. `art/make_audio.py` rebuilds the
five original WAV files using Python's standard library.
Copy `test/tour.script` to an ignored build folder and pass that copy to the
native game's `--script` option for a reproducible title-to-ending walkthrough.
Its captures are written beside the script.

## Scope

This is a playable concept demo for review. The infinite fractal world,
entering physical miniatures, persistent saves, a longer authored journey,
unique multi-step tasks in each garden and bespoke character animation are
future directions. Live windows render one crossing; a window beyond it is
frosted. Photo mode hides the HUD and keeps the normal player camera. Audio
was captured and checked for clipping, but still needs a listening review.

Implementation and validation are in `notes/elsewhere.md`.
