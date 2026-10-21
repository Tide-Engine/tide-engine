---
description: "Draw triangles in Tide with Draw.Mesh, and texture them from an asset, a render target or a grid you paint: filters, wraps, mipmaps, one-channel textures and sampling said once."
---

# Meshes and textures

A mesh is triangles, and a texture is an image they're drawn with. This page has the 2D mesh and everything about textures; [3D meshes](./3d.md) are drawn with them the same way.

## Meshes

`Draw.Mesh(vertices, indices)` draws triangles, the shape every other one can be made of. `vertices` is a `List<Vertex>`, the corners, and `indices` a `List<int>`: every three are a triangle, each a corner's place in `vertices`, so triangles share corners.

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
    Draw.Mesh(corners, [0, 1, 2]);
}
```

Each pixel of a triangle is its corners' colors, blended across it, over what's behind it by its alpha. Triangles draw whichever way round their corners go (a 2D mesh has no back, where a [3D one](./3d.md#which-side-draws) does), in the order they're given, among everything else a view draws. An index past the end of `vertices` leaves its triangle out.

A mesh is one call however many triangles it has, and meshes drawn one after another go to the GPU together, so thousands of triangles a frame are cheap.

A mesh's corners can be a struct of your own instead, with whatever fields the mesh needs, and a `vert` and a `frag` can run for its vertices and its pixels on the GPU: see [Frags and verts](./shaders.md).

## Textures

A `Texture` is what you draw with. It comes from an asset, `asset Texture hero = "art/hero.png";` (see [Assets](./assets.md#images-to-draw-with)), from a render target, `target.color` (see [Targets](./render-targets.md)), or from a grid you paint: a `Grid2<Color>` or a `Grid2<float>` with a size (see [Grids](../language/grids.md)) is a `Texture` wherever one is wanted.

`Draw.Mesh` takes one after its indices. With no frag, each pixel is the triangle's color times the texture's at its `uv`: (0, 0) is the first pixel's outer corner, and (1, 1) the last pixel's outer corner.

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
    Draw.Mesh(corners, [0, 1, 2, 0, 2, 3], art.checker, Filter.Point);
}
```

Drag over the grid below to set its cells, and the next frame draws them. The toggle switches between `Filter.Point` and `Filter.Bilinear`.

::: tide-example textures A grid, drawn as a texture

<<< @/examples/textures.tide

:::

There's nothing to create, upload or free. The grid is ordinary state: change its cells whenever, with `[x, y]`, a loop or `Clear()`, and the next frame draws what it holds. The engine keeps a copy on the GPU while views draw with it, and sends only the cells that changed: a brush that paints a few cells of a big canvas costs those few, not the canvas.

The grid can be local state or the match's, like a canvas each player paints. Views read the match's grids as they are at the latest tick: cells aren't blended.

The argument after the texture says how it's read between pixels: `Filter.Bilinear` blends them, `Filter.Point` takes the nearest, for pixel art, and `Filter.Trilinear` blends them and stays smooth where the texture is drawn smaller than it is (see [Drawn smaller](#drawn-smaller)). A call that leaves it out reads the texture as the texture says (see [Saying it once](#saying-it-once-on-the-texture)), which is blended unless something said otherwise.

- A grid with an open axis has no size, so it's no texture: the mesh draws with its colors alone. Neither is one past 16384 cells a side.
- How big a texture can be is the GPU's limit: at least 2048 by 2048 wherever Tide runs.
- What a grid costs is game state, 16 bytes a cell of colors and 4 a cell of floats, with a copy on the GPU at a byte a channel (values are clamped to 0 to 1) or 16 bits a float. An asset's `Texture` keeps a byte per channel as its fallback, with [automatic GPU compression](./assets.md#gpu-compression) for eligible color images: for an image that's only drawn, a sprite or a tile sheet, declare it as one. A grid is for pixels code reads and writes.

A game's C can draw meshes too, with pixels of its own, like a font atlas a library made: see [Drawing from C](../c/functions.md#drawing-from-c).

### Sprites

