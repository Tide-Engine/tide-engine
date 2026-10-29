---
description: "Make Tide menus and HUDs look polished: themes, gradient fills, soft shadows, controls that ease as they're hovered, and panels that slide in."
---

# Good-looking UI

The [GUI](./gui.md)'s widgets work out of the box, and look plain. Four things make a menu look finished, and each is a line or two:

1. **A theme** for the GUI's own widgets: round corners, a little shading, a border, a bigger text size, more room.
2. **Gradients and shadows** instead of flat rectangles: [fills](./views.md#fills) and soft edges.
3. **Controls that ease** between their states, rather than snapping: a control's `hover` and `press`.
4. **Panels that move** as they open and close: `GUI.Animate`.

The menu below does all four. Hover the buttons, press Options, and press Play.

::: tide-example gui-style A menu with a look of its own

<<< @/examples/gui-style.tide

:::

## A theme

Set one theme for the menu's widgets, rather than a color here and there. A few numbers change the most:

```csharp
const GUITheme LOOK = GUITheme {
    radius = 10,        // Round corners
    shading = 0.2,      // Each widget a little darker at its bottom: raised
    borderWidth = 1,    // A faint edge, which separates widgets from a panel of a similar color
    border = Color(1, 1, 1, 0.1),
    textSize = 18,      // Widgets grow with it
    padding = 18,       // Room inside panels...
    spacing = 10 };     // ...and between widgets
```

Wrap the widgets in `GUI.Theme(LOOK) { ... }`, or call `GUI.SetTheme(LOOK)` at the start of the view. Every panel has a soft shadow and every widget fades between its colors by default; `shadowSize` and `transition` change them. Pick colors from one family: a dark panel, controls a step lighter, the hovered color a step lighter again, and one bright `accent`.

## Gradients and shadows

Never build a gradient from strips of rectangles: give the shape a fill. A [fill](./views.md#fills) is a function the GPU runs for each pixel, and `Gradient`'s functions are the common ones:

```csharp
// The screen's background, lighter at the top
Draw.Rect(Rect(0, 0, Screen.width, Screen.height), 0, Gradient.Vertical(Color(0.1, 0.12, 0.24), Color(0.04, 0.05, 0.1)));
// A raised button
Draw.Rect(rect, 14, Gradient.Vertical(Color(0.35, 0.55, 1), Color(0.25, 0.4, 0.85)));
// A glow behind a title, fading to nothing
Draw.Rect(Rect(x - 220, y - 100, 440, 200), 100, Gradient.Radial(Color(0.35, 0.45, 1, 0.35), Color.clear));
```

A shadow goes under a box, drawn first: `Draw.Shadow(rect, radius, size)` is a soft dark shape the box's size, fading out over `size` pixels and a little lower. A light color, `Draw.Shadow(rect, radius, size, Color(1, 0.8, 0.3, 0.5))`, is a glow.

```csharp
Draw.Shadow(card, 14, 12);
Draw.Rect(card, 14, Gradient.Vertical(Color(0.17, 0.15, 0.25), Color(0.09, 0.08, 0.14)));
```

A heading is a label at a size of its own, `GUILayout.Label("Settings", 24)`, and `color.WithAlpha(a)` fades any color, like a glow of an item's rarity color: `glow.WithAlpha(0.6 * chosen)`.

Fills drawn one after another are drawn together, so a screen of gradient buttons costs no more than one.

## Controls that ease

A [control](./gui.md#widgets-of-your-own) says how hovered, pressed and focused it is as amounts from 0 to 1, `hover`, `press` and `focus`, which ease over the theme's `transition`. Blend by them rather than switching on `hovered`:

```csharp
bool BigButton(Rect rect, string text)
{
    var button = GUI.Control(rect);
    GUI.Draw()
    {
        var lift = button.hover * 3 - button.press * 2; // Up as the pointer comes, down as it's pressed
        var at = Rect(rect.x, rect.y - lift, rect.width, rect.height);
        Draw.Rect(at, 14, Gradient.Vertical(Math.Lerp(Color(0.35, 0.55, 1), Color.white, button.hover * 0.15),
                                            Color(0.25, 0.4, 0.85)));
        Draw.Text(text, float2(at.x + at.width * 0.5, at.y + at.height * 0.5), 22, Color.white, Anchor.MiddleCenter);
    }
    return button.clicked;
}
```

Show `focus` too, as an outline: players on a gamepad or a keyboard see where they are.

## Panels that move

`GUI.Animate(target, seconds)` eases a value toward its target from frame to frame. Give it what the panel should be, open or closed, and draw the panel by what it gives:

```csharp
var shown = GUI.Animate(menu.options ? 1.0 : 0.0, 0.3);
if (shown > 0)
{
    GUILayout.Area(Rect(Screen.width - 340 * shown, 180, 320, 220)) // In from the right
    {
        // ...
    }
}
```

It settles exactly on its target, so `shown > 0` stops drawing the panel once it has slid out. Something that should come in as it first appears starts from a value of its own: `GUI.Animate(1, 0.4, 0)` goes from 0 to 1 the first time it's called. Keyed with `GUI.Key`, a new key starts again, as the "Starting..." note above does each time Play is pressed.

A panel at an anchor needs none of this to change size smoothly: `GUILayout.Area(Anchor.MiddleCenter)` grows and shrinks to its content by itself, as the tabs of a dialog change. For a tooltip, keep what it shows while it fades, and keep it on the screen with `rect.MoveInside(Screen.rect)` (see [Animation](./gui.md#animation)).

For a fixed-length animation, a slide that eases in and out or a pop that overshoots, `GUI.Tween` gives a progress that takes exactly its time, and `Math.EaseInOut`, `Math.EaseOutBack` and the rest shape it. A panel that's warped or bent as a whole draws its widgets into a render target with [`GUI.Target`](./gui.md#widgets-in-a-render-target).

An animated value goes to GPU code like any other: a fill's lambda reads it, and a frag is given it in its [draw's values](./shaders.md#values-for-one-draw). Animate on the CPU and shape on the GPU: a wipe whose edge eases, a glow that pulses with a button's `hover`. See [Animated values in GPU code](./gui.md#animated-values-in-gpu-code).

Animate positions, sizes, colors and alpha; keep times short, a tenth to a third of a second, so the menu feels quick.
