# Mount & Rifle v126 follow-up, 2026-11-07

This is a partial source investigation, not a new battle reproduction or a
claim that the requested first-divergence investigation is complete. The game
and its defaults were not changed. No runtime defect was established or fixed.

## Confirmed boundaries

Read-only game `t3-65b9e93a/game/game.tide` selects Prediction.Delay under
PREDICT_DELAY and range 5 under PREDICT_5. Neither makes the campaign public.
`campaign/economy.tide:145` still creates Audience.None convoy groups. Thus
the default battle is not an all-visible world. The earlier typed endpoint
1301 trace in `mount-rifle-logical-identity-investigation.md` identifies public
Works.stock, entity `20:5edd30a7f97041ed`, server 0 and client
18.41666603088379, caused by a hidden convoy pickup. That is historical evidence,
not a trace of the supplied v126 Delay run.

`infantry/crowd.tide:57` clears and rebuilds public Crowd.spots by querying all
locally present living Troopers. Each entry includes identity and position.
A server with additional living hidden troops therefore produces additional
entries which the client cannot derive. This establishes the dependency and
the disclosure through the public singleton; it does not measure the earliest
v126 mismatch or its hidden troop count.

`compiler/src/predict_far_codegen.inc` and `engine/src/session.c` implement
spatial prediction, not owned-only prediction. The box around owned positions
is expanded by the range; nearby unowned rows also predict. Confirmed ticks
simulate the whole permitted world. Singleton queries still visit far rows
unless explicitly filtered. Near-state retention compares singleton values,
entity/command/task state, input and whole heap hashes; it is not a comparison
of the owned controller alone. Far retained state is bounded by 0.25 seconds.
No automatic per-tick far-component transport follows merely from setting
predictionRange. Increased repair volume needs a measured mismatch trace.

`platform/src/platform.c:548` supplies headless frames a fixed 1/60 second.
`platform/include/tide/host.h:1696` divides frame counts and transport byte
increments by host simulation time. Its 60 fps and download rate are therefore
simulation-time measurements in these runs; CPU profile work can exceed
16.7 ms per frame. Rates are not measured wall-clock throughput.

`session.c` increments client world_bytes for accepted unique snapshot and
download chunk payloads. Duplicate chunks return before incrementing. This
counter includes world/delta/hash transfers, excludes their packet overhead,
and is not total wire traffic or total retransmission volume. The stats panel
formats world data using 1024*1024 bytes despite the MB label. Transport received
bytes are a different counter. Supplied profile JSON has no network category
breakdown; logs contain gameplay output, not typed mismatch dumps.

Idle controls do not imply constant transmitted input: the prior source trace
in netcode.md identifies sampled animated muzzle/barrel and engine-stamped
viewTime. `as_predicted` rejects changed remote input. Eight replayed ticks are
not evidence of eight repairs. Relative contributions of these input fields
remain unmeasured in the supplied v126 run.

A first-class owned-and-descendant predictor combined with delayed full-world
simulation remains unimplemented (netcode.md). Spatial prediction is the
existing approximation and has the dependency limitations above. Changing that
contract requires owner language/behavior decisions.

## Validation and outstanding evidence

Built here with Windows Clang 23 ASan, platform OFF, external directories empty:
16/16 visibility-filtered engine tests, 3/3 predict_near tests, and 1/1
predict_kept test pass. Near tests use fixed loopback seed 17, including latency,
jitter and loss; they check near unowned prediction and exact far retention.
Kept tests check bounded stale far state. These are existing small deterministic
regressions, not new game measurements. No web or full battle run was performed.
Initial configure inherited an unrelated external prototype directory and failed;
explicitly clearing external directories allowed the local build.

Still needed: typed server/client v126 Delay endpoint dumps; an actually
all-visible small Delay control; earliest Crowd divergence and hidden count;
packet categories with retransmissions and both timebases; and range-5
same-tick repair sizes/content plus field-level input rollback attribution.
Similar repair counts alone establish none of those causes. Supplied cumulative
world counts (4.70 versus 11.68 MiB, 28 versus 30 repairs) also cannot explain
the full rate difference without interval-aligned transfer accounting.

All named game/artifact paths were accessible. Initial large concatenated note
reads were truncated, so full subsystem/history reading is not claimed and no
source changes were made on that basis.
