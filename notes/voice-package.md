# Optional session voice (2026-10-22)

The owner requested voice chat in an optional package, using Opus over the
existing microphone, and explicitly selected working session voice rather
than a codec-only package. The host-relayed architecture, API names, packet
limits and buffering choices below are implementation picks, provisional in
`docs/spec.md`; the optional feature and simulation exclusion are decided.

## Package and cost

`packages/voice/tide.packages` declares `Voice`. `voice.tide` provides local
`VoiceClient { enabled, transmitting, volume = 1 }`, `ConfigureVoice` and
`SetVolume(PlayerID, float)`. Both flags default false. A game view writing
the fields uses `[Before(Voice.ConfigureVoice)]` for same-frame application.
The package references `Clip.Microphone` through its ordinary extern call,
which requests the existing Android permission metadata without a new builtin.

The package's C uses `tide_voice_client` explicitly, or registers a provider
for the standard host. Merely listing it registers the provider but allocates
no client, opens no microphone and sends no subscription/voice packets.
Enabling, or explicitly setting a player gain, allocates roughly 1.3 MiB of bounded queues/slots; encoder and at most
32 decoders allocate on use. Disabling stops microphone/playback/subscription
renewal and clears pending captured/decoded content. Capacity remains until
shutdown/reload. Gains clamp finite values to 0..4; nonfinite values become 0.
There are 32 retained per-player gain overrides, preserved through initial
connection setup and cleared by a later reconnect/migration/match replacement.

Games omitting the package compile/link no codec or bridge and allocate no
voice state. The engine's `voice.c` contains only a small provider registration.
The CMake `tide_opus` library belongs only to explicit package tests/hosts;
neither the engine/platform nor other games link it. Normal installed package
discovery/installation need no CLI, CMake game-interface or compiler change.

## Opus and sample flow

libopus 1.6.1 is vendored with COPYING/AUTHORS and an archive SHA-256 pin.
`.tools/import_opus.py` reproduces 137 portable translation units and 235
source/header files from the official tarball. Only quoted include paths and
source suffixes change: `.c.h` vendor sources are compiled by small C wrappers,
and count as headers for Tide's existing C cache. No CLI special flags are
needed. CPU dispatch/assembly, neural models, DRED and 96 kHz extensions are
disabled. The CMake vendor target has warnings disabled and debug -O2 as the
physics packages do; Tide's strict float flags still apply. The bridge remains
strict C17 with warnings as errors; a narrow diagnostic suppression surrounds
upstream's Windows __int16/__int32 typedefs.

Capture and playback use existing `tide_audio_effect` callbacks, not a new
microphone/mixer ABI. Transmission records a muted microphone voice; its effect
copies mono blocks to a bounded SPSC ring and clears the output, so there is
no local monitoring. The frame thread gathers 960 samples into 20 ms frames,
encodes 48 kHz mono VOIP at 24 kbit/s, complexity 5, speech signal, FEC with a
10% loss estimate and DTX. A packet has version 1, uint32 stream/sequence and
at most 400 Opus bytes. No Ogg container or Opus asset import is added.

Receive validates packet length/version, mono channel count and exactly 960
decoded frames before bounded queuing. Each of 32 speaker slots has 16 encoded
packets, a lazy decoder and 8,192 mono PCM samples. It primes for 40 ms, orders
by unsigned modular sequence, ignores duplicates/late frames, uses next-frame
FEC when available and PLC otherwise, and stops after 300 ms without packets.
An inactive slot can be reused after 500 ms. Decoder work is at most four
frames per slot/update; a stall skips old sequence backlog. Audio callbacks
allocate/block/decode nothing and consume fixed sample blocks. Playback is
plain mono through the normal master bus, with overall and per-player gains.

Capture generations prevent an old callback block from leaking across a
push-to-talk release/restart. Playback generations invalidate old frame knobs;
monotonic SPSC counters remain live across slot reuse, and a copied cutoff
discards the previous stream without either thread writing the other's index.
The frame thread never frees a queue while an audio callback may reference it.

## Local session packets

`tide_session_listen_data`, `tide_session_send_data`,
`tide_session_data_generation`, `tide_local_data_fn` and
`TIDE_LOCAL_DATA_BYTES` are C APIs, with no Tide builtin/generated runtime call.
One machine-local receiver exists per session. Its callback/user survive
start/join/migration; the local generation increments on a new match clock so
presentation can reset even when player/cookie values repeat.

Two additive protocol-17 message kinds follow DOWNLOAD: LOCAL_INTEREST (12)
and LOCAL_DATA (13). Existing messages, handshake, world bytes and protocol
number are unchanged. An enabled connected client renews its interest every
500 ms; the server expires it after 1.5 s. Disable sends one immediate
unsubscribe when the client next updates; a lost unsubscribe expires by lease.
No receiver means no interest traffic, packet allocation or resend queue.

