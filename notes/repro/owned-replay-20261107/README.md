# Owned replay diagnosis

Windows Clang 23 / ASan, 720 updates, 60 Hz, loopback seed 42, latency 40 ms.
All four cases verify endpoint 717 with zero repairs and zero resyncs.
Counters below describe the first client only. They are deterministic work
counts, not release CPU timings. Every source/binary was built in this checkout.

| Case | Run ticks | Replayed ticks | Near comparisons | Accepted | Singleton differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| all-entity singleton, constant input | 6324 | 4907 | 701 | 0 | 701 |
| no singleton work, constant input | 1718 | 301 | 658 | 658 | 0 |
| no singleton work, changing remote action input | 6324 | 4907 | 0 | 0 | 0 |
| no singleton work, constant action fields, presented viewTime | 6275 | 4858 | 7 | 7 | 0 |

The last case calls mark_presented every update and reads the engine's implicit
viewTime into a component, as Mount & Rifle reads it into Orders.seen. It changes
no keyboard/action field. as_predicted rejects changed foreign input before
near_same is reached. The remaining 301 replays in the constant control are
consistent with the bounded 15-tick far-state refresh; this probe does not log
each refresh decision independently.

The all-entity singleton still runs in predicted ticks and derives different
values from held far rows. Its singleton comparison rejects every tested reuse.
Both are existing semantics. Skipping remote input blindly is unsafe for
singleton systems/helpers that consume it; skipping singleton systems changes
the approved Owned contract. A smaller prediction execution set needs a design
decision and dependency/side-effect analysis, not just a wider stale interval.

## Generated writable preparation defect

wide.tide contains 1000 unowned rows with 88 float lanes. Its singleton-writing
per-entity system uses canonical execution. In 200 predicted ticks no row is
eligible, but old gen_canonical_lanes copied/prepared all writable lanes anyway.
The new chunk-level all-Far check skips both preparation and finalization.

Instrumented generated-C control (only the two new guards removed): 200 begin
calls before, zero after; Counter.calls remains zero and both final world hashes
are 1444539242358288980. The instrumentation counts writable preparation calls,
not allocations or bytes. No end-to-end battle speedup is established.
wide_probe.c includes the generated C and wraps only lanes_begin_pieces. Compile
it alone with the engine library; do not separately link game.c.

## Reproduction

Set TIDE_HIDDEN=1 in every shell. Generate each .tide into an existing separate
directory under build/ using build/sanitize/bin/tidec.exe SOURCE -o DIR --name
game. Compile probe.c plus that directory's game.c with Windows clang:

```
-std=c17 -O1 -g -fsanitize=address -D_DLL -D_MT -Xclang --dependent-lib=msvcrt
-include string.h -Iengine/include -IDIR probe.c DIR/game.c
build/sanitize/engine/tide.lib -o DIR/probe.exe
```

Copy this clang's clang_rt.asan_dynamic-x86_64.dll beside each probe. With
ASAN_OPTIONS=detect_leaks=0, run with-singleton and without-singleton with no
arguments; run without-singleton with one argument for changing remote input;
run metadata with two arguments for constant actions plus presented timestamps.
All loops have a fixed 720-update bound. An initial scratch build omitted output
directories; another probe forgot to zero its world before init. Both harness
mistakes were corrected before recording the results above.

For wide_probe, use -IDIR and link the same engine library. The baseline copy
game_before.c is generated game.c with only blocks matching
`            { const uint8_t *far =` through their closing indented brace removed
(exactly two matches). Compile wide_probe with BASELINE defined for that control.

## Actual-game evidence and limits

Read-only game dev 241d92d, .shots/visible full_owned profiles: host/client
BuildKitBodies 1.919/19.051 ms per frame, Roam 19.452/18.515. Owned client runs
20435 ticks versus host 2098. Thus Roam's large full-tick work is not multiplied
by the replay count, whereas singleton BuildKitBodies is. System intervals can
overlap; these values must not be summed as CPU work. Samples are whole-run means,
not warmup-controlled performance comparisons. Existing unrelated game processes
were left running.

The source samples animated barrel/muzzle values and stamps viewTime each frame.
This probe establishes replay mechanisms, but no field-by-field trace of the
actual full-world battle was collected. Do not claim an exact share of its
replays is due to each field. Neither the game nor its defaults were changed.
