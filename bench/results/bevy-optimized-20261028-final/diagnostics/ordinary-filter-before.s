tide_rows_Shift_0:
pushq	%r15
pushq	%r14
pushq	%r13
pushq	%r12
pushq	%rsi
pushq	%rdi
pushq	%rbp
pushq	%rbx
subq	$104, %rsp
movaps	%xmm10, 80(%rsp)
movaps	%xmm9, 64(%rsp)
movaps	%xmm8, 48(%rsp)
movaps	%xmm7, 32(%rsp)
movaps	%xmm6, 16(%rsp)
movq	%rcx, 8(%rsp)
cmpl	%r8d, %edx
jae	.LBB_8
movq	296(%rsp), %rcx
movq	288(%rsp), %r10
movq	280(%rsp), %r11
movq	272(%rsp), %rsi
movq	264(%rsp), %rdi
movq	256(%rsp), %rbx
movq	248(%rsp), %r14
movq	240(%rsp), %r15
movq	232(%rsp), %r12
movq	224(%rsp), %r13
movq	216(%rsp), %rbp
movl	%edx, %edx
movl	%r8d, %eax
addq	%rdx, %r9
leaq	(%rcx,%rdx,4), %rcx
movq	%rcx, (%rsp)
leaq	(%r10,%rdx,4), %r8
leaq	(%r11,%rdx,4), %r10
leaq	(%rsi,%rdx,4), %r11
leaq	(%rdi,%rdx,4), %rsi
leaq	(%rbx,%rdx,4), %rdi
leaq	(%r14,%rdx,4), %rbx
leaq	(%r15,%rdx,4), %r14
leaq	(%r12,%rdx,4), %r15
leaq	(%r13,%rdx,4), %r12
leaq	(,%rdx,4), %r13
addq	%rbp, %r13
movq	208(%rsp), %rcx
leaq	(%rcx,%rdx,4), %rbp
subq	%rdx, %rax
xorl	%edx, %edx
movss	__real@3f800000(%rip), %xmm0
movaps	__xmm@80000000800000008000000080000000(%rip), %xmm1
jmp	.LBB_2
.LBB_5:
movaps	%xmm4, %xmm6
.LBB_6:
movss	(%rdi,%rdx,4), %xmm7
mulss	%xmm2, %xmm7
movss	(%r11,%rdx,4), %xmm8
addss	(%r15,%rdx,4), %xmm7
mulss	%xmm2, %xmm8
addss	(%rbx,%rdx,4), %xmm8
mulss	%xmm9, %xmm7
mulss	%xmm9, %xmm8
movaps	%xmm2, %xmm9
mulss	%xmm7, %xmm9
mulss	%xmm8, %xmm2
addss	(%rbp,%rdx,4), %xmm9
addss	(%r12,%rdx,4), %xmm2
movaps	%xmm3, %xmm10
xorps	%xmm1, %xmm10
mulss	(%r8,%rdx,4), %xmm10
cmpltss	%xmm5, %xmm4
movaps	%xmm4, %xmm5
andnps	%xmm3, %xmm5
andps	%xmm10, %xmm4
orps	%xmm5, %xmm4
movss	%xmm9, (%rbp,%rdx,4)
movss	%xmm6, (%r13,%rdx,4)
movss	%xmm2, (%r12,%rdx,4)
movss	%xmm7, (%r15,%rdx,4)
movss	%xmm4, (%r14,%rdx,4)
movss	%xmm8, (%rbx,%rdx,4)
.LBB_7:
incq	%rdx
cmpq	%rdx, %rax
je	.LBB_8
.LBB_2:
cmpb	$0, (%r9,%rdx)
jne	.LBB_7
movq	8(%rsp), %rcx
movss	(%rcx), %xmm2
movss	(%rsi,%rdx,4), %xmm3
mulss	%xmm2, %xmm3
addss	(%r14,%rdx,4), %xmm3
movq	(%rsp), %rcx
movss	(%rcx,%rdx,4), %xmm5
movss	(%r10,%rdx,4), %xmm4
mulss	%xmm2, %xmm4
movaps	%xmm0, %xmm9
subss	%xmm4, %xmm9
mulss	%xmm9, %xmm3
movaps	%xmm2, %xmm4
mulss	%xmm3, %xmm4
addss	(%r13,%rdx,4), %xmm4
ucomiss	%xmm4, %xmm5
jbe	.LBB_5
movaps	%xmm5, %xmm6
addss	%xmm5, %xmm6
subss	%xmm4, %xmm6
jmp	.LBB_6
.LBB_8:
movaps	16(%rsp), %xmm6
movaps	32(%rsp), %xmm7
movaps	48(%rsp), %xmm8
movaps	64(%rsp), %xmm9
movaps	80(%rsp), %xmm10
addq	$104, %rsp
popq	%rbx
popq	%rbp
popq	%rdi
popq	%rsi
popq	%r12
popq	%r13
popq	%r14
popq	%r15
retq
