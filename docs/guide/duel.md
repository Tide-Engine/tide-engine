# Duel

A small 2D arena game inspired by Rounds, made with Tide. Jump, aim, shoot and block, then build a stronger fighter between rounds. Click the game to give it the mouse and keyboard.

<iframe class="demo-frame duel" src="/duel/" title="Tide duel" allow="fullscreen"></iframe>

[Open it on a page of its own](/duel/).

## Play

The game opens on its menu, with the two fighters dueling by themselves behind it. Nobody shoots at you there. **Practice** starts a match against a bot, with you as the blue fighter, and **Invite a friend** opens a room for another player (see [Duel a friend](#duel-a-friend)).

Win five rounds to win the match. After each round, its loser picks one of three upgrades, which stays with them for the rest of the match.

Shots drop as they fly, so aim over a target that's far away: the dots in front of your fighter show where your next shot goes. Block briefly to send incoming shots back. Your shield takes two seconds to come back. Pick an upgrade by clicking or tapping its card, by pressing `1`, `2` or `3`, or with the arrows or a gamepad's d-pad and Enter.

**Menu**, in the corner, ends practice, and so does Escape.

| Action | Mouse and keyboard | Gamepad |
|---|---|---|
| Move | `A` / `D`, or left / right arrows | Left stick |
| Jump | Space, `W`, or up arrow | South face button |
| Aim | Mouse | Right stick |
| Fire | Left mouse button | Right trigger |
| Block | Right mouse button or Shift | Left trigger or left shoulder |

### Touch controls

| Action | Touchscreen |
|---|---|
| Move | Drag left or right on the left half |
| Jump | Drag upward with the same finger |
| Aim and fire | Hold a finger on the right half |
| Block | Tap the block area in the bottom right |

To jump again, lower your finger or lift it and start a new upward swipe.

### Upgrades

| Upgrade | What it does |
|---|---|
| Vitality | 35 more health |
| Rapid fire | 22% shorter reload |
| Scatter | One more pellet a shot, each 15% weaker |
| Ricochet | Shots bounce off one more wall |
| Heavy shot | 30% stronger hits, and a 12% longer reload |
| Swift feet | 15% faster movement |
| Featherweight | Shots that drop 60% less and fly 15% faster |
| Mortar | 45% stronger hits, from shots that drop 60% faster |

A fighter's upgrades show as gems beside its name.

## Duel a friend

On the menu, press **Invite a friend** to open a room. Its code shows in the button's place; **Copy** puts it on the clipboard. Your friend opens this page, types or pastes the code into the field beside **Join** (Ctrl+V, or Cmd+V), and presses **Join**. They take the orange fighter, and the match starts when they arrive. You can practice while you wait.

A player who leaves ends the match: whoever stays is back at the menu, with the room still open.

Both machines run the same arena from the players' inputs.

## The code

The game is `examples/duel` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/duel).

`game.tide` and `physics.tide` are the match: the rules, which every machine runs the same way. The rest is what one machine shows of it, and none of it is in the match:

- `paint.tide` draws the arena, and watches the match to start effects: a view compares what it sees with what it saw the frame before, which it keeps in [local fields](../game/local-state.md#local-fields) of the fighters and the shots.
- `effects.tide` has the sparks, the trails, the rings and the camera's shake, all [local state](../game/local-state.md), and the backdrop, a [frag](../game/shaders.md) over the whole screen.
- `hud.tide` and `play.tide` are the score and the menus, whose buttons and cards are the game's own, [claimed](../game/gui.md#widgets-of-your-own) from the pointer as the GUI's widgets are.
- `shapes.tide` is what draws all of it but the text: one frag that makes a panel, a disc, a ring, a line or a glow of a box with round corners, for every shape of a frame in a single [instanced draw](../game/shaders.md#instances-from-a-list).
