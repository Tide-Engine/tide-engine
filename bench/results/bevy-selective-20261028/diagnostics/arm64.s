	.file	"mask.c"
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function sparse_one
.LCPI0_0:
	.hword	256                             // 0x100
	.hword	512                             // 0x200
	.hword	1024                            // 0x400
	.hword	2048                            // 0x800
	.hword	4096                            // 0x1000
	.hword	8192                            // 0x2000
	.hword	16384                           // 0x4000
	.hword	32768                           // 0x8000
.LCPI0_1:
	.hword	1                               // 0x1
	.hword	2                               // 0x2
	.hword	4                               // 0x4
	.hword	8                               // 0x8
	.hword	16                              // 0x10
	.hword	32                              // 0x20
	.hword	64                              // 0x40
	.hword	128                             // 0x80
	.text
	.globl	sparse_one
	.p2align	2
	.type	sparse_one,@function
sparse_one:                             // @sparse_one
// %bb.0:
	ldr	q0, [x0]
	adrp	x8, .LCPI0_0
	adrp	x9, .LCPI0_1
	ldr	q2, [x8, :lo12:.LCPI0_0]
	ldr	q3, [x9, :lo12:.LCPI0_1]
	cmeq	v0.16b, v0.16b, #0
	ushll	v1.8h, v0.8b, #0
	sshll2	v0.8h, v0.16b, #0
	bic	v0.16b, v2.16b, v0.16b
	bic	v1.16b, v3.16b, v1.16b
	orr	v0.16b, v1.16b, v0.16b
	mov	d1, v0.d[1]
	orr	v0.8b, v0.8b, v1.8b
	fmov	x8, d0
	lsr	x9, x8, #32
	orr	w8, w8, w9
	orr	w8, w8, w8, lsr #16
	and	w0, w8, #0xffff
	ret
.Lfunc_end0:
	.size	sparse_one, .Lfunc_end0-sparse_one
                                        // -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function sparse_two
.LCPI1_0:
	.hword	256                             // 0x100
	.hword	512                             // 0x200
	.hword	1024                            // 0x400
	.hword	2048                            // 0x800
	.hword	4096                            // 0x1000
	.hword	8192                            // 0x2000
	.hword	16384                           // 0x4000
	.hword	32768                           // 0x8000
.LCPI1_1:
	.hword	1                               // 0x1
	.hword	2                               // 0x2
	.hword	4                               // 0x4
	.hword	8                               // 0x8
	.hword	16                              // 0x10
	.hword	32                              // 0x20
	.hword	64                              // 0x40
	.hword	128                             // 0x80
	.text
	.globl	sparse_two
	.p2align	2
	.type	sparse_two,@function
sparse_two:                             // @sparse_two
// %bb.0:
	ldr	q0, [x0]
	ldr	q1, [x1]
	adrp	x8, .LCPI1_0
	adrp	x9, .LCPI1_1
	ldr	q2, [x8, :lo12:.LCPI1_0]
	cmeq	v0.16b, v0.16b, #0
	cmeq	v1.16b, v1.16b, #0
	ldr	q3, [x9, :lo12:.LCPI1_1]
	bic	v0.16b, v1.16b, v0.16b
	ushll	v1.8h, v0.8b, #0
	sshll2	v0.8h, v0.16b, #0
	and	v0.16b, v0.16b, v2.16b
	and	v1.16b, v1.16b, v3.16b
	orr	v0.16b, v1.16b, v0.16b
	mov	d1, v0.d[1]
	orr	v0.8b, v0.8b, v1.8b
	fmov	x8, d0
	lsr	x9, x8, #32
	orr	w8, w8, w9
	orr	w8, w8, w8, lsr #16
	and	w0, w8, #0xffff
	ret
.Lfunc_end1:
	.size	sparse_two, .Lfunc_end1-sparse_two
                                        // -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function sparse_portable
.LCPI2_0:
	.hword	256                             // 0x100
	.hword	512                             // 0x200
	.hword	1024                            // 0x400
	.hword	2048                            // 0x800
	.hword	4096                            // 0x1000
	.hword	8192                            // 0x2000
	.hword	16384                           // 0x4000
	.hword	32768                           // 0x8000
.LCPI2_1:
	.hword	1                               // 0x1
	.hword	2                               // 0x2
	.hword	4                               // 0x4
	.hword	8                               // 0x8
	.hword	16                              // 0x10
	.hword	32                              // 0x20
	.hword	64                              // 0x40
	.hword	128                             // 0x80
	.text
	.globl	sparse_portable
	.p2align	2
	.type	sparse_portable,@function
sparse_portable:                        // @sparse_portable
// %bb.0:
	ldr	q0, [x0]
	adrp	x8, .LCPI2_0
	adrp	x9, .LCPI2_1
	ldr	q2, [x8, :lo12:.LCPI2_0]
	ldr	q3, [x9, :lo12:.LCPI2_1]
	cmeq	v0.16b, v0.16b, #0
	ushll	v1.8h, v0.8b, #0
	sshll2	v0.8h, v0.16b, #0
	bic	v0.16b, v2.16b, v0.16b
	bic	v1.16b, v3.16b, v1.16b
	orr	v0.16b, v1.16b, v0.16b
	mov	d1, v0.d[1]
	orr	v0.8b, v0.8b, v1.8b
	fmov	x8, d0
	lsr	x9, x8, #32
	orr	w8, w8, w9
	orr	w8, w8, w8, lsr #16
	and	w0, w8, #0xffff
	ret
.Lfunc_end2:
	.size	sparse_portable, .Lfunc_end2-sparse_portable
                                        // -- End function
	.globl	count_word                      // -- Begin function count_word
	.p2align	2
	.type	count_word,@function
count_word:                             // @count_word
// %bb.0:
	fmov	s0, w0
	cnt	v0.8b, v0.8b
	addv	b0, v0.8b
	fmov	w0, s0
	ret
.Lfunc_end3:
	.size	count_word, .Lfunc_end3-count_word
                                        // -- End function
	.ident	"clang version 23.1.2 (https://github.com/llvm/llvm-project 85ac560262434c9ccfc0c183ec22d4138ed647fb)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
