# Frags and verts

A mesh's pixels and vertices can run code of yours on the GPU, written in Tide: a `frag` runs once for each pixel a mesh covers, and a `vert` once for each of its vertices. They're declarations, like [views](./views.md), and a `Draw2D.Mesh` or `Draw3D.Mesh` names them:

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

// Once per pixel: the triangle's three vertices blended at that pixel, and the pixel's color to write
frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, Models models)
{
    float light = Math.Max(Math.Dot(Math.Normalize(vertex.normal), -sun.direction), 0.1);
    color = models.wood.Sample(vertex.uv) * box.tint * light;
}

view DrawBox(Box box, Models models)
{
    Draw3D.Mesh(models.crate, models.faces, float4x4.Translate(box.pos), Wave, Shade);
}
```

Their parameters say what they read, and the engine supplies it. There's nothing to bind, upload or declare twice, and no shader language to learn: tidec writes the shaders, for every GPU Tide draws with, and they always compile.

Change the wave height to move the mesh's vertices, and the stripe count to change what each pixel computes. Both shaders read the same local singleton the sliders edit. Turn off animation to inspect a frame.

::: tide-example shaders A frag and a vert

<<< @/examples/shaders.tide

:::

## Your own vertex

A mesh's vertices are a list of a struct, and the struct can be yours:

- It has a `position`: a `float2` makes a 2D mesh, drawn through the 2D camera in order with the other Draw calls, and a `float3` a 3D one, which `Draw3D.Mesh` places with a transform.
- Its other fields are whatever the mesh needs: floats, ints, their vectors and colors, 12 fields at most. The GPU reads them from the list as they are, so a vertex costs what its struct does: a struct with only a `position` is 12 bytes a vertex.

`Draw2D.Mesh` and `Draw3D.Mesh` take a list of it, with or without a vert and a frag:

```csharp
Draw2D.Mesh(vertices, indices);                           // 2D
Draw3D.Mesh(vertices, indices, transform);                // 3D
Draw3D.Mesh(vertices, indices, transform, Shade);         // A frag
Draw3D.Mesh(vertices, indices, transform, Wave);          // A vert
Draw3D.Mesh(vertices, indices, transform, Wave, Shade);   // Both: the vert first
```

With no frag, a pixel is its vertex's `color` times the draw's texture at its `uv`, as the built-in `Vertex` and `Vertex3` draw: a struct with no `Color color` draws white, and a texture needs a `float2 uv` to be read at. With no vert, the vertices are used as they are.

## A frag

```csharp
frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun)
{
    color = box.tint * Math.Max(Math.Dot(vertex.normal, -sun.direction), 0.1);
}
```

- **The vertex comes first.** It's the struct the mesh's list holds, and the frag gets it blended across the triangle: at each pixel, a mix of the triangle's three vertices by how near each is. It can't change it. Its ints don't blend, so a frag can't read them.
- **`mut Color color` is the pixel.** A frag writes its output through `mut`, as a system writes a component, and returns nothing. What `color` holds when the frag ends is the pixel's color, blended over what's behind it by its alpha.
- `color` starts as `Color.white`. It isn't the pixel already on the screen: a GPU doesn't let a frag read that. A frag that never writes it draws white.
- A frag has one output for now, a `Color`. More, and other types, aren't there yet.

## A vert

```csharp
vert Wave(mut BoxVertex vertex, Time time)
{
    vertex.position.y += Math.Sin(time.t * 2 + vertex.position.x) * 0.1;
}
```

A vert takes its vertex `mut` and changes it in place: where it is, and anything else the pixels read of it. The engine then places it with the draw's transform and the camera, and the frag gets the changed vertex, blended. The mesh's list isn't touched: the GPU changes its own copy, each frame.

## What they read

After the vertex (and a frag's color), a frag or vert takes the game's state, as a view does:

- **Components** are those of the entity the view that draws runs for. `box.tint` in the frag above is the tint of the box being drawn: a view that runs for a thousand boxes draws them all at once, each with its own.
- **Singletons**, the match's or [local](./local-state.md) ones, and `Time`. `time.t`, the match's seconds, is blended between ticks like every float a view sees, so what moves by it moves every frame.
- None is `mut`: the GPU reads the game's state, and changes none of it.
- A [`local` field](./local-state.md) of the match's component or singleton is read like any other: `box.highlight`, which a view set for the entity under the mouse.
- With [`Draw2D.Instanced`/`Draw3D.Instanced`](#instances-from-a-list), an ordinary struct parameter takes the current item of the instance list. It is read-only too.
- A read-only `Texture` parameter receives the explicit image passed to either namespace's `Mesh` or `Instanced` or [`Draw.Blit`](./views.md#targets-and-post-processing). Each stage takes at most one; when both stages take it, they receive the same source. `source.Sample(vertex.uv[, Filter.Point])` uses the same top-left UVs as grid sampling. Local component and singleton `Texture` fields can be sampled too.

The view needn't take them itself. It has to run for entities that have the components, so `with Box` is enough, and it's an error at the mesh draw when it doesn't:

```csharp
view DrawBoxes(with Box, Models models)
{
    Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Shade); // Shade reads Box and Sun
}
```

What a frag sees of the match is what a view sees: [blended between ticks](./views.md#smooth-at-any-tick-rate), so it's smooth at any tick rate.

There are no free-standing value arguments to bind. A number a frag needs lives in a component, a singleton, or the instance struct supplied to `Draw2D.Instanced`/`Draw3D.Instanced` or `Draw3D.Instanced`, and a local singleton is the place for what only this machine's drawing cares about:

```csharp
local singleton Look
{
    float fog = 0.02;
    Color sky = Color(0.5, 0.7, 1);
}