Upstream packets carry the connection cookie; the server requires the sender's
existing greeted connection/address and matching cookie before acting. Payloads
are 1..512 bytes. A token bucket allows 60 packets/s with a four-packet burst
per connection. The server relays only to other greeted, subscribed connections,
inserting the real PlayerID and cookie digest, never trusting a claimed sender
or disclosing another player's secret cookie. Each downstream packet also
carries the recipient's cookie. The client validates identity, bounds and
connected state before borrowing the payload to the local callback. No packet
waits for a tick, is acknowledged/resent, or is written into world state.
Session traffic counters include these datagrams, as they do other traffic.

Servers relay bytes without Opus or audio, including dedicated servers and
the existing hosting envelope. Rooms/WebRTC, UDP, loopback and custom networks
all carry the same datagrams; no transport/relay/service change is needed.
Global chat is independent of entity/component visibility. There is no team,
proximity or server-controlled audience policy in this first version.

## Hosts and lifetime

`tide/voice.h` defines optional `tide_voice_provider { self, frame, stop }`
and set/get registration. `tide_host_game.voice` asks the game's own engine
copy for its provider (as `network` does). The standard host calls frame after
views and before audio submission. Hosts and game libraries rebuild together;
the generated Tide interface and simulation identity are unchanged.

The host forgets audio before stop at session/local-host teardown. Retained
native reload also calls the old provider's stop after mixer forget, detaching
its session callback and freeing codec/queue state before library unload.
Voice starts fresh next frame. A reconnect, changed cookie/player or local
session generation clears receive/capture history. Supported session migration
resubscribes on the adopted connection; it transfers no speech history.
Embedded Unity hosts skip voice because their audio is owned elsewhere.

## Validation

`tide_voice` runs five real three-session tests on native and WASI:

- disabled state, capture gating, no sender echo, non-subscriber silence and
  invalid send sizes;
- 15% loss, 15 ms one-way latency, 12 ms jitter/reordering, two talkers,
  per-player mute, disable/re-enable, finite decoded output and zero resyncs;
- callback sender identity, wrong cookie, oversized payload, truncated control
  packets, burst rate bound and immediate local unsubscription;
- reconnect with fresh local generation and sender-context replacement while
  the receiver stays, exercising new stream numbering/decoder reset.
- a player muted before the first frame remains muted through initial
  connection setup and can subsequently be unmuted.

Native `tide_voice_host` runs the standard host in three separate headless
processes (`--players 3 --frames 180`), generates a 440 Hz WAV microphone source,
and checks their actual mixer capture WAVs. Only the subscribed receiver has
audio; sender and disabled peer are exactly silent. Its first reference used
`platform/tests/ping.wav`, which is constant DC and is correctly removed by
Opus's voice high-pass filter; the test now uses an actual tone.

Full Windows native debug: 2,469/2,469 pass (86.84 s), including 37 GPU tests.
Full web debug: 2,409/2,409 pass (297.52 s), including WebGL, ANGLE D3D11,
WebGPU, browser rooms/migration and unchanged deterministic pins. The docs
production build passes with its existing allowed unstaged-example 404s.
Native and web component installation into a scratch prefix succeeds. That
installation's checkout-built CLI compiles the documented push-to-talk example
for web with all 137 codec wrappers and the bridge. A plain game built through
the same CLI has no Voice package objects. No package resolver, toolchain flags
or runtime codec dependency is added for games without voice.
After the full suites, the initial-mute completion passes all five package
cases on native/WASI and the native three-process host again. No engine or
compiler code changed in that completion.
Node 24.16.0 is downloaded from its npm binary package into this checkout's
ignored build directory; the machine's installed Node 24.15 is unchanged.
Physical microphone/permission/UI tests, Android/Apple/Linux device runs,
sanitizers and real Internet voice quality remain untested here. The existing
capture backends and browser worklets are unchanged.

Integration: `origin/dev` through `3a207f31` merged cleanly, bringing Unity
container/struct authoring and embed packet changes. Both complete debug builds
pass again. The affected native voice/host/embed/editor/folder checks pass 5/5;
web voice/embedded/folder checks pass 3/3; native CLI unit and LSP/stdio/pipe
checks pass 4/4. The merged docs build passes. Reinstalling both components
and rebuilding the documented example with the final installed CLI succeeds.
No full-suite repeat was required for that disjoint dependency integration.
No CI workflow was dispatched or watched; other-platform coverage follows the
normal dev push.
