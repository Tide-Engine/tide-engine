# Benchmarks

Scene visibility unification (2026-10-15): removing generated scene audience
fields changes the quick-session world pins to 742cb392be44252d for Sand and
ccd64c3fd8bcbb8b for Gallery, equal on native and WASI. Only the expected hashes
change; workloads, inputs, convergence assertions and saved measurements are
untouched. No performance comparison is claimed. Validation: notes/language.md.

## Map views and Linux cache groups, 2026-10-20

The pass's second round, after the owner approved list views for generated
code and asked for cache groups on Linux and Android with CI as their check
(notes/ecs.md, Map views across a function; notes/schedule-and-threads.md,
Linux and Android). Gated and alternating as above, five rounds, the first
round's final binaries (`45733126`) against `c02addd8`
(`perf-pass-views-before`/`-after`):

| ms a tick, 1 thread (C way) | before | after | Tide/C after |
| --- | ---: | ---: | ---: |
| lookup, 16k keys, 1,024 changes (C) | 1.365 | 1.161 | 3.70× |
| lookup, 16k keys, steady (C) | 1.379 | 1.137 | 8.6× |
| buckets, a list of lists (C) | 0.0627 | 0.0575 | 7.9× |

12 threads alike (lookups run on one). Sand's session clients 18.1 → 16.6 ms
of simulation per second (no map in its hot path; within noise), Gallery as it
was; end hashes the same. Every other versus program moved within noise; a
focused eight-round pairing of wide, churn and events
(`perf-pass-views-wide-*`), as wide's first pairing read 1.61 → 1.86 ms
under heavy machine load, has wide at 1.30× and 1.29× its C, events 7.58×
and 7.59×: no change, as their generated code has no map and they call no
changed function.

Taken a step at a time on the lookup benchmark (single gated runs):
holding views per function but calling `tide_list_view_hold` out of line
saved 2% (clang didn't inline it); forcing the hold and element reads inline
saved 7%; setting an existing key's value through the view instead of
`tide_map_insert` (two prepares, a lookup of its own, a fresh block lookup to
write) another 10%. What's left, profiled: the probe's own chain, hash (four
64-bit multiplies) → slot → entry → key compare, about 14 ns a lookup
against C's 2 ns, whose keys sit in a 64 KB array of their own and whose
hash is two 32-bit multiplies. Shortening it would change the hash or the
entry layout, which every snapshot and world hash pins, or the chunk size
(the element-to-chunk division is on the chain).

Linux cache groups have no number here: WSL2 shows one L3 for all 24 CPUs,
so the pool there is as it was. Its parsing and pinning were checked on
made-up trees (notes/schedule-and-threads.md).

Per-row scratch resets and heap flushes inline (notes/ecs.md), paired six
rounds against the first round's binaries (`perf-pass-rows2-*`, which also
counts `tide_list_cache_fill_mut` finding a block once): versus_rollback
1.934 → 1.728 ms a period on one thread, 1.13× → 1.04× its dirty-block C;
with movers spread 2.889 → 2.718 (1.29× → 1.22× C); versus_systems as it was
(1.09-1.11× at 4,096 a kind, 2.6× at 64, 1.67× empty: they make no text or
lists). Against the map views' binaries (`perf-pass-rows-*`) the other
programs and sessions are where they were on one thread; on 12 threads
particles' runs split between about 0.12 and 0.25 ms on both sides, the
pool's choice of cache groups flipping under other agents' load that day.

## Performance pass against handwritten C, 2026-10-20

The owner asked for a performance pass over everything (threading, GPU,
simulation), each pushed to its limits against hand-coded optimal C for the
same case, as a baseline. This section is the pass's summary and its CPU
simulation part. The two below are its rollback, schedule and thread-scaling
part and its drawing part, done on branches of their own and merged with it.

Method: Windows 11, Ryzen 9 7900X (two CCDs of six cores, 32 MiB L3 each),
RTX 4080 SUPER, Clang 23.1.2, release preset. Every run holds the shared
`Local\TideClaudeContinuationBenchmark` mutex after a quiet spell with no
builds, tests or benchmarks, and is marked `competingWork` when one starts
meanwhile (`bench/results/perf-pass/run.ps1`; `bench/README.md` lists the
tools). Before is `b27a4d90`, dev when the pass began, its binaries frozen as
`perf-pass-baseline`; after is this branch with dev merged in, built at
`aaa64e87` (rows say `45733126+local`, the runner's own commit).
The numbers come from one run of both in turn, before then after, the next
round after then before, five rounds (`perf-pass-end-before-r*` and
`-after-r*`; one of 150 runs, a Vulkan draw before, had competing work):
medians of each run's median tick, and Tide/C the median of each run's ratio
against the fastest C way in that run (`node
bench/results/perf-pass/summarize.mjs perf-pass-end-before
perf-pass-end-after`). A first attempt, `perf-pass-final-*`, stopped after two
rounds when another session's test suite ran for half an hour; its rows stay
in the ledger, unused (its churn on 12 threads read 0.80 ms under that suite).
Each run's binaries (SHA-256), arguments, times and competing work are in
`bench/results/perf-pass/<label>-metadata.json`.

| ms a tick (C way) | 1 thread before → after | Tide/C | 12 threads before → after | Tide/C |
| --- | ---: | ---: | ---: | ---: |
| particles, 1M (C) | 0.452 → 0.459 | 1.12× | 0.185 → 0.091 | 1.10× |
| particles, 16 snapshots kept | 0.982 → 1.022 | 1.07× | 0.655 → 0.583 | 0.93× |
| wide, 1M (C) | 1.381 → 1.502 | 1.27× | 0.789 → 0.817 | 1.17× |
| sand fixture, 1024² (C, lookup) | 1.011 → 0.969 | 2.35× | 0.217 → 0.215 | 2.90× |
| life, 1024² (C) | 0.580 → 0.421 | 5.45× | 0.199 → 0.118 | 1.53× |
| churn, 2,000 a tick (C) | 0.414 → 0.421 | 2.11× | 0.398 → 0.397 | 1.98× |
| events, 200k turrets (C) | 0.888 → 0.878 | 7.43× | 0.880 → 0.879 | 7.42× |
| n-body, 2,000 (C, SIMD) | 3.413 → 3.418 | 1.73× | 0.385 → 0.412 | 1.75× |
| buckets, 4,096 (C) | 0.0867 → 0.0619 | 8.60× | 0.0869 → 0.0619 | 11.9× |
| lookup, 16k keys, 1,024 changes (C) | 1.776 → 1.338 | 4.23× | 1.777 → 1.347 | 4.20× |
| lookup, 16k keys, steady (C) | 1.875 → 1.368 | 10.1× | 1.879 → 1.371 | 8.40× |

Wide's single thread moves between 1.33 and 1.50 ms from run to run with no
change of its code: the pass's other pairing below has it 1.475 → 1.328.
Buckets' C on 12 threads moves between 0.005 and 0.018 ms, so that ratio says
little. N-body on 12 threads is the desktop pool's choice of cache groups
trying the one group now and then (notes/schedule-and-threads.md).

Sessions (`bench_sand`, `bench_gallery`, medians of five): Sand's client
simulates 30.6 → 14.9 ms per simulated second, its update median 0.546 →
0.260 ms and p99 0.873 → 0.456, its snapshots 0.303 → 0.142 ms per second,
the server's simulation 3.04 → 1.51; Gallery's are as they were (client 1.81
→ 1.86 ms per second, update 0.072 → 0.078 ms). Replays, bytes sent, catch-up
times and end hashes (Sand 0x953b9562d90bd9bb at tick 736, Gallery
0x365586644fdce54f at tick 1733) are the same before and after. Sand's
session ticks are small and memory-bound on twelve threads, which is where
keeping a tick on one CCD pays.

The other measurements, the same day and gated the same way:
`perf-pass-pool-*` pairs the binaries from just after the desktop pool change
(`59789b32`, before the other two parts and dev's 24 commits merged) with the
final ones: particles, wide, sand, life, churn and n-body all within noise of
each other, so nothing merged afterwards slowed them. `perf-pass-end-only`
runs the programs with no before: versus_rollback 1.15× its dirty-block C
(1.19× with movers spread), versus_systems 1.10× at 4,096 entities a kind,
2.53× at 64 and 1.67× empty, churn 1.95-2.20× C on 1 to 12 threads, and the
hand-written drawing (Drawing against handwritten OpenGL and Vulkan, below:
Tide's totals are 1.5× C's for an empty Vulkan frame, about 4× for rects and
circles, 9× round rects, 11× cubes, 14× textured quads).

What changed, and where it's described:
- Cached list writes skip preparing a list the cache already holds writable,
  and a grid's windowed parallel loop whose step writes its own cell first
  doesn't copy its window (notes/language.md, Performance pass).
- Maps find their two lists once a lookup, hash keys of up to 16 bytes
  inline, and `tide_list_prepare` returns inline for a world's own lists
  (notes/ecs.md, Performance pass).
- Grid windows plan their row runs once and copy small runs inline, and a
  task's whole-chunk put compares and copies in one pass
  (notes/schedule-and-threads.md, Grid parallel loops, performance pass).
- The desktop pool keeps a tick on one cache group where that's quicker
  (notes/schedule-and-threads.md, Cache groups in the desktop pool): the
  12-thread gains above and Sand's session.
- Lane bookkeeping inline and the tick's threads taking tiny tasks a few at a
  time (Rollback, the schedule and thread scaling, below).
- The renderer: partial readback, shape runs, Vulkan upload memory, recording
  reads (Drawing, below).
- A correctness fix the release suite found: attached local fields were
  `restrict` while query rows wrote them (notes/language.md, Query rows reach
  attached local fields, Correction).

Where Tide stays behind, and why (profiled with `tide_sample`):
- Events, 7.4× (3.7× since: Events, second round, below): every hit resolves
  its target entity to a row (generation, table, chunk, lane) and then follows
  a chain of dependent loads; C indexes its targets directly. A C way that defers its hits as Tide's events are
  (`C, deferred` in versus_events) is 1.6× immediate C, so deferral isn't
  most of it: Tide is about 4.7× deferred C. Record size is about 15%.
  Applying them on threads doesn't help even in C (taking the hits on 4 to 12
  threads, each its own targets, 0.5 ms against 0.2 on one: every thread
  reads every hit).
- Life, 5.5× on one thread: LLVM narrows only part of the kernel (it compares
  at 32 bits where C counts sixteen byte cells at a time), and gathering a
  window and putting cells back is memory traffic C's two flat byte grids
  with a dead border, taking turns, don't have. On 12 threads it's 1.5×.
- Lookup, 4-10×, and buckets: a probe is a chain of dependent loads through a
  chunked list (directory, page, element, for the index and again for the
  entry) on a random pattern.
- Churn, 2×: entity-table cache misses, and structural changes are applied
  serially.
- Wide, 1.2-1.3×: 8 KiB lane pages streamed against C's contiguous arrays;
  stores that change nothing are already gone.
- N-body, 1.7×: C is explicitly 4-wide SIMD across bodies; tidec's 4-lane
  batch of the outer loop does vectorize (packed `sqrtps`/`divps`) but only
  partly, the rest in how the lanes are fed.
- Rollback and systems: the page hash and fixed per-task costs (below).

Proposals that need the owner, none started:
- A per-function view of a list or map (resolve heap, directory and pages
  once for a loop) would take lookups and buckets most of the way to C, but
  it is a new runtime function generated code calls.
- Events: dispatch fused with sending where one handler consumes an event,
  or applied in parallel by target; either changes how events are scheduled.
- Cache groups on Linux and Android (sysfs `shared_cpu_list`,
  `pthread_setaffinity_np`): untestable here, as WSL2 shows one L3.
- N-body: tidec emitting explicit vector code for its lane batches.
- A faster page hash (Rollback, below): changes every pinned hash and the
  protocol version. Measured and done, as the owner asked: Page hash,
  measured, below.
- Drawing: a `gpu.h` call handing out ring room so shapes are built once, and
  a quad/sprite path (Drawing, below). The renderer's quads were built
  internally in a second round (Drawing, second round, below); a sprite call
  or quad command in the list remains an API question.

Validation of the merged branch: warning-free native and web debug builds;
all 2,477 native debug CTests (the Vulkan SDK's SPIR-V checks and 36 GPU tests
under the validation layer included) and all 2,383 web debug CTests pass, run
again after the `restrict` fix; all 2,439 native release CTests pass (no
Vulkan SDK in that build). Linux, macOS, Android, MinGW and sanitizers were
not run; no CI was dispatched.

## Page hash, measured, 2026-10-20

The pass left the page hash as rollback's largest gap (`tide_hash_wide`, four
of `tide_hash`'s chains: 21 GB/s where wyhash ran 37, so 1.76× C, dirty's hash).
The owner's call: "Measure first", natively and as WebAssembly, where wasm32
has no 64 by 64 to 128-bit multiply (ordinary `-msimd128` SIMD allowed,
relaxed SIMD not), and switch only if a candidate beats the hash there was on
both. One did, and is the engine's now (notes/ecs.md, A page's hash by
stripes; notes/netcode.md, Session protocol 17).

