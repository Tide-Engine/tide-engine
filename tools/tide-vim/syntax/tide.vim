if exists('b:current_syntax')
  finish
endif

syntax case match
syntax keyword tideKeyword component singleton system view input event command
syntax keyword tideKeyword scene prefab entity struct enum namespace using extern
syntax keyword tideKeyword const asset frag vert async worker virtual override local verified
syntax keyword tideKeyword mut with without out in var fails is this
syntax keyword tideConditional if else switch case default
syntax keyword tideRepeat while for foreach parallel
syntax keyword tideStatement return break continue fail try await
syntax keyword tideType void bool byte ushort char int int2 int3 int4 long
syntax keyword tideType double double2 double3 double4 double2x2 double3x3 double4x4
syntax keyword tideType float float2 float3 float4 float2x2 float3x3 float4x4 quaternion string
syntax keyword tideType Entity LocalEntity PlayerID Color Rect World List Map Set Grid2 Grid3 Asset Task LocalTask
syntax keyword tideBoolean true false null
syntax match tideType '\<[A-Z][A-Za-z0-9_]*\>'
syntax match tideDirective '^\s*#\s*\(if\|elif\|else\|endif\|define\|undef\)\>'
syntax match tideNumber '\<0[xX][0-9A-Fa-f_]\+\>'
syntax match tideNumber '\<0[bB][01_]\+\>'
syntax match tideNumber '\<\d[0-9_]*\(\.[0-9_]\+\)\?\([eE][+-]\?[0-9_]\+\)\?\(deg\|[fFdD]\)\?\>'
syntax match tideNumber '\%([[:alnum:]_.)\]]\)\@<!\.[0-9_]\+\([eE][+-]\?[0-9_]\+\)\?\(deg\|[fFdD]\)\?\>'
syntax match tideEscape '\\["\\n]' contained
syntax region tideString start=+\$\?"+ skip=+\\.+ end=+"+ end=+$+ contains=tideEscape
syntax match tideCharacter "'\([^'\\]\|\\.\)'"
syntax keyword tideTodo TODO FIXME XXX contained
syntax region tideComment start='/\*' end='\*/' contains=tideTodo
syntax match tideComment '//.*$' contains=tideTodo
syntax sync ccomment tideComment minlines=50

highlight default link tideKeyword Keyword
highlight default link tideConditional Conditional
highlight default link tideRepeat Repeat
highlight default link tideStatement Statement
highlight default link tideType Type
highlight default link tideBoolean Boolean
highlight default link tideDirective PreProc
highlight default link tideNumber Number
highlight default link tideString String
highlight default link tideCharacter Character
highlight default link tideEscape SpecialChar
highlight default link tideComment Comment
highlight default link tideTodo Todo

let b:current_syntax = 'tide'
