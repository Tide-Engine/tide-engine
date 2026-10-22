# Voice

Optional Opus voice chat over Tide sessions. Add `voice` to `tide.packages`,
then set the local `Voice.VoiceClient.enabled` and `transmitting` fields.
Both default to false. A view setting them uses `[Before(Voice.ConfigureVoice)]`.

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
