
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-20261029/simd4/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec d8 00 00 00         	subq	$0xd8, %rsp
      13: 44 0f 29 bc 24 c0 00 00 00   	movaps	%xmm15, 0xc0(%rsp)
      1c: 44 0f 29 b4 24 b0 00 00 00   	movaps	%xmm14, 0xb0(%rsp)
      25: 44 0f 29 ac 24 a0 00 00 00   	movaps	%xmm13, 0xa0(%rsp)
      2e: 44 0f 29 a4 24 90 00 00 00   	movaps	%xmm12, 0x90(%rsp)
      37: 44 0f 29 9c 24 80 00 00 00   	movaps	%xmm11, 0x80(%rsp)
      40: 44 0f 29 54 24 70            	movaps	%xmm10, 0x70(%rsp)
      46: 44 0f 29 4c 24 60            	movaps	%xmm9, 0x60(%rsp)
      4c: 44 0f 29 44 24 50            	movaps	%xmm8, 0x50(%rsp)
      52: 0f 29 7c 24 40               	movaps	%xmm7, 0x40(%rsp)
      57: 0f 29 74 24 30               	movaps	%xmm6, 0x30(%rsp)
      5c: 45 89 c6                     	movl	%r8d, %r14d
      5f: 4c 8b bc 24 80 01 00 00      	movq	0x180(%rsp), %r15
      67: 48 8b ac 24 68 01 00 00      	movq	0x168(%rsp), %rbp
      6f: 48 8b 84 24 60 01 00 00      	movq	0x160(%rsp), %rax
      77: 4c 8b 84 24 58 01 00 00      	movq	0x158(%rsp), %r8
      7f: 48 8b bc 24 50 01 00 00      	movq	0x150(%rsp), %rdi
      87: 4c 8b a4 24 48 01 00 00      	movq	0x148(%rsp), %r12
      8f: f3 0f 10 01                  	movss	(%rcx), %xmm0
      93: 44 89 74 24 0c               	movl	%r14d, 0xc(%rsp)
      98: 41 29 d6                     	subl	%edx, %r14d
      9b: 41 83 fe 04                  	cmpl	$0x4, %r14d
      9f: 0f 29 44 24 10               	movaps	%xmm0, 0x10(%rsp)
      a4: 0f 82 a5 03 00 00            	jb	0x44f <tide_rows_Shift_6+0x44f>
      aa: 0f c6 c0 00                  	shufps	$0x0, %xmm0, %xmm0      # xmm0 = xmm0[0,0,0,0]
      ae: 0f 29 44 24 20               	movaps	%xmm0, 0x20(%rsp)
      b3: 48 89 c1                     	movq	%rax, %rcx
      b6: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
      c0: 89 d0                        	movl	%edx, %eax
      c2: 41 0f b7 74 41 0a            	movzwl	0xa(%r9,%rax,2), %esi
      c8: 8d 42 01                     	leal	0x1(%rdx), %eax
      cb: 45 0f b7 5c 41 0a            	movzwl	0xa(%r9,%rax,2), %r11d
      d1: 8d 42 02                     	leal	0x2(%rdx), %eax
      d4: 45 0f b7 54 41 0a            	movzwl	0xa(%r9,%rax,2), %r10d
      da: 8d 42 03                     	leal	0x3(%rdx), %eax
      dd: 41 0f b7 44 41 0a            	movzwl	0xa(%r9,%rax,2), %eax
      e3: c1 e6 02                     	shll	$0x2, %esi
      e6: 41 c1 e3 02                  	shll	$0x2, %r11d
      ea: 41 c1 e2 02                  	shll	$0x2, %r10d
      ee: c1 e0 02                     	shll	$0x2, %eax
      f1: 4c 8b ac 24 40 01 00 00      	movq	0x140(%rsp), %r13
      f9: f3 41 0f 10 54 05 00         	movss	(%r13,%rax), %xmm2
     100: f3 43 0f 10 64 15 00         	movss	(%r13,%r10), %xmm4
     107: f3 43 0f 10 5c 1d 00         	movss	(%r13,%r11), %xmm3
     10e: 0f 14 e2                     	unpcklps	%xmm2, %xmm4            # xmm4 = xmm4[0],xmm2[0],xmm4[1],xmm2[1]
     111: f3 45 0f 10 44 35 00         	movss	(%r13,%rsi), %xmm8
     118: f3 41 0f 10 14 04            	movss	(%r12,%rax), %xmm2
     11e: f3 43 0f 10 2c 14            	movss	(%r12,%r10), %xmm5
     124: 44 0f 14 c3                  	unpcklps	%xmm3, %xmm8            # xmm8 = xmm8[0],xmm3[0],xmm8[1],xmm3[1]
     128: 0f 14 ea                     	unpcklps	%xmm2, %xmm5            # xmm5 = xmm5[0],xmm2[0],xmm5[1],xmm2[1]
     12b: f3 43 0f 10 14 1c            	movss	(%r12,%r11), %xmm2
     131: f3 45 0f 10 0c 34            	movss	(%r12,%rsi), %xmm9
     137: 44 0f 14 ca                  	unpcklps	%xmm2, %xmm9            # xmm9 = xmm9[0],xmm2[0],xmm9[1],xmm2[1]
     13b: f3 0f 10 14 07               	movss	(%rdi,%rax), %xmm2
     140: f3 42 0f 10 3c 17            	movss	(%rdi,%r10), %xmm7
     146: f3 42 0f 10 1c 1f            	movss	(%rdi,%r11), %xmm3
     14c: 0f 14 fa                     	unpcklps	%xmm2, %xmm7            # xmm7 = xmm7[0],xmm2[0],xmm7[1],xmm2[1]
     14f: f3 44 0f 10 14 37            	movss	(%rdi,%rsi), %xmm10
     155: f3 41 0f 10 14 00            	movss	(%r8,%rax), %xmm2
     15b: f3 47 0f 10 1c 10            	movss	(%r8,%r10), %xmm11
     161: 44 0f 14 d3                  	unpcklps	%xmm3, %xmm10           # xmm10 = xmm10[0],xmm3[0],xmm10[1],xmm3[1]
     165: 44 0f 14 da                  	unpcklps	%xmm2, %xmm11           # xmm11 = xmm11[0],xmm2[0],xmm11[1],xmm2[1]
     169: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     16f: f3 45 0f 10 2c 30            	movss	(%r8,%rsi), %xmm13
     175: 44 0f 14 ea                  	unpcklps	%xmm2, %xmm13           # xmm13 = xmm13[0],xmm2[0],xmm13[1],xmm2[1]
     179: f3 0f 10 14 01               	movss	(%rcx,%rax), %xmm2
     17e: f3 46 0f 10 3c 11            	movss	(%rcx,%r10), %xmm15
     184: f3 42 0f 10 34 19            	movss	(%rcx,%r11), %xmm6
     18a: 44 0f 14 fa                  	unpcklps	%xmm2, %xmm15           # xmm15 = xmm15[0],xmm2[0],xmm15[1],xmm2[1]
     18e: f3 44 0f 10 34 31            	movss	(%rcx,%rsi), %xmm14
     194: f3 0f 10 54 05 00            	movss	(%rbp,%rax), %xmm2
     19a: f3 42 0f 10 5c 15 00         	movss	(%rbp,%r10), %xmm3
     1a1: 44 0f 14 f6                  	unpcklps	%xmm6, %xmm14           # xmm14 = xmm14[0],xmm6[0],xmm14[1],xmm6[1]
     1a5: 0f 14 da                     	unpcklps	%xmm2, %xmm3            # xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1]
     1a8: f3 42 0f 10 54 1d 00         	movss	(%rbp,%r11), %xmm2
     1af: f3 44 0f 10 64 35 00         	movss	(%rbp,%rsi), %xmm12
     1b6: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     1ba: 48 8b 9c 24 70 01 00 00      	movq	0x170(%rsp), %rbx
     1c2: f3 0f 10 34 03               	movss	(%rbx,%rax), %xmm6
     1c7: f3 42 0f 10 14 13            	movss	(%rbx,%r10), %xmm2
     1cd: f3 42 0f 10 04 1b            	movss	(%rbx,%r11), %xmm0
     1d3: 0f 14 d6                     	unpcklps	%xmm6, %xmm2            # xmm2 = xmm2[0],xmm6[0],xmm2[1],xmm6[1]
     1d6: f3 0f 10 34 33               	movss	(%rbx,%rsi), %xmm6
     1db: 0f 14 f0                     	unpcklps	%xmm0, %xmm6            # xmm6 = xmm6[0],xmm0[0],xmm6[1],xmm0[1]
     1de: 48 8b 9c 24 78 01 00 00      	movq	0x178(%rsp), %rbx
     1e6: f3 0f 10 04 03               	movss	(%rbx,%rax), %xmm0
     1eb: 44 0f 16 c4                  	movlhps	%xmm4, %xmm8            # xmm8 = xmm8[0],xmm4[0]
     1ef: f3 42 0f 10 0c 13            	movss	(%rbx,%r10), %xmm1
     1f5: 0f 14 c8                     	unpcklps	%xmm0, %xmm1            # xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
     1f8: f3 42 0f 10 04 1b            	movss	(%rbx,%r11), %xmm0
     1fe: 44 0f 16 cd                  	movlhps	%xmm5, %xmm9            # xmm9 = xmm9[0],xmm5[0]
     202: f3 0f 10 2c 33               	movss	(%rbx,%rsi), %xmm5
     207: 0f 14 e8                     	unpcklps	%xmm0, %xmm5            # xmm5 = xmm5[0],xmm0[0],xmm5[1],xmm0[1]
     20a: f3 41 0f 10 04 07            	movss	(%r15,%rax), %xmm0
     210: 44 0f 16 d7                  	movlhps	%xmm7, %xmm10           # xmm10 = xmm10[0],xmm7[0]
     214: f3 43 0f 10 3c 17            	movss	(%r15,%r10), %xmm7
     21a: 0f 14 f8                     	unpcklps	%xmm0, %xmm7            # xmm7 = xmm7[0],xmm0[0],xmm7[1],xmm0[1]
     21d: f3 43 0f 10 04 1f            	movss	(%r15,%r11), %xmm0
     223: 45 0f 16 eb                  	movlhps	%xmm11, %xmm13          # xmm13 = xmm13[0],xmm11[0]
     227: f3 41 0f 10 24 37            	movss	(%r15,%rsi), %xmm4
     22d: 0f 14 e0                     	unpcklps	%xmm0, %xmm4            # xmm4 = xmm4[0],xmm0[0],xmm4[1],xmm0[1]
     230: 48 8b 9c 24 88 01 00 00      	movq	0x188(%rsp), %rbx
     238: f3 0f 10 04 03               	movss	(%rbx,%rax), %xmm0
     23d: 45 0f 16 f7                  	movlhps	%xmm15, %xmm14          # xmm14 = xmm14[0],xmm15[0]
     241: f3 46 0f 10 1c 13            	movss	(%rbx,%r10), %xmm11
     247: 44 0f 14 d8                  	unpcklps	%xmm0, %xmm11           # xmm11 = xmm11[0],xmm0[0],xmm11[1],xmm0[1]
     24b: f3 42 0f 10 04 1b            	movss	(%rbx,%r11), %xmm0
     251: 0f 16 e9                     	movlhps	%xmm1, %xmm5            # xmm5 = xmm5[0],xmm1[0]
     254: f3 0f 10 0c 33               	movss	(%rbx,%rsi), %xmm1
     259: 0f 14 c8                     	unpcklps	%xmm0, %xmm1            # xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
     25c: 41 0f 16 cb                  	movlhps	%xmm11, %xmm1           # xmm1 = xmm1[0],xmm11[0]
     260: 44 0f 16 e3                  	movlhps	%xmm3, %xmm12           # xmm12 = xmm12[0],xmm3[0]
     264: 48 8b 9c 24 90 01 00 00      	movq	0x190(%rsp), %rbx
     26c: f3 0f 10 04 03               	movss	(%rbx,%rax), %xmm0
     271: f3 42 0f 10 1c 13            	movss	(%rbx,%r10), %xmm3
     277: 0f 14 d8                     	unpcklps	%xmm0, %xmm3            # xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1]
     27a: 0f 16 f2                     	movlhps	%xmm2, %xmm6            # xmm6 = xmm6[0],xmm2[0]
     27d: f3 42 0f 10 04 1b            	movss	(%rbx,%r11), %xmm0
     283: f3 44 0f 10 1c 33            	movss	(%rbx,%rsi), %xmm11
     289: 44 0f 14 d8                  	unpcklps	%xmm0, %xmm11           # xmm11 = xmm11[0],xmm0[0],xmm11[1],xmm0[1]
     28d: 0f 16 e7                     	movlhps	%xmm7, %xmm4            # xmm4 = xmm4[0],xmm7[0]
     290: 48 8b 9c 24 98 01 00 00      	movq	0x198(%rsp), %rbx
     298: f3 0f 10 04 03               	movss	(%rbx,%rax), %xmm0
     29d: f3 42 0f 10 14 13            	movss	(%rbx,%r10), %xmm2
     2a3: 0f 14 d0                     	unpcklps	%xmm0, %xmm2            # xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1]
     2a6: 44 0f 16 db                  	movlhps	%xmm3, %xmm11           # xmm11 = xmm11[0],xmm3[0]
     2aa: f3 42 0f 10 04 1b            	movss	(%rbx,%r11), %xmm0
     2b0: f3 0f 10 3c 33               	movss	(%rbx,%rsi), %xmm7
     2b5: 0f 14 f8                     	unpcklps	%xmm0, %xmm7            # xmm7 = xmm7[0],xmm0[0],xmm7[1],xmm0[1]
     2b8: 0f 16 fa                     	movlhps	%xmm2, %xmm7            # xmm7 = xmm7[0],xmm2[0]
     2bb: 0f 28 54 24 20               	movaps	0x20(%rsp), %xmm2
     2c0: 0f 59 ca                     	mulps	%xmm2, %xmm1
     2c3: 0f 28 05 00 00 00 00         	movaps	(%rip), %xmm0           # 0x2ca <tide_rows_Shift_6+0x2ca>
     2ca: 0f 5c c1                     	subps	%xmm1, %xmm0
     2cd: 0f 59 f2                     	mulps	%xmm2, %xmm6
     2d0: 41 0f 58 f5                  	addps	%xmm13, %xmm6
     2d4: 0f 59 ea                     	mulps	%xmm2, %xmm5
     2d7: 41 0f 58 ee                  	addps	%xmm14, %xmm5
     2db: 0f 59 f0                     	mulps	%xmm0, %xmm6
     2de: 0f 28 da                     	movaps	%xmm2, %xmm3
     2e1: 0f 59 de                     	mulps	%xmm6, %xmm3
     2e4: 0f 59 e8                     	mulps	%xmm0, %xmm5
     2e7: 41 0f 58 d8                  	addps	%xmm8, %xmm3
     2eb: 44 0f 28 c2                  	movaps	%xmm2, %xmm8
     2ef: 44 0f 59 c5                  	mulps	%xmm5, %xmm8
     2f3: 45 0f 58 c1                  	addps	%xmm9, %xmm8
     2f7: 0f 59 e2                     	mulps	%xmm2, %xmm4
     2fa: 41 0f 58 e4                  	addps	%xmm12, %xmm4
     2fe: 0f 59 e0                     	mulps	%xmm0, %xmm4
     301: 44 0f 28 ca                  	movaps	%xmm2, %xmm9
     305: 44 0f 59 cc                  	mulps	%xmm4, %xmm9
     309: 45 0f 58 ca                  	addps	%xmm10, %xmm9
     30d: f3 41 0f 11 5c 35 00         	movss	%xmm3, (%r13,%rsi)
     314: 0f 28 c3                     	movaps	%xmm3, %xmm0
     317: 0f c6 c3 55                  	shufps	$0x55, %xmm3, %xmm0     # xmm0 = xmm0[1,1],xmm3[1,1]
     31b: f3 43 0f 11 44 1d 00         	movss	%xmm0, (%r13,%r11)
     322: 0f 28 c3                     	movaps	%xmm3, %xmm0
     325: 66 0f 15 c3                  	unpckhpd	%xmm3, %xmm0            # xmm0 = xmm0[1],xmm3[1]
     329: f3 43 0f 11 44 15 00         	movss	%xmm0, (%r13,%r10)
     330: 41 0f 28 d0                  	movaps	%xmm8, %xmm2
     334: 0f c2 d7 01                  	cmpltps	%xmm7, %xmm2
     338: 0f c6 db ff                  	shufps	$0xff, %xmm3, %xmm3     # xmm3 = xmm3[3,3,3,3]
     33c: f3 41 0f 11 5c 05 00         	movss	%xmm3, (%r13,%rax)
     343: f3 44 0f 11 0c 37            	movss	%xmm9, (%rdi,%rsi)
     349: 41 0f 28 c1                  	movaps	%xmm9, %xmm0
     34d: 41 0f c6 c1 55               	shufps	$0x55, %xmm9, %xmm0     # xmm0 = xmm0[1,1],xmm9[1,1]
     352: f3 42 0f 11 04 1f            	movss	%xmm0, (%rdi,%r11)
     358: 0f 28 c2                     	movaps	%xmm2, %xmm0
     35b: 0f 55 c5                     	andnps	%xmm5, %xmm0
     35e: 0f 57 2d 00 00 00 00         	xorps	(%rip), %xmm5           # 0x365 <tide_rows_Shift_6+0x365>
     365: 41 0f 59 eb                  	mulps	%xmm11, %xmm5
     369: 41 0f 28 c9                  	movaps	%xmm9, %xmm1
     36d: 66 41 0f 15 c9               	unpckhpd	%xmm9, %xmm1            # xmm1 = xmm1[1],xmm9[1]
     372: f3 42 0f 11 0c 17            	movss	%xmm1, (%rdi,%r10)
     378: 45 0f c6 c9 ff               	shufps	$0xff, %xmm9, %xmm9     # xmm9 = xmm9[3,3,3,3]
     37d: f3 44 0f 11 0c 07            	movss	%xmm9, (%rdi,%rax)
     383: 0f 58 ff                     	addps	%xmm7, %xmm7
     386: 41 0f 5c f8                  	subps	%xmm8, %xmm7
     38a: 0f 54 fa                     	andps	%xmm2, %xmm7
     38d: 0f 54 ea                     	andps	%xmm2, %xmm5
     390: f3 41 0f 11 34 30            	movss	%xmm6, (%r8,%rsi)
     396: 41 0f 55 d0                  	andnps	%xmm8, %xmm2
     39a: 0f 28 ce                     	movaps	%xmm6, %xmm1
     39d: 0f c6 ce 55                  	shufps	$0x55, %xmm6, %xmm1     # xmm1 = xmm1[1,1],xmm6[1,1]
     3a1: f3 43 0f 11 0c 18            	movss	%xmm1, (%r8,%r11)
     3a7: 0f 56 e8                     	orps	%xmm0, %xmm5
     3aa: 0f 28 c6                     	movaps	%xmm6, %xmm0
     3ad: 66 0f 15 c6                  	unpckhpd	%xmm6, %xmm0            # xmm0 = xmm0[1],xmm6[1]
     3b1: f3 43 0f 11 04 10            	movss	%xmm0, (%r8,%r10)
     3b7: 0f 56 fa                     	orps	%xmm2, %xmm7
     3ba: 0f c6 f6 ff                  	shufps	$0xff, %xmm6, %xmm6     # xmm6 = xmm6[3,3,3,3]
     3be: f3 41 0f 11 34 00            	movss	%xmm6, (%r8,%rax)
     3c4: f3 0f 11 2c 31               	movss	%xmm5, (%rcx,%rsi)
     3c9: f3 41 0f 11 3c 34            	movss	%xmm7, (%r12,%rsi)
     3cf: 66 0f 70 c5 55               	pshufd	$0x55, %xmm5, %xmm0     # xmm0 = xmm5[1,1,1,1]
     3d4: 66 42 0f 7e 04 19            	movd	%xmm0, (%rcx,%r11)
     3da: 66 0f 70 c7 55               	pshufd	$0x55, %xmm7, %xmm0     # xmm0 = xmm7[1,1,1,1]
     3df: 66 43 0f 7e 04 1c            	movd	%xmm0, (%r12,%r11)
     3e5: 66 0f 70 c5 ee               	pshufd	$0xee, %xmm5, %xmm0     # xmm0 = xmm5[2,3,2,3]
     3ea: 66 42 0f 7e 04 11            	movd	%xmm0, (%rcx,%r10)
     3f0: 66 0f 70 c7 ee               	pshufd	$0xee, %xmm7, %xmm0     # xmm0 = xmm7[2,3,2,3]
     3f5: 66 43 0f 7e 04 14            	movd	%xmm0, (%r12,%r10)
     3fb: f3 0f 11 64 35 00            	movss	%xmm4, (%rbp,%rsi)
     401: 66 0f 70 c5 ff               	pshufd	$0xff, %xmm5, %xmm0     # xmm0 = xmm5[3,3,3,3]
     406: 66 0f 7e 04 01               	movd	%xmm0, (%rcx,%rax)
     40b: 0f 28 c4                     	movaps	%xmm4, %xmm0
     40e: 0f c6 c4 55                  	shufps	$0x55, %xmm4, %xmm0     # xmm0 = xmm0[1,1],xmm4[1,1]
     412: f3 42 0f 11 44 1d 00         	movss	%xmm0, (%rbp,%r11)
     419: 66 0f 70 c7 ff               	pshufd	$0xff, %xmm7, %xmm0     # xmm0 = xmm7[3,3,3,3]
     41e: 66 41 0f 7e 04 04            	movd	%xmm0, (%r12,%rax)
     424: 0f 28 c4                     	movaps	%xmm4, %xmm0
     427: 66 0f 15 c4                  	unpckhpd	%xmm4, %xmm0            # xmm0 = xmm0[1],xmm4[1]
     42b: f3 42 0f 11 44 15 00         	movss	%xmm0, (%rbp,%r10)
     432: 0f c6 e4 ff                  	shufps	$0xff, %xmm4, %xmm4     # xmm4 = xmm4[3,3,3,3]
     436: f3 0f 11 64 05 00            	movss	%xmm4, (%rbp,%rax)
     43c: 83 c2 04                     	addl	$0x4, %edx
     43f: 41 83 c6 fc                  	addl	$-0x4, %r14d
     443: 41 83 fe 03                  	cmpl	$0x3, %r14d
     447: 0f 87 73 fc ff ff            	ja	0xc0 <tide_rows_Shift_6+0xc0>
     44d: eb 03                        	jmp	0x452 <tide_rows_Shift_6+0x452>
     44f: 48 89 c1                     	movq	%rax, %rcx
     452: 44 8b 54 24 0c               	movl	0xc(%rsp), %r10d
     457: 44 39 d2                     	cmpl	%r10d, %edx
     45a: 4c 8b b4 24 88 01 00 00      	movq	0x188(%rsp), %r14
     462: 48 8b b4 24 78 01 00 00      	movq	0x178(%rsp), %rsi
     46a: 4c 8b ac 24 70 01 00 00      	movq	0x170(%rsp), %r13
     472: 0f 28 7c 24 10               	movaps	0x10(%rsp), %xmm7
     477: 0f 83 25 01 00 00            	jae	0x5a2 <tide_rows_Shift_6+0x5a2>
     47d: 4d 89 c3                     	movq	%r8, %r11
     480: 89 d0                        	movl	%edx, %eax
     482: 44 89 d2                     	movl	%r10d, %edx
     485: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x48d <tide_rows_Shift_6+0x48d>
     48d: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x494 <tide_rows_Shift_6+0x494>
     494: eb 5c                        	jmp	0x4f2 <tide_rows_Shift_6+0x4f2>
     496: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     4a0: 46 8d 04 85 00 00 00 00      	leal	(,%r8,4), %r8d
     4a8: 49 89 dc                     	movq	%rbx, %r12
     4ab: f3 42 0f 11 2c 03            	movss	%xmm5, (%rbx,%r8)
     4b1: f3 42 0f 11 24 01            	movss	%xmm4, (%rcx,%r8)
     4b7: f3 43 0f 10 04 07            	movss	(%r15,%r8), %xmm0
     4bd: f3 0f 59 c7                  	mulss	%xmm7, %xmm0
     4c1: f3 42 0f 58 44 05 00         	addss	(%rbp,%r8), %xmm0
     4c8: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     4cc: 0f 28 c7                     	movaps	%xmm7, %xmm0
     4cf: f3 0f 59 c3                  	mulss	%xmm3, %xmm0
     4d3: f3 42 0f 58 04 07            	addss	(%rdi,%r8), %xmm0
     4d9: f3 42 0f 11 04 07            	movss	%xmm0, (%rdi,%r8)
     4df: f3 42 0f 11 5c 05 00         	movss	%xmm3, (%rbp,%r8)
     4e6: 48 ff c0                     	incq	%rax
     4e9: 48 39 c2                     	cmpq	%rax, %rdx
     4ec: 0f 84 b0 00 00 00            	je	0x5a2 <tide_rows_Shift_6+0x5a2>
     4f2: 45 0f b7 44 41 0a            	movzwl	0xa(%r9,%rax,2), %r8d
     4f8: 45 89 c2                     	movl	%r8d, %r10d
     4fb: 41 c1 e2 02                  	shll	$0x2, %r10d
     4ff: f3 43 0f 10 44 15 00         	movss	(%r13,%r10), %xmm0
     506: f3 0f 59 c7                  	mulss	%xmm7, %xmm0
     50a: f3 43 0f 58 04 13            	addss	(%r11,%r10), %xmm0
     510: f3 43 0f 10 24 16            	movss	(%r14,%r10), %xmm4
     516: f3 0f 59 e7                  	mulss	%xmm7, %xmm4
     51a: 0f 28 d9                     	movaps	%xmm1, %xmm3
     51d: f3 0f 5c dc                  	subss	%xmm4, %xmm3
     521: f3 0f 59 c3                  	mulss	%xmm3, %xmm0
     525: 0f 28 e7                     	movaps	%xmm7, %xmm4
     528: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     52c: 48 8b 9c 24 40 01 00 00      	movq	0x140(%rsp), %rbx
     534: f3 42 0f 58 24 13            	addss	(%rbx,%r10), %xmm4
     53a: f3 42 0f 11 24 13            	movss	%xmm4, (%rbx,%r10)
     540: f3 42 0f 10 24 16            	movss	(%rsi,%r10), %xmm4
     546: f3 0f 59 e7                  	mulss	%xmm7, %xmm4
     54a: f3 42 0f 58 24 11            	addss	(%rcx,%r10), %xmm4
     550: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     554: 0f 28 ef                     	movaps	%xmm7, %xmm5
     557: f3 0f 59 ec                  	mulss	%xmm4, %xmm5
     55b: 4c 89 e3                     	movq	%r12, %rbx
     55e: f3 43 0f 58 2c 14            	addss	(%r12,%r10), %xmm5
     564: f3 43 0f 11 04 13            	movss	%xmm0, (%r11,%r10)
     56a: 4c 8b a4 24 98 01 00 00      	movq	0x198(%rsp), %r12
     572: f3 43 0f 10 34 14            	movss	(%r12,%r10), %xmm6
     578: 0f 2e f5                     	ucomiss	%xmm5, %xmm6
     57b: 0f 86 1f ff ff ff            	jbe	0x4a0 <tide_rows_Shift_6+0x4a0>
     581: f3 0f 58 f6                  	addss	%xmm6, %xmm6
     585: f3 0f 5c f5                  	subss	%xmm5, %xmm6
     589: 0f 57 e2                     	xorps	%xmm2, %xmm4
     58c: 4c 8b 94 24 90 01 00 00      	movq	0x190(%rsp), %r10
     594: f3 43 0f 59 24 82            	mulss	(%r10,%r8,4), %xmm4
     59a: 0f 28 ee                     	movaps	%xmm6, %xmm5
     59d: e9 fe fe ff ff               	jmp	0x4a0 <tide_rows_Shift_6+0x4a0>
     5a2: 0f 28 74 24 30               	movaps	0x30(%rsp), %xmm6
     5a7: 0f 28 7c 24 40               	movaps	0x40(%rsp), %xmm7
     5ac: 44 0f 28 44 24 50            	movaps	0x50(%rsp), %xmm8
     5b2: 44 0f 28 4c 24 60            	movaps	0x60(%rsp), %xmm9
     5b8: 44 0f 28 54 24 70            	movaps	0x70(%rsp), %xmm10
     5be: 44 0f 28 9c 24 80 00 00 00   	movaps	0x80(%rsp), %xmm11
     5c7: 44 0f 28 a4 24 90 00 00 00   	movaps	0x90(%rsp), %xmm12
     5d0: 44 0f 28 ac 24 a0 00 00 00   	movaps	0xa0(%rsp), %xmm13
     5d9: 44 0f 28 b4 24 b0 00 00 00   	movaps	0xb0(%rsp), %xmm14
     5e2: 44 0f 28 bc 24 c0 00 00 00   	movaps	0xc0(%rsp), %xmm15
     5eb: 48 81 c4 d8 00 00 00         	addq	$0xd8, %rsp
     5f2: 5b                           	popq	%rbx
     5f3: 5d                           	popq	%rbp
     5f4: 5f                           	popq	%rdi
     5f5: 5e                           	popq	%rsi
     5f6: 41 5c                        	popq	%r12
     5f8: 41 5d                        	popq	%r13
     5fa: 41 5e                        	popq	%r14
     5fc: 41 5f                        	popq	%r15
     5fe: c3                           	retq
