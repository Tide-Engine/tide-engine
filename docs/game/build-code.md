---
description: "Make and edit Tide assets and constants with code that runs while the game builds: outline an image, bake a curve, generate a mesh, its levels of detail or a sound."
---

# Build code

Build code is Tide code that runs while the game builds. It makes an [asset](./assets.md) or a constant: an image with an outline drawn on it, a table of numbers, a mesh, its levels of detail, a sound. What it made goes into the build as a file's contents do, so every machine has the same value and the game spends no time on it while it runs.

```csharp
build void Outline(mut Grid2<Color> image, Color color)
{
    for (int y = 0; y < image.size.y; y++)
    {
        image[int2(0, y)] = color;
        image[int2(image.size.x - 1, y)] = color;
    }
}

asset Texture hero = "art/hero.png"
{
    Outline(hero, Color.black);
}
```

The block after the path is the asset's code. In it, `hero` is the image as the file has it, and what the code leaves there is what the game is built with.

## An asset's code

Any asset of these types can have code, with a path or without one:

| Asset | What its code edits |
|---|---|
| `string`, `List<byte>` | The text or the bytes |
| `Grid2<Color>` | The grid. The code can give it a new size with `Grid2(width, height)` |
| `Texture` | Its pixels, as a `Grid2<Color>`, which become the texture as a file's would |
| `Mesh<V>` | The mesh, which the code replaces with `Mesh(vertices, indices)`, and its `levels` |
| `Clip` | A `ClipData`: its `rate`, its `channels` (1 or 2) and its `samples`, interleaved |

An asset with no path starts empty:

```csharp
asset Grid2<Color> checker
{
    checker = Grid2(64, 64);
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            if ((x / 8 + y / 8) % 2 == 0) checker[int2(x, y)] = Color.white;
        }
    }
}

asset Clip beep
{
    beep.rate = 48000;
    beep.channels = 1;
    for (int i = 0; i < 4800; i++)
    {
        beep.samples.Add(Math.Sin(i * 0.06) * 0.5);
    }
}
```

An image must have a size when its code is done, and every pixel and sample must be a number, not NaN or infinity. A mesh's triangles must name vertices it has.

Code can read other assets, and what other code made: a body that reads another asset with code runs after it.

## Constants

A constant can be made by build code too. One with a body is its code's, and so is one whose value calls a function:

```csharp
const List<float> CURVE
{
    for (int i = 0; i < 64; i++)
    {
        CURVE.Add(Math.Sin(i * 0.1));
    }
}

const float SCALE = Twice(0.25);

float Twice(float x)
{
    return x * 2;
}
```

A constant build code makes can be a list of plain values (numbers, vectors, colors and structs of those) as well as what any constant can be. Code and field defaults read it. A `case` label, a bound or a setting can't, as its value isn't known until the game builds, and neither can a frag or a vert yet.

## Build functions

Build code can call any function that doesn't need a running game. `build` marks a function that only runs while the game builds: anything but build code calling it is an error, and its body is checked as build code where it's written. A build function can take a grid, `mut` or not, which other CPU functions can't.

Build code runs before there's a game. It can't use a world (queries, `Spawn`, `Send`), `Devices`, drawing, audio, files beyond the assets it names, tasks, or the match's random numbers. `Math.Hash`, or a `Random` the code makes with a seed, gives it random numbers. It can't call [C](../c/functions.md) yet either. The error points at the asset, constant or build function, with a note at the function that does it.

## Levels of detail

A mesh asset's code also has `levels`, which it can fill with its own [levels of detail](./assets.md#levels-of-detail) instead of `[LOD]`'s. Each `MeshLevel` is triangles of the mesh's own vertices, and its error: how far that level's surface is from the full mesh's, in the mesh's units. Draws pick among them just as they pick `[LOD]` levels.

```csharp
asset Mesh<Corner> rock = "art/rock.glb"
{
    levels.Add(MeshLevel { indices = Simplify(rock, 0.5), error = 0.02 });
    levels.Add(MeshLevel { indices = Simplify(rock, 0.1), error = 0.2 });
}
```

A mesh has at most 7 levels after the full one, each error at least the one before it. An asset can have `[LOD]` or levels from its code, not both.

## How it runs

tidec turns the game's build code into a small program, compiles it with the engine's sources, and runs it on the machine that builds, whatever the game is built for. `tide build` and `tide run` do this with the compiler they build the game with. What it made is kept in the game's `.tide/`, by everything that made it: the code, what it calls, the types it uses and the files it reads. A build where nothing of that changed runs no build code. Under `tide run`, saving a file that build code reads makes it again.

Build code that runs for more than ten minutes is stopped, and the build fails. What it logs with `Debug.Log` shows as the game builds.

The details, and which choices are still provisional, are in the [specification](../spec.md#build-code).
