
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/nodiv/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec 58 02 00 00         	subq	$0x258, %rsp            # imm = 0x258
      13: 44 0f 29 9c 24 40 02 00 00   	movaps	%xmm11, 0x240(%rsp)
      1c: 44 0f 29 94 24 30 02 00 00   	movaps	%xmm10, 0x230(%rsp)
      25: 44 0f 29 8c 24 20 02 00 00   	movaps	%xmm9, 0x220(%rsp)
      2e: 44 0f 29 84 24 10 02 00 00   	movaps	%xmm8, 0x210(%rsp)
      37: 0f 29 bc 24 00 02 00 00      	movaps	%xmm7, 0x200(%rsp)
      3f: 0f 29 b4 24 f0 01 00 00      	movaps	%xmm6, 0x1f0(%rsp)
      47: 45 89 c7                     	movl	%r8d, %r15d
      4a: 89 d7                        	movl	%edx, %edi
      4c: 48 89 ce                     	movq	%rcx, %rsi
      4f: 31 c9                        	xorl	%ecx, %ecx
      51: 41 83 f8 01                  	cmpl	$0x1, %r8d
      55: 74 0b                        	je	0x62 <tide_task_Shift+0x62>
      57: 89 f8                        	movl	%edi, %eax
      59: 31 d2                        	xorl	%edx, %edx
      5b: 41 f7 f7                     	divl	%r15d
      5e: 89 d1                        	movl	%edx, %ecx
      60: 89 c7                        	movl	%eax, %edi
      62: 3b 7e 54                     	cmpl	0x54(%rsi), %edi
      65: 0f 83 33 09 00 00            	jae	0x99e <tide_task_Shift+0x99e>
      6b: 48 8d 46 50                  	leaq	0x50(%rsi), %rax
      6f: 8b 00                        	movl	(%rax), %eax
      71: 89 fa                        	movl	%edi, %edx
      73: c1 e2 0b                     	shll	$0xb, %edx
      76: 29 d0                        	subl	%edx, %eax
      78: 3d 00 08 00 00               	cmpl	$0x800, %eax            # imm = 0x800
      7d: 41 b9 00 08 00 00            	movl	$0x800, %r9d            # imm = 0x800
      83: 44 0f 42 c8                  	cmovbl	%eax, %r9d
      87: 41 bd 00 00 00 00            	movl	$0x0, %r13d
      8d: 44 89 cd                     	movl	%r9d, %ebp
      90: 41 83 ff 01                  	cmpl	$0x1, %r15d
      94: 74 4d                        	je	0xe3 <tide_task_Shift+0xe3>
      96: 45 89 c8                     	movl	%r9d, %r8d
      99: 89 c8                        	movl	%ecx, %eax
      9b: 49 0f af c0                  	imulq	%r8, %rax
      9f: 45 89 fa                     	movl	%r15d, %r10d
      a2: 48 89 c2                     	movq	%rax, %rdx
      a5: 48 c1 ea 20                  	shrq	$0x20, %rdx
      a9: 74 0a                        	je	0xb5 <tide_task_Shift+0xb5>
      ab: 31 d2                        	xorl	%edx, %edx
      ad: 49 f7 f2                     	divq	%r10
      b0: 49 89 c5                     	movq	%rax, %r13
      b3: eb 08                        	jmp	0xbd <tide_task_Shift+0xbd>
      b5: 31 d2                        	xorl	%edx, %edx
      b7: 41 f7 f2                     	divl	%r10d
      ba: 41 89 c5                     	movl	%eax, %r13d
      bd: ff c1                        	incl	%ecx
      bf: 4c 0f af c1                  	imulq	%rcx, %r8
      c3: 4c 89 c0                     	movq	%r8, %rax
      c6: 48 c1 e8 20                  	shrq	$0x20, %rax
      ca: 74 0d                        	je	0xd9 <tide_task_Shift+0xd9>
      cc: 4c 89 c0                     	movq	%r8, %rax
      cf: 31 d2                        	xorl	%edx, %edx
      d1: 49 f7 f2                     	divq	%r10
      d4: 48 89 c5                     	movq	%rax, %rbp
      d7: eb 0a                        	jmp	0xe3 <tide_task_Shift+0xe3>
      d9: 44 89 c0                     	movl	%r8d, %eax
      dc: 31 d2                        	xorl	%edx, %edx
      de: 41 f7 f2                     	divl	%r10d
      e1: 89 c5                        	movl	%eax, %ebp
      e3: 48 8b 46 60                  	movq	0x60(%rsi), %rax
      e7: 41 89 fc                     	movl	%edi, %r12d
      ea: 41 c1 e4 04                  	shll	$0x4, %r12d
      ee: 4a 8b 4c e0 70               	movq	0x70(%rax,%r12,8), %rcx
      f3: 4c 8d 71 20                  	leaq	0x20(%rcx), %r14
      f7: 48 c7 84 24 88 01 00 00 0e 00 00 00  	movq	$0xe, 0x188(%rsp)
     103: 48 8b 96 a0 00 00 00         	movq	0xa0(%rsi), %rdx
     10a: 48 85 d2                     	testq	%rdx, %rdx
     10d: 0f 84 85 00 00 00            	je	0x198 <tide_task_Shift+0x198>
     113: 83 3a 01                     	cmpl	$0x1, (%rdx)
     116: 75 66                        	jne	0x17e <tide_task_Shift+0x17e>
     118: 83 7a 04 00                  	cmpl	$0x0, 0x4(%rdx)
     11c: 74 60                        	je	0x17e <tide_task_Shift+0x17e>
     11e: 48 8b 52 08                  	movq	0x8(%rdx), %rdx
     122: 83 3a 01                     	cmpl	$0x1, (%rdx)
     125: 75 57                        	jne	0x17e <tide_task_Shift+0x17e>
     127: 83 7a 04 0b                  	cmpl	$0xb, 0x4(%rdx)
     12b: 75 51                        	jne	0x17e <tide_task_Shift+0x17e>
     12d: 83 7a 08 01                  	cmpl	$0x1, 0x8(%rdx)
     131: 75 4b                        	jne	0x17e <tide_task_Shift+0x17e>
     133: 3b 7a 0c                     	cmpl	0xc(%rdx), %edi
     136: 73 46                        	jae	0x17e <tide_task_Shift+0x17e>
     138: 4c 8b 42 10                  	movq	0x10(%rdx), %r8
     13c: 4d 8b 00                     	movq	(%r8), %r8
     13f: 4c 3b 84 24 88 01 00 00      	cmpq	0x188(%rsp), %r8
     147: 75 35                        	jne	0x17e <tide_task_Shift+0x17e>
     149: 4c 8b 5a 20                  	movq	0x20(%rdx), %r11
     14d: 41 89 f8                     	movl	%edi, %r8d
     150: 4d 69 d0 0c 01 00 00         	imulq	$0x10c, %r8, %r10       # imm = 0x10C
     157: 43 80 7c 13 08 01            	cmpb	$0x1, 0x8(%r11,%r10)
     15d: 75 1f                        	jne	0x17e <tide_task_Shift+0x17e>
     15f: 4d 01 d3                     	addq	%r10, %r11
     162: 45 39 0b                     	cmpl	%r9d, (%r11)
     165: 75 17                        	jne	0x17e <tide_task_Shift+0x17e>
     167: 48 8b 52 18                  	movq	0x18(%rdx), %rdx
     16b: 4a 8b 14 c2                  	movq	(%rdx,%r8,8), %rdx
     16f: 49 c1 e0 07                  	shlq	$0x7, %r8
     173: 4a 3b 54 00 70               	cmpq	0x70(%rax,%r8), %rdx
     178: 0f 84 68 08 00 00            	je	0x9e6 <tide_task_Shift+0x9e6>
     17e: 48 c7 84 24 e0 00 00 00 00 00 00 00  	movq	$0x0, 0xe0(%rsp)
     18a: 48 c7 84 24 e8 00 00 00 00 00 00 00  	movq	$0x0, 0xe8(%rsp)
     196: eb 18                        	jmp	0x1b0 <tide_task_Shift+0x1b0>
     198: 48 c7 84 24 e8 00 00 00 00 00 00 00  	movq	$0x0, 0xe8(%rsp)
     1a4: 48 c7 84 24 e0 00 00 00 00 00 00 00  	movq	$0x0, 0xe0(%rsp)
     1b0: 44 39 ed                     	cmpl	%r13d, %ebp
     1b3: 76 1b                        	jbe	0x1d0 <tide_task_Shift+0x1d0>
     1b5: 45 89 eb                     	movl	%r13d, %r11d
     1b8: 89 ea                        	movl	%ebp, %edx
     1ba: 49 89 d1                     	movq	%rdx, %r9
     1bd: 4d 29 d9                     	subq	%r11, %r9
     1c0: 31 db                        	xorl	%ebx, %ebx
     1c2: 49 83 f9 08                  	cmpq	$0x8, %r9
     1c6: 73 26                        	jae	0x1ee <tide_task_Shift+0x1ee>
     1c8: 4d 89 d8                     	movq	%r11, %r8
     1cb: e9 c0 00 00 00               	jmp	0x290 <tide_task_Shift+0x290>
     1d0: 48 c7 84 24 08 01 00 00 00 00 00 00  	movq	$0x0, 0x108(%rsp)
     1dc: c7 84 24 00 01 00 00 00 00 00 00     	movl	$0x0, 0x100(%rsp)
     1e7: 31 db                        	xorl	%ebx, %ebx
     1e9: e9 c9 00 00 00               	jmp	0x2b7 <tide_task_Shift+0x2b7>
     1ee: 4d 89 ca                     	movq	%r9, %r10
     1f1: 49 83 e2 f8                  	andq	$-0x8, %r10
     1f5: 4f 8d 04 1a                  	leaq	(%r10,%r11), %r8
     1f9: 4c 01 d9                     	addq	%r11, %rcx
     1fc: 48 83 c1 24                  	addq	$0x24, %rcx
     200: 66 0f ef c0                  	pxor	%xmm0, %xmm0
     204: 45 31 db                     	xorl	%r11d, %r11d
     207: 66 0f 6f 1d 00 00 00 00      	movdqa	(%rip), %xmm3           # 0x20f <tide_task_Shift+0x20f>
     20f: 66 0f ef d2                  	pxor	%xmm2, %xmm2
     213: 66 0f ef c9                  	pxor	%xmm1, %xmm1
     217: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     220: 66 42 0f 6e 64 19 fc         	movd	-0x4(%rcx,%r11), %xmm4
     227: 66 42 0f 6e 2c 19            	movd	(%rcx,%r11), %xmm5
     22d: 66 0f 74 e0                  	pcmpeqb	%xmm0, %xmm4
     231: 66 0f 60 e4                  	punpcklbw	%xmm4, %xmm4    # xmm4 = xmm4[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     235: 66 0f 61 e4                  	punpcklwd	%xmm4, %xmm4    # xmm4 = xmm4[0,0,1,1,2,2,3,3]
     239: 66 0f db e3                  	pand	%xmm3, %xmm4
     23d: 66 0f fe d4                  	paddd	%xmm4, %xmm2
     241: 66 0f 74 e8                  	pcmpeqb	%xmm0, %xmm5
     245: 66 0f 60 ed                  	punpcklbw	%xmm5, %xmm5    # xmm5 = xmm5[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     249: 66 0f 61 ed                  	punpcklwd	%xmm5, %xmm5    # xmm5 = xmm5[0,0,1,1,2,2,3,3]
     24d: 66 0f db eb                  	pand	%xmm3, %xmm5
     251: 66 0f fe cd                  	paddd	%xmm5, %xmm1
     255: 49 83 c3 08                  	addq	$0x8, %r11
     259: 4d 39 da                     	cmpq	%r11, %r10
     25c: 75 c2                        	jne	0x220 <tide_task_Shift+0x220>
     25e: 66 0f fe ca                  	paddd	%xmm2, %xmm1
     262: 66 0f 70 c1 ee               	pshufd	$0xee, %xmm1, %xmm0     # xmm0 = xmm1[2,3,2,3]
     267: 66 0f fe c1                  	paddd	%xmm1, %xmm0
     26b: 66 0f 70 c8 55               	pshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
     270: 66 0f fe c8                  	paddd	%xmm0, %xmm1
     274: 66 0f 7e cb                  	movd	%xmm1, %ebx
     278: 48 c7 84 24 08 01 00 00 00 00 00 00  	movq	$0x0, 0x108(%rsp)
     284: 4d 39 d1                     	cmpq	%r10, %r9
     287: 74 23                        	je	0x2ac <tide_task_Shift+0x2ac>
     289: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     290: 43 80 3c 06 01               	cmpb	$0x1, (%r14,%r8)
     295: 83 d3 00                     	adcl	$0x0, %ebx
     298: 49 ff c0                     	incq	%r8
     29b: 4c 39 c2                     	cmpq	%r8, %rdx
     29e: 75 f0                        	jne	0x290 <tide_task_Shift+0x290>
     2a0: 48 c7 84 24 08 01 00 00 00 00 00 00  	movq	$0x0, 0x108(%rsp)
     2ac: c7 84 24 00 01 00 00 00 00 00 00     	movl	$0x0, 0x100(%rsp)
     2b7: 41 83 ff 01                  	cmpl	$0x1, %r15d
     2bb: 75 08                        	jne	0x2c5 <tide_task_Shift+0x2c5>
     2bd: 85 db                        	testl	%ebx, %ebx
     2bf: 0f 84 d9 06 00 00            	je	0x99e <tide_task_Shift+0x99e>
     2c5: 4c 89 b4 24 10 01 00 00      	movq	%r14, 0x110(%rsp)
     2cd: 41 83 ff 01                  	cmpl	$0x1, %r15d
     2d1: 0f 85 2c 01 00 00            	jne	0x403 <tide_task_Shift+0x403>
     2d7: 48 89 ac 24 f0 00 00 00      	movq	%rbp, 0xf0(%rsp)
     2df: 44 89 e5                     	movl	%r12d, %ebp
     2e2: 48 8b 4c e8 10               	movq	0x10(%rax,%rbp,8), %rcx
     2e7: 41 b6 01                     	movb	$0x1, %r14b
     2ea: 83 39 01                     	cmpl	$0x1, (%rcx)
     2ed: 44 89 bc 24 fc 00 00 00      	movl	%r15d, 0xfc(%rsp)
     2f5: 4c 89 a4 24 20 01 00 00      	movq	%r12, 0x120(%rsp)
     2fd: 4c 89 ac 24 18 01 00 00      	movq	%r13, 0x118(%rsp)
     305: 75 70                        	jne	0x377 <tide_task_Shift+0x377>
     307: 48 8b 4c e8 18               	movq	0x18(%rax,%rbp,8), %rcx
     30c: 83 39 01                     	cmpl	$0x1, (%rcx)
     30f: 75 66                        	jne	0x377 <tide_task_Shift+0x377>
     311: 48 8b 4c e8 20               	movq	0x20(%rax,%rbp,8), %rcx
     316: 83 39 01                     	cmpl	$0x1, (%rcx)
     319: 75 5c                        	jne	0x377 <tide_task_Shift+0x377>
     31b: 48 8b 4c e8 28               	movq	0x28(%rax,%rbp,8), %rcx
     320: 83 39 01                     	cmpl	$0x1, (%rcx)
     323: 75 52                        	jne	0x377 <tide_task_Shift+0x377>
     325: 48 8b 4c e8 30               	movq	0x30(%rax,%rbp,8), %rcx
     32a: 83 39 01                     	cmpl	$0x1, (%rcx)
     32d: 75 48                        	jne	0x377 <tide_task_Shift+0x377>
     32f: 48 8b 4c e8 38               	movq	0x38(%rax,%rbp,8), %rcx
     334: 83 39 01                     	cmpl	$0x1, (%rcx)
     337: 75 3e                        	jne	0x377 <tide_task_Shift+0x377>
     339: 48 8b 4c e8 40               	movq	0x40(%rax,%rbp,8), %rcx
     33e: 83 39 01                     	cmpl	$0x1, (%rcx)
     341: 75 34                        	jne	0x377 <tide_task_Shift+0x377>
     343: 48 8b 4c e8 48               	movq	0x48(%rax,%rbp,8), %rcx
     348: 83 39 01                     	cmpl	$0x1, (%rcx)
     34b: 75 2a                        	jne	0x377 <tide_task_Shift+0x377>
     34d: 48 8b 4c e8 50               	movq	0x50(%rax,%rbp,8), %rcx
     352: 83 39 01                     	cmpl	$0x1, (%rcx)
     355: 75 20                        	jne	0x377 <tide_task_Shift+0x377>
     357: 48 8b 4c e8 58               	movq	0x58(%rax,%rbp,8), %rcx
     35c: 83 39 01                     	cmpl	$0x1, (%rcx)
     35f: 75 16                        	jne	0x377 <tide_task_Shift+0x377>
     361: 48 8b 4c e8 60               	movq	0x60(%rax,%rbp,8), %rcx
     366: 83 39 01                     	cmpl	$0x1, (%rcx)
     369: 75 0c                        	jne	0x377 <tide_task_Shift+0x377>
     36b: 48 8b 44 e8 68               	movq	0x68(%rax,%rbp,8), %rax
     370: 83 38 01                     	cmpl	$0x1, (%rax)
     373: 41 0f 95 c6                  	setne	%r14b
     377: 48 c1 e5 03                  	shlq	$0x3, %rbp
     37b: 41 bd 02 00 00 00            	movl	$0x2, %r13d
     381: 45 31 e4                     	xorl	%r12d, %r12d
     384: 4c 8d 3d 00 00 00 00         	leaq	(%rip), %r15            # 0x38b <tide_task_Shift+0x38b>
     38b: eb 43                        	jmp	0x3d0 <tide_task_Shift+0x3d0>
     38d: 0f 1f 00                     	nopl	(%rax)
     390: 4a 8d 04 24                  	leaq	(%rsp,%r12), %rax
     394: 48 05 28 01 00 00            	addq	$0x128, %rax            # imm = 0x128
     39a: 4a 8d 8c 24 90 01 00 00      	leaq	0x190(%rsp,%r12), %rcx
     3a2: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     3a7: 48 89 4c 24 28               	movq	%rcx, 0x28(%rsp)
     3ac: 44 88 74 24 20               	movb	%r14b, 0x20(%rsp)
     3b1: 48 8d 4e 50                  	leaq	0x50(%rsi), %rcx
     3b5: 4c 89 fa                     	movq	%r15, %rdx
     3b8: 41 89 f8                     	movl	%edi, %r8d
     3bb: 45 89 e9                     	movl	%r13d, %r9d
     3be: e8 00 00 00 00               	callq	0x3c3 <tide_task_Shift+0x3c3>
     3c3: 49 83 c4 08                  	addq	$0x8, %r12
     3c7: 41 ff c5                     	incl	%r13d
     3ca: 49 83 fc 60                  	cmpq	$0x60, %r12
     3ce: 74 73                        	je	0x443 <tide_task_Shift+0x443>
     3d0: 45 84 f6                     	testb	%r14b, %r14b
     3d3: 75 bb                        	jne	0x390 <tide_task_Shift+0x390>
     3d5: 48 8b 46 60                  	movq	0x60(%rsi), %rax
     3d9: 48 01 e8                     	addq	%rbp, %rax
     3dc: 49 8b 44 04 10               	movq	0x10(%r12,%rax), %rax
     3e1: 83 38 01                     	cmpl	$0x1, (%rax)
     3e4: 75 aa                        	jne	0x390 <tide_task_Shift+0x390>
     3e6: c7 40 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rax)  # imm = 0xFFFFFFFF
     3ed: 48 83 c0 20                  	addq	$0x20, %rax
     3f1: 4a 89 84 24 28 01 00 00      	movq	%rax, 0x128(%rsp,%r12)
     3f9: 4a 89 84 24 90 01 00 00      	movq	%rax, 0x190(%rsp,%r12)
     401: eb c0                        	jmp	0x3c3 <tide_task_Shift+0x3c3>
     403: 48 8d 84 24 28 01 00 00      	leaq	0x128(%rsp), %rax
     40b: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     410: 48 8d 84 24 90 01 00 00      	leaq	0x190(%rsp), %rax
     418: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
     41d: c7 44 24 20 0c 00 00 00      	movl	$0xc, 0x20(%rsp)
     425: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x42c <tide_task_Shift+0x42c>
     42c: 48 8d 4e 50                  	leaq	0x50(%rsi), %rcx
     430: 41 89 f8                     	movl	%edi, %r8d
     433: 41 b9 02 00 00 00            	movl	$0x2, %r9d
     439: e8 00 00 00 00               	callq	0x43e <tide_task_Shift+0x43e>
     43e: 41 89 c6                     	movl	%eax, %r14d
     441: eb 20                        	jmp	0x463 <tide_task_Shift+0x463>
     443: 44 8b bc 24 fc 00 00 00      	movl	0xfc(%rsp), %r15d
     44b: 4c 8b a4 24 20 01 00 00      	movq	0x120(%rsp), %r12
     453: 4c 8b ac 24 18 01 00 00      	movq	0x118(%rsp), %r13
     45b: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     463: 89 e8                        	movl	%ebp, %eax
     465: 44 29 e8                     	subl	%r13d, %eax
     468: 45 84 f6                     	testb	%r14b, %r14b
     46b: 0f 84 e0 00 00 00            	je	0x551 <tide_task_Shift+0x551>
     471: 39 d8                        	cmpl	%ebx, %eax
     473: 0f 85 d8 00 00 00            	jne	0x551 <tide_task_Shift+0x551>
     479: f3 0f 6f 84 24 78 01 00 00   	movdqu	0x178(%rsp), %xmm0
     482: f3 0f 6f 8c 24 68 01 00 00   	movdqu	0x168(%rsp), %xmm1
     48b: f3 0f 6f 94 24 28 01 00 00   	movdqu	0x128(%rsp), %xmm2
     494: f3 0f 6f 9c 24 38 01 00 00   	movdqu	0x138(%rsp), %xmm3
     49d: f3 0f 6f a4 24 48 01 00 00   	movdqu	0x148(%rsp), %xmm4
     4a6: f3 0f 6f ac 24 58 01 00 00   	movdqu	0x158(%rsp), %xmm5
     4af: 48 8b 84 24 e8 01 00 00      	movq	0x1e8(%rsp), %rax
     4b7: 0f 10 b4 24 d8 01 00 00      	movups	0x1d8(%rsp), %xmm6
     4bf: 0f 10 bc 24 c8 01 00 00      	movups	0x1c8(%rsp), %xmm7
     4c7: 44 0f 10 84 24 b8 01 00 00   	movups	0x1b8(%rsp), %xmm8
     4d0: 44 0f 10 8c 24 a8 01 00 00   	movups	0x1a8(%rsp), %xmm9
     4d9: 44 0f 10 94 24 98 01 00 00   	movups	0x198(%rsp), %xmm10
     4e2: 4c 8b 8c 24 90 01 00 00      	movq	0x190(%rsp), %r9
     4ea: f3 0f 7f 84 24 c8 00 00 00   	movdqu	%xmm0, 0xc8(%rsp)
     4f3: f3 0f 7f 8c 24 b8 00 00 00   	movdqu	%xmm1, 0xb8(%rsp)
     4fc: f3 0f 7f ac 24 a8 00 00 00   	movdqu	%xmm5, 0xa8(%rsp)
     505: f3 0f 7f a4 24 98 00 00 00   	movdqu	%xmm4, 0x98(%rsp)
     50e: f3 0f 7f 9c 24 88 00 00 00   	movdqu	%xmm3, 0x88(%rsp)
     517: f3 0f 7f 54 24 78            	movdqu	%xmm2, 0x78(%rsp)
     51d: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     522: 0f 11 74 24 60               	movups	%xmm6, 0x60(%rsp)
     527: 0f 11 7c 24 50               	movups	%xmm7, 0x50(%rsp)
     52c: 44 0f 11 44 24 40            	movups	%xmm8, 0x40(%rsp)
     532: 44 0f 11 4c 24 30            	movups	%xmm9, 0x30(%rsp)
     538: 44 0f 11 54 24 20            	movups	%xmm10, 0x20(%rsp)
     53e: 48 89 f1                     	movq	%rsi, %rcx
     541: 44 89 ea                     	movl	%r13d, %edx
     544: 41 89 e8                     	movl	%ebp, %r8d
     547: e8 00 00 00 00               	callq	0x54c <tide_task_Shift+0x54c>
     54c: e9 ef 03 00 00               	jmp	0x940 <tide_task_Shift+0x940>
     551: 89 c1                        	movl	%eax, %ecx
     553: 29 d9                        	subl	%ebx, %ecx
     555: 75 73                        	jne	0x5ca <tide_task_Shift+0x5ca>
     557: 48 8b 84 24 80 01 00 00      	movq	0x180(%rsp), %rax
     55f: f3 0f 6f 84 24 70 01 00 00   	movdqu	0x170(%rsp), %xmm0
     568: f3 0f 6f 8c 24 60 01 00 00   	movdqu	0x160(%rsp), %xmm1
     571: f3 0f 6f 94 24 50 01 00 00   	movdqu	0x150(%rsp), %xmm2
     57a: f3 0f 6f 9c 24 40 01 00 00   	movdqu	0x140(%rsp), %xmm3
     583: f3 0f 6f a4 24 30 01 00 00   	movdqu	0x130(%rsp), %xmm4
     58c: 4c 8b 8c 24 28 01 00 00      	movq	0x128(%rsp), %r9
     594: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     599: f3 0f 7f 44 24 60            	movdqu	%xmm0, 0x60(%rsp)
     59f: f3 0f 7f 4c 24 50            	movdqu	%xmm1, 0x50(%rsp)
     5a5: f3 0f 7f 54 24 40            	movdqu	%xmm2, 0x40(%rsp)
     5ab: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     5b1: f3 0f 7f 64 24 20            	movdqu	%xmm4, 0x20(%rsp)
     5b7: 48 89 f1                     	movq	%rsi, %rcx
     5ba: 44 89 ea                     	movl	%r13d, %edx
     5bd: 41 89 e8                     	movl	%ebp, %r8d
     5c0: e8 00 00 00 00               	callq	0x5c5 <tide_task_Shift+0x5c5>
     5c5: e9 76 03 00 00               	jmp	0x940 <tide_task_Shift+0x940>
     5ca: 45 84 f6                     	testb	%r14b, %r14b
     5cd: 0f 84 dd 00 00 00            	je	0x6b0 <tide_task_Shift+0x6b0>
     5d3: f3 0f 6f 84 24 78 01 00 00   	movdqu	0x178(%rsp), %xmm0
     5dc: f3 0f 6f 8c 24 68 01 00 00   	movdqu	0x168(%rsp), %xmm1
     5e5: f3 0f 6f 94 24 28 01 00 00   	movdqu	0x128(%rsp), %xmm2
     5ee: f3 0f 6f 9c 24 38 01 00 00   	movdqu	0x138(%rsp), %xmm3
     5f7: f3 0f 6f a4 24 48 01 00 00   	movdqu	0x148(%rsp), %xmm4
     600: f3 0f 6f ac 24 58 01 00 00   	movdqu	0x158(%rsp), %xmm5
     609: 0f 10 b4 24 e0 01 00 00      	movups	0x1e0(%rsp), %xmm6
     611: 0f 10 bc 24 d0 01 00 00      	movups	0x1d0(%rsp), %xmm7
     619: 44 0f 10 84 24 90 01 00 00   	movups	0x190(%rsp), %xmm8
     622: 44 0f 10 8c 24 a0 01 00 00   	movups	0x1a0(%rsp), %xmm9
     62b: 44 0f 10 94 24 b0 01 00 00   	movups	0x1b0(%rsp), %xmm10
     634: 44 0f 10 9c 24 c0 01 00 00   	movups	0x1c0(%rsp), %xmm11
     63d: f3 0f 7f 84 24 d0 00 00 00   	movdqu	%xmm0, 0xd0(%rsp)
     646: f3 0f 7f 8c 24 c0 00 00 00   	movdqu	%xmm1, 0xc0(%rsp)
     64f: f3 0f 7f ac 24 b0 00 00 00   	movdqu	%xmm5, 0xb0(%rsp)
     658: f3 0f 7f a4 24 a0 00 00 00   	movdqu	%xmm4, 0xa0(%rsp)
     661: f3 0f 7f 9c 24 90 00 00 00   	movdqu	%xmm3, 0x90(%rsp)
     66a: f3 0f 7f 94 24 80 00 00 00   	movdqu	%xmm2, 0x80(%rsp)
     673: 0f 11 74 24 70               	movups	%xmm6, 0x70(%rsp)
     678: 0f 11 7c 24 60               	movups	%xmm7, 0x60(%rsp)
     67d: 44 0f 11 5c 24 50            	movups	%xmm11, 0x50(%rsp)
     683: 44 0f 11 54 24 40            	movups	%xmm10, 0x40(%rsp)
     689: 44 0f 11 4c 24 30            	movups	%xmm9, 0x30(%rsp)
     68f: 44 0f 11 44 24 20            	movups	%xmm8, 0x20(%rsp)
     695: 48 89 f1                     	movq	%rsi, %rcx
     698: 44 89 ea                     	movl	%r13d, %edx
     69b: 41 89 e8                     	movl	%ebp, %r8d
     69e: 4c 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %r9
     6a6: e8 00 00 00 00               	callq	0x6ab <tide_task_Shift+0x6ab>
     6ab: e9 90 02 00 00               	jmp	0x940 <tide_task_Shift+0x940>
     6b0: 80 bc 24 00 01 00 00 00      	cmpb	$0x0, 0x100(%rsp)
     6b8: 4c 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %r9
     6c0: 74 7f                        	je	0x741 <tide_task_Shift+0x741>
     6c2: f3 0f 6f 84 24 78 01 00 00   	movdqu	0x178(%rsp), %xmm0
     6cb: f3 0f 6f 8c 24 68 01 00 00   	movdqu	0x168(%rsp), %xmm1
     6d4: f3 0f 6f 94 24 28 01 00 00   	movdqu	0x128(%rsp), %xmm2
     6dd: f3 0f 6f 9c 24 38 01 00 00   	movdqu	0x138(%rsp), %xmm3
     6e6: f3 0f 6f a4 24 48 01 00 00   	movdqu	0x148(%rsp), %xmm4
     6ef: f3 0f 6f ac 24 58 01 00 00   	movdqu	0x158(%rsp), %xmm5
     6f8: f3 0f 7f 44 24 70            	movdqu	%xmm0, 0x70(%rsp)
     6fe: f3 0f 7f 4c 24 60            	movdqu	%xmm1, 0x60(%rsp)
     704: f3 0f 7f 6c 24 50            	movdqu	%xmm5, 0x50(%rsp)
     70a: f3 0f 7f 64 24 40            	movdqu	%xmm4, 0x40(%rsp)
     710: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     716: f3 0f 7f 54 24 20            	movdqu	%xmm2, 0x20(%rsp)
     71c: 48 89 f1                     	movq	%rsi, %rcx
     71f: 48 8b 94 24 e8 00 00 00      	movq	0xe8(%rsp), %rdx
     727: 4c 8b 84 24 08 01 00 00      	movq	0x108(%rsp), %r8
     72f: 4c 8b 8c 24 e0 00 00 00      	movq	0xe0(%rsp), %r9
     737: e8 00 00 00 00               	callq	0x73c <tide_task_Shift+0x73c>
     73c: e9 ff 01 00 00               	jmp	0x940 <tide_task_Shift+0x940>
     741: 8d 50 0f                     	leal	0xf(%rax), %edx
     744: c1 ea 04                     	shrl	$0x4, %edx
     747: 39 d3                        	cmpl	%edx, %ebx
     749: 73 6d                        	jae	0x7b8 <tide_task_Shift+0x7b8>
     74b: f3 0f 6f 84 24 78 01 00 00   	movdqu	0x178(%rsp), %xmm0
     754: f3 0f 6f 8c 24 68 01 00 00   	movdqu	0x168(%rsp), %xmm1
     75d: f3 0f 6f 94 24 28 01 00 00   	movdqu	0x128(%rsp), %xmm2
     766: f3 0f 6f 9c 24 38 01 00 00   	movdqu	0x138(%rsp), %xmm3
     76f: f3 0f 6f a4 24 48 01 00 00   	movdqu	0x148(%rsp), %xmm4
     778: f3 0f 6f ac 24 58 01 00 00   	movdqu	0x158(%rsp), %xmm5
     781: f3 0f 7f 44 24 70            	movdqu	%xmm0, 0x70(%rsp)
     787: f3 0f 7f 4c 24 60            	movdqu	%xmm1, 0x60(%rsp)
     78d: f3 0f 7f 6c 24 50            	movdqu	%xmm5, 0x50(%rsp)
     793: f3 0f 7f 64 24 40            	movdqu	%xmm4, 0x40(%rsp)
     799: f3 0f 7f 5c 24 30            	movdqu	%xmm3, 0x30(%rsp)
     79f: f3 0f 7f 54 24 20            	movdqu	%xmm2, 0x20(%rsp)
     7a5: 48 89 f1                     	movq	%rsi, %rcx
     7a8: 44 89 ea                     	movl	%r13d, %edx
     7ab: 41 89 e8                     	movl	%ebp, %r8d
     7ae: e8 00 00 00 00               	callq	0x7b3 <tide_task_Shift+0x7b3>
     7b3: e9 88 01 00 00               	jmp	0x940 <tide_task_Shift+0x940>
     7b8: 48 89 ac 24 f0 00 00 00      	movq	%rbp, 0xf0(%rsp)
     7c0: 4c 89 ac 24 18 01 00 00      	movq	%r13, 0x118(%rsp)
     7c8: 4c 89 a4 24 20 01 00 00      	movq	%r12, 0x120(%rsp)
     7d0: 44 89 bc 24 fc 00 00 00      	movl	%r15d, 0xfc(%rsp)
     7d8: 83 c0 03                     	addl	$0x3, %eax
     7db: c1 e8 02                     	shrl	$0x2, %eax
     7de: 4c 8b 84 24 80 01 00 00      	movq	0x180(%rsp), %r8
     7e6: 48 8b 94 24 78 01 00 00      	movq	0x178(%rsp), %rdx
     7ee: 48 8b ac 24 70 01 00 00      	movq	0x170(%rsp), %rbp
     7f6: 4c 8b ac 24 68 01 00 00      	movq	0x168(%rsp), %r13
     7fe: 4c 8b a4 24 60 01 00 00      	movq	0x160(%rsp), %r12
     806: 4c 8b bc 24 58 01 00 00      	movq	0x158(%rsp), %r15
     80e: 4c 8b b4 24 50 01 00 00      	movq	0x150(%rsp), %r14
     816: 4c 8b 9c 24 48 01 00 00      	movq	0x148(%rsp), %r11
     81e: 4c 8b 94 24 40 01 00 00      	movq	0x140(%rsp), %r10
     826: 4c 8b 8c 24 38 01 00 00      	movq	0x138(%rsp), %r9
     82e: 4c 89 8c 24 00 01 00 00      	movq	%r9, 0x100(%rsp)
     836: 4c 8b 8c 24 28 01 00 00      	movq	0x128(%rsp), %r9
     83e: 4c 89 8c 24 08 01 00 00      	movq	%r9, 0x108(%rsp)
     846: 4c 8b 8c 24 30 01 00 00      	movq	0x130(%rsp), %r9
     84e: 39 c3                        	cmpl	%eax, %ebx
     850: 72 72                        	jb	0x8c4 <tide_task_Shift+0x8c4>
     852: 39 c1                        	cmpl	%eax, %ecx
     854: 72 6e                        	jb	0x8c4 <tide_task_Shift+0x8c4>
     856: 4c 89 44 24 78               	movq	%r8, 0x78(%rsp)
     85b: 48 89 54 24 70               	movq	%rdx, 0x70(%rsp)
     860: 48 89 6c 24 68               	movq	%rbp, 0x68(%rsp)
     865: 4c 89 6c 24 60               	movq	%r13, 0x60(%rsp)
     86a: 4c 89 64 24 58               	movq	%r12, 0x58(%rsp)
     86f: 4c 89 7c 24 50               	movq	%r15, 0x50(%rsp)
     874: 4c 89 74 24 48               	movq	%r14, 0x48(%rsp)
     879: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
     87e: 4c 89 54 24 38               	movq	%r10, 0x38(%rsp)
     883: 48 8b 84 24 00 01 00 00      	movq	0x100(%rsp), %rax
     88b: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     890: 4c 89 4c 24 28               	movq	%r9, 0x28(%rsp)
     895: 48 8b 84 24 08 01 00 00      	movq	0x108(%rsp), %rax
     89d: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     8a2: 4c 8b 84 24 f0 00 00 00      	movq	0xf0(%rsp), %r8
     8aa: 4c 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %r9
     8b2: 48 89 f1                     	movq	%rsi, %rcx
     8b5: 48 8b 94 24 18 01 00 00      	movq	0x118(%rsp), %rdx
     8bd: e8 00 00 00 00               	callq	0x8c2 <tide_task_Shift+0x8c2>
     8c2: eb 6c                        	jmp	0x930 <tide_task_Shift+0x930>
     8c4: 4c 89 44 24 78               	movq	%r8, 0x78(%rsp)
     8c9: 48 89 54 24 70               	movq	%rdx, 0x70(%rsp)
     8ce: 48 89 6c 24 68               	movq	%rbp, 0x68(%rsp)
     8d3: 4c 89 6c 24 60               	movq	%r13, 0x60(%rsp)
     8d8: 4c 89 64 24 58               	movq	%r12, 0x58(%rsp)
     8dd: 4c 89 7c 24 50               	movq	%r15, 0x50(%rsp)
     8e2: 4c 89 74 24 48               	movq	%r14, 0x48(%rsp)
     8e7: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
     8ec: 4c 89 54 24 38               	movq	%r10, 0x38(%rsp)
     8f1: 48 8b 84 24 00 01 00 00      	movq	0x100(%rsp), %rax
     8f9: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     8fe: 4c 89 4c 24 28               	movq	%r9, 0x28(%rsp)
     903: 48 8b 84 24 08 01 00 00      	movq	0x108(%rsp), %rax
     90b: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     910: 4c 8b 84 24 f0 00 00 00      	movq	0xf0(%rsp), %r8
     918: 4c 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %r9
     920: 48 89 f1                     	movq	%rsi, %rcx
     923: 48 8b 94 24 18 01 00 00      	movq	0x118(%rsp), %rdx
     92b: e8 00 00 00 00               	callq	0x930 <tide_task_Shift+0x930>
     930: 44 8b bc 24 fc 00 00 00      	movl	0xfc(%rsp), %r15d
     938: 4c 8b a4 24 20 01 00 00      	movq	0x120(%rsp), %r12
     940: 41 83 ff 01                  	cmpl	$0x1, %r15d
     944: 75 58                        	jne	0x99e <tide_task_Shift+0x99e>
     946: 44 89 e3                     	movl	%r12d, %ebx
     949: 48 c1 e3 03                  	shlq	$0x3, %rbx
     94d: 41 be 0d 00 00 00            	movl	$0xd, %r14d
     953: 4c 8d 3d 00 00 00 00         	leaq	(%rip), %r15            # 0x95a <tide_task_Shift+0x95a>
     95a: eb 0d                        	jmp	0x969 <tide_task_Shift+0x969>
     95c: 0f 1f 40 00                  	nopl	(%rax)
     960: 49 ff ce                     	decq	%r14
     963: 49 83 fe 01                  	cmpq	$0x1, %r14
     967: 74 35                        	je	0x99e <tide_task_Shift+0x99e>
     969: 4a 8b 84 f4 18 01 00 00      	movq	0x118(%rsp,%r14,8), %rax
     971: 48 8b 4e 60                  	movq	0x60(%rsi), %rcx
     975: 48 01 d9                     	addq	%rbx, %rcx
     978: 4a 8b 0c f1                  	movq	(%rcx,%r14,8), %rcx
     97c: 48 83 c1 20                  	addq	$0x20, %rcx
     980: 48 39 c8                     	cmpq	%rcx, %rax
     983: 74 db                        	je	0x960 <tide_task_Shift+0x960>
     985: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     98a: 48 8d 4e 50                  	leaq	0x50(%rsi), %rcx
     98e: 4c 89 fa                     	movq	%r15, %rdx
     991: 41 89 f8                     	movl	%edi, %r8d
     994: 45 89 f1                     	movl	%r14d, %r9d
     997: e8 00 00 00 00               	callq	0x99c <tide_task_Shift+0x99c>
     99c: eb c2                        	jmp	0x960 <tide_task_Shift+0x960>
     99e: 0f 28 b4 24 f0 01 00 00      	movaps	0x1f0(%rsp), %xmm6
     9a6: 0f 28 bc 24 00 02 00 00      	movaps	0x200(%rsp), %xmm7
     9ae: 44 0f 28 84 24 10 02 00 00   	movaps	0x210(%rsp), %xmm8
     9b7: 44 0f 28 8c 24 20 02 00 00   	movaps	0x220(%rsp), %xmm9
     9c0: 44 0f 28 94 24 30 02 00 00   	movaps	0x230(%rsp), %xmm10
     9c9: 44 0f 28 9c 24 40 02 00 00   	movaps	0x240(%rsp), %xmm11
     9d2: 48 81 c4 58 02 00 00         	addq	$0x258, %rsp            # imm = 0x258
     9d9: 5b                           	popq	%rbx
     9da: 5d                           	popq	%rbp
     9db: 5f                           	popq	%rdi
     9dc: 5e                           	popq	%rsi
     9dd: 41 5c                        	popq	%r12
     9df: 41 5d                        	popq	%r13
     9e1: 41 5e                        	popq	%r14
     9e3: 41 5f                        	popq	%r15
     9e5: c3                           	retq
     9e6: 4c 89 b4 24 10 01 00 00      	movq	%r14, 0x110(%rsp)
     9ee: 48 89 ac 24 f0 00 00 00      	movq	%rbp, 0xf0(%rsp)
     9f6: 4c 89 9c 24 e0 00 00 00      	movq	%r11, 0xe0(%rsp)
     9fe: 41 8b 5b 04                  	movl	0x4(%r11), %ebx
     a02: 41 8d 51 0f                  	leal	0xf(%r9), %edx
     a06: c1 ea 04                     	shrl	$0x4, %edx
     a09: 39 d3                        	cmpl	%edx, %ebx
     a0b: 41 0f 93 c0                  	setae	%r8b
     a0f: 45 85 ed                     	testl	%r13d, %r13d
     a12: 41 0f 94 c2                  	sete	%r10b
     a16: 85 db                        	testl	%ebx, %ebx
     a18: 41 0f 94 c3                  	sete	%r11b
     a1c: 45 08 d3                     	orb	%r10b, %r11b
     a1f: 45 31 f6                     	xorl	%r14d, %r14d
     a22: 45 08 c3                     	orb	%r8b, %r11b
     a25: 75 3b                        	jne	0xa62 <tide_task_Shift+0xa62>
     a27: 45 31 f6                     	xorl	%r14d, %r14d
     a2a: 41 89 d8                     	movl	%ebx, %r8d
     a2d: eb 08                        	jmp	0xa37 <tide_task_Shift+0xa37>
     a2f: 45 89 d8                     	movl	%r11d, %r8d
     a32: 45 39 c6                     	cmpl	%r8d, %r14d
     a35: 73 2b                        	jae	0xa62 <tide_task_Shift+0xa62>
     a37: 45 89 c2                     	movl	%r8d, %r10d
     a3a: 45 29 f2                     	subl	%r14d, %r10d
     a3d: 41 d1 ea                     	shrl	%r10d
     a40: 47 8d 1c 32                  	leal	(%r10,%r14), %r11d
     a44: 48 8b ac 24 e0 00 00 00      	movq	0xe0(%rsp), %rbp
     a4c: 42 0f b7 6c 5d 0a            	movzwl	0xa(%rbp,%r11,2), %ebp
     a52: 41 39 ed                     	cmpl	%ebp, %r13d
     a55: 76 d8                        	jbe	0xa2f <tide_task_Shift+0xa2f>
     a57: 45 01 d6                     	addl	%r10d, %r14d
     a5a: 41 ff c6                     	incl	%r14d
     a5d: 45 39 c6                     	cmpl	%r8d, %r14d
     a60: 72 d5                        	jb	0xa37 <tide_task_Shift+0xa37>
     a62: 4c 89 b4 24 e8 00 00 00      	movq	%r14, 0xe8(%rsp)
     a6a: 39 d3                        	cmpl	%edx, %ebx
     a6c: 72 3a                        	jb	0xaa8 <tide_task_Shift+0xaa8>
     a6e: 45 85 ed                     	testl	%r13d, %r13d
     a71: 0f 85 86 00 00 00            	jne	0xafd <tide_task_Shift+0xafd>
     a77: 48 c7 84 24 08 01 00 00 00 00 00 00  	movq	$0x0, 0x108(%rsp)
     a83: c7 84 24 00 01 00 00 00 00 00 00     	movl	$0x0, 0x100(%rsp)
     a8e: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     a96: 44 39 cd                     	cmpl	%r9d, %ebp
     a99: 4c 8d 71 20                  	leaq	0x20(%rcx), %r14
     a9d: 0f 84 14 f8 ff ff            	je	0x2b7 <tide_task_Shift+0x2b7>
     aa3: e9 08 f7 ff ff               	jmp	0x1b0 <tide_task_Shift+0x1b0>
     aa8: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     ab0: 44 39 cd                     	cmpl	%r9d, %ebp
     ab3: 75 05                        	jne	0xaba <tide_task_Shift+0xaba>
     ab5: 41 89 d9                     	movl	%ebx, %r9d
     ab8: eb 5c                        	jmp	0xb16 <tide_task_Shift+0xb16>
     aba: 45 31 c9                     	xorl	%r9d, %r9d
     abd: 85 db                        	testl	%ebx, %ebx
     abf: 74 4d                        	je	0xb0e <tide_task_Shift+0xb0e>
     ac1: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     ac9: eb 07                        	jmp	0xad2 <tide_task_Shift+0xad2>
     acb: 89 d3                        	movl	%edx, %ebx
     acd: 41 39 d9                     	cmpl	%ebx, %r9d
     ad0: 73 44                        	jae	0xb16 <tide_task_Shift+0xb16>
     ad2: 89 d9                        	movl	%ebx, %ecx
     ad4: 44 29 c9                     	subl	%r9d, %ecx
     ad7: d1 e9                        	shrl	%ecx
     ad9: 42 8d 14 09                  	leal	(%rcx,%r9), %edx
     add: 4c 8b 84 24 e0 00 00 00      	movq	0xe0(%rsp), %r8
     ae5: 45 0f b7 44 50 0a            	movzwl	0xa(%r8,%rdx,2), %r8d
     aeb: 44 39 c5                     	cmpl	%r8d, %ebp
     aee: 76 db                        	jbe	0xacb <tide_task_Shift+0xacb>
     af0: 41 01 c9                     	addl	%ecx, %r9d
     af3: 41 ff c1                     	incl	%r9d
     af6: 41 39 d9                     	cmpl	%ebx, %r9d
     af9: 72 d7                        	jb	0xad2 <tide_task_Shift+0xad2>
     afb: eb 19                        	jmp	0xb16 <tide_task_Shift+0xb16>
     afd: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     b05: 4c 8d 71 20                  	leaq	0x20(%rcx), %r14
     b09: e9 a2 f6 ff ff               	jmp	0x1b0 <tide_task_Shift+0x1b0>
     b0e: 48 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %rbp
     b16: 4c 89 8c 24 08 01 00 00      	movq	%r9, 0x108(%rsp)
     b1e: 44 89 cb                     	movl	%r9d, %ebx
     b21: 2b 9c 24 e8 00 00 00         	subl	0xe8(%rsp), %ebx
     b28: b1 01                        	movb	$0x1, %cl
     b2a: 89 8c 24 00 01 00 00         	movl	%ecx, 0x100(%rsp)
     b31: 4c 8b b4 24 10 01 00 00      	movq	0x110(%rsp), %r14
     b39: e9 79 f7 ff ff               	jmp	0x2b7 <tide_task_Shift+0x2b7>
