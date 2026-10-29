
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/c30_control/wide.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <tide_task_Shift>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 48 81 ec 68 02 00 00         	subq	$0x268, %rsp            # imm = 0x268
      13: 44 0f 29 9c 24 50 02 00 00   	movaps	%xmm11, 0x250(%rsp)
      1c: 44 0f 29 94 24 40 02 00 00   	movaps	%xmm10, 0x240(%rsp)
      25: 44 0f 29 8c 24 30 02 00 00   	movaps	%xmm9, 0x230(%rsp)
      2e: 44 0f 29 84 24 20 02 00 00   	movaps	%xmm8, 0x220(%rsp)
      37: 0f 29 bc 24 10 02 00 00      	movaps	%xmm7, 0x210(%rsp)
      3f: 0f 29 b4 24 00 02 00 00      	movaps	%xmm6, 0x200(%rsp)
      47: 89 d0                        	movl	%edx, %eax
      49: 48 89 cf                     	movq	%rcx, %rdi
      4c: 31 d2                        	xorl	%edx, %edx
      4e: 41 f7 f0                     	divl	%r8d
      51: 3b 41 54                     	cmpl	0x54(%rcx), %eax
      54: 0f 83 47 09 00 00            	jae	0x9a1 <tide_task_Shift+0x9a1>
      5a: 44 89 c6                     	movl	%r8d, %esi
      5d: 89 d1                        	movl	%edx, %ecx
      5f: 89 c5                        	movl	%eax, %ebp
      61: 48 8d 47 50                  	leaq	0x50(%rdi), %rax
      65: 48 89 84 24 18 01 00 00      	movq	%rax, 0x118(%rsp)
      6d: 8b 00                        	movl	(%rax), %eax
      6f: 89 ea                        	movl	%ebp, %edx
      71: c1 e2 0b                     	shll	$0xb, %edx
      74: 29 d0                        	subl	%edx, %eax
      76: 3d 00 08 00 00               	cmpl	$0x800, %eax            # imm = 0x800
      7b: 41 b8 00 08 00 00            	movl	$0x800, %r8d            # imm = 0x800
      81: 44 0f 42 c0                  	cmovbl	%eax, %r8d
      85: 89 c8                        	movl	%ecx, %eax
      87: 49 0f af c0                  	imulq	%r8, %rax
      8b: 41 89 f1                     	movl	%esi, %r9d
      8e: 48 ba 00 00 00 00 ff 0f 00 00	movabsq	$0xfff00000000, %rdx    # imm = 0xFFF00000000
      98: 48 85 d0                     	testq	%rdx, %rax
      9b: 74 0a                        	je	0xa7 <tide_task_Shift+0xa7>
      9d: 31 d2                        	xorl	%edx, %edx
      9f: 49 f7 f1                     	divq	%r9
      a2: 49 89 c6                     	movq	%rax, %r14
      a5: eb 08                        	jmp	0xaf <tide_task_Shift+0xaf>
      a7: 31 d2                        	xorl	%edx, %edx
      a9: 41 f7 f1                     	divl	%r9d
      ac: 41 89 c6                     	movl	%eax, %r14d
      af: ff c1                        	incl	%ecx
      b1: 49 0f af c8                  	imulq	%r8, %rcx
      b5: 48 89 c8                     	movq	%rcx, %rax
      b8: 48 c1 e8 20                  	shrq	$0x20, %rax
      bc: 74 0d                        	je	0xcb <tide_task_Shift+0xcb>
      be: 48 89 c8                     	movq	%rcx, %rax
      c1: 31 d2                        	xorl	%edx, %edx
      c3: 49 f7 f1                     	divq	%r9
      c6: 49 89 c4                     	movq	%rax, %r12
      c9: eb 0a                        	jmp	0xd5 <tide_task_Shift+0xd5>
      cb: 89 c8                        	movl	%ecx, %eax
      cd: 31 d2                        	xorl	%edx, %edx
      cf: 41 f7 f1                     	divl	%r9d
      d2: 41 89 c4                     	movl	%eax, %r12d
      d5: 4c 8b 5f 60                  	movq	0x60(%rdi), %r11
      d9: 41 89 ef                     	movl	%ebp, %r15d
      dc: 41 c1 e7 04                  	shll	$0x4, %r15d
      e0: 4f 8b 6c fb 70               	movq	0x70(%r11,%r15,8), %r13
      e5: 49 83 c5 20                  	addq	$0x20, %r13
      e9: 48 c7 84 24 98 01 00 00 0e 00 00 00  	movq	$0xe, 0x198(%rsp)
      f5: 48 8b 8f a0 00 00 00         	movq	0xa0(%rdi), %rcx
      fc: 48 85 c9                     	testq	%rcx, %rcx
      ff: 74 6a                        	je	0x16b <tide_task_Shift+0x16b>
     101: 83 39 01                     	cmpl	$0x1, (%rcx)
     104: 75 65                        	jne	0x16b <tide_task_Shift+0x16b>
     106: 83 79 04 00                  	cmpl	$0x0, 0x4(%rcx)
     10a: 74 5f                        	je	0x16b <tide_task_Shift+0x16b>
     10c: 48 8b 49 08                  	movq	0x8(%rcx), %rcx
     110: 83 39 01                     	cmpl	$0x1, (%rcx)
     113: 75 56                        	jne	0x16b <tide_task_Shift+0x16b>
     115: 83 79 04 0b                  	cmpl	$0xb, 0x4(%rcx)
     119: 75 50                        	jne	0x16b <tide_task_Shift+0x16b>
     11b: 83 79 08 01                  	cmpl	$0x1, 0x8(%rcx)
     11f: 75 4a                        	jne	0x16b <tide_task_Shift+0x16b>
     121: 3b 69 0c                     	cmpl	0xc(%rcx), %ebp
     124: 73 45                        	jae	0x16b <tide_task_Shift+0x16b>
     126: 48 8b 51 10                  	movq	0x10(%rcx), %rdx
     12a: 48 8b 12                     	movq	(%rdx), %rdx
     12d: 48 3b 94 24 98 01 00 00      	cmpq	0x198(%rsp), %rdx
     135: 75 34                        	jne	0x16b <tide_task_Shift+0x16b>
     137: 4c 8b 51 20                  	movq	0x20(%rcx), %r10
     13b: 89 ea                        	movl	%ebp, %edx
     13d: 4c 69 ca 0c 01 00 00         	imulq	$0x10c, %rdx, %r9       # imm = 0x10C
     144: 43 80 7c 0a 08 01            	cmpb	$0x1, 0x8(%r10,%r9)
     14a: 75 1f                        	jne	0x16b <tide_task_Shift+0x16b>
     14c: 4d 01 ca                     	addq	%r9, %r10
     14f: 45 39 02                     	cmpl	%r8d, (%r10)
     152: 75 17                        	jne	0x16b <tide_task_Shift+0x16b>
     154: 48 8b 49 18                  	movq	0x18(%rcx), %rcx
     158: 48 8b 0c d1                  	movq	(%rcx,%rdx,8), %rcx
     15c: 48 c1 e2 07                  	shlq	$0x7, %rdx
     160: 49 3b 4c 13 70               	cmpq	0x70(%r11,%rdx), %rcx
     165: 0f 84 7e 08 00 00            	je	0x9e9 <tide_task_Shift+0x9e9>
     16b: 48 c7 84 24 e0 00 00 00 00 00 00 00  	movq	$0x0, 0xe0(%rsp)
     177: 31 c0                        	xorl	%eax, %eax
     179: 45 39 f4                     	cmpl	%r14d, %r12d
     17c: 0f 86 d5 00 00 00            	jbe	0x257 <tide_task_Shift+0x257>
     182: 44 89 f1                     	movl	%r14d, %ecx
     185: 44 89 e2                     	movl	%r12d, %edx
     188: 49 89 d0                     	movq	%rdx, %r8
     18b: 49 29 c8                     	subq	%rcx, %r8
     18e: 31 db                        	xorl	%ebx, %ebx
     190: 49 83 f8 08                  	cmpq	$0x8, %r8
     194: 0f 82 a6 00 00 00            	jb	0x240 <tide_task_Shift+0x240>
     19a: 4c 89 9c 24 e8 00 00 00      	movq	%r11, 0xe8(%rsp)
     1a2: 4d 89 c1                     	movq	%r8, %r9
     1a5: 49 83 e1 f8                  	andq	$-0x8, %r9
     1a9: 4e 8d 14 29                  	leaq	(%rcx,%r13), %r10
     1ad: 4c 01 c9                     	addq	%r9, %rcx
     1b0: 66 0f ef c0                  	pxor	%xmm0, %xmm0
     1b4: 45 31 db                     	xorl	%r11d, %r11d
     1b7: 66 0f 6f 1d 00 00 00 00      	movdqa	(%rip), %xmm3           # 0x1bf <tide_task_Shift+0x1bf>
     1bf: 66 0f ef d2                  	pxor	%xmm2, %xmm2
     1c3: 66 0f ef c9                  	pxor	%xmm1, %xmm1
     1c7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     1d0: 66 43 0f 6e 24 1a            	movd	(%r10,%r11), %xmm4
     1d6: 66 43 0f 6e 6c 1a 04         	movd	0x4(%r10,%r11), %xmm5
     1dd: 66 0f 74 e0                  	pcmpeqb	%xmm0, %xmm4
     1e1: 66 0f 60 e4                  	punpcklbw	%xmm4, %xmm4    # xmm4 = xmm4[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1e5: 66 0f 61 e4                  	punpcklwd	%xmm4, %xmm4    # xmm4 = xmm4[0,0,1,1,2,2,3,3]
     1e9: 66 0f db e3                  	pand	%xmm3, %xmm4
     1ed: 66 0f fe d4                  	paddd	%xmm4, %xmm2
     1f1: 66 0f 74 e8                  	pcmpeqb	%xmm0, %xmm5
     1f5: 66 0f 60 ed                  	punpcklbw	%xmm5, %xmm5    # xmm5 = xmm5[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1f9: 66 0f 61 ed                  	punpcklwd	%xmm5, %xmm5    # xmm5 = xmm5[0,0,1,1,2,2,3,3]
     1fd: 66 0f db eb                  	pand	%xmm3, %xmm5
     201: 66 0f fe cd                  	paddd	%xmm5, %xmm1
     205: 49 83 c3 08                  	addq	$0x8, %r11
     209: 4d 39 d9                     	cmpq	%r11, %r9
     20c: 75 c2                        	jne	0x1d0 <tide_task_Shift+0x1d0>
     20e: 66 0f fe ca                  	paddd	%xmm2, %xmm1
     212: 66 0f 70 c1 ee               	pshufd	$0xee, %xmm1, %xmm0     # xmm0 = xmm1[2,3,2,3]
     217: 66 0f fe c1                  	paddd	%xmm1, %xmm0
     21b: 66 0f 70 c8 55               	pshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
     220: 66 0f fe c8                  	paddd	%xmm0, %xmm1
     224: 66 0f 7e cb                  	movd	%xmm1, %ebx
     228: 4d 39 c8                     	cmpq	%r9, %r8
     22b: 4c 8b 9c 24 e8 00 00 00      	movq	0xe8(%rsp), %r11
     233: 75 0b                        	jne	0x240 <tide_task_Shift+0x240>
     235: 31 c9                        	xorl	%ecx, %ecx
     237: 31 d2                        	xorl	%edx, %edx
     239: eb 22                        	jmp	0x25d <tide_task_Shift+0x25d>
     23b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     240: 41 80 7c 0d 00 01            	cmpb	$0x1, (%r13,%rcx)
     246: 83 d3 00                     	adcl	$0x0, %ebx
     249: 48 ff c1                     	incq	%rcx
     24c: 48 39 d1                     	cmpq	%rdx, %rcx
     24f: 72 ef                        	jb	0x240 <tide_task_Shift+0x240>
     251: 31 d2                        	xorl	%edx, %edx
     253: 31 c9                        	xorl	%ecx, %ecx
     255: eb 06                        	jmp	0x25d <tide_task_Shift+0x25d>
     257: 31 d2                        	xorl	%edx, %edx
     259: 31 c9                        	xorl	%ecx, %ecx
     25b: 31 db                        	xorl	%ebx, %ebx
     25d: 83 fe 01                     	cmpl	$0x1, %esi
     260: 75 08                        	jne	0x26a <tide_task_Shift+0x26a>
     262: 85 db                        	testl	%ebx, %ebx
     264: 0f 84 37 07 00 00            	je	0x9a1 <tide_task_Shift+0x9a1>
     26a: 48 89 8c 24 10 01 00 00      	movq	%rcx, 0x110(%rsp)
     272: 48 89 94 24 88 01 00 00      	movq	%rdx, 0x188(%rsp)
     27a: 48 89 84 24 90 01 00 00      	movq	%rax, 0x190(%rsp)
     282: 4c 89 ac 24 e8 00 00 00      	movq	%r13, 0xe8(%rsp)
     28a: 83 fe 01                     	cmpl	$0x1, %esi
     28d: 0f 85 c8 00 00 00            	jne	0x35b <tide_task_Shift+0x35b>
     293: 44 89 f9                     	movl	%r15d, %ecx
     296: 49 8b 54 cb 10               	movq	0x10(%r11,%rcx,8), %rdx
     29b: 41 b5 01                     	movb	$0x1, %r13b
     29e: 83 3a 01                     	cmpl	$0x1, (%rdx)
     2a1: 48 89 bc 24 00 01 00 00      	movq	%rdi, 0x100(%rsp)
     2a9: 89 b4 24 f4 00 00 00         	movl	%esi, 0xf4(%rsp)
     2b0: 4c 89 bc 24 f8 00 00 00      	movq	%r15, 0xf8(%rsp)
     2b8: 4c 89 b4 24 08 01 00 00      	movq	%r14, 0x108(%rsp)
     2c0: 4c 89 a4 24 80 01 00 00      	movq	%r12, 0x180(%rsp)
     2c8: 0f 85 d4 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     2ce: 49 8b 54 cb 18               	movq	0x18(%r11,%rcx,8), %rdx
     2d3: 83 3a 01                     	cmpl	$0x1, (%rdx)
     2d6: 0f 85 c6 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     2dc: 49 8b 54 cb 20               	movq	0x20(%r11,%rcx,8), %rdx
     2e1: 83 3a 01                     	cmpl	$0x1, (%rdx)
     2e4: 0f 85 b8 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     2ea: 49 8b 54 cb 28               	movq	0x28(%r11,%rcx,8), %rdx
     2ef: 83 3a 01                     	cmpl	$0x1, (%rdx)
     2f2: 0f 85 aa 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     2f8: 49 8b 54 cb 30               	movq	0x30(%r11,%rcx,8), %rdx
     2fd: 83 3a 01                     	cmpl	$0x1, (%rdx)
     300: 0f 85 9c 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     306: 49 8b 54 cb 38               	movq	0x38(%r11,%rcx,8), %rdx
     30b: 83 3a 01                     	cmpl	$0x1, (%rdx)
     30e: 0f 85 8e 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     314: 49 8b 54 cb 40               	movq	0x40(%r11,%rcx,8), %rdx
     319: 83 3a 01                     	cmpl	$0x1, (%rdx)
     31c: 0f 85 80 00 00 00            	jne	0x3a2 <tide_task_Shift+0x3a2>
     322: 49 8b 54 cb 48               	movq	0x48(%r11,%rcx,8), %rdx
     327: 83 3a 01                     	cmpl	$0x1, (%rdx)
     32a: 75 76                        	jne	0x3a2 <tide_task_Shift+0x3a2>
     32c: 49 8b 54 cb 50               	movq	0x50(%r11,%rcx,8), %rdx
     331: 83 3a 01                     	cmpl	$0x1, (%rdx)
     334: 75 6c                        	jne	0x3a2 <tide_task_Shift+0x3a2>
     336: 49 8b 54 cb 58               	movq	0x58(%r11,%rcx,8), %rdx
     33b: 83 3a 01                     	cmpl	$0x1, (%rdx)
     33e: 75 62                        	jne	0x3a2 <tide_task_Shift+0x3a2>
     340: 41 89 ec                     	movl	%ebp, %r12d
     343: 49 8b 54 cb 60               	movq	0x60(%r11,%rcx,8), %rdx
     348: 83 3a 01                     	cmpl	$0x1, (%rdx)
     34b: 75 58                        	jne	0x3a5 <tide_task_Shift+0x3a5>
     34d: 49 8b 44 cb 68               	movq	0x68(%r11,%rcx,8), %rax
     352: 83 38 01                     	cmpl	$0x1, (%rax)
     355: 41 0f 95 c5                  	setne	%r13b
     359: eb 4a                        	jmp	0x3a5 <tide_task_Shift+0x3a5>
     35b: 48 8d 84 24 20 01 00 00      	leaq	0x120(%rsp), %rax
     363: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     368: 48 8d 84 24 a0 01 00 00      	leaq	0x1a0(%rsp), %rax
     370: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
     375: c7 44 24 20 0c 00 00 00      	movl	$0xc, 0x20(%rsp)
     37d: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x384 <tide_task_Shift+0x384>
     384: 48 8b 8c 24 18 01 00 00      	movq	0x118(%rsp), %rcx
     38c: 41 89 e8                     	movl	%ebp, %r8d
     38f: 41 b9 02 00 00 00            	movl	$0x2, %r9d
     395: e8 00 00 00 00               	callq	0x39a <tide_task_Shift+0x39a>
     39a: 41 89 c5                     	movl	%eax, %r13d
     39d: e9 c9 00 00 00               	jmp	0x46b <tide_task_Shift+0x46b>
     3a2: 41 89 ec                     	movl	%ebp, %r12d
     3a5: 48 8d ac 24 a0 01 00 00      	leaq	0x1a0(%rsp), %rbp
     3ad: 48 8d bc 24 20 01 00 00      	leaq	0x120(%rsp), %rdi
     3b5: 49 c7 c7 f4 ff ff ff         	movq	$-0xc, %r15
     3bc: 31 f6                        	xorl	%esi, %esi
     3be: 4c 8d 35 00 00 00 00         	leaq	(%rip), %r14            # 0x3c5 <tide_task_Shift+0x3c5>
     3c5: eb 3f                        	jmp	0x406 <tide_task_Shift+0x406>
     3c7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     3d0: 4d 8d 4f 0e                  	leaq	0xe(%r15), %r9
     3d4: 48 89 7c 24 30               	movq	%rdi, 0x30(%rsp)
     3d9: 48 89 6c 24 28               	movq	%rbp, 0x28(%rsp)
     3de: 44 88 6c 24 20               	movb	%r13b, 0x20(%rsp)
     3e3: 48 8b 8c 24 18 01 00 00      	movq	0x118(%rsp), %rcx
     3eb: 4c 89 f2                     	movq	%r14, %rdx
     3ee: 45 89 e0                     	movl	%r12d, %r8d
     3f1: e8 00 00 00 00               	callq	0x3f6 <tide_task_Shift+0x3f6>
     3f6: 48 ff c6                     	incq	%rsi
     3f9: 48 83 c5 08                  	addq	$0x8, %rbp
     3fd: 48 83 c7 08                  	addq	$0x8, %rdi
     401: 49 ff c7                     	incq	%r15
     404: 74 3b                        	je	0x441 <tide_task_Shift+0x441>
     406: 45 84 ed                     	testb	%r13b, %r13b
     409: 75 c5                        	jne	0x3d0 <tide_task_Shift+0x3d0>
     40b: 48 8b 84 24 00 01 00 00      	movq	0x100(%rsp), %rax
     413: 48 8b 40 60                  	movq	0x60(%rax), %rax
     417: 48 8b 8c 24 f8 00 00 00      	movq	0xf8(%rsp), %rcx
     41f: 01 f1                        	addl	%esi, %ecx
     421: 83 c1 02                     	addl	$0x2, %ecx
     424: 48 8b 04 c8                  	movq	(%rax,%rcx,8), %rax
     428: 83 38 01                     	cmpl	$0x1, (%rax)
     42b: 75 a3                        	jne	0x3d0 <tide_task_Shift+0x3d0>
     42d: c7 40 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rax)  # imm = 0xFFFFFFFF
     434: 48 83 c0 20                  	addq	$0x20, %rax
     438: 48 89 07                     	movq	%rax, (%rdi)
     43b: 48 89 45 00                  	movq	%rax, (%rbp)
     43f: eb b5                        	jmp	0x3f6 <tide_task_Shift+0x3f6>
     441: 48 8b bc 24 00 01 00 00      	movq	0x100(%rsp), %rdi
     449: 44 89 e5                     	movl	%r12d, %ebp
     44c: 8b b4 24 f4 00 00 00         	movl	0xf4(%rsp), %esi
     453: 4c 8b bc 24 f8 00 00 00      	movq	0xf8(%rsp), %r15
     45b: 4c 8b b4 24 08 01 00 00      	movq	0x108(%rsp), %r14
     463: 4c 8b a4 24 80 01 00 00      	movq	0x180(%rsp), %r12
     46b: 44 89 e0                     	movl	%r12d, %eax
     46e: 44 29 f0                     	subl	%r14d, %eax
     471: 45 84 ed                     	testb	%r13b, %r13b
     474: 0f 84 e0 00 00 00            	je	0x55a <tide_task_Shift+0x55a>
     47a: 39 d8                        	cmpl	%ebx, %eax
     47c: 0f 85 d8 00 00 00            	jne	0x55a <tide_task_Shift+0x55a>
     482: f3 0f 6f 84 24 70 01 00 00   	movdqu	0x170(%rsp), %xmm0
     48b: f3 0f 6f 8c 24 60 01 00 00   	movdqu	0x160(%rsp), %xmm1
     494: f3 0f 6f 94 24 20 01 00 00   	movdqu	0x120(%rsp), %xmm2
     49d: f3 0f 6f 9c 24 30 01 00 00   	movdqu	0x130(%rsp), %xmm3
     4a6: f3 0f 6f a4 24 40 01 00 00   	movdqu	0x140(%rsp), %xmm4
     4af: f3 0f 6f ac 24 50 01 00 00   	movdqu	0x150(%rsp), %xmm5
     4b8: 48 8b 84 24 f8 01 00 00      	movq	0x1f8(%rsp), %rax
     4c0: 0f 10 b4 24 e8 01 00 00      	movups	0x1e8(%rsp), %xmm6
     4c8: 0f 10 bc 24 d8 01 00 00      	movups	0x1d8(%rsp), %xmm7
     4d0: 44 0f 10 84 24 c8 01 00 00   	movups	0x1c8(%rsp), %xmm8
     4d9: 44 0f 10 8c 24 b8 01 00 00   	movups	0x1b8(%rsp), %xmm9
     4e2: 44 0f 10 94 24 a8 01 00 00   	movups	0x1a8(%rsp), %xmm10
     4eb: 4c 8b 8c 24 a0 01 00 00      	movq	0x1a0(%rsp), %r9
     4f3: f3 0f 7f 84 24 c8 00 00 00   	movdqu	%xmm0, 0xc8(%rsp)
     4fc: f3 0f 7f 8c 24 b8 00 00 00   	movdqu	%xmm1, 0xb8(%rsp)
     505: f3 0f 7f ac 24 a8 00 00 00   	movdqu	%xmm5, 0xa8(%rsp)
     50e: f3 0f 7f a4 24 98 00 00 00   	movdqu	%xmm4, 0x98(%rsp)
     517: f3 0f 7f 9c 24 88 00 00 00   	movdqu	%xmm3, 0x88(%rsp)
     520: f3 0f 7f 54 24 78            	movdqu	%xmm2, 0x78(%rsp)
     526: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     52b: 0f 11 74 24 60               	movups	%xmm6, 0x60(%rsp)
     530: 0f 11 7c 24 50               	movups	%xmm7, 0x50(%rsp)
     535: 44 0f 11 44 24 40            	movups	%xmm8, 0x40(%rsp)
     53b: 44 0f 11 4c 24 30            	movups	%xmm9, 0x30(%rsp)
     541: 44 0f 11 54 24 20            	movups	%xmm10, 0x20(%rsp)
     547: 48 89 f9                     	movq	%rdi, %rcx
     54a: 44 89 f2                     	movl	%r14d, %edx
     54d: 45 89 e0                     	movl	%r12d, %r8d
     550: e8 00 00 00 00               	callq	0x555 <tide_task_Shift+0x555>
     555: e9 e9 03 00 00               	jmp	0x943 <tide_task_Shift+0x943>
     55a: 89 c1                        	movl	%eax, %ecx
     55c: 29 d9                        	subl	%ebx, %ecx
     55e: 75 73                        	jne	0x5d3 <tide_task_Shift+0x5d3>
     560: 48 8b 84 24 78 01 00 00      	movq	0x178(%rsp), %rax
     568: f3 0f 6f 84 24 68 01 00 00   	movdqu	0x168(%rsp), %xmm0
     571: f3 0f 6f 8c 24 58 01 00 00   	movdqu	0x158(%rsp), %xmm1
     57a: f3 0f 6f 94 24 48 01 00 00   	movdqu	0x148(%rsp), %xmm2
     583: f3 0f 6f 9c 24 38 01 00 00   	movdqu	0x138(%rsp), %xmm3
     58c: f3 0f 6f a4 24 28 01 00 00   	movdqu	0x128(%rsp), %xmm4
     595: 4c 8b 8c 24 20 01 00 00      	movq	0x120(%rsp), %r9
     59d: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     5a2: f3 0f 7f 44 24 60            	movdqu	%xmm0, 0x60(%rsp)
     5a8: f3 0f 7f 4c 24 50            	movdqu	%xmm1, 0x50(%rsp)
     5ae: f3 0f 7f 54 24 40            	movdqu	%xmm2, 0x40(%rsp)
     5b4: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     5ba: f3 0f 7f 64 24 20            	movdqu	%xmm4, 0x20(%rsp)
     5c0: 48 89 f9                     	movq	%rdi, %rcx
     5c3: 44 89 f2                     	movl	%r14d, %edx
     5c6: 45 89 e0                     	movl	%r12d, %r8d
     5c9: e8 00 00 00 00               	callq	0x5ce <tide_task_Shift+0x5ce>
     5ce: e9 70 03 00 00               	jmp	0x943 <tide_task_Shift+0x943>
     5d3: 45 84 ed                     	testb	%r13b, %r13b
     5d6: 0f 84 dd 00 00 00            	je	0x6b9 <tide_task_Shift+0x6b9>
     5dc: f3 0f 6f 84 24 70 01 00 00   	movdqu	0x170(%rsp), %xmm0
     5e5: f3 0f 6f 8c 24 60 01 00 00   	movdqu	0x160(%rsp), %xmm1
     5ee: f3 0f 6f 94 24 20 01 00 00   	movdqu	0x120(%rsp), %xmm2
     5f7: f3 0f 6f 9c 24 30 01 00 00   	movdqu	0x130(%rsp), %xmm3
     600: f3 0f 6f a4 24 40 01 00 00   	movdqu	0x140(%rsp), %xmm4
     609: f3 0f 6f ac 24 50 01 00 00   	movdqu	0x150(%rsp), %xmm5
     612: 0f 10 b4 24 f0 01 00 00      	movups	0x1f0(%rsp), %xmm6
     61a: 0f 10 bc 24 e0 01 00 00      	movups	0x1e0(%rsp), %xmm7
     622: 44 0f 10 84 24 a0 01 00 00   	movups	0x1a0(%rsp), %xmm8
     62b: 44 0f 10 8c 24 b0 01 00 00   	movups	0x1b0(%rsp), %xmm9
     634: 44 0f 10 94 24 c0 01 00 00   	movups	0x1c0(%rsp), %xmm10
     63d: 44 0f 10 9c 24 d0 01 00 00   	movups	0x1d0(%rsp), %xmm11
     646: f3 0f 7f 84 24 d0 00 00 00   	movdqu	%xmm0, 0xd0(%rsp)
     64f: f3 0f 7f 8c 24 c0 00 00 00   	movdqu	%xmm1, 0xc0(%rsp)
     658: f3 0f 7f ac 24 b0 00 00 00   	movdqu	%xmm5, 0xb0(%rsp)
     661: f3 0f 7f a4 24 a0 00 00 00   	movdqu	%xmm4, 0xa0(%rsp)
     66a: f3 0f 7f 9c 24 90 00 00 00   	movdqu	%xmm3, 0x90(%rsp)
     673: f3 0f 7f 94 24 80 00 00 00   	movdqu	%xmm2, 0x80(%rsp)
     67c: 0f 11 74 24 70               	movups	%xmm6, 0x70(%rsp)
     681: 0f 11 7c 24 60               	movups	%xmm7, 0x60(%rsp)
     686: 44 0f 11 5c 24 50            	movups	%xmm11, 0x50(%rsp)
     68c: 44 0f 11 54 24 40            	movups	%xmm10, 0x40(%rsp)
     692: 44 0f 11 4c 24 30            	movups	%xmm9, 0x30(%rsp)
     698: 44 0f 11 44 24 20            	movups	%xmm8, 0x20(%rsp)
     69e: 48 89 f9                     	movq	%rdi, %rcx
     6a1: 44 89 f2                     	movl	%r14d, %edx
     6a4: 45 89 e0                     	movl	%r12d, %r8d
     6a7: 4c 8b 8c 24 e8 00 00 00      	movq	0xe8(%rsp), %r9
     6af: e8 00 00 00 00               	callq	0x6b4 <tide_task_Shift+0x6b4>
     6b4: e9 8a 02 00 00               	jmp	0x943 <tide_task_Shift+0x943>
     6b9: 80 bc 24 10 01 00 00 00      	cmpb	$0x0, 0x110(%rsp)
     6c1: 4c 8b 8c 24 e8 00 00 00      	movq	0xe8(%rsp), %r9
     6c9: 74 7f                        	je	0x74a <tide_task_Shift+0x74a>
     6cb: f3 0f 6f 84 24 70 01 00 00   	movdqu	0x170(%rsp), %xmm0
     6d4: f3 0f 6f 8c 24 60 01 00 00   	movdqu	0x160(%rsp), %xmm1
     6dd: f3 0f 6f 94 24 20 01 00 00   	movdqu	0x120(%rsp), %xmm2
     6e6: f3 0f 6f 9c 24 30 01 00 00   	movdqu	0x130(%rsp), %xmm3
     6ef: f3 0f 6f a4 24 40 01 00 00   	movdqu	0x140(%rsp), %xmm4
     6f8: f3 0f 6f ac 24 50 01 00 00   	movdqu	0x150(%rsp), %xmm5
     701: f3 0f 7f 44 24 70            	movdqu	%xmm0, 0x70(%rsp)
     707: f3 0f 7f 4c 24 60            	movdqu	%xmm1, 0x60(%rsp)
     70d: f3 0f 7f 6c 24 50            	movdqu	%xmm5, 0x50(%rsp)
     713: f3 0f 7f 64 24 40            	movdqu	%xmm4, 0x40(%rsp)
     719: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     71f: f3 0f 7f 54 24 20            	movdqu	%xmm2, 0x20(%rsp)
     725: 48 89 f9                     	movq	%rdi, %rcx
     728: 48 8b 94 24 90 01 00 00      	movq	0x190(%rsp), %rdx
     730: 4c 8b 84 24 88 01 00 00      	movq	0x188(%rsp), %r8
     738: 4c 8b 8c 24 e0 00 00 00      	movq	0xe0(%rsp), %r9
     740: e8 00 00 00 00               	callq	0x745 <tide_task_Shift+0x745>
     745: e9 f9 01 00 00               	jmp	0x943 <tide_task_Shift+0x943>
     74a: 8d 50 0f                     	leal	0xf(%rax), %edx
     74d: c1 ea 04                     	shrl	$0x4, %edx
     750: 39 d3                        	cmpl	%edx, %ebx
     752: 73 6d                        	jae	0x7c1 <tide_task_Shift+0x7c1>
     754: f3 0f 6f 84 24 70 01 00 00   	movdqu	0x170(%rsp), %xmm0
     75d: f3 0f 6f 8c 24 60 01 00 00   	movdqu	0x160(%rsp), %xmm1
     766: f3 0f 6f 94 24 20 01 00 00   	movdqu	0x120(%rsp), %xmm2
     76f: f3 0f 6f 9c 24 30 01 00 00   	movdqu	0x130(%rsp), %xmm3
     778: f3 0f 6f a4 24 40 01 00 00   	movdqu	0x140(%rsp), %xmm4
     781: f3 0f 6f ac 24 50 01 00 00   	movdqu	0x150(%rsp), %xmm5
     78a: f3 0f 7f 44 24 70            	movdqu	%xmm0, 0x70(%rsp)
     790: f3 0f 7f 4c 24 60            	movdqu	%xmm1, 0x60(%rsp)
     796: f3 0f 7f 6c 24 50            	movdqu	%xmm5, 0x50(%rsp)
     79c: f3 0f 7f 64 24 40            	movdqu	%xmm4, 0x40(%rsp)
     7a2: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     7a8: f3 0f 7f 54 24 20            	movdqu	%xmm2, 0x20(%rsp)
     7ae: 48 89 f9                     	movq	%rdi, %rcx
     7b1: 44 89 f2                     	movl	%r14d, %edx
     7b4: 45 89 e0                     	movl	%r12d, %r8d
     7b7: e8 00 00 00 00               	callq	0x7bc <tide_task_Shift+0x7bc>
     7bc: e9 82 01 00 00               	jmp	0x943 <tide_task_Shift+0x943>
     7c1: 4c 89 b4 24 08 01 00 00      	movq	%r14, 0x108(%rsp)
     7c9: 4c 89 bc 24 f8 00 00 00      	movq	%r15, 0xf8(%rsp)
     7d1: 89 b4 24 f4 00 00 00         	movl	%esi, 0xf4(%rsp)
     7d8: 89 ac 24 e0 00 00 00         	movl	%ebp, 0xe0(%rsp)
     7df: 48 89 bc 24 00 01 00 00      	movq	%rdi, 0x100(%rsp)
     7e7: 83 c0 03                     	addl	$0x3, %eax
     7ea: c1 e8 02                     	shrl	$0x2, %eax
     7ed: 48 8b 94 24 78 01 00 00      	movq	0x178(%rsp), %rdx
     7f5: 48 8b ac 24 70 01 00 00      	movq	0x170(%rsp), %rbp
     7fd: 4c 8b ac 24 68 01 00 00      	movq	0x168(%rsp), %r13
     805: 4c 8b bc 24 60 01 00 00      	movq	0x160(%rsp), %r15
     80d: 4c 8b b4 24 58 01 00 00      	movq	0x158(%rsp), %r14
     815: 48 8b bc 24 50 01 00 00      	movq	0x150(%rsp), %rdi
     81d: 48 8b b4 24 48 01 00 00      	movq	0x148(%rsp), %rsi
     825: 4c 8b 9c 24 40 01 00 00      	movq	0x140(%rsp), %r11
     82d: 4c 8b 94 24 38 01 00 00      	movq	0x138(%rsp), %r10
     835: 4c 8b 84 24 30 01 00 00      	movq	0x130(%rsp), %r8
     83d: 4c 8b 8c 24 20 01 00 00      	movq	0x120(%rsp), %r9
     845: 4c 89 8c 24 10 01 00 00      	movq	%r9, 0x110(%rsp)
     84d: 4c 8b 8c 24 28 01 00 00      	movq	0x128(%rsp), %r9
     855: 39 c3                        	cmpl	%eax, %ebx
     857: 72 6d                        	jb	0x8c6 <tide_task_Shift+0x8c6>
     859: 39 c1                        	cmpl	%eax, %ecx
     85b: 72 69                        	jb	0x8c6 <tide_task_Shift+0x8c6>
     85d: 48 89 54 24 78               	movq	%rdx, 0x78(%rsp)
     862: 48 89 6c 24 70               	movq	%rbp, 0x70(%rsp)
     867: 4c 89 6c 24 68               	movq	%r13, 0x68(%rsp)
     86c: 4c 89 7c 24 60               	movq	%r15, 0x60(%rsp)
     871: 4c 89 74 24 58               	movq	%r14, 0x58(%rsp)
     876: 48 89 7c 24 50               	movq	%rdi, 0x50(%rsp)
     87b: 48 89 74 24 48               	movq	%rsi, 0x48(%rsp)
     880: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
     885: 4c 89 54 24 38               	movq	%r10, 0x38(%rsp)
     88a: 4c 89 44 24 30               	movq	%r8, 0x30(%rsp)
     88f: 4c 89 4c 24 28               	movq	%r9, 0x28(%rsp)
     894: 48 8b 84 24 10 01 00 00      	movq	0x110(%rsp), %rax
     89c: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     8a1: 45 89 e0                     	movl	%r12d, %r8d
     8a4: 4c 8b 8c 24 e8 00 00 00      	movq	0xe8(%rsp), %r9
     8ac: 48 8b bc 24 00 01 00 00      	movq	0x100(%rsp), %rdi
     8b4: 48 89 f9                     	movq	%rdi, %rcx
     8b7: 48 8b 94 24 08 01 00 00      	movq	0x108(%rsp), %rdx
     8bf: e8 00 00 00 00               	callq	0x8c4 <tide_task_Shift+0x8c4>
     8c4: eb 67                        	jmp	0x92d <tide_task_Shift+0x92d>
     8c6: 48 89 54 24 78               	movq	%rdx, 0x78(%rsp)
     8cb: 48 89 6c 24 70               	movq	%rbp, 0x70(%rsp)
     8d0: 4c 89 6c 24 68               	movq	%r13, 0x68(%rsp)
     8d5: 4c 89 7c 24 60               	movq	%r15, 0x60(%rsp)
     8da: 4c 89 74 24 58               	movq	%r14, 0x58(%rsp)
     8df: 48 89 7c 24 50               	movq	%rdi, 0x50(%rsp)
     8e4: 48 89 74 24 48               	movq	%rsi, 0x48(%rsp)
     8e9: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
     8ee: 4c 89 54 24 38               	movq	%r10, 0x38(%rsp)
     8f3: 4c 89 44 24 30               	movq	%r8, 0x30(%rsp)
     8f8: 4c 89 4c 24 28               	movq	%r9, 0x28(%rsp)
     8fd: 48 8b 84 24 10 01 00 00      	movq	0x110(%rsp), %rax
     905: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     90a: 45 89 e0                     	movl	%r12d, %r8d
     90d: 4c 8b 8c 24 e8 00 00 00      	movq	0xe8(%rsp), %r9
     915: 48 8b bc 24 00 01 00 00      	movq	0x100(%rsp), %rdi
     91d: 48 89 f9                     	movq	%rdi, %rcx
     920: 48 8b 94 24 08 01 00 00      	movq	0x108(%rsp), %rdx
     928: e8 00 00 00 00               	callq	0x92d <tide_task_Shift+0x92d>
     92d: 8b ac 24 e0 00 00 00         	movl	0xe0(%rsp), %ebp
     934: 8b b4 24 f4 00 00 00         	movl	0xf4(%rsp), %esi
     93b: 4c 8b bc 24 f8 00 00 00      	movq	0xf8(%rsp), %r15
     943: 83 fe 01                     	cmpl	$0x1, %esi
     946: 75 59                        	jne	0x9a1 <tide_task_Shift+0x9a1>
     948: 41 83 cf 02                  	orl	$0x2, %r15d
     94c: be 0b 00 00 00               	movl	$0xb, %esi
     951: 4c 8d 35 00 00 00 00         	leaq	(%rip), %r14            # 0x958 <tide_task_Shift+0x958>
     958: eb 0c                        	jmp	0x966 <tide_task_Shift+0x966>
     95a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     960: 48 83 c6 ff                  	addq	$-0x1, %rsi
     964: 73 3b                        	jae	0x9a1 <tide_task_Shift+0x9a1>
     966: 48 8b 84 f4 20 01 00 00      	movq	0x120(%rsp,%rsi,8), %rax
     96e: 48 8b 4f 60                  	movq	0x60(%rdi), %rcx
     972: 41 8d 14 37                  	leal	(%r15,%rsi), %edx
     976: 48 8b 0c d1                  	movq	(%rcx,%rdx,8), %rcx
     97a: 48 83 c1 20                  	addq	$0x20, %rcx
     97e: 48 39 c8                     	cmpq	%rcx, %rax
     981: 74 dd                        	je	0x960 <tide_task_Shift+0x960>
     983: 44 8d 4e 02                  	leal	0x2(%rsi), %r9d
     987: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     98c: 48 8b 8c 24 18 01 00 00      	movq	0x118(%rsp), %rcx
     994: 4c 89 f2                     	movq	%r14, %rdx
     997: 41 89 e8                     	movl	%ebp, %r8d
     99a: e8 00 00 00 00               	callq	0x99f <tide_task_Shift+0x99f>
     99f: eb bf                        	jmp	0x960 <tide_task_Shift+0x960>
     9a1: 0f 28 b4 24 00 02 00 00      	movaps	0x200(%rsp), %xmm6
     9a9: 0f 28 bc 24 10 02 00 00      	movaps	0x210(%rsp), %xmm7
     9b1: 44 0f 28 84 24 20 02 00 00   	movaps	0x220(%rsp), %xmm8
     9ba: 44 0f 28 8c 24 30 02 00 00   	movaps	0x230(%rsp), %xmm9
     9c3: 44 0f 28 94 24 40 02 00 00   	movaps	0x240(%rsp), %xmm10
     9cc: 44 0f 28 9c 24 50 02 00 00   	movaps	0x250(%rsp), %xmm11
     9d5: 48 81 c4 68 02 00 00         	addq	$0x268, %rsp            # imm = 0x268
     9dc: 5b                           	popq	%rbx
     9dd: 5d                           	popq	%rbp
     9de: 5f                           	popq	%rdi
     9df: 5e                           	popq	%rsi
     9e0: 41 5c                        	popq	%r12
     9e2: 41 5d                        	popq	%r13
     9e4: 41 5e                        	popq	%r14
     9e6: 41 5f                        	popq	%r15
     9e8: c3                           	retq
     9e9: 4c 89 9c 24 e8 00 00 00      	movq	%r11, 0xe8(%rsp)
     9f1: 4c 89 94 24 e0 00 00 00      	movq	%r10, 0xe0(%rsp)
     9f9: 41 8b 5a 04                  	movl	0x4(%r10), %ebx
     9fd: 41 8d 48 0f                  	leal	0xf(%r8), %ecx
     a01: c1 e9 04                     	shrl	$0x4, %ecx
     a04: 39 cb                        	cmpl	%ecx, %ebx
     a06: 0f 93 c2                     	setae	%dl
     a09: 45 85 f6                     	testl	%r14d, %r14d
     a0c: 41 0f 94 c1                  	sete	%r9b
     a10: 85 db                        	testl	%ebx, %ebx
     a12: 41 0f 94 c2                  	sete	%r10b
     a16: 41 08 d2                     	orb	%dl, %r10b
     a19: 31 c0                        	xorl	%eax, %eax
     a1b: 45 08 ca                     	orb	%r9b, %r10b
     a1e: 75 36                        	jne	0xa56 <tide_task_Shift+0xa56>
     a20: 31 c0                        	xorl	%eax, %eax
     a22: 89 da                        	movl	%ebx, %edx
     a24: eb 07                        	jmp	0xa2d <tide_task_Shift+0xa2d>
     a26: 44 89 d2                     	movl	%r10d, %edx
     a29: 39 d0                        	cmpl	%edx, %eax
     a2b: 73 29                        	jae	0xa56 <tide_task_Shift+0xa56>
     a2d: 41 89 d1                     	movl	%edx, %r9d
     a30: 41 29 c1                     	subl	%eax, %r9d
     a33: 41 d1 e9                     	shrl	%r9d
     a36: 45 8d 14 01                  	leal	(%r9,%rax), %r10d
     a3a: 4c 8b 9c 24 e0 00 00 00      	movq	0xe0(%rsp), %r11
     a42: 47 0f b7 5c 53 0a            	movzwl	0xa(%r11,%r10,2), %r11d
     a48: 45 39 de                     	cmpl	%r11d, %r14d
     a4b: 76 d9                        	jbe	0xa26 <tide_task_Shift+0xa26>
     a4d: 44 01 c8                     	addl	%r9d, %eax
     a50: ff c0                        	incl	%eax
     a52: 39 d0                        	cmpl	%edx, %eax
     a54: 72 d7                        	jb	0xa2d <tide_task_Shift+0xa2d>
     a56: 39 cb                        	cmpl	%ecx, %ebx
     a58: 72 22                        	jb	0xa7c <tide_task_Shift+0xa7c>
     a5a: 45 85 f6                     	testl	%r14d, %r14d
     a5d: 75 6e                        	jne	0xacd <tide_task_Shift+0xacd>
     a5f: 31 d2                        	xorl	%edx, %edx
     a61: b9 00 00 00 00               	movl	$0x0, %ecx
     a66: 45 39 e0                     	cmpl	%r12d, %r8d
     a69: 4c 8b 9c 24 e8 00 00 00      	movq	0xe8(%rsp), %r11
     a71: 0f 84 e6 f7 ff ff            	je	0x25d <tide_task_Shift+0x25d>
     a77: e9 fd f6 ff ff               	jmp	0x179 <tide_task_Shift+0x179>
     a7c: 45 39 e0                     	cmpl	%r12d, %r8d
     a7f: 75 04                        	jne	0xa85 <tide_task_Shift+0xa85>
     a81: 89 da                        	movl	%ebx, %edx
     a83: eb 06                        	jmp	0xa8b <tide_task_Shift+0xa8b>
     a85: 31 d2                        	xorl	%edx, %edx
     a87: 85 db                        	testl	%ebx, %ebx
     a89: 75 1c                        	jne	0xaa7 <tide_task_Shift+0xaa7>
     a8b: 89 d3                        	movl	%edx, %ebx
     a8d: 29 c3                        	subl	%eax, %ebx
     a8f: b1 01                        	movb	$0x1, %cl
     a91: 4c 8b 9c 24 e8 00 00 00      	movq	0xe8(%rsp), %r11
     a99: e9 bf f7 ff ff               	jmp	0x25d <tide_task_Shift+0x25d>
     a9e: 89 d3                        	movl	%edx, %ebx
     aa0: 4c 89 ca                     	movq	%r9, %rdx
     aa3: 39 da                        	cmpl	%ebx, %edx
     aa5: 73 e4                        	jae	0xa8b <tide_task_Shift+0xa8b>
     aa7: 89 d9                        	movl	%ebx, %ecx
     aa9: 29 d1                        	subl	%edx, %ecx
     aab: d1 e9                        	shrl	%ecx
     aad: 49 89 d1                     	movq	%rdx, %r9
     ab0: 01 ca                        	addl	%ecx, %edx
     ab2: 4c 8b 84 24 e0 00 00 00      	movq	0xe0(%rsp), %r8
     aba: 45 0f b7 44 50 0a            	movzwl	0xa(%r8,%rdx,2), %r8d
     ac0: 45 39 c4                     	cmpl	%r8d, %r12d
     ac3: 76 d9                        	jbe	0xa9e <tide_task_Shift+0xa9e>
     ac5: 41 01 c9                     	addl	%ecx, %r9d
     ac8: 41 ff c1                     	incl	%r9d
     acb: eb d3                        	jmp	0xaa0 <tide_task_Shift+0xaa0>
     acd: 4c 8b 9c 24 e8 00 00 00      	movq	0xe8(%rsp), %r11
     ad5: e9 9f f6 ff ff               	jmp	0x179 <tide_task_Shift+0x179>