`Draw.Sprite(texture, center, size, color)` draws a texture upright over a box in one call, where a mesh would take four corners and six indices. `center` and `size` are as `Draw.Rect`'s, and the texture's top left is at the box's top left: the top is towards the top of the screen, under a camera (y up) as in [the screen's pixels](./views.md#on-the-screen) (y down). Each pixel is the texture's times `color`, and a call can leave the color out for white.

```csharp
asset Texture hero = "art/hero.png";
asset Texture sheet = "art/run.png"; // Four frames side by side

component Runner
{
    float2 position;
    bool left;
    int frame; // 0 to 3
    float spin;
}

view Runners(Runner runner)
{
    float2 size = float2(runner.left ? -1 : 1, 1); // A negative width mirrors it
    Draw.Sprite(sheet, runner.position, size, Rect(runner.frame * 0.25, 0, 0.25, 1), Color.white);
    Draw.Sprite(hero, runner.position + float2(0, 2), float2(1, 1), runner.spin, Color.white);
}
```

- `Draw.Sprite(texture, center, size, part, color)` draws the part of the texture a `Rect` of uvs covers: (0, 0) is its top left corner and (1, 1) its bottom right, so a sheet's frame or an atlas's picture is one call.
- A `rotation` before the color, in radians from +x towards +y, turns it about its center: `Draw.Sprite(texture, center, size, rotation, color)`, or after the part.
- A negative width or height mirrors it.
- A filter and a wrap after everything else read it as they say, as a mesh's do; a call that leaves them out reads it as the texture says.
- Sprites one after another with the same texture, filter and wrap are drawn together, whatever their colors, as quads of the same texture drawn as meshes are.

### A Texture is a value

A `Texture` is a small value that names an image: a field or a local holds one, a function takes one and gives one back, and copying it copies no pixels. A grid goes wherever a `Texture` does, and is the texture it is there:

```csharp
local singleton Art
{
    Grid2<Color> canvas = Grid2(64, 64);
    RenderTarget scene;
    Texture shown;          // Whichever it's given
}

void Show(List<Vertex> corners, List<int> faces, Texture texture)
{
    Draw.Mesh(corners, faces, texture);
}

view Panel(mut Art art)
{
    art.shown = art.paused ? art.scene.color : art.canvas;   // A target's color, or the grid
    Show(art.corners, art.faces, art.shown);
    Show(art.corners, art.faces, hero);                      // An asset
}
```

- A texture made of a grid is that grid, not a copy: it draws what the grid holds when it's drawn. It lasts as long as the grid does. One kept past its grid (the entity the grid was a field of is gone, the match ended) draws nothing, or whatever took the grid's place: it's a name, as an `Entity` kept past its entity is.
- The default `Texture`, what a field starts as, is no texture: a mesh drawn with it draws its colors alone.
- It belongs to this machine: views, local state and the functions they call have them, and the match doesn't.
- `texture.size` gives its base-level width and height as an `int2` of physical pixels, in local CPU code and shaders. It reads metadata without copying or uploading pixels. Assets and grids report their image dimensions; render-target aliases follow resize, display scale and the device's size limit. Missing, unloaded or disposed images give `int2(0)`. No code reads a `Texture`'s pixels; a grid's cells are read as the grid.

For example, a shader can use `float2 texel = 1.0 / float2(image.size);` for pixel-sized UV offsets. A CPU helper can return `image.size` through ordinary Texture parameters. Custom C hosts provide `draw.target_pixels = tide_platform_target_pixels` before `tide_frame` to resolve target dimensions; the standard host does this automatically. Without that frame metadata, target sizes are zero.

For an explicit `parallel` loop, read the size before the loop and use that `int2` in each step. CPU metadata queries use the frame thread; an ordinary grid `foreach` that reads them stays ordered.

### One channel

A `Grid2<float>` is a texture of one channel, for what isn't a picture: a heightmap, a lookup table, a field of distances. A frag reads it like any texture, and gets the cell's number as the color's `r`, with `g` and `b` 0 and `a` 1:

```csharp
local singleton Land
{
    Grid2<float> heights = Grid2(256, 256);
}

frag Slope(Corner corner, out Color color, Land land)
{
    float height = land.heights.Sample(corner.uv).r;
    float snow = Math.Saturate(height / 40);
    color = Color(snow, 0.6 + 0.4 * snow, snow);   // Green in the valleys, white at 40 and up
}
```

- The numbers are the grid's own, not clamped to 0 to 1: a cell that's 40 reads as 40, and one that's -3 as -3.
- The GPU's copy is a float of 16 bits a cell, which every device Tide draws on can blend between cells. That's about three digits, up to 65504 either way: a bigger number reads as that, and a cell that's NaN as 0. The grid itself keeps its 32 bits for the code that reads it.
- Drawn with no frag, it's red: the vertex's color times `(r, 0, 0, 1)`. It's a frag that makes something of the number.

### Past the edges

A texture's own pixels are where `uv` goes from 0 to 1. What a `uv` past those reads is its wrap, which a draw says after the filter:

```csharp
Draw.Mesh(corners, [0, 1, 2, 0, 2, 3], art.tile, Filter.Point, Wrap.Repeat);
```

- `Wrap.Clamp` reads the pixels at the texture's edge. It's what a texture that says nothing has.
- `Wrap.Repeat` reads the texture again: 1.25 reads what 0.25 does. It's for tiles, and for what scrolls.
- `Wrap.Mirror` reads the texture again, the other way round each time: 1.25 reads what 0.75 does, so no seam shows where its edges don't match.

The quad below has uvs that go three times across its texture and three times down, and the white square is where they go from 0 to 1. Scroll it, and switch between the three.

::: tide-example wrap A texture, past its edges

<<< @/examples/wrap.tide

:::

- It's one wrap, for both ways across the texture, and it's written after the filter: a draw that says how it wraps says its filter too. A texture that's always wrapped one way says so where it's declared instead (below).
- It's a value like the filter: a field, or `tiled ? Wrap.Repeat : Wrap.Clamp`.
- It's how this draw reads the texture, and changes nothing of the image: one grid can be clamped in one draw and repeated in the next. So can a [render target's color](./render-targets.md), and a texture of any size.
- Blended, a cell at the edge blends with what the wrap says is past it: with `Wrap.Repeat` that's the cell at the opposite edge, which is what makes tiles meet without a seam.
- `Wrap` is the engine's name: a game's own type or function takes another one, or a namespace.

