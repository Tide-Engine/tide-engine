
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/versioned/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec 88 00 00 00         	subq	$0x88, %rsp
      13: 44 0f 29 5c 24 70            	movaps	%xmm11, 0x70(%rsp)
      19: 44 0f 29 54 24 60            	movaps	%xmm10, 0x60(%rsp)
      1f: 44 0f 29 4c 24 50            	movaps	%xmm9, 0x50(%rsp)
      25: 44 0f 29 44 24 40            	movaps	%xmm8, 0x40(%rsp)
      2b: 0f 29 7c 24 30               	movaps	%xmm7, 0x30(%rsp)
      30: 0f 29 74 24 20               	movaps	%xmm6, 0x20(%rsp)
      35: 41 89 d3                     	movl	%edx, %r11d
      38: 4c 8b 94 24 48 01 00 00      	movq	0x148(%rsp), %r10
      40: 48 8b b4 24 38 01 00 00      	movq	0x138(%rsp), %rsi
      48: 48 8b bc 24 30 01 00 00      	movq	0x130(%rsp), %rdi
      50: 48 8b 9c 24 28 01 00 00      	movq	0x128(%rsp), %rbx
      58: 4c 8b b4 24 18 01 00 00      	movq	0x118(%rsp), %r14
      60: 4c 8b bc 24 10 01 00 00      	movq	0x110(%rsp), %r15
      68: 4c 8b a4 24 08 01 00 00      	movq	0x108(%rsp), %r12
      70: 4c 8b ac 24 00 01 00 00      	movq	0x100(%rsp), %r13
      78: 41 8b 11                     	movl	(%r9), %edx
      7b: 83 c2 3f                     	addl	$0x3f, %edx
      7e: c1 ea 06                     	shrl	$0x6, %edx
      81: 48 8b ac 24 f8 00 00 00      	movq	0xf8(%rsp), %rbp
      89: 48 8b 84 24 f0 00 00 00      	movq	0xf0(%rsp), %rax
      91: 41 39 51 04                  	cmpl	%edx, 0x4(%r9)
      95: 0f 83 86 02 00 00            	jae	0x321 <tide_rows_Shift_6+0x321>
      9b: 48 89 4c 24 10               	movq	%rcx, 0x10(%rsp)
      a0: 4c 89 44 24 18               	movq	%r8, 0x18(%rsp)
      a5: 44 89 c1                     	movl	%r8d, %ecx
      a8: 44 89 5c 24 08               	movl	%r11d, 0x8(%rsp)
      ad: 44 29 d9                     	subl	%r11d, %ecx
      b0: 89 4c 24 04                  	movl	%ecx, 0x4(%rsp)
      b4: 48 8b 94 24 20 01 00 00      	movq	0x120(%rsp), %rdx
      bc: 0f 86 84 00 00 00            	jbe	0x146 <tide_rows_Shift_6+0x146>
      c2: 44 8b 44 24 08               	movl	0x8(%rsp), %r8d
      c7: 44 8b 5c 24 18               	movl	0x18(%rsp), %r11d
      cc: 49 8d 48 04                  	leaq	0x4(%r8), %rcx
      d0: 49 39 cb                     	cmpq	%rcx, %r11
      d3: 49 0f 42 cb                  	cmovbq	%r11, %rcx
      d7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
      e0: 4d 89 d3                     	movq	%r10, %r11
      e3: 47 0f b7 54 41 0a            	movzwl	0xa(%r9,%r8,2), %r10d
      e9: 41 c1 e2 02                  	shll	$0x2, %r10d
      ed: 42 0f 18 0c 10               	prefetcht0	(%rax,%r10)
      f2: 42 0f 18 4c 15 00            	prefetcht0	(%rbp,%r10)
      f8: 43 0f 18 4c 15 00            	prefetcht0	(%r13,%r10)
      fe: 43 0f 18 0c 14               	prefetcht0	(%r12,%r10)
     103: 43 0f 18 0c 17               	prefetcht0	(%r15,%r10)
     108: 43 0f 18 0c 16               	prefetcht0	(%r14,%r10)
     10d: 42 0f 18 0c 12               	prefetcht0	(%rdx,%r10)
     112: 42 0f 18 0c 13               	prefetcht0	(%rbx,%r10)
     117: 42 0f 18 0c 17               	prefetcht0	(%rdi,%r10)
     11c: 48 8b b4 24 38 01 00 00      	movq	0x138(%rsp), %rsi
     124: 42 0f 18 0c 16               	prefetcht0	(%rsi,%r10)
     129: 48 8b b4 24 40 01 00 00      	movq	0x140(%rsp), %rsi
     131: 42 0f 18 0c 16               	prefetcht0	(%rsi,%r10)
     136: 43 0f 18 0c 13               	prefetcht0	(%r11,%r10)
     13b: 4d 89 da                     	movq	%r11, %r10
     13e: 49 ff c0                     	incq	%r8
     141: 49 39 c8                     	cmpq	%rcx, %r8
     144: 72 9a                        	jb	0xe0 <tide_rows_Shift_6+0xe0>
     146: 48 89 d9                     	movq	%rbx, %rcx
     149: 48 89 fb                     	movq	%rdi, %rbx
     14c: 48 8b bc 24 38 01 00 00      	movq	0x138(%rsp), %rdi
     154: 48 8b b4 24 40 01 00 00      	movq	0x140(%rsp), %rsi
     15c: 4c 8b 44 24 18               	movq	0x18(%rsp), %r8
     161: 45 8d 58 fc                  	leal	-0x4(%r8), %r11d
     165: 83 7c 24 04 05               	cmpl	$0x5, 0x4(%rsp)
     16a: 8b 54 24 08                  	movl	0x8(%rsp), %edx
     16e: 44 0f 42 da                  	cmovbl	%edx, %r11d
     172: 44 39 da                     	cmpl	%r11d, %edx
     175: 0f 83 ee 02 00 00            	jae	0x469 <tide_rows_Shift_6+0x469>
     17b: 41 89 d0                     	movl	%edx, %r8d
     17e: 48 8b 54 24 10               	movq	0x10(%rsp), %rdx
     183: f3 0f 10 02                  	movss	(%rdx), %xmm0
     187: 44 89 c2                     	movl	%r8d, %edx
     18a: 44 89 5c 24 04               	movl	%r11d, 0x4(%rsp)
     18f: 45 89 d8                     	movl	%r11d, %r8d
     192: 48 83 c2 04                  	addq	$0x4, %rdx
     196: 49 f7 d8                     	negq	%r8
     199: 4c 89 44 24 08               	movq	%r8, 0x8(%rsp)
     19e: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x1a6 <tide_rows_Shift_6+0x1a6>
     1a6: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x1ad <tide_rows_Shift_6+0x1ad>
     1ad: 49 89 f3                     	movq	%rsi, %r11
     1b0: 48 89 fe                     	movq	%rdi, %rsi
     1b3: 48 89 df                     	movq	%rbx, %rdi
     1b6: 48 89 cb                     	movq	%rcx, %rbx
     1b9: 4c 8b 84 24 20 01 00 00      	movq	0x120(%rsp), %r8
     1c1: e9 bc 00 00 00               	jmp	0x282 <tide_rows_Shift_6+0x282>
     1c6: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     1d0: 0f 28 f4                     	movaps	%xmm4, %xmm6
     1d3: 4c 8b 84 24 20 01 00 00      	movq	0x120(%rsp), %r8
     1db: f3 41 0f 10 3c 08            	movss	(%r8,%rcx), %xmm7
     1e1: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     1e5: f3 44 0f 10 04 0f            	movss	(%rdi,%rcx), %xmm8
     1eb: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     1f0: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
     1f6: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
     1fc: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     201: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     206: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     20a: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     20f: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     213: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     218: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
     21e: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
     225: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     229: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     22d: f3 45 0f 59 1c 0b            	mulss	(%r11,%rcx), %xmm11
     233: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     238: 0f 28 ec                     	movaps	%xmm4, %xmm5
     23b: 0f 55 eb                     	andnps	%xmm3, %xmm5
     23e: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     242: 0f 56 e5                     	orps	%xmm5, %xmm4
     245: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     24b: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     251: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     258: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     25e: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     264: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     26a: 48 8b 4c 24 08               	movq	0x8(%rsp), %rcx
     26f: 48 01 d1                     	addq	%rdx, %rcx
     272: 48 ff c1                     	incq	%rcx
     275: 48 ff c2                     	incq	%rdx
     278: 48 83 f9 04                  	cmpq	$0x4, %rcx
     27c: 0f 84 f5 01 00 00            	je	0x477 <tide_rows_Shift_6+0x477>
     282: 89 d1                        	movl	%edx, %ecx
     284: 41 0f b7 4c 49 0a            	movzwl	0xa(%r9,%rcx,2), %ecx
     28a: c1 e1 02                     	shll	$0x2, %ecx
     28d: 0f 18 0c 08                  	prefetcht0	(%rax,%rcx)
     291: 0f 18 4c 0d 00               	prefetcht0	(%rbp,%rcx)
     296: 41 0f 18 4c 0d 00            	prefetcht0	(%r13,%rcx)
     29c: 41 0f 18 0c 0c               	prefetcht0	(%r12,%rcx)
     2a1: 41 0f 18 0c 0f               	prefetcht0	(%r15,%rcx)
     2a6: 41 0f 18 0c 0e               	prefetcht0	(%r14,%rcx)
     2ab: 41 0f 18 0c 08               	prefetcht0	(%r8,%rcx)
     2b0: 0f 18 0c 0b                  	prefetcht0	(%rbx,%rcx)
     2b4: 0f 18 0c 0f                  	prefetcht0	(%rdi,%rcx)
     2b8: 0f 18 0c 0e                  	prefetcht0	(%rsi,%rcx)
     2bc: 41 0f 18 0c 0b               	prefetcht0	(%r11,%rcx)
     2c1: 41 0f 18 0c 0a               	prefetcht0	(%r10,%rcx)
     2c6: 41 0f b7 4c 51 02            	movzwl	0x2(%r9,%rdx,2), %ecx
     2cc: c1 e1 02                     	shll	$0x2, %ecx
     2cf: f3 0f 10 1c 0b               	movss	(%rbx,%rcx), %xmm3
     2d4: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     2d8: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     2de: f3 41 0f 10 2c 0a            	movss	(%r10,%rcx), %xmm5
     2e4: f3 0f 10 24 0e               	movss	(%rsi,%rcx), %xmm4
     2e9: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     2ed: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     2f1: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     2f6: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     2fb: 0f 28 e0                     	movaps	%xmm0, %xmm4
     2fe: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     302: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     308: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     30b: 0f 86 bf fe ff ff            	jbe	0x1d0 <tide_rows_Shift_6+0x1d0>
     311: 0f 28 f5                     	movaps	%xmm5, %xmm6
     314: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     318: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     31c: e9 b2 fe ff ff               	jmp	0x1d3 <tide_rows_Shift_6+0x1d3>
     321: 45 39 d8                     	cmpl	%r11d, %r8d
     324: 0f 86 85 02 00 00            	jbe	0x5af <tide_rows_Shift_6+0x5af>
     32a: 48 89 f7                     	movq	%rsi, %rdi
     32d: f3 0f 10 01                  	movss	(%rcx), %xmm0
     331: 44 89 da                     	movl	%r11d, %edx
     334: 45 89 c0                     	movl	%r8d, %r8d
     337: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x33f <tide_rows_Shift_6+0x33f>
     33f: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x346 <tide_rows_Shift_6+0x346>
     346: 48 8b b4 24 40 01 00 00      	movq	0x140(%rsp), %rsi
     34e: e9 bb 00 00 00               	jmp	0x40e <tide_rows_Shift_6+0x40e>
     353: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     360: 0f 28 f4                     	movaps	%xmm4, %xmm6
     363: 4c 8b 9c 24 20 01 00 00      	movq	0x120(%rsp), %r11
     36b: f3 41 0f 10 3c 0b            	movss	(%r11,%rcx), %xmm7
     371: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     375: 4c 8b 9c 24 30 01 00 00      	movq	0x130(%rsp), %r11
     37d: f3 45 0f 10 04 0b            	movss	(%r11,%rcx), %xmm8
     383: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     388: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
     38e: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
     394: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     399: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     39e: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     3a2: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     3a7: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     3ab: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
     3b1: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     3b6: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
     3bd: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     3c1: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     3c5: f3 44 0f 59 1c 0e            	mulss	(%rsi,%rcx), %xmm11
     3cb: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     3d0: 0f 28 ec                     	movaps	%xmm4, %xmm5
     3d3: 0f 55 eb                     	andnps	%xmm3, %xmm5
     3d6: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     3da: 0f 56 e5                     	orps	%xmm5, %xmm4
     3dd: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     3e3: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     3e9: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     3f0: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     3f6: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     3fc: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     402: 48 ff c2                     	incq	%rdx
     405: 49 39 d0                     	cmpq	%rdx, %r8
     408: 0f 84 a1 01 00 00            	je	0x5af <tide_rows_Shift_6+0x5af>
     40e: 41 0f b7 4c 51 0a            	movzwl	0xa(%r9,%rdx,2), %ecx
     414: c1 e1 02                     	shll	$0x2, %ecx
     417: f3 0f 10 1c 0b               	movss	(%rbx,%rcx), %xmm3
     41c: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     420: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     426: f3 41 0f 10 2c 0a            	movss	(%r10,%rcx), %xmm5
     42c: f3 0f 10 24 0f               	movss	(%rdi,%rcx), %xmm4
     431: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     435: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     439: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     43e: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     443: 0f 28 e0                     	movaps	%xmm0, %xmm4
     446: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     44a: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     450: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     453: 0f 86 07 ff ff ff            	jbe	0x360 <tide_rows_Shift_6+0x360>
     459: 0f 28 f5                     	movaps	%xmm5, %xmm6
     45c: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     460: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     464: e9 fa fe ff ff               	jmp	0x363 <tide_rows_Shift_6+0x363>
     469: 49 89 f3                     	movq	%rsi, %r11
     46c: 48 89 fe                     	movq	%rdi, %rsi
     46f: 48 89 df                     	movq	%rbx, %rdi
     472: 48 89 cb                     	movq	%rcx, %rbx
     475: eb 09                        	jmp	0x480 <tide_rows_Shift_6+0x480>
     477: 8b 54 24 04                  	movl	0x4(%rsp), %edx
     47b: 4c 8b 44 24 18               	movq	0x18(%rsp), %r8
     480: 48 8b 4c 24 10               	movq	0x10(%rsp), %rcx
     485: 44 39 c2                     	cmpl	%r8d, %edx
     488: 0f 83 21 01 00 00            	jae	0x5af <tide_rows_Shift_6+0x5af>
     48e: f3 0f 10 01                  	movss	(%rcx), %xmm0
     492: 89 d2                        	movl	%edx, %edx
     494: 44 89 c1                     	movl	%r8d, %ecx
     497: 48 89 4c 24 08               	movq	%rcx, 0x8(%rsp)
     49c: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x4a4 <tide_rows_Shift_6+0x4a4>
     4a4: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x4ab <tide_rows_Shift_6+0x4ab>
     4ab: e9 a4 00 00 00               	jmp	0x554 <tide_rows_Shift_6+0x554>
     4b0: 0f 28 f4                     	movaps	%xmm4, %xmm6
     4b3: 4c 8b 84 24 20 01 00 00      	movq	0x120(%rsp), %r8
     4bb: f3 41 0f 10 3c 08            	movss	(%r8,%rcx), %xmm7
     4c1: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     4c5: f3 44 0f 10 04 0f            	movss	(%rdi,%rcx), %xmm8
     4cb: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     4d0: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
     4d6: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
     4dc: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     4e1: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     4e6: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     4ea: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     4ef: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     4f3: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
     4f9: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     4fe: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
     505: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     509: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     50d: f3 45 0f 59 1c 0b            	mulss	(%r11,%rcx), %xmm11
     513: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     518: 0f 28 ec                     	movaps	%xmm4, %xmm5
     51b: 0f 55 eb                     	andnps	%xmm3, %xmm5
     51e: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     522: 0f 56 e5                     	orps	%xmm5, %xmm4
     525: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     52b: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     531: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     538: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     53e: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     544: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     54a: 48 ff c2                     	incq	%rdx
     54d: 48 39 54 24 08               	cmpq	%rdx, 0x8(%rsp)
     552: 74 5b                        	je	0x5af <tide_rows_Shift_6+0x5af>
     554: 41 0f b7 4c 51 0a            	movzwl	0xa(%r9,%rdx,2), %ecx
     55a: c1 e1 02                     	shll	$0x2, %ecx
     55d: f3 0f 10 1c 0b               	movss	(%rbx,%rcx), %xmm3
     562: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     566: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     56c: f3 41 0f 10 2c 0a            	movss	(%r10,%rcx), %xmm5
     572: f3 0f 10 24 0e               	movss	(%rsi,%rcx), %xmm4
     577: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     57b: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     57f: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     584: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     589: 0f 28 e0                     	movaps	%xmm0, %xmm4
     58c: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     590: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     596: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     599: 0f 86 11 ff ff ff            	jbe	0x4b0 <tide_rows_Shift_6+0x4b0>
     59f: 0f 28 f5                     	movaps	%xmm5, %xmm6
     5a2: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     5a6: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     5aa: e9 04 ff ff ff               	jmp	0x4b3 <tide_rows_Shift_6+0x4b3>
     5af: 0f 28 74 24 20               	movaps	0x20(%rsp), %xmm6
     5b4: 0f 28 7c 24 30               	movaps	0x30(%rsp), %xmm7
     5b9: 44 0f 28 44 24 40            	movaps	0x40(%rsp), %xmm8
     5bf: 44 0f 28 4c 24 50            	movaps	0x50(%rsp), %xmm9
     5c5: 44 0f 28 54 24 60            	movaps	0x60(%rsp), %xmm10
     5cb: 44 0f 28 5c 24 70            	movaps	0x70(%rsp), %xmm11
     5d1: 48 81 c4 88 00 00 00         	addq	$0x88, %rsp
     5d8: 5b                           	popq	%rbx
     5d9: 5d                           	popq	%rbp
     5da: 5f                           	popq	%rdi
     5db: 5e                           	popq	%rsi
     5dc: 41 5c                        	popq	%r12
     5de: 41 5d                        	popq	%r13
     5e0: 41 5e                        	popq	%r14
     5e2: 41 5f                        	popq	%r15
     5e4: c3                           	retq
