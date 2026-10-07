---
description: "Build Tide menus and controls with immediate mode GUI widgets, layouts, themes, modals, keyboard navigation and gamepad input."
---

# GUI

The GUI is immediate mode, like drawing: views call widgets every frame, and they're drawn over the world. It follows Unity's IMGUI.

```csharp
local singleton Settings
{
    bool open;
    bool fullscreen;
    float volume = 1;
}

view Options(Devices devices, mut Settings settings)
{
    if (GUI.Button(Rect(Screen.width - 210, 10, 200, 40), "Options")) settings.open = true;
    if (devices.keyboard.escape.down || devices.gamepad.start.down) settings.open = true;

    GUILayout.Modal(Anchor.MiddleCenter, settings.open)
    {
        GUILayout.Toggle("Fullscreen", settings.fullscreen);
        GUILayout.Slider("Volume", settings.volume, 0, 1);
    }
}
```

The GUI is local: widgets change this machine's state, never the match. To act on the match, put it in the [input](./input.md).

## Widgets change what you pass them

Widgets edit values through `mut` parameters, and return whether they changed them: `GUILayout.Toggle("Fullscreen", settings.fullscreen)` flips `settings.fullscreen` when it's clicked. A button returns whether it was pressed.

The `mut` argument is the variable itself, so its type matches exactly: `Slider` takes a `float` variable, not an `int`.

Widgets have no IDs to write. They're told apart by where they're called from and the entity the view runs for, and the ones a loop makes, by their turn in it.

## GUILayout and GUI

`GUILayout` lays widgets out for you: they stack top to bottom, and containers arrange them. `GUI` has the same widgets, each at a `Rect` you give it first, as in Unity: `GUI.Button(rect, "Quit")`.

**Widgets**, with `GUI`'s taking a `Rect` first:

