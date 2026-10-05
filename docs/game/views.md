# Views and drawing

A **view** draws the game. It looks like a system and takes the same parameters, components and singletons, but it runs once per rendered frame instead of once per tick, and it never changes the match. It reads this machine's devices through a `Devices devices` parameter, and can't read the input (see [Input](./input.md#devices-in-the-match)).

```csharp
view DrawBalls(Body body, Ball ball)
{
    Draw2D.Circle(body.position, body.radius, ball.color);
}

view DrawHud(Arena arena)
{
    Draw2D.Camera(float2(0, 0), arena.halfSize.y);
    Draw2D.Text("fire: space", float2(-arena.halfSize.x, arena.halfSize.y), 18, Color.gray);
}
```

Drawing is immediate mode: views record their drawing calls every frame. Views run after the frame's ticks, in the order they're declared, and later ones draw on top. `[Before]` and `[After]` order views among themselves, as they do systems. A frame can draw as much as it needs: shapes go to the GPU together, so tens of thousands of rects or circles a frame are cheap, on the web too.

Views can share [named phases](./systems.md#named-phases) instead of depending on individual view names:

```csharp
enum RenderStages { Prepare, Objects, Effects, UI }

[Stage(RenderStages.Prepare)]
view ClearScene() { Draw2D.Clear(Color.black); }

[Stage(RenderStages.Objects)]
view DrawBalls(Body body, Ball ball)
{
    Draw2D.Circle(body.position, body.radius, ball.color);
}

[After(RenderStages.Objects), Before(RenderStages.UI)]
view Effects() { ... }
```

An offset such as `[Stage(RenderStages.Objects, 10)]` runs later within `Objects`. `After(RenderStages.Objects)` waits until every view in that phase has run for all its entities, including views with offsets.

## Targets and post-processing

`RenderTarget` names a local drawing destination. Call the drawing methods on it explicitly; ordinary `Draw2D`/`Draw3D` calls draw into `Draw.window`. Camera and clip belong to each destination and are shared across views within the frame. Switching destinations preserves color and depth, and a clear resets both, within that destination's clip.

Create a target with `RenderTarget(width, height)`, using positive pixel dimensions. Its color and depth persist across frames until cleared. Copies share the image, including copies of `.color`. `Resize(width, height)` changes it for every alias and discards its contents, even at the same size. Call `Dispose()` when finished; every alias becomes invalid, and disposing again is harmless. A default target is invalid. Creation and lifetime methods run in views or local handlers and their helpers; field defaults cannot allocate targets.

```csharp
local singleton RenderState { RenderTarget scene; bool initialized; }
enum RenderStages { Prepare, Objects, Effects }

frag Blur(Vertex vertex, mut Color color, Texture source)
{
    color = source.Sample(vertex.uv);
}

[Stage(RenderStages.Prepare)]
view Prepare(mut RenderState render)
{
    if (!render.initialized)
    {
        render.scene = RenderTarget(1280, 720);
        render.initialized = true;
    }
    render.scene.Clear(Color.black);
    render.scene.Camera(float2(0), 10);
}

[Stage(RenderStages.Objects)]
view Objects(Body body, RenderState render)
{
    render.scene.Circle(body.position, body.radius, Color.white);
}

[Stage(RenderStages.Effects)]
view Present(RenderState render)
{
    Draw.Blit(render.scene.color, Draw.window, Blur);
}
```

`Draw.Blit(source, destination, frag)` runs the frag on a fixed `Vertex` quad over the entire destination. It supplies the frag's read-only `Texture` parameter from `source`, replaces color and alpha, and preserves depth. It temporarily ignores camera and clip, then restores them. Its source can also be a `Grid2<Color>`, uploaded from CPU cells. Sampling the image being written is an error; use a second target between effects.

Target color is GPU data: `.color` gives a `Texture` reference and does no readback. `Texture` has no constructor; CPU pixel sources remain `Grid2<Color>`, and C can supply a CPU texture reference. `Draw.window` has no sampleable color and cannot be resized or disposed by the game. Texture references and targets can be stored only in local state. The GUI and stats draw onto the window. Hot reload preserves managed references, dimensions and shared aliases, recreating their images on the web; it discards rendered contents and clears borrowed references supplied by the old build. Device loss also discards rendered contents. Drawing with an invalid or disposed target is skipped with a diagnostic; disposal also invalidates uses already recorded in the frame.

A view's `mut` parameters, `Spawn`, `Add`, `Remove`, `Destroy` and `Send` are all local: views can change this machine's own state, like particles or a menu, but not the match (see [Local state](./local-state.md)). Only views, and the functions they call, can draw.

A view takes a `World` as a system does, to read another entity's component by its handle: `world.Get<Health>(follower.leader)` (see [Other entities](./systems.md#other-entities-the-world)). It reads the match through it and never changes it, and what it gets is the component as of the latest tick, not blended between ticks.

## Your own entities

"My camera, my HUD, my health bar" is in every multiplayer game, so views have a filter for it. `with ownership` picks the entities this machine's player owns, and `without ownership` every other one:

```csharp
view RenderGame(Player player, with ownership)
{
    Draw3D.Camera(player.pos, player.rot, 75);
}

// Everyone else's body: a first-person camera doesn't draw its own
view Bodies(Player player, Scenery scenery, without ownership)
{
    Draw3D.Mesh(scenery.crate, scenery.faces, float4x4.TRS(player.pos, player.rot, float3(1)), scenery.wood);
}
```

- `with ownership`: the entity has an [`Owner`](./input.md#who-gets-which-input), and its `player` is `Session.player`, this machine's player.
- `without ownership`: every other entity the view's parameters admit, which is another player's and those with no `Owner`. With `with Owner` beside it, only those whose owner is someone else.

They're filters, beside `with` and `without`: the view takes neither `Owner` nor `Session` for them, and `if (owner.player != session.player) return;` goes. A view that wants both kinds at once, like one that draws your player yellow and the others gray, still takes `Owner` and `Session` and compares.

Only views have them. The match runs the same on every machine, so a system can't ask whose machine it's on: there it's an error, and `with Owner` picks the entities that any player owns. A local handler doesn't filter by them either: one of a match event takes `Owner owner` and `Session session`, and compares (see [Events](./events.md#what-isn-t-there-yet)). `ownership` is a keyword only right after `with` and `without`, so it still names anything else.

## Confirmed state

In a match with other machines, a client runs ahead of the server: its views draw the world as this machine predicts it, and a prediction can be wrong for a moment, until the server says how the tick went (see [Networking](../multiplayer/networking.md#prediction-and-rollback)). That suits what has to answer at once, like movement. What must not be wrong, like a health bar, a score or a death, is read `verified`: from the world the server has confirmed.

`verified` goes before a parameter's type, like `mut`:

```csharp
// Movement as predicted, the health bar only what's confirmed.
view DrawFighter(Body body, verified Health health)
{
    Draw2D.Circle(body.position, 0.5, Color.white);
    Draw2D.Rect(body.position + float2(0, 0.8), float2(health.value * 0.01, 0.1), Color.red);
}

// The same component twice: what's predicted, and what's confirmed so far.
view HealthBar(Body body, Health health, verified Health confirmed)
{
    var at = body.position + float2(0, 1);
    Draw2D.Rect(at, float2(confirmed.value * 0.01, 0.1), Color.gray);
    Draw2D.Rect(at, float2(health.value * 0.01, 0.1), Color.red);
}

// A filter, and a singleton.
view Corpse(Body body, with verified Dead)
{
    Draw2D.Circle(body.position, 0.5, Color.gray);
}

view Board(verified Duel duel)
{
    Draw2D.Screen();
    Draw2D.Text($"Round {duel.round}", float2(20, 20), 24, Color.white);
}
```

- **A view runs for an entity when each parameter exists in the world it names.** A view with components of both worlds needs the entity in both, so it skips one whose spawn isn't confirmed yet. A game that wants it drawn sooner splits the view in two: one that draws what's predicted, and one that adds what's confirmed.
- **A view whose components are all `verified`** runs for the confirmed world's entities, and draws them as they are there: `this` is the confirmed entity, and corrections aren't smoothed.
- **Filters take it**: `with verified Dead`, `without verified Dead`. Either asks for the entity in the confirmed world: `without verified Dead` is "confirmed, and not `Dead` there", so it skips an entity whose spawn isn't confirmed yet too.
- **Singletons take it**: `verified Duel duel`.
- **A `verified` value isn't blended.** It's as the last confirmed tick has it, where the rest of what a view reads is [blended](#smooth-at-any-tick-rate) between two ticks.
- **`mut` goes with it.** `mut verified Health health` reads the confirmed `Health` and writes the entity's [local fields](./local-state.md#local-fields), which are the entity's whichever world a parameter reads. A view that takes a component both ways reaches its local fields through the one that isn't `verified`.
- **The same entity** in both worlds is the entity, not only its handle: running a tick again can give a handle to another entity, which is then in one world only (see [After a rollback](./local-state.md#after-a-rollback)).
- **It's the view's to say.** A function's parameter can't say `verified`: the view takes the value `verified` and passes it on. Nor can a [frag or vert](./shaders.md), which reads what its view is given.
- **The match has no "confirmed".** It's the same on every machine, so `verified` in a system, a match handler or a task is an error, which points to views and local handlers.

Outside a match, a view with a `verified` parameter doesn't run, like any view that reads the match. A local handler takes `verified` on its event, to hear it once it's certain: see [Events](./events.md#verified-handlers).

### Holding a death until it's confirmed

With a predicted `Body`, an entity is gone from a view the moment the prediction destroys it, whatever a `verified` parameter beside it says: the view needs the entity in both worlds. There are three ways to hold a death until the server has confirmed it:

- **Make the death a state of a living entity**, a field or a tag like `Dead`, and destroy the entity later. Views read the state `verified`, as `Corpse` does above.
- **Take everything `verified` in that view.** It then runs for the confirmed world's entities, so the entity is drawn, as the server last confirmed it, until the server says it's gone.
- **Show it from a local handler**: `local event(verified Destroyed) ...` hears the entity go once it's certain, with its components as they were (see [Events](./events.md#verified-handlers)).

### What isn't there yet

- **Text, lists, maps, sets and grids of the confirmed world.** Naming one through a `verified` parameter is an error, and a copy of a component that has one has it empty. Read them through a parameter that isn't `verified`.
- **`verified World world`**, which is an error that points to `verified` component parameters. A view's `World` is the world it draws.
- **Blending confirmed state.** A `verified` value steps at the tick rate. Drawing other players from confirmed state, blended between confirmed ticks rather than predicted ahead, isn't there yet.

## Draw functions

`Draw2D` draws shapes, text and 2D meshes. `Draw3D` draws 3D meshes and primitives. Each has its own camera; changing one leaves the other as it is. These replace the former `Draw` API: use the namespace matching the draw's dimension. Both `Clear` methods clear the same target and its depth, and both namespaces' `list` members hand the same draw list to C.

Positions and sizes are in world units, with `y` up. The camera maps them to the screen.

| Function | What it draws |
|---|---|
| `Draw2D.Clear(color)` | Fills the screen |
| `Draw2D.Camera(center, size)` | Sets the camera for the calls after it: `center` is the world position at the middle of the screen, and `size` is half the visible height, like Unity's orthographic size |
| `Draw2D.Circle(center, radius, color)` | A filled circle |
| `Draw2D.WireCircle(center, radius, color)` | A circle's outline |
| `Draw2D.Rect(center, size, color)` | A filled rectangle |
| `Draw2D.WireRect(center, size, color)` | A rectangle's outline |
| `Draw2D.Line(from, to, color)` | A line a pixel wide |
| `Draw2D.Line(from, to, width, color)` | A line `width` thick, with round ends: the first of the [smooth shapes](#smooth-shapes), as are the rows from here to `Draw2D.Shape` |
| `Draw2D.WireRect(center, size, width, color)`, `Draw2D.WireCircle(center, radius, width, color)` | Outlines `width` thick |
| `Draw2D.Rect(center, size, rotation, color)` | A rectangle turned about its center |
| `Draw2D.RoundRect(center, size, radius, color)` | A rectangle with round corners. `RoundRect(center, size, radius, rotation, color)` turns it, and `WireRoundRect(center, size, radius, width, color)` is its outline |
| `Draw2D.Ellipse(center, radii, color)` | An ellipse. `Ellipse(center, radii, rotation, color)` turns it, and `WireEllipse(center, radii, width, color)` is its outline |
| `Draw2D.Arc(center, radius, start, sweep, width, color)` | Part of a ring |
| `Draw2D.Shape(shape)` | A [`Shape`](#the-general-shape): a fill from one color to another, an outline around a fill, an edge that fades |
| `Draw2D.Text(text, position, size, color)` | Text, with `position` its top left corner and `size` its font size, the height of a line of it (see [Text](#text)) |
| `Draw2D.Text(text, position, size, color, anchor)` | Text with `position` another place of its box: `Anchor.MiddleCenter` centers it there |
| `Draw2D.Text(text, position, size, color, font)` | Text in a weight: `Font.Bold`. An anchor goes before it: `Draw2D.Text(text, position, size, color, anchor, font)` |
| `Draw2D.MeasureText(text, size)` | Nothing: it returns how big `Draw2D.Text` would draw the text, a `float2` of its width and height (see [Measuring text](#measuring-text)). `MeasureText(text, size, font)` measures in a weight |
| `Draw2D.Mesh(vertices, indices)` | Triangles, with a color at each corner (see [Meshes](#meshes)) |
| `Draw2D.Mesh(vertices, indices, texture)` | Triangles drawn with a grid of colors (see [Textures](#textures)) |
| `Draw2D.Clip(rect)` | Only what's inside `rect` draws, for the calls after it; `Draw2D.Clip()` draws everywhere again (see [Clipping](#clipping)) |
| `Draw2D.Screen()` | The calls after it are in the screen's pixels, from the top left, with `y` down (see [On the screen](#on-the-screen)) |
| `Draw3D.Camera(position, rotation, fieldOfView)` | Sets the 3D camera for the 3D meshes after it (see [3D](#_3d)) |
| `Draw3D.Mesh(vertices, indices, transform)` | A 3D mesh, placed in the world by `transform`, with a texture and filter as a mesh's (see [3D](#_3d)) |
| `Draw3D.Cube(center, size, color)` | A box with full widths `size.x`, `size.y` and `size.z` |
| `Draw3D.Sphere(center, radius, color)` | A sphere |
| `Draw3D.Cylinder(center, radius, height, color)` | A capped cylinder along its local `y` axis, with full `height` |
| `Draw3D.Plane(center, size, color)` | A rectangle in the local `xz` plane, with full widths `size.x` and `size.y` |
| `Draw3D.Instanced(vertices, indices, instances)` / `Draw3D.Instanced(vertices, indices, instances)` | One mesh for each instance in a list, with optional textures and shaders (see [Instances from a list](./shaders.md#instances-from-a-list)) |

Each frame starts black, with the camera at the origin and one world unit per pixel.

## Colors

`Color` has `r`, `g`, `b` and `a`, floats from 0 to 1, as in Unity. `Color(r, g, b)` has alpha 1, and `Color(r, g, b, a)` sets it. The constants are `Color.white`, `black`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray` and `clear`. Components and singletons can hold colors.

Colors add, subtract and multiply channel by channel, alpha too, and scale by a number: `tint * Color.red * 0.5`.

`Color.none` is no color, for where one can be left out, like a [`Shape`](#the-general-shape)'s second color. It isn't one to draw with: nothing shows.

## Smooth shapes

`Rect`, `WireRect` and the `Line` without a width fill whole pixels: rects side by side leave no seam between them, and a line is a pixel wide wherever it lands. A circle's edge is smooth, and so are the edges of the shapes that follow, at any size and turned any way:

```csharp
view Bar(Health health)
{
    Draw2D.Screen();
    var middle = float2(120, 40);
    Draw2D.RoundRect(middle, float2(200, 24), 12, Color(0, 0, 0, 0.6));                 // A pill
    Draw2D.RoundRect(float2(22 + health.left * 98, 40), float2(health.left * 196, 20), 10, Color.red);
    Draw2D.WireRoundRect(middle, float2(200, 24), 12, 2, Color.white);                 // Its outline, 2 pixels thick
    Draw2D.Line(float2(20, 64), float2(220, 64), 3, Color.gray);
    Draw2D.Arc(float2(250, 40), 14, 0, Math.TAU * health.cooldown, 4, Color.yellow);   // A ring that fills up
}
```

- Lengths are in the units the calls are in, like positions: world units under the camera, pixels after `Draw2D.Screen()`. That goes for sizes, radii and widths alike.
- An outline is `width` thick and inside the shape's edge, so a wire shape is as big as the filled one.
- A `radius` rounds a rectangle's corners, up to half its smaller side: that much makes a pill, or a circle of a square.
- An ellipse takes its `radii`, half its width and half its height, as a circle takes its radius.
- A thick line reaches half its width past both ends, which are round, so lines that meet at a point join without a gap.
- Angles are in radians, from `+x` towards `+y`, as `Math.Atan2(y, x)` gives them: counterclockwise under the camera, where `y` goes up, and clockwise on the screen, where it goes down. A `rotation` turns a shape about its center.
- `Arc` draws the part of a circle's outline from the angle `start` on for `sweep`, the other way round for a sweep under 0, and its ends are cut straight across. A `width` of the whole radius makes it a slice of a disc, and a sweep of a whole turn or more is the whole ring.
- A size, radius or width that isn't above 0 draws nothing.

The engine's own shaders draw these, so they take no `frag` or `vert`. For anything they don't draw, there's a [mesh](#meshes) and a [frag](./shaders.md) of your own.

Shapes of every kind drawn one after another go to the GPU together, in one draw call, in the order they're written, so thousands a frame are cheap. Text or a mesh between them starts another.

### The general shape

Those calls are short for `Draw2D.Shape`, which takes everything a shape can have:

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
    Draw2D.Screen();
    // A shadow first: a dark shape whose edge fades outwards
    Draw2D.Shape(Shape { center = deal.at + float2(0, 6), size = float2(180, 240), radius = 16,
                         color = Color(0, 0, 0, 0.5), soft = 18 });
    // The card: lighter at its top than at its bottom, with an outline
    Draw2D.Shape(Shape { center = deal.at, size = float2(180, 240), radius = 16,
                         color = Color(0.16, 0.2, 0.34), color2 = Color(0.07, 0.09, 0.17),
                         outline = 2, outlineColor = Color(0.5, 0.62, 0.8) });
}
```

- **`color2`** makes the fill go from `color` at the shape's top to `color2` at its bottom. Top and bottom are the shape's own, as it is before `rotation` turns it, and the top is the upper one on the screen whether `y` goes up or down. Left at `Color.none`, the fill is `color` all over. A fill that goes to a clear color fades out without getting darker on the way.
- **`outline`** is an outline that wide in `outlineColor`, inside the edge, over the fill.
- **`soft`** above 0 fades the edge outwards: the shape's color goes on past its edge and is gone `soft` further out, which is a glow in a light color and a shadow in a dark one. Under 0, it fades inwards from the edge instead and leaves the middle clear, like a vignette over the whole screen. At 0 the edge is sharp, smooth over one pixel.
- **`ellipse`** makes it the ellipse that fits `size`, whatever `radius` says.

`Arc` is the one call `Shape` doesn't cover.

## Meshes

`Draw2D.Mesh(vertices, indices)` draws triangles, the shape every other one can be made of. `vertices` is a `List<Vertex>`, the corners, and `indices` a `List<int>`: every three are a triangle, each a corner's place in `vertices`, so triangles share corners.

```csharp
struct Vertex // Built in
{
    float2 position;
    float2 uv;                 // Where in the texture: (0, 0) to (1, 1)
    Color color = Color.white;
}
```

```csharp
view Arrows(Body body)
{
    List<Vertex> corners = [
        Vertex { position = body.position + float2(0, 1), color = Color.red },
        Vertex { position = body.position + float2(-1, -1), color = Color.blue },
        Vertex { position = body.position + float2(1, -1), color = Color.blue },
    ];
    Draw2D.Mesh(corners, [0, 1, 2]);
}
```

Each pixel of a triangle is its corners' colors, blended across it, over what's behind it by its alpha. Triangles draw whichever way round their corners go, in the order they're given, among everything else a view draws. An index past the end of `vertices` leaves its triangle out.

A mesh is one call however many triangles it has, and meshes drawn one after another go to the GPU together, so thousands of triangles a frame are cheap.

A mesh's corners can be a struct of your own instead, with whatever fields the mesh needs, and a `vert` and a `frag` can run for its vertices and its pixels on the GPU: see [Frags and verts](./shaders.md).

## Textures

An uploaded texture can come from a grid of colors with a size, `Grid2<Color>` (see [Grids](../language/grids.md)). `Draw2D.Mesh` also accepts a `Texture` reference, including a target's `.color`. With no frag, each pixel is the triangle's color times the image's at its `uv`: (0, 0) is the first pixel's outer corner, and (1, 1) the last pixel's outer corner.

```csharp
local singleton Art
{
    Grid2<Color> checker = Grid2(8, 8);
    bool painted;
}

view Board(mut Art art)
{
    if (!art.painted)
    {
        foreach (var at in art.checker)
        {
            art.checker[at] = (at.x + at.y) % 2 == 0 ? Color.white : Color.black;
        }
        art.painted = true;
    }
    List<Vertex> corners = [
        Vertex { position = float2(-4, -4), uv = float2(0, 0) },
        Vertex { position = float2(4, -4), uv = float2(1, 0) },
        Vertex { position = float2(4, 4), uv = float2(1, 1) },
        Vertex { position = float2(-4, 4), uv = float2(0, 1) },
    ];
    Draw2D.Mesh(corners, [0, 1, 2, 0, 2, 3], art.checker, Filter.Point);
}
```

There's nothing to create, upload or free. The grid is ordinary state: change its cells whenever, with `[x, y]`, a loop or `Clear()`, and the next frame draws what it holds. The engine keeps a copy on the GPU while views draw with it, and sends the cells again only when they changed. Nothing holds a handle, so hot reloading carries a texture as it carries any state.

The grid can be local state or the match's, like a canvas each player paints. Views read the match's grids as they are at the latest tick: cells aren't blended.

The last argument says how the texture is read between cells: `Filter.Bilinear` blends them, and is what a call without it does; `Filter.Point` takes the nearest, for pixel art. Past 0 and 1, `uv` reads the cells at the edge.

- A cell is four floats, and the GPU's copy a byte a channel: values are clamped to 0 to 1.
- A grid with an open axis has no size, so it's no texture: the mesh draws with its colors alone.
- How big a texture can be is the GPU's limit: at least 2048 by 2048 wherever Tide runs.

A game's C can draw meshes too, with pixels of its own, like a font atlas a library made: see [Drawing from C](../c/functions.md#drawing-from-c).

## Clipping

`Draw2D.Clip(rect)` keeps what's drawn after it inside a rectangle: meshes, shapes, text, and `Draw2D.Clear`, which then fills the rectangle. `Draw2D.Clip()` draws everywhere again.

```csharp
view Panel()
{
    Draw2D.Screen();
    Draw2D.Clip(Rect(20, 20, 200, 100));
    Draw2D.Clear(Color.black); // The panel only
    Draw2D.Text("A line too long for the panel is cut off at its edge", float2(28, 28), 18, Color.white);
    Draw2D.Clip();
}
```

The `Rect` goes from (x, y) to (x + width, y + height), in the units the calls are in: the world's under a camera, where `y` is up, and pixels after `Draw2D.Screen()`. It stays where it is on the screen when the camera changes afterwards. Each frame starts with none, and a view's clip never clips the GUI.

## On the screen

After `Draw2D.Screen()`, Draw calls are in the screen's pixels: from the top left, with `y` down, as the GUI's are, `Screen.width` by `Screen.height`. `Draw2D.Camera` goes back to the world.

```csharp
view Health(Stats stats)
{
    Draw2D.Screen();
    Draw2D.Rect(float2(110, 30), float2(200, 20), Color.gray);
    Draw2D.Rect(float2(10 + stats.health, 30), float2(stats.health * 2, 20), Color.red);
}
```

What views draw there is under the GUI's widgets, which draw last.

The mouse, touches and the pointer count pixels from the bottom left instead, with `y` up. `Screen.Flip(devices.pointer.position)` is where the pointer is in the screen's pixels: see [Screen and world coordinates](#screen-and-world-coordinates).

## Text

`Draw2D.Text(text, position, size, color)` draws text in the engine's font. Its `size` is its font size, as CSS's and Unity's: the height of a line of it, with its capitals in the line's middle. A newline in the text starts another line under it. Text is fitted to the display's pixels, so it's sharp at the sizes it's read at.

### Anchors

Without more, `position` is the text's top left corner. An `Anchor` after the color says which place of the text's box it is instead, the nine of Unity's `TextAnchor`: `UpperLeft`, `UpperCenter`, `UpperRight`, `MiddleLeft`, `MiddleCenter`, `MiddleRight`, `LowerLeft`, `LowerCenter` and `LowerRight`.

```csharp
view Banner(Round round)
{
    Draw2D.Screen();
    var middle = float2(Screen.width, Screen.height) * 0.5;
    Draw2D.Text("FIGHT!", middle, 64, Color.white, Anchor.MiddleCenter);
    Draw2D.Text($"Round {round.number}", float2(Screen.width - 16, 16), 24, Color.gray, Anchor.UpperRight);
}
```

That's a word in the middle of the screen, and the round's number against its top right corner, wherever the window's edges are and however wide the text comes out.

The box is as wide as the text's widest line and as tall as its lines. Upper is towards the top of the screen, whether the calls are in the world, where `y` goes up, or in the screen's pixels, where it goes down: a name over a body is `Draw2D.Text(player.name, body.position + float2(0, 1), 0.5, Color.white, Anchor.LowerCenter)` under any camera. Text of several lines lines up on the anchor's side: its lines start together at a left anchor, end together at a right one, and are centered on one between.

### Bold

The font has two weights. `Font.Bold`, last in the call, draws text in the font's own bold:

```csharp
Draw2D.Text("DUEL", at, 88, Color.white, Font.Bold);
Draw2D.Text("DUEL", at, 88, Color.white, Anchor.MiddleCenter, Font.Bold);
```

`Font.Regular` is the weight text has without one. A `Font` is a value like any enum's, so a function can take one and pass it on.

Bold is a second font file, and a game only carries it when its code says `Font.Bold`: about 95 KB more in the program, and nothing for a game that never does. Its strokes are a whole number of pixels thick, as the regular weight's are, so text stays sharp. At the smallest sizes, under 13 of the display's pixels, that's one pixel in both weights: bold is for text of about that size and up, and for titles. A letter the bold weight doesn't have (it covers Latin, Greek and Cyrillic, less widely than the regular one) is drawn in the regular weight.

The GUI's widgets are in the regular weight.

### Measuring text

To fit a panel around a text, or to put something beside it, ask how big it will be: `Draw2D.MeasureText(text, size)` returns a `float2`, the width of the text's widest line in `x` and the height of its lines in `y`. One line is `size` high, and each newline adds a line. It's the box an anchor names a place of.

```csharp
view Banner(Round round)
{
    Draw2D.Screen();
    var middle = float2(Screen.width, Screen.height) * 0.5;
    var size = Draw2D.MeasureText(round.message, 48, Font.Bold);
    Draw2D.Rect(middle, size + float2(32, 16), Color.black);
    Draw2D.Text(round.message, middle, 48, Color.white, Anchor.MiddleCenter, Font.Bold);
}
```

That's the message in the middle of the screen, on a panel that fits it. Bold text is wider than regular, so it's measured in its weight: `Draw2D.MeasureText(text, size, Font.Bold)`.

The answer is in the units the Draw calls are in: pixels after `Draw2D.Screen()`, and the world's under a camera.

It's what `Draw2D.Text` would draw at that point of the view, to the pixel. Text is fitted to the display's pixels to stay sharp, so the same text can come out a little wider or narrower on another display, under another camera or in a window of another size: measure where you draw, each frame, rather than keeping the answer. A [render target](#targets-and-post-processing) has its own pixels and camera, and `target.MeasureText(text, size)` measures for it.

## 3D

Meshes have a 3D version, which goes through a camera of its own. A corner is a `Vertex3`:

```csharp
struct Vertex3 // Built in
{
    float3 position;
    float2 uv;
    Color color = Color.white;
}
```

`Draw3D.Camera(position, rotation, fieldOfView)` sets the 3D camera: where it is, which way it looks, along its rotation's `+z` with `+x` to the right and `+y` up as Unity's cameras do, and how much it sees up and down, in degrees, like Unity's `Camera.fieldOfView`. It sees from 0.3 units in front of it on, however far, and as much across as the screen's shape gives. `Draw3D.Mesh(vertices, indices, transform)` draws a 3D mesh through it, placed in the world by `transform`, which `float4x4.TRS(position, rotation, scale)` makes:

```csharp
local singleton Scenery
{
    List<Vertex3> crate;
    List<int> faces;
    Grid2<Color> wood = Grid2(16, 16);
}

view Look(Viewer viewer)
{
    Draw2D.Clear(Color(0.5, 0.7, 1));
    Draw3D.Camera(viewer.position, viewer.rotation, 60);
}

// Once for each crate, and drawn for them all at once
view Crates(Crate crate, Scenery scenery)
{
    Draw3D.Mesh(scenery.crate, scenery.faces, float4x4.TRS(crate.position, crate.rotation, float3(1)), scenery.wood);
}
```

- 3D meshes hide each other by how far they are, whatever order they're drawn in. Everything else draws over what's there, in order, through the camera it had before: a view can draw a 3D scene and then a HUD over it, with no camera call between. `Draw2D.Clear` clears what's in front too.
- A see-through mesh blends with what's behind it, and still hides what's drawn behind it later: draw see-through meshes last, the farthest first.
- Triangles draw whichever way round their corners go. Textures and filters work as a mesh's do: `Draw3D.Mesh(vertices, indices, transform, texture, filter)`.
- There's no lighting built in: a pixel is its corners' colors times its texture's. A [`frag`](./shaders.md) lights it your way, with normals in a vertex struct of your own.
- Each frame starts with the 3D camera at the origin, looking along `+z`, with a field of view of 60 degrees.

### Primitives

The primitives draw solid colors through the current 3D camera, with depth and no lighting, as meshes do. Cube, cylinder and plane also take a rotation before the color:

```csharp
Draw3D.Cube(float3(0, 0, 5), float3(2, 1, 1), Color.red);
Draw3D.Sphere(float3(3, 0, 5), 1, Color.green);
Draw3D.Cylinder(float3(-3, 0, 5), 0.5, 2, Color.blue);
Draw3D.Plane(float3(0, -1, 5), float2(10, 10), quaternion.identity, Color.gray);
```

Nonpositive dimensions draw nothing. Geometry is cached by shape and color; consecutive instances of the same shape and color are drawn together. Use a mesh with a frag for textures or lighting.

### Big scenes

`Draw3D.Instanced(vertices, indices, transforms)` draws the mesh at every `float4x4` in a list, without an entity for each placement. It can also take a list of your own instance struct, with per-item fields read by a vert or frag: see [Instances from a list](./shaders.md#instances-from-a-list).

The lists a 3D mesh is drawn from are kept on the GPU while frames draw them. A list that didn't change costs next to nothing to draw again, however big it is, so keep meshes in fields rather than making them each frame. A list that changed is sent again the next frame.

A mesh baked for an entity of the match goes in [local fields](./local-state.md#local-fields) of its component: this machine's, which the view bakes once and draws from, and which the match never carries. A list in an ordinary field is part of the match, so every player who joins downloads it, though no system reads it.

```csharp
component Rock
{
    float3 position;
    int seed;
    local List<Vertex3> corners;
    local List<int> faces;
}

view Rocks(mut Rock rock)
{
    if (rock.corners.count == 0) Carve(rock); // Fills corners and faces from the seed, once
    Draw3D.Mesh(rock.corners, rock.faces, float4x4.Translate(rock.position));
}
```

A mesh many entities share, like the crate above, is one list in a singleton, or a local one.

A mesh drawn again and again in a row, from the same lists, with the same texture and filter, is drawn all at once: a view that runs for each of thousands of entities and draws one mesh costs the GPU one draw. Something drawn between them, like another mesh or a camera, starts another.

### Any projection

`Draw3D.Camera(transform, projection)` sets the 3D camera from matrices: `transform` places it in the world, looking along its `+z`, and `projection` takes what it sees to the screen, the screen's shape included. `float4x4.PerspectiveFov(verticalFov, aspect, near, far)` makes a perspective one, with the field of view in radians, and `float4x4.Ortho(width, height, near, far)` one without perspective, as isometric games have, as Unity.Mathematics' do. `Screen.width / Screen.height` is the screen's shape.

```csharp
view Isometric(Viewer viewer)
{
    var place = float4x4.TRS(viewer.position, quaternion.Euler(float3(Math.Radians(30), Math.Radians(45), 0)), float3(1));
    float height = 20;
    Draw3D.Camera(place, float4x4.Ortho(height * Screen.width / Screen.height, height, 0.1, 1000));
}
```

A game's C draws 3D meshes too, with vertices of its own, like a model a library loaded: see [Drawing from C](../c/functions.md#drawing-from-c).

## Screen and world coordinates

The conversion helpers use the currently selected camera, including a camera set earlier in the same frame. Their screen coordinates are logical pixels from the top left, with `y` down, as `Draw2D.Screen()` and the GUI's are.

| Function | Result |
|---|---|
| `Draw2D.WorldToScreen(float2 point)` | The point's screen position |
| `Draw2D.ScreenToWorld(float2 point)` | Its position under the current 2D camera |
| `Draw3D.WorldToScreen(float3 point)` | Screen `x` and `y`, and normalized depth in `z` |
| `Draw3D.ScreenToWorld(float3 point)` | The world position at screen `x`, `y` and normalized depth `z` |
| `Draw3D.ScreenRayOrigin(float2 point)` | The world position on the camera's near plane |
| `Draw3D.ScreenRayDirection(float2 point)` | A normalized world direction through that pixel |

In 3D, depth is the depth buffer's value: 0 at the near plane and 1 at the far limit, rather than a distance in world units. The standard perspective camera has an infinite far limit, so depth 1 has no finite world position. The ray helpers work with perspective and orthographic cameras, including custom projection matrices. Intersect the ray with your game's objects or a plane to pick a world position; a pixel alone has no unique 3D position.

The devices count pixels the other way: from the bottom left, with `y` up, as Unity's Input System does. `Screen.Flip(position)` gives the same point counted from the screen's other edge, so it turns a device's position into a screen position, for these helpers, `Draw2D.Screen()` and a `Rect` of the GUI's. It's its own inverse: it turns a screen position into a device's too.

```csharp
view Pointer(Devices devices)
{
    var screen = Screen.Flip(devices.pointer.position);
    var world = Draw2D.ScreenToWorld(screen);
    Draw2D.WireCircle(world, 0.2, Color.yellow);

    var origin = Draw3D.ScreenRayOrigin(screen);
    var direction = Draw3D.ScreenRayDirection(screen);
    // Hit the world plane y = 0, when the ray points towards it.
    if (Math.Abs(direction.y) > 0.00001)
    {
        float distance = -origin.y / direction.y;
        if (distance >= 0) Draw3D.Sphere(origin + direction * distance, 0.1, Color.yellow);
    }
}
```

After `Draw2D.Screen()`, its conversion helpers are identity mappings. A viewport with no size, a singular camera matrix, or a point whose projection has no finite position returns zero.

## Looking around

A camera that turns with the mouse wants the mouse for itself: the cursor out of sight, and no screen's edge for it to stop at. `Cursor.Lock()` asks for that. Call it every frame the game wants the mouse, as with `GUI.ShowKeyboard()`: the cursor comes back by itself once a frame goes by without it, so there's nothing to unlock. `Cursor.locked` says whether the lock holds.

```csharp
input PlayerInput
{
    float2 look;

    Sample(Devices devices)
    {
        if (Cursor.locked) look = devices.mouse.delta * 0.003;
    }
}

system Turn(PlayerInput input, mut Viewer viewer)
{
    viewer.yaw += input.look.x;
    viewer.pitch = Math.Clamp(viewer.pitch - input.look.y, -1.5, 1.5);
    viewer.rotation = quaternion.Euler(float3(viewer.pitch, viewer.yaw, 0));
}

local singleton Menu
{
    bool paused;
    bool looking; // The window held the cursor last frame
}

view Look(Devices devices, Viewer viewer, mut Menu menu)
{
    Draw3D.Camera(viewer.position, viewer.rotation, 75);
    if (devices.keyboard.escape.down) menu.paused = true;
    if (!menu.paused) Cursor.Lock();
    // The lock went away on its own: show the pause menu
    if (menu.looking && !Cursor.locked) menu.paused = true;
    menu.looking = Cursor.locked;
}

view PauseMenu(mut Menu menu)
{
    GUILayout.Modal(Anchor.MiddleCenter, menu.paused)
    {
        if (GUILayout.Button("Resume")) menu.paused = false;
    }
}
```

While the cursor is locked:

- It's hidden, and `devices.mouse.position` stays where it was, as does the pointer's.
- `devices.mouse.delta` keeps coming, however far the mouse goes. Where the system gives it, it's the mouse's own movement, without the acceleration a cursor has, so the camera turns evenly.
- The GUI sees no pointer: nothing hovers, and a click presses no widget, so it's all the game's. A finger on a touchscreen still presses what it touches.

::: warning Read `Cursor.locked`, don't assume
Asking isn't having. The system decides whether the window holds the cursor, and it differs:

- **In a browser,** the lock is only given at a click or a key press, so it takes hold at the player's next one, or at once when the game asks right after one, like a Resume button's click. The browser lets the cursor go when the player presses Escape, which a page can't prevent and often never sees the key of, and when the page loses the focus. For a moment after Escape it won't lock again, and then it takes another click or key press.
- **On desktop,** a window holds the cursor while it has the focus: Alt+Tab lets it go, and the lock returns with the focus if the game still asks. Escape lets it go too, as in a browser, and the game still gets the key: the cursor is the player's until they click on the window or press another key, or until the game stops asking and asks again, which locks at once.
- **On Android,** it never locks, for now.

So look only while `Cursor.locked`, and show the pause menu when it goes false while the game was looking, as above: that's what Escape does, in a browser and on desktop. A game with no pause menu just keeps asking, and the player's next click or key press locks the cursor again.
:::

Only views, and the functions they call, call `Cursor.Lock()`. They read `Cursor.locked`, and so does the input's `Sample`. The match can do neither: the cursor is this machine's, so what the match needs of it goes in the input. While a [modal](./gui.md#modals) is up the lock isn't asked for, whatever views ask, since its widgets need the pointer.

## Smooth at any tick rate

Views draw at whatever frame rate the game renders, not the tick rate, and see the match blended between its last two ticks. So motion is smooth even at 20 ticks per second, at the cost of drawing up to a tick late.

Every float, vector, quaternion and color a view reads of the match is blended, `Time.t` among them. Ints, bools, enums, entities and text are as they are at the latest tick. An entity that wasn't there last tick is drawn as it is. What a view reads [`verified`](#confirmed-state) isn't blended: it's as the last tick the server confirmed has it.

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

`[Position]` on a float or floating-point vector keeps linear tick interpolation and accumulates an additive presentation offset. `[Rotation]` on a float uses the shortest wrapped angular difference, in radians. On a quaternion it uses the shortest relative rotation, for both tick interpolation and correction smoothing.

The error decays exponentially with frame time toward zero, or the identity rotation, with a provisional half-life of 75 ms. Another correction accumulates with whatever error remains. Ordinary movement follows the corrected simulation, so moving and aiming stay responsive. Untagged fields keep ordinary tick interpolation.

This state belongs to this machine's presentation. Simulation values and collisions correct immediately, and snapshots, hashes and network state are unaffected. `entity.Snap()` and `singleton.Snap()` also clear any accumulated error, keeping intentional jumps immediate. A new match, a hot reload or a replacement world with no comparable presentation history clears the error too. While a host migration reconnects, the frozen presentation retains its error; adopting the new network timeline clears it.

The tags take no arguments. Use them on match fields; local state, inputs and events cannot carry them. A field cannot combine `[Position]`, `[Rotation]` and `[Snap]`.

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

`Interpolate` isn't for code to call. Singletons have no methods, so a singleton blends its own way through a struct in it that has one.

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
        Draw2D.Circle(spark.position, 0.1 * (1 - spark.age / spark.life), Color.yellow);
        i++;
    }
}
```

- **It's this machine's, not the match's.** It's there with no match, in a menu and while connecting, where a view that takes `Time` doesn't run. In a match it's the same value, whatever the tick rate.
- **The match has `Time`.** What's in the match moves by `time.dt`, a tick's length, in systems. A view draws the match as it is, already [blended](#smooth-at-any-tick-rate) to the frame, and uses `Frame.dt` for what it adds on top. A system that reads `Frame.dt` is an error that says so.
- **It's the frame's real length.** A frame after the window was dragged, or the page was hidden, is as long as that took. Clamp it where a big step would show: `var dt = Math.Min(Frame.dt, 0.05);`.
- **There's no clock beside it.** A float of seconds gets coarser as it grows: after four and a half hours it counts in steps of 2 milliseconds, and whatever it animates starts to judder. Add `Frame.dt` up into a field of your own instead, and wrap it where what it drives repeats:

```csharp
local singleton Spinner
{
    float turn; // Radians, always under a full turn
}

view Spin(mut Spinner spinner)
{
    spinner.turn += Frame.dt * 4;
    if (spinner.turn > Math.TAU) spinner.turn -= Math.TAU;
}
```

Views read `Frame.dt`, and so does the rest of [local code](./local-state.md#time): the functions they call, local handlers and local tasks.

## Reading this machine's devices

Views take `Devices devices` to read this machine's input devices for the frame: `.down` and `.up` since the last frame, and the `delta` of the mouse, touches and the pointer, and the mouse's `scroll`, too. A view that wants the mouse for itself [locks the cursor](#looking-around). What the GUI is using is hidden from them, and so is what another view [claimed](./gui.md#widgets-of-your-own) for widgets of its own. They read [what's typed](./input.md#typed-text) too, as `devices.keyboard.text`. Helpers take the devices or a part of them as an explicit parameter. A helper that reads `keyboard.text` needs the frame, so only views and the functions they call can call it.
