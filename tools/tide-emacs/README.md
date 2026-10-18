# Tide for Emacs

`tide-lang-mode` recognizes `.tide` and `.tprefab` files, with basic syntax
highlighting, C-style indentation and comment editing. It registers `tidels`
with Eglot for completion, diagnostics, navigation, rename and formatting.
Requires Emacs 29.1 or newer. This is separate from the TypeScript `tide` package.

Tide includes this mode. Print configuration with the installed paths:

```sh
tide ide emacs
```

Copy the result into your init file and restart Emacs. It loads the mode and
enables Eglot automatically, using the full path to Tide's bundled `tidels`.
The helper prints configuration without changing files or launching Emacs.
Regenerate it if you move Tide. Enable automatic LSP only for projects you trust.

For a source checkout or manually downloaded mode, add its absolute path:

```elisp
(add-to-list 'load-path "/path/to/tide-engine/tools/tide-emacs")
(require 'tide-lang-mode)
```

Install Tide so `tidels` is on Emacs's `exec-path`, open a source file and run
`M-x eglot`. To choose a server explicitly, set:

```elisp
(setq tide-lang-server-command '("/path/to/tide/bin/tidels"))
```

Optional automatic connection:

```elisp
(add-hook 'tide-lang-mode-hook #'eglot-ensure)
```

Use `M-x eglot-format-buffer`, `M-x eglot-rename`, `M-.` for definitions,
`M-?` for references and `C-M-i` for completion. Which other server features
appear depends on Eglot. The mode does not launch games or embed the prefab
editor; use `tide run` or `tide open` in a terminal.
`tide editors` installs the VS Code extension; `tide ide` prints setup for
terminal editors.

From the repository root, run the batch checks without loading personal config:

```sh
emacs -Q --batch -L tools/tide-emacs -l tools/tide-emacs/test/tide-lang-mode-test.el -f ert-run-tests-batch-and-exit
```

Set the `TIDELS` environment variable to an absolute path to a freshly built
server to include the real Eglot connection, hover and shutdown test. Without
it, that integration test is skipped.