**The tool.** `bench_hash` (`bench/hash/hash.c`, `bench/README.md`, Page
hashes) times every candidate on the shapes a world's hash reads: an 8 KiB lane
page and a 16 KiB page in cache, 512 KiB (L2) and 8 MiB (L3) of pages in a
shuffled order, 256 MiB of 16 KiB pages from memory, and 64- and 1,000-byte
runs. 31 rounds take the candidates in turn on each shape, a few milliseconds a
sample; it prints the median and the best, the best being nearest a hash's own
speed on a shared machine. Before timing, every SIMD way is checked against its
plain definition's bits on both builds. Windows 11, Ryzen 9 7900X, Clang
23.1.2; native release (RelWithDebInfo, x86-64's SSE2 baseline) and web release
under Node 24.21.0; each run under the shared gate (`gate.ps1`) with the
process pinned to one logical CPU (`ProcessorAffinity` 0x100, which the
program inherits), as unpinned runs moved by a third between runs.

**Candidates.** `chains4`, the hash there was. `chains8`, eight of its chains.
`wyhash` (final version 4, bench/versus/rollback's copy) with a 64 by 64 to
128-bit multiply, and `wyhash32` with four 32-bit multiplies in its place, as
wasm32 must. `stripes`: XXH3's accumulation (eight 64-bit sums of 32 by 32-bit
products of key-xored words, plus each word into the neighbouring sum, a
scramble every kilobyte) with keys and an end of our own; its ways: plain C
(`stripes_plain`, the definition, vectorizer off), clang's two-word vectors
(`stripes`, `pmuludq`; `_prefetch` asks for the next kilobyte; `_x2` takes two
stripes a turn as well), AVX2's four-word ones (`stripes_avx2`, `_prefetch`),
and on the web WebAssembly SIMD with two shuffles and `extmul_low`/`_high`
(`stripes_wasm`, `_x2`) or with `i64x2.mul` (`stripes_wasm_mul`, and what
clang makes of the generic vectors). `pairs`: the same with each product taken
across two words (`lo(w0) * lo(w2)` and so on), which `extmul` and NEON's
`umull` take as vectors come. `chains4_prefetch`: the hash there was with the
prefetch.

Natively, GB/s, median (best) of 31 rounds, and nanoseconds a hash for short
runs; `stripes` as the engine ships it is `engine` (AVX2, on this CPU) and
`engine_baseline` (SSE2), `chains4` the hash there was:

| | engine | engine_baseline | chains4 | chains8 | wyhash | wyhash32 | pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 KiB page, hot | 76.7 (80.3) | 39.4 (41.7) | 19.2 (20.5) | 20.1 (20.4) | 35.3 (36.2) | 8.6 (8.8) | 39.5 (41.4) |
| 16 KiB page, hot | 81.9 (83.9) | 41.8 (42.5) | 20.2 (20.5) | 20.1 (20.5) | 35.2 (35.9) | 8.7 (8.8) | 41.7 (42.6) |
| 512 KiB of 8 KiB pages (L2) | 76.8 (78.0) | 40.8 (41.5) | 20.0 (20.2) | 19.9 (20.2) | 34.9 (35.5) | 8.6 (8.8) | 40.2 (41.2) |
| 8 MiB of 8 KiB pages (L3) | 45.7 (57.7) | 37.5 (38.9) | 18.9 (19.6) | 18.8 (19.6) | 32.6 (33.6) | 8.0 (8.6) | 37.0 (39.0) |
| 8 MiB of 16 KiB pages (L3) | 74.0 (76.6) | 39.5 (40.7) | 19.6 (20.0) | 19.3 (20.0) | 33.8 (34.5) | 8.4 (8.7) | 37.8 (40.6) |
| 256 MiB of 16 KiB pages (memory) | 16.3 (18.1) | 12.8 (17.3) | 9.5 (12.0) | 10.0 (12.6) | 11.4 (14.7) | 5.8 (6.8) | 12.8 (14.3) |
| 64 B, ns a hash | 8.4 (8.0) | 8.6 (8.3) | 7.5 (7.2) | 10.1 (9.1) | 2.8 (2.7) | 11.0 (10.7) | 8.5 (8.2) |
| 1,000 B, ns a hash | 26.8 (25.4) | 39.7 (38.0) | 57.3 (55.8) | 58.9 (57.8) | 28.5 (28.0) | 120.9 (117.7) | 42.2 (40.0) |

The other native ways, 8 KiB hot: `stripes` 36.6, `_prefetch` 38.4, `_x2`
40.8 (the shipped baseline's loop), `stripes_avx2` 75.1, `_prefetch` 77.8,
plain C 9.8; from memory the prefetch is worth 12.0 → 13.6 (two-word vectors)
and 15.3 → 15.5 (AVX2), `chains4_prefetch` 10.1 against 9.5 but 17.7 in cache.

Under Node (web release, `-msimd128`), GB/s, median (best); `engine` is the
shuffle/extmul way, `stripes` clang's generic vectors (`i64x2.mul`):

| | engine | chains4 | chains8 | wyhash | wyhash32 | stripes | stripes_wasm_mul | pairs_wasm |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 KiB page, hot | 26.4 (27.0) | 17.4 (17.6) | 19.2 (19.6) | 9.2 (9.4) | 6.7 (6.8) | 15.9 (16.5) | 16.3 (16.7) | 28.3 (29.0) |
| 16 KiB page, hot | 26.9 (27.7) | 17.5 (18.1) | 19.6 (20.1) | 8.7 (9.4) | 6.4 (6.8) | 13.3 (17.0) | 16.7 (17.0) | 29.0 (29.9) |
| 512 KiB of 8 KiB pages (L2) | 26.2 (27.2) | 17.7 (18.2) | 19.1 (19.9) | 8.0 (9.5) | 6.5 (6.8) | 16.3 (16.9) | 16.6 (17.0) | 28.5 (29.2) |
| 8 MiB of 8 KiB pages (L3) | 20.4 (23.3) | 15.5 (16.7) | 16.4 (18.4) | 8.7 (9.0) | 6.5 (6.7) | 14.5 (15.8) | 13.6 (16.0) | 23.4 (26.0) |
| 8 MiB of 16 KiB pages (L3) | 23.3 (25.5) | 16.1 (17.2) | 17.3 (19.1) | 8.7 (9.2) | 6.5 (6.7) | 15.2 (16.3) | 15.3 (16.6) | 25.9 (27.7) |
| 256 MiB of 16 KiB pages (memory) | 11.1 (14.4) | 9.0 (11.5) | 9.5 (11.5) | 6.0 (7.1) | 5.4 (6.0) | 9.4 (10.9) | 9.2 (11.2) | 11.8 (14.9) |
| 64 B, ns a hash | 17.5 (16.9) | 23.0 (22.7) | 24.3 (23.4) | 27.0 (26.0) | 23.5 (22.9) | 17.5 (17.0) | 17.6 (17.3) | 16.6 (15.1) |
| 1,000 B, ns a hash | 60.6 (58.9) | 76.0 (74.4) | 72.1 (70.0) | 124.9 (122.7) | 170.0 (166.8) | 85.5 (83.9) | 85.4 (84.1) | 58.3 (56.7) |

Two stripes a turn on the web (`stripes_wasm_x2`) measured the same as one.
These are the last of three pinned runs each way, the same day; the best
figures of the earlier two were within a few percent of these, while unpinned
runs moved by up to a third.

**Quality** (`bench_hash --quality`, native; the bits are the same everywhere).
Avalanche (each input bit flipped for 40,000 random 8- and 16-byte keys down to
2,000 random 16 KiB pages, 512 input bits sampled on the long ones): the
largest distance of any input-bit/output-bit pair's flip rate from a half is
0.009 to 0.046 for stripes, against 0.009 to 0.049 for the hash there was and
for wyhash, which is the noise of that many pairs (about 4σ). Every one-bit and
two-bit change of a 16 KiB page (two bits 1 to 8,192 bytes apart, 2.1 million
runs), of zeros and of random bytes; the same bit (0, 1, 31, 32, 33, 62, 63 of
each word) in every pair of 64 stripes of 4 KiB of zeros (112,897 runs); zeros
of every length to 16 KiB; and a counter at two places in an 8 KiB page (2^20
values; chi-squared of the low byte 273, 255 expected): no two equal, for
stripes, chains4, chains8 and wyhash. Pairs failed: 848 equal pairs among the
one- and two-bit changes of zeros and 324 in the stripes test. Two words share
a product there, and with zeros a top bit flipped twice leaves only
`±2^31 × (the other word's key half)`, which the key's slide makes equal for
word 0 in stripes s, t and word 2 in s + 2, t + 2. XXH3's own construction,
where a word's product is its own halves', has nothing like it.

**Decision: stripes.** It beats the hash there was on every shape under Node
(1.5× in cache, 1.3–1.45× in L3, 1.25× from memory, 64-byte runs in 17.5 ns where
they took 23) and natively on all but the shortest (2× with SSE2 and 4× with
AVX2 in cache, 1.7× from memory, 1,000 bytes in 27 ns where they took 57; a
64-byte run takes 8.4 ns where it took 7.5, as eight accumulators end in nine
steps where four chains end in five). It's the only candidate that wins on
both: wyhash wins natively and runs at half the speed under Node; chains8
gains a tenth on the web and nothing natively; pairs is 5% quicker than
stripes on the web and failed the quality checks. Ways
kept: two-word vectors with the prefetch and two stripes a turn
(`tide_hash_wide_baseline`, natively), AVX2's with the prefetch where `cpuid`
finds it (x86-64; Box3D's dispatch does the same), and the shuffle/extmul way
on the web. Tried and not kept: `i64x2.mul` (a third slower than extmul under
V8, as V8 makes it three `pmuludq`), two stripes a turn on the web (no
quicker), prefetching the old chains (slower in cache, a tenth quicker from
memory), wyhash's 128-bit multiply on wasm32 (a call to compiler-rt's
`__multi3` each; four 32-bit multiplies written out are slower still).

**End to end** (`run.ps1 -Label page-hash -Before`, three alternating rounds,
frozen binaries of `4a59a73a` before and this change after; 30 runs, none with
competing work; ledger rows `page-hash-before-r*`/`-after-r*`, metadata in
`bench/results/perf-pass/page-hash-metadata.json`). Medians of round medians,
ms:

| | Before | After | |
| --- | ---: | ---: | ---: |
| Rollback, 100k movers: hashes (5), 1 thread | 0.229 | 0.120 | 0.52× |
| ...whole period, 1 thread (Tide / C, dirty) | 2.068 (1.17×) | 1.951 (1.08×) | 0.94× |
| ...hashes, 12 threads | 0.212 | 0.129 | 0.61× |
| Rollback, spread movers: hashes, 1 thread | 0.671 | 0.273 | 0.41× |
| ...whole period, 1 thread (Tide / C) | 2.804 (1.27×) | 2.391 (1.12×) | 0.85× |
| Rollback, 1M movers: hashes, 1 thread | 4.66 | 3.22 | 0.69× |
| ...whole period on 1/2/4/8/12 threads | 32.6/22.2/15.3/14.6/11.9 | 31.4/20.8/14.6/13.2/12.0 | |
| Sand: client update median / p99 | 0.291 / 0.637 | 0.259 / 0.461 | |
| ...client simulate, ms per second | 16.8 | 14.8 | |
| Gallery: client update median / p99 | 0.112 / 0.358 | 0.090 / 0.300 | |

C, dirty's own hash at 100k movers was 0.105 and 0.104 ms in the two builds, so
Tide's hashes went from 2.2× it to 1.15×. Ticks, snapshots and restores moved
within noise. Every way's digest of the worlds (computed from their contents,
not by the page hash) is the same before and after, as are the sessions'
replays, bytes sent and catch-up times; only the end hashes differ (Sand
0x953b9562d90bd9bb → 0xfc2b7b098a2836e9 at tick 736, Gallery 0x365586644fdce54f
→ 0xd756ff9c9dc087de at tick 1733).

The same programs as WebAssembly under Node (web release, the frozen `4a59a73a`
builds against this change's, three alternating rounds, each run under
`gate.ps1`; medians of the rounds, ms; `--save` doesn't reach the ledgers from
WASI, so each run's numbers and the binaries' SHA-256 are in
`bench/results/perf-pass/page-hash-web-metadata.json`): rollback at 100k
movers, hashes 0.220 → 0.159 and the period 2.753 → 2.709 (C, dirty's wyhash
0.59, slow on wasm32);
with spread movers, hashes 0.749 → 0.598 and the period 3.423 → 3.405. Sand's
and Gallery's sessions under Node are as they were (clients' update median
0.72 → 0.71 and 0.09 → 0.09 ms; a client simulates about 42 ms a second of
Sand in both), the hash being a smaller part of a slower tick there. Digests
are the same before and after, and the sessions end on the native builds'
new hashes, bit for bit.

**What moved** (the new numbers are the tests' own output, the same natively
and under Node; `PROTOCOL` 16 → 17): `tests/test_hash.c`'s four; the Sand and
Gallery quick sessions (`bench/CMakeLists.txt`: 0x6ac505971b099ca5 at tick 169
and 0xe8641c8f2826ff50 at tick 272); seven decimal pins in
`compiler/tests/CMakeLists.txt` (nested, arrays, FBX, skinned, chars, JSON,
optional fields); in `compiler/tests/e2e`, rows, random, random local order
(two fixtures), rolls, `assets_same.h`, decode, crossplatform text, and the
world hash and whole and delta bytes' digests in `local_fields_same.h` and
`local_events_same.h`; Box2D's stack, course, kit and sleep (nine) and Box3D's
stack, course, sleep and mesh (eleven), every one a world hash or a sum of
each tick's; Towers' and Sumo's servers' worlds (two each); and CRACKED's
scripted match (`prototypes/cracked`, opt-in, built apart with
`TIDE_PRIVATE_PROTOTYPES=ON`: 0x0a216d532fa6445c natively and under Node). 47
world pins in 22 files. CRACKED's pin was already stale before this change: a
build of `4a59a73a`'s page hash ends its match as 0x514280888af9724c, not the
pinned 0x0267de53757af494, with the same bullets, rounds, points and map the
new build prints (notes/cracked.md).
What didn't move, and was checked: every pin of a simulated value (the
physics packages' bodies, Box2D's own image bytes, Box3D's resident digests,
what was answered; `test_crossplatform.c`, `crossplatform.tide`, skinned
pose bits), the raw packed worlds' bytes and sizes and the delta sizes in
the local fields/events fixtures, and their sessions' bytes sent.

**Validation** (this Windows machine): warning-free native debug, web debug
and native release builds. The full native debug suite, 2,478 tests: the 32
that pin a world hash failed on exactly those checks and pass with the new
numbers, and the whole suite passes again (Vulkan SDK's validation layer and
SPIR-V checks included). The full web debug suite, 2,384 tests, passes,
pinned numbers included, so native and WASI agree on all of them. CRACKED's
64 tests pass natively and under Node in builds of their own. Native release:
the 252 tests matching `tide_tests|tidec_e2e|versus|bench` pass. Linux, macOS,
ARM64 (where NEON's way runs: CI's macOS and Android jobs), MinGW and
sanitizers need CI to confirm the pins; none was dispatched.

## Drawing, second round, 2026-10-20

Round 2 of the renderer against the handwritten baseline (`versus_draw`,
Drawing against handwritten OpenGL and Vulkan, below), internal only, as the
owner decided for quads: no Draw API, generated code or runtime function
changed. What was built is in `notes/rendering.md` (Quads, solid shapes and a
fenced flush) and `notes/drawing.md` (Recording sprites and 3D instances):

- 2D meshes' and text's triangles go to the GPU as 40-byte quad instances
  where two of them are an unturned quad of one color (120 bytes of vertices
  before), and a run of solid smooth shapes carries a 4-byte radius a shape in
  place of a 32-byte extra; both fall back exactly when the run gets anything
  else.
- Recording: a run of sprites keeps its texture's slot without hashing, a
  single 3D instance reads its transform once into the list's matrices and
  culls it there, culling's rounding allowance is worked out only for a box
  past a plane, and growing arrays is apart from what records.
