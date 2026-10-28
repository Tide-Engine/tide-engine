
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/prefetch_all/wide.obj:	file format coff-x86-64

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
      35: 0f 83 f9 01 00 00            	jae	0x234 <tide_rows_Shift_6+0x234>
      3b: 48 89 4c 24 08               	movq	%rcx, 0x8(%rsp)
      40: 4c 8b 94 24 38 01 00 00      	movq	0x138(%rsp), %r10
      48: 48 8b 9c 24 18 01 00 00      	movq	0x118(%rsp), %rbx
      50: 48 8b bc 24 10 01 00 00      	movq	0x110(%rsp), %rdi
      58: 4c 8b b4 24 08 01 00 00      	movq	0x108(%rsp), %r14
      60: 4c 8b bc 24 00 01 00 00      	movq	0x100(%rsp), %r15
      68: 4c 8b a4 24 f8 00 00 00      	movq	0xf8(%rsp), %r12
      70: 4c 8b ac 24 f0 00 00 00      	movq	0xf0(%rsp), %r13
      78: 48 8b ac 24 e8 00 00 00      	movq	0xe8(%rsp), %rbp
      80: 48 8b 84 24 e0 00 00 00      	movq	0xe0(%rsp), %rax
      88: 89 d2                        	movl	%edx, %edx
      8a: 45 89 c0                     	movl	%r8d, %r8d
      8d: 48 89 d1                     	movq	%rdx, %rcx
      90: 4d 89 d3                     	movq	%r10, %r11
      93: 45 0f b7 14 49               	movzwl	(%r9,%rcx,2), %r10d
      98: 41 c1 e2 02                  	shll	$0x2, %r10d
      9c: 42 0f 18 0c 10               	prefetcht0	(%rax,%r10)
      a1: 42 0f 18 4c 15 00            	prefetcht0	(%rbp,%r10)
      a7: 43 0f 18 4c 15 00            	prefetcht0	(%r13,%r10)
      ad: 43 0f 18 0c 14               	prefetcht0	(%r12,%r10)
      b2: 43 0f 18 0c 17               	prefetcht0	(%r15,%r10)
      b7: 43 0f 18 0c 16               	prefetcht0	(%r14,%r10)
      bc: 42 0f 18 0c 17               	prefetcht0	(%rdi,%r10)
      c1: 42 0f 18 0c 13               	prefetcht0	(%rbx,%r10)
      c6: 48 8b b4 24 20 01 00 00      	movq	0x120(%rsp), %rsi
      ce: 42 0f 18 0c 16               	prefetcht0	(%rsi,%r10)
      d3: 48 8b b4 24 28 01 00 00      	movq	0x128(%rsp), %rsi
      db: 42 0f 18 0c 16               	prefetcht0	(%rsi,%r10)
      e0: 48 8b b4 24 30 01 00 00      	movq	0x130(%rsp), %rsi
      e8: 42 0f 18 0c 16               	prefetcht0	(%rsi,%r10)
      ed: 43 0f 18 0c 13               	prefetcht0	(%r11,%r10)
      f2: 4d 89 da                     	movq	%r11, %r10
      f5: 48 ff c1                     	incq	%rcx
      f8: 49 39 c8                     	cmpq	%rcx, %r8
      fb: 75 93                        	jne	0x90 <tide_rows_Shift_6+0x90>
      fd: 48 8b 4c 24 08               	movq	0x8(%rsp), %rcx
     102: f3 0f 10 01                  	movss	(%rcx), %xmm0
     106: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x10e <tide_rows_Shift_6+0x10e>
     10e: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x115 <tide_rows_Shift_6+0x115>
     115: 48 8b b4 24 28 01 00 00      	movq	0x128(%rsp), %rsi
     11d: 48 8b bc 24 20 01 00 00      	movq	0x120(%rsp), %rdi
     125: e9 b0 00 00 00               	jmp	0x1da <tide_rows_Shift_6+0x1da>
     12a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     130: 0f 28 f4                     	movaps	%xmm4, %xmm6
     133: 4c 8b 9c 24 10 01 00 00      	movq	0x110(%rsp), %r11
     13b: f3 41 0f 10 3c 0b            	movss	(%r11,%rcx), %xmm7
     141: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     145: f3 44 0f 10 04 0f            	movss	(%rdi,%rcx), %xmm8
     14b: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     150: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
     156: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
     15c: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
     161: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
     166: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     16a: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
     16f: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
     173: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
     179: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
     17e: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
     185: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     189: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     18d: 4c 8b 9c 24 30 01 00 00      	movq	0x130(%rsp), %r11
     195: f3 45 0f 59 1c 0b            	mulss	(%r11,%rcx), %xmm11
     19b: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     1a0: 0f 28 ec                     	movaps	%xmm4, %xmm5
     1a3: 0f 55 eb                     	andnps	%xmm3, %xmm5
     1a6: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     1aa: 0f 56 e5                     	orps	%xmm5, %xmm4
     1ad: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     1b3: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     1b9: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     1c0: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     1c6: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     1cc: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     1d2: 48 ff c2                     	incq	%rdx
     1d5: 49 39 d0                     	cmpq	%rdx, %r8
     1d8: 74 5a                        	je	0x234 <tide_rows_Shift_6+0x234>
     1da: 41 0f b7 0c 51               	movzwl	(%r9,%rdx,2), %ecx
     1df: c1 e1 02                     	shll	$0x2, %ecx
     1e2: f3 0f 10 1c 0b               	movss	(%rbx,%rcx), %xmm3
     1e7: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     1eb: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     1f1: f3 41 0f 10 2c 0a            	movss	(%r10,%rcx), %xmm5
     1f7: f3 0f 10 24 0e               	movss	(%rsi,%rcx), %xmm4
     1fc: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     200: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     204: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     209: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     20e: 0f 28 e0                     	movaps	%xmm0, %xmm4
     211: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     215: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     21b: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     21e: 0f 86 0c ff ff ff            	jbe	0x130 <tide_rows_Shift_6+0x130>
     224: 0f 28 f5                     	movaps	%xmm5, %xmm6
     227: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     22b: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     22f: e9 ff fe ff ff               	jmp	0x133 <tide_rows_Shift_6+0x133>
     234: 0f 28 74 24 10               	movaps	0x10(%rsp), %xmm6
     239: 0f 28 7c 24 20               	movaps	0x20(%rsp), %xmm7
     23e: 44 0f 28 44 24 30            	movaps	0x30(%rsp), %xmm8
     244: 44 0f 28 4c 24 40            	movaps	0x40(%rsp), %xmm9
     24a: 44 0f 28 54 24 50            	movaps	0x50(%rsp), %xmm10
     250: 44 0f 28 5c 24 60            	movaps	0x60(%rsp), %xmm11
     256: 48 83 c4 78                  	addq	$0x78, %rsp
     25a: 5b                           	popq	%rbx
     25b: 5d                           	popq	%rbp
     25c: 5f                           	popq	%rdi
     25d: 5e                           	popq	%rsi
     25e: 41 5c                        	popq	%r12
     260: 41 5d                        	popq	%r13
     262: 41 5e                        	popq	%r14
     264: 41 5f                        	popq	%r15
     266: c3                           	retq
