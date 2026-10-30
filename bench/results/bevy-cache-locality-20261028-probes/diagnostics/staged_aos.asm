
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/staged_aos/wide.obj:	file format coff-x86-64

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
       c: b8 58 18 00 00               	movl	$0x1858, %eax           # imm = 0x1858
      11: e8 00 00 00 00               	callq	0x16 <tide_rows_Shift_6+0x16>
      16: 48 29 c4                     	subq	%rax, %rsp
      19: 44 0f 29 8c 24 40 18 00 00   	movaps	%xmm9, 0x1840(%rsp)
      22: 44 0f 29 84 24 30 18 00 00   	movaps	%xmm8, 0x1830(%rsp)
      2b: 0f 29 bc 24 20 18 00 00      	movaps	%xmm7, 0x1820(%rsp)
      33: 0f 29 b4 24 10 18 00 00      	movaps	%xmm6, 0x1810(%rsp)
      3b: 45 89 c6                     	movl	%r8d, %r14d
      3e: 41 29 d6                     	subl	%edx, %r14d
      41: 0f 84 76 02 00 00            	je	0x2bd <tide_rows_Shift_6+0x2bd>
      47: 41 89 d3                     	movl	%edx, %r11d
      4a: 48 89 0c 24                  	movq	%rcx, (%rsp)
      4e: 4c 8b ac 24 08 19 00 00      	movq	0x1908(%rsp), %r13
      56: 48 8b ac 24 00 19 00 00      	movq	0x1900(%rsp), %rbp
      5e: 48 8b 84 24 f8 18 00 00      	movq	0x18f8(%rsp), %rax
      66: 4c 8b 84 24 f0 18 00 00      	movq	0x18f0(%rsp), %r8
      6e: 4c 8b a4 24 d8 18 00 00      	movq	0x18d8(%rsp), %r12
      76: 48 8b 9c 24 d0 18 00 00      	movq	0x18d0(%rsp), %rbx
      7e: 48 8b 8c 24 c8 18 00 00      	movq	0x18c8(%rsp), %rcx
      86: 4c 8b bc 24 c0 18 00 00      	movq	0x18c0(%rsp), %r15
      8e: 44 89 f2                     	movl	%r14d, %edx
      91: 48 89 54 24 08               	movq	%rdx, 0x8(%rsp)
      96: 48 c1 e2 04                  	shlq	$0x4, %rdx
      9a: 4c 8d 34 52                  	leaq	(%rdx,%rdx,2), %r14
      9e: 45 31 d2                     	xorl	%r10d, %r10d
      a1: 44 89 da                     	movl	%r11d, %edx
      a4: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
      b0: 89 d6                        	movl	%edx, %esi
      b2: 41 0f b7 34 71               	movzwl	(%r9,%rsi,2), %esi
      b7: c1 e6 02                     	shll	$0x2, %esi
      ba: 41 8b 3c 37                  	movl	(%r15,%rsi), %edi
      be: 42 89 7c 14 10               	movl	%edi, 0x10(%rsp,%r10)
      c3: 8b 3c 31                     	movl	(%rcx,%rsi), %edi
      c6: 42 89 7c 14 14               	movl	%edi, 0x14(%rsp,%r10)
      cb: 8b 3c 33                     	movl	(%rbx,%rsi), %edi
      ce: 42 89 7c 14 18               	movl	%edi, 0x18(%rsp,%r10)
      d3: 41 8b 3c 34                  	movl	(%r12,%rsi), %edi
      d7: 42 89 7c 14 1c               	movl	%edi, 0x1c(%rsp,%r10)
      dc: 48 8b bc 24 e0 18 00 00      	movq	0x18e0(%rsp), %rdi
      e4: 8b 3c 37                     	movl	(%rdi,%rsi), %edi
      e7: 42 89 7c 14 20               	movl	%edi, 0x20(%rsp,%r10)
      ec: 48 8b bc 24 e8 18 00 00      	movq	0x18e8(%rsp), %rdi
      f4: 8b 3c 37                     	movl	(%rdi,%rsi), %edi
      f7: 42 89 7c 14 24               	movl	%edi, 0x24(%rsp,%r10)
      fc: 41 8b 3c 30                  	movl	(%r8,%rsi), %edi
     100: 42 89 7c 14 28               	movl	%edi, 0x28(%rsp,%r10)
     105: 8b 3c 30                     	movl	(%rax,%rsi), %edi
     108: 42 89 7c 14 2c               	movl	%edi, 0x2c(%rsp,%r10)
     10d: 8b 7c 35 00                  	movl	(%rbp,%rsi), %edi
     111: 42 89 7c 14 30               	movl	%edi, 0x30(%rsp,%r10)
     116: 41 8b 7c 35 00               	movl	(%r13,%rsi), %edi
     11b: 42 89 7c 14 34               	movl	%edi, 0x34(%rsp,%r10)
     120: 48 8b bc 24 10 19 00 00      	movq	0x1910(%rsp), %rdi
     128: 8b 3c 37                     	movl	(%rdi,%rsi), %edi
     12b: 42 89 7c 14 38               	movl	%edi, 0x38(%rsp,%r10)
     130: 48 8b bc 24 18 19 00 00      	movq	0x1918(%rsp), %rdi
     138: 8b 34 37                     	movl	(%rdi,%rsi), %esi
     13b: 42 89 74 14 3c               	movl	%esi, 0x3c(%rsp,%r10)
     140: 49 83 c2 30                  	addq	$0x30, %r10
     144: ff c2                        	incl	%edx
     146: 4d 39 d6                     	cmpq	%r10, %r14
     149: 0f 85 61 ff ff ff            	jne	0xb0 <tide_rows_Shift_6+0xb0>
     14f: 48 89 ca                     	movq	%rcx, %rdx
     152: 48 8b 04 24                  	movq	(%rsp), %rax
     156: f3 0f 10 00                  	movss	(%rax), %xmm0
     15a: 0f 28 c8                     	movaps	%xmm0, %xmm1
     15d: 0f c6 c8 00                  	shufps	$0x0, %xmm0, %xmm1      # xmm1 = xmm1[0,0],xmm0[0,0]
     161: 31 c9                        	xorl	%ecx, %ecx
     163: f3 0f 10 15 00 00 00 00      	movss	(%rip), %xmm2           # 0x16b <tide_rows_Shift_6+0x16b>
     16b: 0f 28 1d 00 00 00 00         	movaps	(%rip), %xmm3           # 0x172 <tide_rows_Shift_6+0x172>
     172: 0f 28 25 00 00 00 00         	movaps	(%rip), %xmm4           # 0x179 <tide_rows_Shift_6+0x179>
     179: 4c 8b 94 24 e8 18 00 00      	movq	0x18e8(%rsp), %r10
     181: 48 8b b4 24 e0 18 00 00      	movq	0x18e0(%rsp), %rsi
     189: eb 12                        	jmp	0x19d <tide_rows_Shift_6+0x19d>
     18b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     190: 48 83 c1 30                  	addq	$0x30, %rcx
     194: 49 39 ce                     	cmpq	%rcx, %r14
     197: 0f 84 b7 00 00 00            	je	0x254 <tide_rows_Shift_6+0x254>
     19d: f3 0f 10 74 0c 28            	movss	0x28(%rsp,%rcx), %xmm6
     1a3: f3 0f 59 f0                  	mulss	%xmm0, %xmm6
     1a7: f3 0f 58 74 0c 1c            	addss	0x1c(%rsp,%rcx), %xmm6
     1ad: f3 0f 10 7c 0c 34            	movss	0x34(%rsp,%rcx), %xmm7
     1b3: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     1b7: 0f 28 ea                     	movaps	%xmm2, %xmm5
     1ba: f3 0f 5c ef                  	subss	%xmm7, %xmm5
     1be: f2 0f 10 7c 0c 2c            	movsd	0x2c(%rsp,%rcx), %xmm7
     1c4: 0f 59 f9                     	mulps	%xmm1, %xmm7
     1c7: f2 44 0f 10 44 0c 20         	movsd	0x20(%rsp,%rcx), %xmm8
     1ce: 44 0f 58 c7                  	addps	%xmm7, %xmm8
     1d2: f3 0f 59 f5                  	mulss	%xmm5, %xmm6
     1d6: 0f c6 ed 00                  	shufps	$0x0, %xmm5, %xmm5      # xmm5 = xmm5[0,0,0,0]
     1da: 41 0f 59 e8                  	mulps	%xmm8, %xmm5
     1de: 0f 13 6c 0c 20               	movlps	%xmm5, 0x20(%rsp,%rcx)
     1e3: 44 0f 28 c0                  	movaps	%xmm0, %xmm8
     1e7: f3 44 0f 59 c6               	mulss	%xmm6, %xmm8
     1ec: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     1f0: 44 0f 59 cd                  	mulps	%xmm5, %xmm9
     1f4: f2 0f 10 7c 0c 10            	movsd	0x10(%rsp,%rcx), %xmm7
     1fa: 45 0f 14 c1                  	unpcklps	%xmm9, %xmm8            # xmm8 = xmm8[0],xmm9[0],xmm8[1],xmm9[1]
     1fe: 41 0f c6 f1 d4               	shufps	$0xd4, %xmm9, %xmm6     # xmm6 = xmm6[0,1],xmm9[1,3]
     203: 44 0f c6 c6 24               	shufps	$0x24, %xmm6, %xmm8     # xmm8 = xmm8[0,1],xmm6[2,0]
     208: f3 0f 10 74 0c 18            	movss	0x18(%rsp,%rcx), %xmm6
     20e: 0f c6 f3 30                  	shufps	$0x30, %xmm3, %xmm6     # xmm6 = xmm6[0,0],xmm3[3,0]
     212: 0f c6 fe 84                  	shufps	$0x84, %xmm6, %xmm7     # xmm7 = xmm7[0,1],xmm6[0,2]
     216: 41 0f 58 f8                  	addps	%xmm8, %xmm7
     21a: 0f 11 7c 0c 10               	movups	%xmm7, 0x10(%rsp,%rcx)
     21f: f3 0f 10 74 0c 3c            	movss	0x3c(%rsp,%rcx), %xmm6
     225: 0f c6 ff 55                  	shufps	$0x55, %xmm7, %xmm7     # xmm7 = xmm7[1,1,1,1]
     229: 0f 2e f7                     	ucomiss	%xmm7, %xmm6
     22c: 0f 86 5e ff ff ff            	jbe	0x190 <tide_rows_Shift_6+0x190>
     232: f3 0f 58 f6                  	addss	%xmm6, %xmm6
     236: f3 0f 5c f7                  	subss	%xmm7, %xmm6
     23a: f3 0f 11 74 0c 14            	movss	%xmm6, 0x14(%rsp,%rcx)
     240: 0f 57 ec                     	xorps	%xmm4, %xmm5
     243: f3 0f 59 6c 0c 38            	mulss	0x38(%rsp,%rcx), %xmm5
     249: f3 0f 11 6c 0c 20            	movss	%xmm5, 0x20(%rsp,%rcx)
     24f: e9 3c ff ff ff               	jmp	0x190 <tide_rows_Shift_6+0x190>
     254: 48 8b 44 24 08               	movq	0x8(%rsp), %rax
     259: 48 c1 e0 04                  	shlq	$0x4, %rax
     25d: 48 8d 04 40                  	leaq	(%rax,%rax,2), %rax
     261: 31 c9                        	xorl	%ecx, %ecx
     263: 49 89 d6                     	movq	%rdx, %r14
     266: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     270: 44 89 da                     	movl	%r11d, %edx
     273: 41 0f b7 14 51               	movzwl	(%r9,%rdx,2), %edx
     278: c1 e2 02                     	shll	$0x2, %edx
     27b: 44 8b 44 0c 10               	movl	0x10(%rsp,%rcx), %r8d
     280: 45 89 04 17                  	movl	%r8d, (%r15,%rdx)
     284: 44 8b 44 0c 14               	movl	0x14(%rsp,%rcx), %r8d
     289: 45 89 04 16                  	movl	%r8d, (%r14,%rdx)
     28d: 44 8b 44 0c 18               	movl	0x18(%rsp,%rcx), %r8d
     292: 44 89 04 13                  	movl	%r8d, (%rbx,%rdx)
     296: 44 8b 44 0c 1c               	movl	0x1c(%rsp,%rcx), %r8d
     29b: 45 89 04 14                  	movl	%r8d, (%r12,%rdx)
     29f: 44 8b 44 0c 20               	movl	0x20(%rsp,%rcx), %r8d
     2a4: 44 89 04 16                  	movl	%r8d, (%rsi,%rdx)
     2a8: 44 8b 44 0c 24               	movl	0x24(%rsp,%rcx), %r8d
     2ad: 45 89 04 12                  	movl	%r8d, (%r10,%rdx)
     2b1: 48 83 c1 30                  	addq	$0x30, %rcx
     2b5: 41 ff c3                     	incl	%r11d
     2b8: 48 39 c8                     	cmpq	%rcx, %rax
     2bb: 75 b3                        	jne	0x270 <tide_rows_Shift_6+0x270>
     2bd: 0f 28 b4 24 10 18 00 00      	movaps	0x1810(%rsp), %xmm6
     2c5: 0f 28 bc 24 20 18 00 00      	movaps	0x1820(%rsp), %xmm7
     2cd: 44 0f 28 84 24 30 18 00 00   	movaps	0x1830(%rsp), %xmm8
     2d6: 44 0f 28 8c 24 40 18 00 00   	movaps	0x1840(%rsp), %xmm9
     2df: 48 81 c4 58 18 00 00         	addq	$0x1858, %rsp           # imm = 0x1858
     2e6: 5b                           	popq	%rbx
     2e7: 5d                           	popq	%rbp
     2e8: 5f                           	popq	%rdi
     2e9: 5e                           	popq	%rsi
     2ea: 41 5c                        	popq	%r12
     2ec: 41 5d                        	popq	%r13
     2ee: 41 5e                        	popq	%r14
     2f0: 41 5f                        	popq	%r15
     2f2: c3                           	retq