- Vulkan's flush waits for a fence where it waited for the queue to go idle,
  and leaves unused command pools alone.

Every benchmark frame of the 13 workloads is the same bytes before and after
on both backends (`tide_platform_draw --bench --dump-frames`, compared byte for
byte; `versus_draw --compare` against the after frames: 0 pixels differ in any
workload, on either API).

Five gated rounds (`bench/results/perf-pass/run.ps1 -Label draw2 -Rounds 5
-Before`, before `4a59a73a`, after `24c44443`, each program run from both in
turn; `draw2-before-r3`'s OpenGL run had competing work), and five of the C
baseline straight after (`draw2-c`, the same `versus_draw` binary as round 1);
binaries' SHA-256 and times in `bench/results/perf-pass/draw2-metadata.json`
and `draw2-c-metadata.json`. Medians of the rounds' medians, ms a frame,
record / render + readback / total; Tide/C is total over total:

| Vulkan | before | after | C by hand | Tide/C before | after |
| --- | ---: | ---: | ---: | ---: | ---: |
| nothing | 0.000 / 0.074 / 0.075 | 0.000 / 0.053 / 0.054 | 0.000 / 0.049 / 0.049 | 1.52× | 1.09× |
| 65,536 rects | 0.268 / 0.340 / 0.605 | 0.268 / 0.329 / 0.597 | 0.057 / 0.098 / 0.155 | 3.92× | 3.86× |
| 65,536 circles | 0.253 / 0.307 / 0.561 | 0.253 / 0.289 / 0.543 | 0.057 / 0.096 / 0.154 | 3.64× | 3.52× |
| 65,536 textured quads | 1.117 / 1.025 / 2.206 | 0.915 / 0.587 / 1.518 | 0.057 / 0.095 / 0.152 | 14.49× | 9.97× |
| 65,536 cube instances | 1.529 / 0.448 / 1.982 | 1.218 / 0.425 / 1.645 | 0.034 / 0.158 / 0.192 | 10.31× | 8.56× |
| 65,536 round rects | 0.794 / 0.601 / 1.420 | 0.708 / 0.526 / 1.233 | 0.068 / 0.097 / 0.166 | 8.57× | 7.44× |
| rects, text every 1,024 | 0.255 / 0.396 / 0.651 | 0.255 / 0.386 / 0.639 | - | - | - |
| rects, a round rect every 1,024 | 0.255 / 0.476 / 0.729 | 0.254 / 0.350 / 0.604 | - | - | - |
| cubes, a matrix each (C only) | - | - | 0.183 / 0.159 / 0.343 | - | - |

| OpenGL 3.3 | before | after | C by hand | Tide/C before | after |
| --- | ---: | ---: | ---: | ---: | ---: |
| nothing | 0.000 / 0.262 / 0.262 | 0.000 / 0.254 / 0.254 | 0.000 / 0.018 / 0.018 | 14.84× | 14.41× |
| 65,536 rects | 0.267 / 0.425 / 0.693 | 0.267 / 0.424 / 0.691 | 0.063 / 0.118 / 0.183 | 3.78× | 3.77× |
| 65,536 circles | 0.253 / 0.366 / 0.621 | 0.253 / 0.365 / 0.621 | 0.063 / 0.122 / 0.188 | 3.30× | 3.30× |
| 65,536 textured quads | 1.114 / 1.576 / 2.687 | 0.912 / 0.817 / 1.741 | 0.060 / 0.115 / 0.178 | 15.11× | 9.79× |
| 65,536 cube instances | 1.488 / 0.727 / 2.217 | 1.224 / 0.731 / 1.954 | 0.043 / 0.129 / 0.167 | 13.25× | 11.68× |
| 65,536 round rects | 0.793 / 0.826 / 1.619 | 0.707 / 0.622 / 1.334 | 0.064 / 0.137 / 0.200 | 8.08× | 6.66× |
| rects, text every 1,024 | 0.255 / 0.656 / 0.911 | 0.255 / 0.656 / 0.912 | - | - | - |
| rects, a round rect every 1,024 | 0.254 / 0.608 / 0.864 | 0.254 / 0.427 / 0.682 | - | - | - |
| cubes, a matrix each (C only) | - | - | 0.149 / 0.316 / 0.465 | - | - |

The kept-data workloads, Vulkan totals: grid 0.121 -> 0.098, 65,536 lights
read whole 0.159 -> 0.152, read in part 0.139 -> 0.134, a list made each frame
0.110 -> 0.091, a moving mesh 0.346 -> 0.321 (the fenced flush); OpenGL's are
as they were (0.103-0.360). This round's before is slower than round 1's after
in a few rows (Vulkan's empty frame 0.065 there, 0.075 here) on the same
binaries' code: the machine, with other sessions' GPU work, varied between
the days' runs; compare each table's columns with each other.

Where the time went, from temporary per-phase timers (not committed; means
over 120 frames, Vulkan, before against the build of the gated A/B that
preceded the final one): textured quads' conversion 474 -> 350 µs and upload
376 -> 131 µs (7.8 MB a frame to 2.6); round rects' upload 192 -> 105 µs (3.7
MB to 1.8) while their conversion rose 257 -> 300-325 µs (each shape's extra
is still worked out, to be checked); GPU, submission and readback were about
as they were in both. OpenGL's quads gained most in their upload:
`glBufferSubData` costs more a byte there.

Tried and dropped (numbers in the notes): converting quads through
`mesh_vertex` structs compared a field at a time (slower than the triangles:
conversion 650-760 µs); one barrier call a pass with joined stage masks (no
change beyond noise); a sprite's vertices copied 8 bytes at a time (recording
0.91 -> 0.98 ms).

What remains, and why:
- Rects and circles, about 3.5-3.9 times C: recording a 44-byte command a
  shape (about 4 ns, written to memory past the L2 and read back by the
  renderer) is the floor of the draw list's format, which is public; the
  renderer's conversion (about 2 ns a shape) is close to bound by reading
  those commands; the upload is bound by the bus (1.5 MB at about 20 GB/s).
  Converting straight into the upload memory would save about 0.07 ms
  (`notes/rendering.md`, Not built: it needs a `gpu.h` call whose room lasts
  across a flush).
- Textured quads, about 10 times: recording is 0.91 ms of 1.52, 14 ns a quad,
  most of it 128 bytes of vertices and 24 of indices a quad in the list's own
  format (memory traffic and a `memcpy` call), plus the benchmark's own
  building of them; the renderer then reads them all back (about 0.36 ms).
  C writes 20 bytes once. A sprite call or a quad command in the list would
  close most of it, and is an API question.
- Cube instances, 8.6 times (4.8 times C with a matrix each): about 18.5 ns a
  recorded instance remains, the frustum test in double precision against six
  planes, the side's determinant, the mesh lookup and run merging, and 4 MB of
  transforms uploaded a frame. A float fast path for culling, exact by an
  error bound, would need float planes kept in the list (a layout change) or
  converted per call.
- Round rects, 7.4 times: 104 bytes recorded a shape (command and shape data,
  0.71 ms, memory-bound), 28 uploaded where C uploads 24.
- OpenGL's empty frame, 14 times C: the hidden window's swap, which the next
  readback waits for (round 1). Skipping it needs uploads that don't go
  through `glBufferSubData` first; not built.
- Vulkan's empty frame is 1.09 times C.

Validation: warning-free native, release and web debug builds; all 2,477
native debug CTests pass (36 `gpu` tests with Vulkan under the validation
layer and synchronization checks, OpenGL twins, `tide_spirv_shaders_valid`
with the renderer's ten shaders); release `tide_platform_draw`, `_targets`,
`_shaders`, `_skinned` and their OpenGL twins and `versus_draw` pass under the
validation layer; all 2,383 web debug CTests pass (WebGL, the D3D11 WebGL run
and the 11 WebGPU pages, which run `runs_drawn_alike` too). The draw test
counts 2,975 `ok`s on Vulkan (both devices) and 1,537 on OpenGL. Android,
Linux, macOS, MinGW and sanitizers were not run; no CI was dispatched.

## Events, second round, 2026-10-20

The pass's largest simulation gap: `versus_events` (200,000 turrets each
sending a `Hit` a tick to one of 50,000 targets, one handler taking it) at
7.4× its C. Where the time was, from `tide_sample` and the disassembly of the
release build at `4a59a73a`:

- Taking the events was 79% of Tide's tick and recording them 19%. Each event
  went round `tide_apply_from`: `tide_queue_next`, the chain's depth, a switch
  on the kind and one on the event, then the dispatcher's lookup, which read
  the target table's count, page array and first handle again for every
  event (a handler writes through byte pointers, so the C compiler can't keep
  them), all inlined into one function short of registers: about 60
  instructions an event (C's loop has 6). 32% of all samples were on the line
  after the handle check, the instructions behind it waiting.
- Records of 24 bytes an event (a `tide_command`'s head and the value), each
  head written by the row that sent it; C, deferred keeps 8.
- Memory, less than it looked: the handle check is a random read of a 400 KB
  column, but with 500 targets, all in L1, the tick took 0.40 ms where 50,000
  took 0.52 (at the third step below), and prefetching never helped.

What changed (notes/language.md and notes/ecs.md, Events in runs), each step
timed in alternating gated runs of scratch builds, Tide on one thread:

1. A system whose whole body is one Send records runs (an item of the queue,
   12 bytes an `int` event), taken in a loop of their own: 0.87 → 0.69 ms.
2. That loop reads the table's count, pages and first handle once, and the
   dispatcher is split so it calls only what follows finding the target:
   → 0.52.
3. Two calls, so the one where the target was found knows its archetype (no
   switch, no gone check), and `tide_table_column_mut` doesn't write back an
   unchanged page pointer: → 0.475.
4. The loop `noinline`, with the registers to itself, and the shared page's
   copy marked unlikely: → 0.45, about 31 instructions an event.
5. Ordinary event items back to back in a page through the same kind of loop:
   versus_events with its Send in an `if` (so not one Send alone, a scratch
   build): 1.12 → 0.92 ms.

The pair (gated, five rounds alternating before, `4a59a73a`, and after,
`0ceefae5`; `events-r2-before-r*` and `-after-r*` in the ledgers; runner
metadata, binaries' SHA-256 and no competing work in
`bench/results/perf-pass/events-r2-metadata.json`):

| ms a tick | Tide before → after | C | C, deferred | Tide/C | Tide/C, deferred |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 thread | 0.882 → 0.439 | 0.119 | 0.187 | 7.42× → 3.68× | 4.72× → 2.36× |
| 12 threads | 0.878 → 0.435 | 0.119 | 0.188 | 7.40× → 3.64× | 4.67× → 2.32× |

On 1, 2, 4, 8 and 12 threads (`versus_events_scaling`, medians of five) Tide
went 0.878, 0.879, 0.886, 0.881, 0.883 → 0.439, 0.445, 0.444, 0.437, 0.431
ms (two of the after runs on 4 threads read 0.62-0.64), C 0.118-0.120 and C,
deferred 0.186-0.188 throughout. Churn 1 thread 0.451 → 0.415 and 12 threads
0.396 → 0.415, its generated C unchanged (12-thread runs 0.38-0.49 on both
sides). Sand's and Gallery's sessions as they were: client simulation 14.6 →
14.6 and 1.64 → 1.64 ms a simulated second, updates 0.257 → 0.256 and 0.068 →
0.069 ms, the same bytes, replays and end hashes (0x953b9562d90bd9bb at tick
736, 0x365586644fdce54f at 1733).

The other versus programs, three rounds of each in turn (`events-r2-others-*`,
metadata beside the first; its revision says `+local` for the ledgers the
first pair had written, the binaries the same): particles 0.448 → 0.447 and
0.088 → 0.089 ms on 1 and 12 threads, wide 1.468 → 1.396 and 0.809 → 0.753,
sand 0.956 → 0.981 (one after run 1.104) and 0.221 → 0.219, life 0.420 → 0.419
and 0.119 → 0.119, n-body 3.42 → 3.40 and 0.410 → 0.382, buckets 0.0619 →
0.0617 and 0.0619 → 0.0624, lookup 1.312 → 1.331 and 1.313 → 1.339, steady
lookup 1.350 → 1.339 and 1.345 → 1.343: within their noise. Of these only
sand's generated C changed, in `PlayerJoined`'s dispatch, which its runs
never reach.

Tried and dropped:

- Prefetching the targets' rows 8, 16 or 32 events ahead: slower every time,
  0.80-1.02 ms against 0.69 in the first loop and 0.52-0.54 against 0.46 in
  the last. The loop is bound by its instructions more than by memory, and
  working out where to prefetch costs half a lookup.
- Taking ordinary items one after another as found, the next one's place from
  `tide_queue_next` each time: 1.25 ms against 1.13 the way it was, on the
  scratch game with its Send in an `if` (each event waited for its place to be
  read); counting them first through `tide_queue_next`: 1.15. Counting them
  from the page's `used` (built): 0.92.
- A per-run cache of each chunk's lane pages a handler writes, written by hand
  into the generated C: 0.45 → 0.425 (6%). Not built: the handlers' columns
  would need a second way of being generated for a few instructions an event.

What's left, 3.7× C and 2.4× C, deferred:

- The loop still takes about 31 instructions an event to C's 6: checking the
  target's handle against its row (a random read C doesn't make, since C
  trusts its index, and a handle can be gone or reused by the event's turn),
  the lane page's ownership for each event (`tide_table_column_mut`: a load, a
  compare and `hashed` written), gathering and putting back the component, and
  the handler. Recording is about 0.08 ms of the 0.44.
- Sends that aren't a system's whole body (in an `if`, a function or a
  handler) still record 24 bytes each through `tide_cmd_push`, whose page's
  `used` and queue's `count` go through memory event by event: about 0.46 ms
  of the 0.92 in the scratch game. Generated code reserving a loop's room in
  the queue ahead would take most of it, which is a new runtime function
  generated code calls: a proposal for the owner, not built.
- The pass's proposals stand: dispatch fused with sending where one handler
  consumes an event, or applied in parallel by target, both changes to how
  events are scheduled.

Validation: warning-free native and web debug and native release builds; all
2,478 native debug CTests (Vulkan SDK checks and 36 GPU tests included), all
2,384 web debug CTests and the 246 native release `tidec_e2e`, `versus` and
`tide_tests` tests pass; cross-platform, random and session pins unchanged.
Linux, macOS, Android, MinGW and sanitizers were not run; no CI was dispatched.

## Rollback, the schedule and thread scaling against C, 2026-10-20

The performance pass asked for what wasn't compared with handwritten C: a
predicting client's snapshots, hashes and rollbacks, the scheduler's fixed and
per-task cost, and how every concept scales from one thread to twelve. Built
and measured on Windows 11, Ryzen 9 7900X (two CCDs of six cores, 32 MiB L3
each), Clang 23.1.2, release preset; `bench/README.md` (Rollback, the schedule
and thread scaling) describes the programs, options and baseline tables.

- **Harness** (`bench/versus/versus.c`): `--threads-list 1,2,4,8,12` runs every
  way on each count; default runs and their output are unchanged, and tables
  print sub-0.1 ms cells to a hundredth of a microsecond. A pool of fewer
  threads now wraps the platform pool's `run_some` and `more` (it used to keep
  every worker awake in every round and had neither), counting the workers in
  a round so no more than the count take part. A concept may name its tick's
  parts (`versus_desc.part_names`, `.parts`, a function per way read after each
  timed tick); their medians print beside the whole tick's with Tide/C ratios,
  and medians and means go into the saved row (`"parts"` under a way). The
  gated runner (`bench/results/perf-pass/run.ps1`) lists the new programs'
  variants and every versus program's scaling, and takes `-Revision` for a
  frozen earlier build.
