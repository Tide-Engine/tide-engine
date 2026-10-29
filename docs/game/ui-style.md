---
description: "Style Tide menus with a Material-inspired example: tonal surfaces, pill buttons, subtle state layers, themed settings and animated feedback."
---

# Good-looking UI

The [GUI](./gui.md)'s widgets work out of the box. A consistent palette, clear type hierarchy and a little motion give a menu its own character.

This example takes inspiration from Material Design: a light surface, purple primary and tonal actions, pill-shaped buttons and subtle state layers. Settings opens a compact card; Play shows a snackbar. It uses Tide's built-in font and widgets, so it is an interpretation rather than a complete Material component library.

Hover the buttons, open Settings, and press Play. Keyboard and gamepad focus have a visible outline.

::: tide-example gui-style A Material-inspired menu

<<< @/examples/gui-style.tide

:::

## A theme

Set one theme for the menu's widgets, rather than a color here and there. A few numbers change the most:

```csharp
const GUITheme LOOK = GUITheme {
    text = INK, panel = Color(0.95, 0.92, 0.98), control = TONAL,
    hot = Color(0.85, 0.80, 0.94), pressed = Color(0.79, 0.73, 0.88), accent = PRIMARY,
    radius = 20, shading = 0, borderWidth = 0,
    shadow = Color(0.11, 0.10, 0.13, 0.10), shadowSize = 8,
    textSize = 16, padding = 20, spacing = 10, transition = 0.15 };
```

Wrap widgets in `GUI.Theme(LOOK) { ... }`, or call `GUI.SetTheme(LOOK)` at the start of the view. The example uses flat tonal surfaces (`shading = 0`) and a quiet panel shadow. Give primary actions a strong fill, secondary actions a softer tone, and text enough contrast against both. `textSize`, `padding` and `spacing` establish a consistent scale.

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
bool MaterialButton(Rect rect, string text)
{
    var button = GUI.Control(rect);
    GUI.Draw()
    {
        Draw.Rect(rect, 24, PRIMARY);
        Draw.Rect(rect, 24, Color.white.WithAlpha(button.hover * 0.08 + button.press * 0.12));
        Draw.Text(text, float2(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5),
                  16, Color.white, Anchor.MiddleCenter, Font.Bold);
    }
    return button.clicked;
}
```

A translucent state layer makes interaction visible while keeping the button's position stable. The complete example also draws `focus` as an outline so players on a gamepad or keyboard see where they are.

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

It settles exactly on its target, so `shown > 0` stops drawing the panel once it has slid out. Something that should come in as it first appears starts from a value of its own: `GUI.Animate(1, 0.4, 0)` goes from 0 to 1 the first time it's called. Keyed with `GUI.Key`, a new key starts again, as the snackbar above does each time Play is pressed.

A panel at an anchor needs none of this to change size smoothly: `GUILayout.Area(Anchor.MiddleCenter)` grows and shrinks to its content by itself, as the tabs of a dialog change. For a tooltip, keep what it shows while it fades, and keep it on the screen with `rect.MoveInside(Screen.rect)` (see [Animation](./gui.md#animation)).

For a fixed-length animation, a slide that eases in and out or a pop that overshoots, `GUI.Tween` gives a progress that takes exactly its time, and `Math.EaseInOut`, `Math.EaseOutBack` and the rest shape it. A panel that's warped or bent as a whole draws its widgets into a render target with [`GUI.Target`](./gui.md#widgets-in-a-render-target).

An animated value goes to GPU code like any other: a fill's lambda reads it, and a frag is given it in its [draw's values](./shaders.md#values-for-one-draw). Animate on the CPU and shape on the GPU: a wipe whose edge eases, a glow that pulses with a button's `hover`. See [Animated values in GPU code](./gui.md#animated-values-in-gpu-code).

Animate positions, sizes, colors and alpha; keep times short, a tenth to a third of a second, so the menu feels quick.
