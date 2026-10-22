---
description: "Add optional Opus voice chat to Tide sessions, with noise suppression, custom audio effects, receive volume and per-player mute."
---

# Voice chat

The `voice` package sends microphone audio to the other players in a Tide session. It uses Opus and the session's existing transport, including rooms, direct connections and a game's own network. The host forwards voice packets; it does not decode them.

## Using voice

Add this line to `tide.packages`:

```
voice
```

Enable reception and set `transmitting` while the player holds a push-to-talk button:

```csharp
using Voice;

[Before(Voice.ConfigureVoice)]
view Talk(Devices devices, mut VoiceClient voice)
{
    voice.enabled = true;
    voice.transmitting = devices.keyboard.v.pressed;
}
```

Both flags default to false. `enabled` lets this machine receive voice; `transmitting` also requires it to be enabled. The ordering attribute applies the settings before the package reads them in the same frame. A menu can set these fields instead of enabling voice unconditionally.

The microphone opens only while transmitting in a connected session. Releasing the button stops capture and discards unsent captured audio. Phones and browsers ask for microphone permission; until it is allowed the source is silent. Browser audio also requires a player gesture. The existing [microphone platform requirements](../game/audio.md#the-microphone) apply, including macOS bundle usage text. Incoming speech is never played back to its sender.

## Volume and mute

`VoiceClient.volume` controls all received speech, defaulting to 1. Set it to zero to deafen this machine while leaving transmission independent. The normal master audio bus also affects speech.

`Voice.SetVolume(player, volume)` sets this machine's receive volume for one player, including before reception starts. Zero mutes them; 1 restores the normal level. This affects only what this machine hears. Gains clamp to 0–4; nonfinite values become zero. Per-player settings last until changed, reconnect, migration or a match change. Up to 32 player-specific settings are retained.

Voice gains apply before receive-bus effects. Use `Audio.SetVolume` on a dedicated voice bus to also silence that effect's generated sound or echo tails.

## Noise suppression

`VoiceClient.noiseSuppression` defaults to true, filtering this player's microphone before encoding. Set `voice.noiseSuppression = false` in the view above, or from an audio settings menu, to bypass it. It can change while transmitting. It does not filter incoming players or the game's other sounds.

Suppression uses RNNoise 0.2's built-in smaller model, processing 10 ms blocks at 48 kHz on the frame thread. Its filter adds about 20 ms of delay; it adds no network messages or receive work. State is allocated on first suppressed transmission and retained until shutdown or reload. While off, the filter does no work. Each new transmission or setting change resets filter history and discards pending capture, so an earlier utterance cannot leak into a new one. Changing the setting can cause a short audio gap.

Noise suppression reduces background sounds; it does not supply echo cancellation or automatic gain control. Browser microphone capture can also apply the browser's default processing.

## Processing received voice

Route received speech to an audio bus with `VoiceClient.bus`, then use normal [audio effects](../game/audio.md#effects) on that bus. This supports game-defined filters, distortion, static, echoes and live effect parameters. For example, a radio channel can become noisier as signal strength drops:

```csharp
using Voice;

[Bus]
enum Bus { Master, CleanVoice, Radio }

local singleton RadioSettings { float strength = 1; }
struct RadioKnobs { float strength; }
struct RadioFilter { float[2] low; float[2] band; }

audio Radio(mut Samples samples, mut RadioFilter filter, RadioKnobs knobs)
{
    var wet = 1 - Math.Clamp(knobs.strength, 0, 1);
    for (var i = 0; i < samples.values.count; i += 2)
    {
        for (var channel = 0; channel < 2; channel++)
        {
            var x = samples.values[i + channel];
            filter.low[channel] += 0.04 * (x - filter.low[channel]);
            filter.band[channel] += 0.3 * (x - filter.low[channel] - filter.band[channel]);
            var distorted = Math.Clamp(filter.band[channel] * 4, -0.5, 0.5);
            var hiss = Random.NextFloat(-1, 1) * Math.Abs(x) * 0.3;
            samples.values[i + channel] = Math.Lerp(x, distorted + hiss, wet);
        }
    }
}

[Before(Voice.ConfigureVoice)]
view RadioChat(Devices devices, mut VoiceClient voice, RadioSettings radio)
{
    voice.enabled = true;
    voice.transmitting = devices.keyboard.v.pressed;
    voice.bus = int(Bus.Radio);
    Audio.SetEffect(Bus.Radio, Radio(RadioKnobs { strength = radio.strength }));
}
```

Change `RadioSettings.strength` from game code—for example from distance or signal strength. Effect knobs update each frame while filter state persists. The bus effect runs after decoding and the normal receive gains; it does not change transmitted audio or add bandwidth. Other game sounds are affected only if you route them to the same bus.

`Voice.SetBus(player, int(Bus.CleanVoice))` overrides the receive bus for one player, including before reception starts. `Voice.SetBus(player, -1)` removes the override and follows `VoiceClient.bus` again. Overrides persist until changed, reconnect, migration or a match change. Up to 32 routing overrides are retained independently of per-player volume settings; further new overrides are ignored until one is cleared. The default bus setting remains across reconnects.

Bus numbers are local integers: use `int(...)` with your game's `[Bus]` enum. The default is 0, the master bus. Default values outside 0–15, and nonnegative player overrides above 15, fall back to master. Buses are the mixer's existing 16 buses; speakers sharing one are mixed before its effect and share filter state/knobs. Use different buses for independent processing. Apply `Audio.SetEffect` and bus volume each frame, as for other sounds.

## Network and cost

Voice is local presentation. It is never an input, command, event or part of match state, snapshots, rollback, saves or deterministic hashes. Packets are unreliable and are not resent. Receive buffering, Opus forward error correction and packet loss concealment handle ordinary jitter and loss. Voice resumes after a reconnect or supported host migration, with fresh playback buffers.

The first version is global chat among players who enabled it. Every subscribed player can hear every transmitting player. Entity visibility does not restrict this audience. Teams, proximity routing and positional playback are not provided.

The package encodes mono at 48 kHz in 20 ms frames, targeting 24 kbit/s before transport overhead. Silence uses Opus DTX. There are at most 32 active receive streams per machine; an inactive slot can serve a new speaker. Codec work runs on the frame thread; the package's capture/playback callbacks only copy bounded sample blocks. Bus effects run on the normal audio thread.

Games that omit `voice` compile and link no Opus or RNNoise code and allocate no voice buffers. Listing the package includes the codecs and suppression model, even when suppression is off; enabling voice or setting a player volume/routing override allocates its buffers, and encoder/decoder/filter state is made as needed. Disabling stops capture, subscription renewal and playback; allocated capacity remains until shutdown or reload. Merely listing the package requests no microphone access and sends no voice traffic.

Headphones and push-to-talk are useful with the default capture devices. Embedded Unity audio is not connected to this package.

## Testing

A headless run can use `--microphone speech.wav` with `--players` and `--capture-audio` to exercise capture, compression, transport and playback without a device. Live microphone permission and audio still follow each platform's rules.

Custom C hosts can use `packages/voice/voice.h`: keep a `tide_voice_client` per local player, call `tide_voice_client_configure(client, enabled, transmitting, noiseSuppression, volume)`, and call `tide_voice_client_frame` after views and before submitting audio. `tide_voice_client_bus` sets the default receive bus; `tide_voice_client_player_bus` overrides a player or clears their override with a negative bus. The standard host performs this through `tide/voice.h`. Before freeing the client, have the mixer forget its effects; free it before destroying its session. A dedicated server needs only the session relay, with no codec or audio device.
