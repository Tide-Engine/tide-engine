
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-20261029/axis16/wide.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <tide_rows_Shift_6>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 50                           	pushq	%rax
       d: 44 39 c2                     	cmpl	%r8d, %edx
      10: 0f 83 ef 01 00 00            	jae	0x205 <tide_rows_Shift_6+0x205>
      16: 44 89 c6                     	movl	%r8d, %esi
      19: 4c 8b 9c 24 b8 00 00 00      	movq	0xb8(%rsp), %r11
      21: 4c 8b 84 24 a0 00 00 00      	movq	0xa0(%rsp), %r8
      29: 4c 8b b4 24 98 00 00 00      	movq	0x98(%rsp), %r14
      31: 4c 8b bc 24 90 00 00 00      	movq	0x90(%rsp), %r15
      39: 4c 8b a4 24 88 00 00 00      	movq	0x88(%rsp), %r12
      41: 4c 8b ac 24 80 00 00 00      	movq	0x80(%rsp), %r13
      49: 48 8b 6c 24 78               	movq	0x78(%rsp), %rbp
      4e: 4c 8b 54 24 70               	movq	0x70(%rsp), %r10
      53: f3 0f 10 01                  	movss	(%rcx), %xmm0
      57: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x5f <tide_rows_Shift_6+0x5f>
      5f: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x66 <tide_rows_Shift_6+0x66>
      66: 89 34 24                     	movl	%esi, (%rsp)
      69: eb 0d                        	jmp	0x78 <tide_rows_Shift_6+0x78>
      6b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
      70: 39 f2                        	cmpl	%esi, %edx
      72: 0f 83 8d 01 00 00            	jae	0x205 <tide_rows_Shift_6+0x205>
      78: 89 d0                        	movl	%edx, %eax
      7a: 89 f1                        	movl	%esi, %ecx
      7c: 29 d1                        	subl	%edx, %ecx
      7e: 8d 50 10                     	leal	0x10(%rax), %edx
      81: 83 f9 10                     	cmpl	$0x10, %ecx
      84: 89 d1                        	movl	%edx, %ecx
      86: 0f 42 ce                     	cmovbl	%esi, %ecx
      89: 39 c8                        	cmpl	%ecx, %eax
      8b: 73 e3                        	jae	0x70 <tide_rows_Shift_6+0x70>
      8d: 89 54 24 04                  	movl	%edx, 0x4(%rsp)
      91: 89 c0                        	movl	%eax, %eax
      93: 89 cf                        	movl	%ecx, %edi
      95: 48 89 c1                     	movq	%rax, %rcx
      98: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
      a0: 41 0f b7 54 49 0a            	movzwl	0xa(%r9,%rcx,2), %edx
      a6: c1 e2 02                     	shll	$0x2, %edx
      a9: f3 41 0f 10 1c 10            	movss	(%r8,%rdx), %xmm3
      af: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
      b3: f3 41 0f 58 1c 14            	addss	(%r12,%rdx), %xmm3
      b9: f3 41 0f 10 24 13            	movss	(%r11,%rdx), %xmm4
      bf: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
      c3: 0f 28 e9                     	movaps	%xmm1, %xmm5
      c6: f3 0f 5c ec                  	subss	%xmm4, %xmm5
      ca: f3 0f 59 eb                  	mulss	%xmm3, %xmm5
      ce: 0f 28 d8                     	movaps	%xmm0, %xmm3
      d1: f3 0f 59 dd                  	mulss	%xmm5, %xmm3
      d5: f3 41 0f 58 1c 12            	addss	(%r10,%rdx), %xmm3
      db: f3 41 0f 11 1c 12            	movss	%xmm3, (%r10,%rdx)
      e1: f3 41 0f 11 2c 14            	movss	%xmm5, (%r12,%rdx)
      e7: 48 ff c1                     	incq	%rcx
      ea: 48 39 f9                     	cmpq	%rdi, %rcx
      ed: 72 b1                        	jb	0xa0 <tide_rows_Shift_6+0xa0>
      ef: 48 89 c1                     	movq	%rax, %rcx
      f2: eb 2b                        	jmp	0x11f <tide_rows_Shift_6+0x11f>
      f4: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
     100: 8d 14 b5 00 00 00 00         	leal	(,%rsi,4), %edx
     107: f3 0f 11 64 15 00            	movss	%xmm4, (%rbp,%rdx)
     10d: f3 41 0f 11 1c 17            	movss	%xmm3, (%r15,%rdx)
     113: 48 ff c1                     	incq	%rcx
     116: 48 39 f9                     	cmpq	%rdi, %rcx
     119: 0f 83 81 00 00 00            	jae	0x1a0 <tide_rows_Shift_6+0x1a0>
     11f: 41 0f b7 74 49 0a            	movzwl	0xa(%r9,%rcx,2), %esi
     125: 89 f2                        	movl	%esi, %edx
     127: c1 e2 02                     	shll	$0x2, %edx
     12a: 48 8b 9c 24 a8 00 00 00      	movq	0xa8(%rsp), %rbx
     132: f3 0f 10 24 13               	movss	(%rbx,%rdx), %xmm4
     137: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     13b: f3 41 0f 58 24 17            	addss	(%r15,%rdx), %xmm4
     141: f3 41 0f 10 2c 13            	movss	(%r11,%rdx), %xmm5
     147: f3 0f 59 e8                  	mulss	%xmm0, %xmm5
     14b: 0f 28 d9                     	movaps	%xmm1, %xmm3
     14e: f3 0f 5c dd                  	subss	%xmm5, %xmm3
     152: f3 0f 59 dc                  	mulss	%xmm4, %xmm3
     156: 0f 28 e0                     	movaps	%xmm0, %xmm4
     159: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     15d: f3 0f 58 64 15 00            	addss	(%rbp,%rdx), %xmm4
     163: 48 8b 9c 24 c8 00 00 00      	movq	0xc8(%rsp), %rbx
     16b: f3 0f 10 2c 13               	movss	(%rbx,%rdx), %xmm5
     170: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     173: 76 8b                        	jbe	0x100 <tide_rows_Shift_6+0x100>
     175: f3 0f 58 ed                  	addss	%xmm5, %xmm5
     179: f3 0f 5c ec                  	subss	%xmm4, %xmm5
     17d: 0f 57 da                     	xorps	%xmm2, %xmm3
     180: 48 8b 94 24 c0 00 00 00      	movq	0xc0(%rsp), %rdx
     188: f3 0f 59 1c b2               	mulss	(%rdx,%rsi,4), %xmm3
     18d: 0f 28 e5                     	movaps	%xmm5, %xmm4
     190: e9 6b ff ff ff               	jmp	0x100 <tide_rows_Shift_6+0x100>
     195: 66 66 2e 0f 1f 84 00 00 00 00 00     	nopw	%cs:(%rax,%rax)
     1a0: 8b 34 24                     	movl	(%rsp), %esi
     1a3: 8b 54 24 04                  	movl	0x4(%rsp), %edx
     1a7: 48 8b 9c 24 b0 00 00 00      	movq	0xb0(%rsp), %rbx
     1af: 90                           	nop
     1b0: 41 0f b7 4c 41 0a            	movzwl	0xa(%r9,%rax,2), %ecx
     1b6: c1 e1 02                     	shll	$0x2, %ecx
     1b9: f3 0f 10 1c 0b               	movss	(%rbx,%rcx), %xmm3
     1be: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     1c2: f3 41 0f 58 1c 0e            	addss	(%r14,%rcx), %xmm3
     1c8: f3 41 0f 10 24 0b            	movss	(%r11,%rcx), %xmm4
     1ce: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     1d2: 0f 28 e9                     	movaps	%xmm1, %xmm5
     1d5: f3 0f 5c ec                  	subss	%xmm4, %xmm5
     1d9: f3 0f 59 eb                  	mulss	%xmm3, %xmm5
     1dd: 0f 28 d8                     	movaps	%xmm0, %xmm3
     1e0: f3 0f 59 dd                  	mulss	%xmm5, %xmm3
     1e4: f3 41 0f 58 5c 0d 00         	addss	(%r13,%rcx), %xmm3
     1eb: f3 41 0f 11 5c 0d 00         	movss	%xmm3, (%r13,%rcx)
     1f2: f3 41 0f 11 2c 0e            	movss	%xmm5, (%r14,%rcx)
     1f8: 48 ff c0                     	incq	%rax
     1fb: 48 39 f8                     	cmpq	%rdi, %rax
     1fe: 72 b0                        	jb	0x1b0 <tide_rows_Shift_6+0x1b0>
     200: e9 6b fe ff ff               	jmp	0x70 <tide_rows_Shift_6+0x70>
     205: 48 83 c4 08                  	addq	$0x8, %rsp
     209: 5b                           	popq	%rbx
     20a: 5d                           	popq	%rbp
     20b: 5f                           	popq	%rdi
     20c: 5e                           	popq	%rsi
     20d: 41 5c                        	popq	%r12
     20f: 41 5d                        	popq	%r13
     211: 41 5e                        	popq	%r14
     213: 41 5f                        	popq	%r15
     215: c3                           	retq
