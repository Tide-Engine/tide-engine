# Skinned CPU4 implementation validation

Source HEAD during validation: 398f3a49cf1e256c8b5c7489adb14890ef02d55e. Exact frozen source/test hashes are in source-hashes.json (2895 files, including untracked fixtures/goldens).

Node: v24.19.0. TIDE_HIDDEN=1. Builds and suites ran sequentially under the shared gate. No performance benchmarks ran.

- cmake --build --preset debug --target tide_tests tide_platform_skinned tidec_e2e_skinned_rest tidec_e2e_skinned_draw_borrow tidec_e2e_skinned_attribute_limit -j 8: exit 0; 2.42 seconds.
- C:\Users\blenm\.codex\worktrees\1315\PurrEngine\build\debug\bin\tide_tests.exe skinned: exit 0, 42/42 tests; 0.61 seconds.
- ctest --preset debug --verbose -R ^(tide_platform_skinned|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 4/4 tests; 0.69 seconds.
- cmake --build --preset debug -j 8: exit 0; 11.57 seconds.
- ctest --preset debug --output-on-failure: exit 0, 2067/2067 tests; 284.96 seconds.
- cmake --build --preset web-debug -j 8: exit 0; 9.8 seconds.
- ctest --preset web-debug --verbose -R ^(tide_tests|tide_platform_skinned(_webgpu)?|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$: exit 0, 6/6 tests; 5.3 seconds.
- ctest --preset web-debug --output-on-failure: exit 0, 2037/2037 tests; 346.73 seconds.
