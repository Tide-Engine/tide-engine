
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-20261029/simd_blocks4/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec 08 01 00 00         	subq	$0x108, %rsp            # imm = 0x108
      13: 44 0f 29 bc 24 f0 00 00 00   	movaps	%xmm15, 0xf0(%rsp)
      1c: 44 0f 29 b4 24 e0 00 00 00   	movaps	%xmm14, 0xe0(%rsp)
      25: 44 0f 29 ac 24 d0 00 00 00   	movaps	%xmm13, 0xd0(%rsp)
      2e: 44 0f 29 a4 24 c0 00 00 00   	movaps	%xmm12, 0xc0(%rsp)
      37: 44 0f 29 9c 24 b0 00 00 00   	movaps	%xmm11, 0xb0(%rsp)
      40: 44 0f 29 94 24 a0 00 00 00   	movaps	%xmm10, 0xa0(%rsp)
      49: 44 0f 29 8c 24 90 00 00 00   	movaps	%xmm9, 0x90(%rsp)
      52: 44 0f 29 84 24 80 00 00 00   	movaps	%xmm8, 0x80(%rsp)
      5b: 0f 29 7c 24 70               	movaps	%xmm7, 0x70(%rsp)
      60: 66 0f 7f 74 24 60            	movdqa	%xmm6, 0x60(%rsp)
      66: 48 8b 9c 24 b8 01 00 00      	movq	0x1b8(%rsp), %rbx
      6e: 4c 8b a4 24 b0 01 00 00      	movq	0x1b0(%rsp), %r12
      76: 4c 8b ac 24 a8 01 00 00      	movq	0x1a8(%rsp), %r13
      7e: 48 8b ac 24 a0 01 00 00      	movq	0x1a0(%rsp), %rbp
      86: 48 8b bc 24 90 01 00 00      	movq	0x190(%rsp), %rdi
      8e: 48 8b b4 24 88 01 00 00      	movq	0x188(%rsp), %rsi
      96: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
      9e: 4c 8b 9c 24 78 01 00 00      	movq	0x178(%rsp), %r11
      a6: f3 0f 10 01                  	movss	(%rcx), %xmm0
      aa: 4c 8b b4 24 70 01 00 00      	movq	0x170(%rsp), %r14
      b2: 44 89 c0                     	movl	%r8d, %eax
      b5: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
      ba: 89 d0                        	movl	%edx, %eax
      bc: 44 39 c2                     	cmpl	%r8d, %edx
      bf: 4c 89 4c 24 18               	movq	%r9, 0x18(%rsp)
      c4: 0f 83 ce 03 00 00            	jae	0x498 <tide_rows_Shift_6+0x498>
      ca: 41 8b 09                     	movl	(%r9), %ecx
      cd: 89 4c 24 2c                  	movl	%ecx, 0x2c(%rsp)
      d1: 89 c1                        	movl	%eax, %ecx
      d3: 48 89 4c 24 38               	movq	%rcx, 0x38(%rsp)
      d8: 41 8d 48 ff                  	leal	-0x1(%r8), %ecx
      dc: 48 89 4c 24 30               	movq	%rcx, 0x30(%rsp)
      e1: 0f 28 e0                     	movaps	%xmm0, %xmm4
      e4: 0f c6 e0 00                  	shufps	$0x0, %xmm0, %xmm4      # xmm4 = xmm4[0,0],xmm0[0,0]
      e8: f3 0f 10 15 00 00 00 00      	movss	(%rip), %xmm2           # 0xf0 <tide_rows_Shift_6+0xf0>
      f0: 48 8b 8c 24 98 01 00 00      	movq	0x198(%rsp), %rcx
      f8: 4c 89 44 24 08               	movq	%r8, 0x8(%rsp)
      fd: eb 5c                        	jmp	0x15b <tide_rows_Shift_6+0x15b>
      ff: 90                           	nop
     100: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
     108: 4c 8b 44 24 08               	movq	0x8(%rsp), %r8
     10d: 44 89 c8                     	movl	%r9d, %eax
     110: ff c0                        	incl	%eax
     112: 41 c1 e3 02                  	shll	$0x2, %r11d
     116: f3 42 0f 11 0c 1a            	movss	%xmm1, (%rdx,%r11)
     11c: f3 43 0f 10 0c 1c            	movss	(%r12,%r11), %xmm1
     122: f3 0f 59 c8                  	mulss	%xmm0, %xmm1
     126: f3 42 0f 58 0c 19            	addss	(%rcx,%r11), %xmm1
     12c: f3 42 0f 11 3c 1f            	movss	%xmm7, (%rdi,%r11)
     132: f3 0f 59 f1                  	mulss	%xmm1, %xmm6
     136: 0f 28 c8                     	movaps	%xmm0, %xmm1
     139: f3 0f 59 ce                  	mulss	%xmm6, %xmm1
     13d: f3 43 0f 58 0c 1a            	addss	(%r10,%r11), %xmm1
     143: f3 43 0f 11 0c 1a            	movss	%xmm1, (%r10,%r11)
     149: f3 42 0f 11 34 19            	movss	%xmm6, (%rcx,%r11)
     14f: 49 89 d3                     	movq	%rdx, %r11
     152: 44 39 c0                     	cmpl	%r8d, %eax
     155: 0f 83 63 03 00 00            	jae	0x4be <tide_rows_Shift_6+0x4be>
     15b: 4d 89 f7                     	movq	%r14, %r15
     15e: 4c 89 da                     	movq	%r11, %rdx
     161: 49 89 f2                     	movq	%rsi, %r10
     164: 49 89 f8                     	movq	%rdi, %r8
     167: 41 89 c1                     	movl	%eax, %r9d
     16a: 41 89 c3                     	movl	%eax, %r11d
     16d: 4c 8b 74 24 18               	movq	0x18(%rsp), %r14
     172: 43 0f b7 44 5e 0a            	movzwl	0xa(%r14,%r11,2), %eax
     178: 89 c6                        	movl	%eax, %esi
     17a: 83 e6 fc                     	andl	$-0x4, %esi
     17d: 8b 7c 24 2c                  	movl	0x2c(%rsp), %edi
     181: 29 f7                        	subl	%esi, %edi
     183: 83 ff 04                     	cmpl	$0x4, %edi
     186: 0f 82 16 03 00 00            	jb	0x4a2 <tide_rows_Shift_6+0x4a2>
     18c: 48 8b 7c 24 38               	movq	0x38(%rsp), %rdi
     191: 66 41 3b 74 7e 0a            	cmpw	0xa(%r14,%rdi,2), %si
     197: 72 3b                        	jb	0x1d4 <tide_rows_Shift_6+0x1d4>
     199: 8d 7e 03                     	leal	0x3(%rsi), %edi
     19c: 48 89 e9                     	movq	%rbp, %rcx
     19f: 4c 89 ed                     	movq	%r13, %rbp
     1a2: 4d 89 e5                     	movq	%r12, %r13
     1a5: 49 89 dc                     	movq	%rbx, %r12
     1a8: 48 8b 5c 24 18               	movq	0x18(%rsp), %rbx
     1ad: 4c 8b 74 24 30               	movq	0x30(%rsp), %r14
     1b2: 42 0f b7 5c 73 0a            	movzwl	0xa(%rbx,%r14,2), %ebx
     1b8: 39 df                        	cmpl	%ebx, %edi
     1ba: 4c 89 e3                     	movq	%r12, %rbx
     1bd: 4d 89 ec                     	movq	%r13, %r12
     1c0: 49 89 ed                     	movq	%rbp, %r13
     1c3: 48 89 cd                     	movq	%rcx, %rbp
     1c6: 48 8b 8c 24 98 01 00 00      	movq	0x198(%rsp), %rcx
     1ce: 0f 86 bc 00 00 00            	jbe	0x290 <tide_rows_Shift_6+0x290>
     1d4: 41 89 c3                     	movl	%eax, %r11d
     1d7: c1 e0 02                     	shll	$0x2, %eax
     1da: f3 44 0f 10 44 05 00         	movss	(%rbp,%rax), %xmm8
     1e1: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     1e6: 4c 89 d6                     	movq	%r10, %rsi
     1e9: f3 45 0f 58 04 02            	addss	(%r10,%rax), %xmm8
     1ef: f3 0f 10 0c 03               	movss	(%rbx,%rax), %xmm1
     1f4: f3 0f 59 c8                  	mulss	%xmm0, %xmm1
     1f8: 0f 28 f2                     	movaps	%xmm2, %xmm6
     1fb: f3 0f 5c f1                  	subss	%xmm1, %xmm6
     1ff: f3 44 0f 59 c6               	mulss	%xmm6, %xmm8
     204: 0f 28 c8                     	movaps	%xmm0, %xmm1
     207: f3 41 0f 59 c8               	mulss	%xmm8, %xmm1
     20c: 4d 89 fe                     	movq	%r15, %r14
     20f: f3 41 0f 58 0c 07            	addss	(%r15,%rax), %xmm1
     215: f3 41 0f 11 0c 07            	movss	%xmm1, (%r15,%rax)
     21b: f3 41 0f 10 7c 05 00         	movss	(%r13,%rax), %xmm7
     222: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     226: 4c 89 c7                     	movq	%r8, %rdi
     229: f3 41 0f 58 3c 00            	addss	(%r8,%rax), %xmm7
     22f: f3 0f 59 fe                  	mulss	%xmm6, %xmm7
     233: 0f 28 c8                     	movaps	%xmm0, %xmm1
     236: f3 0f 59 cf                  	mulss	%xmm7, %xmm1
     23a: f3 0f 58 0c 02               	addss	(%rdx,%rax), %xmm1
     23f: f3 45 0f 11 04 02            	movss	%xmm8, (%r10,%rax)
     245: 4c 8b 84 24 c8 01 00 00      	movq	0x1c8(%rsp), %r8
     24d: f3 45 0f 10 04 00            	movss	(%r8,%rax), %xmm8
     253: 44 0f 2e c1                  	ucomiss	%xmm1, %xmm8
     257: 0f 86 a3 fe ff ff            	jbe	0x100 <tide_rows_Shift_6+0x100>
     25d: f3 45 0f 58 c0               	addss	%xmm8, %xmm8
     262: f3 44 0f 5c c1               	subss	%xmm1, %xmm8
     267: 0f 57 3d 00 00 00 00         	xorps	(%rip), %xmm7           # 0x26e <tide_rows_Shift_6+0x26e>
     26e: 48 8b 84 24 c0 01 00 00      	movq	0x1c0(%rsp), %rax
     276: f3 42 0f 59 3c 98            	mulss	(%rax,%r11,4), %xmm7
     27c: 41 0f 28 c8                  	movaps	%xmm8, %xmm1
     280: e9 7b fe ff ff               	jmp	0x100 <tide_rows_Shift_6+0x100>
     285: 66 66 2e 0f 1f 84 00 00 00 00 00     	nopw	%cs:(%rax,%rax)
     290: 41 ff c1                     	incl	%r9d
     293: 48 8b 7c 24 08               	movq	0x8(%rsp), %rdi
     298: 44 39 cf                     	cmpl	%r9d, %edi
     29b: 44 0f 47 cf                  	cmoval	%edi, %r9d
     29f: 44 89 4c 24 14               	movl	%r9d, 0x14(%rsp)
     2a4: 29 f0                        	subl	%esi, %eax
     2a6: 0f 57 c9                     	xorps	%xmm1, %xmm1
     2a9: 0f 29 4c 24 50               	movaps	%xmm1, 0x50(%rsp)
     2ae: 83 e0 03                     	andl	$0x3, %eax
     2b1: c7 44 84 50 ff ff ff ff      	movl	$0xffffffff, 0x50(%rsp,%rax,4) # imm = 0xFFFFFFFF
     2b9: 66 0f 6f 74 24 50            	movdqa	0x50(%rsp), %xmm6
     2bf: 49 8d 43 01                  	leaq	0x1(%r11), %rax
     2c3: 48 3b 44 24 20               	cmpq	0x20(%rsp), %rax
     2c8: 4d 89 fe                     	movq	%r15, %r14
     2cb: 73 4d                        	jae	0x31a <tide_rows_Shift_6+0x31a>
     2cd: 0f 1f 00                     	nopl	(%rax)
     2d0: 48 8b 44 24 18               	movq	0x18(%rsp), %rax
     2d5: 42 0f b7 44 58 0c            	movzwl	0xc(%rax,%r11,2), %eax
     2db: 89 c7                        	movl	%eax, %edi
     2dd: 83 e7 fc                     	andl	$-0x4, %edi
     2e0: 66 39 f7                     	cmpw	%si, %di
     2e3: 75 2d                        	jne	0x312 <tide_rows_Shift_6+0x312>
     2e5: 29 f0                        	subl	%esi, %eax
     2e7: 66 0f 7f 74 24 40            	movdqa	%xmm6, 0x40(%rsp)
     2ed: 83 e0 03                     	andl	$0x3, %eax
     2f0: c7 44 84 40 ff ff ff ff      	movl	$0xffffffff, 0x40(%rsp,%rax,4) # imm = 0xFFFFFFFF
     2f8: 66 0f 6f 74 24 40            	movdqa	0x40(%rsp), %xmm6
     2fe: 49 8d 43 01                  	leaq	0x1(%r11), %rax
     302: 49 83 c3 02                  	addq	$0x2, %r11
     306: 4c 3b 5c 24 20               	cmpq	0x20(%rsp), %r11
     30b: 49 89 c3                     	movq	%rax, %r11
     30e: 72 c0                        	jb	0x2d0 <tide_rows_Shift_6+0x2d0>
     310: eb 08                        	jmp	0x31a <tide_rows_Shift_6+0x31a>
     312: 41 ff c3                     	incl	%r11d
     315: 44 89 5c 24 14               	movl	%r11d, 0x14(%rsp)
     31a: c1 e6 02                     	shll	$0x2, %esi
     31d: 49 89 d3                     	movq	%rdx, %r11
     320: 44 0f 10 2c 32               	movups	(%rdx,%rsi), %xmm13
     325: 4c 89 d0                     	movq	%r10, %rax
     328: 45 0f 10 24 32               	movups	(%r10,%rsi), %xmm12
     32d: 4c 89 c7                     	movq	%r8, %rdi
     330: 45 0f 10 0c 30               	movups	(%r8,%rsi), %xmm9
     335: 0f 10 3c 31                  	movups	(%rcx,%rsi), %xmm7
     339: 44 0f 10 5c 35 00            	movups	(%rbp,%rsi), %xmm11
     33f: 45 0f 10 54 35 00            	movups	(%r13,%rsi), %xmm10
     345: 45 0f 10 04 34               	movups	(%r12,%rsi), %xmm8
     34a: 0f 10 0c 33                  	movups	(%rbx,%rsi), %xmm1
     34e: 48 8b 94 24 c0 01 00 00      	movq	0x1c0(%rsp), %rdx
     356: 44 0f 10 3c 32               	movups	(%rdx,%rsi), %xmm15
     35b: 48 8b 94 24 c8 01 00 00      	movq	0x1c8(%rsp), %rdx
     363: 44 0f 10 34 32               	movups	(%rdx,%rsi), %xmm14
     368: 0f 59 cc                     	mulps	%xmm4, %xmm1
     36b: 0f 28 2d 00 00 00 00         	movaps	(%rip), %xmm5           # 0x372 <tide_rows_Shift_6+0x372>
     372: 0f 5c e9                     	subps	%xmm1, %xmm5
     375: 44 0f 59 dc                  	mulps	%xmm4, %xmm11
     379: 45 0f 58 dc                  	addps	%xmm12, %xmm11
     37d: 44 0f 59 dd                  	mulps	%xmm5, %xmm11
     381: 44 0f 59 d4                  	mulps	%xmm4, %xmm10
     385: 45 0f 58 d1                  	addps	%xmm9, %xmm10
     389: 44 0f 59 d5                  	mulps	%xmm5, %xmm10
     38d: 0f 28 cc                     	movaps	%xmm4, %xmm1
     390: 41 0f 59 ca                  	mulps	%xmm10, %xmm1
     394: 41 0f 58 cd                  	addps	%xmm13, %xmm1
     398: 44 0f 59 c4                  	mulps	%xmm4, %xmm8
     39c: 44 0f 58 c7                  	addps	%xmm7, %xmm8
     3a0: 44 0f 59 c5                  	mulps	%xmm5, %xmm8
     3a4: 0f 28 e9                     	movaps	%xmm1, %xmm5
     3a7: 41 0f c2 ee 01               	cmpltps	%xmm14, %xmm5
     3ac: 0f 28 dd                     	movaps	%xmm5, %xmm3
     3af: 41 0f 55 da                  	andnps	%xmm10, %xmm3
     3b3: 44 0f 57 15 00 00 00 00      	xorps	(%rip), %xmm10          # 0x3bb <tide_rows_Shift_6+0x3bb>
     3bb: 45 0f 59 d7                  	mulps	%xmm15, %xmm10
     3bf: 45 0f 58 f6                  	addps	%xmm14, %xmm14
     3c3: 44 0f 5c f1                  	subps	%xmm1, %xmm14
     3c7: 44 0f 54 f5                  	andps	%xmm5, %xmm14
     3cb: 44 0f 54 d5                  	andps	%xmm5, %xmm10
     3cf: 0f 55 e9                     	andnps	%xmm1, %xmm5
     3d2: 41 0f 10 0c 36               	movups	(%r14,%rsi), %xmm1
     3d7: 44 0f 56 f5                  	orps	%xmm5, %xmm14
     3db: 0f 28 ec                     	movaps	%xmm4, %xmm5
     3de: 41 0f 59 eb                  	mulps	%xmm11, %xmm5
     3e2: 0f 58 e9                     	addps	%xmm1, %xmm5
     3e5: 44 0f 56 d3                  	orps	%xmm3, %xmm10
     3e9: 45 0f 57 ff                  	xorps	%xmm15, %xmm15
     3ed: 66 44 0f 66 fe               	pcmpgtd	%xmm6, %xmm15
     3f2: 66 41 0f 6f df               	movdqa	%xmm15, %xmm3
     3f7: 66 0f df d9                  	pandn	%xmm1, %xmm3
     3fb: 41 0f 54 ef                  	andps	%xmm15, %xmm5
     3ff: 0f 56 eb                     	orps	%xmm3, %xmm5
     402: 41 0f 11 2c 36               	movups	%xmm5, (%r14,%rsi)
     407: 41 0f 28 cf                  	movaps	%xmm15, %xmm1
     40b: 41 0f 55 cd                  	andnps	%xmm13, %xmm1
     40f: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
     417: 41 0f 10 1c 32               	movups	(%r10,%rsi), %xmm3
     41c: 45 0f 54 f7                  	andps	%xmm15, %xmm14
     420: 41 0f 56 ce                  	orps	%xmm14, %xmm1
     424: 0f 28 ec                     	movaps	%xmm4, %xmm5
     427: 41 0f 59 e8                  	mulps	%xmm8, %xmm5
     42b: 0f 58 eb                     	addps	%xmm3, %xmm5
     42e: 41 0f 11 0c 33               	movups	%xmm1, (%r11,%rsi)
     433: 41 0f 28 cf                  	movaps	%xmm15, %xmm1
     437: 0f 55 cb                     	andnps	%xmm3, %xmm1
     43a: 41 0f 54 ef                  	andps	%xmm15, %xmm5
     43e: 0f 56 e9                     	orps	%xmm1, %xmm5
     441: 41 0f 11 2c 32               	movups	%xmm5, (%r10,%rsi)
     446: 41 0f 28 cf                  	movaps	%xmm15, %xmm1
     44a: 41 0f 55 cc                  	andnps	%xmm12, %xmm1
     44e: 45 0f 54 df                  	andps	%xmm15, %xmm11
     452: 44 0f 56 d9                  	orps	%xmm1, %xmm11
     456: 44 0f 11 1c 30               	movups	%xmm11, (%rax,%rsi)
     45b: 41 0f 28 cf                  	movaps	%xmm15, %xmm1
     45f: 41 0f 55 c9                  	andnps	%xmm9, %xmm1
     463: 45 0f 54 d7                  	andps	%xmm15, %xmm10
     467: 41 0f 56 ca                  	orps	%xmm10, %xmm1
     46b: 41 0f 11 0c 30               	movups	%xmm1, (%r8,%rsi)
     470: 45 0f 54 c7                  	andps	%xmm15, %xmm8
     474: 44 0f 55 ff                  	andnps	%xmm7, %xmm15
     478: 45 0f 56 f8                  	orps	%xmm8, %xmm15
     47c: 44 0f 11 3c 31               	movups	%xmm15, (%rcx,%rsi)
     481: 48 89 c6                     	movq	%rax, %rsi
     484: 4c 8b 44 24 08               	movq	0x8(%rsp), %r8
     489: 8b 44 24 14                  	movl	0x14(%rsp), %eax
     48d: 44 39 c0                     	cmpl	%r8d, %eax
     490: 0f 82 c5 fc ff ff            	jb	0x15b <tide_rows_Shift_6+0x15b>
     496: eb 26                        	jmp	0x4be <tide_rows_Shift_6+0x4be>
     498: 48 8b 8c 24 98 01 00 00      	movq	0x198(%rsp), %rcx
     4a0: eb 1c                        	jmp	0x4be <tide_rows_Shift_6+0x4be>
     4a2: 4c 89 c7                     	movq	%r8, %rdi
     4a5: 4c 89 d6                     	movq	%r10, %rsi
     4a8: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
     4b0: 49 89 d3                     	movq	%rdx, %r11
     4b3: 4d 89 fe                     	movq	%r15, %r14
     4b6: 4c 8b 44 24 08               	movq	0x8(%rsp), %r8
     4bb: 44 89 c8                     	movl	%r9d, %eax
     4be: 44 39 c0                     	cmpl	%r8d, %eax
     4c1: 4c 8b 4c 24 18               	movq	0x18(%rsp), %r9
     4c6: 0f 83 05 01 00 00            	jae	0x5d1 <tide_rows_Shift_6+0x5d1>
     4cc: 89 c0                        	movl	%eax, %eax
     4ce: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x4d6 <tide_rows_Shift_6+0x4d6>
     4d6: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x4dd <tide_rows_Shift_6+0x4dd>
     4dd: eb 4c                        	jmp	0x52b <tide_rows_Shift_6+0x52b>
     4df: 90                           	nop
     4e0: 8d 14 95 00 00 00 00         	leal	(,%rdx,4), %edx
     4e7: f3 41 0f 11 2c 13            	movss	%xmm5, (%r11,%rdx)
     4ed: f3 0f 11 24 17               	movss	%xmm4, (%rdi,%rdx)
     4f2: f3 41 0f 10 24 14            	movss	(%r12,%rdx), %xmm4
     4f8: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     4fc: f3 0f 58 24 11               	addss	(%rcx,%rdx), %xmm4
     501: f3 0f 59 dc                  	mulss	%xmm4, %xmm3
     505: 0f 28 e0                     	movaps	%xmm0, %xmm4
     508: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     50c: f3 41 0f 58 24 12            	addss	(%r10,%rdx), %xmm4
     512: f3 41 0f 11 24 12            	movss	%xmm4, (%r10,%rdx)
     518: f3 0f 11 1c 11               	movss	%xmm3, (%rcx,%rdx)
     51d: 48 ff c0                     	incq	%rax
     520: 48 39 44 24 20               	cmpq	%rax, 0x20(%rsp)
     525: 0f 84 a6 00 00 00            	je	0x5d1 <tide_rows_Shift_6+0x5d1>
     52b: 41 0f b7 54 41 0a            	movzwl	0xa(%r9,%rax,2), %edx
     531: 41 89 d0                     	movl	%edx, %r8d
     534: 41 c1 e0 02                  	shll	$0x2, %r8d
     538: f3 42 0f 10 74 05 00         	movss	(%rbp,%r8), %xmm6
     53f: f3 0f 59 f0                  	mulss	%xmm0, %xmm6
     543: f3 42 0f 58 34 06            	addss	(%rsi,%r8), %xmm6
     549: f3 42 0f 10 24 03            	movss	(%rbx,%r8), %xmm4
     54f: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     553: 0f 28 d9                     	movaps	%xmm1, %xmm3
     556: f3 0f 5c dc                  	subss	%xmm4, %xmm3
     55a: f3 0f 59 f3                  	mulss	%xmm3, %xmm6
     55e: 0f 28 e0                     	movaps	%xmm0, %xmm4
     561: f3 0f 59 e6                  	mulss	%xmm6, %xmm4
     565: f3 43 0f 58 24 06            	addss	(%r14,%r8), %xmm4
     56b: f3 43 0f 11 24 06            	movss	%xmm4, (%r14,%r8)
     571: f3 43 0f 10 64 05 00         	movss	(%r13,%r8), %xmm4
     578: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     57c: f3 42 0f 58 24 07            	addss	(%rdi,%r8), %xmm4
     582: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     586: 0f 28 e8                     	movaps	%xmm0, %xmm5
     589: f3 0f 59 ec                  	mulss	%xmm4, %xmm5
     58d: f3 43 0f 58 2c 03            	addss	(%r11,%r8), %xmm5
     593: f3 42 0f 11 34 06            	movss	%xmm6, (%rsi,%r8)
     599: 4c 8b bc 24 c8 01 00 00      	movq	0x1c8(%rsp), %r15
     5a1: f3 43 0f 10 34 07            	movss	(%r15,%r8), %xmm6
     5a7: 0f 2e f5                     	ucomiss	%xmm5, %xmm6
     5aa: 0f 86 30 ff ff ff            	jbe	0x4e0 <tide_rows_Shift_6+0x4e0>
     5b0: f3 0f 58 f6                  	addss	%xmm6, %xmm6
     5b4: f3 0f 5c f5                  	subss	%xmm5, %xmm6
     5b8: 0f 57 e2                     	xorps	%xmm2, %xmm4
     5bb: 4c 8b 84 24 c0 01 00 00      	movq	0x1c0(%rsp), %r8
     5c3: f3 41 0f 59 24 90            	mulss	(%r8,%rdx,4), %xmm4
     5c9: 0f 28 ee                     	movaps	%xmm6, %xmm5
     5cc: e9 0f ff ff ff               	jmp	0x4e0 <tide_rows_Shift_6+0x4e0>
     5d1: 0f 28 74 24 60               	movaps	0x60(%rsp), %xmm6
     5d6: 0f 28 7c 24 70               	movaps	0x70(%rsp), %xmm7
     5db: 44 0f 28 84 24 80 00 00 00   	movaps	0x80(%rsp), %xmm8
     5e4: 44 0f 28 8c 24 90 00 00 00   	movaps	0x90(%rsp), %xmm9
     5ed: 44 0f 28 94 24 a0 00 00 00   	movaps	0xa0(%rsp), %xmm10
     5f6: 44 0f 28 9c 24 b0 00 00 00   	movaps	0xb0(%rsp), %xmm11
     5ff: 44 0f 28 a4 24 c0 00 00 00   	movaps	0xc0(%rsp), %xmm12
     608: 44 0f 28 ac 24 d0 00 00 00   	movaps	0xd0(%rsp), %xmm13
     611: 44 0f 28 b4 24 e0 00 00 00   	movaps	0xe0(%rsp), %xmm14
     61a: 44 0f 28 bc 24 f0 00 00 00   	movaps	0xf0(%rsp), %xmm15
     623: 48 81 c4 08 01 00 00         	addq	$0x108, %rsp            # imm = 0x108
     62a: 5b                           	popq	%rbx
     62b: 5d                           	popq	%rbp
     62c: 5f                           	popq	%rdi
     62d: 5e                           	popq	%rsi
     62e: 41 5c                        	popq	%r12
     630: 41 5d                        	popq	%r13
     632: 41 5e                        	popq	%r14
     634: 41 5f                        	popq	%r15
     636: c3                           	retq
