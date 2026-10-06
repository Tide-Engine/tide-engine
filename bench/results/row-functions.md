# Row functions and lanes

Generated 06/10/2026, 19:37 (Europe/Paris).

Fresh measurements compare tested source `6e4b60e` with `e1bb4be` on AMD Ryzen 9 7900X 12-Core Processor. `7a7ba9e` is the compiler/engine full-suite validation point; the measured implementation includes subsequent benchmark corrections. Clang 23.1.2 preserves deterministic floating-point flags `-fno-fast-math -ffp-contract=off`. Five rounds run before and after in turn. Tick and end-to-end build tables use the minimum saved per-round median; session tables use minimum recorded per-round ms/s costs, and direct diagnostics use minimum wall times. Each table value is independently selected. These timings describe the hashed benchmark implementations used for this run, before later dev integration. [Binary/source identities, settings and ledger references](row-functions-measurement-metadata.json) identify both builds; [raw versus records](versus.jsonl) retain every round.

The Windows benchmark mutex serializes cooperating sibling benchmark batches; other builds and processes can still contend. Runtime batches: One complete 13-case round, each case before then after, per acquisition; five fresh rounds; idle check once per complete round. End-to-end compiler batches: One archetype fixture before/after pair per acquisition. Direct compiler batches: Original completed pairs retained; remaining paired rounds use one finishing acquisition, with CPU idle checks before each pair. Pre-run CPU checks span 0–59%; 1 final check stayed above 15% after the bounded wait, and 0 final checks were unavailable. Check counts do not count mutex acquisitions: a recorded acquisition can include several checks. These are pre-run samples, not continuous isolation. Small differences and worker timings should be read with that uncertainty. The metadata records earlier partial runtime diagnostics; final tables select only its fresh date/options identities. Earlier partial pairs remain in the raw ledgers.

## Fresh streaming and sparse-filter cases

One million entities, milliseconds per tick. Kept 1 retains the prior state before each tick in both Tide and handwritten C, and checks that retained state remains unchanged. Best C is the lowest median among the handwritten C ways in the after rounds. The wide component has twelve float lanes, six changed by the body. The same corrected benchmark implementations are copied into the before and after builds.

| Case | Threads | Tide before | Tide after | Best C after |
| --- | ---: | ---: | ---: | ---: |
| Particles, kept 0 | 1 | 0.447 | 0.451 | 0.409 |
| Particles, kept 0 | 12 | 0.297 | 0.286 | 0.295 |
| Wide, unfiltered, kept 0 | 1 | 1.419 | 1.204 | 0.942 |
| Wide, unfiltered, kept 0 | 12 | 0.615 | 0.671 | 0.583 |
| Wide, 100% pass, kept 0 | 1 | 3.768 | 1.318 | 0.902 |
| Wide, 100% pass, kept 0 | 12 | 1.055 | 0.640 | 0.526 |
| Wide, 50% pass, kept 0 | 1 | 4.473 | 3.605 | 1.036 |
| Wide, 50% pass, kept 0 | 12 | 0.906 | 0.639 | 0.625 |
| Wide, 0% pass, kept 0 | 1 | 0.259 | 0.067 | 0.000 |
| Wide, 0% pass, kept 0 | 12 | 0.093 | 0.083 | 0.000 |
| Particles, kept 1 | 1 | 0.882 | 0.852 | 0.913 |
| Particles, kept 1 | 12 | 0.591 | 0.590 | 0.683 |
| Wide, unfiltered, kept 1 | 1 | 6.223 | 3.620 | 1.653 |
| Wide, unfiltered, kept 1 | 12 | 1.459 | 1.396 | 1.424 |
| Wide, 100% pass, kept 1 | 1 | 7.343 | 3.713 | 1.699 |
| Wide, 100% pass, kept 1 | 12 | 1.415 | 1.416 | 1.355 |
| Wide, 50% pass, kept 1 | 1 | 8.082 | 6.875 | 1.813 |
| Wide, 50% pass, kept 1 | 12 | 1.455 | 1.413 | 1.417 |
| Wide, 0% pass, kept 1 | 1 | 2.987 | 0.105 | 0.000 |
| Wide, 0% pass, kept 1 | 12 | 0.548 | 0.124 | 0.000 |

Private, unfiltered wide with twelve offered threads has a higher Tide minimum: 0.61475 → 0.67140 ms (+9.22%). Its five saved medians span 0.61475–0.66700 ms before and 0.67140–0.77605 ms after. At one thread Tide instead improves from 1.41885 → 1.20445 ms (-15.11%). The same handwritten C code at twelve threads also shifts from 0.53360 → 0.58340 ms (+9.33%); its ranges are 0.53360–0.66240 and 0.58340–0.67665 ms, which overlap. The Tide slowdown is visible across its saved ranges, but the comparator shift prevents attributing it solely to the generated kernel.