- **`versus_rollback`**: a period is `--every` ticks, each kept into a ring of
  `--kept` worlds and hashed, then a restore of the world `--back` ticks ago and
  those ticks run again (kept again, not hashed), and a hash of the end checked
  against the first. Tide uses `tide_world_copy`, `tide_world_tick_on` and the
  session's hash (`hash_world_on` with a pool). C copies one block whole with
  `memcpy` and hashes it with wyhash; C, dirty keeps 16 KiB blocks with versions
  its loops bump when they change a bit, copies only blocks whose versions
  differ and rehashes only stale ones. Digests cover the live world, the ring's
  world before the latest tick and the count of mismatched hashes (zero), so
  every way keeps the same ring. Not covered: packing, deltas, inputs and the
  network; replayed ticks are not hashed, as a client hashes a tick when it's
  confirmed.
- **`versus_systems`**: 64 systems over 16 two-float components in four kinds
  (each component in two kinds), twelve independent lanes of four systems and a
  band ten deep; `--count` from 0. The kernels are memory-bound, deliberately
  cheap: what's measured is the tick around them. C runs the same loops in
  order, or in precomputed stages on `versus_for` from 16,384 entities a kind,
  where it measured faster.
- Tests: `versus_rollback`, `_spread`, `_deep` (`--kept 3 --back 5 --every 1`),
  `versus_systems` and `_empty`, native and web, `--quick` (forced C splitting).

Baseline: three gated rounds at `1c6befb1`, `coverage-baseline-r1..r3` in
`bench/results/versus.jsonl`, runner metadata (arguments, binary SHA-256s,
competing work) in `bench/results/perf-pass/coverage/baseline-metadata.json`;
round 2 of the million-mover scaling run overlapped other builds and is marked.
Medians of round medians, ms a period, one thread:

| Rollback, 100k movers (25% moving, first) | Tide | C | C, dirty | Tide/C | Tide/C, dirty |
| --- | ---: | ---: | ---: | ---: | ---: |
| Period | 1.813 | 2.166 | 1.544 | 0.84 | 1.17 |
| 12 ticks | 1.607 | 0.882 | 1.244 | 1.82 | 1.29 |
| 12 snapshots | 0.026 | 0.816 | 0.180 | 0.03 | 0.14 |
| 5 hashes | 0.180 | 0.345 | 0.102 | 0.52 | 1.76 |
| 1 restore | 0.0018 | 0.104 | 0.014 | 0.02 | 0.13 |
| Spread movers: period / ticks / hashes | 2.506 / 1.788 / 0.631 | 2.107 / 0.881 / 0.344 | 2.764 / 1.242 / 0.330 | 1.18 | 0.91 |
| 1M movers, 40k turrets: period / ticks / hashes | 27.76 / 22.85 / 3.90 | 32.11 / 14.93 / 4.64 | 25.88 / 19.90 / 1.21 | 0.85 | 1.08 |

On 1, 2, 4, 8 and 12 threads the million-mover period is Tide 27.8, 22.3, 16.5,
13.6, 12.9 ms; C 32.1, 30.7, 24.8, 21.9, 22.2; C, dirty 25.9, 17.6, 12.7, 9.9,
10.9. Systems at 0/64/4,096/16,384/65,536 entities a kind are 1.50/3.13/1.12/
1.14/1.36× C on one thread; at 16,384 Tide is 0.258, 0.199, 0.136, 0.202 and
0.308 ms on 1/2/4/8/12 threads (0.87× C at 4) and at 65,536 1.292, 0.817,
0.488, 0.526, 0.465. Every versus program's curve is in `bench/README.md`.
Past six threads this machine's curves mostly measure traffic between its two
CCDs, as the harness can't choose cores: memory-bound loops (particles,
systems) are fastest on four to six threads in both languages.

What the parts show, and where the time went (the coordinator's sampling
profiler, `tide_sample`, on Tide-only builds of the fixtures):

- Snapshots and restores cost Tide 30–50× less than whole copies and 5–7× less
  than versioned blocks: they share pages. The cost moves into the ticks: with
  a ring kept, every chunk a system changes is written apart into new pages
  and compared with the shared ones (`tide_table_lanes_end`), and Tide's 12
  ticks take 1.82× C's in place. Move's rows function alone is close to C's
  loop (about 1.1–1.2×); the rest is the page comparisons and copies (about 15% of
  Tide's period in the C runtime's `memcmp`/`memcpy`), Aim's rows (a system
  that writes a list: its rows don't vectorize and each calls
  `tide_scratch_reset` and `tide_heap_flush`), and page taking and giving.
- The hash: per changed page Tide is as incremental as C, dirty, and slower
  only by its function: `tide_hash_wide` runs at 21 GB/s here where wyhash
  runs at 37 (a scratch microbenchmark, 8 KiB to 4 MiB in cache; an
  XXH3-style SSE2 loop ran 1.4–1.9× `tide_hash_wide` in a noisier run), which
  matches the 1.76× at 100k movers. At a
  million movers it's 3.3×, about 8 GB/s, which fits new pages coming from
  memory each tick; not measured apart.
- Systems: with 64 entities a kind about a quarter of the samples were in
  `tide_table_lanes_shared`/`_begin`/`_end`; with 65,536 the loops are
  memory-bound in 8 KiB lane pages (one per 2,048 rows) where C streams whole
  arrays, and the first touch of each page's header shows (`lanes_shared`).
  On threads, 12 workers spent 14% of their samples looking for work
  (`work_once` scanning every system from the first) and 4% counting finished
  tasks, with 1,024 tasks a tick of about 0.25 µs each at 16,384 a kind.

Optimizations kept (engine/src/jobs.c and tide/table.h; notes/ecs.md and
notes/schedule-and-threads.md have the contracts):

- Lane bookkeeping inline for lanes the table alone has, with shared-lane work
  in per-file `noinline` helpers: systems at 64 a kind 4.8 → 3.9 µs a tick
  (alternating runs; 4.1 with everything inline, which grew the executable
  more), at 4,096 65.2 → 63.8 µs. Executable +34 KB (6%).
- On threads: cheap tasks taken a few at a time (`BATCH_WORK`), the search
  for work starting at the first system with anything left, and each system's
  state on its own cache line: systems at 16,384 a kind 0.20 → 0.175 ms on
  2 threads and 0.14 → 0.12 on 4 (alternating runs; 6 and 12 threads vary
  more than that between runs). `jobs_cheap_tasks_are_taken_a_few_at_a_time`
  pins the takings.

Two gated rounds alternate the frozen baseline binaries with the optimized
ones (`coverage-pair-before-p1`, `-after-p1`, `-before-p2`, `-after-p2`; their
runner metadata is in `bench/results/perf-pass/coverage/`). The machine was
busy with other sessions' builds and suites and the owner's own work: round
1's before pass is visibly slower than the baseline in its C ways too, and
three of its runs are marked as overlapping builds. Round 2, the cleaner,
before → after, Tide ms a tick (best C before → after in brackets):

| Program | 1 thread | 2 | 4 | 8 | 12 |
| --- | --- | --- | --- | --- | --- |
| Systems, 64 a kind | 0.0048 → 0.0039 (0.0015) | | | | |
| Systems, 4,096 a kind | 0.0674 → 0.0630 (0.0591 → 0.0573) | | | | |
| Systems, 16,384 a kind | 0.268 → 0.249 (0.231 → 0.225) | 0.206 → 0.171 (0.179 → 0.170) | 0.143 → 0.114 (0.177 → 0.152) | 0.304 → 0.169 (0.360 → 0.288) | 0.244 → 0.301 (0.279 → 0.352) |
| Systems, 65,536 a kind | 1.379 → 1.227 (0.907 → 0.938) | 0.863 → 0.866 (0.561 → 0.656) | 0.492 → 0.407 (0.395 → 0.422) | 0.904 → 0.415 (0.834 → 0.588) | 0.768 → 0.349 (0.879 → 0.640) |
| Rollback, period | 1.990 → 1.812 (C, dirty 1.727 → 1.495) | | | | 1.859 → 1.769 (1.714 → 1.852) |

Systems at 64 a kind go from 3.20× C to 2.60×, and from 1.14× to 1.10× at
4,096, as in the alternating runs above; on 2 and 4 threads at 16,384 a kind
Tide is 17–20% faster and moves from 1.15× to 1.00× and 0.81× to 0.75× C. At 8
and 12 threads both languages' times move by up to half between passes, so
those cells say nothing about the change. The rollback period and the other
programs' curves (particles, wide, sand, life, n-body) change within what C's
own times moved between the passes; nothing there is claimed. The paired
rows' revisions were first recorded wrongly: `run.ps1`'s `-Revision` and its
`$revision` were one variable (PowerShell names ignore case), so the before
rows said `188cc9b4`; the parameter is now `-BuiltFrom`, and the before rows
and their metadata say `1c6befb1`, the build they ran. A final gated paired
measurement of the merged branch is the coordinator's.

Tried and dropped: generated apart rows functions that tracked, lane by lane,
whether any row changed a bit (as C, dirty does), so unchanged lanes gave
their page back without `tide_table_lanes_end`'s comparison. The rollback
period went 1.584 → 1.649 ms on 100k movers (three alternating runs each; the
tracking cost more than the comparisons of pages still in cache) and was
within noise at a million, so the codegen change was reverted. Removing Aim's
per-row `tide_scratch_reset`/`tide_heap_flush` calls by hand in generated C
(safe for that body only) took the ticks from 1.66 to 1.58 ms, an upper bound
for making those calls cheaper.

Remaining gaps, largest first, with the best explanation:

1. The page hash's speed (1.76× C, dirty; 3.3× at a million movers). A faster
   page hash (wyhash's 64×64→128 multiply, about 1.75× here, but emulated on
   wasm32; or XXH3's 32×32→64 SIMD multiplies, about 1.2–1.5× here at SSE2 and
   native on wasm SIMD) would change every pinned world hash and `PROTOCOL`, as
   `tide_hash_wide` did on 2026-10-07: the owner's call, with web throughput
   measured first. Done the same day, after measuring (Page hash, measured,
   above): 1.15× C, dirty's hash at 100k movers.
2. Ticks under a kept ring (1.8–2.0× C in place): writing every changed
   chunk's lanes apart and comparing them afterwards is the price of sharing.
   Tracking changes in the loop didn't pay (above). Writing in place when only
   a few rows change would need a different page scheme.
3. Per-row costs of systems that touch text or lists: two calls a row
   (`tide_scratch_reset`, `tide_heap_flush`) where nothing was made or let go,
   about 5% of the rollback ticks here; inline fast paths in `tide/text.h` and
   `tide/heap.h` (a thread-local compare, a `pending` test) would recover most
   of it without new generated calls.
4. Small worlds: 2.6× C at 64 entities a kind after the change, about 20 ns a
   chunk-task of fixed cost (task dispatch, scratch mark, lanes, the rows
   call) against C's direct loops.
5. Big memory-bound worlds: 1.3–1.4× C at 65,536 a kind, from 8 KiB lane
   pages against contiguous arrays.
6. Thread scaling past one CCD: shared by C; the platform pool's topology is
   the coordinator's (platform/src/jobs.c).

Validation on this branch: native release and web debug build every target
warning-free; 247 native release tests (all `tidec_e2e_*`, `tide_tests`,
`tide_platform_jobs`, every `versus_*` and `bench_*`) pass but one, and 47 web
debug tests (`tide_tests`, platform jobs and web threads, rows, world-set and
schedule fixtures, every `versus_*`) pass. The one,
`tidec_e2e_query_local_fields` (two checks of attached local fields) failed in
the release build with this branch's engine changes reverted too, and passes
on web debug: it predates them (a `restrict` on attached local fields that
query rows also write; fixed on the merged branch, notes/language.md, Query
rows reach attached local fields, Correction). The full native and web debug
suites were not run here; the coordinator ran them on the merged branch.

## Drawing against handwritten OpenGL and Vulkan, 2026-10-20

The owner asked for a performance pass comparing each part of the engine
with hand-coded optimal C for the same case. For drawing, the missing
coverage item was a handwritten graphics baseline. `versus_draw`
(`bench/draw/`, desktop only; `bench/README.md`, Drawing) now draws
`tide_platform_draw --bench`'s nothing, 65,536 rects, circles, textured
quads, cube instances and round rects by hand, in OpenGL 3.3 core
(`draw_gl.c`) or Vulkan 1.3 (`draw_vk.c`), as an expert writes these cases: a
static quad or cube, one instance buffer written straight into GPU-visible
memory, one pipeline and one instanced draw a workload, minimal state, one
pixel read back. Same 256 by 352 target, 20 warmup and 100 measured frames,
same shapes, colors, camera and timing split (record; render + readback;
total). Its frames are byte-identical to Tide's on both APIs for all six
workloads (`versus_draw --compare` against `tide_platform_draw --bench
--dump-frames`). Choices measured while writing it: OpenGL's instances are
written through an unsynchronized map (orphaning with
`GL_MAP_INVALIDATE_BUFFER_BIT` had NVIDIA's driver write 65,536 rects in 0.33
ms instead of 0.05; OpenGL 4.4's persistent mapping matched the
unsynchronized map; `glBufferSubData` cost about 0.18 ms more); Vulkan's are
in device-local host-visible memory (65,536 rects 0.146 ms a frame against
0.18-0.20 in host memory). The baseline re-records its command buffer every
frame and doesn't overlap frames, as Tide's benchmark doesn't. It also draws
the cubes with a whole matrix each, Tide's data, as a seventh workload. Rows
have `"competitive_baseline":true` and `"way":"C"` with the API and device;
`node bench/compare.mjs draw --tide <label> --c <label>` prints them side by
side. `tide_platform_draw --bench` gained `--only <n>` and `--dump-frames`.

