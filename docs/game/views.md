---
description: "Draw a Tide game with views: the camera, shapes, colors, text and the screen's pixels, motion that's smooth between ticks, and a frame's length."
---

# Views and drawing

A **view** draws the game. It looks like a system and takes the same parameters, components and singletons, but it runs once per rendered frame instead of once per tick, and it never changes the match. It reads this machine's devices through a `Devices devices` parameter, and can't read the input (see [Input](./input.md#devices-in-the-match)).

```csharp
view DrawBalls(Body body, Ball ball)
{
    Draw.Circle(body.position, body.radius, ball.color);
}

view DrawHud(Arena arena)
{
    Draw.SetCamera(float2(0, 0), arena.halfSize.y);
    Draw.Text("fire: space", float2(-arena.halfSize.x, arena.halfSize.y), 18, Color.gray);
}
```

Drawing is immediate mode: views record their drawing calls every frame. Views run after the frame's ticks, in the order they're declared, and later ones draw on top. `[Before]` and `[After]` order views among themselves, as they do systems. A frame can draw as much as it needs: shapes go to the GPU together, so tens of thousands of rects or circles a frame are cheap, on the web too.

A view's `mut` parameters, `Spawn`, `Add`, `Remove`, `Destroy` and `Send` are all local: views can change this machine's own state, like particles or a menu, but not the match (see [Local state](./local-state.md)). Only views, and the functions they call, can draw.

For expensive preparation such as voxel meshing, start a local async function
that awaits a [local worker](./tasks.md#local-workers), and draw the last
completed geometry while it works. A heavy calculation directly in a view
blocks that frame.

`[RunInEditor]` also runs a view in the native prefab editor's compiled preview.
`[EditorOnly]` runs it only there; an unmarked view runs only in the game. Choose
one attribute, without arguments. Both keep the ordinary view access rules.
The preview has authored values/defaults and its own local visual state, with
no gameplay handlers or systems. See [the native editor](../tools/editors.md#native-scene-editor)
for initialization, camera and service limits.

For see-through shapes, sprites and 3D meshes, see [Transparency](./draw-settings.md#transparency): alpha controls opacity, and 3D also needs the right depth setting and draw order.

Tap a circle to select it, then change the camera's size. The grid and circles use world coordinates; the heading and slider keep their size in screen pixels. Pointer picking uses `ScreenToWorld` with that frame's camera.

::: tide-example views A view draws the world

<<< @/examples/views.tide

:::

Views can share [named phases](./systems.md#named-phases) instead of depending on individual view names:

```csharp
enum RenderStages { Prepare, Objects, Effects, UI }

[Stage(RenderStages.Prepare)]
view ClearScene() { Draw.Clear(Color.black); }

[Stage(RenderStages.Objects)]
view DrawBalls(Body body, Ball ball)
{
    Draw.Circle(body.position, body.radius, ball.color);
}

[After(RenderStages.Objects), Before(RenderStages.UI)]
view Effects() { ... }
```

An offset such as `[Stage(RenderStages.Objects, 10)]` runs later within `Objects`. `After(RenderStages.Objects)` waits until every view in that phase has run for all its entities, including views with offsets.

This page has what a 2D game draws with: the draw functions, colors, shapes, the screen, text, and how a view sees time. The rest is on pages of its own:

- [Views and the match](./views-and-the-match.md): entities other than a view's own, the ones this machine's player owns, and what the server has confirmed.
- [Draw settings](./draw-settings.md): blending, tints, stencil, transforms and clipping, and what's see-through.
- [Meshes and textures](./meshes-and-textures.md): triangles, and the images they're drawn with.
- [Drawing in 3D](./3d.md): 3D meshes and primitives, depth, and which side of a triangle draws.
- [Cameras and coordinates](./cameras.md): between the screen and the world, a camera that turns with the mouse, projections, and cameras as matrices.
- [Render targets](./render-targets.md): drawing into an image, and post-processing.
- [Frags and verts](./shaders.md): code of the game's that runs on the GPU.

## Draw functions

`Draw` draws into the window: shapes, text and 2D meshes, 3D meshes and primitives, with a 2D camera and a 3D camera of its own. The 2D and 3D forms of a function are told apart by their arguments: `Draw.Camera(center, size)` is the 2D camera and `Draw.Camera(position, rotation, fieldOfView)` the 3D one, `Draw.Mesh(vertices, indices)` draws a `List<Vertex>` where its vertices are and `Draw.Mesh(vertices, indices, transform)` a `List<Vertex3>` in the world, and `Draw.WorldToScreen` takes a `float2` through the 2D camera or a `float3` through the 3D one. A [render target](./render-targets.md) has the same functions as its methods; `Draw.Clear` clears the window and its depth, and `Draw.list` hands the frame's draw list to C.

Positions and sizes are in world units, with `y` up. The camera maps them to the screen.

| Function | What it draws |
|---|---|
| `Draw.Clear(color)` | Fills the screen |
| `Draw.ClearDepth()` | Sets depth back and leaves the picture: 3D meshes drawn after are in front of everything before (see [3D](./3d.md)) |
| `Draw.SetCamera(center, size)` | Sets the camera for the calls after it: `center` is the world position at the middle of the screen, and `size` is half the visible height, like Unity's orthographic size. One of the [draw settings](./draw-settings.md), like the rows that name them below |
| `Draw.Circle(center, radius, color)` | A filled circle |
| `Draw.WireCircle(center, radius, color)` | A circle's outline |
| `Draw.Rect(center, size, color)` | A filled rectangle |
| `Draw.WireRect(center, size, color)` | A rectangle's outline |
| `Draw.Line(from, to, color)` | A line a pixel wide |
| `Draw.Line(from, to, width, color)` | A line `width` thick, with round ends: the first of the [smooth shapes](#smooth-shapes), as are the rows from here to `Draw.Shape` |
| `Draw.WireRect(center, size, width, color)`, `Draw.WireCircle(center, radius, width, color)` | Outlines `width` thick |
| `Draw.Rect(center, size, rotation, color)` | A rectangle turned about its center |
| `Draw.RoundRect(center, size, radius, color)` | A rectangle with round corners. `RoundRect(center, size, radius, rotation, color)` turns it, and `WireRoundRect(center, size, radius, width, color)` is its outline |
| `Draw.Ellipse(center, radii, color)` | An ellipse. `Ellipse(center, radii, rotation, color)` turns it, and `WireEllipse(center, radii, width, color)` is its outline |
| `Draw.Arc(center, radius, start, sweep, width, color)` | Part of a ring |
| `Draw.Shape(shape)` | A [`Shape`](#the-general-shape): a fill from one color to another, an outline around a fill, an edge that fades |
| `Draw.Rect(rect, radius, fill)`, `Draw.Shape(shape, fill)` | A shape [filled by a function](#fills) of where in it each pixel is: a `Gradient`, or any `Func<float2, Color>` |
| `Draw.Text(text, position, size, color)` | Text, with `position` its top left corner and `size` its font size, the height of a line of it (see [Text](#text)) |
| `Draw.Text(text, position, size, color, anchor)` | Text with `position` another place of its box: `Anchor.MiddleCenter` centers it there |
| `Draw.Text(text, position, size, color, font)` | Text in a weight: `Font.Bold`. An anchor goes before it: `Draw.Text(text, position, size, color, anchor, font)` |
| `Draw.MeasureText(text, size)` | Nothing: it returns how big `Draw.Text` would draw the text, a `float2` of its width and height (see [Measuring text](#measuring-text)). `MeasureText(text, size, font)` measures in a weight |
| `Draw.Mesh(vertices, indices)` | Triangles, with a color at each corner (see [Meshes](./meshes-and-textures.md#meshes)) |
| `Draw.Mesh(vertices, indices, texture)` | Triangles drawn with a texture, read as it says or with the filter and the wrap after it (see [Textures](./meshes-and-textures.md#textures)) |
| `Draw.Sprite(texture, center, size, color)` | A texture upright over a box, its top left at the box's top left, times `color` (white without one). `Sprite(texture, center, size, part, color)` draws the part of it a `Rect` of uvs covers, and a `rotation` before the color turns it (see [Sprites](./meshes-and-textures.md#sprites)) |
| `Draw.Blend(mode) { ... }` | How what's drawn in the block goes over what's there: added, multiplied, replaced (see [Blending](./draw-settings.md#blending)) |
| `Draw.Tint(color) { ... }` | Fades or colors what's drawn in the block (see [Tints](./draw-settings.md#tints)) |
| `Draw.Stencil(test, value, operation) { ... }` | What's drawn in the block is drawn only where a number kept for each pixel passes a test, and changes that number where it draws: masks, cut-outs, counting (see [Stencil](./draw-settings.md#stencil)) |
| `Draw.Transform(offset, rotation, scale) { ... }` | Moves, turns and scales what's drawn in the block (see [Transforms](./draw-settings.md#transforms)) |
| `Draw.Clip(rect) { ... }` | Only what's inside `rect` draws, in the block (see [Clipping](./draw-settings.md#clipping)) |
| `Draw.Screen() { ... }` | The calls in the block are in the screen's pixels, from the top left, with `y` down (see [On the screen](#on-the-screen)) |
| `Draw.SetCamera(position, rotation, fieldOfView)` | Sets the 3D camera for the 3D meshes after it (see [3D](./3d.md)) |
| `Draw.Depth(mode) { ... }` | What the 3D meshes drawn in the block do with depth: hide and are hidden, are only hidden, or neither (see [Depth](./3d.md#depth)) |
| `Draw.Cull(side) { ... }` | Which side of the triangles of the 3D meshes drawn in the block isn't drawn: the back, the front, or neither (see [Which side draws](./3d.md#which-side-draws)) |
| `Draw.Mesh(vertices, indices, transform)` | A 3D mesh, placed in the world by `transform`, with a texture, a filter and a wrap as a mesh's (see [3D](./3d.md)) |
| `Draw.Cube(center, size, color)` | A box with full widths `size.x`, `size.y` and `size.z` |
| `Draw.Sphere(center, radius, color)` | A sphere |
| `Draw.Cylinder(center, radius, height, color)` | A capped cylinder along its local `y` axis, with full `height` |
| `Draw.Plane(center, size, color)` | A rectangle in the local `xz` plane, with full widths `size.x` and `size.y` |
| `Draw.Instanced(vertices, indices, instances)` | One mesh for each instance in a list, 2D or 3D by its vertices, with optional textures and shaders (see [Instances from a list](./shaders.md#instances-from-a-list)) |

Each frame starts black, with the camera at the origin and one world unit per pixel.

## Colors

`Color` has `r`, `g`, `b` and `a`, floats from 0 to 1, as in Unity. Its constructor takes scalars and vectors totaling 3 or 4 channels, in order, just as vector constructors do. `Color(r, g, b)`, `Color(uv.xy, 0)` and `Color(rgb)` have alpha 1; `Color(r, g, b, a)`, `Color(rgb, a)` and `Color(rgba)` set it. Ints and int vectors convert to floats. The constants are `Color.white`, `black`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray` and `clear`. Components and singletons can hold colors.

Colors add, subtract and multiply channel by channel, alpha too, and scale by a number: `tint * Color.red * 0.5`. `Math`'s component-by-component functions take them the same way: `Math.Lerp(a, b, t)` blends two colors, in a view or in a frag.

A color's numbers are the display's own: `Color(0.5, 0.5, 0.5)` is the middle gray a paint program shows for 128, a PNG's pixels are drawn as the file has them, and colors blend and add as those numbers. Nothing is converted on the way in or out, and nothing is sRGB-aware. It's what sprites and UI want. It isn't how light adds up: two lamps of half don't make white's brightness, and a half-clear white over black is darker than half the light. A game that wants that works in light itself, in a float target:

```csharp
// Colors and textures to light as they're read, in the frags that draw the scene
Color paint = models.wood.Sample(vertex.uv);
float3 light = float3(Math.Pow(paint.r, 2.2), Math.Pow(paint.g, 2.2), Math.Pow(paint.b, 2.2));

// The last pass, from the float target to the window: under 1 first, then back to the display's numbers
frag Present(Vertex vertex, out Color color, Texture source)
{
    Color lit = source.Sample(vertex.uv);
    color = Color(Math.Pow(lit.r / (1 + lit.r), 1 / 2.2), Math.Pow(lit.g / (1 + lit.g), 1 / 2.2), Math.Pow(lit.b / (1 + lit.b), 1 / 2.2));
}
```

The scene is drawn into a [`Format.Rgba16f`](./render-targets.md#what-a-target-holds) target, where light adds up past 1, and `Draw.Blit(scene.color, Draw.window, Present)` shows it. Without the last step the picture is too dark between its lights and flat white where they meet. What's drawn after it, the GUI and text among it, is in the display's numbers as ever.

`Color.none` is no color, for where one can be left out, like a [`Shape`](#the-general-shape)'s second color. It isn't one to draw with: nothing shows.

## Smooth shapes

`Rect`, `WireRect` and the `Line` without a width fill whole pixels: rects side by side leave no seam between them, and a line is a pixel wide wherever it lands. A circle's edge is smooth, and so are the edges of the shapes that follow, at any size and turned any way:

```csharp
view Bar(Health health)
{
    Draw.SetScreen();
    var middle = float2(120, 40);
    Draw.RoundRect(middle, float2(200, 24), 12, Color(0, 0, 0, 0.6));                 // A pill
    Draw.RoundRect(float2(22 + health.left * 98, 40), float2(health.left * 196, 20), 10, Color.red);
    Draw.WireRoundRect(middle, float2(200, 24), 12, 2, Color.white);                 // Its outline, 2 pixels thick
    Draw.Line(float2(20, 64), float2(220, 64), 3, Color.gray);
    Draw.Arc(float2(250, 40), 14, 0, Math.TAU * health.cooldown, 4, Color.yellow);   // A ring that fills up
}
```

- Lengths are in the units the calls are in, like positions: world units under the camera, pixels [on the screen](#on-the-screen). That goes for sizes, radii and widths alike.
- An outline is `width` thick and inside the shape's edge, so a wire shape is as big as the filled one.
- A `radius` rounds a rectangle's corners, up to half its smaller side: that much makes a pill, or a circle of a square.
- An ellipse takes its `radii`, half its width and half its height, as a circle takes its radius.
- A thick line reaches half its width past both ends, which are round, so lines that meet at a point join without a gap.
- Angles are in radians, from `+x` towards `+y`, as `Math.Atan2(y, x)` gives them: counterclockwise under the camera, where `y` goes up, and clockwise on the screen, where it goes down. A `rotation` turns a shape about its center.
- `Arc` draws the part of a circle's outline from the angle `start` on for `sweep`, the other way round for a sweep under 0, and its ends are cut straight across. A `width` of the whole radius makes it a slice of a disc, and a sweep of a whole turn or more is the whole ring.
- A size, radius or width that isn't above 0 draws nothing.

The engine's own shaders draw these, so they take no `frag` or `vert`. For anything they don't draw, there's a [mesh](./meshes-and-textures.md#meshes) and a [frag](./shaders.md) of your own.

Move the slider to fill the bar and ring and rotate the ellipse. The card uses `Shape` for its gradient, outline and soft shadow.

::: tide-example shapes Smooth shapes

<<< @/examples/shapes.tide

:::

Shapes of every kind drawn one after another go to the GPU together, in one draw call, in the order they're written, so thousands a frame are cheap. Text or a mesh between them starts another.

### The general shape

Those calls are short for `Draw.Shape`, which takes everything a shape can have:

For a rectangle already represented by a `Rect`, use `Draw.Rect(rect, color)`,
`Draw.Rect(rect, radius, color)` or `Draw.Rect(rect, radii, color)`. All three
have antialiased edges, including square corners. A `float4` gives the top-left,
top-right, bottom-right and bottom-left radii; each is clamped to half the
smaller side. Equal radii use the ordinary rounded-box path. These filled boxes
need no GUI theme and work inside `GUI.Draw` as well as ordinary views.

The center/size `Draw.Rect` overload keeps its whole-pixel behavior for seamless
adjacent tiles. `Shape` continues to provide gradients, outlines and soft edges
with a uniform corner radius.

```csharp
struct Shape // Built in
{
    float2 center;
    float2 size;                       // Its width and height
    float rotation;
    float radius;                      // Its corners'
    bool ellipse;                      // The ellipse that fits `size`, rather than a box
    Color color = Color.white;         // Its fill...
    Color color2 = Color.none;         // ...and its fill at its bottom, when it's another
    float outline;                     // How wide its outline is, inside its edge
    Color outlineColor = Color.white;
    float soft;                        // How far its edge fades
}
```

```csharp
view Card(Deal deal)
{
    Draw.SetScreen();
    // A shadow first: a dark shape whose edge fades outwards
    Draw.Shape(Shape { center = deal.at + float2(0, 6), size = float2(180, 240), radius = 16,
                         color = Color(0, 0, 0, 0.5), soft = 18 });
    // The card: lighter at its top than at its bottom, with an outline
    Draw.Shape(Shape { center = deal.at, size = float2(180, 240), radius = 16,
                         color = Color(0.16, 0.2, 0.34), color2 = Color(0.07, 0.09, 0.17),
                         outline = 2, outlineColor = Color(0.5, 0.62, 0.8) });
}
```

- **`color2`** makes the fill go from `color` at the shape's top to `color2` at its bottom. Top and bottom are the shape's own, as it is before `rotation` turns it, and the top is the upper one on the screen whether `y` goes up or down. Left at `Color.none`, the fill is `color` all over. A fill that goes to a clear color fades out without getting darker on the way.
- **`outline`** is an outline that wide in `outlineColor`, inside the edge, over the fill.
- **`soft`** above 0 fades the edge outwards: the shape's color goes on past its edge and is gone `soft` further out, which is a glow in a light color and a shadow in a dark one. Under 0, it fades inwards from the edge instead and leaves the middle clear, like a vignette over the whole screen. At 0 the edge is sharp, smooth over one pixel.
- **`ellipse`** makes it the ellipse that fits `size`, whatever `radius` says.

`Arc` is the one call `Shape` doesn't cover.

### Fills

A fill colors a shape by a function of where in it each pixel is: `Draw.Rect(rect, radius, fill)`, `Draw.Rect(rect, radii, fill)` with four corners, and `Draw.Shape(shape, fill)`, where `fill` is a `Func<float2, Color>`. It runs on the GPU for each pixel, given the pixel's place in the shape from `(0, 0)` at its top left to `(1, 1)` at its bottom right. `Gradient`'s functions make the usual ones:

```csharp
view Buttons()
{
    Draw.SetScreen();
    // Lighter at the top: a button that looks raised
    Draw.Rect(Rect(20, 20, 200, 48), 12, Gradient.Vertical(Color(0.35, 0.5, 0.9), Color(0.15, 0.25, 0.6)));
    // Three colors, left to right
    Draw.Rect(Rect(20, 80, 200, 48), 24, Gradient.Horizontal(Color.red, Color.yellow, Color.green));
    // A glow: bright in the middle, clear at the edge
    Draw.Rect(Rect(240, 20, 108, 108), 54, Gradient.Radial(Color(1, 0.9, 0.5), Color.clear));
    // Any function of the place: stripes
    Draw.Rect(Rect(20, 140, 200, 48), 8, (float2 uv) => int(uv.x * 8) % 2 == 0 ? Color.white : Color.gray);
}
```

| Function | From, to |
|---|---|
| `Gradient.Vertical(top, bottom)` | The shape's top to its bottom |
| `Gradient.Horizontal(left, right)` | Its left to its right |
| `Gradient.Linear(angle, from, to)` | Across it at `angle`, in radians from `+x` towards `+y`, one corner to the opposite one |
| `Gradient.Radial(inner, outer)` | Its middle out to the middle of its sides, and on to its corners |

Each takes two, three or four colors, spread evenly: `Gradient.Vertical(a, b, c)` is `b` at the middle. Colors blend with their alpha, so a gradient to `Color.clear` fades without darkening.

- **A fill is a value.** Keep one in a field, pass it to a helper, or write your own: a function `Color Name(float2 uv)` named where a fill goes, or a lambda, which can use the variables around it: `Draw.Rect(rect, 8, (float2 uv) => tint * (1 - uv.y * 0.3))`. Calling one on the CPU gives the color a pixel there gets.
- **The rest of the shape is as ever.** `Draw.Shape(shape, fill)` takes its center, size, rotation, corners, outline, soft edge and ellipse from `shape`, and the fill replaces `color` and `color2`. Its place in the shape is the shape's own, before `rotation` turns it, and its top is the upper one on the screen whether `y` goes up or down.
- **Fills drawn one after another are drawn together,** whatever their functions: a menu of gradient buttons costs one draw. The function is GPU code, with [what the GPU runs](./shaders.md#what-the-gpu-runs)' rules.
- **They're local code.** A fill is drawn by views and the functions they call, in the window or inside [`GUI.Draw`](./gui.md#custom-controls-in-layout). A render target's own `Rect` and `Shape` don't take one yet.

## On the screen

In `Draw.Screen() { ... }`, Draw calls are in the screen's pixels: from the top left, with `y` down, as the GUI's are, `Screen.width` by `Screen.height`. After the block, the camera is what it was, so a view of the world can draw a label in pixels and leave the world's camera alone for the views after it:

```csharp
view Nameplates(Player player, Body body)
{
    var above = Draw.WorldToScreen(body.position + float2(0, 1.5));
    Draw.Screen()
    {
        var size = Draw.MeasureText(player.name, 14);
        Draw.Text(player.name, above - float2(size.x * 0.5, size.y), 14, Color.white);
    }
}
```

A view that only draws on the screen can set it for the views that follow instead, with `Draw.SetScreen()`:

```csharp
view Health(Stats stats)
{
    Draw.SetScreen();
    Draw.Rect(float2(110, 30), float2(200, 20), Color.gray);
    Draw.Rect(float2(10 + stats.health, 30), float2(stats.health * 2, 20), Color.red);
}
```

`Draw.SetCamera` goes back to the world. What views draw on the screen is under the GUI's widgets, which draw last.

The mouse, touches and the pointer count pixels from the bottom left instead, with `y` up. `Screen.Flip(devices.pointer.position)` is where the pointer is in the screen's pixels: see [Screen and world coordinates](./cameras.md#screen-and-world-coordinates).

## Text

`Draw.Text(text, position, size, color)` draws text in the engine's font. Its `size` is its font size, as CSS's and Unity's: the height of a line of it, with its capitals in the line's middle. A newline in the text starts another line under it. Text is fitted to the display's pixels, so it's sharp at the sizes it's read at.

### Anchors

Without more, `position` is the text's top left corner. An `Anchor` after the color says which place of the text's box it is instead, the nine of Unity's `TextAnchor`: `UpperLeft`, `UpperCenter`, `UpperRight`, `MiddleLeft`, `MiddleCenter`, `MiddleRight`, `LowerLeft`, `LowerCenter` and `LowerRight`.

```csharp
view Banner(Round round)
{
    Draw.SetScreen();
    var middle = float2(Screen.width, Screen.height) * 0.5;
    Draw.Text("FIGHT!", middle, 64, Color.white, Anchor.MiddleCenter);
    Draw.Text($"Round {round.number}", float2(Screen.width - 16, 16), 24, Color.gray, Anchor.UpperRight);
}
```

That's a word in the middle of the screen, and the round's number against its top right corner, wherever the window's edges are and however wide the text comes out.

The box is as wide as the text's widest line and as tall as its lines. Upper is towards the top of the screen, whether the calls are in the world, where `y` goes up, or in the screen's pixels, where it goes down: a name over a body is `Draw.Text(player.name, body.position + float2(0, 1), 0.5, Color.white, Anchor.LowerCenter)` under any camera. Text of several lines lines up on the anchor's side: its lines start together at a left anchor, end together at a right one, and are centered on one between.

### Bold

The font has two weights. `Font.Bold`, last in the call, draws text in the font's own bold:

```csharp
Draw.Text("DUEL", at, 88, Color.white, Font.Bold);
Draw.Text("DUEL", at, 88, Color.white, Anchor.MiddleCenter, Font.Bold);
```

`Font.Regular` is the weight text has without one. A `Font` is a value like any enum's, so a function can take one and pass it on.

Bold is a second font file, and a game only carries it when its code says `Font.Bold`: about 95 KB more in the program, and nothing for a game that never does. Its strokes are a whole number of pixels thick, as the regular weight's are, so text stays sharp. At the smallest sizes, under 13 of the display's pixels, that's one pixel in both weights: bold is for text of about that size and up, and for titles. A letter the bold weight doesn't have (it covers Latin, Greek and Cyrillic, less widely than the regular one) is drawn in the regular weight.

The GUI's widgets are in the regular weight.

### Measuring text

To fit a panel around a text, or to put something beside it, ask how big it will be: `Draw.MeasureText(text, size)` returns a `float2`, the width of the text's widest line in `x` and the height of its lines in `y`. One line is `size` high, and each newline adds a line. It's the box an anchor names a place of.

```csharp
view Banner(Round round)
{
    Draw.SetScreen();
    var middle = float2(Screen.width, Screen.height) * 0.5;
    var size = Draw.MeasureText(round.message, 48, Font.Bold);
    Draw.Rect(middle, size + float2(32, 16), Color.black);
    Draw.Text(round.message, middle, 48, Color.white, Anchor.MiddleCenter, Font.Bold);
}
```

That's the message in the middle of the screen, on a panel that fits it. Bold text is wider than regular, so it's measured in its weight: `Draw.MeasureText(text, size, Font.Bold)`.

Below, the cross is the position the text is given, and the frame is the box `MeasureText` says it has. Move the anchor's slider to put another place of the box on the cross, and change the size and the weight to see the box follow.

::: tide-example text Anchors, sizes and weights

<<< @/examples/text.tide

:::

The answer is in the units the Draw calls are in: pixels on the screen, and the world's under a camera.

It's what `Draw.Text` would draw at that point of the view, to the pixel. Text is fitted to the display's pixels to stay sharp, so the same text can come out a little wider or narrower on another display, under another camera or in a window of another size: measure where you draw, each frame, rather than keeping the answer. A [render target](./render-targets.md) has its own camera, and pixels of its own to fit text to, so `target.MeasureText(text, size)` measures for it.

## Smooth at any tick rate

Views draw at whatever frame rate the game renders, not the tick rate, and see the match blended between its last two ticks. So motion is smooth even at 20 ticks per second, at the cost of drawing up to a tick late.

Every float, vector, quaternion and color a view reads of the match is blended, `Time.t` among them. Ints, bools, enums, entities and text come from the upper endpoint. An entity that wasn't there at the previous endpoint is drawn as it is. What a view reads [`verified`](./views-and-the-match.md#confirmed-state) blends on a separate confirmed clock, which holds at the latest confirmation and never extrapolates.

The match below ticks only five times a second. Both discs are the same position: the amber one is an ordinary field, which the view sees blended between ticks, and the teal one a `[Snap]` field, drawn as the last tick left it.

::: tide-example tick-rate Five ticks a second, drawn every frame

<<< @/examples/tick-rate.tide

:::

`[Snap]` keeps a field out of blending, for angles that wrap and values that jump:

```csharp
component Body
{
    float2 position;        // Blended
    [Snap] float heading;   // Wraps from 360 to 0: as it is
    int lives;              // Ints are as they are
}
```

### Jumps

Something that jumps, like a respawn, a portal or a camera cut, says so from match code: `entity.Snap()` or `singleton.Snap()`. For the tick it happens in, views draw it as it is, instead of sliding from where it was.

```csharp
event(Died dead) Respawn(mut Body body, Arena arena)
{
    body.position = arena.start;
    this.Snap(); // No sliding from where it died
}
```

`entity.Snap()` is applied at the end of the tick, like `Destroy`, so it never makes systems wait. `singleton.Snap()` needs the singleton as a `mut` parameter.

A single field can jump too: `field.Snap()` is the per-field counterpart of `entity.Snap()`. For the tick it happens in, views draw that field as it is at the newer tick, and from the next tick it blends again. The rest of its component or singleton keeps blending, so a position doesn't stutter.

Use it whenever a float starts over together with something that doesn't blend. Here a stance is an enum, so views see the new stance at once, and `since` counts the seconds in it:

```csharp
component Fighter
{
    Stance stance;
    float since;    // Seconds in this stance
    float3 hand;

    mut void Enter(Stance next)
    {
        stance = next;
        since = 0;
        since.Snap(); // Drawn from 0 with the new stance
    }
}
```

::: warning A timer that starts over
Without `since.Snap()`, every frame between the two ticks shows the new stance with `since` blended between its old value and 0: a timer running backwards, which pops whatever the view works out from it. `[Snap]` on the field would avoid it, but then `since` never blends, and `this.Snap()` stops the whole entity blending, position included.
:::

`field.Snap()` goes on a field of a match component or singleton that views blend, wherever code may write that field: systems, match event handlers, and the functions and methods they pass it to. A field that holds a struct snaps all of it, as in `arm.pose.Snap()`; a struct's own field can't snap alone. Each field that snaps keeps a count of its snaps beside it in the match, so snapshots, rollback and host migration carry it, and it is part of the match hash.

### Provisional correction smoothing

::: warning Provisional
`[Position]` and `[Rotation]` are available, with provisional names and behavior. They may change as games test them.
:::

Tick interpolation makes motion smooth between ticks. A prediction correction can still move the next frame abruptly. These attributes tell Tide how to preserve the displayed value and remove the correction error over time:

```csharp
component Body
{
    [Position] float2 position;
    [Rotation] float heading; // Radians, for 2D
    float2 velocity;          // Ordinary tick interpolation
}
```

`[Position]` accepts `float`, `double` and their vector types. It keeps linear tick interpolation and accumulates an additive presentation offset in the field's own precision. Double positions and correction errors remain double, preserving small offsets at large world coordinates. `[Rotation]` on a float uses the shortest wrapped angular difference, in radians. On a quaternion it uses the shortest relative rotation, for both tick interpolation and correction smoothing.

For a game with its own large-world transform, the same attribute works on double coordinates:

```csharp
component WorldPosition
{
    [Position] double3 position;
}
```

This smooths the value views read. Convert to rendering coordinates after subtracting the camera position in double precision; the drawing APIs still take floats.

The error decays exponentially with frame time toward zero, or the identity rotation, with a provisional half-life of 75 ms. Another correction accumulates with whatever error remains. Ordinary movement follows the corrected simulation, so moving and aiming stay responsive. Untagged fields keep ordinary tick interpolation.

The built-in `Transform` already uses this behavior for its match fields: `localPosition` is a `[Position]` and `localRotation` a `[Rotation]`. Its scale uses ordinary tick interpolation. These defaults apply to view bindings; `World.LocalToWorld` and other stored-state queries retain their existing behavior. Local-entity Transforms read their current values.

This state belongs to this machine's presentation. Simulation values and collisions correct immediately, and snapshots, hashes and network state are unaffected. `entity.Snap()` and `singleton.Snap()` also clear any accumulated error, keeping intentional jumps immediate. A new match, a hot reload or a replacement world with no comparable presentation history clears the error too. While a host migration reconnects, the frozen presentation retains its error; adopting the new network timeline clears it.

The tags take no arguments. Use them on match fields; local state, inputs and events cannot carry them. A field cannot combine `[Position]`, `[Rotation]` and `[Snap]`, and such a field can't snap alone with `field.Snap()`: snap its entity or singleton, which clears its error too.

Ordinary structs can contain annotated built-in fields, including through nested structs. Correction smoothing for a type with its own `Interpolate` is deferred; its existing tick interpolation remains available. No new custom-type method or correction-rate setting is agreed. The [specification](../spec.md#provisional-correction-smoothing) records the provisional contract.

### Blending your own way

A struct or component can say how it blends with an `Interpolate` method, declared in it like an operator. It replaces the default blend for that type wherever views see it:

```csharp
struct Angle
{
    float degrees;

    Angle Interpolate(Angle from, Angle to, float t)
    {
        mut var d = to.degrees - from.degrees;
        if (d > 180) d -= 360;
        if (d < -180) d += 360;
        return Angle { degrees = from.degrees + d * t };
    }
}

component Body
{
    float2 position; // Blended as usual
    Angle heading;   // Blended the short way round
}
```

`Interpolate` isn't for code to call. Custom interpolation is declared on structs or components; a singleton blends its own way through a struct in it that has one.

## A frame's length

Frames come at the display's rate: 60 a second on one screen, 144 on another, fewer when the machine is busy. What a view animates of its own, like particles, a menu that slides in or a camera's shake, moves by time rather than by frames. `Frame.dt` is the seconds since the previous frame started:

```csharp
struct Spark
{
    float2 position;
    float2 velocity;
    float age;
    float life = 0.4;
}

local singleton Sparks
{
    List<Spark> list;
}

view DrawSparks(mut Sparks sparks)
{
    mut var i = 0;
    while (i < sparks.list.count)
    {
        mut var spark = sparks.list[i];
        spark.age += Frame.dt;
        if (spark.age >= spark.life)
        {
            sparks.list.RemoveAt(i);
            continue;
        }
        spark.velocity.y -= 9.8 * Frame.dt;
        spark.position += spark.velocity * Frame.dt;
        sparks.list[i] = spark;
        Draw.Circle(spark.position, 0.1 * (1 - spark.age / spark.life), Color.yellow);
        i++;
    }
}
```

- **It's this machine's, not the match's.** It's there with no match, in a menu and while connecting, where a view that takes `Time` doesn't run. In a match it's the same value, whatever the tick rate.
- **The match has `Time`.** What's in the match moves by `time.dt`, a tick's length, in systems. A view draws the match as it is, already [blended](#smooth-at-any-tick-rate) to the frame, and uses `Frame.dt` for what it adds on top. A system that reads `Frame.dt` is an error that says so.
- **It's the frame's real length.** A frame after the window was dragged, or the page was hidden, is as long as that took. Clamp it where a big step would show: `var dt = Math.Min(Frame.dt, 0.05);`.
- **There's no clock beside it.** No `Frame.time`: code that asks `Frame` for one is an error that says what to write. A float of seconds gets coarser as it grows: after four and a half hours it counts in steps of 2 milliseconds, and whatever it animates starts to judder. Keep a clock of your own instead, in [local state](./local-state.md): add `Frame.dt` up into a field, and wrap it where what it drives repeats:

```csharp
local singleton Spinner
{
    float turn; // Radians, always under a full turn
}

view Spin(mut Spinner spinner)
{
    spinner.turn += Frame.dt * 4;
    if (spinner.turn > Math.TAU) spinner.turn -= Math.TAU;
    Draw.Circle(float2(Math.Cos(spinner.turn), Math.Sin(spinner.turn)), 0.1, Color.white);
}
```

What moves with the match, the same for every player, reads the match's time instead: a view takes `Time time` and reads `time.t`, [the seconds since the match started](./entities.md#singletons), which it sees move every frame.

Views read `Frame.dt`, and so does the rest of [local code](./local-state.md#time): the functions they call, local handlers and local tasks.

## Reading this machine's devices

Views take `Devices devices` to read this machine's input devices for the frame: `.down` and `.up` since the last frame, and the `delta` of the mouse, touches and the pointer, and the mouse's `scroll`, too. A view that wants the mouse for itself [locks the cursor](./cameras.md#looking-around). What the GUI is using is hidden from them, and so is what another view [claimed](./gui.md#widgets-of-your-own) for widgets of its own. They read [what's typed](./input.md#typed-text) too, as `devices.keyboard.text`. Helpers take the devices or a part of them as an explicit parameter. A helper that reads `keyboard.text` needs the frame, so only views and the functions they call can call it.
