# Production asset sharing: structural checks

This is the integrated runtime/compiler/session implementation, after the
standalone sharing, owned-reuse and relative-delta experiments. The owner
deferred timing while other work was active. These rows prove storage/transfer
sizes and exact reconstruction, **not a production CPU or FPS improvement**.

The private `D:/Projects/Test` fixture is the owner's unchanged 10x10 zombie
grid, using `movementAnims.rootIdle/Run/Jump`, `baseCharacter` and Rooms. Its
source/FBX assets are not copied into this repository. `probe.c` runs 1,200
ticks, prepares one frame with GUI initialized, checks a known-pack round trip,
and reports outside-state content-pool leases, physical heap, self-contained
save size, actual whole-world transfer sizes and deterministic hashes. Optional
wire prefixes write packets under ignored `build/`, compare every foreign byte,
and decode both known-pack and full packets from the other platform. Full foreign
packets also decode after pack unload.

| Result | Native | WASI |
|---|---:|---:|
| Prepared instances | 100 | 100 |
| Immutable payloads / descriptor leases | 1 / 100 | 1 / 100 |
| Payload bytes retained once | 114,224 | 114,224 |
| Match heap bytes | 6,480 | 6,480 |
| Self-contained raw world | 128,440 | 128,392 |
| Known-pack whole transfer | 6,258 | 6,258 |
| Self-contained whole transfer | 120,482 | 120,482 |
| World hash | `b63eb72b0cc75f1c` | `b63eb72b0cc75f1c` |
| Prepared palette hash | `ab1b21c3dd4b57f1` | `ab1b21c3dd4b57f1` |
| Known-pack packet hash | `b0aa155229c78454` | `b0aa155229c78454` |
| Full packet hash | `e47220f2c6944e66` | `e47220f2c6944e66` |

Known-pack transfer uses 94.8% fewer bytes. This includes all world transfer
framing, whereas earlier relative-delta rows measured individual values. Raw
saves retain the platform's local world header; network headers now exclude
native storage ABI and produce identical bytes. These figures do not measure
transport overhead, handshake bytes, actual join latency, renderer GPU time,
allocation RSS or payload-free save patches. The earlier 13 MB owned-world
measurement was from a different frozen prototype build and is not a controlled
before/after comparison for this integration.

Reproduce from this checkout, with CMake debug/web-debug prerequisites and the
private project available:

```powershell
./bench/results/asset-production/check.ps1 -Project D:/Projects/Test
```

The script sets `TIDE_HIDDEN` in its own process, adds a temporary CMake HOST
target, builds only this checkout's native/WASI programs, checks packets in both
directions and restores both presets' previous project-include cache settings.
It does not edit the game. The saved `native.json`/`web.json` are structural
outputs from the final codec run. `identity.json` records its source, generated
code, binary, pack and private source SHA-256s. No private source/payload or wire
packet is committed.

Production correctness regressions are `tests/test_asset_lists.c` and
`compiler/tests/e2e/asset_sharing.*`; the latter also exercises joining over
latency/jitter/loss and deliberately forces reference decoding failure followed
by self-contained retry. Existing exact sampler/render/serialization suites
remain required. Final full-suite totals are recorded below after validation.

Frozen production validation passes all **2,189 native** and **2,158 web debug**
CTests, including OpenGL/WebGL/WebGPU, safety, serialization, lossy sessions,
hot reload and physics. The docs site builds after `npm ci`. These totals precede
integration with newer dev compiler changes; integration checks are recorded in
the notes. CI, sanitizers, other desktop/mobile targets and controlled timing
were not run.

After merging newer dev compiler work, every native/web target builds and all
112 native plus 104 web overlapping CTests pass. Optional asset-backed list
fields are included in the generated ownership/serialization/join regression.
The docs build passes again. The private project cross-check was rerun with
cached descriptor authentication; all numbers and hashes above are unchanged,
and the saved identities describe this integration run.

Remaining measurement work: alternating release comparisons of the actual
project and ordinary list controls, including cold detach, owned reset/edit,
written-then-read reassignment, snapshot/hash, view/prepare and transfer
encode/decode. Retaining provenance on ordinary local-to-field copies and raw
edited save patches is not implemented. Eligible transfer pages encode both
candidates to choose the smaller, so byte reduction alone cannot prove lower CPU.
