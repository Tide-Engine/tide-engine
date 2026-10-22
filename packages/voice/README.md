# Voice

Optional Opus voice chat over Tide sessions. Add `voice` to `tide.packages`,
then set the local `Voice.VoiceClient.enabled` and `transmitting` fields.
Both default to false. A view setting them uses `[Before(Voice.ConfigureVoice)]`.
`Voice.VoiceClient.noiseSuppression` defaults to true for RNNoise processing
of outgoing microphone audio. Set it to false to bypass; it can change at runtime.

Setup, API, platform requirements and current limits:
[Voice chat](https://tide-engine.dev/packages/voice).

`voice.h` supplies the explicit per-client C API for custom hosts. `bridge.c`
owns codec state, sample queues and receive buffering. No state enters the
simulation. The engine supplies only provider registration and bounded,
authenticated unreliable session data; it contains no Opus dependency.

## Vendored codec

libopus **1.6.1**, from
https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
(SHA-256 `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1`).
Its license and authors are retained in `opus/COPYING` and `opus/AUTHORS`.

`.tools/import_opus.py <archive>` reproduces the selected portable
floating-point encoder/decoder sources. It rewrites quoted includes to relative
paths and stores upstream C as `.c.h`, with one wrapper under `codec/` per
translation unit. This lets Tide compile the package with its ordinary source
discovery and header cache, without engine/CLI special cases or extra include
flags. Upstream algorithms are unchanged. CPU dispatch, architecture-specific
assembly/SIMD, neural models, DRED and 96 kHz extensions are not enabled.

## Vendored noise suppression

RNNoise **0.2**, from https://github.com/xiph/rnnoise/archive/refs/tags/v0.2.tar.gz
(SHA-256 `e4a17eb1493b7551f9f7781d2acc67b8847d6ed7c4b64db0fd62c017021ae1ec`),
with the upstream **little** model from
https://media.xiph.org/rnnoise/models/rnnoise_data-0b50c45.tar.gz
(SHA-256 `4ac81c5c0884ec4bd5907026aaae16209b7b76cd9d7f71af582094a2f98f4b43`).
`rnnoise/COPYING` and `rnnoise/AUTHORS` retain its license and authors.

`python packages/voice/import_rnnoise.py <source-archive> <model-archive>`
reproduces the selected source/header files and ten `denoise/` wrappers.
It checks both pins, selects `rnnoise_data_little.c/.h` as the default model,
rewrites quoted include paths and stores vendor C as `.c.h`, as for Opus.
There is no external model download or model-loading API in the Tide interface.
The wrappers use upstream's compile-time SSE2/NEON or scalar path without
runtime CPU dispatch or additional CLI flags. A hand-maintained `os_support.h`
supplies the missing scalar-path `OPUS_CLEAR` as RNNoise's own `RNN_CLEAR`.
Upstream algorithms and weights are unchanged. The package includes the model
even with the setting off, but allocates/runs no suppressor until requested.
