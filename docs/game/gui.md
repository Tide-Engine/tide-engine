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

For a look of your own (gradients, shadows, animated controls), see [Good-looking UI](./ui-style.md).

The GUI is local: widgets change this machine's state, never the match. To act on the match, put continuous controls in the [input](./input.md), or send a [command](./commands.md) for a button's action.

## Widgets change what you pass them

Widgets edit values through `mut` parameters, and return whether they changed them: `GUILayout.Toggle("Fullscreen", settings.fullscreen)` flips `settings.fullscreen` when it's clicked. A button returns whether it was pressed.

The `mut` argument is the variable itself, so its type matches exactly: `Slider` takes a `float` variable, not an `int`.

Widgets have automatic IDs from where they're called and the entity the view runs for. Repeated calls use their turn in the loop; use [`GUI.Key`](#stable-keys) when items can move or disappear.

## GUILayout and GUI

`GUILayout` lays widgets out for you: they stack top to bottom, and containers arrange them. `GUI` has the same widgets, each at a `Rect` you give it first, as in Unity: `GUI.Button(rect, "Quit")`.

**Widgets**, with `GUI`'s taking a `Rect` first:

| Widget | What it is |
|---|---|
| `Label(text)` | Text |
| `Label(text, size)` | Text at a size of its own, like a heading: `GUILayout.Label("Settings", 24)`. Its line is as tall as a widget with that text size |
| `Button(text)` | A button; returns whether it was pressed |
| `Toggle(text, mut bool value)` | A checkbox |
| `Slider(label, mut float value, min, max)` | A slider |
| `IntSlider(label, mut int value, min, max)` | A slider for ints |
| `TextField(label, mut string text)` | A field the player types into |
| `IntField`, `FloatField`, `Float2Field`, `Float3Field`, `Float4Field` | Number fields: a label and a `mut` value |
| `TextField`, `IntField` and `FloatField` with `out bool entered` last | The same fields, which also say [when the player entered what they typed](#when-a-field-is-entered) |
| `ColorField(label, mut Color value)` | A swatch, and fields for r, g, b and a |
| `GUILayout.Space(size)` | Empty space |
| `GUILayout.Reserve(float2 size)` | A layout rect for custom content, without drawing or interaction |

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
| `GUI.Draw() { ... }` | Records `Draw` calls alongside the GUI's widgets |
| `GUI.Key(key) { ... }` | Gives repeated content a stable data key |

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

- **What the field returns is as ever:** whether it changed its value. A number field keeps its number as it's entered, so it can change and be entered in the same frame. Opening and closing it without typing leaves its value unchanged; editing a float, a vector or a color keeps its full float precision.
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

While a modal is up, it has the focus, the widgets outside it don't work, and the game and views get nothing from the devices. Back (Escape or the gamepad's east button) closes it. The modal drawn last is on top. A [locked cursor](./cameras.md#looking-around) comes back while one is up, since its widgets need the pointer.

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

The GUI has one look, and a theme changes its colors, its corners, its borders, shading and shadows, how fast it fades between states, and its text size and spacing. `GUITheme` is a struct whose defaults are that look, so a theme names only what it changes:

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
| `border` | An outline inside the edge of each widget and panel, `borderWidth` pixels wide |
| `shadow` | A soft shadow under each area's panel, fading out over `shadowSize` pixels |

The rest are numbers:

| Field | Default | What it is |
|---|---|---|
| `radius` | 0 | How round the widgets' corners are, in pixels: 0 is square. A part is never rounder than half its smaller side, so a big radius makes a pill of a button |
| `borderWidth` | 0 | How wide `border` is, in pixels: 0 for none |
| `shading` | 0 | How much darker each widget's bottom is than its top, from 0 (flat) to 1 (black): a little, like 0.2, makes buttons look raised |
| `shadowSize` | 16 | How far the panels' shadow fades out, in pixels: 0 for none |
| `transition` | 0.1 | Seconds a widget takes to fade from one state's color to another's, as the pointer comes and goes, and half the time an anchored area takes to grow or shrink to its content: 0 changes at once |
| `textSize` | 16 | The widgets' text size, in pixels. A widget's height follows it: `textSize + 12` |
| `padding` | 12 | Space inside an area, around its widgets |
| `spacing` | 5 | Space between widgets |

A number that isn't a number, or is out of its range, is taken as the nearest one that is: a negative size as 0, a `shading` over 1 as 1.

A theme is set in one of two ways, as every setting of a frame is:

- `GUI.Theme(theme) { ... }` wraps a block: the widgets in it have the theme, and the look it found is back when the block ends.
- `GUI.SetTheme(theme)` stays: for the widgets that follow, in this view and the views after it, until it's set again or the frame ends.

The buttons below pick a theme, and the slider its corners. The menu under them is in a `GUI.Theme` block, so it takes the theme; the buttons and the slider aren't, and keep the look every frame starts with.

::: tide-example gui-theme A theme around a block

<<< @/examples/gui-theme.tide

:::

Every frame starts from the default look, so a view that sets a theme sets it every frame, and a game that sets none looks as the GUI always did. A theme a block doesn't name fields of takes those from the defaults, not from the theme around it. To change one thing of a theme of your own, change a copy of it:

```csharp
const GUITheme LOOK = GUITheme { radius = 10, shading = 0.2, textSize = 18, spacing = 10 };

view Menu()
{
    GUI.SetTheme(LOOK);
    mut var primary = LOOK;  // The same look...
    primary.control = Color(0.25, 0.45, 1); // ...with one color of its own
    GUILayout.Area(Anchor.MiddleCenter)
    {
        GUILayout.Button("Cancel");
        GUI.Theme(primary) { GUILayout.Button("Save"); }
    }
}
```

A toggle's box is never rounder than a quarter of its size, so a theme with round corners still has checkboxes that look like checkboxes.

An area's panel has the look it opened with. The [stats overlay and the debug views](../tools/cli.md) aren't the GUI's widgets, and keep their own look whatever a game sets.

That's all a theme is: there are no styles for single widgets and no fonts. A widget that should look like nothing here is [one you draw yourself](#widgets-of-your-own).

## Keyboard and gamepad

Navigation is built in. Tab and Shift+Tab move the focus between widgets in declaration order, wrapping at the ends. Arrows, the d-pad and the left stick move to a nearby widget in that direction, using its position on screen; at an edge the focus stays put. Enter, Space and the south button press, and Escape and the east button let go. A slider and a number field keep left and right to step their value; while editing, a field uses them to move its caret.

Call `GUI.FocusNext()` conditionally immediately before the control that should receive focus, such as a search field when opening a menu. It selects the next enabled control. `GUI.ClearFocus()` gives the keyboard and gamepad back; a valid number edit is kept when its field next runs.

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

Click to place the caret, or drag to select. Left/Right move a Unicode code point, Home/End go to either end, and Shift extends the selection. Ctrl+Left/Right move by whitespace-separated words; Ctrl+A selects all. Backspace and Delete remove the selection or the adjacent code point. Ctrl+Z undoes, and Ctrl+Y or Ctrl+Shift+Z redoes. Ctrl+C/X copy or cut the selection in the standard host. Arrow and deletion keys repeat while held. Number fields initially select their whole value so typing replaces it.

Editing is single-line and bounded to 255 UTF-8 bytes, with 15 undo steps for the active field. Long edits scroll horizontally to keep the caret visible. Browser IME input commits once and its candidate-confirmation Enter does not submit the field. Inline preedit text, grapheme-cluster movement and bidirectional editing are not yet provided.

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

Drawing a rectangle needs no theme or control:

```csharp
GUI.Draw()
{
    Draw.Rect(Rect(20, 20, 200, 60), Color.blue);
    Draw.Rect(Rect(20, 100, 200, 60), 12, Color.blue);
    Draw.Rect(Rect(20, 180, 200, 60), float4(0, 8, 16, 24), Color.blue);
}
```

These `Rect` overloads have antialiased edges, including with zero roundness. Four radii run clockwise from top left; each is limited to half the smaller side. The existing center/size `Draw.Rect` fills whole pixels, which is useful for adjacent tiles.

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
| `down` | This frame's pointer press belongs to the control |
| `up` | Its pointer was released this frame, including outside the rect |
| `cancelled` | Its held gesture was aborted because it became disabled, covered by a modal or taken by touch scrolling |
| `position` | Pointer position in GUI pixels, including while held outside the rect |

It also has `hover`, `press` and `focus`: `hovered`, `pressed` and `focused` as amounts from 0 to 1, which ease toward them over the theme's `transition`. Blend a control's look by them, and it fades as the pointer comes and goes, with nothing to keep:

```csharp
var card = GUI.Control(rect);
GUI.Draw()
{
    var lift = card.hover * 4 - card.press * 2;
    Draw.Rect(Rect(rect.x, rect.y - lift, rect.width, rect.height), 12,
              Math.Lerp(Color(0.15, 0.2, 0.35), Color(0.25, 0.35, 0.6), card.hover));
}
```

It works as a button of the GUI's does, and the GUI draws nothing for it, so showing that it's hovered or has the focus is the view's:

- **The pointer on it is the GUI's.** A press there never reaches the input's `Sample` or the other views, from the frame it lands in, a finger's too. The view needs no claim.
- **It takes the focus with the other widgets.** Tab, the arrows, the d-pad and the left stick move the focus to it and from it, alongside the GUI's own widgets, in the order they're called. A click doesn't give it the focus, as it doesn't a button.
- **`GUI.Disabled` and modals stop it.** In a disabled block, or outside the modal that's up, nothing of it is true and the focus skips it. Draw it faded yourself.
- **A button looks down while it's `pressed` and `hovered`.** `pressed` alone stays true while the pointer drags away, which is what a handle or a slider of your own follows.

While a control has the focus, its view goes on reading the keyboard and the gamepad, but for what moves the focus and clicks: the arrows, Tab, Enter, Space and Escape, and the d-pad, the left stick and the south and east buttons. So a menu's shortcuts still work with one of its buttons focused, and a control can be a text field: read `devices.keyboard.text`, and call `GUI.ShowKeyboard()` for phones. The input's `Sample` and the other views read none of it meanwhile.

`down` and `up` describe pointer gestures; keyboard or gamepad submission only sets `clicked`. Cancel an in-progress edit when `cancelled` is true. A control that disappears is no longer called, so it cannot report cancellation. Native pointer-capture loss is not yet reported separately.

### Custom controls in layout

Reserve space, interact with it, then draw into the GUI's list:

```csharp
bool Card(string title)
{
    var rect = GUILayout.Reserve(float2(200, 64));
    var control = GUI.Control(rect);
    GUI.Draw()
    {
        var center = float2(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
        Draw.RoundRect(center, float2(rect.width, rect.height), 12,
                       control.hovered || control.focused ? Color.blue : Color.gray);
        Draw.Text(title, center, 18, Color.white, Anchor.MiddleCenter);
    }
    return control.clicked;
}
```

`GUI.Draw` places these draws after the enclosing panel and before later widgets. It uses the existing renderer, including smooth shapes, sprites, meshes and custom shaders. Helpers called inside it draw into the same GUI list. The block starts in screen pixels and restores the GUI window's drawing settings when it ends, including changes made with `Draw.Set...` and when returning early.

One `GUI.Draw` block can wrap a whole group of custom controls; a separate block per widget is optional. It uses the GUI's existing container nesting limit; a draw block beyond that limit is skipped.

Screen-space content follows an anchored panel when it is placed. Explicit custom cameras and 3D geometry keep their own coordinates. Render-target calls still draw to the target they name. Outside `GUI.Draw`, ordinary view drawing is beneath the GUI.

`Reserve(size)` keeps the requested size. `Reserve(size, minWidth: 80, expandWidth: true)` can shrink horizontally to 80 pixels or expand to the column's remembered natural width. Height is explicit; negative and nonfinite dimensions become zero. Reserving space alone draws nothing and takes no input. UI code runs once per frame.

### Animation

`GUI.Animate(target, seconds)` gives `target`, eased toward from where it was last frame: a value that jumps, like a menu's open state, a bar's fill or a color, moves there smoothly instead, about 99% of the way in `seconds`. It takes a `float`, a `float2` or a `Color`, and it moves fastest at first and settles on the target without overshooting, so a target that changes midway turns smoothly.

```csharp
local singleton Pause
{
    bool open;
}

view PauseMenu(Devices devices, mut Pause pause)
{
    if (devices.keyboard.escape.down) pause.open = !pause.open;
    var shown = GUI.Animate(pause.open ? 1.0 : 0.0, 0.25);
    if (shown <= 0) return; // Gone once it has faded out
    GUI.Draw()
    {
        Draw.Rect(Rect(0, 0, Screen.width, Screen.height), 0, Color(0, 0, 0, 0.6 * shown));
    }
    // Slides up from below the screen's middle as the screen darkens
    GUILayout.Area(Rect(Screen.width * 0.5 - 150, Screen.height * 0.5 - 100 + (1 - shown) * 60, 300, 200))
    {
        GUILayout.Label("Paused");
        if (GUILayout.Button("Resume")) pause.open = false;
    }
}
```

The GUI keeps the value from frame to frame, told apart by where `Animate` is called, as widgets are: each turn of a loop has its own, and [`GUI.Key`](#stable-keys) keeps one with its item when items move. Call it every frame:

- **The first time it's called, it's at its target.** A menu that's closed when the game starts is closed, with no fade.
- **`GUI.Animate(target, seconds, from)` starts at `from` instead,** the first frame: `GUI.Animate(1, 0.3, 0)` fades a toast in as it first appears.
- **A frame it isn't called in starts it over.** Keep calling it while something fades out, and stop once it's at its target.
- It settles exactly on its target, so `shown <= 0` is true once a fade out has finished.
- **Something that fades out needs what it shows until it's gone.** A tooltip for the hovered item has no item once the pointer leaves: keep the last one in local state, and draw it while the fade lasts:

```csharp
local singleton Tip
{
    int item = -1; // The last item hovered, kept while its tooltip fades
}

void Tooltip(mut Tip tip, int hovered, float2 at, string text)
{
    if (hovered >= 0) tip.item = hovered;
    var shown = GUI.Animate(hovered >= 0 ? 1.0 : 0.0, 0.15);
    if (shown <= 0 || tip.item < 0) return;
    GUI.Draw()
    {
        // Beside the pointer, and moved back onto the screen near its edges
        var box = Rect(at.x + 16, at.y + 18, Draw.MeasureText(text, 15).x + 24, 36).MoveInside(Screen.rect);
        Draw.Shadow(box, 8, 10, Color(0, 0, 0, 0.5 * shown));
        Draw.Rect(box, 8, Color(0.05, 0.05, 0.08).WithAlpha(0.95 * shown));
        Draw.Text(text, float2(box.x + 12, box.y + 18), 15, Color.white.WithAlpha(shown), Anchor.MiddleLeft);
    }
}
```

An area at an anchor animates by itself: when its content changes size, as a menu's tabs change, its panel grows or shrinks to it over twice the theme's `transition`, staying at its anchor, and its content is clipped to the panel meanwhile.

`GUI.Tween(target, seconds)` moves at a constant speed instead: a change from rest takes exactly `seconds`, and turning back midway takes as long as it went. It's for a progress from 0 to 1 that an [easing function](../language/math.md#functions) shapes, so it runs on a timeline:

```csharp
var t = Math.EaseInOut(GUI.Tween(menu.open ? 1.0 : 0.0, 0.4)); // Slow, fast, slow: 0.4 seconds either way
var at = Math.Lerp(float2(-300, 100), float2(40, 100), t);
var pop = Math.EaseOutBack(GUI.Tween(1, 0.3, 0));               // 0 to 1 once, as it appears, overshooting
```

It takes a `float`. Like `Animate`, it's at its target the first time it's called, starts at `from` given one, and starts over after a frame it isn't called in. A target that changes while it's moving, other than back, keeps the speed it has.

Both go by the frame's length, `Frame.dt`, so they take as long at any frame rate. Up to 1,024 values are animated at once, the widgets' own fades and the anchored areas' sizes among them; past that, a new one isn't animated: it's at its target, or stays at its `from`.

#### Animated values in GPU code

An animated value is a number the view has, worked out once a frame, so GPU code reacts to it as to any other: a [fill](./views.md#fills)'s lambda reads it from around it, and a [frag or a vert](./shaders.md#values-for-one-draw) is given it in the draw's values, or in each instance's own for many at once. Animate on the CPU, where the GUI keeps it from frame to frame; shape it on the GPU, where the easing functions are too, so each pixel can take its own part of one progress:

```csharp
struct Glow
{
    float amount; // 0 to 1, animated on the CPU
}

frag Pulse(Vertex vertex, out Color color, Glow glow)
{
    color = Color(1, 0.8, 0.3, glow.amount * (1 - vertex.uv.y));
}

view Reveal(Menu menu)
{
    // Animated once a frame: a progress half a second long...
    var t = GUI.Tween(menu.open ? 1.0 : 0.0, 0.5, 0);
    GUI.Draw()
    {
        // ...shaped for each pixel: a wipe from left to right with a soft, eased edge
        Draw.Rect(Rect(40, 40, 320, 200), 16, (float2 uv) =>
            Color(0.2, 0.4, 0.9).WithAlpha(Math.EaseOut(Math.Saturate(t * 1.5 - uv.x * 0.5))));
    }
    // A frag's draw values: the glow fades in and out with the menu's light
    Draw.Mesh(menu.vertices, menu.indices, Pulse, Glow { amount = GUI.Animate(menu.lit ? 1.0 : 0.0, 0.2) });
}
```

GPU code keeps nothing from one frame to the next, so it can't ease a value of its own: what it's given is where the animation is this frame.

### Widgets in a render target

`GUI.Target(target, shown) { ... }` draws the widgets in its block into a [render target](./render-targets.md), for a panel that's warped, turned, faded as a whole, or shown on something in the world. They're laid out in the target's logical pixels, its size the screen's for them (an `Area`'s anchors, a modal's dim), and `shown` is where the game shows the target on the screen: the pointer reaches the widgets through it, scaled, and a press on one is the GUI's as on any widget.

```csharp
view Window(mut Desk desk)
{
    if (!desk.content.valid) desk.content = RenderTarget(520, 340);
    desk.content.Clear(Color.white);
    GUI.Target(desk.content, Rect(desk.at.x, desk.at.y, 520, 340))
    {
        GUILayout.Area(Rect(20, 20, 480, 300))
        {
            GUILayout.TextField("Title", desk.title);
            if (GUILayout.Button("Share")) desk.shared++;
        }
    }
    GUI.Draw() // After the block: this frame's widgets
    {
        Draw.Mesh(desk.quad, desk.corners, desk.content.color);
    }
}
```

- **Draw what shows the target after the block, in `GUI.Draw`.** The GUI goes over the world, so a world draw of the target shows the frame before.
- **`GUI.Target(target)`, with no rect, has no pointer:** the keyboard and the gamepad reach its widgets, for a target shown where the pointer can't follow, like a screen on a wall.
- One block at a time: a `GUI.Target` inside another, or for an invalid target, is skipped with its content.

The desktop below draws its window's widgets into a target and shows it as a mesh, which bends into the dock with a genie effect as it minimizes. Type in the field, press Share, drag the slider, then minimize the window and click its thumbnail in the dock.

::: tide-example gui-desktop A window of widgets, minimized into the dock

<<< @/examples/gui-desktop.tide

:::

### Scrolling

`GUILayout.Scroll(size, offset) { ... }` reserves a viewport and lays out its content inside it. Keep its `float2` offset in local state:

```csharp
GUILayout.Scroll(float2(320, 240), menu.scroll)
{
    for (var i = 0; i < items.count; i++)
    {
        GUI.Key(items[i].id)
        {
            if (GUILayout.Button(items[i].title)) menu.selected = items[i].id;
        }
    }
}
```

`GUI.Scroll(rect, offset)` places the viewport at an explicit rect. Offsets are pixels from the content's top left and are clamped to its size. Drawing and pointer hits are clipped together. Wheel input goes to the innermost hovered viewport; unused motion reaches its parent at an edge. A touch can drag the content; moving more than six pixels cancels a child's pending press. The viewport retains that drag until release. Keyboard navigation and `GUI.FocusNext` scroll the focused control into view.

The offset is read when the block opens and written back when it closes, including on an early return. An indexed argument evaluates its index once and checks its current storage again at writeback; removing that list element makes the write a no-op. Maps and grids use their ordinary assignment semantics. The content runs once per frame. Scrolling does not virtualize long lists; reserve and draw only the visible items when that is needed.

### Stable keys

Wrap reorderable items in `GUI.Key` using an application key:

```csharp
for (var i = 0; i < items.count; i++)
{
    var item = items[i];
    GUI.Key(item.id)
    {
        if (Card(item.title)) menu.selected = item.id;
    }
}
```

Keys can be `int`, `long` or `string`. They must be unique among siblings from the same call site. Zero and an empty string are valid. Nested scopes compose, and helpers inherit the scope, so each item can contain the same reusable controls. Focus and held gestures follow the key when other items move or disappear. Keep displayed labels separate from identity when labels can change.

Without a key, repeated calls still use their occurrence: omitting an earlier item gives its identity to the next one. Runtime identities are 32-bit hashes and should not be stored as application keys.

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
