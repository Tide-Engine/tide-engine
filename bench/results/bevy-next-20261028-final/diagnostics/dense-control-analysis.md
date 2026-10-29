# Dense fallback: observed cost, unresolved attribution

The adverse 90%-frozen result remains in the evidence. The
[separate five-round control](../../bevy-next-20261028-density-control/summary.md)
reports 1.37005 ms Before and 1.49350 ms current, with a median paired change of
+10.16%. This does not establish that the wrapper caused that entire difference.

## Static proof

The current [slow task](slow-task.asm) and the
[c30 control task](../../bevy-next-20261028-probes/diagnostics/probes/c30_control/task.asm)
have identical normalized object disassembly: all 578 instruction lines,
instruction offsets/opcode bytes and relocation targets match. The check used
`llvm-objdump -dr --disassemble-symbols=<symbol> <object>`, retained the symbol
body, normalized LF/outer whitespace, and replaced only
`tide_task_Shift_slow` with `tide_task_Shift`. The exact input paths,
normalization and matching SHA-256 are recorded in the [audit](dense-control-audit.json).
This proves identity of that object function body; it does not prove identical
linked-code placement or machine conditions.

There is real additional work in the [wrapper](task.asm). It saves eight general
registers and reserves 0x98 stack bytes before deciding whether the chunk is
dense. It validates the complete cache key, filter descriptor, actual row count
and presence-page identity before reading/testing density at offsets
0xf0–0xff. A dense chunk then unwinds that frame and tail-jumps to the unchanged
slow task, which performs its ordinary cache lookup again. Static inspection
does not quantify how much tick time this adds.

## The unchanged C reference also varies

These are the recorded per-process medians, in milliseconds:

| Round | Before Tide | Current Tide | C inside Before | C inside current |
|---|---:|---:|---:|---:|
| 1 | 1.40620 | 1.42275 | 1.12980 | 1.19945 |
| 2 | 1.59580 | 1.58745 | 1.28915 | 1.14990 |
| 3 | 1.32780 | 1.49350 | 1.12050 | 1.25305 |
| 4 | 1.37005 | 1.60495 | 1.15300 | 1.28795 |
| 5 | 1.31930 | 1.45340 | 1.10835 | 1.09920 |

In rounds 3 and 4 both current Tide and the unchanged internal C reference slow
relative to their Before process. Other rounds differ. The ways execute at
different times; C is contextual evidence, not a correction factor. No adjusted
or normalized speedup is claimed. Environment, linked placement and wrapper
overhead remain unresolved contributors. Production stays at the measured
implementation; no new density shortcut is promoted from this analysis.

The [15-process records](../../bevy-next-20261028-density-control/records.jsonl)
contain five complete Before/current/Bevy triples. The read-only audit confirms
all exits zero, matching raw stdout hashes and parsed JSON, fixed one-thread
1M/90%-frozen options with 200 warmup/400 measured ticks, all ten Tide-process
`ok:true` results, and one shared digest `0x07c33fc7d6a94b2d` across all fifteen
processes. Metadata records no detected contention or quiet reset. This analysis
ran no builds or workloads and adds no timing samples.
