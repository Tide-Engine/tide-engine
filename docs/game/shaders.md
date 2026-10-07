# Frags and verts

A mesh's pixels and vertices can run code of yours on the GPU, written in Tide: a `frag` runs once for each pixel a mesh covers, and a `vert` once for each of its vertices. They're declarations, like [views](./views.md), and a `Draw.Mesh` or `Draw.Mesh` names them:

```csharp
struct BoxVertex
{
    float3 position;
    float3 normal;
    float2 uv;
}

// Once per vertex: changes it in place
vert Wave(mut BoxVertex vertex, Time time)
{
    vertex.position.y += Math.Sin(time.t * 2 + vertex.position.x) * 0.1;
}

// Once per pixel: the triangle's three vertices blended at that pixel, and the pixel's color, which it writes
frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, Models models)
{
    float light = Math.Max(Math.Dot(Math.Normalize(vertex.normal), -sun.direction), 0.1);
    color = models.wood.Sample(vertex.uv) * box.tint * light;
    color.a = 1; // The light dims the color, not how solid it is
}

view DrawBox(Box box, Models models)
{
    Draw.Mesh(models.crate, models.faces, float4x4.Translate(box.pos), Wave, Shade);
}
```

Their parameters say what they read, and the engine supplies it. There's nothing to bind, upload or declare twice, and no shader language to learn: tidec writes the shaders, for every GPU Tide draws with, and they always compile.

Change the wave height to move the mesh's vertices, and the stripe count to change what each pixel computes. Both shaders read the same local singleton the sliders edit. Turn off animation to inspect a frame.

::: tide-example shaders A frag and a vert

<<< @/examples/shaders.tide

:::

## Your own vertex

A mesh's vertices are a list of a struct, and the struct can be yours:

- It has a `position`: a `float2` makes a 2D mesh, drawn through the 2D camera in order with the other Draw calls, and a `float3` a 3D one, which `Draw.Mesh` places with a transform.
- Its other fields are whatever the mesh needs: floats, ints, their vectors and colors, 12 fields at most. The GPU reads them from the list as they are, so a vertex costs what its struct does: a struct with only a `position` is 12 bytes a vertex.

`Draw.Mesh` and `Draw.Mesh` take a list of it, with or without a vert and a frag:

```csharp
Draw.Mesh(vertices, indices);                           // 2D
Draw.Mesh(vertices, indices, transform);                // 3D
Draw.Mesh(vertices, indices, transform, Shade);         // A frag
Draw.Mesh(vertices, indices, transform, Wave);          // A vert
Draw.Mesh(vertices, indices, transform, Wave, Shade);   // Both: the vert first
```

With no frag, a pixel is its vertex's `color` times the draw's texture at its `uv`, as the built-in `Vertex` and `Vertex3` draw: a struct with no `Color color` draws white, and a texture needs a `float2 uv` to be read at. With no vert, the vertices are used as they are.

Frags and verts can share a name when their parameters differ. A draw then names the one it means with its parameters' types, as [`[After]` names a system](./systems.md#order): `Draw.Mesh(vertices, indices, Tint(Vertex, Color, Sun))`.

## A frag

```csharp
frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun)
{
    color = box.tint * Math.Max(Math.Dot(vertex.normal, -sun.direction), 0.1);
    color.a = box.tint.a;
}
```