Assembly inspection finds four-row SIMD in both versions, with six stores in the new body versus twelve before. The new call for each of 489 chunks passes twelve lane pointers and saves registers, so call/setup overhead is a possible contribution, not an isolated measured cause. Pieces callbacks do not execute in this case: 489 chunks exceed the twelve offered threads, so each chunk uses one piece.

Larger reductions appear in the wide cases: at one thread, kept/unfiltered is 6.22350 → 3.62035 ms (-41.83%), all-pass tagged/kept 0 is 3.76810 → 1.31820 ms (-65.02%), and no-pass tagged/kept 1 is 2.98680 → 0.10465 ms (-96.50%). At twelve threads the latter two are 1.05490 → 0.64000 ms (-39.33%) and 0.54820 → 0.12450 ms (-77.29%). Every fresh particle before/after range overlaps, so these records show no clear gain or regression there. Ranges here describe the five saved round medians, not individual-tick percentiles.

## Original comparison workloads

The original five-round `row-functions-before` / `row-functions` sittings cover Life, Events, N-body, Sand and Churn with kept 0/1 and 1/12 offered threads. Churn was repeated; this table uses its later five-round sitting, while both remain in the ledger. These historical records come from the original continuation and are distinct from the fresh binary identities above. Best C includes explicit SIMD N-body and the Sand lookup alternative where faster. The historical kept C ways did not retain prior state: their values marked † are incomparable to the fair fresh snapshot values. These records provide context, not regression evidence for the final snapshot implementations; small changes are also affected by contention.

| Case | Threads | Tide before | Tide after | Best C after |
| --- | ---: | ---: | ---: | ---: |
| life, kept 0 | 1 | 0.605 | 0.603 | 0.081 |
| life, kept 0 | 12 | 0.219 | 0.217 | 0.080 |
| life, kept 1 | 1 | 0.619 | 0.620 | 0.080 † |
| life, kept 1 | 12 | 0.259 | 0.252 | 0.081 † |
| events, kept 0 | 1 | 0.930 | 0.959 | 0.125 |
| events, kept 0 | 12 | 0.928 | 0.920 | 0.123 |
| events, kept 1 | 1 | 0.921 | 0.908 | 0.122 † |
| events, kept 1 | 12 | 0.907 | 0.915 | 0.123 † |
| nbody, kept 0 | 1 | 3.522 | 3.500 | 2.037 |
| nbody, kept 0 | 12 | 0.447 | 0.443 | 0.249 |
| nbody, kept 1 | 1 | 3.520 | 3.503 | 2.046 † |
| nbody, kept 1 | 12 | 0.444 | 0.482 | 0.256 † |
| sand, kept 0 | 1 | 1.056 | 1.065 | 0.438 |
| sand, kept 0 | 12 | 0.230 | 0.240 | 0.068 |
| sand, kept 1 | 1 | 1.061 | 1.080 | 0.438 † |
| sand, kept 1 | 12 | 0.252 | 0.264 | 0.068 † |
| churn, kept 0 | 1 | 0.553 | 0.534 | 0.203 |
| churn, kept 0 | 12 | 0.544 | 0.534 | 0.204 |
| churn, kept 1 | 1 | 1.072 | 0.902 | 0.207 † |
| churn, kept 1 | 12 | 1.298 | 1.187 | 0.205 † |

## Matches that snapshot every tick

Twelve offered threads, eight players, 50 ms latency, 10 ms jitter and 1% loss. Sand joins a player every 0.25 s and plays 10 s; Gallery joins every 3 s and plays 5 s. Each value is the minimum recorded per-round cost in milliseconds per second of play; client costs are averages per client. [Sand rounds](sand.jsonl) and [Gallery rounds](gallery.jsonl) include update percentiles, replay counts and transfer sizes.

| Match | Server simulation before → after | Client simulation | Client snapshots | Client hashes |
| --- | ---: | ---: | ---: | ---: |
| Sand 256 | 2.911 → 2.799 | 29.549 → 28.970 | 0.281 → 0.291 | 0.537 → 0.539 |
| Sand 1024 | 15.755 → 15.374 | 187.190 → 184.205 | 2.560 → 2.556 | 6.547 → 6.599 |
| Gallery 512, archived 4 | 0.294 → 0.298 | 1.798 → 1.801 | 0.658 → 0.658 | 0.759 → 0.760 |

All twelve before/after ranges for the displayed networking costs overlap across the five saved rounds. The small minimum differences show no clear gain or regression in these match workloads; pre-batch checks do not remove possible contention during play.

## End-to-end build completion

The fixture uses 100 helper functions, 100 entity systems and one or four body archetypes. `bench/compile.mjs` measures process startup, frontend work, C compilation, linking and web page creation. Cold clears the game cache/output while installation and filesystem caches stay warm. Unchanged rebuilds and edits follow in order. Values are minimum saved medians in milliseconds. [Compile records](compile.jsonl) include every sample, compiler identity and changed object list.

