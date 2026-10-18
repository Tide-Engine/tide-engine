# Tide for Emacs

`tide-lang-mode` recognizes `.tide` and `.tprefab` files, with basic syntax
highlighting, C-style indentation and comment editing. It registers `tidels`
with Eglot for completion, diagnostics, navigation, rename and formatting.
Requires Emacs 29.1 or newer. This is separate from the TypeScript `tide` package.

Download this folder from the Tide repository or use your existing checkout.
Add its absolute path to your init file:

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
editor; use `tide run` or `tide open` in a terminal. Installation is manual;
`tide editors` currently installs only the VS Code extension.

From the repository root, run the batch checks without loading personal config:

```sh
emacs -Q --batch -L tools/tide-emacs -l tools/tide-emacs/test/tide-lang-mode-test.el -f ert-run-tests-batch-and-exit
```

Set the `TIDELS` environment variable to an absolute path to a freshly built
server to include the real Eglot connection, hover and shutdown test. Without
it, that integration test is skipped.