| Vulkan, ms | Tide before: record / render / total | Tide after | C by hand | Tide / C before | after |
| --- | ---: | ---: | ---: | ---: | ---: |
| nothing | 0.000 / 0.137 / 0.137 | 0.000 / 0.065 / 0.065 | 0.000 / 0.047 / 0.047 | 2.50× | 1.39× |
| 65,536 rects | 0.338 / 0.740 / 1.075 | 0.265 / 0.325 / 0.590 | 0.057 / 0.090 / 0.147 | 7.60× | 4.01× |
| 65,536 circles | 0.339 / 0.716 / 1.054 | 0.252 / 0.284 / 0.536 | 0.057 / 0.088 / 0.145 | 7.36× | 3.71× |
| 65,536 textured quads | 1.104 / 2.037 / 3.160 | 1.100 / 0.905 / 2.005 | 0.057 / 0.089 / 0.146 | 22.54× | 13.70× |
| 65,536 cube instances | 1.580 / 0.605 / 2.193 | 1.491 / 0.414 / 1.904 | 0.034 / 0.147 / 0.181 | 12.57× | 10.52× |
| 65,536 round rects | 0.796 / 1.499 / 2.294 | 0.786 / 0.544 / 1.331 | 0.068 / 0.089 / 0.158 | 14.96× | 8.44× |
| rects, text every 1,024 | 0.340 / 0.800 / 1.139 | 0.252 / 0.372 / 0.624 | - | - | - |
| rects, a round rect every 1,024 | 0.344 / 0.897 / 1.242 | 0.252 / 0.439 / 0.693 | - | - | - |
| cubes, a matrix each (C only) | - | - | 0.183 / 0.149 / 0.333 | - | - |

| OpenGL 3.3, ms | Tide before: record / render / total | Tide after | C by hand | Tide / C before | after |
| --- | ---: | ---: | ---: | ---: | ---: |
| nothing | 0.000 / 0.172 / 0.172 | 0.000 / 0.144 / 0.145 | 0.000 / 0.012 / 0.012 | 9.14× | 11.75× |
| 65,536 rects | 0.337 / 0.747 / 1.084 | 0.266 / 0.403 / 0.669 | 0.055 / 0.110 / 0.167 | 6.07× | 4.00× |
| 65,536 circles | 0.338 / 0.695 / 1.037 | 0.252 / 0.358 / 0.610 | 0.057 / 0.112 / 0.174 | 5.99× | 3.51× |
| 65,536 textured quads | 1.105 / 2.237 / 3.343 | 1.105 / 1.421 / 2.522 | 0.055 / 0.109 / 0.164 | 20.31× | 15.35× |
| 65,536 cube instances | 1.586 / 0.723 / 2.321 | 1.488 / 0.694 / 2.178 | 0.031 / 0.129 / 0.159 | 14.32× | 13.67× |
| 65,536 round rects | 0.801 / 1.591 / 2.395 | 0.787 / 0.780 / 1.567 | 0.058 / 0.127 / 0.189 | 12.59× | 8.28× |
| rects, text every 1,024 | 0.338 / 0.962 / 1.304 | 0.252 / 0.637 / 0.893 | - | - | - |
| rects, a round rect every 1,024 | 0.344 / 0.924 / 1.271 | 0.253 / 0.588 / 0.844 | - | - | - |
| cubes, a matrix each (C only) | - | - | 0.144 / 0.299 / 0.447 | - | - |

Medians of three gated runs' medians, ms, Ryzen 9 7900X / RTX 4080 SUPER
(NVIDIA 617.42) / Windows 11 / Clang 23.1.2, release. Before is `6278df68`
(`perf-draw-before-r1..3`), after is `dd171ccd` (`perf-draw-final-r1..3`, its
binaries; the tree had uncommitted documentation, hence `+local`), each with
Tide on both backends and C in both APIs alternating; C is the same binary in
both, and each ratio is against its own run's C (C's after column shown). The
kept-data workloads (grid, lights, moved mesh) have no C counterpart and are
in the ledger; all five are faster on both backends (Vulkan totals 0.17-0.44
-> 0.10-0.32 ms, OpenGL 0.12-0.41 -> 0.09-0.34), mostly from the readback.
Tide's record is its draw list; its render part converts, uploads, draws and
reads back the pixels asked for. C's record writes instances into GPU memory.
Compare totals. C's OpenGL empty frame moved between runs (0.019 before,
0.012 after), so that ratio says little; Tide's 0.172 -> 0.145 is mostly its
hidden window's swap (below).

What changed (`notes/rendering.md`, Readback, upload memory and runs of
shapes; `notes/drawing.md`, Recording reads what its caller just wrote):
`tide_platform_read_pixels` reads only the box around its points (Vulkan's
whole-target read and swizzle was an eighth of the benchmark's own CPU
time); runs of plain and smooth shapes convert in one loop each and colors
convert without branches; Vulkan's per-frame upload ring lives in
device-local host-visible memory; recording reads a call's color and
transform a float at a time, as Windows passes them by address and a wide
load over the caller's narrower stores waits for them. Every one of the 13
workloads' frames is the same bytes before and after on both backends.

Tried and dropped, with numbers in those notes: OpenGL swapping the hidden
window only when a frame drew in it (the empty frame 0.131 -> 0.034 ms, but
uploads 1.5-5 times slower: NVIDIA recycles `glBufferSubData` memory at
swaps), a cache of converted mesh corners (quads' conversion 0.82 -> 1.4
ms), and word-at-a-time copies of just-built vertices and shapes (no gain or
slower). Profiling used the coordinator's sampler (`bench/profile/sample.c`
on the performance-pass branch) and temporary per-phase timers.

What remains, and why (Vulkan, after; the parts of a frame are from the
temporary per-phase timers, means over 120 frames):
- Rects and circles, about 4 times C: two passes. Tide records a 44-byte
  command through an exported call (about 4 ns a shape), then converts each
  to a 24-byte instance (about 0.14 ms for 65,536) and copies those into the
  upload ring (about 0.085 ms), which the GPU copies again before drawing. C
  writes 20-byte instances once (0.057 ms). Building instances straight into
  GPU-visible memory would need an upper bound on a list's shapes and a
  `gpu.h` call that hands out room in the ring.
- Textured quads, about 14 times: the generic mesh path. A quad is recorded
  as 4 vertices of 32 bytes and 6 indices (about 17 ns), and expanded into 6
  vertices of 20 bytes (7.8 MB a frame: about 0.6 ms converting, 0.4 ms
  uploading). C sends one 20-byte instance a quad. Recognising quads, or a
  sprite call, would be an API or renderer design question for the owner.
- Cube instances, about 10.5 times: about 23 ns a recorded instance (mesh
  lookup, frustum culling in double precision, a 64-byte transform, its
  determinant for the side drawn, run merging) and 4 MB of transforms a
  frame where C's positions are 0.75 MB. With Tide's data (a matrix each)
  C takes 0.33 ms in total, so most of the gap is the recording path.
- Round rects, about 8.4 times: 104 bytes recorded per shape (the command
  and the shape's own 60), converted to 56 (instance and extra) and
  uploaded, against C's 24.
- OpenGL's empty frame stays near 0.15 ms against C's 0.01-0.02: Tide's frame
  loop swaps the hidden window between frames, outside the timed part, and
  the next readback waits for that swap (C swapping the same way measured
  0.025 -> 0.111 ms); skipping it was the setback above.
- Vulkan's empty frame (0.065 ms against C's 0.047) is most likely its
  submission path: a command buffer a pass with its barriers, and
  `vkQueueWaitIdle` where C waits for one fence. Not measured apart.

