---
description: "Use Tide's deterministic vector, matrix and quaternion math, with noise, random generators and seeds shared by the match."
---

# Math

Tide's math follows Unity.Mathematics, with Tide's naming: functions live on `Math`, in PascalCase. It's all deterministic: the same inputs give the same bits on every platform (see [Determinism](../multiplayer/determinism.md)).

```csharp
quaternion spin = quaternion.AxisAngle(float3(0, 1, 0), input.turn * time.dt);
trs.rotation = Math.Mul(trs.rotation, spin);
float3 forward = Math.Rotate(trs.rotation, float3(0, 0, 1));
trs.position += Math.Normalize(forward) * 5 * time.dt;
float2 flat = trs.position.xz;
```

## Types

| Type | What it is |
|---|---|
| `float2`, `float3`, `float4` | Float vectors |
| `int2`, `int3`, `int4` | Int vectors |
| `quaternion` | A rotation |
| `float2x2`, `float3x3`, `float4x4` | Matrices, stored column by column |

They're lowercase built-in value types, like `float`. Angles are in radians: `Math.Radians(degrees)` and `Math.Degrees(radians)` convert.

## Building values

- Vectors take any mix of scalars and vectors that adds up to their size, `float4(v.xy, 0, 1)`, or one scalar for every component, `float3(1)`.
- `float3(i)` and `int3(f)` convert between int and float vectors, and `int(x)` and `float(x)` between scalars. Float to int truncates toward zero, saturates at the int range, and turns NaN into 0.
- An int vector converts to a float vector of the same size by itself, like `int` to `float`.
- `quaternion(x, y, z, w)`, `quaternion(float4)` and `quaternion(float3x3)`.
- Matrices take one vector per column, `float3x3(c0, c1, c2)`, or all their numbers row by row: in `float2x2(1, 2, 3, 4)`, `1, 2` is the first row. Also `float3x3(quaternion)`, and `float4x4(rotation, translation)` from a `float3x3` and a `float3`.

## Members

- Vectors have `x`, `y`, `z` and `w`.
- Swizzles read any combination: `v.xz`, `v.zyx`, `v.xxyy`. They can be assigned too, as long as no component repeats: `v.xz = float2(1, 2)`.
- A `quaternion`'s `value` is a `float4`, with (x, y, z) as the vector part.
- A matrix's columns are `c0` to `c3`.

## Constants

`Math.PI`, `Math.TAU`, `Math.E`, `quaternion.identity`, `float2x2.identity`, `float3x3.identity` and `float4x4.identity`.

And these build values:

- `quaternion.AxisAngle(axis, angle)`
- `quaternion.Euler(radians)`: Z first, then X, then Y, Unity's default order
- `quaternion.LookRotation(forward, up)`
- `float4x4.TRS(translation, rotation, scale)`
- `float4x4.Translate(translation)`
- `float4x4.PerspectiveFov(verticalFov, aspect, near, far)` and `float4x4.Ortho(width, height, near, far)`: projections for a 3D camera, as Unity.Mathematics' (see [Any projection](../game/views.md#any-projection))

## Operators

- `+ - * /` work component by component on vectors, and `%` on int vectors. A scalar widens to the vector's size, and int to float, so `v * 2 + 1` works.
- Matrices have `+` and `-` with their own type, and `*` and `/` by a number. There's no `*` between matrices, or between a matrix and a vector: that's `Math.Mul`.
- Quaternions have no operators: combine rotations with `Math.Mul`, and rotate vectors with `Math.Rotate`.
- Comparisons and `==` work on scalars only.

## Functions

**Component by component**, on numbers, vectors and colors:

