# Input

A player's **input** is what they do in one tick, and it's what the network sends. Each game declares one `input`, and systems read it.

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

Entities without an `Owner`, or whose owner isn't a known player, get the **server's input**, and so do systems that run once per tick. That's how the server controls what no player owns. An input parameter doesn't pick entities: add `with Owner` to only run on owned ones.

Below are two machines in one match. Move the pointer over a frame to steer that machine's player. One system moves every body, and for each it reads the input of whoever owns that body. The gray body has no `Owner`, so it reads the server's input, which here is player 1's machine's: it follows player 1's pointer, slowly. **Trade bodies** writes the two `Owner`s the other way round, and from then on each player steers the other's body.

::: tide-match owners Whose input a system reads

<<< @/examples/owners.tide

:::

A view picks the entities this machine's player owns with `with ownership`, and every other one with `without ownership` (see [Your own entities](./views.md#your-own-entities)). That's for views: the match runs the same on every machine, so a system has no player of its own to ask about.

Players are `PlayerID`s, not ints or connections. Like `Entity`, a `PlayerID` is opaque, compared with `==`, and has a null value. A player who reconnects gets their `PlayerID` back, and keeps everything they owned. `PlayerID(0)` names a player by number, for local play and tests.

## Held, not pressed

Input carries whether buttons are held, not whether they just went down: when a player's input is late, the other machines guess it by repeating their last one, and a repeated "just went down" would press twice. In the match, `bool` fields of the input get `.down` and `.up`, worked out against the previous tick, inside structs too: `input.aim.fire.down`.

## Devices

`Devices` has a keyboard, a mouse, a gamepad, a touchscreen and the pointer. Every button has `.pressed` (held), `.down` (went down since the last sample) and `.up` (went up). On a device, `.pressed` means held at any point since the last sample, so a quick tap between ticks is never lost.

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
- **Mouse:** `position`, `delta` and `scroll` (`float2`), and the buttons `left`, `right`, `middle`, `back` and `forward`. `position` is in window pixels from the bottom left, with `y` up, as a touch's and the pointer's are; in a view, `Screen.Flip(position)` gives it from the top left, as the screen and the GUI count (see [Screen and world coordinates](./views.md#screen-and-world-coordinates)). While the cursor is [locked](#locking-the-cursor), `position` stays where it was and `delta` is the mouse's own movement.
- **Gamepad:** `connected`, `leftStick` and `rightStick` (`float2`), `leftTrigger` and `rightTrigger` (0 to 1), the face buttons by position (`buttonSouth`, `buttonEast`, `buttonWest`, `buttonNorth`, which is A, B, X and Y on Xbox), `dpad.up` and the other directions, `leftShoulder`, `rightShoulder`, `leftStickButton`, `rightStickButton`, `start` and `select`.
- **Touchscreen:** `connected`, `primaryTouch`, and `touches`, a slot for each of 10 fingers. Each `Touch` has:
  - `press`, a button: `.down` when the finger touched, and `.up` when it lifted
  - `id`, the same while the finger touches, and a new one for each touch
  - `position`, `delta` and `startPosition` (`float2`)
- **Pointer:** `position`, `delta` and `press`: the mouse, with its left button, or the primary touch, whichever was used last.

Each part has a type of its own: `Keyboard`, `Mouse`, `Gamepad`, `Dpad`, `Touchscreen`, `Touch`, `Pointer` and `Button`. Functions take them, and `Devices`, as parameters to read:

```csharp
float2 Steer(Gamepad pad)
{
    return pad.leftStick + float2(pad.dpad.right.pressed ? 1 : 0, 0);
}
```

Axes follow Unity: `y` is positive up, for sticks, the mouse, touches and the pointer, and `scroll.y` is positive scrolling away from you.

### Touch

A finger keeps its slot in `touches` while it touches, and lifts out of it. `primaryTouch` is the finger that touched while no other was the primary one, until it lifts: the one to follow for a single finger. A finger that touches and lifts between two samples reads as held for one, like a quick key press, so no tap is lost.

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
    int x;
    int y;
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

Touches come from the web, on phones and desktops alike, and from touchscreens on Windows. Android and iOS come next. Elsewhere, `connected` is false and no finger ever touches.

### Locking the cursor

A camera that turns with the mouse locks the cursor: a view calls `Cursor.Lock()` every frame the game wants the mouse (see [Looking around](./views.md#looking-around)). `Cursor.locked` is whether the window holds it right now, which the system decides, not the game: the cursor goes back to the player when they press Escape, and a window that loses the focus lets it go.

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

- NaN and infinite floats become the field's default.
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

An input holds numbers, bools, enums, vectors and structs of those; not text or lists. A match takes as many players as the game's `maxPlayers` setting says, 16 without one (see [Matches](../multiplayer/matches.md#how-many-players)).
