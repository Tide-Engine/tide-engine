# Optional session voice (2026-10-22)

The owner requested voice chat in an optional package, using Opus over the
existing microphone, and explicitly selected working session voice rather
than a codec-only package. The host-relayed architecture, API names, packet
limits and buffering choices below are implementation picks, provisional in
`docs/spec.md`; the optional feature and simulation exclusion are decided.

## Game-defined receive processing (2026-10-22)

The owner requested arbitrary audio processing, specifically a dynamic radio
effect. This uses the existing mixer bus/effect machinery: `VoiceClient.bus`
is an integer receive bus (default 0, master), applied to each decoded playback
voice. Games use `Audio.SetEffect(Bus.Radio, Radio(knobs))` every frame, with
ordinary `audio` code, state and dynamic knobs. Game enum values convert with
`int(Bus.Radio)`. This introduces no compiler, generated runtime, mixer,
capture, codec, protocol, server or engine host changes.

`Voice.SetBus(PlayerID, int)` overrides one speaker; negative clears the
override and resumes following the default. There are 32 routing overrides,
independent of the 32 gain overrides. Existing entries update, cleared slots
are reusable, and a new entry at capacity is ignored. The default and all
nonnegative overrides outside 0..15 repair to master; a negative default also
repairs to master. Invalid player IDs are ignored. Clearing an override
with no standard-host client allocates nothing. The custom C APIs are
`tide_voice_client_bus` and `tide_voice_client_player_bus`; the existing C
configure signature is unchanged. The package's ordinary configure extern
additionally carries the integer bus to the standard provider.

Overrides survive initial connection setup (including calls before enabling),
and are cleared with gains on a later local reconnect/migration/match change.
The default bus remains a local setting. Changing routes affects existing
playback without restarting encoders, decoders or receive queues. A bus effect
processes its mixed speakers, with one shared state/knob set per bus. Different
buses give independent processing, within the mixer's existing 16-bus limit.
Normal voice/player gains happen before the bus effect; its output then passes
through bus/master volume as usual. This processes only this receiver's audio,
not transmitted samples, delivery permissions or the game's other buses.

The package test now compiles the existing mixer source into its native/WASI
test executable (no audio device). An eighth three-session case checks a route
set before connection, live gain knobs at 0/1/0.25 while preserving effect
state, clearing a per-player route, changing the default on an active speaker,
arbitrary integer repair and reset on reconnect. It runs actual Opus/session
playback through the mixer. A separate dry sound must remain unchanged when
the voice bus is muted; subtracting that exact dry signal detects accidental
master processing. Teardown forgets the mixer before freeing voice queues.
The native three-process host fixture also routes through a real game-defined
Tide audio effect with knobs changing every 30 frames. All eight package cases
pass on native/WASI, and the standard host capture test passes.

The public docs include a compilable radio example (band filtering, clipping,
amplitude-scaled hiss, live signal strength and clean per-player routes).
The example transpiles without warnings. Both complete debug builds pass.
The full native run covered 2,469 checks: 2,467 passed in the concurrent
24-job run (118.27 s); CLI help and the benchmark harness had child-process
timeouts. Both unchanged checks pass serially with the original deadlines
(19.22 s). The full web debug suite passes 2,409/2,409 (374.19 s), including
WebGL/D3D11/WebGPU, browser rooms and threads. The final host fixture also
calls the Tide per-player SetBus extern and passes again (3.45 s).

The docs production build passes. Native/web component installation succeeds,
and the installed checkout-built CLI builds the documented radio example for
web without special flags. The first scratch CLI attempt accidentally included
transpiled C stored inside the scratch game's source folder and correctly
failed duplicate symbols; a clean game folder containing only the documented
Tide source and package list builds successfully. Diff whitespace checks pass.
No physical microphone/device quality, Android, other desktop OS or sanitizer
run was added; those remain manual/normal CI coverage. No CI was dispatched.

Integration: origin/dev's Unity rendering/HLSL work merged cleanly in
`dc4e8a52`, changing compiler and standard-host dependencies without voice/audio
overlap. Rebuilt merged native voice, voice host and CLI; both voice targets
pass (1.54 s). Rebuilt merged WASI voice and web platform; voice passes (0.82 s).
The merged docs build and a rebuilt/reinstalled CLI's radio web example pass.
No broad full-suite repeat was required for the disjoint dependency changes.

## Configurable RNNoise suppression (2026-10-22)

The owner requested RNNoise support controlled by a setting. Local
`VoiceClient.noiseSuppression` defaults true and filters only outgoing capture.
The owner selected true after the initial off-by-default implementation.
The explicit C configure call now takes `noise_suppression` before `volume`;
the ordinary Tide extern carries the same boolean. No compiler, engine host,
microphone backend, session protocol or generated-runtime API changes.

