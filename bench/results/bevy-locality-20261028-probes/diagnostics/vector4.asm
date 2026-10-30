
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-probe\vector4/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec c8 02 00 00         	subq	$0x2c8, %rsp            # imm = 0x2C8
      13: 66 44 0f 7f bc 24 b0 02 00 00	movdqa	%xmm15, 0x2b0(%rsp)
      1d: 66 44 0f 7f b4 24 a0 02 00 00	movdqa	%xmm14, 0x2a0(%rsp)
      27: 66 44 0f 7f ac 24 90 02 00 00	movdqa	%xmm13, 0x290(%rsp)
      31: 66 44 0f 7f a4 24 80 02 00 00	movdqa	%xmm12, 0x280(%rsp)
      3b: 66 44 0f 7f 9c 24 70 02 00 00	movdqa	%xmm11, 0x270(%rsp)
      45: 66 44 0f 7f 94 24 60 02 00 00	movdqa	%xmm10, 0x260(%rsp)
      4f: 66 44 0f 7f 8c 24 50 02 00 00	movdqa	%xmm9, 0x250(%rsp)
      59: 66 44 0f 7f 84 24 40 02 00 00	movdqa	%xmm8, 0x240(%rsp)
      63: 66 0f 7f bc 24 30 02 00 00   	movdqa	%xmm7, 0x230(%rsp)
      6c: 66 0f 7f b4 24 20 02 00 00   	movdqa	%xmm6, 0x220(%rsp)
      75: 44 39 c2                     	cmpl	%r8d, %edx
      78: 0f 83 7c 06 00 00            	jae	0x6fa <tide_rows_Shift_5+0x6fa>
      7e: 48 89 0c 24                  	movq	%rcx, (%rsp)
      82: 4c 8b ac 24 58 03 00 00      	movq	0x358(%rsp), %r13
      8a: 48 8b ac 24 50 03 00 00      	movq	0x350(%rsp), %rbp
      92: 48 8b 84 24 48 03 00 00      	movq	0x348(%rsp), %rax
      9a: 4c 8b 94 24 40 03 00 00      	movq	0x340(%rsp), %r10
      a2: 45 31 db                     	xorl	%r11d, %r11d
      a5: 66 0f ef c9                  	pxor	%xmm1, %xmm1
      a9: 66 0f 6f 15 00 00 00 00      	movdqa	(%rip), %xmm2           # 0xb1 <tide_rows_Shift_5+0xb1>
      b1: 66 0f 6f 1d 00 00 00 00      	movdqa	(%rip), %xmm3           # 0xb9 <tide_rows_Shift_5+0xb9>
      b9: 66 0f 6f 25 00 00 00 00      	movdqa	(%rip), %xmm4           # 0xc1 <tide_rows_Shift_5+0xc1>
      c1: 66 0f 6f 05 00 00 00 00      	movdqa	(%rip), %xmm0           # 0xc9 <tide_rows_Shift_5+0xc9>
      c9: 66 0f 6f 2d 00 00 00 00      	movdqa	(%rip), %xmm5           # 0xd1 <tide_rows_Shift_5+0xd1>
      d1: eb 16                        	jmp	0xe9 <tide_rows_Shift_5+0xe9>
      d3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
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
     10b: 66 0f 74 f1                  	pcmpeqb	%xmm1, %xmm6
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
     158: 66 0f fe fa                  	paddd	%xmm2, %xmm7
     15c: 66 44 0f 6f cb               	movdqa	%xmm3, %xmm9
     161: 66 44 0f fa ce               	psubd	%xmm6, %xmm9
     166: 4d 8d 61 04                  	leaq	0x4(%r9), %r12
     16a: 4d 01 e6                     	addq	%r12, %r14
     16d: 66 45 0f ef c0               	pxor	%xmm8, %xmm8
     172: 45 31 e4                     	xorl	%r12d, %r12d
     175: 66 45 0f ef d2               	pxor	%xmm10, %xmm10
     17a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     180: 66 47 0f 6e 64 26 fc         	movd	-0x4(%r14,%r12), %xmm12
     187: 66 47 0f 6e 1c 26            	movd	(%r14,%r12), %xmm11
     18d: 66 44 0f 74 e1               	pcmpeqb	%xmm1, %xmm12
     192: 66 45 0f 60 e4               	punpcklbw	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     197: 66 45 0f 61 e4               	punpcklwd	%xmm12, %xmm12  # xmm12 = xmm12[0,0,1,1,2,2,3,3]
     19c: 66 44 0f db e4               	pand	%xmm4, %xmm12
     1a1: 66 44 0f 74 d9               	pcmpeqb	%xmm1, %xmm11
     1a6: 66 45 0f 60 db               	punpcklbw	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
     1ab: 66 45 0f 61 db               	punpcklwd	%xmm11, %xmm11  # xmm11 = xmm11[0,0,1,1,2,2,3,3]
     1b0: 66 44 0f db dc               	pand	%xmm4, %xmm11
     1b5: 66 44 0f 6f ef               	movdqa	%xmm7, %xmm13
     1ba: 66 44 0f fa ee               	psubd	%xmm6, %xmm13
     1bf: 66 44 0f 6f f7               	movdqa	%xmm7, %xmm14
     1c4: 66 45 0f fe f1               	paddd	%xmm9, %xmm14
     1c9: 66 41 0f 72 f5 17            	pslld	$0x17, %xmm13
     1cf: 66 44 0f fe e8               	paddd	%xmm0, %xmm13
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
     20b: 66 44 0f fe f0               	paddd	%xmm0, %xmm14
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
     29e: 45 85 e4                     	testl	%r12d, %r12d
     2a1: 0f 84 39 fe ff ff            	je	0xe0 <tide_rows_Shift_5+0xe0>
     2a7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     2b0: f3 41 0f bc cc               	tzcntl	%r12d, %ecx
     2b5: 01 f1                        	addl	%esi, %ecx
     2b7: 44 89 df                     	movl	%r11d, %edi
     2ba: 41 ff c3                     	incl	%r11d
     2bd: 89 4c bc 20                  	movl	%ecx, 0x20(%rsp,%rdi,4)
     2c1: 41 8d 4c 24 ff               	leal	-0x1(%r12), %ecx
     2c6: 44 21 e1                     	andl	%r12d, %ecx
     2c9: 41 89 cc                     	movl	%ecx, %r12d
     2cc: 75 e2                        	jne	0x2b0 <tide_rows_Shift_5+0x2b0>
     2ce: e9 0d fe ff ff               	jmp	0xe0 <tide_rows_Shift_5+0xe0>
     2d3: 45 85 db                     	testl	%r11d, %r11d
     2d6: 48 8b b4 24 80 03 00 00      	movq	0x380(%rsp), %rsi
     2de: 48 8b 94 24 78 03 00 00      	movq	0x378(%rsp), %rdx
     2e6: 4c 8b bc 24 70 03 00 00      	movq	0x370(%rsp), %r15
     2ee: 4c 8b a4 24 68 03 00 00      	movq	0x368(%rsp), %r12
     2f6: 4c 8b 84 24 60 03 00 00      	movq	0x360(%rsp), %r8
     2fe: 48 8b bc 24 38 03 00 00      	movq	0x338(%rsp), %rdi
     306: 48 8b 9c 24 30 03 00 00      	movq	0x330(%rsp), %rbx
     30e: 48 8b 0c 24                  	movq	(%rsp), %rcx
     312: 0f 84 e2 03 00 00            	je	0x6fa <tide_rows_Shift_5+0x6fa>
     318: f3 0f 10 09                  	movss	(%rcx), %xmm1
     31c: 45 89 d9                     	movl	%r11d, %r9d
     31f: 41 83 fb 04                  	cmpl	$0x4, %r11d
     323: 73 08                        	jae	0x32d <tide_rows_Shift_5+0x32d>
     325: 45 31 f6                     	xorl	%r14d, %r14d
     328: e9 39 04 00 00               	jmp	0x766 <tide_rows_Shift_5+0x766>
     32d: 45 89 ce                     	movl	%r9d, %r14d
     330: 41 83 e6 fc                  	andl	$-0x4, %r14d
     334: 0f 28 d1                     	movaps	%xmm1, %xmm2
     337: 0f c6 d1 00                  	shufps	$0x0, %xmm1, %xmm2      # xmm2 = xmm2[0,0],xmm1[0,0]
     33b: 0f 29 14 24                  	movaps	%xmm2, (%rsp)
     33f: 4c 89 4c 24 10               	movq	%r9, 0x10(%rsp)
     344: 4a 8d 0c 8d 00 00 00 00      	leaq	(,%r9,4), %rcx
     34c: 48 83 e1 f0                  	andq	$-0x10, %rcx
     350: 48 89 4c 24 18               	movq	%rcx, 0x18(%rsp)
     355: 45 31 c9                     	xorl	%r9d, %r9d
     358: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
     360: 42 8b 74 0c 20               	movl	0x20(%rsp,%r9), %esi
     365: 46 8b 5c 0c 24               	movl	0x24(%rsp,%r9), %r11d
     36a: 42 8b 4c 0c 28               	movl	0x28(%rsp,%r9), %ecx
     36f: 46 8b 44 0c 2c               	movl	0x2c(%rsp,%r9), %r8d
     374: f3 42 0f 10 1c 83            	movss	(%rbx,%r8,4), %xmm3
     37a: f3 0f 10 24 8b               	movss	(%rbx,%rcx,4), %xmm4
     37f: 0f 14 e3                     	unpcklps	%xmm3, %xmm4            # xmm4 = xmm4[0],xmm3[0],xmm4[1],xmm3[1]
     382: f3 42 0f 10 1c 9b            	movss	(%rbx,%r11,4), %xmm3
     388: f3 0f 10 34 b3               	movss	(%rbx,%rsi,4), %xmm6
     38d: 0f 14 f3                     	unpcklps	%xmm3, %xmm6            # xmm6 = xmm6[0],xmm3[0],xmm6[1],xmm3[1]
     390: f3 42 0f 10 1c 87            	movss	(%rdi,%r8,4), %xmm3
     396: f3 0f 10 2c 8f               	movss	(%rdi,%rcx,4), %xmm5
     39b: f3 46 0f 10 04 9f            	movss	(%rdi,%r11,4), %xmm8
     3a1: 0f 14 eb                     	unpcklps	%xmm3, %xmm5            # xmm5 = xmm5[0],xmm3[0],xmm5[1],xmm3[1]
     3a4: f3 0f 10 3c b7               	movss	(%rdi,%rsi,4), %xmm7
     3a9: f3 43 0f 10 1c 82            	movss	(%r10,%r8,4), %xmm3
     3af: f3 45 0f 10 24 8a            	movss	(%r10,%rcx,4), %xmm12
     3b5: 41 0f 14 f8                  	unpcklps	%xmm8, %xmm7            # xmm7 = xmm7[0],xmm8[0],xmm7[1],xmm8[1]
     3b9: 44 0f 14 e3                  	unpcklps	%xmm3, %xmm12           # xmm12 = xmm12[0],xmm3[0],xmm12[1],xmm3[1]
     3bd: f3 43 0f 10 1c 9a            	movss	(%r10,%r11,4), %xmm3
     3c3: f3 45 0f 10 04 b2            	movss	(%r10,%rsi,4), %xmm8
     3c9: 44 0f 14 c3                  	unpcklps	%xmm3, %xmm8            # xmm8 = xmm8[0],xmm3[0],xmm8[1],xmm3[1]
     3cd: f3 42 0f 10 1c 80            	movss	(%rax,%r8,4), %xmm3
     3d3: f3 44 0f 10 2c 88            	movss	(%rax,%rcx,4), %xmm13
     3d9: f3 46 0f 10 14 98            	movss	(%rax,%r11,4), %xmm10
     3df: 44 0f 14 eb                  	unpcklps	%xmm3, %xmm13           # xmm13 = xmm13[0],xmm3[0],xmm13[1],xmm3[1]
     3e3: f3 44 0f 10 0c b0            	movss	(%rax,%rsi,4), %xmm9
     3e9: f3 42 0f 10 5c 85 00         	movss	(%rbp,%r8,4), %xmm3
     3f0: f3 44 0f 10 74 8d 00         	movss	(%rbp,%rcx,4), %xmm14
     3f7: 45 0f 14 ca                  	unpcklps	%xmm10, %xmm9           # xmm9 = xmm9[0],xmm10[0],xmm9[1],xmm10[1]
     3fb: 44 0f 14 f3                  	unpcklps	%xmm3, %xmm14           # xmm14 = xmm14[0],xmm3[0],xmm14[1],xmm3[1]
     3ff: f3 42 0f 10 5c 9d 00         	movss	(%rbp,%r11,4), %xmm3
     406: f3 44 0f 10 54 b5 00         	movss	(%rbp,%rsi,4), %xmm10
     40d: 44 0f 14 d3                  	unpcklps	%xmm3, %xmm10           # xmm10 = xmm10[0],xmm3[0],xmm10[1],xmm3[1]
     411: f3 47 0f 10 5c 85 00         	movss	(%r13,%r8,4), %xmm11
     418: f3 41 0f 10 5c 8d 00         	movss	(%r13,%rcx,4), %xmm3
     41f: f3 47 0f 10 7c 9d 00         	movss	(%r13,%r11,4), %xmm15
     426: 41 0f 14 db                  	unpcklps	%xmm11, %xmm3           # xmm3 = xmm3[0],xmm11[0],xmm3[1],xmm11[1]
     42a: f3 45 0f 10 5c b5 00         	movss	(%r13,%rsi,4), %xmm11
     431: 45 0f 14 df                  	unpcklps	%xmm15, %xmm11          # xmm11 = xmm11[0],xmm15[0],xmm11[1],xmm15[1]
     435: 48 8b 94 24 60 03 00 00      	movq	0x360(%rsp), %rdx
     43d: f3 42 0f 10 14 82            	movss	(%rdx,%r8,4), %xmm2
     443: 0f 16 f4                     	movlhps	%xmm4, %xmm6            # xmm6 = xmm6[0],xmm4[0]
     446: f3 44 0f 10 3c 8a            	movss	(%rdx,%rcx,4), %xmm15
     44c: 44 0f 14 fa                  	unpcklps	%xmm2, %xmm15           # xmm15 = xmm15[0],xmm2[0],xmm15[1],xmm2[1]
     450: f3 42 0f 10 14 9a            	movss	(%rdx,%r11,4), %xmm2
     456: 0f 16 fd                     	movlhps	%xmm5, %xmm7            # xmm7 = xmm7[0],xmm5[0]
     459: f3 0f 10 24 b2               	movss	(%rdx,%rsi,4), %xmm4
     45e: 0f 14 e2                     	unpcklps	%xmm2, %xmm4            # xmm4 = xmm4[0],xmm2[0],xmm4[1],xmm2[1]
     461: f3 43 0f 10 14 84            	movss	(%r12,%r8,4), %xmm2
     467: 45 0f 16 c4                  	movlhps	%xmm12, %xmm8           # xmm8 = xmm8[0],xmm12[0]
     46b: f3 45 0f 10 24 8c            	movss	(%r12,%rcx,4), %xmm12
     471: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     475: f3 43 0f 10 14 9c            	movss	(%r12,%r11,4), %xmm2
     47b: 45 0f 16 cd                  	movlhps	%xmm13, %xmm9           # xmm9 = xmm9[0],xmm13[0]
     47f: f3 41 0f 10 2c b4            	movss	(%r12,%rsi,4), %xmm5
     485: 0f 14 ea                     	unpcklps	%xmm2, %xmm5            # xmm5 = xmm5[0],xmm2[0],xmm5[1],xmm2[1]
     488: f3 43 0f 10 14 87            	movss	(%r15,%r8,4), %xmm2
     48e: 45 0f 16 d6                  	movlhps	%xmm14, %xmm10          # xmm10 = xmm10[0],xmm14[0]
     492: f3 45 0f 10 2c 8f            	movss	(%r15,%rcx,4), %xmm13
     498: 44 0f 14 ea                  	unpcklps	%xmm2, %xmm13           # xmm13 = xmm13[0],xmm2[0],xmm13[1],xmm2[1]
     49c: f3 43 0f 10 14 9f            	movss	(%r15,%r11,4), %xmm2
     4a2: 44 0f 16 db                  	movlhps	%xmm3, %xmm11           # xmm11 = xmm11[0],xmm3[0]
     4a6: f3 45 0f 10 34 b7            	movss	(%r15,%rsi,4), %xmm14
     4ac: 44 0f 14 f2                  	unpcklps	%xmm2, %xmm14           # xmm14 = xmm14[0],xmm2[0],xmm14[1],xmm2[1]
     4b0: 48 8b 94 24 78 03 00 00      	movq	0x378(%rsp), %rdx
     4b8: f3 42 0f 10 14 82            	movss	(%rdx,%r8,4), %xmm2
     4be: 41 0f 16 ec                  	movlhps	%xmm12, %xmm5           # xmm5 = xmm5[0],xmm12[0]
     4c2: f3 44 0f 10 24 8a            	movss	(%rdx,%rcx,4), %xmm12
     4c8: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     4cc: f3 42 0f 10 14 9a            	movss	(%rdx,%r11,4), %xmm2
     4d2: f3 0f 10 1c b2               	movss	(%rdx,%rsi,4), %xmm3
     4d7: 0f 14 da                     	unpcklps	%xmm2, %xmm3            # xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1]
     4da: 41 0f 16 dc                  	movlhps	%xmm12, %xmm3           # xmm3 = xmm3[0],xmm12[0]
     4de: 48 8b 94 24 80 03 00 00      	movq	0x380(%rsp), %rdx
     4e6: f3 42 0f 10 14 82            	movss	(%rdx,%r8,4), %xmm2
     4ec: 41 0f 16 e7                  	movlhps	%xmm15, %xmm4           # xmm4 = xmm4[0],xmm15[0]
     4f0: f3 44 0f 10 24 8a            	movss	(%rdx,%rcx,4), %xmm12
     4f6: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     4fa: f3 42 0f 10 14 9a            	movss	(%rdx,%r11,4), %xmm2
     500: 45 0f 16 f5                  	movlhps	%xmm13, %xmm14          # xmm14 = xmm14[0],xmm13[0]
     504: f3 44 0f 10 2c b2            	movss	(%rdx,%rsi,4), %xmm13
     50a: 44 0f 14 ea                  	unpcklps	%xmm2, %xmm13           # xmm13 = xmm13[0],xmm2[0],xmm13[1],xmm2[1]
     50e: 48 8b 94 24 88 03 00 00      	movq	0x388(%rsp), %rdx
     516: f3 42 0f 10 14 82            	movss	(%rdx,%r8,4), %xmm2
     51c: 45 0f 16 ec                  	movlhps	%xmm12, %xmm13          # xmm13 = xmm13[0],xmm12[0]
     520: f3 44 0f 10 3c 8a            	movss	(%rdx,%rcx,4), %xmm15
     526: 44 0f 14 fa                  	unpcklps	%xmm2, %xmm15           # xmm15 = xmm15[0],xmm2[0],xmm15[1],xmm2[1]
     52a: f3 42 0f 10 14 9a            	movss	(%rdx,%r11,4), %xmm2
     530: f3 44 0f 10 24 b2            	movss	(%rdx,%rsi,4), %xmm12
     536: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     53a: 45 0f 16 e7                  	movlhps	%xmm15, %xmm12          # xmm12 = xmm12[0],xmm15[0]
     53e: 44 0f 28 3c 24               	movaps	(%rsp), %xmm15
     543: 41 0f 59 e7                  	mulps	%xmm15, %xmm4
     547: 41 0f 58 e1                  	addps	%xmm9, %xmm4
     54b: 41 0f 59 ef                  	mulps	%xmm15, %xmm5
     54f: 41 0f 58 ea                  	addps	%xmm10, %xmm5
     553: 45 0f 59 f7                  	mulps	%xmm15, %xmm14
     557: 45 0f 58 f3                  	addps	%xmm11, %xmm14
     55b: 41 0f 59 df                  	mulps	%xmm15, %xmm3
     55f: 66 44 0f 6f c8               	movdqa	%xmm0, %xmm9
     564: 44 0f 5c cb                  	subps	%xmm3, %xmm9
     568: 41 0f 59 e1                  	mulps	%xmm9, %xmm4
     56c: 41 0f 59 e9                  	mulps	%xmm9, %xmm5
     570: 45 0f 59 ce                  	mulps	%xmm14, %xmm9
     574: 45 0f 28 d7                  	movaps	%xmm15, %xmm10
     578: 44 0f 59 d4                  	mulps	%xmm4, %xmm10
     57c: 44 0f 58 d6                  	addps	%xmm6, %xmm10
     580: 41 0f 28 d7                  	movaps	%xmm15, %xmm2
     584: 0f 59 d5                     	mulps	%xmm5, %xmm2
     587: 0f 58 d7                     	addps	%xmm7, %xmm2
     58a: 41 0f 28 ff                  	movaps	%xmm15, %xmm7
     58e: 41 0f 59 f9                  	mulps	%xmm9, %xmm7
     592: 41 0f 58 f8                  	addps	%xmm8, %xmm7
     596: 0f 28 f2                     	movaps	%xmm2, %xmm6
     599: 41 0f c2 f4 01               	cmpltps	%xmm12, %xmm6
     59e: 0f 28 de                     	movaps	%xmm6, %xmm3
     5a1: 0f 55 dd                     	andnps	%xmm5, %xmm3
     5a4: 0f 57 2d 00 00 00 00         	xorps	(%rip), %xmm5           # 0x5ab <tide_rows_Shift_5+0x5ab>
     5ab: 41 0f 59 ed                  	mulps	%xmm13, %xmm5
     5af: 45 0f 58 e4                  	addps	%xmm12, %xmm12
     5b3: 44 0f 5c e2                  	subps	%xmm2, %xmm12
     5b7: 0f 54 ee                     	andps	%xmm6, %xmm5
     5ba: 44 0f 54 e6                  	andps	%xmm6, %xmm12
     5be: 0f 55 f2                     	andnps	%xmm2, %xmm6
     5c1: f3 44 0f 11 14 b3            	movss	%xmm10, (%rbx,%rsi,4)
     5c7: 0f 56 eb                     	orps	%xmm3, %xmm5
     5ca: 41 0f 28 d2                  	movaps	%xmm10, %xmm2
     5ce: 41 0f c6 d2 55               	shufps	$0x55, %xmm10, %xmm2    # xmm2 = xmm2[1,1],xmm10[1,1]
     5d3: f3 42 0f 11 14 9b            	movss	%xmm2, (%rbx,%r11,4)
     5d9: 41 0f 56 f4                  	orps	%xmm12, %xmm6
     5dd: 41 0f 28 d2                  	movaps	%xmm10, %xmm2
     5e1: 66 41 0f 15 d2               	unpckhpd	%xmm10, %xmm2           # xmm2 = xmm2[1],xmm10[1]
     5e6: f3 0f 11 14 8b               	movss	%xmm2, (%rbx,%rcx,4)
     5eb: 45 0f c6 d2 ff               	shufps	$0xff, %xmm10, %xmm10   # xmm10 = xmm10[3,3,3,3]
     5f0: f3 46 0f 11 14 83            	movss	%xmm10, (%rbx,%r8,4)
     5f6: f3 0f 11 34 b7               	movss	%xmm6, (%rdi,%rsi,4)
     5fb: 66 0f 70 d6 55               	pshufd	$0x55, %xmm6, %xmm2     # xmm2 = xmm6[1,1,1,1]
     600: 66 42 0f 7e 14 9f            	movd	%xmm2, (%rdi,%r11,4)
     606: 66 0f 70 d6 ee               	pshufd	$0xee, %xmm6, %xmm2     # xmm2 = xmm6[2,3,2,3]
     60b: 66 0f 7e 14 8f               	movd	%xmm2, (%rdi,%rcx,4)
     610: f3 41 0f 11 3c b2            	movss	%xmm7, (%r10,%rsi,4)
     616: 0f 28 d7                     	movaps	%xmm7, %xmm2
     619: 0f c6 d7 55                  	shufps	$0x55, %xmm7, %xmm2     # xmm2 = xmm2[1,1],xmm7[1,1]
     61d: f3 43 0f 11 14 9a            	movss	%xmm2, (%r10,%r11,4)
     623: 0f 28 d7                     	movaps	%xmm7, %xmm2
     626: 66 0f 15 d7                  	unpckhpd	%xmm7, %xmm2            # xmm2 = xmm2[1],xmm7[1]
     62a: f3 41 0f 11 14 8a            	movss	%xmm2, (%r10,%rcx,4)
     630: 0f c6 ff ff                  	shufps	$0xff, %xmm7, %xmm7     # xmm7 = xmm7[3,3,3,3]
     634: f3 43 0f 11 3c 82            	movss	%xmm7, (%r10,%r8,4)
     63a: f3 0f 11 24 b0               	movss	%xmm4, (%rax,%rsi,4)
     63f: f3 0f 11 6c b5 00            	movss	%xmm5, (%rbp,%rsi,4)
     645: f3 45 0f 11 4c b5 00         	movss	%xmm9, (%r13,%rsi,4)
     64c: 66 0f 70 d6 ff               	pshufd	$0xff, %xmm6, %xmm2     # xmm2 = xmm6[3,3,3,3]
     651: 66 42 0f 7e 14 87            	movd	%xmm2, (%rdi,%r8,4)
     657: 0f 28 d4                     	movaps	%xmm4, %xmm2
     65a: 0f c6 d4 55                  	shufps	$0x55, %xmm4, %xmm2     # xmm2 = xmm2[1,1],xmm4[1,1]
     65e: f3 42 0f 11 14 98            	movss	%xmm2, (%rax,%r11,4)
     664: 0f 28 d4                     	movaps	%xmm4, %xmm2
     667: 66 0f 15 d4                  	unpckhpd	%xmm4, %xmm2            # xmm2 = xmm2[1],xmm4[1]
     66b: f3 0f 11 14 88               	movss	%xmm2, (%rax,%rcx,4)
     670: 66 0f 70 d5 55               	pshufd	$0x55, %xmm5, %xmm2     # xmm2 = xmm5[1,1,1,1]
     675: 66 42 0f 7e 54 9d 00         	movd	%xmm2, (%rbp,%r11,4)
     67c: 41 0f 28 d1                  	movaps	%xmm9, %xmm2
     680: 41 0f c6 d1 55               	shufps	$0x55, %xmm9, %xmm2     # xmm2 = xmm2[1,1],xmm9[1,1]
     685: f3 43 0f 11 54 9d 00         	movss	%xmm2, (%r13,%r11,4)
     68c: 0f c6 e4 ff                  	shufps	$0xff, %xmm4, %xmm4     # xmm4 = xmm4[3,3,3,3]
     690: f3 42 0f 11 24 80            	movss	%xmm4, (%rax,%r8,4)
     696: 66 0f 70 d5 ee               	pshufd	$0xee, %xmm5, %xmm2     # xmm2 = xmm5[2,3,2,3]
     69b: 66 0f 7e 54 8d 00            	movd	%xmm2, (%rbp,%rcx,4)
     6a1: 41 0f 28 d1                  	movaps	%xmm9, %xmm2
     6a5: 66 41 0f 15 d1               	unpckhpd	%xmm9, %xmm2            # xmm2 = xmm2[1],xmm9[1]
     6aa: f3 41 0f 11 54 8d 00         	movss	%xmm2, (%r13,%rcx,4)
     6b1: 66 0f 70 d5 ff               	pshufd	$0xff, %xmm5, %xmm2     # xmm2 = xmm5[3,3,3,3]
     6b6: 66 42 0f 7e 54 85 00         	movd	%xmm2, (%rbp,%r8,4)
     6bd: 45 0f c6 c9 ff               	shufps	$0xff, %xmm9, %xmm9     # xmm9 = xmm9[3,3,3,3]
     6c2: f3 47 0f 11 4c 85 00         	movss	%xmm9, (%r13,%r8,4)
     6c9: 49 83 c1 10                  	addq	$0x10, %r9
     6cd: 4c 39 4c 24 18               	cmpq	%r9, 0x18(%rsp)
     6d2: 0f 85 88 fc ff ff            	jne	0x360 <tide_rows_Shift_5+0x360>
     6d8: 4c 8b 4c 24 10               	movq	0x10(%rsp), %r9
     6dd: 45 39 ce                     	cmpl	%r9d, %r14d
     6e0: 48 8b b4 24 80 03 00 00      	movq	0x380(%rsp), %rsi
     6e8: 48 8b 94 24 78 03 00 00      	movq	0x378(%rsp), %rdx
     6f0: 4c 8b 84 24 60 03 00 00      	movq	0x360(%rsp), %r8
     6f8: 75 6c                        	jne	0x766 <tide_rows_Shift_5+0x766>
     6fa: 0f 28 b4 24 20 02 00 00      	movaps	0x220(%rsp), %xmm6
     702: 0f 28 bc 24 30 02 00 00      	movaps	0x230(%rsp), %xmm7
     70a: 44 0f 28 84 24 40 02 00 00   	movaps	0x240(%rsp), %xmm8
     713: 44 0f 28 8c 24 50 02 00 00   	movaps	0x250(%rsp), %xmm9
     71c: 44 0f 28 94 24 60 02 00 00   	movaps	0x260(%rsp), %xmm10
     725: 44 0f 28 9c 24 70 02 00 00   	movaps	0x270(%rsp), %xmm11
     72e: 44 0f 28 a4 24 80 02 00 00   	movaps	0x280(%rsp), %xmm12
     737: 44 0f 28 ac 24 90 02 00 00   	movaps	0x290(%rsp), %xmm13
     740: 44 0f 28 b4 24 a0 02 00 00   	movaps	0x2a0(%rsp), %xmm14
     749: 44 0f 28 bc 24 b0 02 00 00   	movaps	0x2b0(%rsp), %xmm15
     752: 48 81 c4 c8 02 00 00         	addq	$0x2c8, %rsp            # imm = 0x2C8
     759: 5b                           	popq	%rbx
     75a: 5d                           	popq	%rbp
     75b: 5f                           	popq	%rdi
     75c: 5e                           	popq	%rsi
     75d: 41 5c                        	popq	%r12
     75f: 41 5d                        	popq	%r13
     761: 41 5e                        	popq	%r14
     763: 41 5f                        	popq	%r15
     765: c3                           	retq
     766: f3 0f 10 05 00 00 00 00      	movss	(%rip), %xmm0           # 0x76e <tide_rows_Shift_5+0x76e>
     76e: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x775 <tide_rows_Shift_5+0x775>
     775: e9 a1 00 00 00               	jmp	0x81b <tide_rows_Shift_5+0x81b>
     77a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     780: 0f 28 f4                     	movaps	%xmm4, %xmm6
     783: f3 41 0f 10 3c 88            	movss	(%r8,%rcx,4), %xmm7
     789: f3 0f 59 f9                  	mulss	%xmm1, %xmm7
     78d: f3 45 0f 10 04 8f            	movss	(%r15,%rcx,4), %xmm8
     793: f3 44 0f 59 c1               	mulss	%xmm1, %xmm8
     798: f3 0f 58 3c 88               	addss	(%rax,%rcx,4), %xmm7
     79d: f3 45 0f 58 44 8d 00         	addss	(%r13,%rcx,4), %xmm8
     7a4: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     7a9: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     7ae: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     7b2: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     7b7: 44 0f 28 d1                  	movaps	%xmm1, %xmm10
     7bb: f3 44 0f 58 0c 8b            	addss	(%rbx,%rcx,4), %xmm9
     7c1: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     7c6: f3 45 0f 58 14 8a            	addss	(%r10,%rcx,4), %xmm10
     7cc: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     7d0: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     7d4: f3 44 0f 59 1c 8e            	mulss	(%rsi,%rcx,4), %xmm11
     7da: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     7df: 0f 28 ec                     	movaps	%xmm4, %xmm5
     7e2: 0f 55 eb                     	andnps	%xmm3, %xmm5
     7e5: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     7e9: 0f 56 e5                     	orps	%xmm5, %xmm4
     7ec: f3 44 0f 11 0c 8b            	movss	%xmm9, (%rbx,%rcx,4)
     7f2: f3 0f 11 34 8f               	movss	%xmm6, (%rdi,%rcx,4)
     7f7: f3 45 0f 11 14 8a            	movss	%xmm10, (%r10,%rcx,4)
     7fd: f3 0f 11 3c 88               	movss	%xmm7, (%rax,%rcx,4)
     802: f3 0f 11 64 8d 00            	movss	%xmm4, (%rbp,%rcx,4)
     808: f3 45 0f 11 44 8d 00         	movss	%xmm8, (%r13,%rcx,4)
     80f: 49 ff c6                     	incq	%r14
     812: 4d 39 f1                     	cmpq	%r14, %r9
     815: 0f 84 df fe ff ff            	je	0x6fa <tide_rows_Shift_5+0x6fa>
     81b: 42 8b 4c b4 20               	movl	0x20(%rsp,%r14,4), %ecx
     820: f3 41 0f 10 1c 8c            	movss	(%r12,%rcx,4), %xmm3
     826: f3 0f 59 d9                  	mulss	%xmm1, %xmm3
     82a: f3 0f 58 5c 8d 00            	addss	(%rbp,%rcx,4), %xmm3
     830: 4c 8b 9c 24 88 03 00 00      	movq	0x388(%rsp), %r11
     838: f3 41 0f 10 2c 8b            	movss	(%r11,%rcx,4), %xmm5
     83e: f3 0f 10 24 8a               	movss	(%rdx,%rcx,4), %xmm4
     843: f3 0f 59 e1                  	mulss	%xmm1, %xmm4
     847: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     84b: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     850: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     855: 0f 28 e1                     	movaps	%xmm1, %xmm4
     858: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     85c: f3 0f 58 24 8f               	addss	(%rdi,%rcx,4), %xmm4
     861: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     864: 0f 86 16 ff ff ff            	jbe	0x780 <tide_rows_Shift_5+0x780>
     86a: 0f 28 f5                     	movaps	%xmm5, %xmm6
     86d: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     871: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     875: e9 09 ff ff ff               	jmp	0x783 <tide_rows_Shift_5+0x783>