### Drawn smaller

A texture drawn smaller than it is has several of its pixels under each pixel of the screen. `Filter.Bilinear` reads the ones under the pixel's middle, and skips the rest: a tiled floor seen from far away shimmers as the camera moves, and a big image shown as a thumbnail looks grainy. `Filter.Trilinear` reads all of them:

```csharp
Draw.Mesh(floor, faces, float4x4.identity, tiles, Filter.Trilinear, Wrap.Repeat);
```

- The engine keeps smaller copies of the texture for it, each half the size of the last, down to one pixel (its mipmaps), and the GPU blends between the two nearest the size the texture is drawn at. So a pixel is the mean of what it covers.
- Drawn at its own size or bigger, it's the same as `Filter.Bilinear`.
- **It's a filter like the others**: a draw's argument, a `Sample`'s in a [frag](./shaders.md#textures), a declaration's `[Filter(Filter.Trilinear)]`, and what a `Texture` carries from there (below).
- **Nothing to make or keep up to date.** The copies are made the first time a draw reads the texture this way, and again when its pixels changed: a grid whose cells were written, a [render target](./render-targets.md) that was drawn into. A texture no draw reads this way has none.
- **What it costs**: for uncompressed textures, a third more memory on the GPU and making the copies, which is quick for an image that never changes and adds up for a big grid that changes every frame. Automatically compressed assets already include their smaller images, prepared during the build; selecting Trilinear adds no upload or generation work on a GPU that supports their format.
- It's for what's seen at a distance or at many sizes: floors and walls in 3D, a map you zoom out of, a target shown as a thumbnail. Pixel art at its own size wants `Filter.Point`.
- A `Grid2<float>` has them wherever the device can draw into floats, which is nearly everywhere (see [float images](./render-targets.md#what-a-target-holds)); where it can't, it's read as `Filter.Bilinear`.

### Saying it once, on the texture

How a texture is sampled is said in two places: on the texture, where it's made, and on the draw, for that draw.

A texture that's always read one way says so where it's declared, with `[Filter(...)]` and `[Wrap(...)]`, each alone or both:

```csharp
[Filter(Filter.Point), Wrap(Wrap.Repeat)]
asset Texture tiles = "art/tiles.png";

[Filter(Filter.Point)]
asset List<Texture> frames = "art/run/*.png";   // Every one of them

local singleton Art
{
    [Filter(Filter.Point)]
    Grid2<Color> canvas = Grid2(64, 64);
    [Filter(Filter.Point)]
    RenderTarget small;                          // Its color images
    Texture sprite;
}

view Floor(mut Art art)
{
    Draw.Mesh(corners, faces, tiles);                               // Nearest, repeated: as tiles says
    Draw.Mesh(corners, faces, tiles, Filter.Bilinear);              // Blended here, still repeated
    Draw.Mesh(corners, faces, tiles, Filter.Bilinear, Wrap.Clamp);  // Both the draw's
    Draw.Mesh(corners, faces, frames[2]);                           // Nearest: as frames says
    Draw.Mesh(corners, faces, art.canvas);                          // Nearest: as the field says
    Draw.Mesh(corners, faces, art.small.color);                     // Nearest: as the target's field says

    art.sprite = tiles;
    Draw.Mesh(corners, faces, art.sprite);                          // Nearest, repeated: it's tiles
}
```

- They go before an asset that's a `Texture` or a `Grid2<Color>` (one image, or a list or a map of them), and before a field that's a `Grid2<Color>`, a `Grid2<float>` or a `RenderTarget`.
- **A `Texture` carries it.** The texture made of that asset, that grid or that target is sampled as the declaration says wherever it goes: in a field, a local, a function's parameter. `art.sprite = tiles` draws as `tiles` does. So a `Texture` field takes neither attribute: it holds whichever texture it's given, which says it itself.
- A texture with nothing said is blended and clamped.
- What a draw says is for that draw, in the place of the texture's own, each of the two apart. In a frag, a `Sample` that says its filter or its wrap has what it says, and one that doesn't reads the texture as the texture says (see [Sampling a texture](./shaders.md#textures)).

### Six of them, as a cube

A `Cubemap` is six images that are read by a direction where a `Texture` is read by a `uv`: a sky, or what a mirror sees. It isn't a mesh's texture; a frag reads it, `sky.Sample(direction)`. An asset's is six files, and `Cubemap(size)` is one whose six faces are render targets, `cube.faces[0]` to `cube.faces[5]`. See [Skybox and reflections](./shaders.md#skybox-and-reflections).

`cube.Resize(size)` changes all six faces. Hot reload keeps their current size, including after a resize, and copies of the cube still share the same images. Their rendered contents are cleared on the web when the images are recreated.