frag Fog(BoxVertex vertex, mut Color color, Look look, Pixel pixel)
{
    float clear = Math.Exp(-pixel.depth * look.fog * 1000);
    color = Color.white * clear + look.sky * (1 - clear);
}
```

Only the fields the code reads go to the GPU: a component's with each entity drawn, and a singleton's once for the draw. A big component costs nothing for the fields a frag doesn't name.

## Textures

A grid of colors in what a frag takes is a texture (see [Textures](./views.md#textures)), and `Sample` reads it:

```csharp
color = models.wood.Sample(vertex.uv);                 // Blended between its cells
color = models.wood.Sample(vertex.uv, Filter.Point);   // The nearest cell
```

`uv` is as a mesh's: (0, 0) is the outer corner of the grid's cell (0, 0) and (1, 1) the outer corner of its last, and past them it reads the cells at the edge. The filter is written there, `Filter.Point` or `Filter.Bilinear`. Verts sample too.

## Where the pixel is

Two more parameters are the engine's:

```csharp
frag Lit(BoxVertex vertex, mut Color color, Pixel pixel, Instance instance, Sun sun)
{
    float3 normal = Math.Normalize(Math.Rotate(instance.transform, vertex.normal));
    float3 toLight = Math.Normalize(sun.position - pixel.world);
    color = Color.white * Math.Max(Math.Dot(normal, toLight), 0.1);
}
```

- `Instance instance`, in a frag or a vert: `instance.transform` is the `float4x4` the draw was given, so `Math.Rotate(instance.transform, vertex.normal)` is a normal in the world.
- `Pixel pixel`, in a frag:
  - `pixel.world`, a `float3`: where on the mesh the pixel is, in the world.
  - `pixel.position`, a `float2`: where it is on the screen, in pixels from the top left, as [`Draw2D.Screen()`](./views.md#on-the-screen)'s.
  - `pixel.depth`: how far it is, from 0 at the camera's near side to 1.
  - `pixel.Discard()` leaves the pixel undrawn, and ends the frag there: for holes, and edges cut out of a texture.

```csharp
frag Leaves(BoxVertex vertex, mut Color color, Models models, Pixel pixel)
{
    color = models.leaves.Sample(vertex.uv);
    if (color.a < 0.5) pixel.Discard();
}
```

The camera isn't given: keep where yours is in a singleton, where the view that calls `Draw3D.Camera` reads it too, and take that.

## What the GPU runs

Code for the GPU is ordinary Tide, less what a GPU doesn't have:

- **It has** bools, ints, floats, vectors, quaternions, matrices, colors, enums and structs of those; locals, `if`, `?:`, and `for` loops that count (`for (int i = 0; i < 8; i++)`), with `break` and `continue`; your functions, methods and operators; all of [`Math`](#math), and what builds its types; and sampling a grid.
- **It doesn't have** lists, maps, text, entities, `this`, spawning and sending, `Draw2D` and `Draw3D`, the GUI, `Devices`, `Debug`, C, tasks, errors, or recursion. `while` and `switch` aren't there yet: write `for` with a bound, and `if`.

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
frag Stars(BoxVertex vertex, mut Color color)
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
    Draw3D.Mesh(models.crate, models.faces, float4x4.Translate(box.pos), Shade);
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

`Draw3D.Instanced(vertices, indices, instances)` and `Draw3D.Instanced(vertices, indices, instances)` draw one mesh for every item in a list, without needing an entity for each. Grass, scenery and visual particles can live in a local list instead.

For a 3D mesh, a `List<float4x4>` supplies its transforms:

```csharp
Draw3D.Instanced(models.crate, models.faces, scenery.transforms);
Draw3D.Instanced(models.crate, models.faces, scenery.transforms, models.wood, Filter.Point);
Draw3D.Instanced(models.crate, models.faces, scenery.transforms, Wave, Shade);
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

frag Grass(BoxVertex vertex, mut Color color, Blade blade, Sun sun)
{
    color = blade.tint * Math.Max(Math.Dot(vertex.normal, -sun.direction), 0.1);
}

view GrassField(Foliage foliage, Models models)
{
    Draw3D.Instanced(models.blade, models.faces, foliage.blades, Bend, Grass);
}
```

- A 3D instance struct's `float4x4 transform` places the mesh and is available as `Instance.transform`. Without that field, the engine uses identity. Set its default to `float4x4.identity` when it has one: an uninitialized matrix is zero.
- For a 2D mesh, the instance list holds an ordinary struct and a vert places its vertices, as `Place` does above. A field named `transform` is ordinary data there; the engine doesn't apply it, and `Instance.transform` remains identity.
- Components still come from the calling view's entity, and singletons are shared by the draw. The list items don't have to be entities and don't add any.
- Only the instance fields the shaders read are sent, beside the 3D transform. Unread fields take no GPU space. The fields that are read follow the same GPU type and attribute limits as components.
- Textures, filters, verts and frags go after the instance list, in the same order as `Mesh`: `Draw3D.Instanced(vertices, indices, instances[, texture[, filter]][, vert][, frag])`. A frag can sample fields of its parameters, or receive an explicit `Texture` through its read-only `Texture` parameter.
- An empty instance list draws nothing. Items draw in list order, in one instanced draw; adjacent compatible calls can join the same run.

An ordinary instance parameter is supplied only by `Draw2D.Instanced`/`Draw3D.Instanced` or `Draw3D.Instanced`: using that shader with `Mesh`, or with a list of another type, is an error.

## Looking at what's generated

`tide shaders` prints each shader Tide writes for the game, as GLSL and as WGSL: one for each combination of vertex struct, instance data, vert and frag a mesh draw uses.
