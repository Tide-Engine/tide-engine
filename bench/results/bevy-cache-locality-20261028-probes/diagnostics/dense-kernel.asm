
build/bevy-cache-locality-probe/staged_soa_all/wide.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <tide_rows_Shift_2>:
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
      5c: 0f 83 3d 02 00 00            	jae	0x29f <tide_rows_Shift_2+0x29f>
      62: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
      6a: 4c 8b ac 24 78 01 00 00      	movq	0x178(%rsp), %r13
      72: 48 8b b4 24 70 01 00 00      	movq	0x170(%rsp), %rsi
      7a: 48 8b bc 24 68 01 00 00      	movq	0x168(%rsp), %rdi
      82: 4c 8b 9c 24 60 01 00 00      	movq	0x160(%rsp), %r11
      8a: 4c 8b b4 24 58 01 00 00      	movq	0x158(%rsp), %r14
      92: 4c 8b bc 24 50 01 00 00      	movq	0x150(%rsp), %r15
      9a: 48 8b 9c 24 48 01 00 00      	movq	0x148(%rsp), %rbx
      a2: 48 8b ac 24 38 01 00 00      	movq	0x138(%rsp), %rbp
      aa: 4c 8b a4 24 30 01 00 00      	movq	0x130(%rsp), %r12
      b2: f3 0f 10 01                  	movss	(%rcx), %xmm0
      b6: 89 d1                        	movl	%edx, %ecx
      b8: 44 89 c0                     	movl	%r8d, %eax
      bb: 48 89 c2                     	movq	%rax, %rdx
      be: 48 29 ca                     	subq	%rcx, %rdx
      c1: 48 83 fa 04                  	cmpq	$0x4, %rdx
      c5: 0f 82 2e 02 00 00            	jb	0x2f9 <tide_rows_Shift_2+0x2f9>
      cb: 48 89 54 24 08               	movq	%rdx, 0x8(%rsp)
      d0: 49 89 d0                     	movq	%rdx, %r8
      d3: 49 83 e0 fc                  	andq	$-0x4, %r8
      d7: 49 8d 14 08                  	leaq	(%r8,%rcx), %rdx
      db: 48 89 54 24 10               	movq	%rdx, 0x10(%rsp)
      e0: 0f 28 c8                     	movaps	%xmm0, %xmm1
      e3: 0f c6 c8 00                  	shufps	$0x0, %xmm0, %xmm1      # xmm1 = xmm1[0,0],xmm0[0,0]
      e7: 4d 8d 14 8a                  	leaq	(%r10,%rcx,4), %r10
      eb: 4c 89 da                     	movq	%r11, %rdx
      ee: 4d 8d 5c 8d 00               	leaq	(%r13,%rcx,4), %r11
      f3: 48 8d 34 8e                  	leaq	(%rsi,%rcx,4), %rsi
      f7: 48 8d 3c 8f                  	leaq	(%rdi,%rcx,4), %rdi
      fb: 49 89 dd                     	movq	%rbx, %r13
      fe: 48 8d 1c 8a                  	leaq	(%rdx,%rcx,4), %rbx
     102: 4d 8d 34 8e                  	leaq	(%r14,%rcx,4), %r14
     106: 4d 8d 3c 8f                  	leaq	(%r15,%rcx,4), %r15
     10a: 4c 89 e2                     	movq	%r12, %rdx
     10d: 4d 8d 64 8d 00               	leaq	(%r13,%rcx,4), %r12
     112: 4c 8b ac 24 40 01 00 00      	movq	0x140(%rsp), %r13
     11a: 4d 8d 6c 8d 00               	leaq	(%r13,%rcx,4), %r13
     11f: 48 8d 6c 8d 00               	leaq	(%rbp,%rcx,4), %rbp
     124: 48 8d 14 8a                  	leaq	(%rdx,%rcx,4), %rdx
     128: 4c 89 4c 24 18               	movq	%r9, 0x18(%rsp)
     12d: 4d 8d 0c 89                  	leaq	(%r9,%rcx,4), %r9
     131: 31 c9                        	xorl	%ecx, %ecx
     133: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x13a <tide_rows_Shift_2+0x13a>
     13a: 0f 28 1d 00 00 00 00         	movaps	(%rip), %xmm3           # 0x141 <tide_rows_Shift_2+0x141>
     141: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     150: 45 0f 10 1c 89               	movups	(%r9,%rcx,4), %xmm11
     155: 44 0f 10 14 8a               	movups	(%rdx,%rcx,4), %xmm10
     15a: 44 0f 10 4c 8d 00            	movups	(%rbp,%rcx,4), %xmm9
     160: 45 0f 10 44 8d 00            	movups	(%r13,%rcx,4), %xmm8
     166: 45 0f 10 24 8c               	movups	(%r12,%rcx,4), %xmm12
     16b: 45 0f 10 2c 8f               	movups	(%r15,%rcx,4), %xmm13
     170: 41 0f 10 24 8e               	movups	(%r14,%rcx,4), %xmm4
     175: 0f 10 2c 8b                  	movups	(%rbx,%rcx,4), %xmm5
     179: 44 0f 10 34 8f               	movups	(%rdi,%rcx,4), %xmm14
     17e: 44 0f 10 3c 8e               	movups	(%rsi,%rcx,4), %xmm15
     183: 41 0f 10 3c 8b               	movups	(%r11,%rcx,4), %xmm7
     188: 41 0f 10 34 8a               	movups	(%r10,%rcx,4), %xmm6
     18d: 0f 59 e1                     	mulps	%xmm1, %xmm4
     190: 41 0f 58 e0                  	addps	%xmm8, %xmm4
     194: 0f 59 e9                     	mulps	%xmm1, %xmm5
     197: 41 0f 58 ec                  	addps	%xmm12, %xmm5
     19b: 44 0f 59 f1                  	mulps	%xmm1, %xmm14
     19f: 45 0f 58 f5                  	addps	%xmm13, %xmm14
     1a3: 44 0f 59 f9                  	mulps	%xmm1, %xmm15
     1a7: 44 0f 28 c2                  	movaps	%xmm2, %xmm8
     1ab: 45 0f 5c c7                  	subps	%xmm15, %xmm8
     1af: 41 0f 59 e0                  	mulps	%xmm8, %xmm4
     1b3: 41 0f 59 e8                  	mulps	%xmm8, %xmm5
     1b7: 45 0f 59 c6                  	mulps	%xmm14, %xmm8
     1bb: 44 0f 28 e1                  	movaps	%xmm1, %xmm12
     1bf: 44 0f 59 e4                  	mulps	%xmm4, %xmm12
     1c3: 45 0f 58 e3                  	addps	%xmm11, %xmm12
     1c7: 44 0f 28 d9                  	movaps	%xmm1, %xmm11
     1cb: 44 0f 59 dd                  	mulps	%xmm5, %xmm11
     1cf: 45 0f 58 da                  	addps	%xmm10, %xmm11
     1d3: 44 0f 28 d1                  	movaps	%xmm1, %xmm10
     1d7: 45 0f 59 d0                  	mulps	%xmm8, %xmm10
     1db: 45 0f 58 d1                  	addps	%xmm9, %xmm10
     1df: 45 0f 28 cb                  	movaps	%xmm11, %xmm9
     1e3: 44 0f c2 ce 01               	cmpltps	%xmm6, %xmm9
     1e8: 0f 58 f6                     	addps	%xmm6, %xmm6
     1eb: 41 0f 5c f3                  	subps	%xmm11, %xmm6
     1ef: 45 0f 28 e9                  	movaps	%xmm9, %xmm13
     1f3: 44 0f 55 ed                  	andnps	%xmm5, %xmm13
     1f7: 0f 57 eb                     	xorps	%xmm3, %xmm5
     1fa: 0f 59 ef                     	mulps	%xmm7, %xmm5
     1fd: 41 0f 54 e9                  	andps	%xmm9, %xmm5
     201: 41 0f 56 ed                  	orps	%xmm13, %xmm5
     205: 41 0f 54 f1                  	andps	%xmm9, %xmm6
     209: 45 0f 55 cb                  	andnps	%xmm11, %xmm9
     20d: 44 0f 56 ce                  	orps	%xmm6, %xmm9
     211: 45 0f 11 24 89               	movups	%xmm12, (%r9,%rcx,4)
     216: 44 0f 11 0c 8a               	movups	%xmm9, (%rdx,%rcx,4)
     21b: 44 0f 11 54 8d 00            	movups	%xmm10, (%rbp,%rcx,4)
     221: 41 0f 11 64 8d 00            	movups	%xmm4, (%r13,%rcx,4)
     227: 41 0f 11 2c 8c               	movups	%xmm5, (%r12,%rcx,4)
     22c: 45 0f 11 04 8f               	movups	%xmm8, (%r15,%rcx,4)
     231: 48 83 c1 04                  	addq	$0x4, %rcx
     235: 49 39 c8                     	cmpq	%rcx, %r8
     238: 0f 85 12 ff ff ff            	jne	0x150 <tide_rows_Shift_2+0x150>
     23e: 4c 39 44 24 08               	cmpq	%r8, 0x8(%rsp)
     243: 4c 8b 4c 24 18               	movq	0x18(%rsp), %r9
     248: 4c 8b a4 24 30 01 00 00      	movq	0x130(%rsp), %r12
     250: 48 8b ac 24 38 01 00 00      	movq	0x138(%rsp), %rbp
     258: 48 8b 9c 24 48 01 00 00      	movq	0x148(%rsp), %rbx
     260: 4c 8b bc 24 50 01 00 00      	movq	0x150(%rsp), %r15
     268: 4c 8b b4 24 58 01 00 00      	movq	0x158(%rsp), %r14
     270: 4c 8b 9c 24 60 01 00 00      	movq	0x160(%rsp), %r11
     278: 48 8b bc 24 68 01 00 00      	movq	0x168(%rsp), %rdi
     280: 48 8b b4 24 70 01 00 00      	movq	0x170(%rsp), %rsi
     288: 4c 8b ac 24 78 01 00 00      	movq	0x178(%rsp), %r13
     290: 4c 8b 94 24 80 01 00 00      	movq	0x180(%rsp), %r10
     298: 48 8b 4c 24 10               	movq	0x10(%rsp), %rcx
     29d: 75 5a                        	jne	0x2f9 <tide_rows_Shift_2+0x2f9>
     29f: 0f 28 74 24 20               	movaps	0x20(%rsp), %xmm6
     2a4: 0f 28 7c 24 30               	movaps	0x30(%rsp), %xmm7
     2a9: 44 0f 28 44 24 40            	movaps	0x40(%rsp), %xmm8
     2af: 44 0f 28 4c 24 50            	movaps	0x50(%rsp), %xmm9
     2b5: 44 0f 28 54 24 60            	movaps	0x60(%rsp), %xmm10
     2bb: 44 0f 28 5c 24 70            	movaps	0x70(%rsp), %xmm11
     2c1: 44 0f 28 a4 24 80 00 00 00   	movaps	0x80(%rsp), %xmm12
     2ca: 44 0f 28 ac 24 90 00 00 00   	movaps	0x90(%rsp), %xmm13
     2d3: 44 0f 28 b4 24 a0 00 00 00   	movaps	0xa0(%rsp), %xmm14
     2dc: 44 0f 28 bc 24 b0 00 00 00   	movaps	0xb0(%rsp), %xmm15
     2e5: 48 81 c4 c8 00 00 00         	addq	$0xc8, %rsp
     2ec: 5b                           	popq	%rbx
     2ed: 5d                           	popq	%rbp
     2ee: 5f                           	popq	%rdi
     2ef: 5e                           	popq	%rsi
     2f0: 41 5c                        	popq	%r12
     2f2: 41 5d                        	popq	%r13
     2f4: 41 5e                        	popq	%r14
     2f6: 41 5f                        	popq	%r15
     2f8: c3                           	retq
     2f9: 49 8d 14 8a                  	leaq	(%r10,%rcx,4), %rdx
     2fd: 4c 8d 04 8d 00 00 00 00      	leaq	(,%rcx,4), %r8
     305: 4d 01 e8                     	addq	%r13, %r8
     308: 4c 8d 2c 8e                  	leaq	(%rsi,%rcx,4), %r13
     30c: 4c 8d 14 8f                  	leaq	(%rdi,%rcx,4), %r10
     310: 4d 8d 1c 8b                  	leaq	(%r11,%rcx,4), %r11
     314: 49 8d 34 8e                  	leaq	(%r14,%rcx,4), %rsi
     318: 49 8d 3c 8f                  	leaq	(%r15,%rcx,4), %rdi
     31c: 48 8d 1c 8b                  	leaq	(%rbx,%rcx,4), %rbx
     320: 49 89 ef                     	movq	%rbp, %r15
     323: 48 89 cd                     	movq	%rcx, %rbp
     326: 48 8b 8c 24 40 01 00 00      	movq	0x140(%rsp), %rcx
     32e: 4c 8d 34 a9                  	leaq	(%rcx,%rbp,4), %r14
     332: 4d 8d 3c af                  	leaq	(%r15,%rbp,4), %r15
     336: 4d 8d 24 ac                  	leaq	(%r12,%rbp,4), %r12
     33a: 4d 8d 0c a9                  	leaq	(%r9,%rbp,4), %r9
     33e: 48 29 e8                     	subq	%rbp, %rax
     341: 31 c9                        	xorl	%ecx, %ecx
     343: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x34b <tide_rows_Shift_2+0x34b>
     34b: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x352 <tide_rows_Shift_2+0x352>
     352: e9 a3 00 00 00               	jmp	0x3fa <tide_rows_Shift_2+0x3fa>
     357: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     360: 0f 28 f4                     	movaps	%xmm4, %xmm6
     363: f3 0f 10 3c 8e               	movss	(%rsi,%rcx,4), %xmm7
     368: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     36c: f3 45 0f 10 04 8a            	movss	(%r10,%rcx,4), %xmm8
     372: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     377: f3 41 0f 58 3c 8e            	addss	(%r14,%rcx,4), %xmm7
     37d: f3 44 0f 58 04 8f            	addss	(%rdi,%rcx,4), %xmm8
     383: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     388: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     38d: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     391: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     396: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     39a: f3 45 0f 58 0c 89            	addss	(%r9,%rcx,4), %xmm9
     3a0: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     3a5: f3 45 0f 58 14 8f            	addss	(%r15,%rcx,4), %xmm10
     3ab: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     3af: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     3b3: f3 45 0f 59 1c 88            	mulss	(%r8,%rcx,4), %xmm11
     3b9: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     3be: 0f 28 ec                     	movaps	%xmm4, %xmm5
     3c1: 0f 55 eb                     	andnps	%xmm3, %xmm5
     3c4: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     3c8: 0f 56 e5                     	orps	%xmm5, %xmm4
     3cb: f3 45 0f 11 0c 89            	movss	%xmm9, (%r9,%rcx,4)
     3d1: f3 41 0f 11 34 8c            	movss	%xmm6, (%r12,%rcx,4)
     3d7: f3 45 0f 11 14 8f            	movss	%xmm10, (%r15,%rcx,4)
     3dd: f3 41 0f 11 3c 8e            	movss	%xmm7, (%r14,%rcx,4)
     3e3: f3 0f 11 24 8b               	movss	%xmm4, (%rbx,%rcx,4)
     3e8: f3 44 0f 11 04 8f            	movss	%xmm8, (%rdi,%rcx,4)
     3ee: 48 ff c1                     	incq	%rcx
     3f1: 48 39 c8                     	cmpq	%rcx, %rax
     3f4: 0f 84 a5 fe ff ff            	je	0x29f <tide_rows_Shift_2+0x29f>
     3fa: f3 41 0f 10 1c 8b            	movss	(%r11,%rcx,4), %xmm3
     400: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     404: f3 0f 58 1c 8b               	addss	(%rbx,%rcx,4), %xmm3
     409: f3 0f 10 2c 8a               	movss	(%rdx,%rcx,4), %xmm5
     40e: f3 41 0f 10 64 8d 00         	movss	(%r13,%rcx,4), %xmm4
     415: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     419: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     41d: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     422: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     427: 0f 28 e0                     	movaps	%xmm0, %xmm4
     42a: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     42e: f3 41 0f 58 24 8c            	addss	(%r12,%rcx,4), %xmm4
     434: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     437: 0f 86 23 ff ff ff            	jbe	0x360 <tide_rows_Shift_2+0x360>
     43d: 0f 28 f5                     	movaps	%xmm5, %xmm6
     440: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     444: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     448: e9 16 ff ff ff               	jmp	0x363 <tide_rows_Shift_2+0x363>
