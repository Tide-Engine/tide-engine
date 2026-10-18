# Tide for Emacs

`tide-lang-mode` recognizes `.tide` and `.tprefab` files, with basic syntax
highlighting, C-style indentation and comment editing. It registers `tidels`
with Eglot for completion, diagnostics, navigation, rename and formatting.
Requires Emacs 29.1 or newer. This is separate from the TypeScript `tide` package.

## Install with Emacs's package manager

Install [Tide](https://tide-engine.dev/) separately for `tidels`, and put its
`bin` folder on Emacs's `exec-path`. Emacs 29.1+ can install this package
directly from the official repository. Evaluate this once:

```elisp
(require 'package-vc)
(package-vc-install
 '(tide-lang-mode :url "https://github.com/Tide-Engine/tide-emacs"))
```

Then add this to your init file:

```elisp
(require 'tide-lang-mode)
(add-hook 'tide-lang-mode-hook #'eglot-ensure)
```

The hook automatically connects to `tidels` when opening Tide files. Omit it
to connect manually with `M-x eglot`. Use `M-x package-vc-upgrade` for updates.
MELPA inclusion is being prepared; the package is not listed there yet.

## Use the mode bundled with Tide

This is an alternative to package-manager installation. Choose one copy of
the mode so your editor does not load competing installations.

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

## Development

This repository is published from `tools/tide-emacs` in Tide's engine
repository. Please report issues or propose changes in this public repository;
maintainers apply accepted changes to the source before publishing an update.

From this standalone repository's root, run the batch checks without loading
personal config:

```sh
emacs -Q --batch -L . -l test/tide-lang-mode-test.el -f ert-run-tests-batch-and-exit
```

In the engine checkout, use `-L tools/tide-emacs` and
`-l tools/tide-emacs/test/tide-lang-mode-test.el` instead.

Set the `TIDELS` environment variable to an absolute path to a freshly built
server to include the real Eglot connection, hover and shutdown test. Without
it, that integration test is skipped.
