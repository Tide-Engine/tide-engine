# Transform smoothing measurement, 2026-10-25

Windows / Ryzen 9 7900X / Clang 23.1.2, native optimized and WASI under Node
24.22.1, pinned to logical CPU mask 0x100. Production compiler/runtime and
Transform defaults are unchanged. The tagged proxy has Transform's exact
40-byte field layout; the real built-in is an additional untagged control.
[Workload, limits and reproduction](../../corrections/README.md).

The runner holds the shared benchmark mutex and uses an explicitly shortened
15-second continuous quiet guard, polling known builds/tests/benchmarks at
250 ms. Other desktop activity and clocks/power remain uncontrolled. There
are 24 successful processes / 288 workload rows, in three alternating rounds.
312 quiet-guard resets were recorded. Three processes overlapped other builds:
`release-position-r1`, `web-release-position-r2`, `release-both-r2`. They stay
in the raw evidence and are excluded from reported aggregates, giving two
clean native pairs and three clean WASI pairs for both tags versus plain.
The final analyzer adds this contention filtering after the batch; its exact
SHA-256 is recorded in `summary.json`, separate from the launch-time analyzer
hash in `metadata.json`. No workload or executable changed during the batch.

Each process uses 64 warmup frames and 31 samples of 32 frames. Results below
are medians of clean same-round paired differences of process medians,
milliseconds extra over the identical untagged Pose. The combined interval is
correction callback + error decay + one minimal view per entity; no GPU,
network, replay, hierarchy, population or simulation work is included.

| Entities | Correction workload | Native extra ms | WASI extra ms |
|---:|---|---:|---:|
| 1,000 | No callback / no error | 0.00263 | 0.00478 |
| 1,000 | Callback each frame, no changed values | 0.06108 | 0.08869 |
| 1,000 | 1% changed each frame | 0.06173 | 0.09001 |
| 1,000 | All changed each frame | 0.14726 | 0.18678 |
| 10,000 | No callback / no error | 0.04190 | 0.03866 |
| 10,000 | Callback each frame, no changed values | 0.63938 | 0.89830 |
| 10,000 | 1% changed each frame | 0.65278 | 0.93673 |
| 10,000 | All changed each frame | 1.60387 | 2.39659 |

At 1,000 in steady motion the native paired difference spans 0.00147–0.00378 ms;
WASI spans -0.00178–0.00511, so its few-microsecond delta is near the observed
variation. At 10,000 the steady differences span 0.04162–0.04217 native and
0.03821–0.04310 WASI. Native real-Transform versus untagged-Pose steady medians
at 10,000 are 0.14734 / 0.14740 ms, supporting the proxy for this field-read
workload. It does not establish equivalent behavior/cost for hierarchies.

Dense total tagged intervals are 0.16238 / 0.20883 ms at 1,000 (native/WASI)
and 1.76001 / 2.61465 at 10,000. The last native total spans 1.73270–1.78732,
WASI 2.56628–2.66137. At 10,000 native recurring correction/decay/view medians
are separately 0.84133 / 0.52643 / 0.39281 ms; WASI 1.35999 / 0.61206 /
0.65749. These phase medians are not added to construct the total. First
correction into an empty table at this count takes 1.17035 / 1.43800 ms,
excluding local reset/free and with process allocators already warm.

The correction scan has an appreciable cost even when values are unchanged:
0.59966 / 0.85918 ms for both tags at 10,000. The experiment calls it every
frame, whereas a game's callback frequency can be much lower. Dense rotation
decay/application also costs more than position only. Variant timings include
the C compiler's optimization/layout effects; e.g. the native position-only
steady view is slower than both tags in this build. No universal incremental
instruction-cost or whole-game FPS conclusion is claimed.

Requested correction-table allocations for both tags are 72,160 / 71,392
bytes at 1,000 active entities and 750,112 / 737,824 at 10,000 (native/WASI).
With 1% of 10,000 active, the full-span handle index still makes it
73,120 / 73,024 bytes. Fresh no-error tables allocate zero dynamic bytes.
This excludes fixed local state, world birth metadata, allocator overhead and
retained page pools. Position-only and both keep the same whole-Pose error
payload; this implementation could store only annotated fields in future.

These measurements support considering position/rotation smoothing as a
reasonable Transform default for modest entity counts. They also identify
the broad correction scan and rotation decay as costs worth optimizing for
large, frequently corrected worlds. They do not prove a phone/browser frame
budget, decide hierarchy/reparenting behavior, or enable the defaults.

Validation: eight focused correctness CTests pass in each of native debug,
native optimized, web debug and web optimized (32/32 total), including 100 and
10,000 entities, continuity/half-life/count/hash checks. Every timed workload
also checks its semantics; the analyzer verifies all raw hashes, complete
repeats, exact active rows and equal native/WASI match hashes between tag
configurations. No compiler/runtime change requires full production suites;
no browser/GPU, Android, other OS, sanitizer or manual CI run is claimed.

Earlier attempts are retained alongside this directory:

- [Initial guard failure](../transform-corrections-20261025/metadata.json): no
  measurements; a Node inventory process exited during inspection. The guard
  now rechecks the process before treating an unreadable command as failure.
- [Initial workload failure](../transform-corrections-20261025-b/metadata.json):
  11 partial rows, excluded. Its float checksum sum lost precision at 10,000
  rotations. The final observer uses doubles and large correctness cases are
  registered; sparse errors also now span the full handle range.

Recompute the checked tables and paired ranges:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/corrections/summarize.py bench/results/transform-corrections-20261025-c
```
