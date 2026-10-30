# Aggressive component-layout experiments

These prototypes close the measured sparse-update gap, but do not implement a
production Tide ECS storage change. Both the prototype and the original generated
Tide world run the same seeded arithmetic and match the independent C digest.
The prototype uses Tide's task scheduler and preserves values by stable logical
entity ID. It replaces generated tables with a small workload-specific store.
No compiler/runtime/game sources or expected hash pins change.

## Sparse update results

One million entities, one thread, 200 warmup and 400 measured ticks; median of
three process tick medians. Each prototype has its own adjacent Bevy comparisons.
The baseline Tide and prototype execute in the same process, reversing their
order by round; the independent C reference runs third. The Tide/prototype process
and Bevy process also reverse order by round. Ranges and every observation are in
each batch's summary-layout.json and raw/. Absolute values vary between batches.

| Prototype | Frozen | Current Tide ms | Prototype ms | Bevy SparseSet ms |
| --- | ---: | ---: | ---: | ---: |
| Contiguous records (AoS) | 95% | 0.87830 | 0.22135 | 0.26025 |
| Contiguous records (AoS) | 99% | 0.09575 | 0.04380 | 0.04840 |
| Four-row field blocks (AoSoA4) | 95% | 0.87290 | 1.09380 | 0.30980 |
| Four-row field blocks (AoSoA4) | 99% | 0.09610 | 0.11920 | 0.04850 |
| Packed active field arrays | 95% | 0.86350 | 0.02870 | 0.24170 |
| Packed active field arrays | 99% | 0.09610 | 0.00570 | 0.04870 |
| Packed + six-field write-back | 95% | 0.87620 | 0.44450 | 0.22605 |
| Packed + six-field write-back | 99% | 0.09630 | 0.04830 | 0.04830 |
| Repack + update + write-back | 95% | 0.91045 | 1.88855 | 0.29545 |
| Repack + update + write-back | 99% | 0.09495 | 0.22235 | 0.04810 |

AoS approximately reaches the ordinary Bevy comparator; packing active entities
together goes substantially faster. AoSoA4 is adverse. Copying changed fields
back into the original order each tick consumes much of the packed gain;
repacking from that mirror every tick is worse still. These are separate storage
designs, not progressive optimizations to the production engine.

## Stronger Bevy comparison: partition both sides

A separate Bevy build changes only Frozen from SparseSet to default Table storage.
Its active and frozen components can occupy different tables. Normal mutable-query
change detection, scheduler, Rust flags, arithmetic, count and tick scope remain.
This removes the unfair implication that Bevy could not also exploit partitioning.

| Workload | Current Tide ms | Packed prototype ms | Bevy Table ms | Paired Bevy/prototype |
| --- | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1.19710 | 0.93140 | 2.31220 | 2.50x |
| filtered-1m-1 | 2.02770 | 0.32310 | 1.03535 | 3.26x |
| filtered-frozen95-1m-1 | 0.86590 | 0.02870 | 0.10090 | 3.52x |
| filtered-frozen99-1m-1 | 0.09585 | 0.00540 | 0.01990 | 3.67x |
| filtered-frozen99-1m-4 | 0.03990 | 0.00550 | 0.02170 | 4.02x |

A ratio above one favors the prototype. This still compares an experimental
numeric storage path with a full Bevy ECS query; it is not a shipping Tide win
or a maximum-performance claim about Bevy.

## Density and offered-thread controls

| Prototype | Workload | Current Tide ms | Prototype ms | Bevy SparseSet ms |
| --- | --- | ---: | ---: | ---: |
| Contiguous records (AoS) | wide-1m-1 | 1.17410 | 2.77585 | 2.34335 |
| Contiguous records (AoS) | filtered-1m-1 | 2.03205 | 1.73455 | 2.21855 |
| Contiguous records (AoS) | filtered-frozen99-1m-4 | 0.04090 | 0.04410 | 0.03030 |
| Packed active field arrays | wide-1m-1 | 1.17400 | 0.93665 | 2.33325 |
| Packed active field arrays | filtered-1m-1 | 2.02870 | 0.41170 | 2.18205 |
| Packed active field arrays | filtered-frozen99-1m-4 | 0.04080 | 0.00550 | 0.03050 |

AoS loses dense throughput and does not beat Bevy in the four-thread rare case.
The packed layout streams densely even when few entities are active. Its scheduler
groups 2,048 selected rows per task (25 tasks at 95% frozen and five at 99%),
where current Tide dispatches approximately 489 physical chunks. Both locality
and task count change; this experiment does not isolate their contributions.

## Maintenance and snapshots

The packed canonical store contains all million entities, including unchanged
frozen entities. It uses 48,009,216 data bytes plus 12,000,000 bytes for three
index arrays in this prototype. Write-back variants additionally keep a 48 MB
mirror. None discards frozen state. Data-dependent setup/seeding/allocation is
outside steady-state ticks, as in the original comparator.

