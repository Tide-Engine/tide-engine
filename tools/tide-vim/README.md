# Tide for Vim and Neovim

This runtime detects `.tide` and `.tprefab`, adds basic syntax highlighting,
C-style indentation and comment settings. Vim 8+ and Neovim can use it without
any other plugin. Interpolated strings have string highlighting; expressions
inside them do not have separate lexical scopes.

Download this folder from the Tide repository or use your existing checkout.
Add its absolute path **before** enabling filetypes in your vimrc:

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
Installation is manual; `tide editors` currently installs only the VS Code
extension.

Run from the repository root without loading personal config:

```sh
vim -Nu NONE -n -es -S tools/tide-vim/test/editor.vim
nvim --headless -u NONE -S tools/tide-vim/test/editor.vim
nvim --headless -u NONE -l tools/tide-vim/test/lsp.lua
```

Set the `TIDELS` environment variable to an absolute path to a freshly built
server to include real Neovim attachment, hover and shutdown in the Lua test.
