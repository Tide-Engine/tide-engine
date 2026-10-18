" Run from the repository root with Vim -Nu NONE -n -es -S this-file.
set nocompatible
set runtimepath^=tools/tide-vim
filetype plugin indent on
syntax enable

function! s:group(line, column)
  return synIDattr(synID(a:line, a:column, 1), 'name')
endfunction

new game.tide
call assert_equal('tide', &filetype)
call assert_equal('// %s', &commentstring)
call assert_equal(4, &shiftwidth)
call assert_equal(1, &expandtab)
call setline(1, ['component Health { int value; }', '// component',
      \ 'string text = "view // text";', '/* system', 'view */',
      \ 'float angle = .5deg;', 'char mark = ''"'';', '#if DEBUG'])
call assert_equal('tideKeyword', s:group(1, 1))
call assert_equal('tideType', s:group(1, 11))
call assert_equal('tideComment', s:group(2, 4))
call assert_equal('tideString', s:group(3, 16))
call assert_equal('tideComment', s:group(5, 1))
call assert_equal('tideNumber', s:group(6, 15))
call assert_equal('tideNumber', s:group(6, 19))
call assert_equal('tideCharacter', s:group(7, 14))
call assert_equal('tideDirective', s:group(8, 1))
%delete _
call setline(1, ['system Move()', '{', 'if (true)', '{', 'Step();', '}', '}'])
normal! gg=G
call assert_equal('        Step();', getline(5))
call assert_equal('}', getline(7))
setfiletype text
setlocal filetype=text
call assert_equal(&g:shiftwidth, &l:shiftwidth)
bwipeout!
new Level.tprefab
call assert_equal('tide', &filetype)
call assert_equal('tide', b:current_syntax)
bwipeout!

if !empty(v:errors)
  for error in v:errors
    echoerr error
  endfor
  cquit
endif
qa!
