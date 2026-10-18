-- Loaded after the real helper output, including its path with a comma.
local folder = vim.fn.tempname() .. ' generated ide'
vim.fn.mkdir(folder, 'p')
vim.fn.writefile({}, folder .. '/tide.packages')
vim.fn.writefile({ 'component Health { int value; }' }, folder .. '/game.tide')
local client
local ok, err = pcall(function()
  vim.cmd.edit(vim.fn.fnameescape(folder .. '/game.tide'))
  assert(vim.bo.filetype == 'tide', 'generated config did not detect Tide')
  local buf = vim.api.nvim_get_current_buf()
  assert(vim.wait(10000, function()
    client = vim.lsp.get_clients({ name = 'tidels', bufnr = buf })[1]
    return client and client.initialized
  end), 'generated config did not start tidels')
  local reply = client:request_sync('textDocument/hover', {
    textDocument = { uri = vim.uri_from_bufnr(buf) },
    position = { line = 0, character = 12 },
  }, 10000, buf)
  assert(reply and reply.result.contents.value:find('Health'))
end)
vim.lsp.enable('tidels', false)
if client then assert(vim.wait(10000, function() return client:is_stopped() end)) end
vim.fn.delete(folder, 'rf')
assert(ok, err)