Three separate gated diagnostic processes measure full setup and 100 intervals
per membership-change rate. Results below are medians of process means, not tick
medians and not paired Bevy comparisons. Each interval toggles the requested
logical entity IDs and updates the resulting active set; restoring membership
happens outside the timer. More toggles can mean more active entities, so this
is update-plus-maintenance cost, not isolated structural-change latency.

Full prototype setup: 31.970 ms (includes seeding and conversion).

| Toggles per interval | Update + changes, ms |
| ---: | ---: |
| 0 | 0.028748 |
| 1 | 0.028670 |
| 100 | 0.030349 |
| 1000 | 0.043892 |
| 10000 | 0.196777 |

A membership toggle swaps twelve fields at the active/frozen boundary and updates
both ID maps. This does not implement entity spawn/destruction, generations,
multiple filters, component add/remove callbacks or command/event ordering.

A separate comparison keeps the previous world before every tick. Current Tide
uses its real COW snapshots; the prototype deep-copies its entire data allocation.
Membership is static here, so no ID-map snapshot is needed for this workload.
This is not a complete snapshot/restore or rollback implementation. Bevy is not
measured in this snapshot comparison.

| Frozen | Current Tide tick + snapshot ms | Prototype tick + snapshot ms | C ms |
| ---: | ---: | ---: | ---: |
| 95% | 4.12580 | 1.65640 | 1.82330 |
| 99% | 4.01280 | 1.62650 | 1.81815 |

The tiny steady-state tick cost is therefore not a whole-networked-engine cost.
A general compact-page implementation would still need correct COW ownership,
snapshots of membership/mappings, restoration, canonical hashing/packing and
reload, while preserving all observable ordering.

## Correctness, limits and reproducibility

- Native and ordinary WASM SIMD pass 240 collected comparisons: five layouts,
  0/17/4,099 entities, 0/95/99/100% frozen, with/without a retained previous state.
  The generated Tide, prototype and independent C digests agree, and native
  and WASI match. These clocks are not used as performance evidence.
- The membership test changes 13 IDs per tick for 30 ticks over ten sizes from
  zero to 4,099, comparing every field by original entity ID and checking both
  maps. It passes 3,742,200 exact field comparisons on each of native and WASI.
- All valid timed cases match Tide/C/Bevy logical digests. The Table variant
  retains the same digest as SparseSet at each workload and thread budget.
- No entity-generational storage, arbitrary query iteration order, multiple
  overlapping filters, random/events, dynamic-snapshot restoration, hot reload
  or network integration is implemented by the prototype. Arithmetic is pure
  and row-independent; arbitrary systems cannot be reordered this way blindly.
- No new runtime API, canonical production format, language behavior or hash
  expectation is introduced. Existing particles and other games are untouched.

Windows 11 / Ryzen 9 7900X, Clang 23.1.2 O2 and Rust 1.95.0 / Bevy 0.20.0 O2.
Native uses baseline x86-64 with no fast math or FMA; WASI validation uses O2
and ordinary -msimd128. Prototype and original generated Tide use the same
runtime libraries and scheduler. Current Tide is the frozen d65b66ed4 generated
code; the enclosing checkout at measurement is 8cebf05bf (later notes/CLI changes).
No GPU, web timing, hardware counters, other CPU/OS or full production suite
is claimed. World hashing, final digests, rendering and networking are excluded;
only the explicitly named snapshot batch includes retained-state work.

The named mutex, 15-second continuous quiet guard and 250 ms process inventory
poll apply. Affinity, clocks, temperatures and ordinary desktop activity remain
uncontrolled. Completed evidence has 135 valid processes: 126 standard paired
processes, six snapshot processes and three maintenance diagnostics.
The first six-process snapshot attempt (snapshots2/) is invalid and retained:
PowerShell unwrapped a one-element engine array into a string, so indexing
launched Bevy as engine T instead of the layout executable. Its snapshot metadata
is wrong; its records identify the actual program and arguments. It contributes
no snapshot numbers. The runner now uses an explicit string[] and the corrected
batch verifies the actual executable, engine, --kept 1 and internal way order.

diagnostics/ contains sources, exact build commands/input/binary identities,
runners, native/WASI validation, and both original and corrected snapshot runners.
pairs.py is a frozen copy of the existing Bevy audit with the sole implementation
allow-list change permitting Layout alongside Tide/C. It verifies Bevy samples
and statistics, raw output hashes, options, binary identity and complete pairs.
analyze.py adds prototype statistics. The CMake database recorded by the timing
runner is context only; build.json records the actual manual compile/link commands.
The Bevy Table build has a separate source manifest and executable identity.

Restore diagnostics under build/bevy-layout-20261029 with the recorded common/
sources and baseline libraries to rebuild; build.py documents native/WASI links.
The original old/common dependency in build.py is the preserved baseline from
the previous locality experiment. Use fresh output directories. For ordinary
comparisons invoke run.ps1 -Layout 0..4 -Rounds 3 -Warmup 200 -Ticks 400
-QuietSeconds 15 -FilterSweep -OnlyCases <the case IDs in metadata.json>.
The alternative Bevy executable is selected with -BevyExe. Snapshot and
maintenance runners are separate, as their scopes differ. No extra CI dispatch
or production rollout accompanies these experimental records.
