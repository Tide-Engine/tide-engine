-- Neovim 0.11+: opt in with vim.lsp.enable('tidels').
return {
  cmd = { 'tidels' },
  filetypes = { 'tide' },
  root_dir = function(bufnr, on_dir)
    on_dir(vim.fs.root(bufnr, { 'tide.packages' })
      or vim.fs.root(bufnr, { 'CMakeLists.txt', '.git' })
      or vim.fs.dirname(vim.api.nvim_buf_get_name(bufnr)))
  end,
}
