---
description: "Capture structured profiling reports or use Tide's in-game profiler to inspect game and engine costs, source locations and draw culling work."
---

# Debug views

Debug views are panels the engine draws over a running game, for the people making it. There's one so far: the profiler, which shows where a frame's time goes, and a tick's.

For automated investigation, a profile capture records measurements without
showing either panel. It includes source locations and engine draw diagnostics
that can be read directly by an agent or a script.

## Profile captures

Run a representative input script in an optimized build, keeping the seed,
resolution and settings fixed:

```sh
tide run --release --script play.txt --frames 360 --size 1280 720 --seed 7 --profile profile.json --profile-warmup 60
```

`--profile` writes a JSON report when the run ends normally. Its path is relative
to where `tide` was invoked, and the parent folder must already exist. It needs
`--frames`, `--script` or `--interactive`; the option itself does not start a
window or set a frame limit. A script must quit or have a `--frames` cap. The
same options work on a built game's command line. Report capture is currently
for native desktop players, not dedicated servers, Android or `tide run --web`.

`--profile-warmup 60` plays the first 60 frames normally without including them
in the report. With `--frames 360`, frames 61–360 are measured, unless the game
ends sooner. The default is zero warmup frames; the accepted range is
0–1,000,000,000. Keep startup frames when startup is what you want to investigate.
With `--players 2`, each player writes its own report, such as `profile-1.json`
and `profile-2.json`.

For dynamic play, add `--profile profile.json` to `--interactive play.txt`, step
the game and finish with `quit`. Frames after warmup are measured across those
steps; time waiting for the controller is excluded. There are no interactive
profile start/stop commands. The recorded `play.txt` lets a later run repeat
the actions without the controller.

The report separates actual CPU callback work from the fixed simulation step
used by hidden runs. It does not infer FPS from that step or from the reciprocal
of CPU work. Headless audio mixing, capture readback, display waiting and controller thinking happen
outside the measured callback. Timings include profiling overhead; compare
equivalent optimized runs with the same instrumentation and avoid concurrent
builds or other benchmark runs.

The capture includes CPU phase measurements, systems and views with their full
names and source locations, worker measurements, available GPU samples, and
draw-recording diagnostics. Read the report's schema version and measurement
descriptions before combining values. System timings span their first task's
start to their last task's end; overlapping systems are not CPU totals to add
together. Worker and GPU intervals can overlap frame work too. GPU samples
arrive asynchronously and are not associated with individual CPU frames;
unavailable samples are not zero GPU cost.

### Reading the JSON

Check `schema == "tide.profile"` and `version == 1` before interpreting a report.
The `semantics` object repeats the measurement definitions in the file itself.

| Field | How to read it |
|---|---|
| `metadata` | Build identity, optimization/sanitizer flags, renderer, threads, player, resolution and exit code. `executable_sha256` identifies the executable when available; `layout_hash` describes the generated layout and is not an executable hash. Keep the input script, seed, settings and saves with the report too. |
| `frames` | Total frames, requested warmup, measured count and first/last measured frame. Warmup can consume the entire run: then `measured`, `first` and `last` are zero. |
| `cpu_work` | A distribution of callback wall time for each measured frame, in milliseconds. It excludes presentation, headless audio mixing, screenshot readback, controller waits and report collection; instrumentation and any enabled overlays remain included. |
| `cpu_phases` | Distributions for `input`, `match`, `views`, `render` and `other`. Match includes netcode and ticks; Views includes game views and local/GUI work; Render is CPU renderer submission. A phase's p95 need not come from the frame with the highest total time. |
| `ticks` | Tick count and accumulated systems interval and following changes/events/tasks interval. This includes ticks run again for prediction; it is not just the number of newly advanced ticks. |
| `systems`, `views`, `workers` | Rows with `calls`, active `frames`, `total_ms`, `mean_call_ms`, `mean_frame_ms` and `max_frame_ms`. `mean_frame_ms` divides by **all measured frames**, including those where the row did not run. These rows have no per-source percentile distribution. |
| `gpu` | `available` means at least one timing sample arrived. Inspect `timing.samples`; `frame_aligned` is false. Pending or unsupported results are absent, and CPU and GPU intervals overlap. |
| `render_work` | Total renderer draw calls and vertices across the measured frames. These are separate from instances retained in draw commands. |
| `draw_diagnostics` | Mesh recording time and work counts, grouped by source, destination `target` and `camera_slot`; see below. |
| `limits` | Dropped timing observations, draw calls and draw candidates. Nonzero values mean the affected source tables are incomplete. |

