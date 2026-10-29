# Packed storage integration proposal

Status: runtime boundary approved by the owner on 2026-10-29. An isolated
runtime integration probe is implemented and validated; automatic compiler
selection and general production integration are not implemented.

The probe and measured acceptance problems are recorded in
`bench/results/packed-integration-20261029/README.md`. It keeps real Tide
table/COW ownership, logical entity locations, hashes, raw saves and deltas.
Native/WASI validation agrees, including growth, removals and table moves;
a separate native four-thread check agrees with serial execution. It is not
safe to enable indiscriminately: unchanged generated order-sensitive loops
still use physical columns, and smaller/mixed-density workloads regress.
The remaining items below stay required before production activation.

The layout experiment in `bench/results/bevy-layout-20261029/README.md`
establishes a promising pure numeric update path. It does not establish that
general Tide storage can use its physical iteration order. In particular,
`tide_location.row`, generated query traversal, table removal, raw packing,
page hashes and migration currently refer directly to the same row numbering.
Changing entity locations after partitioning alone would change what subsequent
order-sensitive code observes.

## Recommended boundary

Keep logical rows as the existing deterministic traversal and removal order.
Allow physical rows to partition data into matching and nonmatching regions.
Store both permutations in world-owned COW pages, with identity mapping as the
zero-cost default. A physical rearrangement never changes entity generation,
logical row identity, component presence, deferred order or logical values.

Approved runtime entry points, exercised in the isolated probe:

```c
uint32_t tide_table_physical_row(const tide_table *table, uint32_t logical_row);
uint32_t tide_table_logical_row(const tide_table *table, uint32_t physical_row);
uint32_t tide_table_partition(tide_table *table, const tide_columns *columns,
                              uint32_t presence_column, bool require_present);
```

The first two functions translate a valid existing row and return the argument
for an identity layout. The partition function is called serially at a safe
structural boundary with no outstanding row pointers or running pieces. It
returns the physical prefix length matching the specified presence byte.
It moves all columns and maintains both inverse maps, detaching shared pages
before mutation. Allocation arithmetic and column/row validation must follow
the runtime's memory-safety contracts. Allocation failure uses the existing
runtime failure mechanism. It creates no entities and invokes no callbacks.

Batching several filters is deliberately absent from this API shape.
The existing gather/get/scatter/removal interfaces continue to accept logical
rows. Optimized row loops receive explicitly physical ranges. Mixing these
coordinate spaces must not be an implicit convention.

## Required integration

1. Implement identity and partitioned table storage with inverse-map validation,
   growth, removal, moves between tables and COW copies. Cover empty/tail/full
   tables, repeated partitioning, membership changes and retained snapshots.
2. Route entity lookups and ordinary queries through logical coordinates.
   Preserve existing iteration order for random draws, extern calls, events,
   reductions, spawning and all other observable effects.
3. Select physical traversal only for compiler-proven independent numeric row
   updates. Other systems retain logical traversal. Multiple competing filters
   must not repeatedly repartition a table without a measured policy.
4. Integrate snapshots/restoration, hashing, raw and delta packing, visibility
   projections, attached state, C row interop and reload metadata. Do not serialize
   derived pointers. Specify and test whether permutations are serialized or
   rebuilt before deciding any layout/version/hash-pin updates.
5. Add independent logical-state/order checks comparing partitioned execution
   with the unchanged path, across native/WASI, one/multiple threads, structural
   churn, overlapping filters and snapshot/restore/reload cycles.
6. Measure full tick plus required hashing, retained snapshots and membership
   changes, as well as the previous isolated ECS comparison. Include dense,
   sparse, tiny-world and conflicting-filter controls. Keep identity layout when
   partitioning does not repay its maintenance and indirection costs.

No speedup is assumed for these additional contracts. Logical-order hashing
and packing may consume part of the prototype's gain; that cost must be measured.
No shipping production code, runtime ABI or expected hash has changed. The
probe adds mapping/cache pointers to its private table header, clears them from
raw saves through the existing generated header clearing, and serializes tables
in logical order; restored tables start in identity layout. Its raw world header
size differs from the shipping build, so raw-byte equivalence was checked
between identity and packed worlds within the probe, not against old saves.

## Production sequence and game baseline (2026-10-29)

The owner asked how to complete integration and whether the docs samples and
`D:/Projects/tide-prototypes` hold up. The regressions above reject unconditional
packing, not a selective implementation. The next implementation should proceed
in independently testable steps:

1. Move the mapped table implementation into the engine with automatic selection
   disabled, rebuilding all consumers against its header. Test forced mapped
   tables through the actual generated/runtime paths, not a mixed-ABI overlay.
2. Make ordinary generated traversal preserve logical order: queries, events,
   commands, views, interpolation across differently mapped snapshots, visibility,
   local attachments and deferred changes. Translate entity locations before
   direct physical access. Audit C rows and hot reload explicitly; initially
   exclude whole tables used by unsupported consumers from packing.
3. Prove physical-loop eligibility through helper calls. Begin with independent
   numeric row writes and read-only singleton inputs, excluding cross-entity
   queries, random draws, extern calls, shared writes and queued effects unless
   their ordering has separately been proved safe. A safe system alone does not
   make its table's other readers safe.
4. Add conservative layout selection using table size, selectivity and membership
   changes, with one selected partition per table and protection against repeated
   repartitioning. Keep identity for tiny/mixed-density cases. The current evidence
   does not establish a universal size/density threshold. Include the logical
   hash cache, snapshot retention and extra memory in the selection measurements.
5. Replay identical gameplay inputs with packing forced off/on, asserting that
   the on run really packs rows. Compare every tick's logical hash, entity IDs,
   event/command order, restores and network corrections; validate views as well
   as simulation. Then measure full tick/hash/snapshot and frame costs. Merely
   passing games which stayed on the identity fallback is not packed coverage.

Concrete existing cases:

- `docs/examples/systems.tide` has only nine bodies, two filters, `Snap()` and
  filtered views. It should stay cheap through the identity path.
- `docs/examples/entities.tide` changes Poisoned membership via commands and
  destroys depleted entities, exercising more than numeric row updates.
- `examples/samples/asteroids/game.tide`, `Shoot`, sends a hit to the first
  overlapping rock and returns. Physical reordering must not change that rock.
- Foldline uses Box3D C interop, shared mutable state and cross-entity queries;
  Gravewake's combat and views also query other entities. Neither is covered by
  the isolated wide-game proof.

Baseline validation uses engine `765935637` and the owner's prototype checkout
at `31429e7`, whose tracked files were clean (untracked `.claude/` was left alone).
The release and web-release configurations include that checkout through
`TIDE_EXTERNAL_DIRS`, with platform and docs examples enabled. All 49 docs
examples and 11 gallery games build on both targets, and all 60 pass a native
`--frames 3` startup check with `TIDE_HIDDEN=1`. The existing Inside Out,
Underfoot, Foldline, Gravewake, Banked and Two Forks simulation test targets
pass on native and WASI (six targets per platform).
Inside Out, Underfoot, Foldline, Gravewake, Banked and Parry also build on both
targets and pass the same bounded native startup check. Foldline's build-time
map export/connection check passes on both targets.

These checks use shipping identity storage, not the packed overlay. They do not
measure gameplay performance or prove future packed compatibility. No browser
playtest, long gameplay run, Sidekick tool test or new Bevy comparison is claimed;
Parry has no registered simulation test. No prototype source or hash pin changed.
Local build/test/startup logs are under `build/packed-integration/`.
