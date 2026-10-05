# Duel

A small 2D arena game inspired by Rounds, made with Tide. Jump, aim, shoot and block, then build a stronger fighter between rounds. Click the game to give it the mouse and keyboard.

<iframe class="demo-frame duel" src="/duel/" title="Tide duel" allow="fullscreen"></iframe>

[Open it on a page of its own](/duel/).

## Play

The game opens on its menu, with the two fighters dueling by themselves behind it. Nobody shoots at you there. **Practice** starts a match against a bot, with you as the blue fighter, and **Invite a friend** opens a room for another player (see [Duel a friend](#duel-a-friend)).

Win five rounds to win the match. After each round, its loser picks one of three upgrades, which stays with them for the rest of the match.

Shots drop as they fly, so aim over a target that's far away: the dots in front of your fighter show where your next shot goes. Block briefly to send incoming shots back. Your shield takes two seconds to come back. Pick an upgrade by clicking or tapping its card, by pressing `1`, `2` or `3`, or with the arrows or a gamepad's d-pad and Enter. Every menu works that way: the arrows, Tab or the d-pad move between its buttons, and Enter, Space or the gamepad's south button press one.

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

`game.tide` and `physics.tide` are the match: the rules, which every machine runs the same way. The match also says what happens in it, as [events](../game/events.md) nothing in the match handles: a hit that counted, a shot's bounce and where it ended, a jump, a landing, who took a round. They cost the match nothing. The rest is what one machine shows of it, and none of it is in the match:

- `heard.tide` has the local handlers of those events: each runs once for an event, with the fighter or the shot as the event left it, and sets off sparks, a flash or a number. A hit's sparks come as this machine predicts it; what must not be wrong, like the number a hit took, a knockout or who took a round, comes from `verified` handlers, once the server has confirmed it.
- `paint.tide` draws the arena. How a fighter or a shot looks beyond what the match says of it (a flash that fades, a squash that springs back, a trail) is in their [local fields](../game/local-state.md#local-fields), which the handlers set and the views let settle. The health bars and the score read the match `verified`. A fighter's view goes through the platforms and the other fighter with [queries](../game/views.md#other-entities), for its shadow and for the dots that show where its shot would end.
- `effects.tide` has the sparks, the trails, the rings and the camera's shake, all [local state](../game/local-state.md), and the backdrop, a [frag](../game/shaders.md) over the whole screen. It also has the light: the arena is drawn into a [target that's always the window's size](../game/views.md#targets-and-post-processing), its bright parts are blurred through smaller ones, and a frag puts the arena and its glow in the window, before the score and the menus are drawn there. The pieces that fall land on the platforms, which a view of their own queries, so the rest goes on with no match.
- `hud.tide` and `play.tide` are the score and the menus. Their buttons and cards are [controls](../game/gui.md#widgets-of-your-own): the game draws them, and the GUI handles their input, so the pointer, the keyboard and a gamepad all work them with no code of the game's. The field for a room's code is the GUI's own, in a [theme](../game/gui.md#themes) that matches the menu.
- `shapes.tide` has the few shapes the game draws again and again: panels whose fill goes from one color to another under an outline, glows and shadows. Everything but the backdrop is the engine's [smooth shapes](../game/views.md#smooth-shapes), round rects, ellipses, thick lines and arcs, drawn in the order they're written with the text among them.
- The [draw settings](../game/views.md#draw-settings) do the rest. What's light (sparks, trails, rings, a shot's glow, the flash of a knockout) is drawn inside `Draw.Blend(Blend.Add)`, so it adds to what's under it and is brighter where it crosses. A menu, a banner or a card that fades in is drawn as it is when it's there, inside `Draw.Tint`. An upgrade card is drawn around (0, 0), and `Draw2D.Transform` puts it in its place, turning as it lands; its picture is drawn in a box 100 across that another transform makes the size it is.
