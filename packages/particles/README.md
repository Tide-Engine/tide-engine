# Particles

Particle effects written in ordinary Tide, in namespace `Particles`: bounded
emitters, bursts and continuous emission, lifetime updates, custom spawn and
movement functions, and instanced 2D sprites or 3D camera-facing billboards.
There is no C library or required renderer package. Games that omit it compile
none of it.

```sh
tide add particles
```

Keep an `Emitter` and a `Particles.Renderer` in local state. Define a function
`void Spark(mut Particle particle, mut Random random)` to set each newborn's
position, velocity, lifetime, size and color. In a view:

```csharp
float dt = Math.Min(Frame.dt, 0.05);
Particles.Update(effects.sparks, dt, acceleration: float3(0, -4, 0), drag: 0.5);
Particles.Emit(effects.sparks, 80, dt, Spark);
Particles.DrawSprites(effects.renderer, effects.sparks.particles, Blend.Add);
```

`Burst(emitter, count, initialize)` emits immediately. An `Update` overload
takes your movement function, and the emitter's public particle list supports
direct edits. Seed `emitter.random` explicitly for repeatable CPU effects.

See the [Particles guide](https://tide-engine.dev/packages/particles) for the
API, capacity and timing rules, textured and 3D drawing, and the complete
editable sparks/smoke example in `docs/examples/particles.tide`.
