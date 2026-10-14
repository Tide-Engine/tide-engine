# Skinned CPU3 implementation validation

Source HEAD during validation: 874422c0becc35116c94e31e4d92bbe074dde6b3. Exact frozen source/test hashes are in source-hashes.json (2886 files, including untracked fixtures/goldens).

Node: v24.19.0. TIDE_HIDDEN=1. Correctness builds and suites ran sequentially under the shared gate; docs built concurrently with the native correctness build. No saved performance comparisons ran. One bounded unsaved rendering smoke checked work counts and left the ledger unchanged; its timings are not performance evidence.

- cmake --build --preset debug --target tide_tests tide_platform_skinned tidec_e2e_skinned_rest tidec_e2e_skinned_draw_borrow tidec_e2e_skinned_attribute_limit -j 8: exit 0; 1.55 seconds.
- C:\Users\blenm\.codex\worktrees\1315\PurrEngine\build\debug\bin\tide_tests.exe skinned: exit 0, 40/40 tests; 0.61 seconds.
- ctest --preset debug --verbose -R ^(tide_platform_skinned|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 4/4 tests; 0.69 seconds.
- cmake --build --preset debug -j 8: exit 0; 25.4 seconds.
- ctest --preset debug --output-on-failure: exit 0, 2064/2064 tests; 260.7 seconds.
- cmake --build --preset web-debug -j 8: exit 0; 27.16 seconds.
- ctest --preset web-debug --verbose -R ^(tide_tests|tide_platform_skinned(_webgpu)?|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 6/6 tests; 5.13 seconds.
- ctest --preset web-debug --output-on-failure: exit 0, 2034/2034 tests; 311.13 seconds.
- cmake --build --preset release --target bench_skinned -j 8: exit 0; 6.95 seconds.
- npm run build (docs): exit 0.
- bench_skinned --counts 1000 --frames 3 --warmup 2 --repeat 1: exit 0.

Observed platform pool proof stdout:
- 2011: ok: 2048 independent palettes match serial preparation (pool 12, callbacks 4, workers 3)
- 1976: ok: 2048 independent palettes match serial preparation (pool 1, callbacks 0, workers 0)
- 1977: ok: 2048 independent palettes match serial preparation (pool 24, callbacks 1, workers 0)
