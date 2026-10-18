# Tide for Vim and Neovim

Released under the [MIT license](LICENSE).

This runtime detects `.tide` and `.tprefab`, adds basic syntax highlighting,
C-style indentation and comment settings. Vim 8+ and Neovim can use it without
any other plugin. Interpolated strings have string highlighting; expressions
inside them do not have separate lexical scopes.

## Install with a plugin manager

Install [Tide](https://tide-engine.dev/) separately for `tidels`, and put its
`bin` folder on `PATH`. The official repository works directly with Git-based
plugin managers; no registry registration is needed.

With vim-plug, add this between your existing `plug#begin()` and `plug#end()`,
then run `:PlugInstall`:

```vim
Plug 'Tide-Engine/tide-vim'
```

With lazy.nvim on Neovim 0.11+, add this plugin specification:

```lua
{
  'Tide-Engine/tide-vim',
  lazy = false, -- Load file detection before opening a Tide buffer.
  config = function()
    vim.lsp.enable('tidels')
  end,
}
```

Use your plugin manager's update command for updates. Plain Vim needs a
separate LSP client for completion and diagnostics; highlighting and
indentation work without one. Server selection and controls are below.

## Use the runtime bundled with Tide

This is an alternative to plugin-manager installation. Choose one copy of
the runtime so your editor does not load competing installations.

Tide includes this runtime. Print configuration with the installed paths:

```sh
tide ide vim
tide ide neovim
```

Copy the appropriate result into your vimrc or Neovim init.lua and restart the
editor. The Neovim snippet enables LSP using the bundled `tidels` by full path.
Vim's snippet supplies highlighting and names the server for your own client.
The helper changes no files and launches no editor. Regenerate the configuration
if you move Tide. Enable automatic LSP only for projects you trust.

For a source checkout or manually downloaded runtime, add its absolute path
**before** enabling filetypes in your vimrc:

```vim
set runtimepath^=/path/to/tide-engine/tools/tide-vim
filetype plugin indent on
syntax enable
```

Escape spaces with `\ ` in Vim's `set` command. In Neovim's `init.lua`, use:

```lua
vim.opt.runtimepath:prepend('/path/to/tide-engine/tools/tide-vim')
vim.cmd('filetype plugin indent on')
vim.cmd('syntax enable')
vim.lsp.enable('tidels') -- Neovim 0.11 or newer
```

Install Tide so `tidels` is on `PATH`. The supplied Neovim LSP config uses
the nearest `tide.packages`, then CMake/Git root, then the source file's folder.
Enable it only for projects you trust. To choose another server, before enabling:

```lua
vim.lsp.config('tidels', { cmd = { '/path/to/tide/bin/tidels' } })
```

Neovim supplies hover (`K`), definitions (`:lua vim.lsp.buf.definition()`),
rename (`:lua vim.lsp.buf.rename()`), diagnostics and formatting
(`:lua vim.lsp.buf.format()`). Use `:help lsp` to configure completion and
other client features. Plain Vim needs an LSP client of your choice: register
the `tide` filetype and the command `['tidels']`, using stdio. This runtime does
not choose or install a Vim LSP client. Server features depend on that client;
VS Code-specific clickable code-lens commands are not adapted here.

Use `tide run` and `tide open` in a terminal for games and prefab editing.
`tide editors` installs the VS Code extension; `tide ide` prints setup for
terminal editors.

## Development

This repository is published from `tools/tide-vim` in Tide's engine
repository. Please report issues or propose changes in this public repository;
maintainers apply accepted changes to the source before publishing an update.

Run from this standalone repository's root without loading personal config:

```sh
vim -Nu NONE -n -es -S test/editor.vim
nvim --headless -u NONE -S test/editor.vim
nvim --headless -u NONE -l test/lsp.lua
```

In the engine checkout, prefix these test paths with `tools/tide-vim/`.

Set the `TIDELS` environment variable to an absolute path to a freshly built
server to include real Neovim attachment, hover and shutdown in the Lua test.
