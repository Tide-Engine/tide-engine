---
description: "Write Tide frag and vert shaders for the GPU, with textures, per-draw values, instancing, shadow maps and multiple render outputs."
---

# Frags and verts

A mesh's pixels and vertices can run code of yours on the GPU, written in Tide: a `frag` runs once for each pixel a mesh covers, and a `vert` once for each of its vertices. They're declarations, like [views](./views.md), and a `Draw.Mesh` names them:

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

`Draw.Mesh` takes a list of it, with or without a vert and a frag:

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
- **`this` is the pixel the frag runs for.** Its read-only fields say where it is, and `this.Discard()` leaves it undrawn: see [Where the pixel is](#where-the-pixel-is).
- **`out Color color` is the pixel.** A frag writes its output through an [`out` parameter](../language/functions.md#out-parameters), and returns nothing. What it wrote there when it ends is the pixel's color, blended over what's behind it by its alpha.
- **It starts with no value, so the frag writes it.** On every path that ends, and before reading it: `color.a = 1` alone, with nothing written first, is an error, and so is a frag whose only write is inside an `if` with no `else`. It isn't the pixel already on the screen either: a GPU doesn't let a frag read that. A path that ends in [`this.Discard()`](#where-the-pixel-is) draws no pixel, and owes it nothing.
- **The draw's [settings](./draw-settings.md) apply to it.** Inside `Draw.Blend(Blend.Add) { ... }` the pixel is added instead, and inside `Draw.Tint(...)` it's multiplied by the tint after the frag ran, so a faded menu fades its shaded meshes too, with nothing for the frag to do. A 2D mesh's vertices go through `Draw.Transform` after its vert, and a 3D one's transform goes through `Draw.Transform`. A 3D mesh does with depth what [`Draw.Depth`](./3d.md#depth) says: inside `Draw.Depth(Depth.Read) { ... }` it's hidden behind what's nearer and hides nothing itself, which is how something see-through is drawn. And its triangles are drawn from the side [`Draw.Cull`](./3d.md#which-side-draws) says, their front unless it says otherwise: a vert that mirrors its mesh, or turns it inside out, has them reach the screen the other way round, and is drawn inside `Draw.Cull(Cull.Front)`. Under [`Draw.Stencil`](./draw-settings.md#stencil) it's drawn where its pixels pass the test, and it stores where it's drawn: a pixel the frag leaves out with `this.Discard()` stores nothing, which is how a mask gets the shape of a picture.
- **Light the color, not its alpha.** A color times a number scales its alpha too, so `box.tint * light` alone leaves a pixel in the shade see-through, and what was drawn behind the mesh shows in it, its own far side too. The frag above sets `color.a` back.
- **For intentional transparency, set `color.a` to the opacity you want.** A 3D draw also needs depth and ordering: see [Transparent 3D](./draw-settings.md#transparent-3d). For solid pixels with holes, use [cutout holes](./draw-settings.md#cutout-holes) instead.
- A frag can write more than one color, each an `out Color` of its own: see [Several outputs](#several-outputs). An output of another type than `Color` isn't there yet.

::: tip It used to be mut
A frag's color used to be `mut Color color`, which started white. That spelling is an error now, and the editor offers the change to `out`. A frag that leaned on the white writes it itself, `color = Color.white;`, which the editor offers too where a path is missing it.
:::

### Several outputs

A frag writes up to four colors at once, each an `out Color` parameter, into a [render target made to hold as many](./render-targets.md#several-colors). They go to the target's color images in the order they're written: the first is the pixel's color, as ever, and each one after it goes to the next image.

```csharp
// One pass over the scene leaves what it looks like, which way it faces and where it is
frag Fill(Corner corner, out Color albedo, out Color normal, out Color place)
{
    albedo = corner.color;
    normal = Color(corner.normal.x, corner.normal.y, corner.normal.z);
    place = Color(this.world.x, this.world.y, this.world.z);
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

- **Each is owed on every path,** as the pixel's color is: a frag that can end without writing its second output is an error that names it, and `this.Discard()` owes none of them anything.
- **They're matched by their order,** not their names: `out Color albedo, out Color normal` writes a target's first image and its second, whatever the frag calls them, and wherever they are among its other parameters.
- **A `Color` is four floats,** and an output holds any number. What's kept of it is the image's to say: a [`Format.Rgba8`](./render-targets.md#what-a-target-holds) image keeps each channel from 0 to 1, and a `Format.Rgba16f` one keeps it as it is, which is what a normal, a place or light brighter than white needs.
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
- A read-only `Texture` parameter receives the explicit image passed to `Draw.Mesh`, `Draw.Instanced`, a target's `Mesh` or `Instanced`, or [`Draw.Blit`](./render-targets.md#post-processing). Each stage takes at most one; when both stages take it, they receive the same source. `source.Sample(vertex.uv[, Filter.Point[, Wrap.Repeat]])` uses the same top-left UVs as grid sampling. Local component and singleton `Texture` fields can be sampled too.

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

frag Fog(BoxVertex vertex, out Color color, Look look)
{
    float clear = Math.Exp(-this.depth * look.fog * 1000);
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

- It goes last, after the vert and the frag, in `Draw.Mesh`, `Draw.Instanced`, [`Draw.Blit`](./render-targets.md#post-processing) and a target's `Mesh` and `Instanced`: `Draw.Mesh(vertices, indices, transform, Wave, Shade, Swell { height = 2 })`. Any expression of the struct's type will do: a literal, a local, a field, a function's result.
- It's found by its type, as a component is, whatever the parameter is called. A draw gives one struct. Group what the vert and the frag each need in it: each reads the fields it wants.
- It's sent once for the draw, with what the code reads of singletons, and only the fields the vert and the frag read. A field neither reads costs nothing, and needn't be a type a GPU has.
- Draws in a row with the same mesh, the same vert and frag and the same values are still one draw call. Other values start another.
- With [`Draw.Instanced`](#instances-from-a-list), a frag can take both: the list's struct, which each instance has its own of, and the draw's, which they share. The two have to be different types, and neither can be the type the draw's vert [writes for its frag](#from-a-vert-to-its-frag).
- It holds no texture to sample. A draw's image is its `Texture` argument, or a field of a component or singleton.

It's an error at the draw when its frag or vert takes a struct that nothing supplies (not the draw's vert, its instances or its values), and when the draw gives a struct neither takes.

## Lists and arrays

A frag or a vert reads a list that's a field of what it takes (a component's, a singleton's, or the values a draw gives) or a `List<byte>` [asset](./assets.md) by its name. It reads an element by its place, how many there are, and all of them with `foreach`:

```csharp
struct Light
{
    float3 position;
    Color color;
    float reach;
}

local singleton Lights
{
    List<Light> items;
}

frag Lit(BoxVertex vertex, out Color color, Lights lights)
{
    mut var sum = Color(0.1, 0.1, 0.1);
    foreach (var light in lights.items)
    {
        var far = Math.Length(light.position - this.world);
        if (far < light.reach) sum += light.color * (1 - far / light.reach);
    }
    color = Color(sum.r, sum.g, sum.b);
}
```

- `lights.items[i]` is an element, `lights.items.count` how many, and `foreach` goes through them in order. A list has no fixed size: it's as long as the view left it, thousands of elements if it likes, and the frag loops over what's there.
- An element past the end, or before the start, is zeros, as it is on the CPU.
- Elements are what the GPU has values of: numbers, bytes, vectors, colors, matrices, enums, bools, structs of those, and [arrays](#arrays) in them. Not text, longs, entities or other lists.
- The GPU reads a list, and changes none: there's no `Add` there. A view or a system fills it.
- A local can name it, and a function can take it, as anywhere: see [Naming a list and passing it on](#naming-a-list-and-passing-it-on).
- Each list a draw's code reads is one of its 8 textures, as that's how its numbers travel: an image of numbers, read by an element's place.
- **Only what's read is sent.** Of an element, the GPU gets the fields that some frag or vert of the game reads, and no others: a `Light` with a dozen fields of which the frags read `position` and `color` costs those two. It's the same whether the code writes `lights.items[i].color`, reads it off a `foreach`'s variable, or off a local that holds an element (`var light = lights.items[i];`). A struct inside an element counts field by field. Handing a whole element to a function, or returning one, reads all of it.
- **Only what changed is sent again.** A list that didn't change since the last frame costs nothing. In one that did, the elements that changed go to the GPU and the rest stay as they are there, so a few lights moving in a list of thousands cost those few. A list that's only counted (`items.count`) sends nothing at all.
- A component's list is its entity's own, so entities with lists are drawn one by one, not all in one draw call. What many entities share belongs in a singleton, with each entity saying where its part starts.
- The struct [`Draw.Instanced`](#instances-from-a-list) takes for each instance can't hold a list the code reads: a draw sends one list for all of them.

### A grid's cells

A grid is read the same way, a cell by its place, beside [sampling it](#textures) as a picture: `cells[x, y]`, `cells[at]` for an `int2`, and `cells.size`; for a `Grid3`, `cells[x, y, z]`, `cells[at]` for an `int3`, and a `size` of three. It's the cell as the grid has it, exactly, of any type the GPU has values of, where `Sample` gives a color blended between cells.

```csharp
local singleton Board
{
    Grid2<int> owner = Grid2(64, 64);
}

frag Tiles(BoxVertex vertex, out Color color, Board board)
{
    var at = int2(int(vertex.uv.x * board.owner.size.x), int(vertex.uv.y * board.owner.size.y));
    color = board.owner[at] == 1 ? Color.red : Color.blue;
}
```

- A cell outside the grid is zeros. The grid is a field of what the frag or vert takes, or an [asset](./assets.md) by its name, and counts as one of the draw's 8 textures, once more if it's also sampled.
- As with a list, only the fields of a cell that the code reads are sent, and when cells change, only those cells go to the GPU again.
- An asset's grid, and an asset's list, are read by name in the functions a frag or vert calls too.
- A grid with no size on an axis is read like any other: its cells are wherever they were set, before 0 too, `size` is 0 on that axis as it is on the CPU, and a cell nothing set is zeros.
- What's sent is the box around the cells that were set, never the whole space a grid spans. (A grid keeps its cells in chunks, 64 by 64 or 16 by 16 by 16 of them for a cell of one number, and the box is the chunks that have something in them, within the grid's size.) It travels as one image of numbers, a `Grid3`'s layers side by side in it.
- So how big a grid the GPU reads goes by that box. One of 256 by 256 by 256 cells of one number each (an int, a float, a byte, an enum), or 128 by 128 by 128 colors, fits an image 2,048 pixels a side, which every GPU has. The most is an image of 16,777,216 pixels and 16,384 a side, on a GPU that has textures so big: 256 by 256 by 256 colors, or a `Grid2` of 16,384 by 4,096 numbers. A box past that isn't sent, the code reads zeros, and the game says so once in its output; two cells set a million apart in a grid with no size are such a box.
- `foreach` over a grid's cells isn't there yet: write the loop over the places you read, `for (int y = 0; y < cells.size.y; y++)`.

### Maps and sets

A `Map<K, V>` or a `Set<K>` that's such a field is read by its keys, as it is anywhere:

```csharp
local singleton Land
{
    Grid2<int> ground = Grid2(64, 64);
    Map<int, Color> paint;      // By a tile's number
    Set<int> lit;
}

frag Tiles(BoxVertex vertex, out Color color, Land land)
{
    var at = int2(int(vertex.uv.x * land.ground.size.x), int(vertex.uv.y * land.ground.size.y));
    var tile = land.ground[at];
    var known = land.paint.TryGet(tile, out var paint);
    var shown = known ? paint : Color.gray;
    color = land.lit.Has(tile) ? shown : shown * 0.5;
    color.a = 1;
}
```

- `map[key]` is the key's value, or zeros when the key isn't there. `map.Has(key)` and `set.Has(key)` say whether it is, and `map.TryGet(key, out var value)` says so and gives the value, or the value type's default. `count` is how many keys there are.
- `foreach (var entry in map)` goes through a map's entries, each with its `key` and its `value`, and `foreach (var key in set)` through a set's keys: in the order the keys were added, without the ones removed since, as on the CPU.
- Keys are ints, bytes or enums. A map keyed by text, a `long`, an `Entity` or a `PlayerID` stays on the CPU, and reading one in a frag is an error that says why: keep a second map keyed by a number for what the GPU reads.
- Values are what the GPU has values of, as a list's elements are.
- Read-only, as lists are: a view or a system fills the map. There's no `Get` there either, as it gives a value that may be missing: `TryGet` asks the same.
- `TryGet`'s `out` argument is a local, `out var value` or a variable declared before.
- A lookup reads a pixel or two of an index the engine sends beside the entries, however many keys the map has, so it costs about what reading two list elements does.
- A map or a set is one of the draw's 8 textures, and isn't sent again while it doesn't change. Of its values, only the fields the code reads are sent, as a list's elements'. The other rules are a list's: a local can name one and a function can take one ([below](#naming-a-list-and-passing-it-on)), a component's is its entity's own, and none in an instance's struct.

### Naming a list and passing it on

A local can name a list or a grid, and a function can take one, so the code that goes through a list is written once:

```csharp
float Brightest(List<Light> lights, float3 at)
{
    mut var most = 0.0;
    foreach (var light in lights)
    {
        most = Math.Max(most, 1 - Math.Length(light.position - at) / light.reach);
    }
    return most;
}

int Owner(Grid2<int> owner, float2 uv)
{
    return owner[int(uv.x * owner.size.x), int(uv.y * owner.size.y)];
}

frag Lit(BoxVertex vertex, out Color color, Lights lights, Board board)
{
    var items = lights.items;
    var glow = Brightest(items, this.world);
    color = Owner(board.owner, vertex.uv) == 1 ? Color(glow, 0, 0) : Color(0, 0, glow);
}
```

- A function that takes a `List<T>` is the same function a system or a view calls: it compiles for both.
- On the GPU the list isn't handed over as the code runs. Which list a name stands for is settled as the shader is written, and a function is written once for each list it's called with. It costs nothing while it runs, and it's why a name there is for one list, for good:
  - A list's or a grid's local isn't `mut`, and it's given a list by its name: a field of what the frag or vert takes, an asset, another such local, or a parameter. Not one picked as the code runs, like `flag ? a.items : b.items`: read each under its own name, and pick between what was read.
  - A function reads the list or grid it takes: no `mut` or `out`, and none is returned, made, or kept in a struct.
- A function that takes a grid is the GPU's alone: frags, verts and the functions they call call it. On the CPU a grid's cells are read through its component or singleton, `board.owner[x, y]`. A grid's local is in a frag, a vert or such a function.
- `Sample` still reads a grid or a texture where the frag or vert takes it, or an asset by its name, in the frag or vert itself.

### Arrays

An [array of a fixed length](../language/text-and-lists.md#arrays) is a value like a vector: `float[8]`, `Stop[4]`. As a field of a component, a singleton or a draw's values it's sent with the other fields the code reads, and in code it's a local, a parameter or a function's result like any other value:

```csharp
local singleton Ramp
{
    Color[4] stops;
}

frag Shade(BoxVertex vertex, out Color color, Ramp ramp)
{
    mut float[3] sums = [0, 0, 0];
    foreach (var stop in ramp.stops) sums[0] += stop.r;
    color = ramp.stops[int(vertex.uv.x * 4)] * sums[0];
}
```

- `a[i]` reads an element, zeros past the ends; `a[i] = v` writes one, and does nothing past the ends; `a.count` is its length; `foreach` goes through it.
- An array is sent whole, with a draw's other values: those hold 3,984 bytes, about 249 float4s, for everything a draw reads of singletons. Past a few dozen elements, a list is the better fit.
- An array of arrays is one too: `float[4][8] rows`, with `rows[i][j]` read and written by the same rules at each index, and `rows[i]` a row to keep, pass on, assign or `foreach` through.
- `?:` picks between arrays, as between structs and matrices: `var stops = night ? ramp.dark : ramp.light;`. Both sides are worked out, as everywhere on the GPU.
- A `for`'s step can write an element (`for (var k = 0; k < 6; counts[k % 3]++)`), and a `continue` still comes to it.

## Textures

`Sample` reads a [texture](./meshes-and-textures.md#textures) at a `uv`, and gives its color there. A frag samples the textures it can name three ways:

```csharp
frag Shade(Corner corner, out Color color, Models models, Texture source)
{
    color = models.wood.Sample(corner.uv)        // A field of a component or singleton it takes
          * tiles.Sample(corner.uv)              // An asset, by its name
          * source.Sample(corner.uv);            // The texture its draw gives it
}

view Crates(Models models)
{
    Draw.Mesh(models.crate, models.faces, models.label, Shade);   // models.label is Shade's `source`
}
```

- **A field**: a `Texture`, a `Grid2<Color>` or a `Grid2<float>` of a component or a singleton the frag takes.
- **`Sample` gives the color itself**, with its alpha beside it, blended between pixels so that a clear pixel counts for none of the color (see [Alpha](./draw-settings.md#alpha)). A pixel with no alpha reads as `Color(0, 0, 0, 0)`. What a frag writes is a color as it is too: `Color(1, 0, 0, 0.5)` is red, half clear.
- **An asset**, by its name, with no parameter for it: `asset Texture tiles = "art/tiles.png";` and then `tiles.Sample(uv)` (see [Assets](./assets.md#images-to-draw-with)).
- **Its own `Texture` parameter**, one at the most, which is whatever texture the draw gives after its mesh: an asset, a target's color, a grid, a `Texture` a field holds. A [`Draw.Blit`](./render-targets.md#post-processing)'s source is its frag's.

`uv` is as a mesh's: (0, 0) is the outer corner of the texture's first pixel and (1, 1) the outer corner of its last. Verts sample too. A `Grid2<float>` gives its number as the color's `r`, with `g` and `b` 0 and `a` 1 (see [One channel](./meshes-and-textures.md#one-channel)).

How it's read is the texture's own to say, and the `Sample`'s where it says:

```csharp
color = models.wood.Sample(corner.uv);                                   // As `wood` says: blended and clamped, if it says nothing
color = models.wood.Sample(corner.uv, Filter.Point);                     // The nearest pixel
color = models.wood.Sample(corner.uv * 4, Filter.Point, Wrap.Repeat);    // Four times across, and four down
```

Past 0 and 1, `uv` reads what the [wrap](./meshes-and-textures.md#past-the-edges) says: the pixels at the edge (`Wrap.Clamp`), the texture again (`Wrap.Repeat`), or again the other way round each time (`Wrap.Mirror`). A frag is where a wrap earns its keep, since it works its `uv` out: `corner.uv * 4` tiles a floor, and `corner.uv + float2(time.t * 0.1, 0)` scrolls one.

- What a `Sample` leaves out is the texture's: what its declaration says ([`[Filter]` and `[Wrap]`](./meshes-and-textures.md#saying-it-once-on-the-texture)), and blended and clamped with nothing said. A `Texture` carries that with it, so a frag's `source` is read as whichever texture the draw gave says, and a `Texture` field as whichever it holds: the same frag draws `tiles` repeated and `hero` clamped, in two draws.
- A draw can say it for that draw, after the texture: `Draw.Mesh(corners, faces, tiles, Filter.Bilinear, Wrap.Clamp, Shade)` gives `source` blended and clamped, whatever `tiles` says.
- A filter and a wrap written at a `Sample` are written out, `Filter.Point` and `Wrap.Repeat`, never read from a variable: the GPU is told how a texture is read before any pixel runs. Where they come with the texture, the engine tells it for each draw.
- A wrap comes after a filter, so a `Sample` that says how it wraps says both. The names work too: `Sample(uv: corner.uv, filter: Filter.Bilinear, wrap: Wrap.Repeat)`.
- One texture can be read several ways in one frag, clamped here and repeated there. Each way counts as one of the 8 textures a draw samples, and the same way twice counts once. A `Sample` that says nothing and one that says the default are two ways for a `Texture` parameter or field, since the texture may say otherwise.
- A draw gives a texture to a frag that takes one, and none to a frag that doesn't: `Draw.Mesh(corners, faces, models.wood, Shade)` is an error where `Shade` has no `Texture` parameter.

`Filter.Trilinear` reads between the texture's smaller copies too, so it's smooth where the texture ends up [smaller than it is](./meshes-and-textures.md#drawn-smaller):

```csharp
frag Ground(Corner corner, out Color color, Land land)
{
    var uv = corner.uv * land.tiling;            // Worked out from what the frag is given
    color = Color.black;
    if (corner.height > 0)
    {
        color = land.grass.Sample(uv, Filter.Trilinear, Wrap.Repeat);   // In a branch, as any Sample
    }
}
```

The GPU picks which copies to read by how fast the `uv` changes from one pixel to the next, and it only knows that before any branch or loop. So a frag works such a `uv` out a first time as it starts, asks there, and reads with the answer wherever the `Sample` is. You write nothing for it, and the `Sample` goes where it would anyway: in a branch, in a loop, after `this.Discard()`. What it asks of you is a `uv` it can work out as it starts:

- **Made of** the frag's parameters (its vertex, what its vert wrote for it, components, singletons, the draw's values), the pixel's fields through `this`, constants, `Math`, constructors, your own functions that take everything by value, another texture read at its own pixels, and variables declared with `var` that are never assigned again.
- **Not of** a variable that changes (`mut var`, one a loop counts): `tiles.Sample(corner.uv + float2(i * 0.01, 0), Filter.Trilinear)` in a loop is an error, which says so. Sample those with `Filter.Bilinear`, or work each `uv` out before the loop.
- Where the filter is the texture's to say (a frag's `Texture` parameter, a `Texture` field) and the `uv` can't be worked out as the frag starts, the `Sample` reads the texture's own pixels, as `Filter.Bilinear` would, whatever the texture says, and tidec warns about it: write `Filter.Bilinear` there to say that's what you want, or make the `uv` one the frag can work out as it starts.
- A vert has no pixels to compare, so there `Filter.Trilinear` reads the texture's own pixels.

### Textures that are numbers

A texture is colors, each with how much of it there is: where a pixel's alpha is 0 it has no color, and a `Sample` there gives `Color(0, 0, 0, 0)` (see [Alpha](./draw-settings.md#alpha)). That's wrong for an image whose channels are four numbers of their own: a mask in alpha beside a roughness in red, a lookup table, a flow map. `[Raw]` says so where the texture is declared:

```csharp
[Raw] asset Texture masks = "art/masks.png";     // Kept as the file has it

local singleton Terrain
{
    [Raw] Grid2<Color> blend = Grid2(64, 64);    // ...or as code wrote its cells
}

frag Ground(Vertex vertex, out Color color, Terrain terrain)
{
    Color mask = masks.Sample(vertex.uv);                 // Four numbers, as they are
    Color weights = terrain.blend.Sample(vertex.uv);      // Blended between cells as numbers
    color = Color(mask.r * weights.r, mask.g, mask.b * weights.a);
}
```

- It goes before an asset that's one `Texture` or one `Grid2<Color>`, and on a field that's a `Grid2<Color>`. A `Grid2<float>` needs none: it has no alpha.
- A frag or a vert reads it **by its name**, as above. It isn't a texture to draw with, to give a draw for its frag's `Texture` parameter, or to keep in a `Texture` field: each is an error that says so, as everything that draws a texture takes it for colors, and a `Texture` handed on doesn't say which kind it is.
- The same file can be declared twice, once `[Raw]` and once not: they're two textures.
- `[Filter]` and `[Wrap]` go beside it as on any texture, and code reads a `[Raw]` grid's cells as it reads any grid's.

## A target's depth

A [render target](./render-targets.md#what-a-target-holds) keeps how far the nearest thing drawn at each of its pixels is, and a frag reads that as it reads a color: `target.depth`, a `DepthTexture`, which a view keeps in a field of a singleton or a component the frag takes.

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

- **`Sample(uv)`** gives the depth as a number: 0 at the camera's near plane, 1 at its far limit, as [`this.depth`](#where-the-pixel-is) is, and 1 where nothing was drawn since the target was cleared. It's the nearest pixel's, with no filter to pick: a depth blended between two pixels would be neither's.
- **`Compare(uv, value)`** compares what's stored with a number of yours, and gives a number from 0 to 1: 1 where what's stored isn't nearer than `value`, 0 where it is, and in between along an edge, as the GPU compares the pixels around `uv` and blends what that gives. `value` is kept between 0 and 1, so what's past the far limit counts as at it.

`uv` is a color's: (0, 0) is the target's top left corner. Past the target's edges, both read the pixels at the edge: a depth takes no `Wrap`. Verts read a depth too. Each way a frag reads one is one of its draw's [8 textures](#what-the-gpu-runs).

### A shadow map

That second read is what a shadow is. Draw what casts into a target of depth alone, through a camera where the lamp is, and that target holds how far the lamp sees along each of its pixels. A frag that lights a pixel then asks whether the lamp sees that far: it works out where its pixel is in the lamp's image, and compares how far the pixel is from the lamp with what's stored there.

The scene below does it. **Lamp** turns the lamp, **Soft edges** takes four comparisons around the pixel where one would do, and **The lamp's depth** shows the lamp's own image in the corner: near is dark, and what nothing was drawn on is white.

::: tide-example shadows A shadow map

<<< @/examples/shadows.tide

:::

- **What casts is drawn with no frag,** and has the engine's own look, as anywhere: in a target of depth alone, a color has nowhere to go, and how far each pixel is, is what's kept. A caster with holes in it names a frag that calls `this.Discard()` for them.
- **The lamp's camera is an ordinary one:** `target.SetCamera(transform, projection)`, with `float4x4.Ortho` for a lamp that's far away, like a sun, and `float4x4.PerspectiveFov` for one that's near.
- **Where a pixel is in the lamp's image is yours to work out,** from the matrix that takes the world to the lamp's clip space. That's the lamp's camera, which the target gives back as the GPU has it: [`target.WorldToClip()`](./cameras.md#a-camera-as-matrices), asked once the camera is set, and kept where the frag reads it:

  ```csharp
  lamp.map.SetCamera(transform, projection);
  lamp.toLamp = lamp.map.WorldToClip();
  ```

  ```csharp
  float4 seen = Math.Mul(lamp.toLamp, float4(this.world, 1));
  float2 uv = float2(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5);   // From the image's top left corner
  float depth = seen.z * 0.5 + 0.5;                             // From 0 to 1, as what's stored is
  ```

  Under a perspective, divide `seen.xyz` by `seen.w` first.

- **Compare a little nearer than the pixel is,** `depth - 0.003`: a surface the lamp sees is exactly as far as what's stored there, to the rounding, and would be in its own shadow in stripes. How much it takes goes with how slanted the surface is to the lamp and how few pixels the lamp's image has across what it sees.
- **A target with pixels of its own** (`RenderTarget(1024, 1024, 1, ...)`) casts the same shadows on every display. The fewer pixels it has across what the lamp sees, the blockier a shadow's edge.
- **The target that's read isn't the one drawn into.** A draw into a target that reads that target's depth is passed by, with a line that says so.
- **The target that's read has no `samples`.** A target made with [`samples`](./render-targets.md#smooth-edges) keeps several depths a pixel, which no frag reads: a draw that reads its depth is passed by the same way.

## Skybox and reflections

A `Cubemap` is six square images, the faces of a cube, which a frag reads by a direction from the cube's middle: the color of whatever is that way. It's how a sky is drawn, and what a mirror reads.

```csharp
asset Cubemap sky = "art/sky_??.png";

frag Sky(Corner corner, out Color color)
{
    color = sky.Sample(corner.position);
}
```

- **`Sample(direction)`** takes a `float3` of any length, in the world's axes. One face goes on in the next, so there's no edge, and nothing to [wrap](./meshes-and-textures.md#past-the-edges).
- **`Sample(direction, filter)`** says how it's read between its pixels, written there like a texture's: `Filter.Bilinear` when left out, `Filter.Point` for the nearest pixel, and `Filter.Trilinear` between its [smaller images](#textures) too, in a frag, of a direction the frag can work out as it starts.
- **The cube is an asset by its name,** or a field of a component or a singleton the frag takes, where a view put one. Verts read one too. Each way a frag reads one is one of its draw's [8 textures](#what-the-gpu-runs).
- A cube that was never set reads as black.

There are two kinds, and a frag reads both the same way.

**An asset's** is six files that a glob matches, each named for its face by how its name ends before the extension: `px`, `nx`, `py`, `ny`, `pz` and `nz`, the faces `+x`, `-x`, `+y`, `-y`, `+z` and `-z` point at. Each is a PNG, a square, and all six are one size (see [Assets](./assets.md#cubemaps)).

**One a view draws into** is `Cubemap(size)`: six images of `size` pixels a side, each a [render target](./render-targets.md) to draw into.

```csharp
local singleton Room { Cubemap around; bool made; }

view Reflect(mut Room room)
{
    if (!room.made) room.around = Cubemap(256);
    room.made = true;
    for (var face = 0; face < 6; face++)
    {
        var into = room.around.faces[face];
        into.SetCamera(float3(0), Facing(face), 90);
        into.Clear(Color.black);
        into.Mesh(corners, faces, transform, Look);
    }
}
```

- **`cube.faces[0]` to `cube.faces[5]`** are `RenderTarget`s, in the order `+x`, `-x`, `+y`, `-y`, `+z`, `-z`. Each has everything a target has: its own camera, clip and settings, `Clear`, `Mesh`, `Instanced` and the rest, and depth of its own.
- **A face that's drawn again each frame is cleared first.** Its color and its depth stay from frame to frame, as any target's do: without a `Clear`, what was nearest in the frames before hides what's drawn now.
- **A face is what a camera at the cube's middle sees along the face's axis,** with a field of view of 90: `+y` is up for the four around, and for the one above and the one below, `-z` and `+z`. That's six rotations, which the example below has as `Facing`.
- `Cubemap(size, Format.Rgba16f)` holds floats, which keep what's brighter than 1, where the device draws into them.
- Its size is in pixels, whatever the display. `cube.Resize(size)` gives it another, with empty images, and `cube.Dispose()` lets it go. Copies share it, as a target's do.
- **A draw into a face doesn't read that face's cube.** It's passed by, with a line that says so. What reflects itself in itself takes two cubes.
- A face has no `color` or `depth` of its own to hand out: the cube is what's read.

The scene below has both. The sky is the asset, drawn on a ball around the camera. The ball in the middle reads a cube that's drawn again each frame from where it is: the sky and the three moons, a face at a time. **Reflect the moons** switches it to the sky's own files, which have no moons in them. Drag to look around.

::: tide-example skybox A sky, and a ball that reflects what's around it

<<< @/examples/skybox.tide

:::

- **The sky is a ball around the eye,** drawn from inside (`Cull.Front`) and with `Depth.None`, so everything else is in front of it. Its frag reads the cube towards each corner's own position.
- **A reflection is `Math.Reflect`:** the way from the eye to the pixel, bounced off the pixel's normal, is the way the mirror reads.
- **Six passes a frame is what a live reflection costs.** A cube that only holds what doesn't move is drawn once and kept: its images stay from frame to frame, like any target's.
- **`Filter.Trilinear` reads a cube's smaller images,** which the renderer makes of what was last drawn into it when a draw reads them. That's what keeps a reflection of something far away from shimmering. There's no choosing the level yet, so a rough surface's blurred reflection isn't there.

## Where the pixel is

The engine supplies the draw's transform through `Instance instance`, and the pixel a frag runs for through `this`:

```csharp
frag Lit(BoxVertex vertex, out Color color, Instance instance, Sun sun)
{
    float3 normal = instance.Normal(vertex.normal);
    float3 toLight = Math.Normalize(sun.position - this.world);
    color = Color.white * Math.Max(Math.Dot(normal, toLight), 0.1);
    color.a = 1;
}
```

The sphere below has a vertex of its own, with a normal, and a frag that lights each pixel from it: the normal as it is in the world, from `instance.Normal`, against a light the slider moves. **Squash** flattens the sphere through the draw's transform, and its light stays right. **Bands** rounds the light to steps, in the same frag.

::: tide-example lighting Lighting, in a frag

<<< @/examples/lighting.tide

:::

- `Instance instance`, in a frag or a vert, is how the mesh was drawn:
  - `instance.transform` is the `float4x4` the draw was given: `Math.Transform(instance.transform, vertex.position)` is a corner's place in the world.
  - `instance.Normal(vertex.normal)` is a normal of the mesh as it is in the world, of length 1, however the transform scales the mesh. Nothing more is sent for it: the GPU works it out from the transform, once a vertex.
  - **A normal isn't turned like a direction.** `Math.Rotate(instance.transform, vertex.normal)` is right only for a mesh scaled the same every way. On a ball drawn as an egg, or a cylinder drawn as a flat disc, it leans the normals the wrong way, and the lighting is subtly off with nothing to say why. `Normal` is right for those, for a mesh turned inside out by a scale under 0 (its normals still point out of it), and for one flattened to nothing along an axis (they're along that axis).
  - A vert that moves its mesh's corners itself (a wave, a bend) changes its normals in ways only the game knows: `Normal` goes by the draw's transform.
- In a frag, `this` is the pixel it runs for, as `this` in a system is its entity. It needs no parameter, and its fields are read-only:
  - `this.world`, a `float3`: where on the mesh the pixel is, in the world.
  - `this.position`, a `float2`: where it is in the destination's logical pixels from the top left, as [`Draw.Screen`](./views.md#on-the-screen)'s.
  - `this.depth`: how far it is, from 0 at the camera's near side to 1.
  - `this.Discard()` leaves the pixel undrawn, and ends the frag there: for holes, and edges cut out of a texture. Nothing is written for it: no color, no depth and no [stencil](./draw-settings.md#stencil).

`this` is context accessed through its fields and `Discard()`: it can't be copied, stored or passed whole. It belongs to the frag itself: a vert has no pixel yet, and an ordinary function a frag calls has no implicit `this`. Pass any pixel coordinates a helper needs as ordinary values. An old built-in `Pixel pixel` parameter is an error that says to remove it and use `this`; a game can still declare a type of its own named `Pixel`.

```csharp
frag Leaves(BoxVertex vertex, out Color color, Models models)
{
    color = models.leaves.Sample(vertex.uv);
    if (color.a < 0.5) this.Discard();
}
```

The camera isn't given: keep where yours is in a singleton, where the view that calls `Draw.SetCamera` reads it too, and take that.

## What the GPU runs

Code for the GPU is ordinary Tide, less what a GPU doesn't have:

- **It has** bools, bytes, ints, floats, vectors, quaternions, matrices, colors, enums, structs of those and [arrays](#arrays) of them; locals, `if`, `?:`, `switch` on an int or an enum, `for`, `while` and `foreach`, with `break` and `continue`; [lists and grids](#lists-and-arrays) that are fields of what it takes, read by an element, a count or `foreach`; your functions, methods and operators; all of [`Math`](#math), and what builds its types; [`Random`'s rolls](#random-numbers); and sampling a grid, a texture or a target's depth, 8 of them to a draw, anywhere in the code: in a branch, in a loop, in a vert.
- **It doesn't have** text, entities, spawning and sending, `Draw`, the GUI, `Devices`, `Debug`, C, tasks, errors, or recursion. It makes and changes no list, map or set. A frag's `this` is its pixel; verts and ordinary functions have no `this`. A `long` has no place there, as a GPU's ints are 32 bits.

Each of those is an error where it's written, which says what to do instead. A function a frag calls has to fit too, and the error says which frag reaches it; the same function still runs on the CPU for the code that calls it there.

```
error: string can't go to the GPU: text is the CPU's
  = note: frag 'Shade' runs on the GPU, and 'Label' with it, as it calls it
```

A loop that never ends hangs the GPU as it hangs a system: nothing stops it for you.

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

- **Noise is the match's cells.** `Math.Perlin`, `Simplex` and `Cellular` take each cell's gradient or feature point from `Math.Hash`, so a frag shades a tile with the noise the match computed for it, up to the GPU's rounding of the floats between. [Noise](../language/math.md#noise) shows them, drawn by a frag.
- **Floats come out as the GPU rounds them.** `Sin`, `Pow`, a quaternion's `Slerp` and a matrix's `Inverse` follow the same formulas as on the CPU, and each GPU rounds them its own way. Big numbers lose the most: the sine of a number in the thousands is wherever that GPU's rounding puts it, so the old trick of a hash made from a sine comes out striped on some machines. `Math.Hash` is what that's for.
- **It's as forgiving as on the CPU** (see [Forgiving with bad values](../language/math.md#forgiving-with-bad-values)): `Normalize` gives zero for a vector with no direction and the identity for a quaternion with none, `Min` and `Max` with one NaN give the other, and `Clamp` and `Saturate` always give a value in range.
- **It gives a number wherever the CPU does.** A GPU's own functions leave some answers open, and Tide's don't: a whole power of a number under 0 (`Math.Pow(-2, 2)` is 4), anything to the power of 0 (1), `Math.Atan2(0, 0)` (0), and `Math.SmoothStep` from an edge down to a lower one (it falls where the other way around rises). Where the CPU has no number to give either (the square root of a number under 0, the inverse of a matrix that has none), the GPU gives whatever it gives.

What's left out is left out for a reason, and its error says what to write instead. There's one:

| Not on the GPU | Why | Instead |
|---|---|---|
| `Math.Hash` of a `long` | A GPU's ints are 32 bits, so no `long` gets there | `Math.Hash(int2(low, high))` is the same number, for the long's halves: `int(x)` and `int(x >> 32)`, split on the CPU and kept in fields the frag reads |

A `Random` of your own isn't there either: its state is 64 bits. [Rolls on the type](#random-numbers) are.

::: warning The GPU's numbers aren't the match's
Every machine's GPU rounds a little differently, so what a frag or a vert works out never reaches the match: they're local code, as views are, and what `Math` makes of floats there is that GPU's own. Only its integers, `Math.Hash`, are the same everywhere. Anything the match depends on is worked out in a system.
:::

### Random numbers

[`Random`'s rolls](../language/math.md#random-numbers) work in a frag and in a vert, with the same calls as anywhere: `Random.NextFloat()`, `Random.NextInt(6)`, `Random.NextFloat2Direction()` and the rest.

```csharp
// Film grain over the frame: a blit's frag, which rolls for each pixel
frag Grain(Vertex vertex, out Color color, Texture source)
{
    var seen = source.Sample(vertex.uv);
    var speck = (Random.NextFloat() - 0.5) * 0.08;
    color = Color(seen.r + speck, seen.g + speck, seen.b + speck);
}
```

There's nothing to set up and nothing to pass: a function a frag calls rolls too.

- **A pixel rolls its own numbers, and so does a vertex.** Each time a frag runs, it has a generator made for that pixel: from where the pixel is in what's drawn into (the window's own pixels, or a target's), and which instance of its draw it belongs to. A vert's is made from which vertex of the mesh it is, and which instance. Rolls in the same run follow each other, as in a system.
- **Every frame is other numbers.** The frame rolls one number with this machine's generator, the one [views roll with](../language/math.md#where-rolls-work), and every pixel's and vertex's generator is made with it. So grain moves, static flickers, and a sparkle is somewhere else the next frame.
- **To hold still, hash.** `Math.Hash(int2(this.position))` is the same number for a pixel every frame, and `Math.Hash` of a cell is the same for the cell wherever it's drawn. [Noise](../language/math.md#noise) is that, smoothed.
- **Two frags roll apart.** Which frag or vert is rolling goes into the generator too, so two that draw over the same pixel don't roll the same there.
- **Draws still join.** The frame's number is the same for every draw of the frame, so rolling doesn't split [instances drawn in a row](#many-at-once) into draws of their own. Two draws that don't join each count their instances from the first, so where they cover the same pixel through the same frag, they roll the same there.
- **`Random.seed` is the match's seed**, as in a view: 0 with no match. With it a frag draws what the match made from its seed, like `Math.Perlin(cell, Random.seed)`.

::: tide-example grain Grain and sparks, rolled by the GPU

<<< @/examples/grain.tide

:::

What's different from rolling on the CPU:

- **They aren't the CPU's numbers.** A GPU's ints are 32 bits, so its generator is a smaller one than a system's. What each roll gives is the same: `NextInt(6)` is 0 to 5, `NextFloat()` never gives 1, and a range with nothing in it gives its start.
- **Order is a statement's.** Rolls on separate lines come in that order. Two rolls in one expression come in whichever order that GPU works the expression out, so where it matters which is which, give each a line. (`NextFloat2()` and the other vectors always roll x first.) A `?:` with a roll in it makes the rolls of both its sides, whichever it picks.
- **A `Random` you keep doesn't go there.** A field, a local or a parameter of type `Random` is the CPU's: its state is 64 bits. On the GPU, roll on the type.
- **`Filter.Trilinear` can't sample at a place a roll made.** The frag works such a place out once more as it starts, to ask the GPU how fast it changes, and a roll made twice is two numbers. Sample there with `Filter.Bilinear`.
- **A frag that rolls carries one more value** from the mesh's corners to its pixels, of the 15 a draw has: which instance.

The generator is whole numbers, so `Random.NextInt` gives a pixel the same number on every GPU, for the same frame. Like everything a frag works out, it never reaches the match.

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
