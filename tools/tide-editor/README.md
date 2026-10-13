# Native scene editor

Build `tide_editor` and `tide_cli` in a native platform-enabled build. The package
installs `tide-editor` beside `tide`; `tide open file.tprefab --project folder`
launches it. Fixed preview needs no game build; **Views**/`--preview` compiles
opted-in views using the installed native toolchain. Public controls and limits are
in [Editors](../../docs/tools/editors.md#native-scene-editor).

`document.c` owns the draft, source-span edits, undo/redo, conflict checks and
recovery. It shares the compiler's recovering parser/checker and existing folder,
package and filesystem implementations. Checking never runs game code. AST and
token pointers live for one analysis; UI edits apply after frame recording, and
temporary display strings are rewound every frame. The viewport interprets a
limited set of constant TRS expressions and reports unsupported values.

The app uses Tide's existing native window, draw-list and immediate GUI APIs;
engine/platform code and public game callback signatures are unchanged.

`preview.c` builds the applied draft through the private CLI `_editor-preview`
command and loads its module. The compiler selects `[RunInEditor]`/`[EditorOnly]`
views and emits the versioned private `preview_api.h` entry point. Allocators,
world, local state, draw list and GUI stay inside the module; the host copies
prepared commands, forgetting cached resource pointers before unloading.
Ordinary Main/events/systems do not initialize the authored tree. Rebuilds
replace whole preview worlds; failed builds retain visibly stale output.

Tests:

```sh
cmake --build --preset debug --target tide_editor tide_cli tide_editor_tests
ctest --preset debug -R '^tide_editor_' --output-on-failure
```

`tide_editor_document` covers source preservation, typed validation, composition,
package/project discovery, external updates, undo, save and recovery conflicts.
`tide_editor_cli` exercises real CLI dispatch without a GPU. `tide_editor_ui`
drives the native inspector using a standard input script and reads rendered PNG
pixels; its `gpu` label requires a desktop OpenGL context. All tests operate on
private folders in the configured build directory. Set `TIDE_HIDDEN=1` in agent
shells; the app requires `--check`, `--frames`, `--capture` or `--script` there.

Compiler preview selection/startup tests run in both native/web suites. The
installed-toolchain integration also checks real fragment-shader pixels, nested
models, match/local placement, unsaved overrides, undo/redo, switching prefab
roots, toggle/reload and failed builds:

```sh
node tools/tide-editor/tests/preview.mjs <installed-tide> tools/tide-editor/tests/project build/editor-preview-test
```