Measurement hygiene: every timed run, exploratory ones too, held the
shared `Local\TideClaudeContinuationBenchmark` mutex after a quiet period
with no builds, tests or benchmarks; the scratch gate also waited for any
other process using more than half a core (other agents' game runs), except
one Claude Code `pwsh` of another session that had spun a core since
2026-10-16, which was running for before and after alike. The runner
(`bench/results/perf-pass/run.ps1`, which gained the Vulkan baseline entry)
has to be invoked in-process (`& ./run.ps1 -Only a,b`): through `pwsh -File`
the list arrives as one string and nothing runs. An after run labelled
`perf-draw-after` (Tide) / `perf-draw-after-c` (C, rerun separately when
LLVM was briefly uninstalled from the machine and the baseline couldn't be
relinked; the identical frozen binary was used) ran under visibly more noise
(p95s two to three times the before run's) and is kept in the ledger but not
used for the table. Other desktop activity and clocks were not controlled.

Validation: warning-free native and web debug builds with Clang 23.1.2 and
Node 24.21.0; all 2,455 native debug CTests pass (36 `gpu`, Vulkan under the
validation layer with synchronization checks, and OpenGL twins, including
the new `versus_draw` and `versus_draw_opengl`, and `bench_harness`, which
checks the draw comparison on fixture rows), and all 2,362 web debug CTests
pass (WebGL, the D3D11 WebGL run, and 11 WebGPU pages). Release
`tide_platform_draw`, `_targets`, `_shaders` and `_skinned` pass on Vulkan
under validation and on OpenGL after each change, and every benchmark frame
was compared byte for byte before and after. Android, Linux, macOS, MinGW
and sanitizers were not run; no CI was dispatched. The Vulkan upload ring's
device-local placement is checked on this NVIDIA GPU alone; integrated and
mobile GPUs, and MoltenVK, take the same path through their own memory types.

## O2 versus O3 on the external stress test, 2026-10-12

The owner requested an optimization-level benchmark after approving optimized
engine code in development builds. At `83ccb219`, identical unoptimized caller
objects exercise the unchanged 2,500-character private FBX fixture against
`-O2 -g` and `-O3 -g` engine libraries, retaining assertions, no fast-math and
no FMA contraction. Five alternating process pairs per platform run 30 warmup
and 300 measured 60 Hz manual-world frames, covering idle/run/jump. All twenty
rows and 6,000 measured frames are kept in
[optimization-levels](../bench/results/optimization-levels/README.md), with
input/source/library/binary identities, scripts, summaries and core-test logs.

On Windows/Ryzen 9 7900X/Clang 23.1.2, median-of-process-mean view recording
is 20.615984 -> 21.646871 ms: O3 is 5.00% slower, and every native pair is
slower (median paired ratio 1.0468). Node 24.16.0 WASI is 28.705785 ->
28.675940 ms (-0.10%), within observed variation. Actual combined tick/hash/
view/preparation intervals are native 20.989503 -> 22.023309 and WASI
29.257243 -> 29.227485 ms. This workload gives no reason to change O2;
production defaults remain unchanged. GPU, display wait, interpolation
snapshots, rollback, networking and Rooms UI are excluded; no FPS is inferred.

The named gate/90-second quiet guard waited through eight detections of other
build/test work before runtime timing. No competing work was detected during
measured processes. Other desktop activity and clocks remain uncontrolled.
A subsequent five-pair cold-engine compilation batch holds the gate and
checks before/after each build; twelve compiler jobs, warm tools/filesystem,
configuration/caller compilation/final links excluded. Medians are native
1.487 -> 1.508 s and WASI 1.278 -> 1.344 s; these small noisy
differences do not establish a general compilation penalty. Earlier setup
build rows precede the quiet guard, are retained and explicitly excluded.

All runs match world hash `818c38d6c6daad37` and accumulated active integer
palette/influence texture hash `aef3539b6e00d762`, with state mask 7, 2,500
characters, one draw command and two active integer textures. Separate O3
core-test builds with optimized callers pass 435/435 on native and WASI,
including unchanged exact math/random/pose pins. The harness builds warning-free
on native/WASI and the analyzer verifies every runtime/compile pair and digest.
No all-platform O3 rollout, full-suite rerun, CI dispatch or deployment was
performed for this measurement-only work.

## Production retained assets and transfer checks, 2026-10-11

The owner asked to finish integration and put it on dev after the standalone experiments below. Production now retains stable immutable content across field copies/snapshots/reload and detaches on write, including ordinary asset values. Already-owned reset storage is reused; same known-ID reassignment does no work. Production networking negotiates exact pack identity, sends known references or sparse asset-relative chunks only when smaller, and retries self-contained state when decoding fails. Raw saves include each retained payload once. This supersedes the earlier experiments' *production integration gaps*; their timing claims and deferred adverse-workload timing gate remain separate.

The unchanged `D:/Projects/Test` 100-character project runs 1,200 deterministic ticks and prepares one 100-instance frame. Native and WASI each retain one 114,224-byte animation payload with 100 leases, in a 6,480-byte match heap. Self-contained raw world bytes are native 128,440/web 128,392 (the local save header still includes platform storage layout). Known-pack whole-world transfer is 6,258 bytes versus 120,482 self-contained: 94.8% fewer bytes. Simulation hash `b63eb72b0cc75f1c`, prepared palette hash `ab1b21c3dd4b57f1` and both packet hashes agree exactly. Both platforms decode each other's packets, including full packets after unload. A fresh native hidden three-frame capture also renders the project. These are structural sizes and exact correctness checks, not FPS, CPU timing or real join traffic/latency measurements.

The machine remains busy with other work; the owner deferred timing tests. No production speedup or ordinary-container overhead claim follows from this run. The earlier read-only prototype gain is encouraging, but production timing of cold detachment, owned reset/edit, read-only reassignment, hashing, views and encode/decode remains pending. Comparing both candidate encodings costs CPU only on eligible asset-origin transfer pages; end-to-end measurements must include it. Sources, structural rows and reproduction are in [asset-production](../bench/results/asset-production/README.md). Full native/web and docs validation results are recorded there once complete. No CI, sanitizers or other desktop/mobile platforms were run for this integration.

Production integration validation above passes all 2,189 native and 2,158 web debug CTests, including browser/GPU coverage, plus the docs build. Initial full-suite failures exposed a local-borrow lifetime issue (retired-pool lookup) and the invalid grid-header scan; both are corrected and covered. Descriptor and portable-wire hash pins were updated only after native/WASI agreement. Later dev integration validation is recorded separately.

Integration with newer dev (`e377d0a9`, optional fields/function task starts/request timeouts) builds every native/web target warning-free and passes 112 native plus 104 web overlapping CTests, including the new features, asset/container ownership, sessions, migration/reload and ordinary sand/gallery controls. The docs build passes again. The asset fixture now stores, reads, clears and retains an asset-backed optional list through snapshots/transfers. Descriptor authentication uses a cached sum/count updated only on lease changes, preserving all pinned hashes; heaps with no asset dictionary avoid the new registry validation allocation/walk. The private project check rerun keeps every structural number and simulation/palette/packet hash unchanged, including both foreign packet decodes after unload. Its source/binary/input identities are refreshed in the artifact directory. A later clean merge of unrelated camera/prototype/docs work does not require repeating these checks.

## Asset-relative value encoding, 2026-10-11

The owner asked to try asset IDs plus changes for saves/transfers, especially
new connections. A standalone C byte-value codec registers the exact immutable
baseline by SHA-256, encodes an unchanged known reference by ID, and uses the
existing delta codec's XOR/unchanged-region compression against the asset for
owned values. Full encoding uses that same codec without a baseline. The smaller
complete full/patch frame wins, including ID overhead. A receiver with that exact
asset version needs no prior world or edit history, reconstructs ordinary owned
mutable bytes and verifies their hash; missing/wrong IDs fail and the independent
full form works without an asset. Production compiler/runtime/protocol, installed
package and user game are unchanged; this is no approved production wire format.

For the first canonical `movementAnims` model blob in the owner's frozen pack
(114,224 bytes), existing compressed full encoding is 107,471 bytes. An unchanged
known reference is 37, owned unchanged 50, one-byte edit 59, and 64 sparse edits
441 (full 107,482). Replacing 4 KiB with deterministic noise is 4,160 versus
107,516; a constant-XOR block compresses to 300 versus 107,247. Dense replacement
uses full at 114,383 bytes, and zeros use full at 158. Appending 257 repeated bytes
is 313 versus 107,478; truncation is 55 versus 107,298. Middle deletion shifts the
remaining bytes and still needs 57,012 versus 107,737: no edit-script compressor
is implemented. The 16-byte reference control is adverse (37 versus 35 full),
while its owned form chooses full. These are exact value-frame bytes rather than
raw-size ratios, whole-world/save sizes, actual join traffic/time or FPS.

Warning-free native/WASI builds and all 38 value cases pass: twelve workloads on
each of 1 MiB noise, repeating-pattern and private animation-byte fixtures, plus
two tiny-value controls. Every reconstructed byte and logical length is checked,
receiver edits leave the asset unchanged, and complete output rows (including
encoded-message SHA-256s) agree across platforms. Each executes 11,252 rejection
assertions for cut frames, trailing bytes, invalid modes, corrupt result hashes,
missing/wrong versions and an oversized logical length; failures preserve caller
output/guards. An initial fixture decoder used uninitialized output, contrary
to `tide_delta_get`'s zeroed-buffer contract; the growing-value check exposed it,
and the final decoder uses `calloc`. No timing evidence came from that run.

The owner has deferred runtime timing measurements while the machine is busy.
No clocks were recorded in this batch. The optional guarded timing runner and
analyzer are ready, with alternating encode/decode order, hashing/compression/
allocation inside timing, and exact message/value checks outside it. Encoding
both candidate forms may cost more CPU; existing eligible joins already reuse
initial-world pages, so an end-to-end comparison remains necessary. Persisted
provenance, stable registry/lifetimes and reload, exact world/allocator/hash
reconstruction, save compatibility, missing-asset recovery, nested/typed values
and all mutation paths remain integration gaps. No production suites, browser/
GPU, other platforms, sanitizers or CI were rerun for these standalone artifacts.
Sources, raw rows, input/library/binary identities, byte-size table, verifier and
reproduction are in
[asset-relative-delta](../bench/results/asset-relative-delta/README.md).

## Owned storage reuse after asset writes, 2026-10-11

The owner asked to investigate the asset-sharing prototype's bulk reset/edit
regression. The naive policy releases an owned list on asset assignment, then
allocates another on the next write before the pending old blocks are flushed.
A scoped candidate copies the asset into an already-owned destination through
the existing `tide_list_set`; empty/still-shared destinations retain the asset
reference and detach on first write. Ordinary owned sources are unchanged.
Only known-asset assignment adds an ownership check; there is no data comparison,
new runtime entry point, allocator/layout change or production storage change.

Instrumented native 16 KiB reset/edit runs confirm the mechanism: naive has
192 pending blocks after assignment and 6,291,456 dirty bytes before hashing;
baseline/reuse each have zero pending blocks and 3,145,728 dirty bytes. Reuse
restores the original 3,146,600-byte heap high-water mark from naive's 6,293,096,
with original heap hash and identical independently checked final values.
These diagnostic clocks were collected under competing work and are not speed
evidence. All 27 WASI checks pass across three policies and nine workloads,
including 4/16/64 KiB reset/edit, fresh first-write, ordinary owned copies/edits
and the unchanged 100-character grid. Every retained snapshot is hash-checked
before/after mutation; assets and every list's expected values are checked.
Native diagnostic values/layouts agree with their three WASI counterparts;
grid semantic/palette outputs agree across all WASI policies.

After continuous work in other checkouts caused a 45-minute quiet-guard timeout,
the owner explicitly deferred timing/tests to later and requested merging this
experiment. The restarted timing runner was stopped; neither attempt produced
a comparison row. No speedup, ordinary-list overhead or preservation of the
previous grid timing gain is established for this candidate. The rotated
81-row native comparison, full native/WASI comparison and timing analysis remain
ready to run later. The existing correctness/mechanism/export verifier passes.
No full production suites, browser/GPU, other platforms, sanitizers or CI were
rerun for these standalone artifacts; production code is unchanged.

The policy keeps once-edited destinations owned even when later repeatedly
assigned assets without editing. That retains original copy/hash costs instead
of recovering naive sharing's read-only benefit; `written-then-read` is included
in the deferred comparison. Stable identities/pack lifetimes, reload, portable
reference packing, nested containers and all mutation paths remain prerequisites
for a production sharing implementation. This is a measured allocation-mechanism
fix in a prototype, with its runtime performance gate still pending. Sources,
checks, diagnostic counters, raw identities, reproduction scripts and the
checks-only verifier are in
[asset-owned-reuse](../bench/results/asset-owned-reuse/README.md).

## Asset sharing ownership gate, 2026-10-11

The owner approved considering known asset sharing with detach-on-write for
ordinary asset values as well as immutable animation data, and required proof
before adopting the broader storage change. A scratch generated-code/C-wrapper
experiment retains current pack handles until writing; no production compiler,
runtime, installed package or user game source is changed. It measures ownership
placement, not a complete stable-reference/reload implementation.

Three alternating release pairs of the unchanged 100-character game, with
one pre-tick snapshot retained, reduce the median of process-mean CPU
tick+hash+view/preparation sums from 4.139521 to 1.023344 ms (75.3% lower).
Every pair improves, with ratios 3.87x/4.06x/4.16x. Tick/hash/views are separately
1.633971/0.879640/1.625910 -> 0.010648/0.000272/1.011959 ms; retaining the
snapshot separately is 0.022408 -> 0.000112. Packed native world size is
13,117,656 -> 7,256 bytes. All game/palette digests agree across both variants
and native/WASI. Raw world hashes intentionally differ across representations.
This excludes GPU rendering, networking, restore/replay, Rooms UI and session
startup; no FPS claim is made.

The corrected ordinary-container controls include snapshotting, cleanup before
hashing, cold detachment and bulk repeated asset reset followed by editing.
Cached owned-edit total is close (0.087626 -> 0.088710 ms), with writes about
1.5% higher; cold first-write totals have mixed pairwise changes and essentially
equal medians. Bulk reset-and-edit is 1.93x slower (0.256220 -> 0.493319 ms),
with doubled heap high-water size while old blocks await the batch flush.
This fails the blanket rollout gate despite the read-only gain. Sharing known
unchanged data is supported; a complete general candidate must address or
explicitly justify that adverse workload, preserve stable identities/value
semantics through reload/snapshots, cover every mutation path and remeasure.
No general container-storage feature is landed by this experiment.

All 66 valid comparison rows, the 30 initial rows and fixture correction,
strictly compiled native/WASI checks, source/binary/library/input identities,
reproduction scripts and verifier are retained in
[asset-sharing-prototype](../bench/results/asset-sharing-prototype/README.md).
An initial C fixture wrongly read a byte asset as a float list; the native/web
digest mismatch exposed its overread. Its mutable-edit results are invalid,
explicitly excluded and retained for audit; both corrected experiments were
remeasured and the float results independently checked. Final comparison runs
each followed a complete 90-second quiet window with no detected competing
build/test/benchmark work. Other desktop activity/clocks remain uncontrolled.
Full production suites, browsers/GPU, other platforms, sanitizers and CI were
not rerun for these standalone experimental artifacts; no production code
changed.

## Owned key reductions and validated binding, 2026-10-11

The owner's unchanged 100-character `D:/Projects/Test` scenario now uses
vectorizable complete validation for owned animation keys and private node
readers after both separate-binding node tables pass. There is no retained
proof/cache, skipped content validation, ownership change or interpolation
arithmetic change. This builds on, rather than replaces, the earlier separate
export binding and owned-channel-span improvement below.

Three alternating release pairs, after a complete 90-second quiet period,
exercise idle/run/jump over 30 warmup and 300 measured manual-world frames per
process at 60 Hz. CPU view recording plus serial palette preparation falls
from a median of process means of 2.765558 to 1.495396 ms (45.9% lower, 1.85×).
Before means are [2.649967, 2.774680, 2.765558], after [1.495396, 1.478381,
1.540453]. All six processes have 100 characters, one mesh command, 5,689,760
pack bytes, 13,117,656 packed-world bytes, state mask 7, final world hash
`10175709146634639136` and accumulated palette/influence texture hash
`5069683498435541530`. No competing builds/tests/benchmarks were detected during
the measured processes; the guard reset while waiting for other work. Other
desktop activity and clocks were not controlled. Tick/hash are observed
separately, without a speedup claim. This excludes GPU rendering, networking,
rollback, Rooms UI and session startup; no FPS claim is made. The longer run
and newer pack distinguish these rows from the prior short comparison below.
Raw rows, harness, identities and reproduction are in
[owned-animation-scans](../bench/results/owned-animation-scans/README.md).

Full warning-free native debug 2,136/web debug 2,107 suites pass, including
exact pose bits and OpenGL/WebGL/WebGPU checks. Both complete runtime test
executables pass again after the final test-only tail-length addition.
The local installed runtime received only these performance hunks, retaining
its older drawing interface; a checkout-built harness using a separate library
compiled entirely from installed sources/headers confirms matching world and
palette hashes across all three states. Project sources remain untouched.
Animation assignments still create large component-owned match data, with
copying, hash, snapshot and networking costs. This improves the existing path;
state/time plus shared asset selection in the view still avoids those copies.

## Owned separate-animation grid, 2026-10-11

The owner's `D:/Projects/Test` assigns an `Animation` component field every tick and samples it in a view. A scratch 10×10 grid exposes repeated key-span validation in this owned-model path, independently of the previous default-pose binding rejection. `channel_valid` now scans the already checked spans once, retaining finite/nonnegative/strictly increasing time checks and finite checks on every value/cubic tangent before mutation. Sampling arithmetic, state bytes, hashes and draw counts are unchanged.

Three alternating release pairs, after over 90 seconds without our builds/tests, measure 10 warmup plus 20 timed frames per process. Both variants include the new binding policy; only owned-channel validation differs. Median of the three process means for CPU view recording plus serial palette preparation is 6.711750→3.463030 ms (48.4% lower); individual before means [7.085040, 6.711750, 6.219630], after [3.463030, 3.619675, 3.433880]. This short CPU-only test excludes GPU rendering, networking, rollback and window/session startup; it is not FPS or an isolated binding-cost measurement. Tick/hash timings do not establish a separate improvement. Every run has 100 entities, one mesh command, 13,117,656 packed-world bytes and final hash `13602177110211490499`. Raw rows, harness and identities are in [separate-animation-grid](../bench/results/separate-animation-grid/README.md).

The large world remains: assigning clips to component fields creates owned canonical model bytes per entity, then tick hashing/snapshots/networking process them. Keeping only animation state/time in match components and selecting the immutable asset clip in a view avoids this storage pattern; no new asset-handle language semantics or runtime storage system was introduced. Hidden manual-world native captures verify run/jump drawing, but do not measure a 100-entity session join. Full native 2,079/web 2,050 debug suites and docs build pass; the existing exact pose hash is unchanged. The owner's project sources remain untouched.

## GPU-resident clip experiment, 2026-10-11

At the owner's request to try a larger animation speedup, `bench_skinned --modes gpu` adds an opt-in visual experiment using existing shader/list/mesh APIs. Untimed setup samples the unchanged one-second, 64-bone, four-influence fixture at 60 Hz into a 245,760-byte retained palette table. The vertex shader interpolates adjacent final matrices and applies each character's independent phase, preserving source geometry, lighting, viewport and coverage. This is a visual approximation of CPU local-TRS/quaternion sampling, not a new language/engine feature or an IK implementation. Default runs keep the original four modes. `--subframe 0..1` supplies fractional-tick rendering time to any selected mode; `--verify-gpu` checks every vertex at 240 phases when GPU mode is selected, outside measured intervals. CPU gameplay/procedural Pose behavior is unchanged.

Three alternating same-binary CPU/GPU pairs on Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2, release deterministic flags, 20 warmup/100 measured frames, repeat one, use a half-tick rendering offset and all distinct character phases. At 10K, median-of-process-medians recording is 25.0526→2.36885 ms, submission 6.1343→0.0322, GPU 13.6929→7.11629 and synchronized readback total 43.5922→9.9702. Median paired recording/submission/total speedups are 10.58×/184.40×/4.37×. At 1K, recording is 2.45715→0.21935 ms (11.04× paired), submission 0.4475→0.0076 and total 4.3982→1.33705. Warmed texture uploads become zero from 4.096/40.96 MB per frame; draws including clear become two from two/eleven. Every experiment frame asserts residency and batching; all 12 rows receive 100 GPU samples. CPU/GPU overlap and readback synchronization preclude a pipelined-FPS claim. GPU intervals include uploads/batching, so they do not isolate shader arithmetic. Source triangles remain 60.48 million/frame at 10K. One-time host table/geometry preparation is about 0.6 ms, excluding renderer upload/pipeline setup.

Full warning-free debug builds and native 2,079/web 2,050 CTests pass. New OpenGL/WebGL/WebGPU checks exercise interpolation and CPU reference error bounds: maximum model-space vertex deviation 0.001320204 and unit-normal vector difference 0.00160562643 over 240 phases, identical native/web results, below the explicit 0.01 bounds. These are CPU evaluations of the approximation, not universal animation guarantees or direct GPU vertex readback. Existing exact pose/hash and GPU pixel checks pass unchanged. A visually inspected final one-character half-tick capture differs in 123/589,824 RGB pixels, two by more than two levels; maximum edge channel difference is 127. Approximate captures are not asserted bit-identical. Linux/macOS/MinGW/Android/sanitizers and CI were not run. [Evidence and reproduction](../bench/results/gpu-clip/README.md) retain raw ledger rows, source/binary identities, paired ratios/p95s, GPU monitoring, reference output, capture hashes/differences and validation. The guard waited for a complete 90-second quiet period after tests; measured runs had no detected competing work or exclusions. Multiple clips/rigs, large-motion quality, blending, procedural/IK overrides, residency and hot reload remain future work; no generic GPU animation API has been agreed.

## Large-scope rerun and optimization, 2026-10-11

All nine versus-C simulation fixtures, Sand/Gallery sessions, drawing and skinned rendering were rerun three times on the Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2 machine at `969e1406`. Baseline rows are `scope-baseline-r1/r2/r3`, plus `scope-baseline-steady-r1/r2/r3`, in the existing ledgers. Independent C digests and session world hashes agree. Both animated-scene scale and multiplayer simulation scale are priorities for this pass.

Forced-inline `tide_map_find` specializes fixed generated kinds without changing storage/hash/probe/order or snapshot bytes. Three alternating frozen-binary pairs reduce whole lookup-fixture tick time by 11–13% at 16K and 65K keys. One-thread steady 16K is 1.979→1.735 ms; 1,024 replacements is 1.799→1.593 ms. At 65K these are 9.099→8.031 and 7.149→6.330 ms. Offered twelve workers give similar results; shared map writes remain serial. Steady 16K still costs 10.25× handwritten C, so the performance bug remains. Plain optional inlining and a private generic cached reader were tried unsaved and rejected before the retained forced-inline candidate; no timing claim relies on those exploratory runs.

Explicit skinned draws now reuse the existing unique current pack proof identity to avoid repeating comparisons of unchanged model dependencies; different identities retain exact owned-byte comparisons. At 10K characters, shared-pose recording falls 4.980→3.960 ms (21.5% median paired reduction), independent-pose recording 24.683→23.471 ms (4.7%). Independent submission remains about 6.06 ms. At 1K, paired view reductions are 14.9% shared and 4.2% independent. Static/rest controls gain no meaningful whole-frame time; static recording rises by 0.00685 ms at 1K and 0.03110 ms at 10K. All draw/upload counts remain equal, every case gets all 100 GPU timer samples, and five matching before/after captures have identical PNG bytes. Native readback totals synchronize frames, CPU/GPU intervals overlap, and no pipelined-FPS or GPU-speedup claim follows.

The 10K independent case still uploads 40.96 MB of palettes and draws 60.48 million triangles per frame, with 11 calls including clear, no LOD/culling and no match simulation. It is not established as a 60 Hz game workload. Sampling, pose preparation and uploads remain performance targets; animation LOD/visibility decisions need the owner and a representative game. The simulation rerun still shows Life 7.49× C (one thread), Events 7.33× and nested buckets 6.06×. Multiple retained snapshots with restore/replay and larger-player-count session comparisons remain coverage gaps. Existing eight-player Sand/Gallery joins/replay/snapshot tests converge without resyncs; Gallery's 4.41 MB join takes up to 2.917 s catch-up with the configured latency/loss.

Full native (2,078) and web (2,048) debug suites pass warning-free, including new map collision/chunk/wrap/snapshot and skinned equal/changed-reload ownership regressions, unchanged determinism checks and OpenGL/WebGL/WebGPU pixels. Linux/macOS/MinGW/Android/sanitizers were not run; CI was not requested. [Compact evidence and reproduction](../bench/results/scope-performance/README.md) retain baseline/paired source and binary identities, raw rows, medians/p95s, GPU monitoring, capture hashes and validation commands. All measurements exclude competing build/test/benchmark processes after a 90-second quiet guard.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Building

### Benchmarks

- `bench/` holds games that stress the engine, played by bots, as targets for what a general engine has to handle: `sand` (falling sand and water that every player paints into at once) and `gallery` (every player has a big canvas of their own, and players keep joining). A voxel world, endless, with blocks placed and broken, joins them once the engine can hold one.
- Each is a program, `bench_<name>` (`--help` lists its options), on a harness of its own (`bench/bench.h`): a dedicated server and bots, each a client of its own on the loopback network, with latency and loss, in made-up time, so a run goes as fast as the machine can take it and repeats exactly. It reports each machine's update times, what they spent simulating, hashing, snapshotting and packing worlds (through the game's `tide_game`), what each machine sends, how long joins take, and the world's size.
- Measure with the `release` preset. The machines run one after another, so a run takes about as long as all of them together.
- `bench_skinned` measures rendering through generated Tide views (2026-10-10): static geometry, omitted rest pose, one animated pose shared across characters, and an independently sampled pose per character. Its reproducible embedded-buffer glTF has 3,072 vertices, 6,048 triangles, 64 skin joints (66 canonical nodes), four positive influences per vertex, and a one-second clip with 17 rotation keys per joint. `bench/skinned/generate.mjs --check` verifies the fixture. Geometry, fragment, viewport (1024 by 576) and summed nominal projected areas match across cases; every character remains visible. A preceding view samples the shared pose once and each local Character has its own drawing view. The custom host resets local state and applies entity spawns in an untimed setup frame before each case. This per-entity view boundary matters for temporary lifetime: a single view containing a large draw loop keeps its intermediate value copies until that whole view returns.
  - Defaults are 1, 10, 100 and 1,000 characters, 20 warmup frames, 100 measured frames and three repeats; CLI selectors and methodology are in `bench/README.md`. CPU generated views/recording, CPU renderer submission, and asynchronous GPU queries are reported separately, with median/p95, GPU sample count, draw calls and texture-upload bytes. Native headless runs synchronize each measured frame with framebuffer readback, timed separately and in a whole-frame total; web readback metrics are null. GPU queries exclude upfront renderer preparation/geometry uploads and readback, but include on-demand palette texture uploads inside shaded draws; they are not pure shader timings. CPU and GPU overlap, so do not add them or treat these isolated frame measurements as pipelined FPS. Unsupported GPU timers are null, never zero. Retained process/GPU memory includes caches from previous cases. `--save` appends per-case/per-repeat records with fixture hash and hardware/compiler/source identity to `bench/results/skinned.jsonl`; web prints JSONL. CTest exercises all four cases at small counts on OpenGL and software WebGL/WebGPU without timing assertions. This corrects the broader upload-exclusion description retained in the original measurement metadata; the measured boundaries and values themselves have not changed.
  - Validation: full Windows debug suites pass 2,040 native and 2,013 web tests, including the new benchmark smoke and scratch-lifetime regression. Release OpenGL also completes all four cases at 1,000 characters and captures every instance. The benchmark exposed missing compiler scratch-use tracking for owned structs; that fix and its regression are recorded in `notes/language.md`.
  - Initial native baseline at `b9faf142`, saved as `initial-skinned-native` in `bench/results/skinned.jsonl`: three repeats of 100 measured frames at each default count/mode, Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2 release OpenGL. At 1,000 characters, median-of-repeat-medians CPU view/recording costs are 0.057 ms static, 133.064 ms omitted rest, 139.063 ms shared animated pose and 151.682 ms independent poses; GPU queries report 0.500, 5.784, 5.829 and 58.685 ms. Clock observations in the last independent-pose case are all 210 MHz graphics / 405 MHz memory / P8, so GPU numbers are observed low-clock shared-desktop costs, not peak throughput. There were no concurrent task builds/tests; other user apps remained running. Metadata and the 15 clock samples are committed beside the runs.
  - At the initial baseline, shared/default poses batch into one mesh draw; independent poses use one mesh draw per character and upload 4,096,000 palette bytes/frame at 1,000 characters (shared: 4,096; rest: zero). The initial study found approximately linear CPU preparation even when GPU work batches, identifying owned-model copies and per-draw validation/hashing as investigation targets. The follow-up at `c06de5f5` removes safe generated model/Pose copies, reuses pack-load validation and canonical hashing for verified loaded models, reads only the header for owned-model rig compatibility, and reuses validated animation keys while retaining the sampler's stricter first-time check. Owned/decoded draws retain full validation/hashing and unproven samples retain full node/channel checks. Standalone `Animations` byte assets do not establish provenance, although deduplicated bytes can share a separately verified model block. Replacement, unload and overlapping geometry writes invalidate loaded provenance; no pose cache or independently posed instancing was added. Full Windows debug validation passes 2,041 native and 2,014 web tests, including unchanged pose hashes and OpenGL/WebGL/WebGPU pixels. `bench/README.md` records timing boundaries, results and reproduction commands.
  - CPU comparison, 2026-10-10: preserved `b9faf142` and tested `c06de5f5` release executables, identical host/fixture/settings, three alternating before/after pairs, all 16 count/mode cases, 20 warmup and 100 measured frames each. All 96 rows, 9,600 CPU frames and 9,600 GPU samples completed. At 1,000 characters median-of-three-round CPU view/recording times are static 0.05660→0.05655 ms, rest 132.549→3.46265 ms, shared 139.436→2.66275 ms and independent 151.600→8.96385 ms. Median within-pair speedups are 1.01×/38.30×/52.37×/16.97× respectively. Draw counts/uploads match in every paired case; the final frame-119 independent-pose captures match PNG bytes and RGB pixels exactly. The instrumented baseline attributes about 44 ms/frame to validation and 52 ms/frame to combined hashing/influence capture at 1,000 characters, arithmetic means over ten frames with timing overhead, separate from the uninstrumented comparison. The gate/process guard followed a 90-second quiet window; two earlier attempts interrupted by external builds were excluded and the original ledger restored. Other user applications remained running. Whole-process 250 ms observations mostly show P8 before and P0 after, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory, so GPU timing changes do not establish a GPU shader improvement or pipelined FPS. `bench/results/skinned-cpu/` retains paired rows, summaries, metadata, clocks, logs, baseline phase evidence, scripts and original/normalized artifact hashes; large binaries/captures remain in scratch and their identities are recorded. The main `skinned.jsonl` retains the original 48 rows plus the 96 paired rows.
  - Integration at `2dffac2d`, merging the image/audio import and format-diagnostic changes from `f23e5569`, builds all native and web debug targets and passes 113 native plus 106 web overlapping checks (219 total): importer/editor units, asset and animation diagnostics/e2e, decode/pack/provenance/borrow/default identity, and OpenGL/WebGL/WebGPU rendering including the benchmark smoke. The optimization's pre-merge full suites above are not repeated. Measurement identities remain `b9faf142`/`c06de5f5`; integration validation is separate from the performance comparison.
  - Second CPU improvement at `625596e8` (2026-10-10): frame-local exact palette memoization shares hierarchy/palette preparation across identical poses; validated sampling reads only binary-search times and selected keys, with contiguous scratch writes and rest initialization; renderer scratch integer textures reuse inactive matching GPU allocations within a frame while protecting every current draw reference. Distinct pose values retain separate captures/draws. Owned/unproven models retain checked fallback behavior, and pose/matrix arithmetic is unchanged. Full Windows debug validation passes 2,061 native and 2,032 web checks, including malformed-data/COW/cache lifecycle regressions and OpenGL/WebGL/WebGPU texture reuse/device-loss pixels. Comparison against preserved `77be2f24` uses three alternating pairs, unchanged settings and 96 rows covering 16 count/mode cases, with 9,600 CPU frames and GPU samples. At 1,000 characters median-of-round-medians CPU views are static 0.05655→0.05940 ms, rest 3.45065→0.64655 ms, shared 2.65005→0.67100 ms and independent 8.74930→4.82380 ms; median paired skinned speedups are 5.36×/3.95×/1.83×. Independent submission is 6.22325→3.26320 ms (1.91× median paired), with optimized round medians ranging 0.62095–3.27460 ms. Draw/upload counts match throughout and final frame-119 PNG/RGB captures match exactly. Instrumented diagnostics confirm one palette/hierarchy computation per rest/shared frame and zero warmed texture creates/frees plus 1,000 updates per independent frame. Those phase means include instrumentation overhead and are separate from paired medians. The gate/process guard followed a 90-second quiet window; one earlier guard-only timeout appended no rows, and no measured process was interrupted. Other desktop apps remained running. GPU clocks are mostly P0 (93/95 observations) with two initial P8 samples, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory; GPU queries include on-demand palette uploads and do not establish shader speedup or pipelined FPS. `bench/results/skinned-cpu2/` retains rows, clocks, both diagnostics, validation summaries, scripts and original/normalized hashes; the ledger preserves its previous 144 rows plus 96 new rows.
  - Integration at `495a5c33`, merging release stripping from `1886fbb9`, passes 52 bounded overlapping CTests: 20 native and 21 web debug checks, plus 4 isolated native RelWithDebInfo and 7 web Release checks. Coverage includes asset-use analysis, skinned/runtime/default/draw-borrow paths and OpenGL/WebGL/WebGPU rendering/benchmark smoke. A standalone automatically pruned game removes a leading unused texture while retaining exactly its used zero-clip SkinnedMesh and separate Animations; a finite native/WASI verifier checks the shifted pack's current model proof, separate clip binding, repeated rest/posed palette reuse and scratch reclamation across three frames. The actual native standalone also completes three hidden frames. Benchmark HOST targets preserve their deliberate automatic-pruning exemption. The isolated release builds leave both recorded executable hashes unchanged. Full pre-merge validation and measured source identities remain those above; the integration adds no new timing comparison.
  - Third CPU improvement at `a10d2f4b` (2026-10-10): draw-list-owned palette pages plus hidden per-instance matrix bases batch compatible distinct poses; deferred pure palette calculations use up to three additional pool workers for sufficient work. Exact dependencies/TRS are captured immediately, sampling remains inline, and saturated shaders/owned or unproven models keep the immediate checked fallback. Full Windows debug validation passes 2,064 native and 2,034 web CTests, with 40 focused skinned runtime tests and unchanged deterministic bits. Native actual-pool preparation observes three worker callbacks and matches serial bytes; the initial served WebGPU round observes caller-only completion, and WebGL exercises NULL-pool fallback. Comparison against preserved `874422c0` uses the same host/fixture/settings and three alternating pairs: 96 rows, 9,600 CPU frames/GPU samples, a 90-second quiet window, no competing builds/tests or exclusions. At 1,000 independent poses, median-of-round-medians view/recording is 4.92020→2.96060 ms and CPU submission 3.33085→0.46480 ms. Palette math moved into submission: the median of per-round sums of those separate medians is a derived estimate, 8.17805→3.42540 ms, with 2.21× median paired ratio; it is not a median combined sample. Submission round medians vary before [3.49680, 0.64595, 3.33085] and after [0.46480, 0.42640, 0.65005]. Rest/shared derived CPU sums remain essentially unchanged (~0.680→0.682 and 0.710→0.711 ms), and their synchronized readback-frame medians slightly increase (~0.072/0.074 ms); no wins are claimed for them or for static submission's small alternating-process variation. Independent total draws fall 1,001→2 including clear; uploads remain 4,096,000 bytes/frame. Ten-pose row padding raises uploads 40,960→49,152 bytes, with 100/1,000 unchanged. Final frame-119 PNG/RGB captures match exactly. Independent synchronized readback-frame medians fall 13.588→4.7883 ms; GPU queries 6.37491→1.07110 ms include uploads and changed batching, not isolated shader arithmetic or pipelined FPS. Whole-process clocks have 65 P0/1 initial P8 observations, 210–2,835 MHz graphics and 405–11,501 MHz memory; other desktop apps remained running. `bench/results/skinned-cpu3/` retains both snapshot identities/source hashes, paired rows, clocks, validation/smoke summaries, scripts and original/normalized artifact hashes. The ledger retains its exact 240-row prefix plus 96 new rows (336 total).
  - Integration at `7c5566bf`, merging web release compression and private-header packaging through `a14dcb08`, passes 26 bounded overlapping CTests: 6 native and 7 web debug, plus 4 isolated native RelWithDebInfo and 9 web Release. Native palette preparation again matches serial bytes with three worker callbacks; served WebGPU completes on its caller, while the independent web-thread fixture passes 240 identical ticks with two participating threads. Staging verifies all 42 engine source/header copies and resolves 94 quoted includes, including `skinned_cache.h`. A standalone pruned game retains its used zero-clip model and separate Animations; native and served WebGL/WebGPU observers verify proven model data, matching-rig clip binding, four instances in one batch with bases 0/1, 21 pixels over three frames, and scratch cleanup. The release page uses the generated minified runtime, with exact decompression parity for separately compressed WASM (546,738→223,883 bytes) and pack (1,744→517 bytes). The actual native standalone also completes three hidden frames. All frozen source and measured executable identities remain unchanged; there are no production changes or new timing comparisons during integration. The scratch report aggregate is corrected from its four successful logged stage counts (6+7+4+9).
  - 10,000-character scaling (2026-10-10): the same preserved `a10d2f4b` executable/fixture measures fresh 1,000/10,000 counts in three processes, all four modes, 20 warmup/100 measured frames. All 24 saved rows, 2,400 CPU frames and GPU samples pass work-count checks, after the named gate/90-second quiet window with no competing builds/tests or excluded attempts. At 10,000, median-of-process-medians views/submission/GPU are static 0.65490/0.02985/4.72422 ms; rest 6.79750/0.04305/8.17869; shared 6.84595/0.04430/8.19200; independent 30.49150/5.08955/13.69750. Fresh 1,000 independent values are 2.95690/0.45375/1.07827. Independent GPU medians span 13.5444–13.9264 ms, with median-of-process-p95 15.4122 ms (not pooled). The derived per-process view/submission median-sum estimate scales 3.40350→35.57640 ms; synchronized readback-frame medians 4.92160→48.28100, not pipelined FPS. Independent drawing uses ten 65,536-matrix pages, ten mesh draws plus clear and 40,960,000 palette bytes/frame; other modes retain one mesh draw plus clear. Sampling remains serial and palette preparation caps at three workers plus caller. Fixed viewport/summed nominal screen coverage makes the 100×100 characters about 4.6 pixels tall, while submitting 30.72 million source vertices and 60.48 million triangles/frame without LOD, culling or match simulation. Whole-process clocks have 134 P0/3 initial P8 observations, 210–2,835 MHz graphics and 405–11,501 MHz memory; other desktop apps remain running with no settings changes. `bench/results/skinned-10k/` retains source/executable/fixture identities, rows, ranges, clocks and reproduction scripts; a final frame-119 capture has recorded PNG/RGB hashes and stays in scratch, with no pixel equality across different grids claimed. The ledger retains its exact previous 336 rows plus 24 (360 total). No production code changes or new correctness suites are needed for this measurement.
  - Fourth CPU improvement at `711af038` (2026-10-10): current pack proof permits private channel reads and preflight of each channel's first key time; all selected channels still pass before output mutation. A four-slot per-call memo reuses binary-search results only for exact short time-array equality, preserving interpolation and both rotation normalizations. Private pose indexing uses the existing wide hash, and synchronous capture borrows contiguous scratch TRS before the retained arena copy. Owned/unproven validation, proof invalidation, palette layout and shader work remain unchanged. Full frozen validation passes 2,067 native and 2,037 web debug CTests, with 42 focused native skinned checks and the unchanged `e5d23f82e5d18f26` pose assertion on native/WASI. Native preparation again matches serial bytes with three workers; WebGL/WebGPU palette and pixel checks pass. An isolated RelWithDebInfo release smoke passes all eight 1k/10k mode cases without saving rows. Three alternating pairs against immutable `a10d2f4b` add 48 rows, 4,800 CPU frames and completed GPU queries at counts 1,000/10,000, all four modes, 20 warmup/100 measured frames. At 10k, median-of-round-medians view recording is static 0.69395→0.70900 ms, rest 6.79140→4.71755, shared 6.69115→5.18675, independent 30.10100→26.70900; independent submission 4.83890→4.90365. Its derived per-round sum-of-interval-medians estimate falls 34.93990→31.61265 ms (about 9.5% lower), with a distinct median paired ratio of 1.115×. Independent view round medians are before [29.8186, 30.1010, 30.3239], after [26.8006, 26.3477, 26.7090]; median-of-process-p95 falls 30.5573→27.1997. Rest/shared derived sums fall 6.82880→4.75485 and 6.73065→5.22530. At 1k independent, view/submission are 2.93510→2.58355 / 0.43780→0.44060 ms, and derived sums 3.36680→3.02755. GPU queries at 10k independent are 13.8803→13.3345 ms and synchronized readback-frame medians 47.3672→43.5442; unchanged shaders/workload preclude a shader-speedup claim, and no pipelined FPS is inferred. Draws/uploads remain exact, including ten mesh draws plus clear and 40,960,000 palette bytes at 10k. Both 1k/10k frame-119 PNG/RGB captures match exactly. The named gate/90-second quiet guard detects no contention or excluded measurement attempts. Whole-process clocks have 252 P0/2 initial P8 observations, 210–2,820 MHz graphics and 405–11,501 MHz memory, with other desktop apps running and no settings changes. The separate instrumented baseline attributes sampling and pose hashing costs; its nested ten-frame means include material timing overhead and are not improvement measurements. `bench/results/skinned-cpu4/` retains sources/executable/fixture identities, exact two-`#line`-path generated-source comparison, rows, clocks, validation, attribution and reproduction scripts. Its sealed text verifier and the earlier CPU3/10k verifiers pass. The ledger preserves its exact previous 360 rows plus 48 (408 total).
  - Fifth CPU improvement at `42a7ba2d` (2026-10-11): omitted-pose draws reuse one owned rest capture per current pack proof identity/frame, avoiding repeated rest construction, TRS hashing and model-table comparisons. Explicit poses retain edit-safe capture. Pack loading also records whether every channel starts at a nonnegative time; current strong proof skips selected-channel preflight, while a false flag retains selected-clip checks and unproven/invalidated models retain full checks. No general read-only-local hoisting or new `mut` semantics is added. Full frozen validation passes 2,067 native and 2,037 web debug CTests, 46 focused skinned/30 pack checks and WASI 406/406 with exact pose bits unchanged. OpenGL/WebGL/WebGPU pixels pass; native preparation matches serial bytes with three workers. Three alternating pairs against immutable CPU4 `711af038`, all four modes at 1k/10k with 20 warmup/100 measured frames, add 48 final rows and 4,800 CPU/GPU samples. At 10k, median-of-round-medians views/submission/GPU are static 0.58940/0.02710/4.42061 -> 0.60380/0.02600/4.40883 ms; omitted rest 4.82325/0.04505/7.72966 -> 0.81085/0.03545/7.76602; shared 5.30550/0.04615/7.93242 -> 5.10155/0.04155/7.46189; independent 29.22050/6.50270/13.96680 -> 24.07120/6.45840/12.97460. Default recording is 5.95x faster; independent recording falls 17.6%, improving in each pair, with before [29.2047,29.2205,29.3683] and after [24.6077,24.0712,24.0413]. Independent median-of-process-p95 is 30.3179 -> 29.1059 ms, a smaller tail improvement. Derived median-of-round view/submission median sums fall 35.86385 -> 30.52960 ms (14.9% lower; median paired ratio 1.1747x); default/shared derived sums fall 4.86260 -> 0.84805 and 5.35595 -> 5.14345. At 1k, default views are 0.46535 -> 0.07720, independent views/submission 2.89725/0.49535 -> 2.34155/0.45605 and derived sums 3.39260 -> 2.80595; shared views are 0.52150 -> 0.53410. Independent synchronized readback-frame medians fall 48.11180 -> 42.11020 ms, default 13.02110 -> 9.02925; no pipelined FPS is inferred and CPU/GPU intervals are not added. Shaders and work are unchanged, so GPU observations do not establish a shader speedup. All four default/independent PNG/RGB pairs at 1k/10k match exactly. The 10k workload retains ten mesh draws plus clear, 40,960,000 palette bytes and 60.48M triangles/frame, tiny projected characters and no LOD/culling/match simulation; sampling stays serial with at most three palette workers plus caller. The final gate/90-second quiet guard has no detected contention, reset or excluded attempt; other desktop apps run with no settings changes. Whole-process clocks have 245 observations (243 P0/1 P5/1 initial P8), graphics 390–2,820 MHz and memory 405–11,501 MHz; these are not per-case clock-normalized results. The initial `000a5986` comparison is retained as an adverse result (10k independent views 27.08380 -> 30.48430), including all 48 rows and lightweight snapshot/validation identities. Its preflight quiet period reset once for unrelated Last Light tests before the full quiet window passed; measured runs did not overlap detected work. Private timer/layout diagnostics show placement sensitivity but do not establish a sole cause or contribute to final timings; the final measurement covers combined code and layout. `bench/results/skinned-cpu5/` seals final and superseded evidence, identities, ranges, clocks, validation and reproduction scripts with raw and normalized text hashes; BOM-marked PowerShell UTF-16 logs are decoded losslessly. The original 408-row history plus 48 adverse rows stays exact as the 456-row input; 48 distinct final-label rows bring the ledger to 504.
  - `.gitattributes` keeps the generated fixture and saved skinned measurement text in LF form across checkouts, preserving fixture reproduction and artifact hashes. The improvement export records original measured bytes separately from normalized committed text, including the expanded JSONL ledger.
- Runs are kept: measuring a change saves its runs (`--save <label>`, named for the change, with `-1024` for sand at that size) into `bench/results/`, a few of each so the noise shows, and commits them, so the files are a history of improvements, setbacks and noise. A comparison with an older build saves that build's runs too, under its own label. `node bench/compare.mjs [benchmark...] [--last <n>]` puts runs side by side.
- Tests run each one `--quick`, which fails when a client's world went wrong, a bot couldn't join, or the server's world ends with another hash than the one in `bench/CMakeLists.txt`, which every platform and thread count has to agree on. Timings aren't checked: machines differ too much.
- Sand is a game people play too: `sand` (`sand.html` on the web) is `sand/sand.tide` with `sand/play.tide`, its views, GUI and rooms, and the mouse through the input's `Sample`, which the benchmark's bots don't call. The docs site shows it (see Docs site).
- A benchmark holds its game as the engine can today, and says where that's not how it should be. It changes as the engine does (sand's cells were a list, then a grid that a chunk system stepped on threads, then 2 by 2 blocks that a parallel loop steps), and its expected hash with it.
- `bench/versus/` measures Tide against C: a concept written both ways, each as fast as its language allows, on the same work (`bench/versus/versus.h`). Each is a program, `versus_<concept>`: particles (an ECS system streaming through a component), wide (the same through a component of a dozen floats, half of which a tick changes, and with `--tagged 1`, by a system that leaves out the entities with a sparse tag, `--frozen` in a hundred of them), sand (`sand/sand.tide` itself), life (a grid stencil), churn (spawning and destroying), events, nbody (every pair, through a list), rollback (a predicting client's ring of kept worlds, hashes and corrections, by part) and systems (64 systems' schedule, from no entities up). Each takes `--kept 1`, which keeps the world before every tick, as a client keeps every tick it predicts: the snapshot shares the world's pages, so the tick writes none of them in place. `cmake --build --preset release --target versus` runs them all.
  - Every way starts from nothing and runs the same ticks, the first of which sets it up, and has to end with the same result, bit for bit: the same work, so the times compare. A digest adds up a hash of each thing a way holds (`versus_item`), so ways keep things in any order. Tests run each one `--quick` and fail when the ways disagree.
  - `versus_lookup` compares map/set hits, misses, score updates and ordered key replacement. Its Tide host reads entries directly from the world heap for the semantic digest: a signed 64-bit insertion-order header, then the primitive key and value, both map and set entries 16 bytes with the key at 8. It uses `tide_map_live` so negative removed-entry orders are skipped; storage IDs and padding are never included in the digest. This mirror must follow tidec's entry layout. Layout assertions pin its offsets and strides, and `versus_lookup`, `_empty`, `_single` and `_steady` compare live keys, values and insertion order against the independent C tables on one and several threads.
  - The C is what someone who knows the machine writes: arrays rather than structs where that's faster, branches turned into selects for SIMD, arrays staggered so they don't share their addresses' low bits (4K aliasing), and threads on the same pool Tide ticks on (`versus_for`), only once there's enough work for them to pay (`versus_pieces`; `--split` and `--quick` always split). A way with a trick Tide can't do yet is a way of its own (`C, lookup`, `C, SIMD`). The C builds with the flags generated code does, so the results stay bit-identical.
  - It times each tick after the warm-up ones, on one thread and then on every core (or each count `--threads-list` gives), and reports medians, and how many times as long Tide takes, with the parts a concept times its ticks in (rollback's) beside them. `--save <label>` adds a run to `bench/results/versus.jsonl`.

## Shared component syntax migration (2026-10-12)

The skinned benchmark's Character is now a shared component definition placed
with local Spawn and accessed with local bindings, including its shader binding.
Workload, fixture, draw behavior and saved measurements are unchanged. This
syntax migration makes no new timing claim; full native/web validation is
recorded with the compiler's Transform/placement work in notes/language.md.
