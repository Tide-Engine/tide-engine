if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

setlocal cindent expandtab shiftwidth=4 softtabstop=4
setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal suffixesadd=.tide,.tprefab

let b:undo_ftplugin = 'setlocal cindent< expandtab< shiftwidth< softtabstop< commentstring< comments< suffixesadd<'
