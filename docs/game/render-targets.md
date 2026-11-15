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

`target.size` is its size in logical pixels: the window's share for one that follows it, and the size it was made or resized to otherwise. `target.valid` says whether drawing into it draws: false for a target never made (a field's default) and for one disposed, true for `Draw.window`. So a view makes one the first frame it needs it:

```csharp
if (!ui.card.valid) ui.card = RenderTarget(320, 200);
```

When the display's scale changes (the window moves to another monitor, a browser zooms), the targets at the display's scale are made again, empty. One with pixels per unit of its own is left alone. There's no way to read the display's scale, and nothing here needs it.

Otherwise a target's color, depth and stencil persist across frames until cleared. Copies share the image, including copies of `.color`. Call `Dispose()` when finished; every alias becomes invalid, and disposing again is harmless. A default target is invalid. Creation and lifetime methods run in views or local handlers and their helpers; field defaults cannot allocate targets.

## Post-processing

For an image baked only when needed, keep its `contentRevision` in local state.
This read-only `long` changes when its pixels are discarded by resizing, hot
reload or device loss, including automatic window/display changes. Copies of
the target share the revision. Ordinary drawing and clearing leave it unchanged.
An invalid or disposed target and `Draw.window` return zero.

```csharp
if (!cache.card.valid) cache.card = RenderTarget(200, 300);
var revision = cache.card.contentRevision;
if (cache.bakedRevision != revision)
{
    cache.card.Clear(Color.black);
    // Draw the cached image here.
    cache.bakedRevision = revision;
}
```

Store `bakedRevision` as a `long` alongside the target. Compare for equality;
the revision is an invalidation token, not a count of drawing operations.

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

`Draw.Blit(source, destination, frag)` runs the frag on a fixed `Vertex` quad over the entire destination. It supplies the frag's first read-only `Texture` parameter from `source` (any others from the textures after it, below), replaces color and alpha, and preserves depth. It temporarily ignores camera and clip, then restores them. Its source can also be a `Grid2<Color>`, uploaded from CPU cells. Sampling the image being written is an error; use a second target between effects.

