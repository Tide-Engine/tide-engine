# Native scene editor

Build `tide_editor` and `tide_cli` in a native platform-enabled build. The package
installs `tide-editor` beside `tide`; `tide open file.tprefab --project folder`
launches it. No browser or game build is needed. Public controls and limits are
in [Editors](../../docs/tools/editors.md#native-scene-editor).

`document.c` owns the draft, source-span edits, undo/redo, conflict checks and
recovery. It shares the compiler's recovering parser/checker and existing folder,
package and filesystem implementations. Checking never runs game code. AST and
token pointers live for one analysis; UI edits apply after frame recording, and
temporary display strings are rewound every frame. The viewport interprets a
limited set of constant TRS expressions and reports unsupported values.

The app uses Tide's existing native window, draw-list and immediate GUI APIs;
engine/platform code and generated runtime interfaces are unchanged.

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
