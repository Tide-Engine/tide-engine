
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-probe\selected/wide.obj:	file format coff-x86-64

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
      78: 0f 83 af 03 00 00            	jae	0x42d <tide_rows_Shift_5+0x42d>
      7e: 48 89 4c 24 08               	movq	%rcx, 0x8(%rsp)
      83: 48 8b 9c 24 40 03 00 00      	movq	0x340(%rsp), %rbx
      8b: 4c 8b ac 24 38 03 00 00      	movq	0x338(%rsp), %r13
      93: 48 8b ac 24 30 03 00 00      	movq	0x330(%rsp), %rbp
      9b: 48 8b 84 24 28 03 00 00      	movq	0x328(%rsp), %rax
      a3: 4c 8b 94 24 20 03 00 00      	movq	0x320(%rsp), %r10
      ab: 45 31 db                     	xorl	%r11d, %r11d
      ae: 66 0f ef c0                  	pxor	%xmm0, %xmm0
      b2: 66 0f 6f 0d 00 00 00 00      	movdqa	(%rip), %xmm1           # 0xba <tide_rows_Shift_5+0xba>
      ba: 66 0f 6f 15 00 00 00 00      	movdqa	(%rip), %xmm2           # 0xc2 <tide_rows_Shift_5+0xc2>
      c2: 66 0f 6f 1d 00 00 00 00      	movdqa	(%rip), %xmm3           # 0xca <tide_rows_Shift_5+0xca>
      ca: 66 0f 6f 25 00 00 00 00      	movdqa	(%rip), %xmm4           # 0xd2 <tide_rows_Shift_5+0xd2>
      d2: 66 0f 6f 2d 00 00 00 00      	movdqa	(%rip), %xmm5           # 0xda <tide_rows_Shift_5+0xda>
      da: eb 0d                        	jmp	0xe9 <tide_rows_Shift_5+0xe9>
      dc: 0f 1f 40 00                  	nopl	(%rax)
      e0: 44 39 c2                     	cmpl	%r8d, %edx
      e3: 0f 83 ea 01 00 00            	jae	0x2d3 <tide_rows_Shift_5+0x2d3>
      e9: 89 d6                        	movl	%edx, %esi
      eb: 44 89 c1                     	movl	%r8d, %ecx
      ee: 29 d1                        	subl	%edx, %ecx
      f0: 8d 56 10                     	leal	0x10(%rsi), %edx
      f3: 83 f9 10                     	cmpl	$0x10, %ecx
      f6: 41 0f 42 d0                  	cmovbl	%r8d, %edx
      fa: 89 d1                        	movl	%edx, %ecx
      fc: 29 f1                        	subl	%esi, %ecx
      fe: 83 f9 10                     	cmpl	$0x10, %ecx
     101: 75 1d                        	jne	0x120 <tide_rows_Shift_5+0x120>
     103: 89 f1                        	movl	%esi, %ecx
     105: f3 41 0f 6f 34 09            	movdqu	(%r9,%rcx), %xmm6
     10b: 66 0f 74 f0                  	pcmpeqb	%xmm0, %xmm6
     10f: 66 44 0f d7 e6               	pmovmskb	%xmm6, %r12d
     114: e9 85 01 00 00               	jmp	0x29e <tide_rows_Shift_5+0x29e>
     119: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     120: 39 f2                        	cmpl	%esi, %edx
     122: 76 bc                        	jbe	0xe0 <tide_rows_Shift_5+0xe0>
     124: 41 89 f6                     	movl	%esi, %r14d
     127: 89 d7                        	movl	%edx, %edi
     129: 49 89 ff                     	movq	%rdi, %r15
     12c: 4d 29 f7                     	subq	%r14, %r15
     12f: 45 31 e4                     	xorl	%r12d, %r12d
     132: 49 83 ff 08                  	cmpq	$0x8, %r15
     136: 73 08                        	jae	0x140 <tide_rows_Shift_5+0x140>
     138: 4c 89 f3                     	movq	%r14, %rbx
     13b: e9 35 01 00 00               	jmp	0x275 <tide_rows_Shift_5+0x275>
     140: 4c 89 f9                     	movq	%r15, %rcx
     143: 48 83 e1 f8                  	andq	$-0x8, %rcx
     147: 4a 8d 1c 31                  	leaq	(%rcx,%r14), %rbx
     14b: 66 0f 6e f6                  	movd	%esi, %xmm6
     14f: 66 0f 70 f6 00               	pshufd	$0x0, %xmm6, %xmm6      # xmm6 = xmm6[0,0,0,0]
     154: 66 0f 6f fe                  	movdqa	%xmm6, %xmm7
     158: 66 0f fe f9                  	paddd	%xmm1, %xmm7
     15c: 66 44 0f 6f ca               	movdqa	%xmm2, %xmm9
     161: 66 44 0f fa ce               	psubd	%xmm6, %xmm9
     166: 4d 8d 61 04                  	leaq	0x4(%r9), %r12
     16a: 4d 01 e6                     	addq	%r12, %r14
     16d: 66 45 0f ef c0               	pxor	%xmm8, %xmm8
     172: 45 31 e4                     	xorl	%r12d, %r12d
     175: 66 45 0f ef d2               	pxor	%xmm10, %xmm10
     17a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     180: 66 47 0f 6e 64 26 fc         	movd	-0x4(%r14,%r12), %xmm12
     187: 66 47 0f 6e 1c 26            	movd	(%r14,%r12), %xmm11
     18d: 66 44 0f 74 e0               	pcmpeqb	%xmm0, %xmm12
     192: 66 45 0f 60 e4               	punpcklbw	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     197: 66 45 0f 61 e4               	punpcklwd	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3]
     19c: 66 44 0f db e3               	pand	%xmm3, %xmm12
     1a1: 66 44 0f 74 d8               	pcmpeqb	%xmm0, %xmm11
     1a6: 66 45 0f 60 db               	punpcklbw	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1ab: 66 45 0f 61 db               	punpcklwd	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3]
     1b0: 66 44 0f db db               	pand	%xmm3, %xmm11
     1b5: 66 44 0f 6f ef               	movdqa	%xmm7, %xmm13
     1ba: 66 44 0f fa ee               	psubd	%xmm6, %xmm13
     1bf: 66 44 0f 6f f7               	movdqa	%xmm7, %xmm14
     1c4: 66 45 0f fe f1               	paddd	%xmm9, %xmm14
     1c9: 66 41 0f 72 f5 17            	pslld	$0x17, %xmm13
     1cf: 66 44 0f fe ec               	paddd	%xmm4, %xmm13
     1d4: f3 45 0f 5b ed               	cvttps2dq	%xmm13, %xmm13
     1d9: 66 45 0f 70 fc f5            	pshufd	$0xf5, %xmm12, %xmm15   # xmm15 = xmm12[1,1,3,3]
     1df: 66 45 0f f4 e5               	pmuludq	%xmm13, %xmm12
     1e4: 66 45 0f 70 e4 e8            	pshufd	$0xe8, %xmm12, %xmm12   # xmm12 = xmm12[0,2,2,3]
     1ea: 66 45 0f 70 ed f5            	pshufd	$0xf5, %xmm13, %xmm13   # xmm13 = xmm13[1,1,3,3]
     1f0: 66 45 0f f4 ef               	pmuludq	%xmm15, %xmm13
     1f5: 66 45 0f 70 ed e8            	pshufd	$0xe8, %xmm13, %xmm13   # xmm13 = xmm13[0,2,2,3]
     1fb: 66 45 0f 62 e5               	punpckldq	%xmm13, %xmm12  # xmm12 = xmm12[0],xmm13[0],xmm12[1],xmm13[1]
     200: 66 45 0f eb d4               	por	%xmm12, %xmm10
     205: 66 41 0f 72 f6 17            	pslld	$0x17, %xmm14
     20b: 66 44 0f fe f4               	paddd	%xmm4, %xmm14
     210: f3 45 0f 5b e6               	cvttps2dq	%xmm14, %xmm12
     215: 66 45 0f 70 eb f5            	pshufd	$0xf5, %xmm11, %xmm13   # xmm13 = xmm11[1,1,3,3]
     21b: 66 45 0f f4 dc               	pmuludq	%xmm12, %xmm11
     220: 66 45 0f 70 db e8            	pshufd	$0xe8, %xmm11, %xmm11   # xmm11 = xmm11[0,2,2,3]
     226: 66 45 0f 70 e4 f5            	pshufd	$0xf5, %xmm12, %xmm12   # xmm12 = xmm12[1,1,3,3]
     22c: 66 45 0f f4 e5               	pmuludq	%xmm13, %xmm12
     231: 66 45 0f 70 e4 e8            	pshufd	$0xe8, %xmm12, %xmm12   # xmm12 = xmm12[0,2,2,3]
     237: 66 45 0f 62 dc               	punpckldq	%xmm12, %xmm11  # xmm11 = xmm11[0],xmm12[0],xmm11[1],xmm12[1]
     23c: 66 45 0f eb c3               	por	%xmm11, %xmm8
     241: 49 83 c4 08                  	addq	$0x8, %r12
     245: 66 0f fe fd                  	paddd	%xmm5, %xmm7
     249: 4c 39 e1                     	cmpq	%r12, %rcx
     24c: 0f 85 2e ff ff ff            	jne	0x180 <tide_rows_Shift_5+0x180>
     252: 66 45 0f eb c2               	por	%xmm10, %xmm8
     257: 66 41 0f 70 f0 ee            	pshufd	$0xee, %xmm8, %xmm6     # xmm6 = xmm8[2,3,2,3]
     25d: 66 41 0f eb f0               	por	%xmm8, %xmm6
     262: 66 0f 70 fe 55               	pshufd	$0x55, %xmm6, %xmm7     # xmm7 = xmm6[1,1,1,1]
     267: 66 0f eb fe                  	por	%xmm6, %xmm7
     26b: 66 41 0f 7e fc               	movd	%xmm7, %r12d
     270: 49 39 cf                     	cmpq	%rcx, %r15
     273: 74 29                        	je	0x29e <tide_rows_Shift_5+0x29e>
     275: 41 89 f6                     	movl	%esi, %r14d
     278: 41 f7 de                     	negl	%r14d
     27b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     280: 45 31 ff                     	xorl	%r15d, %r15d
     283: 41 80 3c 19 00               	cmpb	$0x0, (%r9,%rbx)
     288: 41 0f 94 c7                  	sete	%r15b
     28c: 41 8d 0c 1e                  	leal	(%r14,%rbx), %ecx
     290: 41 d3 e7                     	shll	%cl, %r15d
     293: 45 09 fc                     	orl	%r15d, %r12d
     296: 48 ff c3                     	incq	%rbx
     299: 48 39 df                     	cmpq	%rbx, %rdi
     29c: 75 e2                        	jne	0x280 <tide_rows_Shift_5+0x280>
     29e: 48 8b 9c 24 40 03 00 00      	movq	0x340(%rsp), %rbx
     2a6: 45 85 e4                     	testl	%r12d, %r12d
     2a9: 0f 84 31 fe ff ff            	je	0xe0 <tide_rows_Shift_5+0xe0>
     2af: 90                           	nop
     2b0: f3 41 0f bc cc               	tzcntl	%r12d, %ecx
     2b5: 01 f1                        	addl	%esi, %ecx
     2b7: 44 89 df                     	movl	%r11d, %edi
     2ba: 41 ff c3                     	incl	%r11d
     2bd: 89 4c bc 10                  	movl	%ecx, 0x10(%rsp,%rdi,4)
     2c1: 41 8d 4c 24 ff               	leal	-0x1(%r12), %ecx
     2c6: 44 21 e1                     	andl	%r12d, %ecx
     2c9: 41 89 cc                     	movl	%ecx, %r12d
     2cc: 75 e2                        	jne	0x2b0 <tide_rows_Shift_5+0x2b0>
     2ce: e9 0d fe ff ff               	jmp	0xe0 <tide_rows_Shift_5+0xe0>
     2d3: 45 85 db                     	testl	%r11d, %r11d
     2d6: 4c 8b 8c 24 78 03 00 00      	movq	0x378(%rsp), %r9
     2de: 48 8b b4 24 70 03 00 00      	movq	0x370(%rsp), %rsi
     2e6: 48 8b 4c 24 08               	movq	0x8(%rsp), %rcx
     2eb: 0f 84 3c 01 00 00            	je	0x42d <tide_rows_Shift_5+0x42d>
     2f1: f3 0f 10 01                  	movss	(%rcx), %xmm0
     2f5: 44 89 d9                     	movl	%r11d, %ecx
     2f8: 31 d2                        	xorl	%edx, %edx
     2fa: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x302 <tide_rows_Shift_5+0x302>
     302: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x309 <tide_rows_Shift_5+0x309>
     309: e9 b6 00 00 00               	jmp	0x3c4 <tide_rows_Shift_5+0x3c4>
     30e: 66 90                        	nop
     310: 0f 28 f4                     	movaps	%xmm4, %xmm6
     313: 4c 8b 9c 24 50 03 00 00      	movq	0x350(%rsp), %r11
     31b: f3 43 0f 10 3c 83            	movss	(%r11,%r8,4), %xmm7
     321: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     325: 4c 8b 9c 24 60 03 00 00      	movq	0x360(%rsp), %r11
     32d: f3 47 0f 10 04 83            	movss	(%r11,%r8,4), %xmm8
     333: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     338: f3 43 0f 58 7c 85 00         	addss	(%r13,%r8,4), %xmm7
     33f: 4c 8b 9c 24 48 03 00 00      	movq	0x348(%rsp), %r11
     347: f3 47 0f 58 04 83            	addss	(%r11,%r8,4), %xmm8
     34d: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     352: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     357: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     35b: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     360: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     364: f3 47 0f 58 0c 82            	addss	(%r10,%r8,4), %xmm9
     36a: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     36f: f3 46 0f 58 54 85 00         	addss	(%rbp,%r8,4), %xmm10
     376: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     37a: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     37e: f3 46 0f 59 1c 86            	mulss	(%rsi,%r8,4), %xmm11
     384: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     389: 0f 28 ec                     	movaps	%xmm4, %xmm5
     38c: 0f 55 eb                     	andnps	%xmm3, %xmm5
     38f: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     393: 0f 56 e5                     	orps	%xmm5, %xmm4
     396: f3 47 0f 11 0c 82            	movss	%xmm9, (%r10,%r8,4)
     39c: f3 42 0f 11 34 80            	movss	%xmm6, (%rax,%r8,4)
     3a2: f3 46 0f 11 54 85 00         	movss	%xmm10, (%rbp,%r8,4)
     3a9: f3 43 0f 11 7c 85 00         	movss	%xmm7, (%r13,%r8,4)
     3b0: f3 42 0f 11 24 83            	movss	%xmm4, (%rbx,%r8,4)
     3b6: f3 47 0f 11 04 83            	movss	%xmm8, (%r11,%r8,4)
     3bc: 48 ff c2                     	incq	%rdx
     3bf: 48 39 d1                     	cmpq	%rdx, %rcx
     3c2: 74 69                        	je	0x42d <tide_rows_Shift_5+0x42d>
     3c4: 44 8b 44 94 10               	movl	0x10(%rsp,%rdx,4), %r8d
     3c9: 4c 8b 9c 24 58 03 00 00      	movq	0x358(%rsp), %r11
     3d1: f3 43 0f 10 1c 83            	movss	(%r11,%r8,4), %xmm3
     3d7: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     3db: f3 42 0f 58 1c 83            	addss	(%rbx,%r8,4), %xmm3
     3e1: f3 43 0f 10 2c 81            	movss	(%r9,%r8,4), %xmm5
     3e7: 4c 8b 9c 24 68 03 00 00      	movq	0x368(%rsp), %r11
     3ef: f3 43 0f 10 24 83            	movss	(%r11,%r8,4), %xmm4
     3f5: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     3f9: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     3fd: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     402: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     407: 0f 28 e0                     	movaps	%xmm0, %xmm4
     40a: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     40e: f3 42 0f 58 24 80            	addss	(%rax,%r8,4), %xmm4
     414: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     417: 0f 86 f3 fe ff ff            	jbe	0x310 <tide_rows_Shift_5+0x310>
     41d: 0f 28 f5                     	movaps	%xmm5, %xmm6
     420: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     424: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     428: e9 e6 fe ff ff               	jmp	0x313 <tide_rows_Shift_5+0x313>
     42d: 0f 28 b4 24 10 02 00 00      	movaps	0x210(%rsp), %xmm6
     435: 0f 28 bc 24 20 02 00 00      	movaps	0x220(%rsp), %xmm7
     43d: 44 0f 28 84 24 30 02 00 00   	movaps	0x230(%rsp), %xmm8
     446: 44 0f 28 8c 24 40 02 00 00   	movaps	0x240(%rsp), %xmm9
     44f: 44 0f 28 94 24 50 02 00 00   	movaps	0x250(%rsp), %xmm10
     458: 44 0f 28 9c 24 60 02 00 00   	movaps	0x260(%rsp), %xmm11
     461: 44 0f 28 a4 24 70 02 00 00   	movaps	0x270(%rsp), %xmm12
     46a: 44 0f 28 ac 24 80 02 00 00   	movaps	0x280(%rsp), %xmm13
     473: 44 0f 28 b4 24 90 02 00 00   	movaps	0x290(%rsp), %xmm14
     47c: 44 0f 28 bc 24 a0 02 00 00   	movaps	0x2a0(%rsp), %xmm15
     485: 48 81 c4 b8 02 00 00         	addq	$0x2b8, %rsp            # imm = 0x2B8
     48c: 5b                           	popq	%rbx
     48d: 5d                           	popq	%rbp
     48e: 5f                           	popq	%rdi
     48f: 5e                           	popq	%rsi
     490: 41 5c                        	popq	%r12
     492: 41 5d                        	popq	%r13
     494: 41 5e                        	popq	%r14
     496: 41 5f                        	popq	%r15
     498: c3                           	retq