RNNoise 0.2 and upstream model archive `0b50c45` are pinned by SHA-256 in
`packages/voice/import_rnnoise.py` and the package README, with upstream
COPYING/AUTHORS retained. The importer selects the smaller `little` model,
rewrites quoted includes and stores C as `.c.h` behind ten portable wrappers.
It preserves algorithms/weights. Upstream's compile-time SSE2/NEON/scalar
selection remains; runtime CPU dispatch is off. The scalar `vec.h` includes
an absent `os_support.h`: a small hand-maintained compatibility header maps
its only required macro, `OPUS_CLEAR`, to RNNoise's own `RNN_CLEAR`. Vendor
warnings are disabled and debug vendor code optimized, as for Opus. Strict
Tide float flags remain. Only the upstream public header's old-style empty
parameter declarations get a narrow strict-prototypes diagnostic suppression.

On the frame thread, captured normalized mono samples are scaled to PCM16's
float range, processed in two 480-sample/10 ms blocks, repaired/clamped back
to normalized finite samples and encoded by the existing 960-sample Opus path.
No callback gains allocation, filtering or locking. RNNoise's analysis overlap
and delayed spectrum add about 20 ms of filter delay; no extra queue, wire
format, receive processing or network messages are introduced.

Filter state is allocated only for an enabled, connected, transmitting client
requesting suppression. We allocate explicitly and check failure before init;
upstream `rnnoise_create` does not check malloc before dereferencing. Failure
leaves transmission stopped and retries later, as encoder failure does.
The 32,688-byte state remains until client teardown but does no work while
bypassed. Transmission start, reconnect/migration and a setting change reset
RNNoise/Opus history and stream numbering, discard partial capture and
invalidate queued capture generations. Receivers and other game audio are
unaffected. The model is included in games using Voice even with the flag off;
games omitting Voice compile/link neither codec nor filter/model. The model
object has 2,809,098 bytes of native readonly data (before compression).

Two new three-session tests exercise actual RNNoise -> Opus -> session ->
playback: deterministic steady noise falls below 20% of bypass energy,
switching off restores above 80%, a stopped/restarted silent utterance has no
stale history, and synthetic speech retains above 25% of bypass energy.
These broad signal bounds avoid pinning vendor samples across architectures.
The speech fixture's provenance is in `packages/voice/test/README.md`.
All seven package cases pass on native and WASI. The three-process standard
host fixture now relies on the true Tide default and passes its real
microphone-file/mixer/transport/playback assertions.

Release measurements on Ryzen 9 7900X (clang 23.1.2, three runs, synthesized
48 kHz mono speech, 200 warmup + 2,000 measured packets per case): median of
run means is 98.510 us/20 ms frame for Opus encoding alone and 235.472 us with
RNNoise (about 0.69% of one core extra while transmitting). Encoding p95 is
121.3 us versus 314.3 us. Mean Opus payload is 42.986 versus 43.172 bytes:
suppression adds no protocol overhead. This measures frame-thread filtering
plus encoding, excluding device/network time. Mobile/web CPU, physical
microphone quality and actual mouth-to-ear latency have not been measured.

Full Windows native debug: 2,469/2,469 pass (517.93 s, serial), including all
37 GPU tests. Full web debug: 2,409/2,409 pass (331.24 s, 24 CTest jobs),
including WebGL/D3D11/WebGPU and browser rooms/threading. Both complete builds
and the docs production build pass. Reinstalling native/web components into
the scratch prefix succeeds; that installation's checkout-built CLI builds
the documented push-to-talk game with `noiseSuppression = true` for web,
and a plain game without Voice. No runtime downloads or special flags are
needed. `git diff --check` is clean outside the unchanged upstream whitespace
retained in three vendor lines. Other operating systems, Android, physical
microphones and sanitizers remain CI/manual coverage; none were run here.

Default-on follow-up: only the package's Tide initializer, C header comment,
host fixture and docs changed. The host fixture omits an explicit suppression
assignment to exercise the ordinary default through generated local state,
ConfigureVoice and the real microphone-file/mixer/session path. Custom C
hosts still pass their explicit suppression boolean when configuring a client.
Both voice targets pass natively (2/2), all seven package cases pass on WASI,
and the docs production build passes. Generated native/web local initializers
both explicitly set `noiseSuppression = true`; the diff passes whitespace checks.

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
Embedded hosts (Unity) run voice too since 2026-10-22, as their frames' audio
reaches the other program (`tide_embed_audio`, `tide_embed_microphone`); until
then they skipped it. `embed_voice_provider` in `tide_platform_embed` covers it.

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
