---
description: "Tide's draw settings: blending, alpha, tints, stencil, transforms and clipping, each around a block or set for the draws that follow, and how to draw what's see-through."
---

# Draw settings

A setting changes how everything drawn after it comes out: how it blends, what tints it, what it does with depth and stencil, which side of it shows, where it lands. Each has two forms, and its name says which.

The plain name wraps a block. It's set for the draws in the block, and when the block ends, what it found is back:

```csharp
Draw.Blend(Blend.Add)
{
    Draw.Circle(spark.position, 2, spark.color);
}
```

The `Set` name is a statement. It stays for the draws that follow, in this view and the views after it, until it's set again or the frame ends, which is how a camera is set once for a frame:

```csharp
view Follow(Player player, with ownership)
{
    Draw.SetCamera(player.position, 10);
}
```

| Around a block | For the draws that follow | What it sets |
|---|---|---|
| `Draw.Blend(mode)` | `Draw.SetBlend(mode)` | How colors go over what's there (see [Blending](#blending)) |
| `Draw.Tint(color)` | `Draw.SetTint(color)` | A color everything is multiplied by (see [Tints](#tints)) |
| `Draw.Stencil(test, value)`, `Draw.Stencil(test, value, operation)` | `Draw.SetStencil(test, value)`, `Draw.SetStencil(test, value, operation)` | The test draws pass, against a number kept for each pixel, and what becomes of that number where they draw (see [Stencil](#stencil)) |
| `Draw.Transform(...)`, `Draw.Transform(transform)` | `Draw.SetTransform(...)`, `Draw.SetTransform(transform)` | Where draws land: moved, turned, scaled (see [Transforms](#transforms)) |
| `Draw.Clip(rect)` | `Draw.SetClip(rect)` | The rectangle draws are kept inside (see [Clipping](#clipping)) |
| `Draw.Camera(center, size)`, `Draw.Camera(...)` | `Draw.SetCamera(center, size)`, `Draw.SetCamera(...)` | The camera |
| `Draw.Screen()` | `Draw.SetScreen()` | The screen's pixels in the camera's place (see [On the screen](./views.md#on-the-screen)) |
| `Draw.Depth(mode)` | `Draw.SetDepth(mode)` | What 3D meshes do with depth: whether they're hidden behind what's nearer, and whether they hide what's behind them (see [Depth](./3d.md#depth)) |
| `Draw.Cull(side)` | `Draw.SetCull(side)` | Which side of a 3D mesh's triangles isn't drawn: the back, unless this says otherwise (see [Which side draws](./3d.md#which-side-draws)) |

Every frame starts from the defaults, so a `Set` never outlives its frame, and the GUI and the stats are never drawn with what a game set. A `Set` with no arguments puts the default back early: `Draw.SetTint()`, `Draw.SetStencil()`, `Draw.SetClip()`, `Draw.SetDepth()`, `Draw.SetCull()`; `Draw.SetCamera()` puts both cameras back, the 2D and the 3D, and `Draw.SetTransform()` both transforms.

Writing one form as the other is a compile error that names the right one: `Draw.Blend(Blend.Add);` with no block says to write `Draw.SetBlend`, and `Draw.SetBlend(Blend.Add) { }` says `Draw.Blend`. Your editor offers the rename.

A block that's left early still puts back what it found, as a GUI container still closes:

```csharp
Draw.Tint(Color(1, 1, 1, 0.5))
{
    if (!card.shown) return; // The tint is gone, here too
    DrawCard(card);
}
```

Every setting belongs to the destination it's called on. `Draw`'s are the window's, and a [render target](./render-targets.md) has its own, by the same names (`target.Blend(Blend.Add) { ... }`, `target.Clip(rect) { ... }`, `target.SetCamera(...)`, `target.Depth(mode) { ... }`): a blend or a tint set for the window doesn't reach a target's draws, and a target's doesn't reach the window's. Depth and cull are for 3D meshes alone: around 2D draws they change nothing, and that's no error.

::: info
What a `Set` with no arguments does, how one setting nests in another, `Draw.Depth`'s name and its three modes', `Cull`'s three names and which way round a triangle's front goes, and how `Draw.Stencil` is written, its tests' and operations' names and each of its rules, are provisional: they're how it works today, and may still change.
:::

## Several at once

Calls that take a block can be listed with commas before one block, so settings don't nest three deep:

```csharp
Draw.Blend(Blend.Add),
Draw.Tint(Color(1, 1, 1, shown)),
Draw.Clip(panel)
{
    DrawGlow(panel);
}
```

It means the same as each inside the one before. That's the language's, not `Draw`'s: the [GUI](./gui.md#guilayout-and-gui)'s containers and [your own functions that take a block](../language/functions.md#actions) go in the same list.

## Blending

`Blend.Alpha` is the default: a color goes over what's there by its alpha. The others:

| Mode | What it does | For |
|---|---|---|
| `Blend.Add` | Adds the color to what's there, as light adds | Sparks, trails, glows, fire |
| `Blend.Multiply` | Multiplies what's there by the color: white changes nothing, black makes black | Shadows, stains, tinted glass |
| `Blend.Replace` | Puts the color and its alpha in place of what's there | Writing into a [render target](./render-targets.md) exactly |

In each, a color's alpha is how much of it there is, so what's clear changes nothing: a shape's smooth edge and the gaps between a text's strokes stay clean when they're added or multiplied.

```csharp
view Trails(Bullet bullet)
{
    Draw.Blend(Blend.Add)
    {
        // Where trails cross, they're brighter
        Draw.Line(bullet.position - bullet.velocity * 0.05, bullet.position, 0.2, Color(1, 0.6, 0.2, 0.6));
    }
}
```

`Draw.Clear`, and a target's `Clear`, fill with their color whatever the blend, and [`Draw.Blit`](./render-targets.md#post-processing) takes a blend of its own (`Draw.Blit(source, destination, Blend.Add, frag)`).

## Alpha

A color's alpha is how much of it there is, and you write colors as they are everywhere: `Color(1, 0, 0, 0.5)` is red, half clear, in a draw, a tint, a grid's cell, a PNG and a frag. There's nothing to multiply yourself and no second kind of blend to pick.

See [Transparency](#transparency) for drawing something see-through, fading it, and cutting holes in a 3D mesh.

What that buys:

- **No fringes.** A sprite's clear pixels have a color too in its file, often black or white, which never shows. Scaled or turned with `Filter.Bilinear`, a pixel at the sprite's edge is blended with its clear neighbor, and the neighbor counts for none of the color: a white sprite over a white wall has no gray rim, whatever the file kept in its clear pixels.
- **Layers.** What's drawn into a cleared [render target](./render-targets.md), `target.Clear(Color.clear)`, and then drawn from it with `target.color` looks as it would drawn straight onto the window: a HUD, a minimap or a trail kept in a target goes over the game as it is.

A few things follow from how that's kept, which is each color times its alpha, in textures and in targets:

- **A pixel with no alpha has no color.** A frag that samples one gets `Color(0, 0, 0, 0)`, whatever the file or the cell said its color was. An image whose alpha is something other than how clear it is (a mask beside unrelated colors) would lose its colors where the alpha is 0: mark it [`[Raw]`](./shaders.md#textures-that-are-numbers), and a frag reads its numbers as the file has them.
- **`Blend.Replace` with an alpha under 1** leaves the window, which has no alpha of its own, darker by that much, as if over black. Into a target it's exact: drawn from the target later, it's the color and the alpha that were written.
- **`Blend.Add` raises the alpha that's there** towards 1 by its own, and never past it, so light added into a clear target shows when the target is drawn over something.
- A dim color that's nearly clear keeps few of its 256 steps in an image of bytes. It doesn't show once it's blended; a [`Format.Rgba16f`](./render-targets.md#what-a-target-holds) target keeps them all.

## Tints

A tint multiplies the color of everything drawn inside, alpha too. `Color(1, 1, 1, 0.5)` fades it to half, with no alpha to pass to every call:

```csharp
view Menu(MenuState menu)
{
    Draw.Screen(),
    Draw.Tint(Color(1, 1, 1, menu.shown))
    {
        Draw.RoundRect(float2(200, 120), float2(320, 160), 12, Color(0.1, 0.1, 0.15));
        Draw.Text("Paused", float2(160, 80), 32, Color.white);
    }
}
```

A tint in a tint's block multiplies it: a faded menu's faded button is fainter than either.

## Stencil

Beside its color and its depth, every pixel has a whole number from 0 to 255, its stencil. Nothing shows it. Draws store numbers there, and other draws are drawn only where the number passes a test, which is how one thing is cut to the shape of another: a view through a porthole, a map inside a round frame, a mirror that shows its reflection only where the glass is, an outline around whatever is selected.

`Draw.Stencil(test, value, operation)` says both things for the draws in its block: the test they pass to be drawn at a pixel, their `value` against the number stored there, and what becomes of the stored number where they are drawn. It usually takes two steps. Something stores a number where it is, then something else is drawn where that number is:

```csharp
view Porthole(Ship ship)
{
    // The glass stores 1 where it is. It's drawn clear: it's there for what it stores
    Draw.Stencil(StencilTest.Always, 1, StencilOp.Replace)
    {
        Draw.Circle(ship.window, 3, Color.clear);
    }
    // The sea is drawn where the number is 1, and nowhere else
    Draw.Stencil(StencilTest.Equal, 1)
    {
        DrawSea(ship);
    }
}
```

Move the lens below. The wall is drawn where the lens didn't store its number, and the pipes where it did:

::: tide-example stencil A lens, cut out of a wall

<<< @/examples/stencil.tide

:::

The test is between the draw's `value`, on the left, and the stored number:

| Test | A pixel is drawn where |
|---|---|
| `StencilTest.Always` | Everywhere: no test, which is what every frame starts with |
| `StencilTest.Never` | Nowhere |
| `StencilTest.Equal` | The stored number is `value` |
| `StencilTest.NotEqual` | It's any other |
| `StencilTest.Less` | `value` is less than the stored number |
| `StencilTest.LessEqual` | `value` is less than it, or the same |
| `StencilTest.Greater` | `value` is greater than the stored number |
| `StencilTest.GreaterEqual` | `value` is greater than it, or the same |

The operation says what becomes of the stored number at each pixel the draw is drawn at. Without one it stays as it is, so `Draw.Stencil(test, value)` only tests:

| Operation | The stored number becomes |
|---|---|
| `StencilOp.Keep` | What it was: the draw only tests |
| `StencilOp.Replace` | `value` |
| `StencilOp.Increment` | One more, 255 at the most |
| `StencilOp.Decrement` | One less, 0 at the least |
| `StencilOp.Zero` | 0 |
| `StencilOp.Invert` | 255 less it: each of its 8 bits the other way |

- **Every pixel's number is 0 when a frame starts,** in the window. A [render target](./render-targets.md) keeps its numbers from frame to frame, as it keeps its colors, until it's cleared.
- **A draw stores where it's drawn, whatever its color.** `Color.clear` stores and shows nothing, which is how to lay a mask down without painting anything. That's so under every [blend](#blending) but `Blend.Replace`, which puts the clear color there.
- **Where it's drawn is every pixel it touches:** all of a rect, a circle out to its edge, a smooth shape to the pixels its edge only partly covers, text where its glyphs are (not the boxes around them), and all of a mesh's triangles, whatever its texture's alpha is there. For a mask the shape of a picture, draw it through a [frag](./shaders.md) that leaves out what's clear with `this.Discard()`: a pixel a frag discards stores nothing.
- **It's cut along pixels.** A pixel is in a mask or it isn't, so what's drawn where a mask is has a stepped edge where the mask's shape is round, however smooth the shape itself would be drawn. Draw a rim over it with no test, as the example does, where that shows.
- **A draw that fails the test changes nothing,** and neither does a 3D mesh where it's hidden behind what's nearer: the number changes only where the draw is drawn.
- **It's for everything that draws:** shapes, text, meshes, 3D meshes and the [primitives](./3d.md#primitives), with a frag or without. Draws in a row under the same stencil are still one draw.
- **`Draw.Clear`, and a target's `Clear`, set every number back to 0,** within the [clip](#clipping), whatever is set, and are tested against nothing. [`Draw.Blit`](./render-targets.md#post-processing) neither tests nor stores, whatever is set.
- **A render target has its own numbers, and its own setting:** `target.Stencil(StencilTest.Equal, 1) { target.Rect(...); }`. `Draw.Stencil` is the window's, and doesn't reach what's drawn into a target; a target's doesn't reach the window.
- **`value` is any `int`.** Under 0 it's 0, and over 255 it's 255. The test and the operation are values too: `Draw.Stencil(inside ? StencilTest.Equal : StencilTest.NotEqual, 1)`.
- **A block's stencil replaces the one it found,** and puts it back when it ends. `Draw.SetStencil()` puts back none: every pixel passes, and nothing is stored.

Counting is the other use. Each of these adds one where it's drawn, so afterwards a pixel's number is how many of them cover it, and a draw under `StencilTest.Less` with a `value` of 1 is drawn where more than one does:

```csharp
Draw.Stencil(StencilTest.Always, 0, StencilOp.Increment)
{
    foreach (var zone in zones.list) Draw.Circle(zone.center, zone.radius, Color.clear);
}
Draw.Stencil(StencilTest.Less, 1) // Where 1 is less than the count: two zones or more
{
    Draw.Rect(zones.middle, zones.size, Color(1, 0.2, 0.2, 0.5));
}
```

What a GPU's stencil has beyond this isn't here: a setting for each side of a triangle, one for what happens where a mesh is hidden, and masks over the number's bits.

## Transforms

A transform moves, turns and scales everything drawn inside, in the units the calls are in. `Draw.Transform` takes an offset, then a rotation in radians, then a scale, each optional after the first:

```csharp
view Cards(Card card)
{
    // Scaled about the card's middle, then turned, then moved there
    Draw.Transform(card.position, card.tilt, 1 + card.lift * 0.1)
    {
        Draw.RoundRect(float2(0, 0), float2(3, 4), 0.3, Color.white);
        Draw.Text(card.name, float2(-1.2, 1.6), 0.5, Color.black);
    }
}
```

What's inside is drawn around (0, 0), and the transform puts it in the world: scaled, then turned (from `+x` towards `+y`, as [smooth shapes](./views.md#smooth-shapes) turn), then moved. The scale is a number, or a `float2` to stretch more one way than the other, and `Draw.Transform(matrix)` takes any `float3x3` instead. `Draw.Transform(transform)` does the same for 3D meshes with a `float4x4`. A transform in a transform's block is inside it, so a card's badge can have one of its own.

What's drawn through one:

- Shapes stay shapes, with their smooth edges: moved, turned and scaled, exactly. Stretched, a circle is the ellipse it becomes, and a rect follows its own sides; a skew is approximated. Rects that are only moved and scaled still fill whole pixels, so tiles under a transform leave no seams.
- Meshes go through any transform as they are.
- Text that's moved and scaled as much each way is text of that size, as sharp as any. Turned or stretched, it has no pixels to be fitted to, so it's a little softer than upright text: fine for a tilting card, and worth avoiding for text people read at length.
- A clip made inside one is the upright rectangle around what its `Rect` becomes: clips don't turn.

`Draw.WorldToScreen` and the other [conversions](./cameras.md#screen-and-world-coordinates) are the camera's alone, whatever the transform. [`Draw.MeasureText`](./views.md#measuring-text) answers in the units the transform takes, so inside one, text measures as it's drawn.

## Clipping

`Draw.Clip(rect) { ... }` keeps what's drawn in its block inside a rectangle: meshes, shapes, text, and `Draw.Clear`, which then fills the rectangle.

```csharp
view Panel()
{
    Draw.Screen(),
    Draw.Clip(Rect(20, 20, 200, 100))
    {
        Draw.Clear(Color.black); // The panel only
        Draw.Text("A line too long for the panel is cut off at its edge", float2(28, 28), 18, Color.white);
    }
}
```

The `Rect` goes from (x, y) to (x + width, y + height), in the units the calls are in: the world's under a camera, where `y` is up, and pixels on the screen. It stays where it is on the screen when the camera changes afterwards.

A clip in a clip's block only narrows it: what draws is inside both, which is what a list in a panel wants. `Draw.SetClip(rect)` is the [other form](#draw-settings), for the draws that follow, and sets the rectangle outright; `Draw.SetClip()` draws everywhere again. Each frame starts with no clip, and a view's clip never clips the GUI.

## Transparency

Set a color's **alpha**, its fourth number, to the opacity you want: `0` is invisible, `0.5` is half opaque, and `1` is solid. `Color(r, g, b)` is solid. The default [`Blend.Alpha`](#blending) blends the color over what's already drawn, so shapes and text need no extra setting:

```csharp
view Overlay()
{
    Draw.Screen()
    {
        Draw.Rect(float2(160, 100), float2(240, 80), Color(0.18, 0.83, 0.75, 0.5));
        Draw.Text("Half opaque", float2(40, 60), 20, Color(1, 1, 1, 0.5));
    }
}
```

Draw the background first, then what goes over it. This is also how a **sprite with transparent pixels** works: give `Draw.Mesh` its PNG or texture, and the pixel's alpha supplies its opacity. A mesh's vertex colors multiply that alpha, and a [frag](./shaders.md#a-frag) controls it through its `out Color`.

To **fade a whole set of draws**, wrap them in [`Draw.Tint(Color(1, 1, 1, opacity))`](#tints). This keeps their colors and multiplies each draw's alpha by `opacity`. Each draw still blends separately: if overlapping elements should fade as one finished layer, draw them into a [render target](./render-targets.md) cleared with `Color.clear`, then tint the draw of `target.color`. Colors stay as you wrote them; Tide handles [premultiplied alpha](#alpha) internally.

### Transparent 3D

A 3D mesh also has depth. For glass, smoke and other translucent surfaces:

1. Draw solid geometry first with the default `Depth.ReadWrite`.
2. Draw translucent geometry inside `Draw.Depth(Depth.Read) { ... }`, with alpha below 1. It tests against the solid geometry's depth and writes no depth itself.
3. Draw the translucent surfaces **farthest first**, then the nearer ones, so they blend over what's behind them.

Change **Opacity** below. The amber cube is behind the panes and shows through them; the solid pillar in front hides them. The camera stays fixed, so the rose pane is always farther away and is drawn first. The bar above the scene uses the same opacity on a 2D shape.

::: tide-example transparency Opacity in 2D and 3D

<<< @/examples/transparency.tide

:::

Tide draws in the order your views and calls give it; it **doesn't sort transparent entities or a mesh's triangles by distance**. For a moving camera, gather your transparent objects in a local list, [sort it](../language/text-and-lists.md#sorting) from far to near for that camera, then draw them in that order. Intersecting transparent surfaces, or faces within one mesh, may need to be split and ordered too; sorting whole objects alone can't resolve every overlap.

- **Alpha doesn't turn depth writes off.** Even alpha 0 can hide later geometry under `Depth.ReadWrite`. Use `Depth.Read` for translucency, or discard the pixel for a hole.
- **`Depth.None` also stops testing depth,** so the mesh shows through solid walls. Use `Depth.Read` when walls should hide it.
- **A thin pane has a back too.** If it should show from either side, use [`Draw.Cull(Cull.None)`](./3d.md#which-side-draws). On a closed mesh that also draws the far faces, whose alpha blends with the near ones.
- **A target has its own settings:** use `target.Depth(Depth.Read)` and `target.Tint(...)` for its draws. `Draw`'s settings reach the window alone.

### Cutout holes

Leaves, a fence or a sprite standing in a 3D world often want solid pixels with holes between them. A frag can discard pixels below an alpha threshold and keep the rest opaque:

```csharp
frag Cutout(Vertex3 vertex, out Color color, Texture image)
{
    color = image.Sample(vertex.uv);
    if (color.a < 0.5) this.Discard();
    color.a = 1;
}

// Keep the default Depth.ReadWrite: only the surviving pixels hide what's behind.
Draw.Mesh(vertices, indices, transform, texture, Cutout);
```

[`this.Discard()`](./shaders.md#where-the-pixel-is) ends the frag and writes no color, depth or stencil for that pixel. The surviving pixels can use the usual opaque draw order. The threshold gives a hard edge; use the translucent recipe above when you want partial opacity at the edge instead.
