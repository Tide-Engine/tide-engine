
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/vector4/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec c8 00 00 00         	subq	$0xc8, %rsp
      13: 44 0f 29 bc 24 b0 00 00 00   	movaps	%xmm15, 0xb0(%rsp)
      1c: 44 0f 29 b4 24 a0 00 00 00   	movaps	%xmm14, 0xa0(%rsp)
      25: 44 0f 29 ac 24 90 00 00 00   	movaps	%xmm13, 0x90(%rsp)
      2e: 44 0f 29 a4 24 80 00 00 00   	movaps	%xmm12, 0x80(%rsp)
      37: 44 0f 29 5c 24 70            	movaps	%xmm11, 0x70(%rsp)
      3d: 44 0f 29 54 24 60            	movaps	%xmm10, 0x60(%rsp)
      43: 44 0f 29 4c 24 50            	movaps	%xmm9, 0x50(%rsp)
      49: 44 0f 29 44 24 40            	movaps	%xmm8, 0x40(%rsp)
      4f: 0f 29 7c 24 30               	movaps	%xmm7, 0x30(%rsp)
      54: 0f 29 74 24 20               	movaps	%xmm6, 0x20(%rsp)
      59: 44 39 c2                     	cmpl	%r8d, %edx
      5c: 0f 83 79 04 00 00            	jae	0x4db <tide_rows_Shift_6+0x4db>
      62: 48 8b bc 24 88 01 00 00      	movq	0x188(%rsp), %rdi
      6a: 48 8b 9c 24 80 01 00 00      	movq	0x180(%rsp), %rbx
      72: 4c 8b 94 24 78 01 00 00      	movq	0x178(%rsp), %r10
      7a: 4c 8b 9c 24 70 01 00 00      	movq	0x170(%rsp), %r11
      82: 48 8b b4 24 68 01 00 00      	movq	0x168(%rsp), %rsi
      8a: 4c 8b b4 24 58 01 00 00      	movq	0x158(%rsp), %r14
      92: 4c 8b bc 24 50 01 00 00      	movq	0x150(%rsp), %r15
      9a: 4c 8b a4 24 48 01 00 00      	movq	0x148(%rsp), %r12
      a2: 4c 8b ac 24 40 01 00 00      	movq	0x140(%rsp), %r13
      aa: 48 8b ac 24 38 01 00 00      	movq	0x138(%rsp), %rbp
      b2: 48 8b 84 24 30 01 00 00      	movq	0x130(%rsp), %rax
      ba: f3 0f 10 01                  	movss	(%rcx), %xmm0
      be: 89 d1                        	movl	%edx, %ecx
      c0: 44 89 c2                     	movl	%r8d, %edx
      c3: 48 89 54 24 18               	movq	%rdx, 0x18(%rsp)
      c8: 48 29 ca                     	subq	%rcx, %rdx
      cb: 48 83 fa 04                  	cmpq	$0x4, %rdx
      cf: 73 10                        	jae	0xe1 <tide_rows_Shift_6+0xe1>
      d1: 48 89 ca                     	movq	%rcx, %rdx
      d4: 4c 8b 84 24 60 01 00 00      	movq	0x160(%rsp), %r8
      dc: e9 54 04 00 00               	jmp	0x535 <tide_rows_Shift_6+0x535>
      e1: 48 89 54 24 08               	movq	%rdx, 0x8(%rsp)
      e6: 48 83 e2 fc                  	andq	$-0x4, %rdx
      ea: 48 89 14 24                  	movq	%rdx, (%rsp)
      ee: 48 01 ca                     	addq	%rcx, %rdx
      f1: 0f 28 c8                     	movaps	%xmm0, %xmm1
      f4: 0f c6 c8 00                  	shufps	$0x0, %xmm0, %xmm1      # xmm1 = xmm1[0,0],xmm0[0,0]
      f8: 4c 89 4c 24 10               	movq	%r9, 0x10(%rsp)
      fd: 49 8d 3c 49                  	leaq	(%r9,%rcx,2), %rdi
     101: 48 83 c7 06                  	addq	$0x6, %rdi
     105: 31 c9                        	xorl	%ecx, %ecx
     107: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     110: 0f b7 74 4f fa               	movzwl	-0x6(%rdi,%rcx,2), %esi
     115: 44 0f b7 5c 4f fc            	movzwl	-0x4(%rdi,%rcx,2), %r11d
     11b: 44 0f b7 54 4f fe            	movzwl	-0x2(%rdi,%rcx,2), %r10d
     121: 44 0f b7 0c 4f               	movzwl	(%rdi,%rcx,2), %r9d
     126: c1 e6 02                     	shll	$0x2, %esi
     129: 41 c1 e3 02                  	shll	$0x2, %r11d
     12d: 41 c1 e2 02                  	shll	$0x2, %r10d
     131: 41 c1 e1 02                  	shll	$0x2, %r9d
     135: f3 42 0f 10 1c 08            	movss	(%rax,%r9), %xmm3
     13b: f3 42 0f 10 24 10            	movss	(%rax,%r10), %xmm4
     141: 0f 14 e3                     	unpcklps	%xmm3, %xmm4            # xmm4 = xmm4[0],xmm3[0],xmm4[1],xmm3[1]
     144: f3 42 0f 10 1c 18            	movss	(%rax,%r11), %xmm3
     14a: f3 0f 10 34 30               	movss	(%rax,%rsi), %xmm6
     14f: 0f 14 f3                     	unpcklps	%xmm3, %xmm6            # xmm6 = xmm6[0],xmm3[0],xmm6[1],xmm3[1]
     152: f3 42 0f 10 5c 0d 00         	movss	(%rbp,%r9), %xmm3
     159: f3 42 0f 10 6c 15 00         	movss	(%rbp,%r10), %xmm5
     160: f3 46 0f 10 44 1d 00         	movss	(%rbp,%r11), %xmm8
     167: 0f 14 eb                     	unpcklps	%xmm3, %xmm5            # xmm5 = xmm5[0],xmm3[0],xmm5[1],xmm3[1]
     16a: f3 0f 10 7c 35 00            	movss	(%rbp,%rsi), %xmm7
     170: f3 43 0f 10 5c 0d 00         	movss	(%r13,%r9), %xmm3
     177: f3 47 0f 10 64 15 00         	movss	(%r13,%r10), %xmm12
     17e: 41 0f 14 f8                  	unpcklps	%xmm8, %xmm7            # xmm7 = xmm7[0],xmm8[0],xmm7[1],xmm8[1]
     182: 44 0f 14 e3                  	unpcklps	%xmm3, %xmm12           # xmm12 = xmm12[0],xmm3[0],xmm12[1],xmm3[1]
     186: f3 43 0f 10 5c 1d 00         	movss	(%r13,%r11), %xmm3
     18d: f3 45 0f 10 44 35 00         	movss	(%r13,%rsi), %xmm8
     194: 44 0f 14 c3                  	unpcklps	%xmm3, %xmm8            # xmm8 = xmm8[0],xmm3[0],xmm8[1],xmm3[1]
     198: f3 43 0f 10 1c 0c            	movss	(%r12,%r9), %xmm3
     19e: f3 47 0f 10 2c 14            	movss	(%r12,%r10), %xmm13
     1a4: f3 47 0f 10 14 1c            	movss	(%r12,%r11), %xmm10
     1aa: 44 0f 14 eb                  	unpcklps	%xmm3, %xmm13           # xmm13 = xmm13[0],xmm3[0],xmm13[1],xmm3[1]
     1ae: f3 45 0f 10 0c 34            	movss	(%r12,%rsi), %xmm9
     1b4: f3 43 0f 10 1c 0f            	movss	(%r15,%r9), %xmm3
     1ba: f3 47 0f 10 34 17            	movss	(%r15,%r10), %xmm14
     1c0: 45 0f 14 ca                  	unpcklps	%xmm10, %xmm9           # xmm9 = xmm9[0],xmm10[0],xmm9[1],xmm10[1]
     1c4: 44 0f 14 f3                  	unpcklps	%xmm3, %xmm14           # xmm14 = xmm14[0],xmm3[0],xmm14[1],xmm3[1]
     1c8: f3 43 0f 10 1c 1f            	movss	(%r15,%r11), %xmm3
     1ce: f3 45 0f 10 14 37            	movss	(%r15,%rsi), %xmm10
     1d4: 44 0f 14 d3                  	unpcklps	%xmm3, %xmm10           # xmm10 = xmm10[0],xmm3[0],xmm10[1],xmm3[1]
     1d8: f3 47 0f 10 1c 0e            	movss	(%r14,%r9), %xmm11
     1de: f3 43 0f 10 1c 16            	movss	(%r14,%r10), %xmm3
     1e4: f3 47 0f 10 3c 1e            	movss	(%r14,%r11), %xmm15
     1ea: 41 0f 14 db                  	unpcklps	%xmm11, %xmm3           # xmm3 = xmm3[0],xmm11[0],xmm3[1],xmm11[1]
     1ee: f3 45 0f 10 1c 36            	movss	(%r14,%rsi), %xmm11
     1f4: 45 0f 14 df                  	unpcklps	%xmm15, %xmm11          # xmm11 = xmm11[0],xmm15[0],xmm11[1],xmm15[1]
     1f8: 4c 8b 84 24 60 01 00 00      	movq	0x160(%rsp), %r8
     200: f3 43 0f 10 14 08            	movss	(%r8,%r9), %xmm2
     206: 0f 16 f4                     	movlhps	%xmm4, %xmm6            # xmm6 = xmm6[0],xmm4[0]
     209: f3 47 0f 10 3c 10            	movss	(%r8,%r10), %xmm15
     20f: 44 0f 14 fa                  	unpcklps	%xmm2, %xmm15           # xmm15 = xmm15[0],xmm2[0],xmm15[1],xmm2[1]
     213: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     219: 0f 16 fd                     	movlhps	%xmm5, %xmm7            # xmm7 = xmm7[0],xmm5[0]
     21c: f3 41 0f 10 24 30            	movss	(%r8,%rsi), %xmm4
     222: 0f 14 e2                     	unpcklps	%xmm2, %xmm4            # xmm4 = xmm4[0],xmm2[0],xmm4[1],xmm2[1]
     225: 4c 8b 84 24 68 01 00 00      	movq	0x168(%rsp), %r8
     22d: f3 43 0f 10 14 08            	movss	(%r8,%r9), %xmm2
     233: 45 0f 16 c4                  	movlhps	%xmm12, %xmm8           # xmm8 = xmm8[0],xmm12[0]
     237: f3 47 0f 10 24 10            	movss	(%r8,%r10), %xmm12
     23d: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     241: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     247: 45 0f 16 cd                  	movlhps	%xmm13, %xmm9           # xmm9 = xmm9[0],xmm13[0]
     24b: f3 41 0f 10 2c 30            	movss	(%r8,%rsi), %xmm5
     251: 0f 14 ea                     	unpcklps	%xmm2, %xmm5            # xmm5 = xmm5[0],xmm2[0],xmm5[1],xmm2[1]
     254: 4c 8b 84 24 70 01 00 00      	movq	0x170(%rsp), %r8
     25c: f3 43 0f 10 14 08            	movss	(%r8,%r9), %xmm2
     262: 45 0f 16 d6                  	movlhps	%xmm14, %xmm10          # xmm10 = xmm10[0],xmm14[0]
     266: f3 47 0f 10 2c 10            	movss	(%r8,%r10), %xmm13
     26c: 44 0f 14 ea                  	unpcklps	%xmm2, %xmm13           # xmm13 = xmm13[0],xmm2[0],xmm13[1],xmm2[1]
     270: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     276: 44 0f 16 db                  	movlhps	%xmm3, %xmm11           # xmm11 = xmm11[0],xmm3[0]
     27a: f3 45 0f 10 34 30            	movss	(%r8,%rsi), %xmm14
     280: 44 0f 14 f2                  	unpcklps	%xmm2, %xmm14           # xmm14 = xmm14[0],xmm2[0],xmm14[1],xmm2[1]
     284: 4c 8b 84 24 78 01 00 00      	movq	0x178(%rsp), %r8
     28c: f3 43 0f 10 14 08            	movss	(%r8,%r9), %xmm2
     292: 41 0f 16 ec                  	movlhps	%xmm12, %xmm5           # xmm5 = xmm5[0],xmm12[0]
     296: f3 47 0f 10 24 10            	movss	(%r8,%r10), %xmm12
     29c: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     2a0: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     2a6: f3 41 0f 10 1c 30            	movss	(%r8,%rsi), %xmm3
     2ac: 0f 14 da                     	unpcklps	%xmm2, %xmm3            # xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1]
     2af: 41 0f 16 dc                  	movlhps	%xmm12, %xmm3           # xmm3 = xmm3[0],xmm12[0]
     2b3: f3 42 0f 10 14 0b            	movss	(%rbx,%r9), %xmm2
     2b9: 41 0f 16 e7                  	movlhps	%xmm15, %xmm4           # xmm4 = xmm4[0],xmm15[0]
     2bd: f3 46 0f 10 24 13            	movss	(%rbx,%r10), %xmm12
     2c3: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     2c7: f3 42 0f 10 14 1b            	movss	(%rbx,%r11), %xmm2
     2cd: 45 0f 16 f5                  	movlhps	%xmm13, %xmm14          # xmm14 = xmm14[0],xmm13[0]
     2d1: f3 44 0f 10 2c 33            	movss	(%rbx,%rsi), %xmm13
     2d7: 44 0f 14 ea                  	unpcklps	%xmm2, %xmm13           # xmm13 = xmm13[0],xmm2[0],xmm13[1],xmm2[1]
     2db: 4c 8b 84 24 88 01 00 00      	movq	0x188(%rsp), %r8
     2e3: f3 43 0f 10 14 08            	movss	(%r8,%r9), %xmm2
     2e9: 45 0f 16 ec                  	movlhps	%xmm12, %xmm13          # xmm13 = xmm13[0],xmm12[0]
     2ed: f3 47 0f 10 3c 10            	movss	(%r8,%r10), %xmm15
     2f3: 44 0f 14 fa                  	unpcklps	%xmm2, %xmm15           # xmm15 = xmm15[0],xmm2[0],xmm15[1],xmm2[1]
     2f7: f3 43 0f 10 14 18            	movss	(%r8,%r11), %xmm2
     2fd: f3 45 0f 10 24 30            	movss	(%r8,%rsi), %xmm12
     303: 44 0f 14 e2                  	unpcklps	%xmm2, %xmm12           # xmm12 = xmm12[0],xmm2[0],xmm12[1],xmm2[1]
     307: 45 0f 16 e7                  	movlhps	%xmm15, %xmm12          # xmm12 = xmm12[0],xmm15[0]
     30b: 0f 59 e1                     	mulps	%xmm1, %xmm4
     30e: 41 0f 58 e1                  	addps	%xmm9, %xmm4
     312: 0f 59 e9                     	mulps	%xmm1, %xmm5
     315: 41 0f 58 ea                  	addps	%xmm10, %xmm5
     319: 44 0f 59 f1                  	mulps	%xmm1, %xmm14
     31d: 45 0f 58 f3                  	addps	%xmm11, %xmm14
     321: 0f 59 d9                     	mulps	%xmm1, %xmm3
     324: 44 0f 28 0d 00 00 00 00      	movaps	(%rip), %xmm9           # 0x32c <tide_rows_Shift_6+0x32c>
     32c: 44 0f 5c cb                  	subps	%xmm3, %xmm9
     330: 41 0f 59 e1                  	mulps	%xmm9, %xmm4
     334: 41 0f 59 e9                  	mulps	%xmm9, %xmm5
     338: 45 0f 59 ce                  	mulps	%xmm14, %xmm9
     33c: 44 0f 28 d1                  	movaps	%xmm1, %xmm10
     340: 44 0f 59 d4                  	mulps	%xmm4, %xmm10
     344: 44 0f 58 d6                  	addps	%xmm6, %xmm10
     348: 0f 28 d1                     	movaps	%xmm1, %xmm2
     34b: 0f 59 d5                     	mulps	%xmm5, %xmm2
     34e: 0f 58 d7                     	addps	%xmm7, %xmm2
     351: 0f 28 f9                     	movaps	%xmm1, %xmm7
     354: 41 0f 59 f9                  	mulps	%xmm9, %xmm7
     358: 41 0f 58 f8                  	addps	%xmm8, %xmm7
     35c: 0f 28 f2                     	movaps	%xmm2, %xmm6
     35f: 41 0f c2 f4 01               	cmpltps	%xmm12, %xmm6
     364: 0f 28 de                     	movaps	%xmm6, %xmm3
     367: 0f 55 dd                     	andnps	%xmm5, %xmm3
     36a: 0f 57 2d 00 00 00 00         	xorps	(%rip), %xmm5           # 0x371 <tide_rows_Shift_6+0x371>
     371: 41 0f 59 ed                  	mulps	%xmm13, %xmm5
     375: 45 0f 58 e4                  	addps	%xmm12, %xmm12
     379: 44 0f 5c e2                  	subps	%xmm2, %xmm12
     37d: 0f 54 ee                     	andps	%xmm6, %xmm5
     380: 44 0f 54 e6                  	andps	%xmm6, %xmm12
     384: 0f 55 f2                     	andnps	%xmm2, %xmm6
     387: f3 44 0f 11 14 30            	movss	%xmm10, (%rax,%rsi)
     38d: 0f 56 eb                     	orps	%xmm3, %xmm5
     390: 41 0f 28 d2                  	movaps	%xmm10, %xmm2
     394: 41 0f c6 d2 55               	shufps	$0x55, %xmm10, %xmm2    # xmm2 = xmm2[1,1],xmm10[1,1]
     399: f3 42 0f 11 14 18            	movss	%xmm2, (%rax,%r11)
     39f: 41 0f 56 f4                  	orps	%xmm12, %xmm6
     3a3: 41 0f 28 d2                  	movaps	%xmm10, %xmm2
     3a7: 66 41 0f 15 d2               	unpckhpd	%xmm10, %xmm2           # xmm2 = xmm2[1],xmm10[1]
     3ac: f3 42 0f 11 14 10            	movss	%xmm2, (%rax,%r10)
     3b2: 45 0f c6 d2 ff               	shufps	$0xff, %xmm10, %xmm10   # xmm10 = xmm10[3,3,3,3]
     3b7: f3 46 0f 11 14 08            	movss	%xmm10, (%rax,%r9)
     3bd: f3 0f 11 74 35 00            	movss	%xmm6, (%rbp,%rsi)
     3c3: 66 0f 70 d6 55               	pshufd	$0x55, %xmm6, %xmm2     # xmm2 = xmm6[1,1,1,1]
     3c8: 66 42 0f 7e 54 1d 00         	movd	%xmm2, (%rbp,%r11)
     3cf: 66 0f 70 d6 ee               	pshufd	$0xee, %xmm6, %xmm2     # xmm2 = xmm6[2,3,2,3]
     3d4: 66 42 0f 7e 54 15 00         	movd	%xmm2, (%rbp,%r10)
     3db: f3 41 0f 11 7c 35 00         	movss	%xmm7, (%r13,%rsi)
     3e2: 0f 28 d7                     	movaps	%xmm7, %xmm2
     3e5: 0f c6 d7 55                  	shufps	$0x55, %xmm7, %xmm2     # xmm2 = xmm2[1,1],xmm7[1,1]
     3e9: f3 43 0f 11 54 1d 00         	movss	%xmm2, (%r13,%r11)
     3f0: 0f 28 d7                     	movaps	%xmm7, %xmm2
     3f3: 66 0f 15 d7                  	unpckhpd	%xmm7, %xmm2            # xmm2 = xmm2[1],xmm7[1]
     3f7: f3 43 0f 11 54 15 00         	movss	%xmm2, (%r13,%r10)
     3fe: 0f c6 ff ff                  	shufps	$0xff, %xmm7, %xmm7     # xmm7 = xmm7[3,3,3,3]
     402: f3 43 0f 11 7c 0d 00         	movss	%xmm7, (%r13,%r9)
     409: f3 41 0f 11 24 34            	movss	%xmm4, (%r12,%rsi)
     40f: f3 41 0f 11 2c 37            	movss	%xmm5, (%r15,%rsi)
     415: f3 45 0f 11 0c 36            	movss	%xmm9, (%r14,%rsi)
     41b: 66 0f 70 d6 ff               	pshufd	$0xff, %xmm6, %xmm2     # xmm2 = xmm6[3,3,3,3]
     420: 66 42 0f 7e 54 0d 00         	movd	%xmm2, (%rbp,%r9)
     427: 0f 28 d4                     	movaps	%xmm4, %xmm2
     42a: 0f c6 d4 55                  	shufps	$0x55, %xmm4, %xmm2     # xmm2 = xmm2[1,1],xmm4[1,1]
     42e: f3 43 0f 11 14 1c            	movss	%xmm2, (%r12,%r11)
     434: 0f 28 d4                     	movaps	%xmm4, %xmm2
     437: 66 0f 15 d4                  	unpckhpd	%xmm4, %xmm2            # xmm2 = xmm2[1],xmm4[1]
     43b: f3 43 0f 11 14 14            	movss	%xmm2, (%r12,%r10)
     441: 66 0f 70 d5 55               	pshufd	$0x55, %xmm5, %xmm2     # xmm2 = xmm5[1,1,1,1]
     446: 66 43 0f 7e 14 1f            	movd	%xmm2, (%r15,%r11)
     44c: 41 0f 28 d1                  	movaps	%xmm9, %xmm2
     450: 41 0f c6 d1 55               	shufps	$0x55, %xmm9, %xmm2     # xmm2 = xmm2[1,1],xmm9[1,1]
     455: f3 43 0f 11 14 1e            	movss	%xmm2, (%r14,%r11)
     45b: 0f c6 e4 ff                  	shufps	$0xff, %xmm4, %xmm4     # xmm4 = xmm4[3,3,3,3]
     45f: f3 43 0f 11 24 0c            	movss	%xmm4, (%r12,%r9)
     465: 66 0f 70 d5 ee               	pshufd	$0xee, %xmm5, %xmm2     # xmm2 = xmm5[2,3,2,3]
     46a: 66 43 0f 7e 14 17            	movd	%xmm2, (%r15,%r10)
     470: 41 0f 28 d1                  	movaps	%xmm9, %xmm2
     474: 66 41 0f 15 d1               	unpckhpd	%xmm9, %xmm2            # xmm2 = xmm2[1],xmm9[1]
     479: f3 43 0f 11 14 16            	movss	%xmm2, (%r14,%r10)
     47f: 66 0f 70 d5 ff               	pshufd	$0xff, %xmm5, %xmm2     # xmm2 = xmm5[3,3,3,3]
     484: 66 43 0f 7e 14 0f            	movd	%xmm2, (%r15,%r9)
     48a: 45 0f c6 c9 ff               	shufps	$0xff, %xmm9, %xmm9     # xmm9 = xmm9[3,3,3,3]
     48f: f3 47 0f 11 0c 0e            	movss	%xmm9, (%r14,%r9)
     495: 48 83 c1 04                  	addq	$0x4, %rcx
     499: 48 39 0c 24                  	cmpq	%rcx, (%rsp)
     49d: 0f 85 6d fc ff ff            	jne	0x110 <tide_rows_Shift_6+0x110>
     4a3: 48 8b 0c 24                  	movq	(%rsp), %rcx
     4a7: 48 39 4c 24 08               	cmpq	%rcx, 0x8(%rsp)
     4ac: 4c 8b 4c 24 10               	movq	0x10(%rsp), %r9
     4b1: 48 8b bc 24 88 01 00 00      	movq	0x188(%rsp), %rdi
     4b9: 4c 8b 94 24 78 01 00 00      	movq	0x178(%rsp), %r10
     4c1: 4c 8b 9c 24 70 01 00 00      	movq	0x170(%rsp), %r11
     4c9: 48 8b b4 24 68 01 00 00      	movq	0x168(%rsp), %rsi
     4d1: 4c 8b 84 24 60 01 00 00      	movq	0x160(%rsp), %r8
     4d9: 75 5a                        	jne	0x535 <tide_rows_Shift_6+0x535>
     4db: 0f 28 74 24 20               	movaps	0x20(%rsp), %xmm6
     4e0: 0f 28 7c 24 30               	movaps	0x30(%rsp), %xmm7
     4e5: 44 0f 28 44 24 40            	movaps	0x40(%rsp), %xmm8
     4eb: 44 0f 28 4c 24 50            	movaps	0x50(%rsp), %xmm9
     4f1: 44 0f 28 54 24 60            	movaps	0x60(%rsp), %xmm10
     4f7: 44 0f 28 5c 24 70            	movaps	0x70(%rsp), %xmm11
     4fd: 44 0f 28 a4 24 80 00 00 00   	movaps	0x80(%rsp), %xmm12
     506: 44 0f 28 ac 24 90 00 00 00   	movaps	0x90(%rsp), %xmm13
     50f: 44 0f 28 b4 24 a0 00 00 00   	movaps	0xa0(%rsp), %xmm14
     518: 44 0f 28 bc 24 b0 00 00 00   	movaps	0xb0(%rsp), %xmm15
     521: 48 81 c4 c8 00 00 00         	addq	$0xc8, %rsp
     528: 5b                           	popq	%rbx
     529: 5d                           	popq	%rbp
     52a: 5f                           	popq	%rdi
     52b: 5e                           	popq	%rsi
     52c: 41 5c                        	popq	%r12
     52e: 41 5d                        	popq	%r13
     530: 41 5e                        	popq	%r14
     532: 41 5f                        	popq	%r15
     534: c3                           	retq
     535: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x53d <tide_rows_Shift_6+0x53d>
     53d: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x544 <tide_rows_Shift_6+0x544>
     544: e9 a7 00 00 00               	jmp	0x5f0 <tide_rows_Shift_6+0x5f0>
     549: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     550: 0f 28 f4                     	movaps	%xmm4, %xmm6
     553: f3 41 0f 10 3c 08            	movss	(%r8,%rcx), %xmm7
     559: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     55d: f3 45 0f 10 04 0b            	movss	(%r11,%rcx), %xmm8
     563: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     568: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
     56e: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
     574: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     579: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     57e: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     582: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     587: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     58b: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
     591: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     596: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
     59d: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     5a1: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     5a5: f3 44 0f 59 1c 0b            	mulss	(%rbx,%rcx), %xmm11
     5ab: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     5b0: 0f 28 ec                     	movaps	%xmm4, %xmm5
     5b3: 0f 55 eb                     	andnps	%xmm3, %xmm5
     5b6: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     5ba: 0f 56 e5                     	orps	%xmm5, %xmm4
     5bd: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     5c3: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     5c9: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     5d0: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     5d6: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     5dc: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     5e2: 48 ff c2                     	incq	%rdx
     5e5: 48 39 54 24 18               	cmpq	%rdx, 0x18(%rsp)
     5ea: 0f 84 eb fe ff ff            	je	0x4db <tide_rows_Shift_6+0x4db>
     5f0: 41 0f b7 0c 51               	movzwl	(%r9,%rdx,2), %ecx
     5f5: c1 e1 02                     	shll	$0x2, %ecx
     5f8: f3 0f 10 1c 0e               	movss	(%rsi,%rcx), %xmm3
     5fd: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     601: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     607: f3 0f 10 2c 0f               	movss	(%rdi,%rcx), %xmm5
     60c: f3 41 0f 10 24 0a            	movss	(%r10,%rcx), %xmm4
     612: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     616: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     61a: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     61f: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     624: 0f 28 e0                     	movaps	%xmm0, %xmm4
     627: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     62b: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     631: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     634: 0f 86 16 ff ff ff            	jbe	0x550 <tide_rows_Shift_6+0x550>
     63a: 0f 28 f5                     	movaps	%xmm5, %xmm6
     63d: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     641: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     645: e9 09 ff ff ff               	jmp	0x553 <tide_rows_Shift_6+0x553>
