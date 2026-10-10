# Skinned CPU5 final implementation validation

Source HEAD during validation: 000a5986ba637eb9638f8b187b74736e95103378. Exact frozen source/test hashes are in source-hashes.json (2895 files, including untracked fixtures/goldens).

Node: v24.19.0. TIDE_HIDDEN=1. Builds and suites ran sequentially under the shared gate. No standalone saved comparison benchmarks ran. CTest includes bounded benchmark correctness tests.

- cmake --build --preset debug --target tide_tests tide_platform_skinned tidec_e2e_skinned_rest tidec_e2e_skinned_draw_borrow tidec_e2e_skinned_attribute_limit -j 8: exit 0; 1.58 seconds.
- C:\Users\blenm\.codex\worktrees\1315\PurrEngine\build\debug\bin\tide_tests.exe skinned: exit 0, 46/46 tests; 0.62 seconds.
- C:\Users\blenm\.codex\worktrees\1315\PurrEngine\build\debug\bin\tide_tests.exe pack: exit 0, 30/30 tests; 0.03 seconds.
- ctest --preset debug --verbose -R ^(tide_platform_skinned|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 4/4 tests; 0.6 seconds.
- cmake --build --preset debug -j 8: exit 0; 49.21 seconds.
- ctest --preset debug --output-on-failure: exit 0, 2067/2067 tests; 313.94 seconds.
- cmake --build --preset web-debug -j 8: exit 0; 34.66 seconds.
- ctest --preset web-debug --verbose -R ^(tide_tests|tide_platform_skinned(_webgpu)?|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 6/6 tests; 5.69 seconds.
- ctest --preset web-debug --output-on-failure: exit 0, 2037/2037 tests; 371.54 seconds.