- **The vertex comes first.** It's the struct the mesh's list holds, and the frag gets it blended across the triangle: at each pixel, a mix of the triangle's three vertices by how near each is. It can't change it. Its ints don't blend, so a frag can't read them.
- **`out Color color` is the pixel.** A frag writes its output through an [`out` parameter](../language/functions.md#out-parameters), and returns nothing. What it wrote there when it ends is the pixel's color, blended over what's behind it by its alpha.
- **It starts with no value, so the frag writes it.** On every path that ends, and before reading it: `color.a = 1` alone, with nothing written first, is an error, and so is a frag whose only write is inside an `if` with no `else`. It isn't the pixel already on the screen either: a GPU doesn't let a frag read that. A path that ends in [`pixel.Discard()`](#where-the-pixel-is) draws no pixel, and owes it nothing.
- **The draw's [settings](./views.md#draw-settings) apply to it.** Inside `Draw.Blend(Blend.Add) { ... }` the pixel is added instead, and inside `Draw.Tint(...)` it's multiplied by the tint after the frag ran, so a faded menu fades its shaded meshes too, with nothing for the frag to do. A 2D mesh's vertices go through `Draw.Transform` after its vert, and a 3D one's transform goes through `Draw.Transform`. A 3D mesh does with depth what [`Draw.Depth`](./views.md#depth) says: inside `Draw.Depth(Depth.Read) { ... }` it's hidden behind what's nearer and hides nothing itself, which is how something see-through is drawn. And its triangles are drawn from the side [`Draw.Cull`](./views.md#which-side-draws) says, their front unless it says otherwise: a vert that mirrors its mesh, or turns it inside out, has them reach the screen the other way round, and is drawn inside `Draw.Cull(Cull.Front)`. Under [`Draw.Stencil`](./views.md#stencil) it's drawn where its pixels pass the test, and it stores where it's drawn: a pixel the frag leaves out with `pixel.Discard()` stores nothing, which is how a mask gets the shape of a picture.
- **Light the color, not its alpha.** A color times a number scales its alpha too, so `box.tint * light` alone leaves a pixel in the shade see-through, and what was drawn behind the mesh shows in it, its own far side too. The frag above sets `color.a` back.
- A frag can write more than one color, each an `out Color` of its own: see [Several outputs](#several-outputs). An output of another type than `Color` isn't there yet.

::: tip It used to be mut
A frag's color used to be `mut Color color`, which started white. That spelling is an error now, and the editor offers the change to `out`. A frag that leaned on the white writes it itself, `color = Color.white;`, which the editor offers too where a path is missing it.
:::

### Several outputs

A frag writes up to four colors at once, each an `out Color` parameter, into a [render target made to hold as many](./views.md#several-colors). They go to the target's color images in the order they're written: the first is the pixel's color, as ever, and each one after it goes to the next image.

```csharp
// One pass over the scene leaves what it looks like, which way it faces and where it is
frag Fill(Corner corner, out Color albedo, out Color normal, out Color place, Pixel pixel)
{
    albedo = corner.color;
    normal = Color(corner.normal.x, corner.normal.y, corner.normal.z);
    place = Color(pixel.world.x, pixel.world.y, pixel.world.z);
}

// ...and a second one, over the whole screen, lights it from what the first left: its
// first image is the Blit's source, and the others are Texture fields of a singleton
frag Lit(Vertex vertex, out Color color, Texture source, Deferred deferred, Lamp lamp)
{
    Color albedo = source.Sample(vertex.uv, Filter.Point);
    Color normal = deferred.normals.Sample(vertex.uv, Filter.Point);
    Color place = deferred.places.Sample(vertex.uv, Filter.Point);
    float3 toward = lamp.position - float3(place.r, place.g, place.b);
    float facing = Math.Max(Math.Dot(float3(normal.r, normal.g, normal.b), Math.Normalize(toward)), 0);
    color = Color(albedo.r * facing, albedo.g * facing, albedo.b * facing);
}

view Show(Models models, Deferred deferred)
{
    deferred.scene.Clear(Color.black);
    deferred.scene.Mesh(models.room, models.faces, float4x4.identity, Fill);
    Draw.Blit(deferred.scene.color, Draw.window, Lit);
}
```

- **Each is owed on every path,** as the pixel's color is: a frag that can end without writing its second output is an error that names it, and `pixel.Discard()` owes none of them anything.
- **They're matched by their order,** not their names: `out Color albedo, out Color normal` writes a target's first image and its second, whatever the frag calls them, and wherever they are among its other parameters.
- **A `Color` is four floats,** and an output holds any number. What's kept of it is the image's to say: a [`Format.Rgba8`](./views.md#what-a-target-holds) image keeps each channel from 0 to 1, and a `Format.Rgba16f` one keeps it as it is, which is what a normal, a place or light brighter than white needs.
- **`Color(x, y, z)` has an alpha of 1.** Each output is blended over what's in its image by its own alpha, as the draw's blend says, so an output that's data is written with an alpha of 1, or inside `Draw.Blend(Blend.Replace) { ... }`.
- **Where there's no image for it, an output goes nowhere.** Drawn into the window, or into a target of one color image, such a frag is its first output alone; into a target of depth alone, none of them. So one frag serves a pass that fills three images and a pass that only casts a shadow.
- **A target's other images are left alone** by a frag with fewer outputs than the target has images, and by everything the engine draws itself, which has one color.
- A frag reads the images another draw wrote as it reads any texture, through a `Texture` field of a singleton or a component it takes: `deferred.normals = deferred.scene.colors[1];` in a view, then `deferred.normals.Sample(uv)` in the frag. It can't read an image of the target it's drawn into.

The scene below is drawn once, by a frag that leaves three images: each pixel's paint, which way it faces and where it is. A second frag, over the whole window, lights every pixel from those three, with a lamp that follows the pointer, so moving the lamp draws nothing of the scene again. The slider raises the lamp, and the buttons show each image as the first pass left it.

::: tide-example outputs Three images from one frag

<<< @/examples/outputs.tide

:::

## A vert

```csharp
vert Wave(mut BoxVertex vertex, Time time)
{
    vertex.position.y += Math.Sin(time.t * 2 + vertex.position.x) * 0.1;
}
```

A vert takes its vertex `mut` and changes it in place: where it is, and anything else the pixels read of it. The engine then places it with the draw's transform and the camera, and the frag gets the changed vertex, blended. The mesh's list isn't touched: the GPU changes its own copy, each frame.

### From a vert to its frag

What a vert works out for the pixels needn't be a field of every vertex in the mesh. A vert writes a struct of its own, an `out` parameter, and the frag it's drawn with reads it, blended across the triangle as its vertex is:

```csharp
struct FromLamp
{
    float3 toward; // From the corner to the lamp
    float reach;   // How much of the lamp's light gets there
}

vert Place(mut Corner corner, out FromLamp lamp, Lamp light, Instance instance)
{
    float3 place = Math.Transform(instance.transform, corner.position);
    lamp.toward = light.position - place;
    lamp.reach = Math.Saturate(1 - Math.Length(lamp.toward) / light.range);
}

frag Look(Corner corner, FromLamp lamp, out Color color)
{
    float facing = Math.Max(Math.Dot(Math.Normalize(corner.normal), Math.Normalize(lamp.toward)), 0);
    color = Color.white * (0.1 + facing * lamp.reach);
    color.a = 1;
}

view DrawRoom(Models models)
{
    Draw.Mesh(models.room, models.faces, float4x4.identity, Place, Look);
}
```

- **The draw pairs them.** A vert and a frag stay declarations of their own, and one vert can be drawn with several frags. A frag's struct is found by its type among what the draw's vert writes: a frag that reads a `FromLamp` in a draw whose vert writes none, or that names no vert, is an error at the draw.
- **The vert writes all of it.** The struct starts with no value: each field is written on every path, a field at a time or all at once (`lamp = FromLamp { ... };`), before it's read.
- **Its fields are what blends**: floats, their vectors and colors. Ints don't blend across a triangle, so a struct with one can't be handed on: keep it as a float.
- **Only what the frag reads is carried.** A field no frag reads costs nothing between the two, and a vert drawn with no frag, or with one that takes none of its structs, just doesn't hand them on. A GPU carries 15 values from a mesh's corners to its pixels: each field read of the vertex or of such a struct is one.
- A vert can write several structs, and a frag take any of them, each by its type. A frag can also take what its vert wrote beside an [instance's struct](#instances-from-a-list), as long as the two are different types.

## What they read

After the vertex, and what each writes (a frag's color, the structs a vert hands its frag), a frag or vert takes the game's state, as a view does:

- **Components** are those of the entity the view that draws runs for. `box.tint` in the frag above is the tint of the box being drawn: a view that runs for a thousand boxes draws them all at once, each with its own.
- **Singletons**, the match's or [local](./local-state.md) ones, and `Time`. `time.t`, the match's seconds, is blended between ticks like every float a view sees, so what moves by it moves every frame.
- None is `mut` or `out`: the GPU reads the game's state, and changes none of it.
- A [`local` field](./local-state.md) of the match's component or singleton is read like any other: `box.highlight`, which a view set for the entity under the mouse.
- With [`Draw.Instanced`](#instances-from-a-list), an ordinary struct parameter takes the current item of the instance list. It is read-only too. (A frag's struct of a type its draw's vert writes is [the vert's](#from-a-vert-to-its-frag) instead.)
- An ordinary struct parameter also takes [the values a draw gives](#values-for-one-draw) after its vert and frag: `Draw.Blit(source, destination, Blur, BlurStep { along = float2(1, 0) })`.
- A read-only `Texture` parameter receives the explicit image passed to either namespace's `Mesh` or `Instanced` or [`Draw.Blit`](./views.md#targets-and-post-processing). Each stage takes at most one; when both stages take it, they receive the same source. `source.Sample(vertex.uv[, Filter.Point[, Wrap.Repeat]])` uses the same top-left UVs as grid sampling. Local component and singleton `Texture` fields can be sampled too.

The view needn't take them itself. It has to run for entities that have the components, so `with Box` is enough, and it's an error at the mesh draw when it doesn't:

```csharp
view DrawBoxes(with Box, Models models)
{
    Draw.Mesh(models.crate, models.faces, float4x4.identity, Shade); // Shade reads Box and Sun
}
```

What a frag sees of the match is what a view sees: [blended between ticks](./views.md#smooth-at-any-tick-rate), so it's smooth at any tick rate.

A frag takes no loose numbers. What it needs lives in a component, a singleton, the instance struct supplied to `Draw.Instanced`, or [a struct the draw gives it](#values-for-one-draw). A local singleton is the place for what only this machine's drawing cares about, and every draw of the frame shares:

```csharp
local singleton Look
{
    float fog = 0.02;
    Color sky = Color(0.5, 0.7, 1);
}

frag Fog(BoxVertex vertex, out Color color, Look look, Pixel pixel)
{
    float clear = Math.Exp(-pixel.depth * look.fog * 1000);
    color = Color.white * clear + look.sky * (1 - clear);
}
```

Only the fields the code reads go to the GPU: a component's with each entity drawn, and a singleton's once for the draw. A big component costs nothing for the fields a frag doesn't name.

## Values for one draw

A singleton is the same for every draw of a frame. What differs from one draw to the next, with the same frag, is a struct the draw gives after the vert and the frag it names. The frag, the vert or both take it as a read-only parameter of that struct's type:

```csharp
struct BlurStep
{
    float2 along; // From one sample of the blur to the next
}

frag Blur(Vertex vertex, out Color color, Texture source, BlurStep step)
{
    var near = step.along * 1.3846;
    var far = step.along * 3.2308;
    var sum = source.Sample(vertex.uv) * 0.227
            + (source.Sample(vertex.uv + near) + source.Sample(vertex.uv - near)) * 0.3162
            + (source.Sample(vertex.uv + far) + source.Sample(vertex.uv - far)) * 0.0703;
    color = Color(sum.r, sum.g, sum.b, 1);
}

view PaintLight(Lens lens)
{
    // Across, then down: one frag, and each Blit says its own direction
    Draw.Blit(lens.bright.color, lens.spread, Blur, BlurStep { along = float2(lens.pixel.x * 4, 0) });
    Draw.Blit(lens.spread.color, lens.bright, Blur, BlurStep { along = float2(0, lens.pixel.y * 4) });
}
```

That's [Duel](../guide/duel.md)'s blur. Without it, the direction would be a field of a singleton that the view writes between the two Blits.

- It goes last, after the vert and the frag, in `Draw.Mesh`, `Draw.Instanced`, [`Draw.Blit`](./views.md#targets-and-post-processing) and a target's `Mesh` and `Instanced`: `Draw.Mesh(vertices, indices, transform, Wave, Shade, Swell { height = 2 })`. Any expression of the struct's type will do: a literal, a local, a field, a function's result.
- It's found by its type, as a component is, whatever the parameter is called. A draw gives one struct. Group what the vert and the frag each need in it: each reads the fields it wants.
- It's sent once for the draw, with what the code reads of singletons, and only the fields the vert and the frag read. A field neither reads costs nothing, and needn't be a type a GPU has.
- Draws in a row with the same mesh, the same vert and frag and the same values are still one draw call. Other values start another.
- With [`Draw.Instanced`](#instances-from-a-list), a frag can take both: the list's struct, which each instance has its own of, and the draw's, which they share. The two have to be different types, and neither can be the type the draw's vert [writes for its frag](#from-a-vert-to-its-frag).
- It holds no texture to sample. A draw's image is its `Texture` argument, or a field of a component or singleton.

It's an error at the draw when its frag or vert takes a struct that nothing supplies (not the draw's vert, its instances or its values), and when the draw gives a struct neither takes.

## Textures

A grid of colors in what a frag takes is a texture (see [Textures](./views.md#textures)), and `Sample` reads it:

```csharp
color = models.wood.Sample(vertex.uv);                             // Blended between its cells
color = models.wood.Sample(vertex.uv, Filter.Point);               // The nearest cell
color = models.wood.Sample(vertex.uv * 4, Filter.Point, Wrap.Repeat); // Four times across, and four down
```

`uv` is as a mesh's: (0, 0) is the outer corner of the grid's cell (0, 0) and (1, 1) the outer corner of its last. The filter is written there, `Filter.Point` or `Filter.Bilinear`. Verts sample too.

Past 0 and 1, `uv` reads what the [wrap](./views.md#past-the-edges) after the filter says: the cells at the edge without one (`Wrap.Clamp`), the grid again (`Wrap.Repeat`), or again the other way round each time (`Wrap.Mirror`). A frag is where a wrap earns its keep, since it works its `uv` out: `vertex.uv * 4` tiles a floor, and `vertex.uv + float2(time.t * 0.1, 0)` scrolls one.

- The filter and the wrap are written in the call, `Filter.Point` and `Wrap.Repeat`, never read from a variable: the GPU is told how a texture is read before any pixel runs.
- A wrap comes after a filter, so a `Sample` that says how it wraps says both. The names work too: `Sample(uv: vertex.uv, filter: Filter.Bilinear, wrap: Wrap.Repeat)`.
- What a `Sample` leaves out is the texture's own to say, where its declaration does ([`[Filter]` and `[Wrap]`](./views.md#saying-it-once-on-the-texture)): `models.wood.Sample(uv)` is read as the field `wood` says, and `tiles.Sample(uv)` as the asset `tiles` says. A direct `Texture` parameter is read as its draw's source says, so `Draw.Mesh(corners, faces, tiles, Shade)` gives `source.Sample(uv)` the sampling `tiles` declared, and a `Texture` that's a value (a field's, a target's color) the usual: blended, the cells at the edge.
- An image file of the game's is sampled by its name, with no parameter for it: `asset Texture tiles = "art/tiles.png";` and then `tiles.Sample(vertex.uv)` (see [Assets](./assets.md#images-to-draw-with)).
- One grid can be read several ways in one frag, clamped here and repeated there. Each way counts as one of the 8 textures a draw samples, and the same way twice counts once.
- A draw through a frag takes no filter and no wrap of its own: `Draw.Mesh(corners, faces, image, Filter.Point, Shade)` is an error that says to write them at `Sample`.

## A target's depth

A [render target](./views.md#what-a-target-holds) keeps how far the nearest thing drawn at each of its pixels is, and a frag reads that as it reads a color: `target.depth`, a `DepthTexture`, which a view keeps in a field of a singleton or a component the frag takes.

```csharp
local singleton Lamp { RenderTarget map; DepthTexture shadow; float4x4 toLamp; }

view Make(mut Lamp lamp)
{
    lamp.map = RenderTarget(1024, 1024, 1, Format.Depth);
    lamp.shadow = lamp.map.depth;
}
```

A frag reads it two ways:

```csharp
float far = lamp.shadow.Sample(uv);           // The depth stored there
float lit = lamp.shadow.Compare(uv, depth);   // How much of what's stored there isn't nearer than `depth`
```

- **`Sample(uv)`** gives the depth as a number: 0 at the camera's near plane, 1 at its far limit, as [`pixel.depth`](#where-the-pixel-is) is, and 1 where nothing was drawn since the target was cleared. It's the nearest pixel's, with no filter to pick: a depth blended between two pixels would be neither's.
- **`Compare(uv, value)`** compares what's stored with a number of yours, and gives a number from 0 to 1: 1 where what's stored isn't nearer than `value`, 0 where it is, and in between along an edge, as the GPU compares the pixels around `uv` and blends what that gives. `value` is kept between 0 and 1, so what's past the far limit counts as at it.

`uv` is a color's: (0, 0) is the target's top left corner. Past the target's edges, both read the pixels at the edge: a depth takes no `Wrap`. Verts read a depth too. Each way a frag reads one is one of its draw's [8 textures](#what-the-gpu-runs).

### A shadow map

That second read is what a shadow is. Draw what casts into a target of depth alone, through a camera where the lamp is, and that target holds how far the lamp sees along each of its pixels. A frag that lights a pixel then asks whether the lamp sees that far: it works out where its pixel is in the lamp's image, and compares how far the pixel is from the lamp with what's stored there.

The scene below does it. **Lamp** turns the lamp, **Soft edges** takes four comparisons around the pixel where one would do, and **The lamp's depth** shows the lamp's own image in the corner: near is dark, and what nothing was drawn on is white.

::: tide-example shadows A shadow map

<<< @/examples/shadows.tide

:::

- **What casts is drawn with no frag,** and has the engine's own look, as anywhere: in a target of depth alone, a color has nowhere to go, and how far each pixel is, is what's kept. A caster with holes in it names a frag that calls `pixel.Discard()` for them.
- **The lamp's camera is an ordinary one:** `target.SetCamera(transform, projection)`, with `float4x4.Ortho` for a lamp that's far away, like a sun, and `float4x4.PerspectiveFov` for one that's near.
- **Where a pixel is in the lamp's image is yours to work out,** from the matrix that takes the world to the lamp's clip space. A camera looks along its `+z`, and clip space along `-z`, so that matrix is the projection, after z turned around, after the camera's transform undone:

  ```csharp
  float4x4 flip = float4x4.TRS(float3(0), quaternion.identity, float3(1, 1, -1));
  lamp.toLamp = Math.Mul(projection, Math.Mul(flip, Math.Inverse(transform)));
  ```

  ```csharp
  float4 seen = Math.Mul(lamp.toLamp, float4(pixel.world, 1));
  float2 uv = float2(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5);   // From the image's top left corner
  float depth = seen.z * 0.5 + 0.5;                             // From 0 to 1, as what's stored is
  ```

  Under a perspective, divide `seen.xyz` by `seen.w` first.

- **Compare a little nearer than the pixel is,** `depth - 0.003`: a surface the lamp sees is exactly as far as what's stored there, to the rounding, and would be in its own shadow in stripes. How much it takes goes with how slanted the surface is to the lamp and how few pixels the lamp's image has across what it sees.
- **A target with pixels of its own** (`RenderTarget(1024, 1024, 1, ...)`) casts the same shadows on every display. The fewer pixels it has across what the lamp sees, the blockier a shadow's edge.
- **The target that's read isn't the one drawn into.** A draw into a target that reads that target's depth is passed by, with a line that says so.

## Where the pixel is

Two more parameters are the engine's:

```csharp
frag Lit(BoxVertex vertex, out Color color, Pixel pixel, Instance instance, Sun sun)
{
    float3 normal = Math.Normalize(Math.Rotate(instance.transform, vertex.normal));
    float3 toLight = Math.Normalize(sun.position - pixel.world);
    color = Color.white * Math.Max(Math.Dot(normal, toLight), 0.1);
    color.a = 1;
}
```

The sphere below has a vertex of its own, with a normal, and a frag that lights each pixel from it: the normal turned into the world by `instance.transform`, against a light the slider moves. **Bands** rounds the light to steps, in the same frag.

::: tide-example lighting Lighting, in a frag

<<< @/examples/lighting.tide

:::

- `Instance instance`, in a frag or a vert: `instance.transform` is the `float4x4` the draw was given, so `Math.Rotate(instance.transform, vertex.normal)` is a normal in the world.
- `Pixel pixel`, in a frag:
  - `pixel.world`, a `float3`: where on the mesh the pixel is, in the world.
  - `pixel.position`, a `float2`: where it is on the screen, in pixels from the top left, as [`Draw.Pixels`](./views.md#on-the-screen)'s.
  - `pixel.depth`: how far it is, from 0 at the camera's near side to 1.
  - `pixel.Discard()` leaves the pixel undrawn, and ends the frag there: for holes, and edges cut out of a texture. Nothing is written for it: no color, no depth and no [stencil](./views.md#stencil).

```csharp
frag Leaves(BoxVertex vertex, out Color color, Models models, Pixel pixel)
{
    color = models.leaves.Sample(vertex.uv);
    if (color.a < 0.5) pixel.Discard();
}
```

The camera isn't given: keep where yours is in a singleton, where the view that calls `Draw.SetCamera` reads it too, and take that.

## What the GPU runs

Code for the GPU is ordinary Tide, less what a GPU doesn't have:

- **It has** bools, ints, floats, vectors, quaternions, matrices, colors, enums and structs of those; locals, `if`, `?:`, and `for` loops that count (`for (int i = 0; i < 8; i++)`), with `break` and `continue`; your functions, methods and operators; all of [`Math`](#math), and what builds its types; and sampling a grid, a texture or a target's depth, 8 of them to a draw.
- **It doesn't have** lists, maps, text, entities, `this`, spawning and sending, `Draw`, the GUI, `Devices`, `Debug`, C, tasks, errors, or recursion. `while` and `switch` aren't there yet: write `for` with a bound, and `if`.

Each of those is an error where it's written, which says what to do instead. A function a frag calls has to fit too, and the error says which frag reaches it; the same function still runs on the CPU for the code that calls it there.

```
error: a loop the GPU runs counts, so it's known how far it goes: 'while' could go on forever
  = note: frag 'Shade' runs on the GPU, and 'Falloff' with it, as it calls it
  = note: write it as a for with a bound, and 'break' out of it: 'for (int i = 0; i < 8; i++) { ... }'
```

### Math

Everything in [`Math`](../language/math.md) is there unless it makes no sense on a GPU, with the types it works on and what builds them: vectors, quaternions (`Math.Mul`, `Rotate`, `Slerp`, `quaternion.AxisAngle`, `Euler`, `LookRotation`), matrices (`Math.Inverse`, `Determinant`, `float4x4.TRS`, `PerspectiveFov`) and `Math.Hash`. A function gives the same answer in a system, in a view and in a frag, up to how the GPU rounds.

- **`Math.Hash` is exact.** It's integers, so a frag gets the number a system gets: a pattern made from a cell's hash is the same on every GPU.

```csharp
frag Stars(BoxVertex vertex, out Color color)
{
    // About one cell in thirty has a star, the same cells on every machine
    int2 cell = int2(Math.Floor(vertex.uv * 64));
    color = Math.Hash(cell) % 30 == 0 ? Color.white : Color.black;
}
```

- **Floats come out as the GPU rounds them.** `Sin`, `Pow`, a quaternion's `Slerp` and a matrix's `Inverse` follow the same formulas as on the CPU, and each GPU rounds them its own way. Big numbers lose the most: the sine of a number in the thousands is wherever that GPU's rounding puts it, so the old trick of a hash made from a sine comes out striped on some machines. `Math.Hash` is what that's for.
- **It's as forgiving as on the CPU** (see [Forgiving with bad values](../language/math.md#forgiving-with-bad-values)): `Normalize` gives zero for a vector with no direction and the identity for a quaternion with none, `Min` and `Max` with one NaN give the other, and `Clamp` and `Saturate` always give a value in range.
- **It gives a number wherever the CPU does.** A GPU's own functions leave some answers open, and Tide's don't: a whole power of a number under 0 (`Math.Pow(-2, 2)` is 4), anything to the power of 0 (1), `Math.Atan2(0, 0)` (0), and `Math.SmoothStep` from an edge down to a lower one (it falls where the other way around rises). Where the CPU has no number to give either (the square root of a number under 0, the inverse of a matrix that has none), the GPU gives whatever it gives.

What's left out is left out for a reason, and its error says what to write instead. There's one:

| Not on the GPU | Why | Instead |
|---|---|---|
| `Math.Hash` of a `long` | A GPU's ints are 32 bits, so no `long` gets there | `Math.Hash(int2(low, high))` is the same number, for the long's halves: `int(x)` and `int(x >> 32)`, split on the CPU and kept in fields the frag reads |

A [`Random`](../language/math.md#not-on-the-gpu) isn't there either: its state is the CPU's, so a frag reads a number the view rolled, or hashes.

::: warning The GPU's numbers aren't the match's
Every machine's GPU rounds a little differently, so what a frag or a vert works out never reaches the match: they're local code, as views are, and what `Math` makes of floats there is that GPU's own. Only its integers, `Math.Hash`, are the same everywhere. Anything the match depends on is worked out in a system.
:::

## Many at once

Instances of one mesh drawn in a row through the same vert and frag are one draw, each with its own transform and its own components' fields. A view that runs for each entity and draws one mesh draws them all at once:

```csharp
view DrawBoxes(Box box, Models models)
{
    Draw.Mesh(models.crate, models.faces, float4x4.Translate(box.pos), Shade);
}
```

What's the same for every entity belongs in a singleton, which is sent once. A GPU has room for so much with each vertex: its struct's fields, the transform, and the components' fields the code reads, 4 floats or 4 ints to a slot, 16 slots in all. A draw that reads more is an error at the mesh draw, which counts what it reads.

2D meshes have no transform, so their instances would land on each other: a vert puts each where its component says.

```csharp
vert Place(mut Sprite vertex, Body body)
{
    vertex.position += body.position;
}
```

### Instances from a list

`Draw.Instanced(vertices, indices, instances)` and `Draw.Instanced(vertices, indices, instances)` draw one mesh for every item in a list, without needing an entity for each. Grass, scenery and visual particles can live in a local list instead.

For a 3D mesh, a `List<float4x4>` supplies its transforms:

```csharp
Draw.Instanced(models.crate, models.faces, scenery.transforms);
Draw.Instanced(models.crate, models.faces, scenery.transforms, models.wood, Filter.Point);
Draw.Instanced(models.crate, models.faces, scenery.transforms, models.wood, Filter.Point, Wrap.Repeat);
Draw.Instanced(models.crate, models.faces, scenery.transforms, Wave, Shade);
```

For data of your own, use a `List` of an ordinary struct. A vert or frag takes that struct after its vertex (and the frag's color), and receives the current list item. Either shader can take it, or both can:

```csharp
struct Blade
{
    float4x4 transform = float4x4.identity;
    Color tint = Color.green;
    float bend;
}

local singleton Foliage
{
    List<Blade> blades;
}

vert Bend(mut BoxVertex vertex, Blade blade)
{
    vertex.position.x += vertex.position.y * blade.bend;
}

frag Grass(BoxVertex vertex, out Color color, Blade blade, Sun sun)
{
    color = blade.tint * Math.Max(Math.Dot(vertex.normal, -sun.direction), 0.1);
    color.a = blade.tint.a;
}

view GrassField(Foliage foliage, Models models)
{
    Draw.Instanced(models.blade, models.faces, foliage.blades, Bend, Grass);
}
```

- A 3D instance struct's `float4x4 transform` places the mesh and is available as `Instance.transform`. Without that field, the engine uses identity. Set its default to `float4x4.identity` when it has one: an uninitialized matrix is zero.
- For a 2D mesh, the instance list holds an ordinary struct and a vert places its vertices, as `Place` does above. A field named `transform` is ordinary data there; the engine doesn't apply it, and `Instance.transform` remains identity.
- Components still come from the calling view's entity, and singletons are shared by the draw. The list items don't have to be entities and don't add any.
- Only the instance fields the shaders read are sent, beside the 3D transform. Unread fields take no GPU space. The fields that are read follow the same GPU type and attribute limits as components.
- Textures, filters, wraps, verts and frags go after the instance list, in the same order as `Mesh`: `Draw.Instanced(vertices, indices, instances[, texture[, filter[, wrap]]][, vert][, frag])`. A frag can sample fields of its parameters, or receive an explicit `Texture` through its read-only `Texture` parameter.
- An empty instance list draws nothing. Items draw in list order, in one instanced draw; adjacent compatible calls can join the same run.

Every mote below is an item of a list, and none is an entity. A vert places the quad's four corners for each, with that mote's own center, size and phase, and a frag makes a soft dot of it in its tint. The slider changes how many the list holds: they're one draw either way.

::: tide-example instances A list of motes, in one draw

<<< @/examples/instances.tide

:::

A struct parameter that's meant as an instance's is supplied by `Draw.Instanced` and a list of that type. With `Mesh`, or a list of another type, the draw is an error, unless it gives a struct of that type as [its values](#values-for-one-draw), which every instance then shares.

## Looking at what's generated

`tide shaders` prints each shader Tide writes for the game, as GLSL and as WGSL: one for each combination of vertex struct, instance data, vert and frag a mesh draw uses.