`Abs`, `Sign`, `Min`, `Max`, `Clamp` (ints too), `Floor`, `Ceil`, `Round` (ties to even), `Trunc`, `Frac`, `Sqrt`, `Rsqrt`, `Saturate`, `Radians`, `Degrees`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Exp2`, `Log`, `Log2`, `Log10`, `Pow`, `Step`, `Lerp`, `Unlerp`, `SmoothStep`.

A color goes through them channel by channel, alpha too, with other colors and with numbers, which count for every channel: `Math.Lerp(cold, hot, heat)` is the color that far from `cold` to `hot`, and `Math.Saturate(color)` keeps each channel from 0 to 1. A color and a vector don't mix in one call.

**Vectors:**

`Dot`, `Cross`, `Length`, `LengthSq`, `Distance`, `DistanceSq`, `Normalize` (zero for a zero vector, never NaN), `Reflect`, `Csum`, `Cmin`, `Cmax`.

**Quaternions:**

`Mul` (two rotations, or a rotation and a `float3`, which it rotates), `Rotate`, `Inverse`, `Conjugate`, `Normalize` (the identity for a zero quaternion), `Dot`, `Slerp`, `Nlerp`, `Forward`, `Up`, `Right`, `Angle`.

**Matrices:**

`Mul` (two matrices, or a matrix and a vector of its size), `Transpose`, `Inverse`, `Determinant`, and for `float4x4`, `Transform` (a point) and `Rotate` (a direction).

Each button below plots a function of `Math` from 0 to 1. The white dot goes along the curve, and the teal one moves by the same number, to see what it does to something that moves.

::: tide-example math Curves from Math

<<< @/examples/math.tide

:::

**Hashing:**

`Math.Hash(value)` turns an `int`, `long`, `int2`, `int3` or `int4` into a random-looking int from 0 to 2147483647. The same value always gives the same number, on every machine. It keeps no state, so it works anywhere, parallel loops included:

```csharp
// About one cell in four starts as sand, the same cells everywhere.
if (Math.Hash(int2(x, y)) % 4 == 0) field.cells[x, y] = Material.Sand;
```

Put a seed in the value to get other numbers for the same cell: `Math.Hash(int3(x, y, seed))`. It's xxHash32, so every bit of the value changes every bit of the result. With the match's seed, [`Time.seed`](#the-match-s-seed), no two matches get the same: `Math.Hash(int3(x, y, time.seed))`.

It gives the same numbers in a [frag or vert](../game/shaders.md#math), so a pattern drawn from it is the same on every GPU. Only a `long`'s hash stays on the CPU.

## Noise

Noise is random that's smooth: a number for every point, with nearby points nearby. It's what terrain, clouds, water and wear are made of. Each function takes a `float2` or a `float3` and a seed, which a call can leave out (it's 0 then), and keeps no state, so it works anywhere a hash does: in a system, a parallel loop or a frag.

| Function | Gives | What it's like |
|---|---|---|
| `Math.Perlin(point[, seed])` | A float from -1 to 1 | Gradient noise: smooth hills, 0 at every whole number, so the features are about a unit across |
| `Math.Simplex(point[, seed])` | A float from about -1 to 1 | The same kind of hills with no grid showing through, cheaper in 3D |
| `Math.Cellular(point[, seed])` | A `float2`: the distances to the nearest feature point and the next, from 0 to about 1.4 | Cells, like a dry lake bed or a cobbled road: there's a feature point in every unit cell, `.x` is 0 on one, and `.y - .x` is 0 on the borders between cells |

The point's scale sets the features' size: `Math.Perlin(position * 0.1)` has hills about ten units across. Octaves add detail: each twice as fine and half as strong, summed.

```csharp
// Terrain: three octaves of Perlin noise, from -1 to 1, the same on every machine
float Height(float2 at, int seed)
{
    return Math.Perlin(at * 0.05, seed) * 0.57 + Math.Perlin(at * 0.1, seed + 1) * 0.29 + Math.Perlin(at * 0.2, seed + 2) * 0.14;
}
```

Every one of them is built from the basic operations and `Math.Hash`, so a point gives the same bits on every machine: noise can be in the match, and a cell of terrain is the same for every player. Another seed is another field of noise, so [`Time.seed`](#the-match-s-seed) gives each match its own world. They go to the GPU too (next), with the same cells, so a frag that shades a tile by its noise shades it as the match computed it.

::: tide-example noise Noise, drawn by the GPU

<<< @/examples/noise.tide

:::

## Random numbers

`Math.Hash` keeps no state. A `Random` does: it's a random number generator whose state is 8 bytes of data, which live wherever the game puts them. Each `Next` call gives a number and steps the state.

```csharp
component Wanderer
{
    Random random;
    float2 heading;
}

