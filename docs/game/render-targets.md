---
description: "Render targets in Tide: draw into an image, bring it to the window through a frag for post-processing, and pick its format, its samples for smooth edges, several colors and its depth."
---

# Render targets and post-processing

`RenderTarget` names a local drawing destination. It has every method `Draw` has, and `Draw`'s own calls draw into `Draw.window`, which is a `RenderTarget` too. Every setting belongs to the destination it's called on, and is shared across views within the frame. Switching destinations preserves color, depth and [stencil](./draw-settings.md#stencil), and a clear resets all three, within that destination's clip.

There are three ways to make one:

```csharp
render.scene = RenderTarget.Window();      // Always the window's size, at full resolution
render.blur  = RenderTarget.Window(0.5);   // Always half of it, each way
render.card  = RenderTarget(200, 300);     // A size of its own, in logical pixels, at the display's resolution
render.small = RenderTarget(320, 180, 1);  // A size of its own, in exactly these pixels
```

A target's size is in logical pixels, like the window's, and its image is made at the display's scale. So drawing into a target is the same as drawing into the window: on a display scaled to 150%, a target of 200 by 300 has 300 by 450 pixels, its text is fitted to them, its shapes' edges are one of them wide, and `target.Screen()`, clips, conversions like `WorldToScreen` and a frag's `this.position` are all in logical pixels. A pass over the whole screen that goes through a target comes out as sharp as the window.

- `RenderTarget.Window()` is for exactly that: a target that's always the window's size. `RenderTarget.Window(factor)` is always that part of it, for the smaller targets a blur or a glow goes through. The engine makes them again when the window's size changes, so there's nothing to resize and nothing to forget. What was drawn in one is lost then, which doesn't matter for a target a view draws again every frame. `Resize` on one is refused.
- `RenderTarget(width, height)` is a target of a size of its own. `Resize(width, height)` changes it for every alias and discards its contents, even at the same size.
- A third argument is the target's pixels per unit, where the display's isn't wanted: `RenderTarget(320, 180, 1)` is exactly 320 by 180 pixels on every display, for pixel art and anything else that must not depend on the display.

When the display's scale changes (the window moves to another monitor, a browser zooms), the targets at the display's scale are made again, empty. One with pixels per unit of its own is left alone. There's no way to read the display's scale, and nothing here needs it.

Otherwise a target's color, depth and stencil persist across frames until cleared. Copies share the image, including copies of `.color`. Call `Dispose()` when finished; every alias becomes invalid, and disposing again is harmless. A default target is invalid. Creation and lifetime methods run in views or local handlers and their helpers; field defaults cannot allocate targets.

## Post-processing

```csharp
local singleton RenderState { RenderTarget scene; bool made; }
enum RenderStages { Prepare, Objects, Effects }

frag Present(Vertex vertex, out Color color, Texture source)
{
    color = source.Sample(vertex.uv);
}

[Stage(RenderStages.Prepare)]
view Prepare(mut RenderState render)
{
    if (!render.made)
    {
        render.scene = RenderTarget.Window();
        render.made = true;
    }
    render.scene.Clear(Color.black);
    render.scene.SetCamera(float2(0), 10);
}

[Stage(RenderStages.Objects)]
view Objects(Body body, RenderState render)
{
    render.scene.Circle(body.position, body.radius, Color.white);
}

[Stage(RenderStages.Effects)]
view Show(RenderState render)
{
    Draw.Blit(render.scene.color, Draw.window, Present);
}
```

The view below draws its scene into a target, then blits it to the window through a frag that reads it in blocks. The left of the line is the image as it was drawn. Move the sliders to change the blocks' size and where the line is.

::: tide-example post A scene through a frag

<<< @/examples/post.tide

:::

`Draw.Blit(source, destination, frag)` runs the frag on a fixed `Vertex` quad over the entire destination. It supplies the frag's read-only `Texture` parameter from `source`, replaces color and alpha, and preserves depth. It temporarily ignores camera and clip, then restores them. Its source can also be a `Grid2<Color>`, uploaded from CPU cells. Sampling the image being written is an error; use a second target between effects.

What one pass needs beyond its source goes after the frag, as a struct the frag takes: `Draw.Blit(lens.bright.color, lens.spread, Blur, BlurStep { along = float2(1, 0) })` and then the same frag the other way, `BlurStep { along = float2(0, 1) }`. Those are [the Blit's values](./shaders.md#values-for-one-draw), and each Blit has its own.

A Blit replaces what's in its destination, and nothing blends two images for you yet, so an effect that adds to the picture, like a glow, ends in a frag that reads both and writes their sum: the scene as the Blit's source, and the other image as a `Texture` field of a singleton the frag takes. The [Duel](../guide/duel.md) example does it: its arena goes into a `RenderTarget.Window()`, the arena's bright parts are blurred through targets a quarter and an eighth of the size, and one frag puts the arena and its light in the window. Its score and menus are drawn into the window afterwards, so their text has none of that light on it.

A frag has no way to ask how many pixels a texture has. It doesn't need to: a step of a blur is a logical pixel, `1 / Screen.width` across the screen's texture and `1 / Screen.height` down, which the view puts in a singleton for the frag, and the blur is then as wide on every display.

Target color is GPU data: `.color` gives a `Texture` reference and does no readback, as `.colors[i]` does for each image of a target that holds several, and so is its depth, a `DepthTexture`. None of them is assigned: what's drawn into the target is what changes them. Neither has a constructor; CPU pixel sources remain `Grid2<Color>`, and C can supply a CPU texture reference. `Draw.window` has no sampleable color or depth and cannot be resized or disposed by the game. Texture references, depth references and targets can be stored only in local state. The GUI and stats draw onto the window. Hot reload preserves managed references, their sizes and pixels per unit, whether they follow the window, what they hold, and shared aliases, recreating their images on the web; it discards rendered contents and clears borrowed references supplied by the old build. Device loss also discards rendered contents. Drawing with an invalid or disposed target is skipped with a diagnostic; disposal also invalidates uses already recorded in the frame.

## What a target holds

Every target has depth, which its 3D meshes read and write, and a color image beside it. A target says otherwise when it's made, with a `Format` after its size, and keeps what it said from then on:

```csharp
lamp.map    = RenderTarget(1024, 1024, 1, Format.Depth);  // Depth alone, in exactly these pixels
lamp.map    = RenderTarget(512, 512, Format.Depth);       // ...or at the display's resolution
lamp.screen = RenderTarget.Window(Format.Depth);          // ...or the window's size, or part of it:
lamp.half   = RenderTarget.Window(0.5, Format.Depth);
glow.light  = RenderTarget.Window(Format.Rgba16f);              // A color image that keeps numbers past 1
```

| Format | What an image of it holds | For |
|---|---|---|
| `Format.Rgba8` | A color of 4 bytes, each channel from 0 to 1: what's made when nothing is said | Everything that's looked at |
| `Format.Rgba16f` | A color of four floats, each any number, to three digits or so | Light that adds up past white, and numbers that aren't colors: a normal, a place, a speed |
| `Format.Depth` | No color image at all | What only a later frag reads: a shadow map, a pass that lays down depth |

A `Format.Rgba16f` image is read as it was written: `Sample` gives the numbers back, blended between its pixels like any texture's, and what's drawn into it is blended and added as into any other, without stopping at 1. It takes twice the memory of a `Format.Rgba8` one. A clear's color is a color, each channel from 0 to 1, whatever the image holds.

::: warning Where a GPU can't draw into floats
Nearly every machine does. An old phone, or an old browser on WebGL, may not: there a `Format.Rgba16f` image is made as a `Format.Rgba8` one, and the engine says so once, in the game's output. The game runs, with what it draws there kept from 0 to 1: light stops at white, and what's under 0 is 0.
:::

A target of depth alone is drawn into like any other: it has its `Clear`, its cameras and its `Mesh`. A color has nowhere to go there, so the only things that leave anything are the ones that write depth, or [stencil](./draw-settings.md#stencil), which it has too:

- **A clear** sets its depth back to the far limit, and its stencil to 0. It takes a color like any clear, which goes nowhere.
- **A 3D mesh** writes how far it is, with a frag or without. A mesh drawn with no frag has the engine's own look, as anywhere, and in this target that look's color is dropped: what a draw writes is said by where it's drawn, never by what it leaves out. A frag still runs for each pixel, as it may leave some out with `this.Discard()`, which is how a fence or a leaf casts a shadow with holes in it.
- **A draw that stores stencil** stores it, whatever it is: a shape, text, a mesh. Its color goes nowhere.
- **Everything else draws nothing there:** shapes, text, 2D meshes, a `Draw.Blit` into it, and 3D meshes under [`Depth.Read` or `Depth.None`](./3d.md#depth).

## Smooth edges

Text and the smooth shapes have smooth edges by themselves. A mesh's triangles don't: each pixel is in a triangle or out of it, so a slanted edge is a staircase. A target made with `samples` draws each of its pixels with that many samples, and a triangle's edge covers some of them, so the pixels along it are part the triangle's color and part what's behind it:

```csharp
render.scene = RenderTarget.Window(samples: 4);                  // The window's size, 4 samples a pixel
render.light = RenderTarget.Window(Format.Rgba16f, samples: 4);  // ...after what it holds
render.card  = RenderTarget(200, 300, samples: 2);               // ...or after a size of its own
```

`samples` is the last argument, and it's written with its name. It's 1, 2, 4 or 8; 1 is a target that says nothing.

The window itself has none. For a frame with smooth edges, draw the scene into a target of the window's size and bring it to the window:

```csharp
view Scene(mut Render render)
{
    render.scene.Clear(Color.black);
    render.scene.SetCamera(eye, look, 1.0);
    render.scene.Mesh(render.crate, render.faces, float4x4.identity);
}

[After(Scene)]
view Show(Render render)
{
    Draw.Blit(render.scene.color, Draw.window, Copy);  // What its samples came to, one color a pixel
}

frag Copy(Vertex vertex, out Color color, Texture source)
{
    color = source.Sample(vertex.uv);
}
```

- **Its color is read like any target's.** `target.color` and `target.colors[i]` are what each pixel's samples come to, worked out each time drawing into the target ends. A frag samples them, and a `Draw.Blit` reads them, as it would any other target's.
- **Its depth can't be read.** A GPU keeps several depths a pixel there, with no one depth for a frag to sample. A draw that reads `target.depth` of a target with samples is passed by, with a line that says so in the game's output. A shadow map, or any depth a frag reads, goes into a target without `samples`. `Format.Depth` with `samples` is an error for the same reason.
- **It costs memory:** as many colors and depths a pixel as it has samples, plus the color they come to. Four samples at the window's size is about four and a half times a plain target. A frag still runs once for each pixel a triangle covers, so drawing costs little more.
- **A device may have fewer.** 4 is there on everything Tide draws with, for images of bytes. Where a device hasn't got what's asked, the target is drawn with the most it has under that, and the engine says so once in the game's output. WebGPU has 1 and 4 alone: 2 is 4 there, and 8 is 4.
- **Everything draws into it** as into any target: shapes, text, meshes, frags, clears, [stencil](./draw-settings.md#stencil). What's in it persists from frame to frame, like any target's.

## Several colors

A target holds up to four color images, one for each format it's made with, and a frag writes them all at once: each of its `out Color` parameters goes to the image that's its place among them (see [Frags and verts](./shaders.md#several-outputs)).

```csharp
local singleton Deferred
{
    RenderTarget scene;
    Texture albedo;
    Texture normals;
    Texture places;
}

view Make(mut Deferred deferred)
{
    // A color as it's looked at, and beside it two images of floats
    deferred.scene   = RenderTarget.Window(Format.Rgba8, Format.Rgba16f, Format.Rgba16f);
    deferred.albedo  = deferred.scene.color;       // Its first image, as ever
    deferred.normals = deferred.scene.colors[1];   // ...its second
    deferred.places  = deferred.scene.colors[2];   // ...and its third
}
```

- **`target.colors[i]` is the image at that place,** a `Texture` like `target.color`, which is the first. They're counted from 0, in the order of the formats. Which images a target has is said where it's made, so one it hasn't got is only found out as a draw samples it: that draw is passed by, with a line that says so.
- **A frag's outputs go to them in order.** Its first `out Color` is the first image, its second the second. An output with no image in its place goes nowhere, as a color does in a target of depth alone, so the same frag draws into the window, where its first is the pixel and the rest are dropped. An image past the frag's outputs is left as it was.
- **What the engine draws itself has one color,** which goes into the first image and leaves the others: shapes, text, and meshes drawn with no frag.
- **A clear fills every image** with its color, and sets depth back.
- **`Draw.Blit` replaces as many images as its frag writes,** and leaves the others.
- **Each output is blended like the pixel's color:** over what's in its own image, as the draw's [blend](./draw-settings.md#blending) says, by its own alpha, after the draw's [tint](./draw-settings.md#tints). `Color(x, y, z)` has an alpha of 1, so it replaces what was there, which is what a normal or a place wants.
- `Format.Depth` is said alone: every target has its depth already, and `target.depth` reads it whatever colors are beside it.

The formats are the constructor's last arguments, after its pixels per unit when it says them: `RenderTarget(1024, 1024, 1, Format.Rgba8, Format.Rgba16f)`. Only [`samples`](#smooth-edges) comes after them.

[Frags and verts](./shaders.md#several-outputs) has a scene drawn this way to try: one frag fills three images, and a second lights the window from them.

## A target's depth

A frag reads a target's depth as it reads its color: `target.depth` beside `target.color`. It's a `DepthTexture`, which a view keeps in local state, and a frag reads where it's a field of a component or a singleton it takes (see [Frags and verts](./shaders.md#a-target-s-depth)):

```csharp
local singleton Lamp { RenderTarget map; DepthTexture shadow; }

view Make(mut Lamp lamp)
{
    lamp.map = RenderTarget(1024, 1024, 1, Format.Depth);
    lamp.shadow = lamp.map.depth;   // A reference, like a Texture: copies share the image
}
```

Every target has one, with color or without, and `Draw.window` has none to read. A depth is how far the nearest thing drawn at a pixel is, from 0 at the camera's near plane to 1 at its far limit, and 1 where nothing was drawn since the target was cleared.

A draw into a target can't read that target's depth, any more than its color: the draw is passed by, with a line that says so. Draw what reads it into another target, or the window.
