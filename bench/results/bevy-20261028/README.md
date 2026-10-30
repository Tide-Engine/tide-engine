# Tide / Bevy ECS, 2026-10-28

Windows 11 build 26200, Ryzen 9 7900X (12 cores / 24 logical processors),
Tide tree `367ae4da2d31913176ae1e2624e1d78a1204f03c` plus the saved benchmark
harness; Clang 23.1.2, Rust 1.95.0 / LLVM 22.1.2, Bevy ECS 0.20.0.
Both builds use O2 and baseline x86-64 without fast math or fused arithmetic.
Source/generated-C/configuration/executable SHA-256s are in metadata.json.

Tide is faster for these dense component updates. Bevy is faster for serial
sparse filtering and churn. This is an ordinary scheduled ECS comparison with
matching logical work; it establishes no whole-engine or rendering/networking
winner. [Method, exact APIs, limitations and reproduction](../../bevy/README.md).

Milliseconds per tick, median of five process medians:

| Workload | Tide, 1 thread | Bevy, 1 thread | Tide, 4-thread budget | Bevy, 4-thread budget |
| --- | ---: | ---: | ---: | ---: |
| 1M particles | 0.47430 | 1.16390 | 0.12935 | 0.43320 |
| 1M twelve-float components | 1.35745 | 2.48685 | 0.63360 | 1.20025 |
| 1M components, half frozen | 4.00665 | 2.46325 | 1.08240 | 1.26125 |
| 2,000 bullet spawns/tick | 0.46690 | 0.35955 | 0.45600 | 0.39410 |

Median paired Bevy/Tide ratios are respectively 2.448/3.349 for particles,
1.872/1.894 for wide, 0.619/1.169 for filtered, and 0.751/0.798 for churn
(one/four-thread budgets). Ratios above one favor Tide. They are medians of
paired ratios and can differ from ratios of the displayed medians.
10k/100k particles take Tide 0.00420/0.04240 ms and Bevy 0.01165/0.11430 ms.
The filtered case contains exactly 499,674 frozen entities; churn ends with
198,814 live bullets. Churn's timing includes spawning, destruction and deferred
command application; entity identities/order are not compared.

[summary.md](summary.md) retains all cases, process-median min/max and the
handwritten C reference. [summary.json](summary.json) also retains every process
median and paired ratio. There is no pooled-tick percentile or FPS estimate.
For example, four-thread churn process medians span 0.44305-0.58705 ms in Tide
and 0.34425-0.41335 ms in Bevy; the pool budget helps neither engine consistently
in this small structural workload. The serial sparse-filter result identifies
a useful Tide performance target; no causal profile or optimization was made.

Five alternating process rounds cover 100 processes and 40,000 measured ECS
ticks, plus 20,000 warmup ticks. Each Tide process additionally times its C
reference for 400 ticks. Every Tide/C digest agrees, every Bevy/Tide pair agrees,
and each case's digest remains identical across thread budgets and rounds.
The timed batch ran 20:38:23-20:40:19 UTC, including its initial 15-second quiet
guard, under the shared benchmark mutex. No quiet reset, competing process,
failed process or excluded timing attempt occurred. Polling every 250 ms can
miss short activity and does not control ordinary desktop load, CPU placement,
clocks or temperature. Nothing establishes hardware-independent factors.

Validation: all five scoped native and all five WASI optimized CTests pass;
[validation.json](validation.json) records 66 additional Bevy comparisons against
native Tide/C and WASI Tide/C over early/steady ticks, small/empty counts and
0/50/100% filtering. The analyzer verifies raw stdout hashes, successful exits,
complete alternating pairs, options, thread budgets and invariant digests.
Six synthetic analyzer checks cover valid/missing/contended/corrupt/reordered
evidence. No engine/compiler/game semantics were changed. Bevy web, browser/GPU,
other operating systems, mobile, sanitizers and whole-engine performance were
not measured. No extra CI run was dispatched.

Recheck the committed evidence without compiling or running either engine:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/bevy/analyze.py bench/results/bevy-20261028
```

Raw outputs are preserved byte-for-byte under raw/. Empty .stderr files mean
the process emitted no stderr. records.jsonl contains exact launch arguments,
times, result JSON and stdout SHA-256; metadata.json pins the measured input
identities. Executables remain in the ignored build directories, not this export.
The Rust source received formatting only after its successful build; no semantic
source edit followed that build. Recorded source byte hashes describe this
Windows checkout and can differ after line-ending conversion.