system Wander(mut Wanderer wanderer, mut Body body)
{
    // About twice a second, another way to go.
    if (wanderer.random.NextInt(30) == 0) wanderer.heading = wanderer.random.NextFloat2Direction();
    body.velocity = wanderer.heading * 3;
}
```

| Method | What it gives |
|---|---|
| `NextInt()` | An int from 0 to 2147483647, like `Math.Hash`'s, so `%` never goes negative |
| `NextInt(max)` | From 0 up to `max`, `max` excluded: `NextInt(6)` is 0 to 5 |
| `NextInt(min, max)` | From `min` up to `max`, `max` excluded: `NextInt(1, 7)` is a die |
| `NextFloat()` | A float from 0 up to 1, 1 excluded |
| `NextFloat(max)` | From 0 up to `max`, `max` excluded |
| `NextFloat(min, max)` | From `min` up to `max`, `max` excluded |
| `NextBool()` | True or false, each as likely |
| `NextFloat2()`, `NextFloat3()` | A vector with each component from 0 up to 1 |
| `NextFloat2(min, max)`, `NextFloat3(min, max)` | Each component from `min`'s up to `max`'s: a point in a rectangle or a box |
| `NextFloat2Direction()`, `NextFloat3Direction()` | A vector of length 1, every direction as likely |
| `NextQuaternionRotation()` | A rotation, every one as likely |

They follow Unity.Mathematics' `Random`. A range with nothing in it gives its start: `NextInt(0)` is 0, and `NextFloat(2, 1)` is 2.

### Where a generator lives

A `Random` is a value, like a `float3`: it goes in a field or a local, and where it is decides whose numbers they are.

- **In a component**, every entity has a sequence of its own. A system that rolls for its entities still runs them on several threads at once, each from its own generator, with the same results.
- **In a singleton**, it's the match's: one sequence, for what the whole match shares, like a deck.
- **In [local state](../game/local-state.md)**, it's this machine's, for effects: sparks, shakes, anything the match doesn't see.
- **In a local**, it lasts as long as the code that made it: `mut var random = Random(seed);`.

```csharp
singleton Deck
{
    Random random;
    List<int> cards;
}

