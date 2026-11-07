---
description: "Make and process Tide assets and constants with code that runs while the game builds: outline an image, bake a curve, generate a mesh, its levels of detail or a sound."
---

# Build code

Build code is Tide code that runs while the game builds. It makes an [asset](./assets.md) or a constant: an image with an outline drawn on it, a table of numbers, a mesh, its levels of detail, a sound. What it made goes into the build as a file's contents do, so every machine has the same value and the game spends no time on it while it runs.

Build code runs through the installed tools, including when building a web game
with `tide run --web`. The browser playground cannot run its native build program.

An asset's value is either its file's path or a call of code that makes it. That code is ordinary Tide: functions that return the value.

```csharp
void Outline(mut Grid2<Color> image, Color color)
{
    for (int y = 0; y < image.size.y; y++)
    {
        image[int2(0, y)] = color;
        image[int2(image.size.x - 1, y)] = color;
    }
}

Grid2<Color> Outlined(Grid2<Color> source, Color color)
{
    mut Grid2<Color> image = source.Copy();
    Outline(image, color);
    return image;
}

asset Grid2<Color> heroSource = "art/hero.png";
asset Texture hero = Outlined(heroSource, Color.black);
```

`heroSource` is the file's pixels, and `hero` is the texture the game draws: the outlined copy. Functions that return and change grids are what any code can write (see [Grids in code](../language/grids.md#grids-in-code)).

## What code makes

An asset of these types can be made by code, which returns:

| Asset | What its code returns |
|---|---|
| `string`, `List<byte>` | The text or the bytes |
| `Grid2<Color>` | The grid, with a size: `Grid2(width, height)` |
| `Texture` | Its pixels, a `Grid2<Color>`, which become the texture as a file's would |
| `Mesh<V>` | `Mesh(vertices, indices)`, or with [levels of detail](#levels-of-detail) of its own |
| `Clip` | A `ClipData`: its `rate`, its `channels` (1 or 2) and its `samples`, interleaved |
| `ClipData` | The same, kept as data the game reads |

Code can make an asset from nothing:

```csharp
Grid2<Color> Checker(int size)
{
    mut Grid2<Color> image = Grid2(size, size);
    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            if ((x / 8 + y / 8) % 2 == 0) image[int2(x, y)] = Color.white;
        }
    }
    return image;
}

ClipData Beep()
{
    mut ClipData beep = ClipData { rate = 48000, channels = 1 };
    for (int i = 0; i < 4800; i++)
    {
        beep.samples.Add(Math.Sin(i * 0.06) * 0.5);
    }
    return beep;
}

asset Texture board = Checker(64);
asset Clip beep = Beep();
```

An image must have a size, and every pixel and sample must be a number, not NaN or infinity. A mesh's triangles must name vertices it has.

## Files code reads

A file that code changes is declared as the data the code reads: an image as a `Grid2<Color>`, a sound as a `ClipData`, a mesh as a `Mesh<V>`, text as a `string`. The asset code makes from it has a name of its own.

```csharp
asset ClipData stepSource = "sfx/step.wav";
asset Clip step = Quieter(stepSource, 0.5);

ClipData Quieter(ClipData source, float scale)
{
    mut ClipData quiet = source;
    for (int i = 0; i < quiet.samples.count; i++)
    {
        quiet.samples[i] *= scale;
    }
    return quiet;
}
```

Code can read other assets and what other code made: an asset whose code reads another asset made by code is made after it. A game built without what nothing reads (`--strip-unused-assets`, as release builds are) leaves out a file only build code reads.

## Constants

A constant whose value calls a function is made by build code too:

```csharp
const List<float> CURVE = Curve(64);
const float SCALE = Twice(0.25);

List<float> Curve(int count)
{
    mut List<float> curve = [];
    for (int i = 0; i < count; i++)
    {
        curve.Add(Math.Sin(i * 0.1));
    }
    return curve;
}

float Twice(float x)
{
    return x * 2;
}
```

A constant build code makes can be a list of plain values (numbers, vectors, colors and structs of those) as well as what any constant can be. Code and field defaults read it. A `case` label, a bound or a setting can't, as its value isn't known until the game builds, and neither can a frag or a vert yet.

## Build functions

Build code can call any function that doesn't need a running game. `build` marks a function that only runs while the game builds: anything but build code calling it is an error, and its body is checked as build code where it's written. It follows the same rules as any other code.

Build code runs before there's a game. It can't use a world (queries, `Spawn`, `Send`), `Devices`, drawing, audio, files beyond the assets it names, tasks, or the match's random numbers. `Math.Hash`, or a `Random` the code makes with a seed, gives it random numbers. It can't call [C](../c/functions.md) yet either. The error points at the asset, constant or build function, with a note at the function that does it.

## Levels of detail

A mesh can have levels of detail of its own instead of `[LOD]`'s: `Mesh(vertices, indices, levels)`. Each `MeshLevel` is triangles of the mesh's own vertices, and its error: how far that level's surface is from the full mesh's, in the mesh's units. Draws pick among them just as they pick `[LOD]` levels.

```csharp
asset Mesh<Corner> rockSource = "art/rock.glb";
asset Mesh<Corner> rock = Leveled(rockSource);

build Mesh<Corner> Leveled(Mesh<Corner> source)
{
    mut List<MeshLevel> levels = [];
    levels.Add(MeshLevel { indices = Simplify(source, 0.5), error = 0.02 });
    levels.Add(MeshLevel { indices = Simplify(source, 0.1), error = 0.2 });
    return Mesh(source.vertices, source.indices, levels);
}
```

A mesh has at most 7 levels after the full one, each error at least the one before it. An asset can have `[LOD]` or levels from its code, not both. Only build code makes a mesh with levels of its own; a mesh made as the game runs gets its levels from the draw's `lod:` (see [Levels of detail](./assets.md#levels-of-detail)).

## How it runs

tidec turns the game's build code into a small program, compiles it with the engine's sources, and runs it on the machine that builds, whatever the game is built for. `tide build` and `tide run` do this with the compiler they build the game with. What it made is kept in the game's `.tide/`, by everything that made it: the code, what it calls, the types it uses and the files it reads. A build where nothing of that changed runs no build code. Under `tide run`, saving a file that build code reads makes it again.

Build code that runs for more than ten minutes is stopped, and the build fails. What it logs with `Debug.Log` shows as the game builds.

The details, and which choices are still provisional, are in the [specification](../spec.md#build-code).