Source objects contain the full `name`, `file`, `line` and `column`. Generated
systems and views point to their declaration, not the deepest helper or an
individual draw call. Helpers and engine work remain inside their caller's
inclusive measurement. Code without a source location has `file: null` and
zero line/column; worker rows currently group queue wait, compute, input capture
and result transfer across workers, rather than identifying worker declarations.

Each distribution contains `samples`, `total_ms`, `mean_ms`, `min_ms`, `max_ms`,
`p50_ms`, `p95_ms`, `quantile_samples` and `quantiles_estimated`. With no samples,
the total is zero and the mean/min/max/percentiles are null. Percentiles are
exact nearest-rank values through 4,096 samples, then estimates from a bounded
deterministic reservoir. Totals, means and extrema still cover every sample.
The timing table holds at most 8,192 source rows and 1 MiB of copied names/paths;
draw diagnostics have a separate limit of 1,024 rows and 1 MiB of source text.

For example, Python can rank views and inspect draw work without losing integer
counter precision:

```python
import json

with open("profile.json", encoding="utf-8") as file:
    report = json.load(file)
assert (report["schema"], report["version"]) == ("tide.profile", 1)
print("frames:", report["frames"], "limits:", report["limits"])
print("CPU:", report["cpu_work"], "GPU:", report["gpu"])
for row in sorted(report["views"], key=lambda row: row["total_ms"], reverse=True)[:10]:
    print(row["source"], row["mean_frame_ms"], "ms/frame")
for row in sorted(report["draw_diagnostics"], key=lambda row: row["mesh_recording_ms"], reverse=True)[:10]:
    print(row["source"], row["target"], row["camera_slot"],
          row["mesh_recording_ms"], "ms", row["tested"], "tested", row["rejected"], "rejected")
```

### Finding unnecessary drawing work

Mesh frustum tests happen while a view records its draw list. Their cost belongs
to that view, before the host's Render phase submits the list to the GPU.
Draw diagnostics connect that engine work to its originating view and
destination/camera, with counts of candidates tested, rejected, bypassed and
submitted. Mesh recording time starts after mesh acquisition and includes
bounds, frustum tests, LOD selection and instance recording; it is not a
culling-only, upload or GPU timer. Submitted instances are those retained in
draw commands, not GPU draw calls. Untestable instances are kept conservatively
when their bounds, camera or transform cannot be tested safely. Bounds-cache
hits, bounds builds and frustum rebuilds help explain repeated preparation.

Camera slots are one-based and follow the order in which a view uses distinct cameras for a
destination in each frame. Camera movement preserves that grouping when the
use order stays the same; changing conditional camera order can regroup it.
`target: "0000000000000000"` identifies the window. Other targets and camera
slots are diagnostic identities, not named GPU passes.
The diagnostics table is bounded; check dropped-call/candidate counts before
treating its rows as complete coverage.

For example, a grass view may spend substantial time recording meshes while
its frustum tests reject almost no instances. Follow the view's source location
and inspect whether it already selects visible instances. The existing scoped
setting lets you compare the appropriate draw:

```csharp
Draw.FrustumCulling(false)
{
    // Draw the selected meshes here.
}
```

A low rejection rate is a reason to investigate, not proof that culling can be
removed. Prioritizing on-screen objects does not guarantee every submitted
object is visible. A main-camera selection also says nothing about an off-screen
object's shadow. Keep the destination and camera separate when comparing work.

Make one change and replay the same script with the same warmup, resolution,
seed and build mode. Compare CPU and GPU measurements alongside the work counts.
Check captures across camera directions and aspect ratios, including shadows;
check simulation values or hashes when the change could affect the match.
Repeat alternating before/after runs when the difference is close to run-to-run
noise. The report identifies measured costs and candidate waste; it does not
automatically decide that an operation is redundant.

## Turning them on

They're off until the game, or whoever starts it, asks for them:

