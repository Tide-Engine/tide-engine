
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/prefetch4/wide.obj:	file format coff-x86-64

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
       c: 48 83 ec 78                  	subq	$0x78, %rsp
      10: 44 0f 29 5c 24 60            	movaps	%xmm11, 0x60(%rsp)
      16: 44 0f 29 54 24 50            	movaps	%xmm10, 0x50(%rsp)
      1c: 44 0f 29 4c 24 40            	movaps	%xmm9, 0x40(%rsp)
      22: 44 0f 29 44 24 30            	movaps	%xmm8, 0x30(%rsp)
      28: 0f 29 7c 24 20               	movaps	%xmm7, 0x20(%rsp)
      2d: 0f 29 74 24 10               	movaps	%xmm6, 0x10(%rsp)
      32: 44 39 c2                     	cmpl	%r8d, %edx
      35: 0f 83 79 02 00 00            	jae	0x2b4 <tide_rows_Shift_6+0x2b4>
      3b: 48 89 4c 24 08               	movq	%rcx, 0x8(%rsp)
      40: 4c 8b 9c 24 38 01 00 00      	movq	0x138(%rsp), %r11
      48: 48 8b 9c 24 10 01 00 00      	movq	0x110(%rsp), %rbx
      50: 4c 8b b4 24 08 01 00 00      	movq	0x108(%rsp), %r14
      58: 4c 8b bc 24 00 01 00 00      	movq	0x100(%rsp), %r15
      60: 4c 8b a4 24 f8 00 00 00      	movq	0xf8(%rsp), %r12
      68: 4c 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %r13
      70: 48 8b ac 24 e8 00 00 00      	movq	0xe8(%rsp), %rbp
      78: 41 89 d2                     	movl	%edx, %r10d
      7b: 44 89 c2                     	movl	%r8d, %edx
      7e: 4d 8d 42 04                  	leaq	0x4(%r10), %r8
      82: 4c 39 c2                     	cmpq	%r8, %rdx
      85: 4c 89 c1                     	movq	%r8, %rcx
      88: 48 0f 42 ca                  	cmovbq	%rdx, %rcx
      8c: 48 8b 84 24 e0 00 00 00      	movq	0xe0(%rsp), %rax
      94: 4c 89 14 24                  	movq	%r10, (%rsp)
      98: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
      a0: 4c 89 de                     	movq	%r11, %rsi
      a3: 47 0f b7 1c 51               	movzwl	(%r9,%r10,2), %r11d
      a8: 41 c1 e3 02                  	shll	$0x2, %r11d
      ac: 42 0f 18 0c 18               	prefetcht0	(%rax,%r11)
      b1: 42 0f 18 4c 1d 00            	prefetcht0	(%rbp,%r11)
      b7: 43 0f 18 4c 1d 00            	prefetcht0	(%r13,%r11)
      bd: 43 0f 18 0c 1c               	prefetcht0	(%r12,%r11)
      c2: 43 0f 18 0c 1f               	prefetcht0	(%r15,%r11)
      c7: 43 0f 18 0c 1e               	prefetcht0	(%r14,%r11)
      cc: 42 0f 18 0c 1b               	prefetcht0	(%rbx,%r11)
      d1: 48 8b bc 24 18 01 00 00      	movq	0x118(%rsp), %rdi
      d9: 42 0f 18 0c 1f               	prefetcht0	(%rdi,%r11)
      de: 48 8b bc 24 20 01 00 00      	movq	0x120(%rsp), %rdi
      e6: 42 0f 18 0c 1f               	prefetcht0	(%rdi,%r11)
      eb: 48 8b bc 24 28 01 00 00      	movq	0x128(%rsp), %rdi
      f3: 42 0f 18 0c 1f               	prefetcht0	(%rdi,%r11)
      f8: 48 8b bc 24 30 01 00 00      	movq	0x130(%rsp), %rdi
     100: 42 0f 18 0c 1f               	prefetcht0	(%rdi,%r11)
     105: 42 0f 18 0c 1e               	prefetcht0	(%rsi,%r11)
     10a: 49 89 f3                     	movq	%rsi, %r11
     10d: 49 ff c2                     	incq	%r10
     110: 49 39 ca                     	cmpq	%rcx, %r10
     113: 72 8b                        	jb	0xa0 <tide_rows_Shift_6+0xa0>
     115: 48 8b 4c 24 08               	movq	0x8(%rsp), %rcx
     11a: f3 0f 10 01                  	movss	(%rcx), %xmm0
     11e: 48 2b 14 24                  	subq	(%rsp), %rdx
     122: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x12a <tide_rows_Shift_6+0x12a>
     12a: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x131 <tide_rows_Shift_6+0x131>
     131: 48 8b b4 24 28 01 00 00      	movq	0x128(%rsp), %rsi
     139: 48 8b bc 24 20 01 00 00      	movq	0x120(%rsp), %rdi
     141: 48 8b 9c 24 18 01 00 00      	movq	0x118(%rsp), %rbx
     149: eb 63                        	jmp	0x1ae <tide_rows_Shift_6+0x1ae>
     14b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     150: 44 0f 28 d7                  	movaps	%xmm7, %xmm10
     154: 44 0f 28 dd                  	movaps	%xmm5, %xmm11
     158: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     15c: 4c 8b 94 24 30 01 00 00      	movq	0x130(%rsp), %r10
     164: f3 45 0f 59 1c 0a            	mulss	(%r10,%rcx), %xmm11
     16a: f3 0f c2 fc 01               	cmpltss	%xmm4, %xmm7
     16f: 0f 28 e7                     	movaps	%xmm7, %xmm4
     172: 0f 55 e5                     	andnps	%xmm5, %xmm4
     175: 41 0f 54 fb                  	andps	%xmm11, %xmm7
     179: 0f 56 fc                     	orps	%xmm4, %xmm7
     17c: f3 44 0f 11 04 08            	movss	%xmm8, (%rax,%rcx)
     182: f3 44 0f 11 54 0d 00         	movss	%xmm10, (%rbp,%rcx)
     189: f3 45 0f 11 4c 0d 00         	movss	%xmm9, (%r13,%rcx)
     190: f3 41 0f 11 1c 0c            	movss	%xmm3, (%r12,%rcx)
     196: f3 41 0f 11 3c 0f            	movss	%xmm7, (%r15,%rcx)
     19c: f3 41 0f 11 34 0e            	movss	%xmm6, (%r14,%rcx)
     1a2: 49 ff c0                     	incq	%r8
     1a5: 48 ff ca                     	decq	%rdx
     1a8: 0f 84 06 01 00 00            	je	0x2b4 <tide_rows_Shift_6+0x2b4>
     1ae: 48 83 fa 05                  	cmpq	$0x5, %rdx
     1b2: 72 54                        	jb	0x208 <tide_rows_Shift_6+0x208>
     1b4: 44 89 c1                     	movl	%r8d, %ecx
     1b7: 41 0f b7 0c 49               	movzwl	(%r9,%rcx,2), %ecx
     1bc: c1 e1 02                     	shll	$0x2, %ecx
     1bf: 0f 18 0c 08                  	prefetcht0	(%rax,%rcx)
     1c3: 0f 18 4c 0d 00               	prefetcht0	(%rbp,%rcx)
     1c8: 41 0f 18 4c 0d 00            	prefetcht0	(%r13,%rcx)
     1ce: 41 0f 18 0c 0c               	prefetcht0	(%r12,%rcx)
     1d3: 41 0f 18 0c 0f               	prefetcht0	(%r15,%rcx)
     1d8: 41 0f 18 0c 0e               	prefetcht0	(%r14,%rcx)
     1dd: 4c 8b 94 24 10 01 00 00      	movq	0x110(%rsp), %r10
     1e5: 41 0f 18 0c 0a               	prefetcht0	(%r10,%rcx)
     1ea: 0f 18 0c 0b                  	prefetcht0	(%rbx,%rcx)
     1ee: 0f 18 0c 0f                  	prefetcht0	(%rdi,%rcx)
     1f2: 0f 18 0c 0e                  	prefetcht0	(%rsi,%rcx)
     1f6: 4c 8b 94 24 30 01 00 00      	movq	0x130(%rsp), %r10
     1fe: 41 0f 18 0c 0a               	prefetcht0	(%r10,%rcx)
     203: 41 0f 18 0c 0b               	prefetcht0	(%r11,%rcx)
     208: 43 0f b7 4c 41 f8            	movzwl	-0x8(%r9,%r8,2), %ecx
     20e: c1 e1 02                     	shll	$0x2, %ecx
     211: f3 41 0f 10 24 0b            	movss	(%r11,%rcx), %xmm4
     217: 4c 8b 94 24 10 01 00 00      	movq	0x110(%rsp), %r10
     21f: f3 41 0f 10 1c 0a            	movss	(%r10,%rcx), %xmm3
     225: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     229: f3 0f 10 2c 0b               	movss	(%rbx,%rcx), %xmm5
     22e: f3 0f 59 e8                  	mulss	%xmm0, %xmm5
     232: f3 0f 10 3c 0f               	movss	(%rdi,%rcx), %xmm7
     237: f3 41 0f 58 1c 0c            	addss	(%r12,%rcx), %xmm3
     23d: f3 41 0f 58 2c 0f            	addss	(%r15,%rcx), %xmm5
     243: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     247: f3 41 0f 58 3c 0e            	addss	(%r14,%rcx), %xmm7
     24d: f3 44 0f 10 04 0e            	movss	(%rsi,%rcx), %xmm8
     253: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     258: 0f 28 f1                     	movaps	%xmm1, %xmm6
     25b: f3 41 0f 5c f0               	subss	%xmm8, %xmm6
     260: f3 0f 59 de                  	mulss	%xmm6, %xmm3
     264: f3 0f 59 ee                  	mulss	%xmm6, %xmm5
     268: f3 0f 59 f7                  	mulss	%xmm7, %xmm6
     26c: 44 0f 28 c0                  	movaps	%xmm0, %xmm8
     270: f3 44 0f 59 c3               	mulss	%xmm3, %xmm8
     275: 0f 28 f8                     	movaps	%xmm0, %xmm7
     278: f3 0f 59 fd                  	mulss	%xmm5, %xmm7
     27c: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     280: f3 44 0f 59 ce               	mulss	%xmm6, %xmm9
     285: f3 44 0f 58 04 08            	addss	(%rax,%rcx), %xmm8
     28b: f3 0f 58 7c 0d 00            	addss	(%rbp,%rcx), %xmm7
     291: f3 45 0f 58 4c 0d 00         	addss	(%r13,%rcx), %xmm9
     298: 0f 2e e7                     	ucomiss	%xmm7, %xmm4
     29b: 0f 86 af fe ff ff            	jbe	0x150 <tide_rows_Shift_6+0x150>
     2a1: 44 0f 28 d4                  	movaps	%xmm4, %xmm10
     2a5: f3 44 0f 58 d4               	addss	%xmm4, %xmm10
     2aa: f3 44 0f 5c d7               	subss	%xmm7, %xmm10
     2af: e9 a0 fe ff ff               	jmp	0x154 <tide_rows_Shift_6+0x154>
     2b4: 0f 28 74 24 10               	movaps	0x10(%rsp), %xmm6
     2b9: 0f 28 7c 24 20               	movaps	0x20(%rsp), %xmm7
     2be: 44 0f 28 44 24 30            	movaps	0x30(%rsp), %xmm8
     2c4: 44 0f 28 4c 24 40            	movaps	0x40(%rsp), %xmm9
     2ca: 44 0f 28 54 24 50            	movaps	0x50(%rsp), %xmm10
     2d0: 44 0f 28 5c 24 60            	movaps	0x60(%rsp), %xmm11
     2d6: 48 83 c4 78                  	addq	$0x78, %rsp
     2da: 5b                           	popq	%rbx
     2db: 5d                           	popq	%rbp
     2dc: 5f                           	popq	%rdi
     2dd: 5e                           	popq	%rsi
     2de: 41 5c                        	popq	%r12
     2e0: 41 5d                        	popq	%r13
     2e2: 41 5e                        	popq	%r14
     2e4: 41 5f                        	popq	%r15
     2e6: c3                           	retq
