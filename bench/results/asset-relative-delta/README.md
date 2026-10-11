# Asset-relative value serialization experiment (2026-10-11)

The owner asked to try storing/transferring an asset-derived value as an asset
ID plus changes, especially for a new connection. This is an isolated C value
codec using Tide's existing delta implementation. Production compiler, engine,
session protocol, installed package and user game are unchanged. No language
semantics or production wire format is decided by this experiment.

## What it does

`AP01` frames have a five-byte magic/mode header. An asset is registered once by
SHA-256 of its exact immutable bytes; the byte-list codec supplies the type and
version namespace. Physical pack handles and declaration names never go on wire.
Three modes are tested:

- Full: the existing delta codec with no baseline, including its compression,
  result hash and logical length.
- Reference: the 32-byte content ID alone, for a known unchanged asset reference.
- Patch: the ID plus the existing delta codec XOR'd against that asset's bytes,
  in 4 KiB regions. Unchanged regions are omitted.

The receiver has the exact asset version but no previous match world or edit
history. It constructs ordinary owned mutable bytes, verifies the result hash
and consumes the complete frame. Missing/wrong IDs fail rather than substituting
a newer asset with the same name. The experiment demonstrates decoding the
independent full form without any available asset; requesting/resending that
fallback is future session work. Source assets remain unchanged after decoding
and editing the receiver's value.

Owned values retain their source ID out of band in this fixture. Only their
serialization compares bytes; there is no matching search over known assets
and no overlay on ordinary reads/writes. A value without provenance uses exactly
the original full encoding. Changed values are encoded both ways and the smaller
complete frame wins, including the ID overhead. This deliberately simple choice
may cost more CPU to pack: runtime timings remain deferred by the owner.

The known-reference mode stays constant work and does not encode the full value
to compare sizes. A 16-byte control exposes its tradeoff: 37 reference bytes
versus 35 full bytes. Production compact negotiated IDs or a small-value policy
need design and measurement; the adverse result is retained.

## Exact size results

These are encoded byte counts, unaffected by machine contention. The baseline
already uses Tide's zero-run/stride compression, rather than raw byte copying.
For the first canonical model blob in the owner's frozen `movementAnims` pack
entry (114,224 bytes), the complete value frames are:

| Current value | Full encoding | Selected encoding |
|---|---:|---:|
| Known unchanged reference | 107,471 | 37 |
| Owned but unchanged | 107,471 | 50 |
| One byte changed | 107,471 | 59 |
| 64 sparse byte edits | 107,482 | 441 |
| 4 KiB replaced with pseudorandom bytes | 107,516 | 4,160 |
| 4 KiB XOR'd by the same constant | 107,247 | 300 |
| Completely replaced with pseudorandom bytes | 114,383 | 114,383 (full) |
| Replaced with zeros | 158 | 158 (full) |
| Appended 257 repeated bytes | 107,478 | 313 |
| Truncated 257 bytes | 107,298 | 55 |
| Deleted 257 bytes from the middle | 107,737 | 57,012 |
| Empty | 16 | 16 (full) |

The same twelve cases run on deterministic 1 MiB noise and repeating-pattern
fixtures, plus two 16-byte controls. A middle deletion shifts subsequent data;
this positional XOR codec sends much more than the deletion's 257 bytes. It is
not an insertion/deletion edit-script compressor. Artificial edits of the model
blob test it as an ordinary byte value; they are not asserted valid animations.
The private asset payload is not redistributed; its input/content identities
are recorded. Public synthetic fixtures need no external assets.

## Validation and limits

All 38 value cases round-trip exactly on native and WASI. Complete output rows
agree, including value hashes and SHA-256 of encoded full/selected messages.
Each platform executes 11,252 rejection assertions: exact-sized truncated frames
(exhaustive below 128 bytes, sampled for larger frames), trailing data, invalid
modes, corrupted result hashes, missing/wrong asset IDs and an oversized logical
length. Failure preserves a caller's output and guard bytes. Repeated checks of
the same full/selected form count as assertions, not unique hostile inputs.

The initial growing-value check caught a fixture mistake: `tide_delta_get`
requires zeroed output, including the new tail past the baseline. The corrected
decoder uses `calloc`. The failing run produced no timing measurements; both
platforms passed after the correction and final control additions.

Both builds are warning-free strict C17 with `-O2 -fno-fast-math
-ffp-contract=off`. Native uses the checkout-built frozen release engine library;
WASI uses its frozen web-debug library, wasi-sdk 24 and pinned Node 24.16.
The same existing delta codec is linked for full and candidate within each
binary. Sources, libraries, binary/input hashes and exported artifact hashes
are recorded in `identities.json`. `verify.mjs` checks values, selection,
fallbacks, the tiny-value regression, platform parity and exported hashes.

These are value-frame sizes, **not an actual session join measurement or whole
world/save-file size**. World metadata, heap layout/allocator bytes, cross-field
deduplication, MTU/chunk framing, latency, loss and catch-up are absent. Existing
eligible joins already reuse matching initial-world pages; this experiment
does not compare that end-to-end path. It establishes exact value transfer
savings with an immutable baseline, not FPS or packing-speed improvements.

Production integration still needs stable asset registration/lifetimes through
reload, persisted provenance after detachment, mapping value patches into exact
world reconstruction and hashes, save compatibility, missing-asset handling,
all container mutation paths and resource budgets. Nested containers and typed
assets are outside this byte-value experiment. No full production suites,
browser/GPU, other platforms, sanitizers or CI were run for these artifacts.

## Reproduction and deferred timings

With the checkout-built native release/web-debug libraries and pinned toolchain:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/results/asset-relative-delta/check.ps1
```

The private fixture is included only when `build/owned-animation/gen/game.pack`
is available. The public cases work independently. Copy the scratch JSONL rows
here and regenerate the identities/exports for a new batch, then run
`build/node/node.exe bench/results/asset-relative-delta/verify.mjs`.

`measure.ps1` is ready for a future quiet window, but has **not been run**. It
holds the existing benchmark gate, requires 90 seconds without detected competing
build/test/benchmark activity, checks competition during each process, and keeps
three dated native output sets. `--timing` alternates full/candidate encoding and
decoding order within every iteration, discards 30 warmups and averages 150.
Registration is outside timing; encoding includes hashing, compression, both
candidate forms and allocation, and decoding includes reconstruction/allocation
and hash verification. Correctness checks/freeing are outside those intervals.
Every measured process must match this batch's input and message identities.
The verifier can analyze those rows with `--timings <relative-output-directory>`.
No speed claim relies on correctness-run clocks: none were recorded.
