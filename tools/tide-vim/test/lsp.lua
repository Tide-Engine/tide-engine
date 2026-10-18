-- Run with nvim --headless -u NONE -l this-file from either repository.
local runtime = vim.fs.dirname(vim.fs.dirname(debug.getinfo(1, 'S').source:sub(2)))
vim.opt.runtimepath:prepend(vim.fn.escape(runtime, ','))
local config = vim.lsp.config.tidels
assert(vim.deep_equal(config.cmd, { 'tidels' }))
assert(vim.deep_equal(config.filetypes, { 'tide' }))

local folder = vim.fn.tempname() .. ' tide project'
vim.fn.mkdir(folder .. '/nested', 'p')
local function root_for(path)
  local buf = vim.fn.bufadd(path)
  local root
  config.root_dir(buf, function(dir) root = dir end)
  vim.api.nvim_buf_delete(buf, { force = true })
  return vim.fs.normalize(root)
end
local ok, err = pcall(function()
  assert(root_for(folder .. '/nested/game.tide') == vim.fs.normalize(folder .. '/nested'))
  vim.fn.writefile({}, folder .. '/tide.packages')
  assert(root_for(folder .. '/nested/Level.tprefab') == vim.fs.normalize(folder))
  vim.fn.delete(folder .. '/tide.packages')
  vim.fn.writefile({}, folder .. '/CMakeLists.txt')
  assert(root_for(folder .. '/nested/game.tide') == vim.fs.normalize(folder))

  if vim.env.TIDELS then
    vim.fn.writefile({ 'component Health { int value; }' }, folder .. '/game.tide')
    vim.cmd('filetype plugin indent on')
    vim.lsp.config('tidels', { cmd = { vim.env.TIDELS } })
    vim.lsp.enable('tidels')
    vim.cmd.edit(vim.fn.fnameescape(folder .. '/game.tide'))
    local buf = vim.api.nvim_get_current_buf()
    assert(vim.wait(10000, function()
      local clients = vim.lsp.get_clients({ bufnr = buf, name = 'tidels' })
      return clients[1] and clients[1].initialized
    end), 'tidels did not attach')
    local client = vim.lsp.get_clients({ bufnr = buf, name = 'tidels' })[1]
    local reply = client:request_sync('textDocument/hover', {
      textDocument = { uri = vim.uri_from_bufnr(buf) },
      position = { line = 0, character = 12 },
    }, 10000, buf)
    assert(reply and reply.result and reply.result.contents.value:find('Health'))
    vim.lsp.enable('tidels', false)
    assert(vim.wait(10000, function() return client:is_stopped() end), 'tidels did not stop')
    vim.api.nvim_buf_delete(buf, { force = true })
  end
end)
vim.fn.delete(folder, 'rf')
assert(ok, err)
print('Tide Neovim LSP configuration passed')
