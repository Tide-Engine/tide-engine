" Loaded after the real helper output, with isolated user directories.
new generated.tide
call assert_equal('tide', &filetype)
call assert_equal('tide', b:current_syntax)
call assert_equal(1, len(globpath(&runtimepath, 'ftplugin/tide.vim', 0, 1)))
call assert_equal(4, &shiftwidth)
if !empty(v:errors)
  for error in v:errors
    echoerr error
  endfor
  cquit
endif
qa!
