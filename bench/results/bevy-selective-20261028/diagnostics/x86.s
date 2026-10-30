	.att_syntax
	.file	"mask.c"
	.text
	.globl	sparse_one                      # -- Begin function sparse_one
	.prefalign	4, .Lfunc_end0, nop
	.type	sparse_one,@function
sparse_one:                             # @sparse_one
# %bb.0:
	movdqu	(%rdi), %xmm0
	pxor	%xmm1, %xmm1
	pcmpeqb	%xmm0, %xmm1
	pmovmskb	%xmm1, %eax
	xorl	$65535, %eax                    # imm = 0xFFFF
	retq
.Lfunc_end0:
	.size	sparse_one, .Lfunc_end0-sparse_one
                                        # -- End function
	.globl	sparse_two                      # -- Begin function sparse_two
	.prefalign	4, .Lfunc_end1, nop
	.type	sparse_two,@function
sparse_two:                             # @sparse_two
# %bb.0:
	movdqu	(%rdi), %xmm0
	movdqu	(%rsi), %xmm1
	pxor	%xmm2, %xmm2
	pcmpeqb	%xmm2, %xmm0
	pcmpeqb	%xmm2, %xmm1
	pandn	%xmm1, %xmm0
	pmovmskb	%xmm0, %eax
	retq
.Lfunc_end1:
	.size	sparse_two, .Lfunc_end1-sparse_two
                                        # -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          # -- Begin function sparse_portable
.LCPI2_0:
	.short	256                             # 0x100
	.short	512                             # 0x200
	.short	1024                            # 0x400
	.short	2048                            # 0x800
	.short	4096                            # 0x1000
	.short	8192                            # 0x2000
	.short	16384                           # 0x4000
	.short	32768                           # 0x8000
.LCPI2_1:
	.short	1                               # 0x1
	.short	2                               # 0x2
	.short	4                               # 0x4
	.short	8                               # 0x8
	.short	16                              # 0x10
	.short	32                              # 0x20
	.short	64                              # 0x40
	.short	128                             # 0x80
	.text
	.globl	sparse_portable
	.prefalign	4, .Lfunc_end2, nop
	.type	sparse_portable,@function
sparse_portable:                        # @sparse_portable
# %bb.0:
	movdqu	(%rdi), %xmm0
	pxor	%xmm1, %xmm1
	pcmpeqb	%xmm0, %xmm1
	movdqa	%xmm1, %xmm0
	punpcklbw	%xmm1, %xmm0            # xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
	punpckhbw	%xmm1, %xmm1            # xmm1 = xmm1[8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15]
	pandn	.LCPI2_0(%rip), %xmm1
	pandn	.LCPI2_1(%rip), %xmm0
	por	%xmm1, %xmm0
	pshufd	$238, %xmm0, %xmm1              # xmm1 = xmm0[2,3,2,3]
	por	%xmm0, %xmm1
	pshufd	$85, %xmm1, %xmm0               # xmm0 = xmm1[1,1,1,1]
	por	%xmm1, %xmm0
	movdqa	%xmm0, %xmm1
	psrld	$16, %xmm1
	por	%xmm0, %xmm1
	pextrw	$0, %xmm1, %eax
	retq
.Lfunc_end2:
	.size	sparse_portable, .Lfunc_end2-sparse_portable
                                        # -- End function
	.globl	count_word                      # -- Begin function count_word
	.prefalign	4, .Lfunc_end3, nop
	.type	count_word,@function
count_word:                             # @count_word
# %bb.0:
	movl	%edi, %eax
	shrl	%eax
	andl	$1431655765, %eax               # imm = 0x55555555
	subl	%eax, %edi
	movl	%edi, %eax
	andl	$858993459, %eax                # imm = 0x33333333
	shrl	$2, %edi
	andl	$858993459, %edi                # imm = 0x33333333
	addl	%eax, %edi
	movl	%edi, %eax
	shrl	$4, %eax
	addl	%edi, %eax
	andl	$252645135, %eax                # imm = 0xF0F0F0F
	imull	$16843009, %eax, %eax           # imm = 0x1010101
	shrl	$24, %eax
	retq
.Lfunc_end3:
	.size	count_word, .Lfunc_end3-count_word
                                        # -- End function
	.ident	"clang version 23.1.2 (https://github.com/llvm/llvm-project 85ac560262434c9ccfc0c183ec22d4138ed647fb)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
