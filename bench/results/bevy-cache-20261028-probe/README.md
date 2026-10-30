# Cached sparse selection probe

This cache-only candidate compares frozen Tide `66b8ddb8f`, the candidate and
the unchanged Bevy ECS 0.20.0 executable on 1M twelve-float components with
95% or 99% frozen. Windows 11 / Ryzen 9 7900X, Clang 23.1.2 and Rust 1.95.0 /
LLVM 22.1.2 use the same optimized O2 baseline x86-64 flags, without fast math
or FMA. Game sources, starting values, arithmetic and 60 Hz tick length remain
unchanged. This probe improves Tide's warmed selective queries, but Bevy still
leads in all three cases.

## Measured result

Three alternating adjacent Before/Tide/Bevy triples per case preserve 27
successful processes, each with 200 warmup and 400 measured ticks. This is
10,800 measured ECS ticks and 5,400 ECS warmup ticks, plus 7,200 measured
handwritten-C reference ticks inside the two Tide processes per triple.
All final logical digests agree across implementations, rounds and the two
99%-frozen thread budgets.

Milliseconds per scheduled ECS tick, median of three process medians. Change
is the median paired current/before percentage change, rather than a ratio of
the two reported medians:

| Frozen | Execution budget | Before Tide | Cached Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 95% | 1 | 1.39235 | 1.07290 | 0.22540 | -22.90% |
| 99% | 1 | 0.27420 | 0.11995 | 0.04875 | -57.08% |
| 99% | 4 | 0.08585 | 0.04905 | 0.03790 | -42.86% |

Cached Tide process medians span 1.07115-1.07810, 0.11770-0.12000 and
0.04840-0.05440 ms respectively. Bevy spans 0.21275-0.22835,
0.04870-0.04995 and 0.03455-0.04460 ms. Exact paired ratios and all process
ranges are in [summary.md](summary.md) and [summary.json](summary.json).
The C reference runs after Tide within the same process and is contextual;
it is not another independently alternating competitor.

The batch spans 22:37:54-22:39:42 UTC. The shared benchmark mutex and explicit
15-second quiet guard coordinate launches; 250 ms inventory polling detects no
contention during any recorded process. The guard records 56 reset observations
of build processes while waiting between launches, from 22:38:22 to 22:39:05.
These are repeated inventory observations, not 56 distinct jobs. Their cause is
not established. Desktop activity, CPU placement, clocks and temperature remain
uncontrolled. No attempts or adverse cases in this directory are discarded.

## Candidate contracts and limits

Generated private helpers retain each filter's presence pages and cache the
exact plan shape: archetype, chunk shift, required/excluded column descriptors,
row count and retained page identities. Retention makes subsequent membership
writes detach through COW, so unchanged pointer identities prove unchanged
presence bytes. Cache plans are prepared serially before dispatch; workers only
read them. Rare matches reuse ordered 16-bit row indices; ordinary filtered
paths remain available. This changes no runtime API, component field layout,
math, entity IDs, deferred ordering or scheduling semantics.

The cache belongs to its world and is derived data. World copies do not share
its ownership; restore and reload cannot treat a matching ordinal alone as
proof of a matching plan. Exact filter/shape/page checks invalidate stale
selection, including changed membership and row counts. Raw unpack clears any
received cache pointer before a later decode failure can free the world. The
private payload owns no code pointers or static descriptor pointers.

The generated world gains a private cache pointer. In the native wide fixture,
its raw packed header grows from 160 to 168 bytes, with the new slot always
serialized as zero. Raw packs therefore are not unchanged byte for byte.
Component layouts, simulation hashes and logical delta state exclude the
cache. [The 75-case comparison](diagnostics/comparison.json) confirms equal
native world hashes and equal normalized raw packs after removing the added
zero header slot. Counts are 0/1/65/2,049/4,099; frozen percentages are
0/50/95/99/100; observations are ticks 1/3/17. The C driver and both result
streams are retained under diagnostics/.

These timings cover stable membership after warming the cache. They establish
neither cold cache construction cost nor speed under changing membership,
retained snapshots or reload. Correctness checks exercise those boundaries;
they are not performance measurements. Setup, final digest, per-tick hashing,
snapshots, rollback, networking, rendering and application frame loops are
outside the timed workload. No whole-engine, FPS or portable speed factor
follows. [Shared methodology and thread semantics](../../bevy/README.md).

## Correctness and reproduction

The initial 11 and boundary 23 optimized CTests pass on native and WASI,
covering 33 unique checks per platform because one check appears in both runs.
Saved logs include sparse-cache dynamic membership, exact row/math comparison,
world ownership, COW snapshots, malformed raw headers, private descriptor/page
validation, migration/reload boundaries, deltas, tasks and unchanged random/
cross-platform pins. [validation.json](validation.json) records 66 further
Bevy comparisons against native Tide/C and WASI Tide/C. The offline analyzer
verifies complete triples, successful exits, raw output hashes, options,
binary identities, Bevy samples/statistics and invariant logical digests.
No full suites, browser/GPU, Bevy web, other OS/mobile or sanitizers are claimed.

metadata.json pins the measured source/configuration/generated-C/binary hashes.
The exact measured compiler can be reconstructed in a checkout of `66b8ddb8f`
by applying [diagnostics/codegen.patch](diagnostics/codegen.patch), then copying
[diagnostics/rows_cache_codegen.inc](diagnostics/rows_cache_codegen.inc) to
`compiler/src/rows_cache_codegen.inc`. The resulting measured SHA-256 values are:

- codegen.c: `0aeb724752e08a3f31129ffb78b291f01b9228a38086532f8bd0bad9ed4ca66a`
- rows_cache_codegen.inc: `dd686e6df6de0fb3b898561039da6ed7a55fc6eef2ce0ce4fc2e215cb90f974d`

Keep the frozen baseline's three executables before rebuilding. With the
documented build prerequisites, repeat the same subset in a PowerShell session:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-cache-new -Rounds 3 `
    -BeforeBuildDir build/bevy-before-66b8ddb8f -BeforeRevision 66b8ddb8f `
    -FilterSweep -OnlyCases 'filtered-frozen95-1m-1','filtered-frozen99-1m-1','filtered-frozen99-1m-4'
python bench/bevy/analyze.py bench/results/bevy-cache-new
```

Recheck the saved evidence without running either engine:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/bevy/analyze.py bench/results/bevy-cache-20261028-probe
```