What one pass needs beyond its source goes after the frag, as a struct the frag takes: `Draw.Blit(lens.bright.color, lens.spread, Blur, BlurStep { along = float2(1, 0) })` and then the same frag the other way, `BlurStep { along = float2(0, 1) }`. Those are [the Blit's values](./shaders.md#values-for-one-draw), and each Blit has its own.

A frag that reads more than one image takes a `Texture` parameter for each, and the Blit gives them in that order, before its destination: `Draw.Blit(scene.color, lens.glow.color, Draw.window, Combine)` for `frag Combine(Vertex vertex, out Color color, Texture scene, Texture glow)` (see [Several textures](./shaders.md#several-textures)).

A Blit replaces what's in its destination, and nothing blends two images for you yet, so an effect that adds to the picture, like a glow, ends in a frag that reads both and writes their sum: the scene as the Blit's source, and the other image as the Texture after it, or as a `Texture` field of a singleton the frag takes. The [Duel](https://github.com/Tide-Engine/tide-engine/tree/dev/examples/duel) example does it: its arena goes into a `RenderTarget.Window()`, the arena's bright parts are blurred through targets a quarter and an eighth of the size, and one frag puts the arena and its light in the window. Its score and menus are drawn into the window afterwards, so their text has none of that light on it.

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

| Format | Frag output | Bytes per pixel | What it keeps |
|---|---|---|---|
| `Format.Rgba8` | `Color` | 4 | Four channels from 0 to 1; the default |
| `Format.Rgba16f` | `Color` or `float4` | 8 | Four 16-bit floats, up to about 65,000 |
| `Format.R8` | `float` | 1 | One channel from 0 to 1 |
| `Format.Rg8` | `float2` | 2 | Two channels from 0 to 1 |
| `Format.R16f` | `float` | 2 | One 16-bit float |
| `Format.Rg16f` | `float2` | 4 | Two 16-bit floats |
| `Format.R32f` | `float` | 4 | One 32-bit float |
| `Format.Rgba32f` | `float4` | 16 | Four 32-bit floats |
| `Format.R32i` | `int` | 4 | One signed 32-bit integer, exactly |
| `Format.Depth` | No color image | — | Depth and stencil alone |

A number output replaces what's in its image whatever blend is set, with no tint or alpha multiplication.
In a `float4`, all four channels are independent: `float4(2, -1, 0.5, 0.25)` keeps its fourth channel without
multiplying the first three by it. A `Color` output retains normal tint, alpha and blend behavior, including
in `Rgba16f`. An output that does not fit the image is a compile error at the draw, naming the type to use.

Read a numeric image through a declared [typed texture field](./shaders.md#number-images):
`[Raw(float2)] Texture velocity;` then `velocity = target.colors[1];` in local code. Its `Sample` returns a
`float2`. A typed read that names no filter reads the nearest pixel. Images in `R8`, `Rg8`, `R16f`, `Rg16f`
and `Rgba16f` also blend (`Filter.Bilinear`, `Filter.Trilinear`, `Filter.Anisotropic`) and have mipmaps, read
with `SampleLevel` and `SampleGrad`: a blurred mask or a variance shadow map works on every device. `R32f` and
`Rgba32f` blend with `Filter.Bilinear` as four exact loads blended in the shader, the same bits on every device
(only some GPUs blend 32-bit floats themselves), and have no mipmaps; `R32i` reads its nearest pixel, since no GPU
blends integers. Asking one of them for what it hasn't got is a compile error that names the format to draw into
instead.

Every number format makes array layers and cube faces: `TextureArray(256, 256, 4, Format.R16f)`,
`Cubemap(256, Format.R32f)`, `TextureArray(64, 64, 8, Format.R32i)`. Draw numbers into `array.layers[i]` or
`cube.faces[i]`, and read them through a `[Raw(float)] TextureArray`, `[Raw(int)] Cubemap` or other typed field.
A cube of `R32f`, `Rgba32f` or `R32i` is read at its nearest pixel, exactly: a distance cube a lamp's shadow keeps
to full precision, or an id per direction.

### A cube of depth alone

`Cubemap(1024, Format.Depth)` is six faces of depth alone, for a lamp that shines every way. Draw each face from
the lamp's place, as for any cube, and read the depth through `cube.depth`, a `DepthCubemap`, kept in a field of a
component or singleton the frag takes. `Sample(direction)` gives the nearest thing's depth that way, 0 near to 1
far, and `Compare(direction, depth)` how much of what's stored around there isn't nearer than `depth`, blended
within the face: 1 is lit.

```tide
local singleton Lamp { Cubemap cube; DepthCubemap shadows; float3 at; bool made; }

view MakeLamp(mut Lamp lamp)
{
    if (!lamp.made) { lamp.cube = Cubemap(1024, Format.Depth); lamp.shadows = lamp.cube.depth; lamp.made = true; }
    // ...then each face drawn from lamp.at with float4x4.PerspectiveFov(1.5707964, 1, 0.1, 50)
}

// The depth a face's camera stores for a point `way` from the lamp: along the face's axis, its perspective's
float LampDepth(float3 way)
{
    float along = Math.Max(Math.Abs(way.x), Math.Max(Math.Abs(way.y), Math.Abs(way.z)));
    return 50 / (50 - 0.1) * (1 - 0.1 / along);
}

frag Lit(Vertex3 vertex, out Color color, Lamp lamp)
{
    float3 way = this.world - lamp.at;
    float lit = lamp.shadows.Compare(way, LampDepth(way) - 0.0005);
    color = Color(lit, lit, lit);
}
```

Its faces read back with `await cube.faces[i].ReadDepth()`. A cube of colors keeps no depth to read: each face
draws with depth of its own, and `cube.depth` of one is a compile error where its format is known.

### Clearing each image

Supply one value per image, in format order. Values have the same types as frag outputs; floats can be
negative or greater than one, and an integer image takes an `int`:

```csharp
render.data = RenderTarget(256, 256, 1, Format.R16f, Format.Rg16f, Format.Rgba32f, Format.R32i);
render.data.Clear(2.5, float2(-1, 3), float4(2, 4, 8, 0.25), -17);
```

The clear also resets depth and stencil, within the destination's clip. The existing single-`Color` clear
continues to fill every image of a target containing ordinary color formats. Number clears preserve their
channels without tint, blending or alpha multiplication; byte formats clamp and quantize them to 0–1.

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

`samples` is the last argument, and it's written with its name. It's 1, 2, 4 or 8; 1 is a target that says nothing. `R32f`, `Rgba32f` and `R32i` require one sample. A target containing any of them cannot use MSAA. The other formats resolve each channel by the mean of the samples, including numeric channels.

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

- **Its color is read like any target's.** `target.color` and `target.colors[i]` are what each pixel's samples come to, worked out each time drawing into the target ends. A frag or a kernel samples them, and a `Draw.Blit` reads them, as it would any other target's: between its smaller images too (`Filter.Trilinear`, `SampleLevel`), made of what the samples came to. (Unity's replay doesn't make those yet, and reads the resolved image alone there.)
- **Its depth is read like any target's.** `target.depth` is one depth a pixel: one of the pixel's samples, as they are when the draw that reads it comes. It's only worked out for a target whose depth some draw reads, and again only after more was drawn into the target, so a target nothing reads the depth of costs nothing more. An edge in it is a step, as in a target with no samples. `Format.Depth` with `samples` is still an error: a target of depth alone has one sample.
- **It costs memory:** as many colors and depths a pixel as it has samples, plus the color they come to. Four samples at the window's size is about four and a half times a plain target. A frag still runs once for each pixel a triangle covers, so drawing costs little more.
- **A device may have fewer.** 4 is there on everything Tide draws with, for images of bytes. Where a device hasn't got what's asked, the target is drawn with the most it has under that, and the engine says so once in the game's output. WebGPU has 1 and 4 alone: 2 is 4 there, and 8 is 4.
- **Everything draws into it** as into any target: shapes, text, meshes, frags, clears, [stencil](./draw-settings.md#stencil). What's in it persists from frame to frame, like any target's.
- **Edges a frag cuts with `this.Discard()` aren't smoothed:** the frag decides once a pixel. A frag that calls [`this.Cutout(alpha)`](./shaders.md#cut-outs) has its alpha say where it's there instead, and those edges are as smooth as the triangles' (alpha to coverage).

## Several colors

A target holds up to four color images, one for each format it's made with, and a frag writes them all at once: each of its `out` parameters goes to the image that's its place among them (see [Frags and verts](./shaders.md#several-outputs)).

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
- **A frag's outputs go to them in order.** Its first output is the first image, its second the second; each type must fit that image's format. An output with no image in its place goes nowhere, as a color does in a target of depth alone, so the same frag draws into the window, where its first is the pixel and the rest are dropped. An image past the frag's outputs is left as it was.
- **What the engine draws itself has one color,** which goes into the first image and leaves the others: shapes, text, and meshes drawn with no frag.
- **A clear supplies each image's value** in order, and sets depth back; a single Color retains its broadcast behavior for ordinary color targets.
- **`Draw.Blit` replaces as many images as its frag writes,** and leaves the others.
- **Each Color output is blended like the pixel's color:** over what's in its own image, as the draw's [blend](./draw-settings.md#blending) says, by its own alpha, after the draw's [tint](./draw-settings.md#tints). Numeric outputs always replace what's there, with no tint or alpha multiplication; a float4 has four independent channels.
- `Format.Depth` is said alone: every target has its depth already, and `target.depth` reads it whatever colors are beside it.

The formats are the constructor's last arguments, after its pixels per unit when it says them: `RenderTarget(1024, 1024, 1, Format.Rgba8, Format.Rgba16f)`. Only [`samples`](#smooth-edges) comes after them.

[Frags and verts](./shaders.md#several-outputs) has a scene drawn this way to try: one frag fills three images, and a second lights the window from them.

## A target's depth

Depth storage uses reversed 32-bit floats with stencil where supported, improving distant
surface separation without changing camera or shader conventions. Clip coordinates still
use OpenGL's -w to +w range. `this.depth`, depth `Sample` and `Compare` references still
use 0 near and 1 far. These forward reads lose the additional far precision of internal
storage; there is currently no raw or precise linear-depth read. WebGPU devices without
`depth32float-stencil8`, and Vulkan devices without the required float depth support, keep
the earlier forward depth path and its precision. Clear depth still means the far limit.


A frag reads a target's depth as it reads its color: `target.depth` beside `target.color`. It's a `DepthTexture`, which a view keeps in local state, and a frag reads where it's a field of a component or a singleton it takes (see [Frags and verts](./shaders.md#a-target-s-depth)). `target.depth.size` gives the actual physical dimensions in local CPU code, including the device's size limit, even for a depth-only target. The retained depth reference's `.size` also works in shaders. This reads metadata without allocating, resolving or reading back the image; `target.size` still gives logical dimensions:

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

## Reading pixels in local code

In a local async function, `await target.Read()` returns `Grid2<Color> fails IOError`, an owned copy of the
first color image after the requested frame draws. For example:

```tide
async void SavePixels(RenderTarget target)
{
    if (await target.Read() is Grid2<Color> pixels)
    {
        // pixels.size is the physical image size; pixels[x, y] is a straight Color.
    }
}
```

It takes a frame or more and can fail if the device is lost or what reads hold together passes the limit. Never
read it in match code. Multisampled targets read their resolved color. Float images retain colors above one, and
alpha is not multiplied into RGB. Copy a grid into a field with `.Copy()`, following normal grid ownership, or keep
it in the task: a task keeps a grid across its later waits as it keeps its lists, so `pixels` can still be read
after another `await`. Reads share 64 slots with GPU list reads. An image of any size is read, a 4K or 5K window's
too; what reads hold together, until they're let go of, is at most 512 MiB.

### A part of an image

A read takes a part of the image after its number, if it has one: its first pixel and how many pixels across and
down, in the image's own pixels, as the grid is. The part is clamped to the image, and the grid is what's inside it,
from that part's first pixel; a part with nothing of the image in it fails. Reading one pixel under the mouse copies
one pixel, and a big image can be read in tiles.

```tide
local singleton Picked { RenderTarget ids; int id; }

async void PickAt(mut Picked picked, int2 pixel)
{
    // ids: RenderTarget.Window(Format.R32i), each object drawn with its number
    if (await picked.ids.Read<int>(pixel, int2(1, 1)) is Grid2<int> one) picked.id = one[0, 0];
}
```

`ReadDepth(from, size)` and a face's or a layer's reads take a part the same way.

### Which image, and as what

A target that holds several images reads any of them, by its number: `await target.Read(1)` reads
`target.colors[1]`. The number is a constant, as what a read gives depends on the image.

An image of numbers is read as its own type, written in `<>`, with the bits it stores:

| The image's format | The read | What it gives |
|---|---|---|
| `Rgba8`, `Rgba16f` | `Read()` | `Grid2<Color>`, alpha not multiplied in |
| `R8`, `R16f`, `R32f` | `Read<float>()` | `Grid2<float>` |
| `Rg8`, `Rg16f` | `Read<float2>()` | `Grid2<float2>` |
| `Rgba32f`, `Rgba16f` | `Read<float4>()` | `Grid2<float4>`, its four numbers as stored |
| `R32i` | `Read<int>()` | `Grid2<int>` |

```tide
local singleton Picking { RenderTarget scene; Grid2<int> ids; Grid2<float> depths; }

view Make(mut Picking picking)
{
    if (!picking.scene.valid) picking.scene = RenderTarget.Window(Format.Rgba8, Format.R32i);
}

async void ReadIds(mut Picking picking)
{
    if (await picking.scene.Read<int>(1) is Grid2<int> ids) picking.ids = ids.Copy();
    if (await picking.scene.ReadDepth() is Grid2<float> depths) picking.depths = depths.Copy();
}
```

- **Floats and ints come back bit for bit.** `R32f`, `Rgba32f` and `R32i` give what a frag or a clear wrote.
  `R16f` and `Rg16f` give the 16-bit float that was stored, and `R8` and `Rg8` the stored byte over 255.
- **The compiler checks the type against the target's format,** so the target has to be made one way wherever
  it's assigned, with its formats written there: `Read<float>()` of a `RenderTarget` parameter, or of a target
  made with `R32f` in one place and `Rg16f` in another, is an error that says so. A plain `Read()` of a target
  whose formats aren't known is checked as it runs, and fails where the image isn't a Color's.
- **`Read<float4>()` of an `Rgba16f` image** gives its four numbers as they're stored, where `Read()` gives
  them as a Color, dividing its alpha out again. Use it for an image a frag wrote a `float4` into.

### Depth

`await target.ReadDepth()` returns `Grid2<float> fails IOError`: each pixel's depth, 0 at the near plane and 1
at the far limit, as [`this.depth` and a depth's `Sample`](#a-target-s-depth) give it, whichever way the device
stores it. Every target has one, a target of depth alone too. A multisampled target gives one depth a pixel.
Where depth is stored as floats (reversed, as most devices store it), a depth a frag wrote comes back exactly when
it's 0.5 or more, or a multiple of 2^-24; otherwise to the nearest such step, about 6e-8 (0.1 comes back as
0.10000002). On a device with 24-bit depth it comes back to the nearest of that format's steps. Compare with a
tolerance, not `==`.

### Cube faces and array layers

A [cube's face](./meshes-and-textures.md) and an array's layer are targets, and are read the same way, one at
a time: `await cube.faces[2].Read()`, `await array.layers[1].Read()`, and `ReadDepth()` of either. Each is one
image, with its rows from the top as it was drawn, of the format its cube or array was made with: a face of a
`Cubemap(256, Format.R16f)` is read with `Read<float>()`, a layer of a `Format.Rg8` array with `Read<float2>()`,
an `R32i` face or layer with `Read<int>()`, by the table above. A face of a `Cubemap(size, Format.Depth)` has its
depth alone: `ReadDepth()`.

### A volume's cells

A `Texture3D` is read the same way, into a `Grid3` of its format's type: `await volume.Read()` for an `Rgba8` or
`Rgba16f` volume, `Read<float>()` for `R32f`, `Read<float4>()` for `Rgba32f`, `Read<int>()` for `R32i`, each cell as a
kernel's `volume[at]` reads it. A box of it is its first cell and how many along each axis, `int3`s:

```tide
local singleton Fluid { [Raw(float)] Texture3D pressure; Grid3<float> slice; }

async void Probe(mut Fluid fluid)
{
    if (await fluid.pressure.Read<float>(int3(0, 0, 8), int3(32, 32, 1)) is Grid3<float> slice) fluid.slice = slice.Copy();
}
```

### How many images

`target.colors.count` is how many color images a target holds: 0 for a target of depth alone and for one that
was never made or was disposed.

### The window

`Draw.window` can't be read: its image is the display's own, which the GPU doesn't hand back to a kernel, and
under Unity it's Unity's. Draw the frame into `RenderTarget.Window()`, read that, and
[`Draw.Blit`](./shaders.md) it to the window.
