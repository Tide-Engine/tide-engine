
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-probe\mask16/wide.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <tide_rows_Shift_5>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 48 81 ec c8 00 00 00         	subq	$0xc8, %rsp
      13: 66 44 0f 7f bc 24 b0 00 00 00	movdqa	%xmm15, 0xb0(%rsp)
      1d: 66 44 0f 7f b4 24 a0 00 00 00	movdqa	%xmm14, 0xa0(%rsp)
      27: 66 44 0f 7f ac 24 90 00 00 00	movdqa	%xmm13, 0x90(%rsp)
      31: 66 44 0f 7f a4 24 80 00 00 00	movdqa	%xmm12, 0x80(%rsp)
      3b: 66 44 0f 7f 5c 24 70         	movdqa	%xmm11, 0x70(%rsp)
      42: 66 44 0f 7f 54 24 60         	movdqa	%xmm10, 0x60(%rsp)
      49: 66 44 0f 7f 4c 24 50         	movdqa	%xmm9, 0x50(%rsp)
      50: 66 44 0f 7f 44 24 40         	movdqa	%xmm8, 0x40(%rsp)
      57: 66 0f 7f 7c 24 30            	movdqa	%xmm7, 0x30(%rsp)
      5d: 66 0f 7f 74 24 20            	movdqa	%xmm6, 0x20(%rsp)
      63: 48 89 4c 24 18               	movq	%rcx, 0x18(%rsp)
      68: 44 39 c2                     	cmpl	%r8d, %edx
      6b: 0f 83 9a 03 00 00            	jae	0x40b <tide_rows_Shift_5+0x40b>
      71: 4c 8b bc 24 58 01 00 00      	movq	0x158(%rsp), %r15
      79: 4c 8b a4 24 50 01 00 00      	movq	0x150(%rsp), %r12
      81: 4c 8b ac 24 48 01 00 00      	movq	0x148(%rsp), %r13
      89: 48 8b ac 24 40 01 00 00      	movq	0x140(%rsp), %rbp
      91: 48 8b 84 24 38 01 00 00      	movq	0x138(%rsp), %rax
      99: 4c 8b 94 24 30 01 00 00      	movq	0x130(%rsp), %r10
      a1: 49 8d 49 04                  	leaq	0x4(%r9), %rcx
      a5: 48 89 4c 24 10               	movq	%rcx, 0x10(%rsp)
      aa: 66 0f ef c0                  	pxor	%xmm0, %xmm0
      ae: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0xb6 <tide_rows_Shift_5+0xb6>
      b6: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0xbd <tide_rows_Shift_5+0xbd>
      bd: 66 0f 6f 2d 00 00 00 00      	movdqa	(%rip), %xmm5           # 0xc5 <tide_rows_Shift_5+0xc5>
      c5: 66 0f 6f 35 00 00 00 00      	movdqa	(%rip), %xmm6           # 0xcd <tide_rows_Shift_5+0xcd>
      cd: 66 0f 6f 3d 00 00 00 00      	movdqa	(%rip), %xmm7           # 0xd5 <tide_rows_Shift_5+0xd5>
      d5: 44 89 44 24 0c               	movl	%r8d, 0xc(%rsp)
      da: eb 0d                        	jmp	0xe9 <tide_rows_Shift_5+0xe9>
      dc: 0f 1f 40 00                  	nopl	(%rax)
      e0: 44 39 c2                     	cmpl	%r8d, %edx
      e3: 0f 83 22 03 00 00            	jae	0x40b <tide_rows_Shift_5+0x40b>
      e9: 41 89 d6                     	movl	%edx, %r14d
      ec: 44 89 c1                     	movl	%r8d, %ecx
      ef: 29 d1                        	subl	%edx, %ecx
      f1: 41 8d 56 10                  	leal	0x10(%r14), %edx
      f5: 83 f9 10                     	cmpl	$0x10, %ecx
      f8: 41 0f 42 d0                  	cmovbl	%r8d, %edx
      fc: 89 d1                        	movl	%edx, %ecx
      fe: 44 29 f1                     	subl	%r14d, %ecx
     101: 83 f9 10                     	cmpl	$0x10, %ecx
     104: 75 1a                        	jne	0x120 <tide_rows_Shift_5+0x120>
     106: 89 54 24 08                  	movl	%edx, 0x8(%rsp)
     10a: 44 89 f1                     	movl	%r14d, %ecx
     10d: f3 41 0f 6f 1c 09            	movdqu	(%r9,%rcx), %xmm3
     113: 66 0f 74 d8                  	pcmpeqb	%xmm0, %xmm3
     117: 66 0f d7 db                  	pmovmskb	%xmm3, %ebx
     11b: e9 8b 01 00 00               	jmp	0x2ab <tide_rows_Shift_5+0x2ab>
     120: 44 39 f2                     	cmpl	%r14d, %edx
     123: 76 bb                        	jbe	0xe0 <tide_rows_Shift_5+0xe0>
     125: 44 89 f1                     	movl	%r14d, %ecx
     128: 41 89 d0                     	movl	%edx, %r8d
     12b: 4c 89 c7                     	movq	%r8, %rdi
     12e: 48 29 cf                     	subq	%rcx, %rdi
     131: 31 db                        	xorl	%ebx, %ebx
     133: 48 83 ff 08                  	cmpq	$0x8, %rdi
     137: 89 54 24 08                  	movl	%edx, 0x8(%rsp)
     13b: 73 08                        	jae	0x145 <tide_rows_Shift_5+0x145>
     13d: 49 89 cb                     	movq	%rcx, %r11
     140: e9 3e 01 00 00               	jmp	0x283 <tide_rows_Shift_5+0x283>
     145: 48 89 fe                     	movq	%rdi, %rsi
     148: 48 83 e6 f8                  	andq	$-0x8, %rsi
     14c: 4c 8d 1c 0e                  	leaq	(%rsi,%rcx), %r11
     150: 66 45 0f 6e c6               	movd	%r14d, %xmm8
     155: 66 41 0f 70 d8 00            	pshufd	$0x0, %xmm8, %xmm3      # xmm3 = xmm8[0,0,0,0]
     15b: 66 44 0f 6f c3               	movdqa	%xmm3, %xmm8
     160: 66 44 0f fe 05 00 00 00 00   	paddd	(%rip), %xmm8           # 0x169 <tide_rows_Shift_5+0x169>
     169: 66 44 0f 6f 1d 00 00 00 00   	movdqa	(%rip), %xmm11          # 0x172 <tide_rows_Shift_5+0x172>
     172: 66 44 0f fa db               	psubd	%xmm3, %xmm11
     177: 48 03 4c 24 10               	addq	0x10(%rsp), %rcx
     17c: 66 45 0f ef d2               	pxor	%xmm10, %xmm10
     181: 31 d2                        	xorl	%edx, %edx
     183: 66 45 0f ef e4               	pxor	%xmm12, %xmm12
     188: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
     190: 66 44 0f 6e 74 11 fc         	movd	-0x4(%rcx,%rdx), %xmm14
     197: 66 44 0f 6e 2c 11            	movd	(%rcx,%rdx), %xmm13
     19d: 66 44 0f 74 f0               	pcmpeqb	%xmm0, %xmm14
     1a2: 66 45 0f 60 f6               	punpcklbw	%xmm14, %xmm14  # xmm14 = xmm14[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1a7: 66 45 0f 61 f6               	punpcklwd	%xmm14, %xmm14  # xmm14 = xmm14[0,0,1,1,2,2,3,3]
     1ac: 66 44 0f db f5               	pand	%xmm5, %xmm14
     1b1: 66 44 0f 74 e8               	pcmpeqb	%xmm0, %xmm13
     1b6: 66 45 0f 60 ed               	punpcklbw	%xmm13, %xmm13  # xmm13 = xmm13[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1bb: 66 45 0f 61 ed               	punpcklwd	%xmm13, %xmm13  # xmm13 = xmm13[0,0,1,1,2,2,3,3]
     1c0: 66 44 0f db ed               	pand	%xmm5, %xmm13
     1c5: 66 45 0f 6f f8               	movdqa	%xmm8, %xmm15
     1ca: 66 44 0f fa fb               	psubd	%xmm3, %xmm15
     1cf: 66 45 0f 6f c8               	movdqa	%xmm8, %xmm9
     1d4: 66 45 0f fe cb               	paddd	%xmm11, %xmm9
     1d9: 66 41 0f 72 f7 17            	pslld	$0x17, %xmm15
     1df: 66 44 0f fe fe               	paddd	%xmm6, %xmm15
     1e4: f3 45 0f 5b ff               	cvttps2dq	%xmm15, %xmm15
     1e9: 66 41 0f 70 e6 f5            	pshufd	$0xf5, %xmm14, %xmm4    # xmm4 = xmm14[1,1,3,3]
     1ef: 66 45 0f f4 f7               	pmuludq	%xmm15, %xmm14
     1f4: 66 45 0f 70 f6 e8            	pshufd	$0xe8, %xmm14, %xmm14   # xmm14 = xmm14[0,2,2,3]
     1fa: 66 45 0f 70 ff f5            	pshufd	$0xf5, %xmm15, %xmm15   # xmm15 = xmm15[1,1,3,3]
     200: 66 44 0f f4 fc               	pmuludq	%xmm4, %xmm15
     205: 66 41 0f 70 e7 e8            	pshufd	$0xe8, %xmm15, %xmm4    # xmm4 = xmm15[0,2,2,3]
     20b: 66 44 0f 62 f4               	punpckldq	%xmm4, %xmm14   # xmm14 = xmm14[0],xmm4[0],xmm14[1],xmm4[1]
     210: 66 45 0f eb e6               	por	%xmm14, %xmm12
     215: 66 41 0f 72 f1 17            	pslld	$0x17, %xmm9
     21b: 66 44 0f fe ce               	paddd	%xmm6, %xmm9
     220: f3 41 0f 5b e1               	cvttps2dq	%xmm9, %xmm4
     225: 66 45 0f 70 cd f5            	pshufd	$0xf5, %xmm13, %xmm9    # xmm9 = xmm13[1,1,3,3]
     22b: 66 44 0f f4 ec               	pmuludq	%xmm4, %xmm13
     230: 66 45 0f 70 ed e8            	pshufd	$0xe8, %xmm13, %xmm13   # xmm13 = xmm13[0,2,2,3]
     236: 66 0f 70 e4 f5               	pshufd	$0xf5, %xmm4, %xmm4     # xmm4 = xmm4[1,1,3,3]
     23b: 66 41 0f f4 e1               	pmuludq	%xmm9, %xmm4
     240: 66 0f 70 e4 e8               	pshufd	$0xe8, %xmm4, %xmm4     # xmm4 = xmm4[0,2,2,3]
     245: 66 44 0f 62 ec               	punpckldq	%xmm4, %xmm13   # xmm13 = xmm13[0],xmm4[0],xmm13[1],xmm4[1]
     24a: 66 45 0f eb d5               	por	%xmm13, %xmm10
     24f: 48 83 c2 08                  	addq	$0x8, %rdx
     253: 66 44 0f fe c7               	paddd	%xmm7, %xmm8
     258: 48 39 d6                     	cmpq	%rdx, %rsi
     25b: 0f 85 2f ff ff ff            	jne	0x190 <tide_rows_Shift_5+0x190>
     261: 66 45 0f eb d4               	por	%xmm12, %xmm10
     266: 66 41 0f 70 da ee            	pshufd	$0xee, %xmm10, %xmm3    # xmm3 = xmm10[2,3,2,3]
     26c: 66 41 0f eb da               	por	%xmm10, %xmm3
     271: 66 0f 70 e3 55               	pshufd	$0x55, %xmm3, %xmm4     # xmm4 = xmm3[1,1,1,1]
     276: 66 0f eb e3                  	por	%xmm3, %xmm4
     27a: 66 0f 7e e3                  	movd	%xmm4, %ebx
     27e: 48 39 f7                     	cmpq	%rsi, %rdi
     281: 74 28                        	je	0x2ab <tide_rows_Shift_5+0x2ab>
     283: 44 89 f6                     	movl	%r14d, %esi
     286: f7 de                        	negl	%esi
     288: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
     290: 31 ff                        	xorl	%edi, %edi
     292: 43 80 3c 19 00               	cmpb	$0x0, (%r9,%r11)
     297: 40 0f 94 c7                  	sete	%dil
     29b: 42 8d 0c 1e                  	leal	(%rsi,%r11), %ecx
     29f: d3 e7                        	shll	%cl, %edi
     2a1: 09 fb                        	orl	%edi, %ebx
     2a3: 49 ff c3                     	incq	%r11
     2a6: 4d 39 d8                     	cmpq	%r11, %r8
     2a9: 75 e5                        	jne	0x290 <tide_rows_Shift_5+0x290>
     2ab: 85 db                        	testl	%ebx, %ebx
     2ad: 44 8b 44 24 0c               	movl	0xc(%rsp), %r8d
     2b2: 8b 54 24 08                  	movl	0x8(%rsp), %edx
     2b6: 48 8b b4 24 88 01 00 00      	movq	0x188(%rsp), %rsi
     2be: 48 8b bc 24 80 01 00 00      	movq	0x180(%rsp), %rdi
     2c6: 0f 84 14 fe ff ff            	je	0xe0 <tide_rows_Shift_5+0xe0>
     2cc: 48 8b 4c 24 18               	movq	0x18(%rsp), %rcx
     2d1: f3 44 0f 10 01               	movss	(%rcx), %xmm8
     2d6: e9 bd 00 00 00               	jmp	0x398 <tide_rows_Shift_5+0x398>
     2db: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     2e0: 45 0f 28 e2                  	movaps	%xmm10, %xmm12
     2e4: 4c 8b 9c 24 60 01 00 00      	movq	0x160(%rsp), %r11
     2ec: f3 45 0f 10 2c 8b            	movss	(%r11,%rcx,4), %xmm13
     2f2: f3 45 0f 59 e8               	mulss	%xmm8, %xmm13
     2f7: 4c 8b 9c 24 70 01 00 00      	movq	0x170(%rsp), %r11
     2ff: f3 45 0f 10 34 8b            	movss	(%r11,%rcx,4), %xmm14
     305: f3 45 0f 59 f0               	mulss	%xmm8, %xmm14
     30a: f3 45 0f 58 6c 8d 00         	addss	(%r13,%rcx,4), %xmm13
     311: f3 45 0f 58 34 8f            	addss	(%r15,%rcx,4), %xmm14
     317: 44 8d 5b ff                  	leal	-0x1(%rbx), %r11d
     31b: f3 45 0f 59 ef               	mulss	%xmm15, %xmm13
     320: f3 45 0f 59 f7               	mulss	%xmm15, %xmm14
     325: 41 0f 28 d8                  	movaps	%xmm8, %xmm3
     329: f3 41 0f 59 dd               	mulss	%xmm13, %xmm3
     32e: 41 0f 28 e0                  	movaps	%xmm8, %xmm4
     332: f3 41 0f 58 1c 8a            	addss	(%r10,%rcx,4), %xmm3
     338: f3 41 0f 59 e6               	mulss	%xmm14, %xmm4
     33d: f3 0f 58 64 8d 00            	addss	(%rbp,%rcx,4), %xmm4
     343: 45 0f 28 f9                  	movaps	%xmm9, %xmm15
     347: 44 0f 57 fa                  	xorps	%xmm2, %xmm15
     34b: f3 44 0f 59 3c 8f            	mulss	(%rdi,%rcx,4), %xmm15
     351: f3 45 0f c2 d3 01            	cmpltss	%xmm11, %xmm10
     357: 45 0f 28 da                  	movaps	%xmm10, %xmm11
     35b: 45 0f 55 d9                  	andnps	%xmm9, %xmm11
     35f: 45 0f 54 d7                  	andps	%xmm15, %xmm10
     363: 45 0f 56 d3                  	orps	%xmm11, %xmm10
     367: f3 41 0f 11 1c 8a            	movss	%xmm3, (%r10,%rcx,4)
     36d: f3 44 0f 11 24 88            	movss	%xmm12, (%rax,%rcx,4)
     373: f3 0f 11 64 8d 00            	movss	%xmm4, (%rbp,%rcx,4)
     379: f3 45 0f 11 6c 8d 00         	movss	%xmm13, (%r13,%rcx,4)
     380: f3 45 0f 11 14 8c            	movss	%xmm10, (%r12,%rcx,4)
     386: f3 45 0f 11 34 8f            	movss	%xmm14, (%r15,%rcx,4)
     38c: 41 21 db                     	andl	%ebx, %r11d
     38f: 44 89 db                     	movl	%r11d, %ebx
     392: 0f 84 48 fd ff ff            	je	0xe0 <tide_rows_Shift_5+0xe0>
     398: f3 0f bc cb                  	tzcntl	%ebx, %ecx
     39c: 44 01 f1                     	addl	%r14d, %ecx
     39f: 4c 8b 9c 24 68 01 00 00      	movq	0x168(%rsp), %r11
     3a7: f3 45 0f 10 0c 8b            	movss	(%r11,%rcx,4), %xmm9
     3ad: f3 45 0f 59 c8               	mulss	%xmm8, %xmm9
     3b2: f3 45 0f 58 0c 8c            	addss	(%r12,%rcx,4), %xmm9
     3b8: f3 44 0f 10 1c 8e            	movss	(%rsi,%rcx,4), %xmm11
     3be: 4c 8b 9c 24 78 01 00 00      	movq	0x178(%rsp), %r11
     3c6: f3 41 0f 10 1c 8b            	movss	(%r11,%rcx,4), %xmm3
     3cc: f3 41 0f 59 d8               	mulss	%xmm8, %xmm3
     3d1: 44 0f 28 f9                  	movaps	%xmm1, %xmm15
     3d5: f3 44 0f 5c fb               	subss	%xmm3, %xmm15
     3da: f3 45 0f 59 cf               	mulss	%xmm15, %xmm9
     3df: 45 0f 28 d0                  	movaps	%xmm8, %xmm10
     3e3: f3 45 0f 59 d1               	mulss	%xmm9, %xmm10
     3e8: f3 44 0f 58 14 88            	addss	(%rax,%rcx,4), %xmm10
     3ee: 45 0f 2e da                  	ucomiss	%xmm10, %xmm11
     3f2: 0f 86 e8 fe ff ff            	jbe	0x2e0 <tide_rows_Shift_5+0x2e0>
     3f8: 45 0f 28 e3                  	movaps	%xmm11, %xmm12
     3fc: f3 45 0f 58 e3               	addss	%xmm11, %xmm12
     401: f3 45 0f 5c e2               	subss	%xmm10, %xmm12
     406: e9 d9 fe ff ff               	jmp	0x2e4 <tide_rows_Shift_5+0x2e4>
     40b: 0f 28 74 24 20               	movaps	0x20(%rsp), %xmm6
     410: 0f 28 7c 24 30               	movaps	0x30(%rsp), %xmm7
     415: 44 0f 28 44 24 40            	movaps	0x40(%rsp), %xmm8
     41b: 44 0f 28 4c 24 50            	movaps	0x50(%rsp), %xmm9
     421: 44 0f 28 54 24 60            	movaps	0x60(%rsp), %xmm10
     427: 44 0f 28 5c 24 70            	movaps	0x70(%rsp), %xmm11
     42d: 44 0f 28 a4 24 80 00 00 00   	movaps	0x80(%rsp), %xmm12
     436: 44 0f 28 ac 24 90 00 00 00   	movaps	0x90(%rsp), %xmm13
     43f: 44 0f 28 b4 24 a0 00 00 00   	movaps	0xa0(%rsp), %xmm14
     448: 44 0f 28 bc 24 b0 00 00 00   	movaps	0xb0(%rsp), %xmm15
     451: 48 81 c4 c8 00 00 00         	addq	$0xc8, %rsp
     458: 5b                           	popq	%rbx
     459: 5d                           	popq	%rbp
     45a: 5f                           	popq	%rdi
     45b: 5e                           	popq	%rsi
     45c: 41 5c                        	popq	%r12
     45e: 41 5d                        	popq	%r13
     460: 41 5e                        	popq	%r14
     462: 41 5f                        	popq	%r15
     464: c3                           	retq