| Archetypes | Target/config | Cold before → after | Unchanged | Tide body edit |
| ---: | --- | ---: | ---: | ---: |
| 1 | native debug | 2493.3 → 2586.9 | 155.3 → 163.4 | 365.9 → 475.7 |
| 1 | native release | 12974.2 → 6422.3 | 151.3 → 148.2 | 8417.8 → 2018.9 |
| 1 | web debug | 2483.2 → 2627.2 | 211.0 → 200.7 | 493.2 → 611.4 |
| 1 | web release | 8283.8 → 5659.7 | 191.7 → 202.2 | 4285.3 → 1748.4 |
| 4 | native debug | 2745.5 → 2835.6 | 170.0 → 176.9 | 624.8 → 727.8 |
| 4 | native release | 37488.4 → 7588.1 | 158.1 → 164.9 | 33066.6 → 3158.1 |
| 4 | web debug | 2913.7 → 2998.7 | 211.4 → 216.3 | 895.5 → 954.0 |
| 4 | web release | 20279.2 → 6630.8 | 208.2 → 207.1 | 16223.3 → 2690.8 |

With four archetypes, the minimum Tide-body edit cost falls by 90.45% in native release and 83.41% in web release, while debug increases by 16.48% and 6.52%, respectively. All four before/after ranges are disjoint, as shown below. All unchanged/C/header edit ranges overlap; their small minimum differences do not establish a clear gain or regression.

Four-archetype Tide-body edits, ranges of the five saved round medians in milliseconds:

| Target/config | Before range | After range |
| --- | ---: | ---: |
| native debug | 624.8–711.0 | 727.8–759.9 |
| native release | 33066.6–33699.0 | 3158.1–3313.2 |
| web debug | 895.5–932.5 | 954.0–1007.2 |
| web release | 16223.3–16574.2 | 2690.8–2775.5 |

C/header edits retain the same fixture and order:

| Archetypes | Target/config | C body edit before → after | Header edit |
| ---: | --- | ---: | ---: |
| 1 | native debug | 184.3 → 200.5 | 216.2 → 224.2 |
| 1 | native release | 171.4 → 177.2 | 205.8 → 203.3 |
| 1 | web debug | 221.9 → 236.9 | 267.5 → 252.7 |
| 1 | web release | 207.4 → 213.5 | 239.4 → 243.4 |
| 4 | native debug | 199.2 → 201.9 | 225.1 → 226.1 |
| 4 | native release | 180.4 → 185.5 | 214.2 → 220.2 |
| 4 | web debug | 229.2 → 239.5 | 260.9 → 265.2 |
| 4 | web release | 221.3 → 226.2 | 248.9 → 266.0 |

## Direct compiler diagnostics

One generated C file, 100 systems and four body archetypes; five alternating rounds each at `-O0` and `-O2`, with deterministic float flags and `-ftime-report`. The table selects minimum whole-command wall times and available phase wall times. These omit Tide startup/linking and are separate from end-to-end build times. [Direct compiler records and generated-source hashes](row-functions-compile-phases.json) identify the retained original rows fixture; the later parameter-cap fix does not change this fixture. Phase minima are independent and must not be added to wall minima.

| Optimization | Wall ms before → after | LLVM IR ms | Code generation ms | Object KiB |
| --- | ---: | ---: | ---: | ---: |
| -O0 | 428.6 → 515.0 | 61.5 → 67.5 | 176.0 → 248.4 | 1137.2 → 1370.6 |
| -O2 | 33462.6 → 3444.1 | 73.4 → 79.2 | 5517.5 → 1729.2 | 2594.9 → 969.8 |

The exact 100-system/four-archetype C inputs contain 0 → 250 row functions and 0 → 100 completion callbacks. Generated source grows from 1,997,218 to 2,106,690 bytes (+5.48%), while system-body call sites fall from 700 to 250. Extra separately emitted row functions/callbacks, argument setup and lane/pieces/filter bookkeeping give the unoptimized backend more code to emit; shared bodies across archetypes reduce duplicated body optimization in optimized builds. This is an explanation from the generated structure, not an isolated measurement of each cause. In the fresh direct records, minimum -O0 object size is 1,164,463 → 1,403,509 bytes (+20.53%); -O2 object size changes by -62.63%. Whole-command wall times are 428.6 → 515.0 ms at -O0 and 33462.6 → 3444.1 ms at -O2; -O0 backend phase wall times are 176.0 → 248.4 ms. These measured costs qualify the structural explanation and do not isolate frontend, scheduling or contention effects.

Every matched runtime configuration retained its exact digest/hash across before/after, and all selected records passed. Two measured implementation gaps remain: mixed passing/skipped rows still run their filtered loop instead of runs through the filter-free function, and unchanged lanes of a `mut` component are still written apart and compared. Neither optimization is implemented by this change.
