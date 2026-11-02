---
description: "Create particle effects in Tide code with bounded emitters, custom spawn and update functions, and instanced 2D sprites or 3D billboards."
---

# Particles

`particles`, in namespace `Particles`, supplies reusable particle storage, emission, lifetime updates and drawing. Effects are ordinary Tide functions: write how a particle starts, how it moves and what it looks like. People and agents can edit the same source, with normal completion, error messages, version control and hot reload.

```sh
tide add particles
```

Keep visual effects in [local state](../game/local-state.md), usually an `Emitter` field of a local singleton or a local field attached to an entity. Each emitter owns an ordinary `List<Particle>`, a random generator and its fractional emission count. There is no entity per particle and no global effect manager.

## Write an effect

An initializer receives a new particle and its emitter's random generator. It can use ordinary math, conditions, helper functions and captured values to choose a shape, velocity, lifetime and appearance:

```csharp
void Spark(mut Particles.Particle particle, mut Random random)
{
    float2 direction = random.NextFloat2Direction();
    float speed = random.NextFloat(1, 3);
    particle.velocity = float3(direction * speed, 0);
    particle.lifetime = random.NextFloat(0.4, 0.9);
    particle.size = float2(0.08);
    particle.endSize = float2(0);
    particle.color = Color(1, 0.75, 0.2);
    particle.endColor = Color(1, 0.3, 0.1, 0);
}

local singleton Effects
{
    Particles.Emitter sparks;
}
```

`Particles.Burst(effects.sparks, 40, Spark)` emits immediately. `Particles.Emit(effects.sparks, 80, dt, Spark)` emits 80 per second, carrying fractional counts between calls. Both return the number actually added. Call emission once for each interval you want it to cover.

For an effect at a particular place, capture that place in a lambda:

```csharp
void SparkAt(mut Particles.Particle particle, mut Random random, float3 position)
{
    Spark(particle, random);
    particle.position = position;
}

Particles.Burst(effects.sparks, 40,
    (mut Particles.Particle particle, mut Random random) => SparkAt(particle, random, hitPosition));
```

The initializer runs for each new particle. Changing its code or the parameters it captures affects later emissions; existing particles retain the values already stored in them.

The example combines a continuous fountain, smoke with a custom movement function and bursts at the pointer. Stop flow to let the existing particles finish, change the emission rate, or clear both emitters. Its source is the complete effect.

::: tide-example particles Sparks and smoke in code

<<< @/examples/particles.tide

:::

## Move and expire particles

Call `Update` once per frame, with the same elapsed time used for emission:

```csharp
float dt = Math.Min(Frame.dt, 0.05);
Particles.Update(effects.sparks, dt, acceleration: float3(0, -4, 0), drag: 0.8);
Particles.Emit(effects.sparks, 80, dt, Spark);
```

Updating before emission leaves new particles at age zero for their first draw. `Frame.dt` is the frame's real length; the clamp above is the game's choice to avoid a large leap after a stall. Zero, negative or nonfinite time does not advance the effect.

The standard update advances age, removes particles that have reached their lifetime, applies acceleration and drag, moves position and advances rotation. A custom update replaces the movement part, while the package still advances age and removes expired particles:

```csharp
void Drift(mut Particles.Particle particle, float dt)
{
    Particles.Integrate(particle, dt, acceleration: float3(0, 0.4, 0), drag: 0.3);
    particle.position.x += Math.Sin(particle.age * 5 + particle.custom.x) * dt * 0.2;
}

Particles.Update(effects.smoke, dt, Drift);
```

This is also where a game can implement attraction, visual bounces, noise or motion along a path. `custom` is a `float4` for effect-specific values such as a phase, a starting height or a sprite-frame index. The particle list is public, so game code can inspect or change it directly.

## Particle data

| Field | Meaning |
|---|---|
| `position`, `velocity` | `float3` position and units per second; use `z = 0` for 2D |
| `age`, `lifetime` | Seconds lived and total lifetime; lifetime defaults to 1 |
| `size`, `endSize` | `float2` width and height at birth and death; defaults are `(0.1, 0.1)` and `(0, 0)` |
| `color`, `endColor` | Color at birth and death; defaults are white and transparent white |
| `rotation`, `angularVelocity` | Radians and radians per second |
| `custom` | Four numbers for the game's own effect code |

