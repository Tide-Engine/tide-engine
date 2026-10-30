# Correctness and profiling diagnostics

`profile-before.txt` samples the frozen `a2564c773` executable;
`profile-probe.txt` samples the intermediate quarter-density mask candidate.
Both run 1M rows, 99% frozen, one thread, 200 warmup/4,000 measured ticks through
`tide_sample --top 25 --lines 30 -- <versus_wide> ...`. Sampling perturbs timing;
their printed times are not performance estimates. They include the subsequent
handwritten C reference and thread samples, so percentages are not normalized
to Tide-only tick time. The original profile concentrates Tide samples in its
filtered rows loop; the probe reduces traversal work but still samples row
arithmetic and task preparation/counting. No cache-miss or memory-bandwidth
counter was measured, and no storage-layout causal claim follows.

The two `*-tests.txt` logs retain the initial final-guard validation, including
the old four-variant parameter-count assertion failure. The two subsequent
`*-parameters.txt` logs pass after checking all six variants: filtered/every
in-place/apart plus selected-index and masked loops. Every signature remains
within 1,000 parameters; V8 also accepts the actual generated WASM object.
The other 17 selected tests per platform pass without that assertion change.
`../validation.json` records the 66 native Tide/C, WASI Tide/C and Bevy checks.

`mask.c` is a standalone extraction kernel. It compares presence bytes before
combining masks, and contains an additional weighted reduction experiment.
The generated Tide implementation uses SSE2/WASM bit extraction or a scalar
lane-extraction fallback, rather than the weighted experiment. `check.c`
exhaustively checks all 65,536 patterns, with presence bytes 0/1/128/255 and
two-column intersections against independent scalar bits. The native command
below exits zero and prints:

```text
All 65536 presence masks match native, scalar, weighted and two-filter references.
```

To rerun from this directory with Clang 23:

```powershell
$env:TIDE_HIDDEN = '1'
& 'C:/Program Files/LLVM/bin/clang.exe' -std=c17 -O2 -fno-fast-math -ffp-contract=off -Wall -Wextra -Wpedantic -Werror check.c -o check.exe
./check.exe
```

The saved `.s` files are warning-free assembly-only checks of that standalone
kernel. Each uses Clang 23.1.2 with common flags
`-std=c17 -O2 -ffreestanding -Wall -Wextra -Wpedantic -Werror -fno-fast-math
-ffp-contract=off -S mask.c` and the following target/output options:

| Output | Target options |
| --- | --- |
| x86.s | `--target=x86_64-unknown-linux-gnu -march=x86-64 -o x86.s` |
| wasm.s | `--target=wasm32-unknown-unknown -msimd128 -o wasm.s` |
| arm64.s | `--target=aarch64-unknown-linux-gnu -march=armv8-a+simd -o arm64.s` |

These checks establish neither ARM execution nor generated Tide ARM coverage.
Actual generated Tide WASI SIMD execution is covered by the separate CTests.