| Widget | What it is |
|---|---|
| `Label(text)` | Text |
| `Button(text)` | A button; returns whether it was pressed |
| `Toggle(text, mut bool value)` | A checkbox |
| `Slider(label, mut float value, min, max)` | A slider |
| `IntSlider(label, mut int value, min, max)` | A slider for ints |
| `TextField(label, mut string text)` | A field the player types into |
| `IntField`, `FloatField`, `Float2Field`, `Float3Field`, `Float4Field` | Number fields: a label and a `mut` value |
| `TextField`, `IntField` and `FloatField` with `out bool entered` last | The same fields, which also say [when the player entered what they typed](#when-a-field-is-entered) |
| `ColorField(label, mut Color value)` | A swatch, and fields for r, g, b and a |
| `GUILayout.Space(size)` | Empty space |

Try typing a name, changing the circles, and counting clicks. Turn on **Disable Click** to see how `GUI.Disabled` keeps a button visible while stopping it from acting. Reset restores the local defaults.

::: tide-example gui-controls Try the controls

<<< @/examples/gui-controls.tide

:::

**Containers** take a block (see [Actions](../language/functions.md#actions)):

| Container | What it does |
|---|---|
| `GUILayout.Vertical() { ... }` | Stacks its widgets top to bottom |
| `GUILayout.Horizontal() { ... }` | Puts its widgets side by side |
| `GUILayout.Area(anchor) { ... }` | A panel sized to its content, at one of nine anchors |
| `GUILayout.Area(rect) { ... }` | A panel at a rect |
| `GUILayout.Modal(anchor, mut bool open) { ... }` | A panel over the whole screen while `open` is true, like a pause menu |
| `GUI.Disabled(bool disabled) { ... }` | Grays out its widgets while `disabled` is true (see [Disabled widgets](#disabled-widgets)) |
| `GUI.Theme(GUITheme theme) { ... }` | Gives its widgets a look of your own (see [Themes](#themes)) |

```csharp
view Hud(mut Look look)
{
    GUILayout.Area(Anchor.UpperLeft)
    {
        GUILayout.Horizontal()
        {
            if (GUILayout.Button("Options")) look.open = true;
            GUILayout.Label("v1.0");
        }
    }
}
```

When the screen is too narrow for them, laid out widgets shrink to fit: fields, sliders and buttons get narrower, and a label's column gives up its spare room first. Labels and toggles keep their size.

The engine builds nothing a game couldn't build itself: containers are functions with an `Action`, and you can write your own the same way. A widget that looks nothing like these is a [control](#widgets-of-your-own): you draw it, and the GUI handles its input.

## When a field is entered

A field the player types into can say when they pressed Enter in it. `TextField`, `IntField` and `FloatField` each have a second version that takes one more argument, an [`out`](../language/functions.md#out-parameters) one after the value, which is true in the frame Enter ends the typing in that field. So a room's code is joined from the keyboard, with no button to reach for:

```csharp
local singleton Lobby
{
    string code;
}

view Join(mut Lobby lobby)
{
    GUILayout.Area(Anchor.MiddleCenter)
    {
        GUILayout.TextField("Room", lobby.code, out var entered);
        if ((GUILayout.Button("Join") || entered) && lobby.code != "") Session.Join(lobby.code);
    }
}
```

- **What the field returns is as ever:** whether it changed its value. A number field keeps its number as it's entered, so it can change and be entered in the same frame.
- **It's true for one frame, and false in every other:** while the player types, when Enter starts the typing in a field that already has the focus, and when the typing ends another way, with Escape, with Tab, with a click elsewhere, or because the field was disabled.
- **The numpad's Enter is Enter,** and so is the one on a phone's keyboard, whatever it's labelled. A gamepad's south button enters a number field, as it ends its typing; it doesn't end a text field's.
- **The button comes first** in `GUILayout.Button("Join") || entered`. A widget is called every frame, so it can't be on the right of `||`, which only runs when the left is false.
- **It's an `out` argument like any other.** `out var entered` declares the variable there, `out` before a variable or a field of local state assigns it, and `out _` leaves it out. Without the argument, the field is the one that doesn't say.

The fields with several numbers, the sliders and the toggle have no such version. With `GUI`, the argument is last too: `GUI.TextField(rect, "Room", lobby.code, out var entered)`.

A call passes its other arguments as they are: `lobby.code` goes to the field's `mut` parameter with no word before it.

## Screen units

Positions and sizes are in pixels, as on a web page, so widgets keep their size when the window changes size. `Screen.width` and `Screen.height` are the window's size. On a display scaled to 150%, a pixel is the display's logical one, 1.5 real pixels wide, as CSS pixels are.

`Rect(x, y, width, height)` is measured from the top left corner, with `y` down, as in Unity's GUI. World drawing, the mouse and touches have `y` up: `Screen.Flip(devices.pointer.position)` is where the pointer is counted the GUI's way, from the top left, and `Screen.Flip` of a GUI position is where it is for the devices.

`Anchor` names the nine places an area can go, as Unity's `TextAnchor` does: `UpperLeft`, `UpperCenter`, `UpperRight`, `MiddleLeft`, `MiddleCenter`, `MiddleRight`, `LowerLeft`, `LowerCenter` and `LowerRight`.

## Modals

While a modal is up, it has the focus, the widgets outside it don't work, and the game and views get nothing from the devices. Back (Escape or the gamepad's east button) closes it. The modal drawn last is on top. A [locked cursor](./views.md#looking-around) comes back while one is up, since its widgets need the pointer.

The buttons below sit side by side in a `Horizontal`, while the labels stack in a `Vertical`. Open the modal and try choosing a color outside it: the choice count stays still until the modal closes. Its toggle can still change the guide around the shape.

::: tide-example gui-layout Layout and modal focus

<<< @/examples/gui-layout.tide

:::

## Disabled widgets

`GUI.Disabled(disabled) { ... }` keeps its widgets on screen but grays them out while `disabled` is true, as Unity's `GUI.enabled = false` does. They're drawn faded, and can't be clicked, focused or typed into, so a menu can stay up while it waits without anyone pressing Connect twice:

```csharp
local singleton Lobby
{
    string address = "127.0.0.1";
}

view Menu(Session session, mut Lobby lobby)
{
    if (session.state == SessionState.Connected) return;
    GUILayout.Area(Anchor.MiddleCenter)
    {
        GUI.Disabled(session.state == SessionState.Connecting)
        {
            GUILayout.TextField("Address", lobby.address);
            if (GUILayout.Button("Connect")) Session.Connect(lobby.address);
        }
        if (session.state == SessionState.Connecting) GUILayout.Label("Connecting...");
    }
}
```

`GUI.Disabled` and the `Area` it's in can share one block, [listed with a comma](../language/functions.md#several-calls-one-block), when nothing else goes in the area:

```csharp
GUILayout.Area(Anchor.MiddleCenter),
GUI.Disabled(busy)
{
    if (GUILayout.Button("Connect")) Session.Connect(lobby.address);
}
```

It lays nothing out: its widgets stay where they'd be without it, in the container around it, and it works with `GUI` and `GUILayout` widgets alike. Tab skips disabled widgets. One disabled while it's pressed lets go, and a field disabled while the player types into it keeps its old value. The pointer on a disabled widget is still the GUI's, so a click on it doesn't reach the game. Inside another `GUI.Disabled` that's disabled, a block stays disabled whatever its own `disabled` is.

## Themes

The GUI has one look, and a theme changes its colors and its corners. `GUITheme` is a struct whose defaults are that look, so a theme names only what it changes:

```csharp
const Color INK = Color(0.86, 0.92, 1);
const Color NAVY = Color(0.07, 0.1, 0.19);

view Menu(mut Lobby lobby)
{
    GUI.SetTheme(GUITheme { text = INK, panel = NAVY, control = Color(0.15, 0.19, 0.32), radius = 10 });
    GUILayout.Area(Anchor.MiddleCenter)
    {
        GUILayout.TextField("Room", lobby.code);
        // One button that stands out
        GUI.Theme(GUITheme { text = NAVY, control = Color.cyan, hot = Color.white, radius = 10 })
        {
            if (GUILayout.Button("Join")) Session.Join(lobby.code);
        }
        if (GUILayout.Button("Back")) lobby.open = false;
    }
}
```

| Field | What has that color |
|---|---|
| `text` | Text, and a slider's thumb |
| `panel` | An area's background |
| `control` | A button, a toggle's box, a slider's rail |
| `hot` | ...with the pointer on it |
| `pressed` | A button held down |
| `field` | A field to type into |
| `fieldHot` | ...with the pointer on it |
| `accent` | The outline of the widget that has the focus, a toggle's check, a slider's filled part |

`radius` is how round the widgets' corners are, in pixels: 0, its default, is square. A part is never rounder than half its smaller side, so a big radius makes a pill of a button.

A theme is set in one of two ways, as every setting of a frame is:

- `GUI.Theme(theme) { ... }` wraps a block: the widgets in it have the theme, and the look it found is back when the block ends.
- `GUI.SetTheme(theme)` stays: for the widgets that follow, in this view and the views after it, until it's set again or the frame ends.

The buttons below pick a theme, and the slider its corners. The menu under them is in a `GUI.Theme` block, so it takes the theme; the buttons and the slider aren't, and keep the look every frame starts with.

::: tide-example gui-theme A theme around a block

<<< @/examples/gui-theme.tide

:::

Every frame starts from the default look, so a view that sets a theme sets it every frame, and a game that sets none looks as the GUI always did. A theme a block doesn't name fields of takes those from the defaults, not from the theme around it: keep a theme in a `const` or a singleton and change a copy of it to build one on another.

An area's panel has the look it opened with. The [stats overlay and the debug views](../tools/cli.md) aren't the GUI's widgets, and keep their own look whatever a game sets.

That's all a theme is: there are no styles for single widgets, no text sizes and no fonts. A widget that should look like nothing here is [one you draw yourself](#widgets-of-your-own).

## Keyboard and gamepad

Navigation is built in. Tab and Shift+Tab move the focus between widgets, in the order they're called, and so do the arrows, the d-pad and the left stick once a widget has the focus: down and right go on to the next, up and left go back. Enter, Space and the south button press, and Escape and the east button let go. A slider and a number field keep left and right, to step their value.

The arrows, the d-pad and the left stick only start moving the focus while no match reads the devices, so a button on a HUD never takes the d-pad from the player: there, Tab starts it. A menu that's up over a match says so, every frame it is, and they start it there too:

```csharp
view Paused(Pause pause)
{
    if (!pause.open) return;
    GUI.Navigate();
    GUILayout.Area(Anchor.MiddleCenter)
    {
        if (GUILayout.Button("Resume")) Send(Resume { });
        if (GUILayout.Button("Leave")) Session.Leave();
    }
}
```

Once a widget has the focus, the keyboard and the gamepad are the GUI's, and the input's `Sample` reads neither. A key or a button that goes down then stays the GUI's until it's let go, even if the widget is gone by the next frame: Space that clicks a menu's Play, and closes the menu, isn't also the game's Space while it's still held.

Typing into a field uses the characters the player types, which follow their keyboard layout, not keys by position. Clicking a number field, or pressing Enter on it, starts typing into it; Enter or leaving the field keeps a valid number, and Escape keeps the old one. A view can ask a field [whether Enter ended the typing in it](#when-a-field-is-entered). On a phone, and in a phone's browser, the system's keyboard shows while the player types into a field, and goes when they stop.

## Copy and paste

Ctrl+V (Cmd+V on macOS) pastes into the field that has the focus, as if the player typed it, but for newlines and tabs. `Clipboard.Copy(text)` puts text on the clipboard, from a view or a local handler, like `Session`'s calls:

```csharp
if (session.room != "")
{
    GUILayout.Label("Room " + session.room);
    if (GUILayout.Button("Copy")) Clipboard.Copy(session.room);
}
```

It copies up to 255 bytes for now. In a browser, it works shortly after a click or a key, as when a button is pressed.

## The GUI and input

Widgets follow the pointer: the mouse, or a finger on a touchscreen (see [Input](./input.md#touch)). A finger that lifted is nowhere, so nothing stays hovered where it was.

Whatever the GUI is using, such as a click on a button, a finger on one, or typing in a field, is hidden from the input's `Sample` and from views' `Devices`. So clicking a button never fires a weapon, and typing a name never moves the player.

The pointer is the GUI's wherever it's on a widget or an area, from the moment it's there: a finger that touches a button never reaches the game, not even for the frame it lands in. The engine goes by where the last frame drew its widgets, so the one thing that gets through is a press on a widget in the very frame it first appears.

## Widgets of your own

A widget with a look of its own is a control: the view draws it, with `Draw` or anything else, and the GUI handles its input.

```csharp
local singleton Draft
{
    int picked = -1;
}

view Cards(mut Draft draft)
{
    Draw.SetScreen();
    for (var slot = 0; slot < 3; slot++)
    {
        var rect = Rect(Screen.width * 0.5 - 330 + slot * 220, 200, 200, 280);
        var card = GUI.Control(rect);
        var lit = card.hovered || card.focused;
        var center = float2(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5 + (card.pressed ? 2 : 0));
        Draw.RoundRect(center, float2(rect.width, rect.height), 18, lit ? Color(0.2, 0.3, 0.6) : Color(0.1, 0.14, 0.25));
        if (card.clicked) draft.picked = slot;
    }
}
```

The cards below are three controls, drawn from [smooth shapes](./views.md#smooth-shapes). Hover one, press it, and let go on it or off it. Then click the game and press Tab: the focus moves through the cards and on to the GUI's own button, and Enter picks.

::: tide-example gui-control Cards that are controls

<<< @/examples/gui-control.tide

:::

`GUI.Control(rect)` returns a `GUIControl`, which says what the player is doing with the rect:

| Field | When it's true |
|---|---|
| `hovered` | The pointer is on it, with nothing of the GUI's over it |
| `pressed` | The pointer holds it: from a press on it until it lets go, wherever it goes meanwhile |
| `focused` | The keyboard and the gamepad are on it |
| `clicked` | For one frame: a press on it let go on it, or Enter, Space or the south button went down with the focus on it |

It works as a button of the GUI's does, and the GUI draws nothing for it, so showing that it's hovered or has the focus is the view's:

- **The pointer on it is the GUI's.** A press there never reaches the input's `Sample` or the other views, from the frame it lands in, a finger's too. The view needs no claim.
- **It takes the focus with the other widgets.** Tab, the arrows, the d-pad and the left stick move the focus to it and from it, alongside the GUI's own widgets, in the order they're called. A click doesn't give it the focus, as it doesn't a button.
- **`GUI.Disabled` and modals stop it.** In a disabled block, or outside the modal that's up, nothing of it is true and the focus skips it. Draw it faded yourself.
- **A button looks down while it's `pressed` and `hovered`.** `pressed` alone stays true while the pointer drags away, which is what a handle or a slider of your own follows.

While a control has the focus, its view goes on reading the keyboard and the gamepad, but for what moves the focus and clicks: the arrows, Tab, Enter, Space and Escape, and the d-pad, the left stick and the south and east buttons. So a menu's shortcuts still work with one of its buttons focused, and a control can be a text field: read `devices.keyboard.text`, and call `GUI.ShowKeyboard()` for phones. The input's `Sample` and the other views read none of it meanwhile.

::: tip Controls in a loop
The three cards above come from one call, so the GUI tells them apart by their turn: the first one made, the second, the third. Make the same ones every frame, in the same order, and each keeps its own hover and focus. A loop that leaves an earlier one out one frame gives its turn to the next, and the focus goes on with whatever is now in its place: leave out the drawing, not the call.
:::

A control takes its rect, and `GUILayout` has none: what a view draws is under the GUI's own widgets, an area's panel included, so a control laid out inside an area would be drawn beneath it.

::: info What's drawn where
The GUI's widgets are drawn last, over everything views draw. So a control you draw is under any widget of the GUI's that overlaps it, which also takes the pointer there, and under a modal's dim.
:::

### Widgets that read the devices themselves

A UI library, reached through [Calling C](../c/functions.md), reads the devices itself, and so does a view that works its widgets out on its own. The engine doesn't know those widgets are there, so the view says what they're using:

| Call | What it does |
|---|---|
| `GUI.ClaimPointer(rect)` | This view has a widget at `rect`: the pointer on it is this view's |
| `GUI.ClaimPointer()` | This view is using the pointer wherever it is, as while it drags something |
| `GUI.ClaimKeyboard()` | This view is using the keyboard: the keys and [what's typed](./input.md#typed-text) |
| `GUI.ShowKeyboard()` | The player is typing: phones show their keyboard, as they do for a text field |

What a view claims is hidden from the input's `Sample` and from the other views, as what the GUI uses is, and the view that claimed it goes on reading it. The pointer's claim covers the mouse's buttons and scroll, the pointer's press and the primary touch. A camera view that drags with the mouse needs no changes to stay still while a toolbox is dragged. A view whose menu covers the screen claims all of it, so a click between its controls isn't the game's either: `GUI.ClaimPointer(Rect(0, 0, Screen.width, Screen.height))`.

```csharp
local singleton Toolbox
{
    bool dragging;
    bool renaming;
    string name;
}

// A toolbox in the window's lower left corner
view Tools(Devices devices, mut Toolbox box)
{
    var top = Screen.height - 200;
    GUI.ClaimPointer(Rect(0, top, 200, 200));
    if (box.dragging) GUI.ClaimPointer();

    var pointer = devices.pointer;
    var at = Screen.Flip(pointer.position); // From the top left, as the Rect is
    var over = at.x < 200 && at.y >= top;
    if (over && pointer.press.down) box.dragging = true;
    if (!pointer.press.pressed) box.dragging = false;

    if (box.renaming)
    {
        GUI.ClaimKeyboard();
        GUI.ShowKeyboard();
        box.name += devices.keyboard.text;
        if (devices.keyboard.enter.down) box.renaming = false;
    }
}
```

A claim lasts one frame: make it every frame, as you draw the widget every frame. A view that stops running leaves nothing claimed. A function's claim is the view's that called it.

`GUI.ClaimPointer(rect)` says where a widget is, in the GUI's pixels, from the top left like every `Rect`; the pointer's own position counts from the bottom left, which `Screen.Flip` turns into the Rect's. Call it every frame the widget is there, whether the pointer is on it or not. A press on it is then the view's from the frame it lands in, as on the GUI's own widgets: a finger that touches it never reaches the game.

`GUI.ClaimPointer()`, with no rect, claims the pointer wherever it is: for a widget dragged off its rect, as in the example, or a library that only says it wants the mouse.

::: tip Give the rect where you can
A claim with no rect hides from the next frame on, since the engine doesn't know where your widget is. With a mouse, claiming while it's over the widget is enough: the click comes later. A finger hovers over nothing, so its touch reaches the game and the other views for the frame it lands in. On a phone, only a rect keeps it from them.
:::

The rest follows from the GUI being drawn on top:

- The GUI's own use comes first. A view never reads what the GUI is using, whatever it claimed, so a button of the GUI's over your widget takes the click.
- While a view has the keyboard, Tab and the arrows don't move the GUI's focus onto its widgets.
- The gamepad isn't the keyboard's: a claim leaves it to the game.
- Where the pointer is stays everyone's, as it does over the GUI.
- Two views that claim the same device both go on reading it. Which of their widgets is on top is theirs to know.

`GUI.ShowKeyboard()` is apart from the keyboard's claim, since a widget that only takes shortcuts doesn't want a phone's keyboard over half the screen. Call it every frame the player is typing into your widget.
