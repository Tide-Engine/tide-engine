# Audio

Sound is presentation, like drawing: it's this machine's, and nothing of it is in the match. A view says what should be sounding as it says what should be drawn, and a local handler plays a sound when the match sends an event. Nothing in the simulation hears any of it, so a match is the same on a machine with its speakers off.

::: info
What's here so far: clips as assets, `Audio.Play`, `Sound` fields and the listener. Buses, `audio` declarations that filter and make sound, and the device backends are coming; see the [spec](../spec.md#audio) for the whole design. Until the backends land, a game with a window is silent, and a run with no window captures what it played (below).
:::

## Clips

A sound is a file the game is built with, declared as an [asset](assets.md):

```csharp
asset Clip shot = "sfx/shot.wav";
asset List<Clip> hits = "sfx/hit_*.wav";
asset Map<string, Clip> voices = "voice/*.wav";
```

A `Clip` is a WAV of PCM (8, 16, 24 or 32 bits) or floats, of one or two channels, at any rate. The build checks the file decodes, and the pack keeps it as it is: the engine decodes it the first time it's played, and keeps that. Ogg Vorbis is to come.

A `Clip` is a value: keep it in a local field, pass it to a function, pick one from the glob's list or map. It names the declaration's file, so under `tide run` a saved file plays through every copy the game kept. The match can't use one: a `Clip` is this machine's, as a `Texture` is.

## Playing a sound

`Audio.Play` starts a clip once, from its start. The mixer owns it from then on: nothing comes back, and nothing stops it but its end.

```csharp
local event(Hit hit) HitSound(Body body)
{
    Audio.Play(shot, body.position);
}

view Steps(Walker walker, mut Feet feet)
{
    if (walker.stepped != feet.lastStep)
    {
        feet.lastStep = walker.stepped;
        Audio.Play(hits[feet.lastStep % hits.count], walker.position, 0.6);
    }
}
```

- `Audio.Play(clip[, volume[, pitch]])` plays it plain stereo, from nowhere in particular. `volume` 1 is the clip as it is; `pitch` 1 is its own speed, 2 an octave up.
- `Audio.Play(clip, position[, volume[, pitch]])` places it in the world, as a `float2` or a `float3`: it's heard from the side it's on against the listener, and quieter with its distance. Until a view sets a listener, the listener is at the origin, facing +z, with +x at its right, and a sound is at its volume within 1 unit and silent past 50.

Who can play: views, local handlers and the functions they call. A system or an event handler of the match can't (the match runs the same on every machine), nor the input's `Sample`, a task, a method or code the GPU runs. A function that plays is local code's to call, as one that reads `Frame.dt` is.

**A local handler is the right place for a one-shot**: it hears the match's event once, even when a correction makes a client run the tick again, so the sound plays once too. A sound the client only guessed at plays as soon as it's guessed; `local event(verified Hit hit)` waits until the server confirmed it.

## Sounds that go on

A one-shot can't be changed once it started. A sound a game keeps track of, to change its pitch, its volume or where it is as it plays, is a `Sound` in a local field:

```csharp
component Engine
{
    float throttle;
    local Sound hum; // The match's entity, with a sound of this machine's
}

view Engines(Body body, mut Engine engine)
{
    engine.hum.clip = engineLoop;
    engine.hum.spatial = true;
    engine.hum.position = body.position;
    engine.hum.loop = true;
    engine.hum.pitch = 0.8 + engine.throttle * 0.6;
    engine.hum.playing = true;
}
```

A `Sound` has `clip`, `position` and `spatial` (placed in the world, or plain stereo), `volume` and `pitch` (1 each unless set), `loop` and `playing`. The frame reads every `Sound` in local state once the views ran, and names each that's `playing` to the mixer: the mixer keeps a voice for it from frame to frame, by the field it's in and the entity it's on, and moves the voice to whatever the fields say. A sound stops when `playing` goes false, or when its field goes (a destroyed entity's, say): the mixer fades the voice out over a few milliseconds. `playing` false and then true starts the clip over, and so does another clip. A looping sound goes on; one that doesn't loop plays to its end and stays silent until it's started over. Changes to the volume and the pan ramp over a few milliseconds, so a value written every frame doesn't click.

A `Sound` lives in local singletons, local components, a match component's `local` fields and structs inside those. The match can't hold one, nor can a list.

### The listener

Spatial sounds are heard from the listener, which a view sets each frame it wants it elsewhere than the origin:

```csharp
view Ears(Camera camera)
{
    Audio.SetListener(camera.position, camera.rotation);
    Audio.SetFalloff(2, 60);
}
```

`Audio.SetListener(position, rotation)` says where it is and which way it faces (its right is +x turned by `rotation`); `Audio.SetListener(center)` with a `float2` is the 2D form, facing +z. `Audio.SetFalloff(near, far)` says how a sound's volume falls with its distance: at its volume within `near`, silent past `far`, and as `near / distance` between, scaled to reach 0 at `far`. Like a draw setting, each holds from where it's set to the end of the frame, and every frame starts from the defaults: the origin, facing +z, and 1 and 50.

## Hearing a run with no window

A [run with no window](../tools/cli.md#without-a-window) mixes what the game plays, a 60th of a second a frame, and writes it out:

```
tide run --frames 300 --capture-audio run.wav
```

`--capture-audio` writes the whole run as a WAV of 16-bit stereo at 48 kHz, and a script's `capture-audio hit.wav` line writes what played since the last such line. The mixer is the engine's own arithmetic, so a run plays the same bytes on every machine.
