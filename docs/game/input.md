---
description: "Sample keyboard, mouse, touch and gamepad devices into Tide player input, then predict, sanitize and share it across the multiplayer match."
---

# Input

A player's **input** is what they do in one tick, and it's what the network sends. A game can declare one `input`, whose fields systems read, or read [devices directly in the match](#devices-in-the-match).

```csharp
input PlayerInput
{
    float2 move;
    bool jump;

    Sample(Devices devices)
    {
        var keys = devices.keyboard;
        if (keys.d.pressed) move.x += 1;
        if (keys.a.pressed) move.x -= 1;
        move += devices.gamepad.leftStick;
        jump = keys.space.pressed || devices.gamepad.buttonSouth.pressed;
    }
}

system Jump(PlayerInput input, mut Velocity velocity)
{
    if (input.jump.down)
        velocity.value.y = 5;
}
```

Click the game below so it has the keyboard, then walk with `A` and `D` or the arrows and jump with space. A drag works too: sideways to walk, up to jump. `Sample` turns what the devices say into the input's two fields, a system moves the hero by them, and the line at the top shows what the match got.

::: tide-example input An input, sampled and read

<<< @/examples/input.tide

:::

## Sampling

The engine calls the input's `Sample` method on the player's own machine, once per tick. Its fields start at their defaults, and `Sample` assigns them by name, without `mut`.

`Sample` runs outside the match. It reads this machine's `Devices`, and the local singletons it takes as parameters, like `Sample(Devices devices, Settings settings)`, but never the match. It's the place to work out what the match needs from things only this machine knows: an aim direction from the mouse, which is in this machine's window, or anything that depends on its settings.

## Who gets which input

An input parameter gives a system the input of the player who **owns** the entity it's running for. `Owner` is a built-in component that ties an entity to a player:

```csharp
Spawn(Body, Owner { player = joined.player });
```

It's a normal component: it can be read, written, added and removed, and writing it hands control over.

Entities without an `Owner`, or whose owner is no player the match has a place for (`default`, or a `PlayerID` past its `maxPlayers`), get the **server's input**, and so do systems that run once per tick. An owner the match has a place for who isn't in it right now is still that player: the entity reads their input, not the server's. That's how the server controls what no player owns. An input parameter doesn't pick entities: add `with Owner` to only run on owned ones.

Below are two machines in one match. Move the pointer over a frame to steer that machine's player. One system moves every body, and for each it reads the input of whoever owns that body. The gray body has no `Owner`, so it reads the server's input, which here is player 1's machine's: it follows player 1's pointer, slowly. **Trade bodies** writes the two `Owner`s the other way round, and from then on each player steers the other's body.

::: tide-match owners Whose input a system reads

<<< @/examples/owners.tide

:::

A view picks the entities this machine's player owns with `with ownership`, and every other one with `without ownership` (see [Your own entities](./views-and-the-match.md#your-own-entities)). That's for views: the match runs the same on every machine, so a system has no player of its own to ask about.

Players are `PlayerID`s, not ints or connections. Like `Entity`, a `PlayerID` is opaque, compared with `==`, and has a null value. A player who reconnects gets their `PlayerID` back, and keeps everything they owned. `PlayerID(0)` names a player by number, for local play and tests.

## Held, not pressed

Input carries whether buttons are held, not whether they just went down: when a player's input is late, the other machines guess it by repeating their last one, and a repeated "just went down" would press twice. In the match, `bool` fields of the input get `.down` and `.up`, worked out against the previous tick, inside structs too: `input.aim.fire.down`.

## Passing input to helpers

Synchronous functions and methods can read the same input as their caller:

```csharp
bool WantsJump(PlayerInput controls)
{
    return controls.jump.down;
}

system Jump(PlayerInput input, mut Velocity velocity)
{
    if (WantsJump(input)) velocity.value.y = 5;
}
```

Pass the caller's input parameter directly. The helper borrows its current and previous tick together, preserving the selected player's input, nested `.down`/`.up` edges, prediction and replay behavior. Helpers can forward it to other helpers, without an extra argument or a full input copy. They gain no implicit player, entity or world access.

Input parameters are read-only: no `mut`, `out`, `in` or default. A borrow cannot be stored, returned or kept by a task. Copy individual fields when ordinary values need to outlive the call. Synchronous action blocks, comparisons and parallel steps can read or forward input under their existing rules. Externs, operators, shaders, audio declarations and async routines take no input borrow. `Sample`, `Sanitize` and `Predict` still pass their field values to helpers; they have no input parameter to borrow.

## What other machines guess

A machine runs ahead of the last tick the server confirmed to it, by about its round trip (see [Networking](../multiplayer/networking.md#prediction-and-rollback)). For those ticks it has to guess what the other players do, and its guess is their last input, repeated. A guess that turns out wrong is put right when the server's answer comes, and the further ahead a machine runs, the more there is to put right: at a ping of 500 ms, a player who stopped running is shown running for half a second more, and then moved back all of it.

So a guess only repeats an input for 0.15 seconds past what the machine knows. After that it takes the player to have let go: every field of their input is back at its default. However slow the connection, a guess that was wrong moves things by what a player does in 0.15 seconds, at most. A machine with a round trip under about a tenth of a second never runs that far ahead, and nothing changes for it.

That suits what a player is **doing**: an axis that's pushed, a button that's held. A field that says **where something is**, like an aim or the cell under a pointer, has no rest to go back to: its default is only another place. Mark those `[Absolute]`, and a guess keeps them for as long as it goes on:

```csharp
input Controls
{
    [Clamp(-1, 1)] float move;   // Doing: back to 0
    bool fire;                   // Doing: let go
    [Absolute] float2 aim;       // Where: kept
    [Absolute] int weapon;       // Which: kept
}
```

- **Letting go presses nothing.** No field's `.up` is true for it, so a shot that fires when a button is let go doesn't fire on a guess.
- **The devices match code reads** (see [Devices in the match](#devices-in-the-match)) are let go the same way: keys up, sticks centered.
- **Only guesses change.** What the server runs, and what it confirms, is the input each player sent. A player whose input arrives late still keeps their last one for that tick, on every machine.
- **What it costs.** A player who keeps going is shown a little behind where they'll turn out to be, and each tick the server confirms, the machine runs the ticks past its guess again. For the default guess, both happen only on a machine further ahead than 0.15 seconds, and only while someone is doing something. Custom guesses can require replay sooner.

`[Absolute]` takes no arguments, and goes on the input's own fields, not on the fields of a struct it holds.

## Choosing a guess

Use `[DontPredict]` for actions that should occur only when a real input sample says so. On every guessed tick, the field starts at its declared default:

```csharp
input Controls
{
    float2 move;
    [DontPredict] bool jump;
    [DontPredict] bool dash;
    [Absolute] float2 aim;
}
```

Movement continues from the previous input; jump and dash become false immediately. Real input from this machine still acts immediately, and received input keeps its actions. The attribute takes no arguments and goes on the input's own fields. A marked struct or array resets as a whole. It cannot combine with `[Absolute]` on the same field.

For a computed guess, add `Predict()`:

```csharp
input Controls
{
    float2 move;
    bool dash;

    Predict()
    {
        dash = false;
        move *= 0.9;
    }
}
```

It runs once per guessed tick, starting from the previous input, after `[DontPredict]` resets. Fields it leaves untouched continue; computations such as the movement fade accumulate across guessed ticks. It can also explicitly replace a marked field's default. Repair, bounds and `Sanitize()` apply afterward. The existing 0.15-second limit still resets non-absolute fields after `Predict()` runs.

`Predict()` takes no parameters and reads only the input's fields. It can call pure helpers, as `Sanitize()` does. It cannot read devices, match or local state, frame time or randomness, or send events, spawn entities or start tasks. The engine's `viewTime` remains read-only.

These changes apply to client guesses of other players and a remote server. Actual samples, authoritative server ticks and confirmed replay keep their existing behavior. Changing a guess creates no `.down` or `.up` edges, so clearing an action does not invent a release. Normal `.down` already avoids repeated presses when a held bool is repeated; use these hooks when the held value itself or another action field should not continue into guessed ticks.

## Devices

`Devices` has a keyboard, a mouse, a gamepad, a touchscreen and the pointer. Every button has `.pressed` (held), `.down` (went down since the last sample) and `.up` (went up). On a device, `.pressed` means held at any point since the last sample, so a quick tap between ticks is never lost. A match that starts from a menu doesn't read what was pressed there: its first tick gets what's held as it starts.

- **Keyboard:** every key by its position, named after the US layout, so `WASD` is in the same place on AZERTY:
  - `a` to `z`, and `digit0` to `digit9`
  - `space`, `enter`, `escape`, `tab`, `backspace`
  - `insert`, `delete`, `home`, `end`, `pageUp`, `pageDown`
  - `upArrow`, `downArrow`, `leftArrow`, `rightArrow`
  - `leftShift`, `rightShift`, `leftCtrl`, `rightCtrl`, `leftAlt`, `rightAlt`, `capsLock`
  - `f1` to `f12`
  - `minus`, `equals`, `leftBracket`, `rightBracket`, `backslash`, `semicolon`, `quote`, `comma`, `period`, `slash`, `backquote`
  - `numpad0` to `numpad9`, `numpadEnter`, `numpadPlus`, `numpadMinus`, `numpadMultiply`, `numpadDivide`, `numpadPeriod`
  - `text`, a `string`: what was typed since the last frame, which views read (see [Typed text](#typed-text))
- **Mouse:** `position`, `delta` and `scroll` (`float2`), and the buttons `left`, `right`, `middle`, `back` and `forward`. `position` is in window pixels from the bottom left, with `y` up, as a touch's and the pointer's are; in a view, `Screen.Flip(position)` gives it from the top left, as the screen and the GUI count (see [Screen and world coordinates](./cameras.md#screen-and-world-coordinates)). While the cursor is [locked](#locking-the-cursor), `position` stays where it was and `delta` is the mouse's own movement.
- **Gamepad:** `connected`, `leftStick` and `rightStick` (`float2`), `leftTrigger` and `rightTrigger` (0 to 1), the face buttons by position (`buttonSouth`, `buttonEast`, `buttonWest`, `buttonNorth`, which is A, B, X and Y on Xbox), `dpad.up` and the other directions, `leftShoulder`, `rightShoulder`, `leftStickButton`, `rightStickButton`, `start` and `select`.
- **Touchscreen:** `connected`, `primaryTouch`, and `touches`, a slot for each of 10 fingers. Each `Touch` has:
  - `press`, a button: `.down` when the finger touched, and `.up` when it lifted
  - `id`, the same while the finger touches, and a new one for each touch
  - `position`, `delta` and `startPosition` (`float2`)
- **Pointer:** `position`, `delta` and `press`: the mouse, with its left button, or the primary touch, whichever was used last. Once a finger lifts, the pointer stays where it lifted, until another finger touches or the mouse is used.

Each part has a type of its own: `Keyboard`, `Mouse`, `Gamepad`, `Dpad`, `Touchscreen`, `Touch`, `Pointer` and `Button`. Functions take them, and `Devices`, as parameters to read:

```csharp
float2 Steer(Gamepad pad)
{
    return pad.leftStick + float2(pad.dpad.right.pressed ? 1 : 0, 0);
}
```

Axes follow Unity: `y` is positive up, for sticks, the mouse, touches and the pointer, and `scroll.y` is positive scrolling away from you.

### Touch

A finger keeps its slot in `touches` while it touches, and lifts out of it. With ten fingers on the screen, an eleventh is left out. `primaryTouch` is the finger that touched while no other was the primary one, until it lifts: the one to follow for a single finger. A finger that touches and lifts between two samples reads as held for one, like a quick key press, so no tap is lost.

`touches[i]` is the touch in slot `i` (an empty one past the last), `touches.count` is how many slots there are, and `foreach` goes through every slot, touching or not:

```csharp
mut var fingers = 0;
foreach (var touch in devices.touchscreen.touches)
{
    if (touch.press.pressed) fingers += 1;
}
```

A finger is never the mouse, as in Unity's Input System: the mouse only sees mice. Code that should work with both reads the **pointer**, which follows the mouse or the primary touch, whichever was used last, as Unity's `Pointer.current` does. The GUI follows it too, so buttons work with a finger.

```csharp
input Brush
{
    [Absolute] int x;
    [Absolute] int y;
    bool paint;

    // Cells of 16 pixels, under the mouse or a finger
    Sample(Devices devices)
    {
        var pointer = devices.pointer;
        x = int(Math.Floor(pointer.position.x / 16));
        y = int(Math.Floor(pointer.position.y / 16));
        paint = pointer.press.pressed;
    }
}
```

Touches come from Android, the web on phones and desktops, and touchscreens on Windows. Native iOS support isn't built yet; other native desktops have no touch support yet. A browser on those desktops can still report touches. Where no touchscreen is available, `connected` is false and no finger ever touches.

The cell coordinates above are `[Absolute]` because a prediction should keep the last cell the player pointed at, even after it guesses they stopped painting.

Try a mouse, then a finger in the example below. The amber ring is the pointer; teal circles are individual touches, with lines back to where each started. A touch leaves the mouse where it was, and lifting it leaves the pointer where it ended. The ten slots show which fingers are still held. Click or tap the frame first so swipes go to the example.

::: tide-example pointer The pointer and individual fingers

<<< @/examples/pointer.tide

:::

### Locking the cursor

A camera that turns with the mouse locks the cursor: a view calls `Cursor.Lock()` every frame the game wants the mouse (see [Looking around](./cameras.md#looking-around)). `Cursor.locked` is whether the window holds it right now, which the system decides, not the game: the cursor goes back to the player when they press Escape, and a window that loses the focus lets it go.

`Sample` reads `Cursor.locked`, so the mouse only turns the camera while the cursor is locked, and moving it over a menu doesn't:

```csharp
input PlayerInput
{
    float2 look;

    Sample(Devices devices)
    {
        if (Cursor.locked) look = devices.mouse.delta * 0.003;
    }
}
```

`Sample` can't call `Cursor.Lock()`: it runs once per tick, and the lock is asked for every frame, by views. The match can't read `Cursor.locked`, as it can't read anything of this machine's: put what it needs in the input, as `look` is here.

### Typed text

`devices.keyboard.text` is what the player typed since the last frame, as a `string`. Unlike keys, it follows the keyboard's layout: on AZERTY, the key in the `q` position types `a`, and `text` says `a`. Shift, accents and a phone's keyboard are applied, and what's pasted with Ctrl+V comes through it too. Backspace, Enter and the arrows are keys, not characters.

A text field of the GUI's reads it for you. Views read it themselves, and so do the functions they call, for a text box of their own, or to pass on to a library:

```csharp
local singleton Chat
{
    string line;
}

view Typing(Devices devices, mut Chat chat)
{
    chat.line += devices.keyboard.text;
    if (devices.keyboard.backspace.down) chat.line = chat.line.Substring(0, chat.line.length - 1);
}
```

It's a frame's, so `Sample` can't read it, and neither can the match: an input holds no text, and what's typed is never sent. It's empty while the GUI has the keyboard, as when the player types into a `TextField`, and in every view but the one whose [control](./gui.md#widgets-of-your-own) has the focus, or that [claimed the keyboard](./gui.md#widgets-that-read-the-devices-themselves). A long paste comes over a few frames, 32 characters in each.

### Devices in the match

Match code can read devices too, with a `Devices` parameter: the devices of the player who owns the entity, like an input parameter. The input then sends what match code reads of them, and nothing else. A game can do without an `input` declaration this way:

```csharp
system Hop(Devices devices, mut Velocity velocity)
{
    if (devices.keyboard.space.down || devices.gamepad.buttonSouth.down)
        velocity.value.y = 5;
}
```

Device access always takes a `Devices` parameter. `Sample` and views receive this machine's devices; match code receives the entity owner's, or the server's. A system takes at most one `Devices` parameter and one input parameter. Views can't read the input. Local event handlers and tasks can't take devices.

The match can't read positions on the screen either, the mouse's, a touch's or the pointer's: they're in this machine's window, which the other machines don't have. Work out what the match needs from them in `Sample`, like an aim direction, and read that from the input.

## Input is an attack point

Inputs come from other players' machines, so the engine doesn't trust them:

- NaN and infinite floats and doubles become the field's default. Double fields retain their full 64 bits in input packets and deltas.
- Fields can declare bounds, `[Clamp(lo, hi)]`, `[Min(x)]` and `[Max(x)]`, which the engine applies to every input. A number bounds every component of a vector.
- Then the input's `Sanitize` method runs, if it has one, for what bounds can't say.

```csharp
input PlayerInput
{
    [Clamp(-1, 1)] float2 move;
    [Min(0), Max(3)] int gear = 1;
    bool boost;

    Sample(Devices devices) { move = devices.gamepad.leftStick; }

    // After the bounds: what attributes can't say.
    Sanitize()
    {
        if (gear == 0) boost = false;
    }
}
```

Every input goes through these steps before the match reads it, including this machine's, so systems can rely on them without checking again. Devices are repaired the same way, since the engine knows their ranges.

An input holds numbers, bools, chars, enums, vectors and structs of those; not text or lists. A match takes as many players as the game's `maxPlayers` setting says, 16 without one (see [Matches](../multiplayer/matches.md#how-many-players)).

## Presented view time

With `settings { maxRewind = 0.25; }`, your input gains `ViewTime viewTime`; do not declare this field yourself. It is read-only and stamped by the engine after `Sample`, from the last actually presented confirmed frame. `tick` names the upper endpoint, `alpha` its interpolation fraction, and `valid` is false before a frame is presented. Use it with [collider rewind](../multiplayer/networking.md#collider-rewind); the server validates the wire value as an untrusted claim.
