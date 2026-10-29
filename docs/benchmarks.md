---
description: "Measured ECS performance for Tide and other ECS implementations, with workload details, methodology, variation and the limits of each comparison."
---

# Benchmarks

How much work can Tide's ECS do in a tick? These small CPU workloads give a baseline for comparing ECS performance.

This measures **ECS updates**, including deferred structural changes where used. It excludes setup, world hashing, snapshots, networking and rendering. It does not measure the cost of a complete Tide multiplayer tick or predict a game's frame rate.

<div data-tide-benchmarks></div>

## Reading the results

Lower is faster. Each cell is the median of separate process medians, in milliseconds per tick. The smaller numbers underneath give the minimum and maximum of those process medians. These ranges show run-to-run variation; they are **not confidence intervals**. Close or overlapping results need more measurements before calling a winner.

Every workload stays visible, including cases where another implementation is faster. There is no overall score: moving particles, filtering wide components and changing entity structure exercise different costs. Reports from different runs remain separate.

## What the workloads do

| Workload | Work measured |
| --- | --- |
| 1M particles | Update position and velocity for one million particles. |
| 1M wide components | Update one million entities with wider component data. |
| 50%, 95% or 99% frozen | Update only the remaining active entities. Frozen membership is fixed during the run; this does not measure changing filter membership. |
| 2k bullets, churn | Repeated spawning and destruction alongside movement. Deferred changes are applied inside the timed tick. |

The implementations use the same initial values and update arithmetic. The runner checks final state digests across implementations before accepting a result. That checks these workloads; it does not establish cross-platform determinism for the other ECS libraries.

## What is being compared

Tide generates specialized C storage and systems for each game. Bevy uses its Table component storage and schedule. The broader serial comparison also uses Flecs cached archetype systems and EnTT views and an owning filtered group with explicit dispatch. EnTT does not supply an integrated scheduler in this comparison, so these paths do not provide identical engine services.

Each report names its exact implementations, versions and thread count. Compiler settings preserve the update's floating-point operation order: no fast math or fused multiply-add contraction. Tide keeps its determinism requirements; the harness does not disable them to improve a result.

These are selected synthetic workloads on one machine, maintained by the Tide project. Cache layout, entity count, query shape, structural changes, scheduling and hardware can change the outcome. Use them as a starting point for a workload that resembles your game.