- The game asks with a setting (see [Settings](../language/basics.md#settings)):

  ```csharp
  settings
  {
      debugViews = true;
  }
  ```

- Whoever starts it asks with `--debug-views`: `tide run --debug-views --stats`, or a built game's own command line, like `mygame --debug-views --stats`. On a web page, it's `debug-views` in the page's address: `https://example.com/game.html?debug-views&stats`. These examples also enable the stats drawer. This works whatever the setting says, so a build made to ship can be looked into without building it again.

With either, **F7** shows the profiler, and F7 again hides it. The profiler measures while it is visible; the [stats](cli.md#stats) also measure while their drawer is open. With the profiler hidden and the stats closed, neither asks for timings unless a profile capture is active. The game sees the key too.

Under `tide run`, changing the setting takes effect at the next reload. A setting is part of the build, so `tide build` keeps it: take `debugViews` out before players get the game, unless they may see the profiler too.

## The profiler

The profiler has two columns: a frame on the left, a tick on the right. Its numbers are averages over the last half second, so they hold still long enough to read, and each row's bar is its share of its column's first row.

```
Frame  60 fps, longest 21.4 ms   16.67 ms     Tick  180 a second          0.93 ms
  Input                           0.02          Systems                   0.78
  Match                           3.20            Movement.Integrate      0.23
    Ticks  3.0 a frame, 2.0 again 2.80            Movement.Collide        0.19
    Netcode                       0.40            Combat.ApplyDamage      0.07
  Views                           1.37            3 more                  0.01
    DrawArena                     0.63          Changes, events           0.16
    Hud                           0.23
    GUI, tasks                    0.13
  Render                          0.60
  Other                           0.00
  Swap, idle                     11.48
Memory  162 MB in RAM, 12 MB on the GPU
```

**A frame**, in milliseconds a frame:

| Row | What it is |
|---|---|
| Frame | From one frame's start to the next, with the frame rate and the longest frame of the half second |
| Input | Reading the keyboard, the mouse, gamepads and touches |
| Match | The match's share of the frame: its ticks, and its netcode |
| Ticks | The game's ticks, and how many a frame ran. On a client, `again` is how many of them were predicted ticks run again, after another player's input turned out not to be what was guessed (see [Networking](../multiplayer/networking.md#prediction-and-rollback)) |
| Netcode | What the match took beside its ticks: packets, snapshots, and hashing and packing worlds |
| Views | Every view, then the six that took longest, by name, and the others together |
| GUI, tasks | What the frame's views took beside the views themselves: the GUI, local tasks, and applying local changes |
| Render | Drawing what the views recorded, as far as the CPU does it. GPU drawing time appears in the [stats](cli.md#stats) on backends with a GPU timer |
| Other | The rest of what the frame did |
| Swap, idle | The rest of the frame: waiting for the display, the GPU or the browser |
| Memory | What the program has, as the system counts it, and what its drawing has on the GPU by the engine's own count (every texture, mesh and render target, by what each holds; not the window's framebuffer or the driver's own). No time, so no bar |

**A tick**, in milliseconds a tick:

| Row | What it is |
|---|---|
| Tick | One tick, and how many ran a second: more than the tick rate when ticks run again |
| Systems | The tick's systems, from the first one's start to the last one's end, then the ten that took longest, by name with their namespace, and the others together |
| Changes, events | What follows the systems in a tick: spawns, destroys and other structural changes, the events' handlers, and tasks |

- A system's time is from the start of its first piece of work to the end of its last. Systems that ran at the same time on several threads add up to more than `Systems`, which is what the clock says. `tide schedule` shows which systems wait for which, and why (see [The schedule](../game/schedule.md)).
- In hidden runs, the panel's Frame/FPS and Swap, idle rows use the supplied simulation interval. Use a [profile capture](#profile-captures) for measured CPU work; those panel rows do not measure hidden-run throughput.
- Outside a match, no tick runs, and the tick's column says so.
- A view that didn't run, like one that reads the match while there's none, has no row.
- When `Ticks` is most of a frame and many of them run again, clients are paying for prediction: see [Input delay](../multiplayer/matches.md#input-delay).

The [stats](cli.md#stats) are separate: a line in a corner once they're asked for, which a click or F8 opens to a few numbers and the three systems that took longest on average per measured run over the last half second.

A C host shows the profiler itself with `tide/profiler.h` (see [C hosts](../c/hosts.md#the-standard-host)).
