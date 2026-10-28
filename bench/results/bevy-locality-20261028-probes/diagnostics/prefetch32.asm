
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-probe\prefetch32/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec b8 02 00 00         	subq	$0x2b8, %rsp            # imm = 0x2B8
      13: 66 44 0f 7f bc 24 a0 02 00 00	movdqa	%xmm15, 0x2a0(%rsp)
      1d: 66 44 0f 7f b4 24 90 02 00 00	movdqa	%xmm14, 0x290(%rsp)
      27: 66 44 0f 7f ac 24 80 02 00 00	movdqa	%xmm13, 0x280(%rsp)
      31: 66 44 0f 7f a4 24 70 02 00 00	movdqa	%xmm12, 0x270(%rsp)
      3b: 66 44 0f 7f 9c 24 60 02 00 00	movdqa	%xmm11, 0x260(%rsp)
      45: 66 44 0f 7f 94 24 50 02 00 00	movdqa	%xmm10, 0x250(%rsp)
      4f: 66 44 0f 7f 8c 24 40 02 00 00	movdqa	%xmm9, 0x240(%rsp)
      59: 66 44 0f 7f 84 24 30 02 00 00	movdqa	%xmm8, 0x230(%rsp)
      63: 66 0f 7f bc 24 20 02 00 00   	movdqa	%xmm7, 0x220(%rsp)
      6c: 66 0f 7f b4 24 10 02 00 00   	movdqa	%xmm6, 0x210(%rsp)
      75: 44 39 c2                     	cmpl	%r8d, %edx
      78: 0f 83 3a 04 00 00            	jae	0x4b8 <tide_rows_Shift_5+0x4b8>
      7e: 48 89 4c 24 08               	movq	%rcx, 0x8(%rsp)
      83: 48 8b 9c 24 48 03 00 00      	movq	0x348(%rsp), %rbx
      8b: 4c 8b b4 24 40 03 00 00      	movq	0x340(%rsp), %r14
      93: 4c 8b ac 24 38 03 00 00      	movq	0x338(%rsp), %r13
      9b: 48 8b ac 24 30 03 00 00      	movq	0x330(%rsp), %rbp
      a3: 48 8b 84 24 28 03 00 00      	movq	0x328(%rsp), %rax
      ab: 4c 8b 94 24 20 03 00 00      	movq	0x320(%rsp), %r10
      b3: 45 31 db                     	xorl	%r11d, %r11d
      b6: 66 0f ef c0                  	pxor	%xmm0, %xmm0
      ba: 66 0f 6f 0d 00 00 00 00      	movdqa	(%rip), %xmm1           # 0xc2 <tide_rows_Shift_5+0xc2>
      c2: 66 0f 6f 15 00 00 00 00      	movdqa	(%rip), %xmm2           # 0xca <tide_rows_Shift_5+0xca>
      ca: 66 0f 6f 1d 00 00 00 00      	movdqa	(%rip), %xmm3           # 0xd2 <tide_rows_Shift_5+0xd2>
      d2: 66 0f 6f 25 00 00 00 00      	movdqa	(%rip), %xmm4           # 0xda <tide_rows_Shift_5+0xda>
      da: 66 0f 6f 2d 00 00 00 00      	movdqa	(%rip), %xmm5           # 0xe2 <tide_rows_Shift_5+0xe2>
      e2: eb 15                        	jmp	0xf9 <tide_rows_Shift_5+0xf9>
      e4: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
      f0: 44 39 c2                     	cmpl	%r8d, %edx
      f3: 0f 83 fa 01 00 00            	jae	0x2f3 <tide_rows_Shift_5+0x2f3>
      f9: 89 d6                        	movl	%edx, %esi
      fb: 44 89 c1                     	movl	%r8d, %ecx
      fe: 29 d1                        	subl	%edx, %ecx
     100: 8d 56 10                     	leal	0x10(%rsi), %edx
     103: 83 f9 10                     	cmpl	$0x10, %ecx
     106: 41 0f 42 d0                  	cmovbl	%r8d, %edx
     10a: 89 d1                        	movl	%edx, %ecx
     10c: 29 f1                        	subl	%esi, %ecx
     10e: 83 f9 10                     	cmpl	$0x10, %ecx
     111: 75 1d                        	jne	0x130 <tide_rows_Shift_5+0x130>
     113: 89 f1                        	movl	%esi, %ecx
     115: f3 41 0f 6f 34 09            	movdqu	(%r9,%rcx), %xmm6
     11b: 66 0f 74 f0                  	pcmpeqb	%xmm0, %xmm6
     11f: 66 44 0f d7 e6               	pmovmskb	%xmm6, %r12d
     124: e9 85 01 00 00               	jmp	0x2ae <tide_rows_Shift_5+0x2ae>
     129: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     130: 39 f2                        	cmpl	%esi, %edx
     132: 76 bc                        	jbe	0xf0 <tide_rows_Shift_5+0xf0>
     134: 41 89 f6                     	movl	%esi, %r14d
     137: 89 d7                        	movl	%edx, %edi
     139: 49 89 ff                     	movq	%rdi, %r15
     13c: 4d 29 f7                     	subq	%r14, %r15
     13f: 45 31 e4                     	xorl	%r12d, %r12d
     142: 49 83 ff 08                  	cmpq	$0x8, %r15
     146: 73 08                        	jae	0x150 <tide_rows_Shift_5+0x150>
     148: 4c 89 f3                     	movq	%r14, %rbx
     14b: e9 35 01 00 00               	jmp	0x285 <tide_rows_Shift_5+0x285>
     150: 4c 89 f9                     	movq	%r15, %rcx
     153: 48 83 e1 f8                  	andq	$-0x8, %rcx
     157: 4a 8d 1c 31                  	leaq	(%rcx,%r14), %rbx
     15b: 66 0f 6e f6                  	movd	%esi, %xmm6
     15f: 66 0f 70 f6 00               	pshufd	$0x0, %xmm6, %xmm6      # xmm6 = xmm6[0,0,0,0]
     164: 66 0f 6f fe                  	movdqa	%xmm6, %xmm7
     168: 66 0f fe f9                  	paddd	%xmm1, %xmm7
     16c: 66 44 0f 6f ca               	movdqa	%xmm2, %xmm9
     171: 66 44 0f fa ce               	psubd	%xmm6, %xmm9
     176: 4d 8d 61 04                  	leaq	0x4(%r9), %r12
     17a: 4d 01 e6                     	addq	%r12, %r14
     17d: 66 45 0f ef c0               	pxor	%xmm8, %xmm8
     182: 45 31 e4                     	xorl	%r12d, %r12d
     185: 66 45 0f ef d2               	pxor	%xmm10, %xmm10
     18a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     190: 66 47 0f 6e 64 26 fc         	movd	-0x4(%r14,%r12), %xmm12
     197: 66 47 0f 6e 1c 26            	movd	(%r14,%r12), %xmm11
     19d: 66 44 0f 74 e0               	pcmpeqb	%xmm0, %xmm12
     1a2: 66 45 0f 60 e4               	punpcklbw	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1a7: 66 45 0f 61 e4               	punpcklwd	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3]
     1ac: 66 44 0f db e3               	pand	%xmm3, %xmm12
     1b1: 66 44 0f 74 d8               	pcmpeqb	%xmm0, %xmm11
     1b6: 66 45 0f 60 db               	punpcklbw	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1bb: 66 45 0f 61 db               	punpcklwd	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3]
     1c0: 66 44 0f db db               	pand	%xmm3, %xmm11
     1c5: 66 44 0f 6f ef               	movdqa	%xmm7, %xmm13
     1ca: 66 44 0f fa ee               	psubd	%xmm6, %xmm13
     1cf: 66 44 0f 6f f7               	movdqa	%xmm7, %xmm14
     1d4: 66 45 0f fe f1               	paddd	%xmm9, %xmm14
     1d9: 66 41 0f 72 f5 17            	pslld	$0x17, %xmm13
     1df: 66 44 0f fe ec               	paddd	%xmm4, %xmm13
     1e4: f3 45 0f 5b ed               	cvttps2dq	%xmm13, %xmm13
     1e9: 66 45 0f 70 fc f5            	pshufd	$0xf5, %xmm12, %xmm15   # xmm15 = xmm12[1,1,3,3]
     1ef: 66 45 0f f4 e5               	pmuludq	%xmm13, %xmm12
     1f4: 66 45 0f 70 e4 e8            	pshufd	$0xe8, %xmm12, %xmm12   # xmm12 = xmm12[0,2,2,3]
     1fa: 66 45 0f 70 ed f5            	pshufd	$0xf5, %xmm13, %xmm13   # xmm13 = xmm13[1,1,3,3]
     200: 66 45 0f f4 ef               	pmuludq	%xmm15, %xmm13
     205: 66 45 0f 70 ed e8            	pshufd	$0xe8, %xmm13, %xmm13   # xmm13 = xmm13[0,2,2,3]
     20b: 66 45 0f 62 e5               	punpckldq	%xmm13, %xmm12  # xmm12 = xmm12[0],xmm13[0],xmm12[1],xmm13[1]
     210: 66 45 0f eb d4               	por	%xmm12, %xmm10
     215: 66 41 0f 72 f6 17            	pslld	$0x17, %xmm14
     21b: 66 44 0f fe f4               	paddd	%xmm4, %xmm14
     220: f3 45 0f 5b e6               	cvttps2dq	%xmm14, %xmm12
     225: 66 45 0f 70 eb f5            	pshufd	$0xf5, %xmm11, %xmm13   # xmm13 = xmm11[1,1,3,3]
     22b: 66 45 0f f4 dc               	pmuludq	%xmm12, %xmm11
     230: 66 45 0f 70 db e8            	pshufd	$0xe8, %xmm11, %xmm11   # xmm11 = xmm11[0,2,2,3]
     236: 66 45 0f 70 e4 f5            	pshufd	$0xf5, %xmm12, %xmm12   # xmm12 = xmm12[1,1,3,3]
     23c: 66 45 0f f4 e5               	pmuludq	%xmm13, %xmm12
     241: 66 45 0f 70 e4 e8            	pshufd	$0xe8, %xmm12, %xmm12   # xmm12 = xmm12[0,2,2,3]
     247: 66 45 0f 62 dc               	punpckldq	%xmm12, %xmm11  # xmm11 = xmm11[0],xmm12[0],xmm11[1],xmm12[1]
     24c: 66 45 0f eb c3               	por	%xmm11, %xmm8
     251: 49 83 c4 08                  	addq	$0x8, %r12
     255: 66 0f fe fd                  	paddd	%xmm5, %xmm7
     259: 4c 39 e1                     	cmpq	%r12, %rcx
     25c: 0f 85 2e ff ff ff            	jne	0x190 <tide_rows_Shift_5+0x190>
     262: 66 45 0f eb c2               	por	%xmm10, %xmm8
     267: 66 41 0f 70 f0 ee            	pshufd	$0xee, %xmm8, %xmm6     # xmm6 = xmm8[2,3,2,3]
     26d: 66 41 0f eb f0               	por	%xmm8, %xmm6
     272: 66 0f 70 fe 55               	pshufd	$0x55, %xmm6, %xmm7     # xmm7 = xmm6[1,1,1,1]
     277: 66 0f eb fe                  	por	%xmm6, %xmm7
     27b: 66 41 0f 7e fc               	movd	%xmm7, %r12d
     280: 49 39 cf                     	cmpq	%rcx, %r15
     283: 74 29                        	je	0x2ae <tide_rows_Shift_5+0x2ae>
     285: 41 89 f6                     	movl	%esi, %r14d
     288: 41 f7 de                     	negl	%r14d
     28b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     290: 45 31 ff                     	xorl	%r15d, %r15d
     293: 41 80 3c 19 00               	cmpb	$0x0, (%r9,%rbx)
     298: 41 0f 94 c7                  	sete	%r15b
     29c: 41 8d 0c 1e                  	leal	(%r14,%rbx), %ecx
     2a0: 41 d3 e7                     	shll	%cl, %r15d
     2a3: 45 09 fc                     	orl	%r15d, %r12d
     2a6: 48 ff c3                     	incq	%rbx
     2a9: 48 39 df                     	cmpq	%rbx, %rdi
     2ac: 75 e2                        	jne	0x290 <tide_rows_Shift_5+0x290>
     2ae: 48 8b 9c 24 48 03 00 00      	movq	0x348(%rsp), %rbx
     2b6: 4c 8b b4 24 40 03 00 00      	movq	0x340(%rsp), %r14
     2be: 45 85 e4                     	testl	%r12d, %r12d
     2c1: 0f 84 29 fe ff ff            	je	0xf0 <tide_rows_Shift_5+0xf0>
     2c7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     2d0: f3 41 0f bc cc               	tzcntl	%r12d, %ecx
     2d5: 01 f1                        	addl	%esi, %ecx
     2d7: 44 89 df                     	movl	%r11d, %edi
     2da: 41 ff c3                     	incl	%r11d
     2dd: 89 4c bc 10                  	movl	%ecx, 0x10(%rsp,%rdi,4)
     2e1: 41 8d 4c 24 ff               	leal	-0x1(%r12), %ecx
     2e6: 44 21 e1                     	andl	%r12d, %ecx
     2e9: 41 89 cc                     	movl	%ecx, %r12d
     2ec: 75 e2                        	jne	0x2d0 <tide_rows_Shift_5+0x2d0>
     2ee: e9 fd fd ff ff               	jmp	0xf0 <tide_rows_Shift_5+0xf0>
     2f3: 45 85 db                     	testl	%r11d, %r11d
     2f6: 4c 8b 8c 24 78 03 00 00      	movq	0x378(%rsp), %r9
     2fe: 48 8b b4 24 70 03 00 00      	movq	0x370(%rsp), %rsi
     306: 48 8b 4c 24 08               	movq	0x8(%rsp), %rcx
     30b: 0f 84 a7 01 00 00            	je	0x4b8 <tide_rows_Shift_5+0x4b8>
     311: f3 0f 10 01                  	movss	(%rcx), %xmm0
     315: 44 89 d9                     	movl	%r11d, %ecx
     318: 31 d2                        	xorl	%edx, %edx
     31a: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x322 <tide_rows_Shift_5+0x322>
     322: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x329 <tide_rows_Shift_5+0x329>
     329: eb 5b                        	jmp	0x386 <tide_rows_Shift_5+0x386>
     32b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     330: 44 0f 28 d7                  	movaps	%xmm7, %xmm10
     334: 44 0f 28 dd                  	movaps	%xmm5, %xmm11
     338: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     33c: f3 46 0f 59 1c 86            	mulss	(%rsi,%r8,4), %xmm11
     342: f3 0f c2 fc 01               	cmpltss	%xmm4, %xmm7
     347: 0f 28 e7                     	movaps	%xmm7, %xmm4
     34a: 0f 55 e5                     	andnps	%xmm5, %xmm4
     34d: 41 0f 54 fb                  	andps	%xmm11, %xmm7
     351: 0f 56 fc                     	orps	%xmm4, %xmm7
     354: f3 47 0f 11 04 82            	movss	%xmm8, (%r10,%r8,4)
     35a: f3 46 0f 11 14 80            	movss	%xmm10, (%rax,%r8,4)
     360: f3 46 0f 11 4c 85 00         	movss	%xmm9, (%rbp,%r8,4)
     367: f3 43 0f 11 5c 85 00         	movss	%xmm3, (%r13,%r8,4)
     36e: f3 43 0f 11 3c 86            	movss	%xmm7, (%r14,%r8,4)
     374: f3 42 0f 11 34 83            	movss	%xmm6, (%rbx,%r8,4)
     37a: 48 ff c2                     	incq	%rdx
     37d: 48 39 d1                     	cmpq	%rdx, %rcx
     380: 0f 84 32 01 00 00            	je	0x4b8 <tide_rows_Shift_5+0x4b8>
     386: 4c 8d 42 20                  	leaq	0x20(%rdx), %r8
     38a: 45 39 d8                     	cmpl	%r11d, %r8d
     38d: 73 66                        	jae	0x3f5 <tide_rows_Shift_5+0x3f5>
     38f: 45 89 c0                     	movl	%r8d, %r8d
     392: 46 8b 44 84 10               	movl	0x10(%rsp,%r8,4), %r8d
     397: 43 0f 18 0c 82               	prefetcht0	(%r10,%r8,4)
     39c: 42 0f 18 0c 80               	prefetcht0	(%rax,%r8,4)
     3a1: 42 0f 18 4c 85 00            	prefetcht0	(%rbp,%r8,4)
     3a7: 43 0f 18 4c 85 00            	prefetcht0	(%r13,%r8,4)
     3ad: 43 0f 18 0c 86               	prefetcht0	(%r14,%r8,4)
     3b2: 42 0f 18 0c 83               	prefetcht0	(%rbx,%r8,4)
     3b7: 48 8b bc 24 50 03 00 00      	movq	0x350(%rsp), %rdi
     3bf: 42 0f 18 0c 87               	prefetcht0	(%rdi,%r8,4)
     3c4: 48 8b bc 24 58 03 00 00      	movq	0x358(%rsp), %rdi
     3cc: 42 0f 18 0c 87               	prefetcht0	(%rdi,%r8,4)
     3d1: 48 8b bc 24 60 03 00 00      	movq	0x360(%rsp), %rdi
     3d9: 42 0f 18 0c 87               	prefetcht0	(%rdi,%r8,4)
     3de: 48 8b bc 24 68 03 00 00      	movq	0x368(%rsp), %rdi
     3e6: 42 0f 18 0c 87               	prefetcht0	(%rdi,%r8,4)
     3eb: 42 0f 18 0c 86               	prefetcht0	(%rsi,%r8,4)
     3f0: 43 0f 18 0c 81               	prefetcht0	(%r9,%r8,4)
     3f5: 44 8b 44 94 10               	movl	0x10(%rsp,%rdx,4), %r8d
     3fa: f3 43 0f 10 24 81            	movss	(%r9,%r8,4), %xmm4
     400: 48 8b bc 24 50 03 00 00      	movq	0x350(%rsp), %rdi
     408: f3 42 0f 10 1c 87            	movss	(%rdi,%r8,4), %xmm3
     40e: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     412: 48 8b bc 24 58 03 00 00      	movq	0x358(%rsp), %rdi
     41a: f3 42 0f 10 2c 87            	movss	(%rdi,%r8,4), %xmm5
     420: f3 0f 59 e8                  	mulss	%xmm0, %xmm5
     424: 48 8b bc 24 60 03 00 00      	movq	0x360(%rsp), %rdi
     42c: f3 42 0f 10 3c 87            	movss	(%rdi,%r8,4), %xmm7
     432: f3 43 0f 58 5c 85 00         	addss	(%r13,%r8,4), %xmm3
     439: f3 43 0f 58 2c 86            	addss	(%r14,%r8,4), %xmm5
     43f: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     443: f3 42 0f 58 3c 83            	addss	(%rbx,%r8,4), %xmm7
     449: 48 8b bc 24 68 03 00 00      	movq	0x368(%rsp), %rdi
     451: f3 46 0f 10 04 87            	movss	(%rdi,%r8,4), %xmm8
     457: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     45c: 0f 28 f1                     	movaps	%xmm1, %xmm6
     45f: f3 41 0f 5c f0               	subss	%xmm8, %xmm6
     464: f3 0f 59 de                  	mulss	%xmm6, %xmm3
     468: f3 0f 59 ee                  	mulss	%xmm6, %xmm5
     46c: f3 0f 59 f7                  	mulss	%xmm7, %xmm6
     470: 44 0f 28 c0                  	movaps	%xmm0, %xmm8
     474: f3 44 0f 59 c3               	mulss	%xmm3, %xmm8
     479: 0f 28 f8                     	movaps	%xmm0, %xmm7
     47c: f3 0f 59 fd                  	mulss	%xmm5, %xmm7
     480: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     484: f3 44 0f 59 ce               	mulss	%xmm6, %xmm9
     489: f3 47 0f 58 04 82            	addss	(%r10,%r8,4), %xmm8
     48f: f3 42 0f 58 3c 80            	addss	(%rax,%r8,4), %xmm7
     495: f3 46 0f 58 4c 85 00         	addss	(%rbp,%r8,4), %xmm9
     49c: 0f 2e e7                     	ucomiss	%xmm7, %xmm4
     49f: 0f 86 8b fe ff ff            	jbe	0x330 <tide_rows_Shift_5+0x330>
     4a5: 44 0f 28 d4                  	movaps	%xmm4, %xmm10
     4a9: f3 44 0f 58 d4               	addss	%xmm4, %xmm10
     4ae: f3 44 0f 5c d7               	subss	%xmm7, %xmm10
     4b3: e9 7c fe ff ff               	jmp	0x334 <tide_rows_Shift_5+0x334>
     4b8: 0f 28 b4 24 10 02 00 00      	movaps	0x210(%rsp), %xmm6
     4c0: 0f 28 bc 24 20 02 00 00      	movaps	0x220(%rsp), %xmm7
     4c8: 44 0f 28 84 24 30 02 00 00   	movaps	0x230(%rsp), %xmm8
     4d1: 44 0f 28 8c 24 40 02 00 00   	movaps	0x240(%rsp), %xmm9
     4da: 44 0f 28 94 24 50 02 00 00   	movaps	0x250(%rsp), %xmm10
     4e3: 44 0f 28 9c 24 60 02 00 00   	movaps	0x260(%rsp), %xmm11
     4ec: 44 0f 28 a4 24 70 02 00 00   	movaps	0x270(%rsp), %xmm12
     4f5: 44 0f 28 ac 24 80 02 00 00   	movaps	0x280(%rsp), %xmm13
     4fe: 44 0f 28 b4 24 90 02 00 00   	movaps	0x290(%rsp), %xmm14
     507: 44 0f 28 bc 24 a0 02 00 00   	movaps	0x2a0(%rsp), %xmm15
     510: 48 81 c4 b8 02 00 00         	addq	$0x2b8, %rsp            # imm = 0x2B8
     517: 5b                           	popq	%rbx
     518: 5d                           	popq	%rbp
     519: 5f                           	popq	%rdi
     51a: 5e                           	popq	%rsi
     51b: 41 5c                        	popq	%r12
     51d: 41 5d                        	popq	%r13
     51f: 41 5e                        	popq	%r14
     521: 41 5f                        	popq	%r15
     523: c3                           	retq
