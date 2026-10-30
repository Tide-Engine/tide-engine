	.file	"mask.c"
	.functype	sparse_one (i32) -> (i32)
	.functype	sparse_two (i32, i32) -> (i32)
	.functype	sparse_portable (i32) -> (i32)
	.functype	count_word (i32) -> (i32)
	.section	.text.sparse_one,"",@
	.hidden	sparse_one                      # -- Begin function sparse_one
	.globl	sparse_one
	.type	sparse_one,@function
sparse_one:                             # @sparse_one
	.functype	sparse_one (i32) -> (i32)
# %bb.0:
	local.get	0
	v128.load	0:p2align=0
	v128.const	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	i8x16.ne
	i8x16.bitmask
                                        # fallthrough-return
	end_function
                                        # -- End function
	.section	.text.sparse_two,"",@
	.hidden	sparse_two                      # -- Begin function sparse_two
	.globl	sparse_two
	.type	sparse_two,@function
sparse_two:                             # @sparse_two
	.functype	sparse_two (i32, i32) -> (i32)
	.local  	v128
# %bb.0:
	local.get	0
	v128.load	0:p2align=0
	v128.const	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	local.tee	2
	i8x16.ne
	local.get	1
	v128.load	0:p2align=0
	local.get	2
	i8x16.eq
	v128.and
	i8x16.bitmask
                                        # fallthrough-return
	end_function
                                        # -- End function
	.section	.text.sparse_portable,"",@
	.hidden	sparse_portable                 # -- Begin function sparse_portable
	.globl	sparse_portable
	.type	sparse_portable,@function
sparse_portable:                        # @sparse_portable
	.functype	sparse_portable (i32) -> (i32)
	.local  	v128, v128
# %bb.0:
	v128.const	0, 0, 0, 0, 0, 0, 0, 0
	local.tee	1
	v128.const	1, 2, 4, 8, 16, 32, 64, 128
	local.get	0
	v128.load	0:p2align=0
	v128.const	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	i8x16.eq
	local.tee	2
	i16x8.extend_low_i8x16_s
	v128.bitselect
	local.get	1
	v128.const	256, 512, 1024, 2048, 4096, 8192, 16384, 32768
	local.get	2
	i16x8.extend_high_i8x16_s
	v128.bitselect
	v128.or
	local.tee	1
	local.get	1
	local.get	1
	i8x16.shuffle	8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 0, 1, 0, 1, 0, 1
	v128.or
	local.tee	1
	local.get	1
	local.get	1
	i8x16.shuffle	4, 5, 6, 7, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1
	v128.or
	local.tee	1
	local.get	1
	local.get	1
	i8x16.shuffle	2, 3, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1
	v128.or
	i16x8.extract_lane_u	0
                                        # fallthrough-return
	end_function
                                        # -- End function
	.section	.text.count_word,"",@
	.hidden	count_word                      # -- Begin function count_word
	.globl	count_word
	.type	count_word,@function
count_word:                             # @count_word
	.functype	count_word (i32) -> (i32)
# %bb.0:
	local.get	0
	i32.popcnt
                                        # fallthrough-return
	end_function
                                        # -- End function
	.ident	"clang version 23.1.2 (https://github.com/llvm/llvm-project 85ac560262434c9ccfc0c183ec22d4138ed647fb)"
	.section	.custom_section.producers,"",@
	.int8	1
	.int8	12
	.ascii	"processed-by"
	.int8	1
	.int8	5
	.ascii	"clang"
	.int8	86
	.ascii	"23.1.2 (https://github.com/llvm/llvm-project 85ac560262434c9ccfc0c183ec22d4138ed647fb)"
	.section	.text.count_word,"",@
	.section	.custom_section.target_features,"",@
	.int8	9
	.int8	43
	.int8	11
	.ascii	"bulk-memory"
	.int8	43
	.int8	15
	.ascii	"bulk-memory-opt"
	.int8	43
	.int8	22
	.ascii	"call-indirect-overlong"
	.int8	43
	.int8	10
	.ascii	"multivalue"
	.int8	43
	.int8	15
	.ascii	"mutable-globals"
	.int8	43
	.int8	19
	.ascii	"nontrapping-fptoint"
	.int8	43
	.int8	15
	.ascii	"reference-types"
	.int8	43
	.int8	8
	.ascii	"sign-ext"
	.int8	43
	.int8	7
	.ascii	"simd128"
	.section	.text.count_word,"",@