system Shuffle(mut Deck deck)
{
    // Each card trades places with one before it, or stays.
    for (var i = deck.cards.count - 1; i > 0; i--)
    {
        var other = deck.random.NextInt(i + 1);
        var card = deck.cards[i];
        deck.cards[i] = deck.cards[other];
        deck.cards[other] = card;
    }
}
```

A `Next` call changes the generator, so the code that rolls needs it `mut`: `mut Wanderer wanderer`, `mut Deck deck`, a `mut var`. That's all a system says, and the rest follows as for any other data: two systems that roll with the same component run one after the other, a generator in the match is in every snapshot, and a tick a client runs again after a wrong prediction rolls the same numbers again.

A function that rolls takes the generator as `mut`:

```csharp
float2 Scatter(mut Random random, float spread)
{
    return random.NextFloat2Direction() * random.NextFloat(spread);
}
```

Rolls run in the order they're written, left to right: in `float2(random.NextFloat(), random.NextFloat())`, x is rolled first, on every platform.

### Seeds

A generator the game leaves alone is seeded by the engine, so there's nothing to do for the common case:

- A `Random` field of a component is seeded as its entity spawns (or as the component is added), from the match's seed and the entity. Two entities spawned alike in the same tick still get sequences of their own.
- A `Random` field of a singleton is seeded as the match starts, from the match's seed.
- In local state, each gets a sequence of its own too, the same every run: a local component's, a local singleton's, and a `local Random` field of a match component, which is the entity's own on this machine.

`Random(seed)` makes one for a seed of the game's, an `int` or a `long`: the same seed, the same numbers. Seeds next to each other (1, 2, 3) give sequences that have nothing to do with each other.

```csharp
Spawn(Wanderer { random = Random(7) }); // Every match, the same wanderer
```

The view below makes a generator from the slider's seed every frame, and rolls every dot's place, size and color with it. The dots stay where they are, since the same seed rolls the same numbers; another seed gives other dots, and going back gives the first ones again.

::: tide-example random The same seed, the same numbers

<<< @/examples/random.tide

:::

A `Random` has one field, `long state`, which is all there is to it: copy it to keep where a sequence is, and set it to go on from there. A state of 0 is a generator nothing seeded yet. It still rolls, but the same numbers as every other one at 0: what a `Random` in a list starts as, since the engine seeds the fields of components and singletons, and no further.

### The match's seed

`Time.seed` is an `int` from 0 to 2147483647 that the machine starting a match picks, so no two matches go the same (but in a game run [without a window](../tools/cli.md#without-a-window), where it's 0, so the run repeats). `tide run --seed 7` picks it, to play a match's rolls again or to try others. It's part of the match, like everything in `Time`: a player who joins gets it with the world, and nobody sends it. The engine seeds the match's generators from it, and a game reads it where it wants numbers that keep no state:

```csharp
// The same cave on every machine, and another one every match.
if (Math.Hash(int3(x, y, time.seed)) % 4 == 0) field.cells[x, y] = Material.Rock;
```

It's 0 where nothing picks one: tests, benchmarks, and a world a C host starts with `tide_world_init`. A run there repeats exactly.

::: warning Every player can read it
The seed is in the world, and every player's machine has the whole world for now. A game whose players mustn't know what comes next, like the order of a deck, can't keep that from someone who reads their own machine's memory yet. Scenes only some players see are coming (see [Networking](../multiplayer/networking.md)).
:::

### Not on the GPU

A [frag or vert](../game/shaders.md) can't roll: a generator's state is the CPU's. Roll in the view and keep the number in a field the shader reads.

## Forgiving with bad values

Values can come from other players' input, so math avoids spreading NaN where it can:

- `Math.Clamp` always returns a value in range: NaN gives the lower bound.
- `Math.Min` and `Math.Max` with one NaN argument return the other.
- `Math.Normalize` gives zero for a vector too short to have a direction, and the identity for a quaternion that short. Dividing a zero vector by its length would give NaN.

That last one is the first thing most games run into: a move is zero while no key is held.

```csharp
// No key held: move is zero, and the body stays where it is.
float3 move = Math.Normalize(float3(input.horizontal, 0, input.vertical));
body.position += move * 5 * time.dt;
```

Too short means a length under about 1e-19. Anything longer comes out at length 1.

::: info
Unity.Mathematics has two, `normalize` and `normalizesafe`. Tide has one, and it's the safe one: there's no `Math.NormalizeSafe`.
:::

Everywhere else, results match Unity.Mathematics. The transcendental functions (`Sin`, `Exp`, `Pow` and the others) are Tide's own, accurate to about 1 ulp, and give the same bits everywhere.

## On the GPU

Everything on this page works in a [frag or vert](../game/shaders.md#math) too, unless it makes no sense there: the types, what builds them, and every `Math` function, quaternions and matrix inverses included. The same function runs in a system and in a frag, and gives the same answer up to how the GPU rounds.

- `Math.Hash` is integers, so the GPU's number is exactly the CPU's.
- `Math.Perlin`, `Simplex` and `Cellular` take their cells and gradients from `Math.Hash`, so a frag draws the cells the match computed, and their floats come out as the GPU rounds, like any.
- Floats follow the same formulas with the GPU's own rounding, which differs from one GPU to the next, most for big numbers. They're only ever drawn: nothing a GPU works out reaches the match.
- It's forgiving in the same ways: `Normalize`, `Min`, `Max`, `Clamp` and `Saturate` do there what [Forgiving with bad values](#forgiving-with-bad-values) says.
- `Math.Hash` of a `long` is the one function that isn't there, as a GPU's ints are 32 bits. `Math.Hash(int2(int(x), int(x >> 32)))` is the same number as `Math.Hash(x)`: split the long on the CPU, and hash its halves in the frag.
