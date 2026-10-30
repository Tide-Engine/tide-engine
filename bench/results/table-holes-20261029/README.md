# Holes: rows entities leave stay where they were

A = `dev` at 2bee50f69 (rows filled from the end), B = holes (notes/ecs.md,
Holes). Both built release from the same benchmark sources (the sleep
benchmark's balanced toggles included). Every A/B pair ends with the same
digest. `ab.py` alternates the two programs for five rounds and reports the
median of each side's medians and the median paired change of B against A;
`final_runs.sh` is the matrix. Windows 11 / Ryzen 9 7900X, Clang 23.1.2 O2,
one thread unless noted. No quiet guard or mutex: other desktop activity was
uncontrolled, and churn's baseline moved between batches (0.31 to 0.42 ms), so
small changes there are noise-sized.

`versus_sleep` runs a predicting client's period: 12 ticks each kept in a ring
of 16 and hashed, with a rollback of 8. Half of `--toggles` wake sleeping bodies
and half settle awake ones each tick, so the share asleep stays at 90%.

| 100k bodies, ms a period | Sparse tag | Moving tag before | Moving tag, holes | Change |
| ---: | ---: | ---: | ---: | ---: |
| 0 toggles | 3.90 | 0.229 | 0.230 | +0.6% |
| 10 | 4.05 | 1.119 | 0.557 | -49.7% |
| 30 | 4.31 | 2.866 | 0.959 | -66.4% |
| 100 | 4.88 | 5.762 | 2.163 | -62.5% |
| 300 | 5.52 | 8.517 | 4.133 | -51.2% |
| 1000 | 6.50 | 10.816 | 8.306 | -23.4% |

At 10k bodies, 10/100 toggles: -41.2%/-7.1%; at 1M bodies, 10/100 toggles:
-11.6%/-51.6%. Sparse tags don't move rows and are unchanged (within 1.3%).

Regression checks, ms a tick: particles 1M -2.5%, wide 1M +2.4%, wide 1M 50%
frozen -2.4%, wide `[Lasting]` 1M 95% frozen -0.3%, rollback -0.2%. Churn at
2,000 bullets a tick: without kept snapshots (a server) +0.9% to +3.9% in the
rerun and +13% in the threshold probe; with a kept snapshot (a client) -6.5% to
-10.4%. Four threads, no snapshot: +2.4%.

`threshold-probes.txt` keeps the rejected policies: filling the lowest hole on
arrival (which copies a cold chunk per arrival), and settle thresholds of 1/8,
1/16 and 1/64.
