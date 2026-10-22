---
description: "Add optional Opus voice chat to Tide sessions, with push-to-talk, receive volume and per-player mute."
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

## Network and cost

Voice is local presentation. It is never an input, command, event or part of match state, snapshots, rollback, saves or deterministic hashes. Packets are unreliable and are not resent. Receive buffering, Opus forward error correction and packet loss concealment handle ordinary jitter and loss. Voice resumes after a reconnect or supported host migration, with fresh playback buffers.

The first version is global chat among players who enabled it. Every subscribed player can hear every transmitting player. Entity visibility does not restrict this audience. Teams, proximity routing and positional playback are not provided.

The package encodes mono at 48 kHz in 20 ms frames, targeting 24 kbit/s before transport overhead. Silence uses Opus DTX. There are at most 32 active receive streams per machine; an inactive slot can serve a new speaker. Codec work runs on the frame thread; the audio callback only copies bounded sample blocks.

Games that omit `voice` compile and link no Opus code and allocate no voice buffers. Listing the package adds its code; enabling voice or setting a player volume allocates its buffers, and encoder/decoder state is made as needed. Disabling stops capture, subscription renewal and playback; allocated capacity remains until shutdown or reload. Merely listing the package requests no microphone access and sends no voice traffic.

No new noise suppression, native echo cancellation or automatic gain control is supplied. Headphones and push-to-talk are useful with the default capture devices. Embedded Unity audio is not connected to this package.

## Testing

A headless run can use `--microphone speech.wav` with `--players` and `--capture-audio` to exercise capture, compression, transport and playback without a device. Live microphone permission and audio still follow each platform's rules.

Custom C hosts can use `packages/voice/voice.h`: keep a `tide_voice_client` per local player, configure it, and call `tide_voice_client_frame` after views and before submitting audio. The standard host performs this through `tide/voice.h`. Before freeing the client, have the mixer forget its effects; free it before destroying its session. A dedicated server needs only the session relay, with no codec or audio device.