`Age(particle)` returns normalized lifetime progress, from 0 to 1. `Size(particle)` interpolates the size endpoints and keeps the result nonnegative. `Tint(particle)` blends alpha-weighted colors, so fading to a transparent endpoint keeps the visible hue instead of darkening it. Changing those endpoints changes the remaining appearance of a live particle. For another curve, calculate it in your own update or shader with ordinary functions such as `Math.SmoothStep`.

## Draw in 2D or 3D

Keep a `Particles.Renderer` in local state alongside the emitters. It reuses the quad geometry and rendering lists. `DrawSprites` uses the current [2D camera or screen coordinates](../game/cameras.md), drawing `position.xy` as soft discs:

```csharp
Particles.DrawSprites(effects.renderer, effects.smoke.particles);
Particles.DrawSprites(effects.renderer, effects.sparks.particles, Blend.Add);
```

An optional texture between the particle list and blend mode draws full textured quads instead. `size` and `endSize` are the full width and height in the current drawing units, with rotation around the center. Positive 2D rotation turns from `+x` toward `+y`: counterclockwise under a world camera, clockwise in top-left screen coordinates. Texture coordinate `(0, 0)` stays at the top left in both modes:

```csharp
Particles.DrawSprites(effects.renderer, effects.leaves.particles, leafTexture);
```

For 3D, supply the position and rotation of the camera already selected for drawing. Each quad faces that camera; its size is in world units, and positive rotation turns from camera-right toward camera-up. The helper tests against opaque geometry's depth without writing depth itself, so draw opaque objects first:

```csharp
Draw.SetCamera(eye, cameraRotation, 50deg);
Particles.DrawBillboards(effects.renderer, effects.smoke.particles, eye, cameraRotation);
Particles.DrawBillboards(effects.renderer, effects.sparks.particles, eye, cameraRotation,
                 Blend.Add, sort: false);
```

`DrawBillboards` sorts the particles back to front by camera-forward distance unless `sort: false` is supplied. Particle positions must be world-space positions, with no additional `Draw.Transform` around the draw: inherited transforms are not part of the sort key. Sorting belongs to that draw only; overlapping transparent effects in separate calls are not sorted together. The particle simulation list keeps its order. Additive effects can usually skip sorting.

The texture overload places its texture after `cameraRotation`. Both drawing functions also have an overload with a `RenderTarget` first. For example, draw into [Render3D](./render3d.md)'s HDR scene after its transparent pass and before its effects/presentation:

```csharp
[After(Render3D.Stages.Transparent), Before(Render3D.Stages.PostPrepare)]
view DrawSmoke(mut Effects effects, Render3D.Renderer scene)
{
    if (!scene.enabled || !scene.ready) return;
    Particles.DrawBillboards(scene.hdr, effects.renderer, effects.smoke.particles,
                     scene.camera.position, scene.camera.rotation);
}
```

The helpers restore the destination's previous blend, depth and culling settings. To use another shader or draw a mesh per particle, use the ordinary list with [`Draw.Instanced`](../game/shaders.md#instances-from-a-list). A custom vert or frag can receive `Particles.Particle` as its instance parameter, including `custom`, and use `Age`, `Size` and `Tint` for lifetime effects.

## Capacity, seeds and timing

`emitter.capacity` defaults to 4,096 and is clamped to zero through 1,048,576 when used. Reducing it trims the end of the existing list on the next add, burst or valid emission/update. When an emitter is full, new emissions are dropped, without running their initializers. Emission never accumulates a burst of missed whole particles to release later. Its fractional count survives a pause. `Clear(emitter)` removes its particles and fractional emission count; seed it explicitly when a repeatable effect is useful:

```csharp
Particles.Clear(effects.sparks);
effects.sparks.random = Random(42);
```

The same seed, callbacks and sequence of updates and emissions reproduce the same CPU values. Different frame lengths need not produce the same trajectory. Local particles are outside match snapshots, rollback, hashes and network transfers. Use [local handlers](../game/events.md#local-handlers) to start visual effects from match events; keep damage and gameplay collision in match code.

`Add(emitter, particle)` adds a fully authored particle and returns whether it was accepted. Invalid particles, including nonfinite fields, negative age or age at/past lifetime, are rejected; `Update` also removes particles made invalid by direct edits or a callback. `Clear` keeps the random generator's current sequence. Survivors keep their relative order, but particles have no stable handle: removal compacts the list, so retain game-specific identity in your own data when needed.

## Current scope

Simulation runs on the CPU, with instanced drawing on the GPU. There is no compute-shader simulation, built-in physics collision, ribbon/trail topology, effect-file format or editor graph. Spawn shapes, forces and lifetime curves are functions in your game; custom shaders can use the particle fields directly.
