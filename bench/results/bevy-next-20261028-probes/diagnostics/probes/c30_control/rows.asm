
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/c30_control/wide.obj:	file format coff-x86-64

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
      35: 48 89 4c 24 18               	movq	%rcx, 0x18(%rsp)
      3a: 48 8b b4 24 48 01 00 00      	movq	0x148(%rsp), %rsi
      42: 48 8b ac 24 20 01 00 00      	movq	0x120(%rsp), %rbp
      4a: 4c 8b b4 24 18 01 00 00      	movq	0x118(%rsp), %r14
      52: 4c 8b bc 24 10 01 00 00      	movq	0x110(%rsp), %r15
      5a: 41 8b 01                     	movl	(%r9), %eax
      5d: 41 8b 49 04                  	movl	0x4(%r9), %ecx
      61: 83 c0 3f                     	addl	$0x3f, %eax
      64: c1 e8 06                     	shrl	$0x6, %eax
      67: 89 44 24 10                  	movl	%eax, 0x10(%rsp)
      6b: 89 4c 24 14                  	movl	%ecx, 0x14(%rsp)
      6f: 39 c1                        	cmpl	%eax, %ecx
      71: 0f 93 c0                     	setae	%al
      74: 44 39 c2                     	cmpl	%r8d, %edx
      77: 0f 93 c1                     	setae	%cl
      7a: 08 c1                        	orb	%al, %cl
      7c: 4c 8b ac 24 08 01 00 00      	movq	0x108(%rsp), %r13
      84: 4c 8b a4 24 00 01 00 00      	movq	0x100(%rsp), %r12
      8c: 48 8b 8c 24 f8 00 00 00      	movq	0xf8(%rsp), %rcx
      94: 48 8b 9c 24 f0 00 00 00      	movq	0xf0(%rsp), %rbx
      9c: 89 54 24 08                  	movl	%edx, 0x8(%rsp)
      a0: 41 89 d3                     	movl	%edx, %r11d
      a3: 44 89 44 24 0c               	movl	%r8d, 0xc(%rsp)
      a8: 44 89 c0                     	movl	%r8d, %eax
      ab: 0f 85 96 00 00 00            	jne	0x147 <tide_rows_Shift_6+0x147>
      b1: 4d 8d 53 04                  	leaq	0x4(%r11), %r10
      b5: 4c 39 d0                     	cmpq	%r10, %rax
      b8: 4c 0f 42 d0                  	cmovbq	%rax, %r10
      bc: 4c 89 da                     	movq	%r11, %rdx
      bf: 90                           	nop
      c0: 48 89 f7                     	movq	%rsi, %rdi
      c3: 41 0f b7 74 51 0a            	movzwl	0xa(%r9,%rdx,2), %esi
      c9: c1 e6 02                     	shll	$0x2, %esi
      cc: 0f 18 0c 33                  	prefetcht0	(%rbx,%rsi)
      d0: 0f 18 0c 31                  	prefetcht0	(%rcx,%rsi)
      d4: 41 0f 18 0c 34               	prefetcht0	(%r12,%rsi)
      d9: 41 0f 18 4c 35 00            	prefetcht0	(%r13,%rsi)
      df: 41 0f 18 0c 37               	prefetcht0	(%r15,%rsi)
      e4: 41 0f 18 0c 36               	prefetcht0	(%r14,%rsi)
      e9: 0f 18 4c 35 00               	prefetcht0	(%rbp,%rsi)
      ee: 4c 8b 84 24 28 01 00 00      	movq	0x128(%rsp), %r8
      f6: 41 0f 18 0c 30               	prefetcht0	(%r8,%rsi)
      fb: 4c 8b 84 24 30 01 00 00      	movq	0x130(%rsp), %r8
     103: 41 0f 18 0c 30               	prefetcht0	(%r8,%rsi)
     108: 4d 89 f8                     	movq	%r15, %r8
     10b: 4d 89 f7                     	movq	%r14, %r15
     10e: 49 89 ee                     	movq	%rbp, %r14
     111: 48 8b ac 24 38 01 00 00      	movq	0x138(%rsp), %rbp
     119: 0f 18 4c 35 00               	prefetcht0	(%rbp,%rsi)
     11e: 4c 89 f5                     	movq	%r14, %rbp
     121: 4d 89 fe                     	movq	%r15, %r14
     124: 4d 89 c7                     	movq	%r8, %r15
     127: 4c 8b 84 24 40 01 00 00      	movq	0x140(%rsp), %r8
     12f: 41 0f 18 0c 30               	prefetcht0	(%r8,%rsi)
     134: 0f 18 0c 37                  	prefetcht0	(%rdi,%rsi)
     138: 48 89 fe                     	movq	%rdi, %rsi
     13b: 48 ff c2                     	incq	%rdx
     13e: 4c 39 d2                     	cmpq	%r10, %rdx
     141: 0f 82 79 ff ff ff            	jb	0xc0 <tide_rows_Shift_6+0xc0>
     147: 8b 54 24 08                  	movl	0x8(%rsp), %edx
     14b: 3b 54 24 0c                  	cmpl	0xc(%rsp), %edx
     14f: 48 8b bc 24 38 01 00 00      	movq	0x138(%rsp), %rdi
     157: 49 89 e8                     	movq	%rbp, %r8
     15a: 8b 6c 24 10                  	movl	0x10(%rsp), %ebp
     15e: 0f 83 b1 01 00 00            	jae	0x315 <tide_rows_Shift_6+0x315>
     164: 48 8b 54 24 18               	movq	0x18(%rsp), %rdx
     169: f3 0f 10 02                  	movss	(%rdx), %xmm0
     16d: 4c 29 d8                     	subq	%r11, %rax
     170: 49 83 c3 04                  	addq	$0x4, %r11
     174: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x17c <tide_rows_Shift_6+0x17c>
     17c: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0x183 <tide_rows_Shift_6+0x183>
     183: eb 52                        	jmp	0x1d7 <tide_rows_Shift_6+0x1d7>
     185: 66 66 2e 0f 1f 84 00 00 00 00 00     	nopw	%cs:(%rax,%rax)
     190: 44 0f 28 df                  	movaps	%xmm7, %xmm11
     194: f3 0f c2 fc 01               	cmpltss	%xmm4, %xmm7
     199: 0f 28 e7                     	movaps	%xmm7, %xmm4
     19c: 0f 55 e5                     	andnps	%xmm5, %xmm4
     19f: 41 0f 54 fa                  	andps	%xmm10, %xmm7
     1a3: 0f 56 fc                     	orps	%xmm4, %xmm7
     1a6: f3 46 0f 11 04 13            	movss	%xmm8, (%rbx,%r10)
     1ac: f3 46 0f 11 1c 11            	movss	%xmm11, (%rcx,%r10)
     1b2: f3 47 0f 11 0c 14            	movss	%xmm9, (%r12,%r10)
     1b8: f3 43 0f 11 5c 15 00         	movss	%xmm3, (%r13,%r10)
     1bf: f3 43 0f 11 3c 17            	movss	%xmm7, (%r15,%r10)
     1c5: f3 43 0f 11 34 16            	movss	%xmm6, (%r14,%r10)
     1cb: 49 ff c3                     	incq	%r11
     1ce: 48 ff c8                     	decq	%rax
     1d1: 0f 84 3e 01 00 00            	je	0x315 <tide_rows_Shift_6+0x315>
     1d7: 39 6c 24 14                  	cmpl	%ebp, 0x14(%rsp)
     1db: 0f 93 c2                     	setae	%dl
     1de: 48 83 f8 05                  	cmpq	$0x5, %rax
     1e2: 41 0f 92 c2                  	setb	%r10b
     1e6: 41 08 d2                     	orb	%dl, %r10b
     1e9: 75 5d                        	jne	0x248 <tide_rows_Shift_6+0x248>
     1eb: 44 89 da                     	movl	%r11d, %edx
     1ee: 41 0f b7 54 51 0a            	movzwl	0xa(%r9,%rdx,2), %edx
     1f4: c1 e2 02                     	shll	$0x2, %edx
     1f7: 0f 18 0c 13                  	prefetcht0	(%rbx,%rdx)
     1fb: 0f 18 0c 11                  	prefetcht0	(%rcx,%rdx)
     1ff: 41 0f 18 0c 14               	prefetcht0	(%r12,%rdx)
     204: 41 0f 18 4c 15 00            	prefetcht0	(%r13,%rdx)
     20a: 41 0f 18 0c 17               	prefetcht0	(%r15,%rdx)
     20f: 41 0f 18 0c 16               	prefetcht0	(%r14,%rdx)
     214: 41 0f 18 0c 10               	prefetcht0	(%r8,%rdx)
     219: 4c 8b 94 24 28 01 00 00      	movq	0x128(%rsp), %r10
     221: 41 0f 18 0c 12               	prefetcht0	(%r10,%rdx)
     226: 4c 8b 94 24 30 01 00 00      	movq	0x130(%rsp), %r10
     22e: 41 0f 18 0c 12               	prefetcht0	(%r10,%rdx)
     233: 0f 18 0c 17                  	prefetcht0	(%rdi,%rdx)
     237: 4c 8b 94 24 40 01 00 00      	movq	0x140(%rsp), %r10
     23f: 41 0f 18 0c 12               	prefetcht0	(%r10,%rdx)
     244: 0f 18 0c 16                  	prefetcht0	(%rsi,%rdx)
     248: 47 0f b7 54 59 02            	movzwl	0x2(%r9,%r11,2), %r10d
     24e: 41 c1 e2 02                  	shll	$0x2, %r10d
     252: f3 42 0f 10 24 16            	movss	(%rsi,%r10), %xmm4
     258: f3 43 0f 10 1c 10            	movss	(%r8,%r10), %xmm3
     25e: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     262: 48 8b 94 24 28 01 00 00      	movq	0x128(%rsp), %rdx
     26a: f3 42 0f 10 2c 12            	movss	(%rdx,%r10), %xmm5
     270: f3 0f 59 e8                  	mulss	%xmm0, %xmm5
     274: 48 8b 94 24 30 01 00 00      	movq	0x130(%rsp), %rdx
     27c: f3 42 0f 10 3c 12            	movss	(%rdx,%r10), %xmm7
     282: f3 43 0f 58 5c 15 00         	addss	(%r13,%r10), %xmm3
     289: f3 43 0f 58 2c 17            	addss	(%r15,%r10), %xmm5
     28f: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
     293: f3 43 0f 58 3c 16            	addss	(%r14,%r10), %xmm7
     299: f3 46 0f 10 04 17            	movss	(%rdi,%r10), %xmm8
     29f: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
     2a4: 0f 28 f1                     	movaps	%xmm1, %xmm6
     2a7: f3 41 0f 5c f0               	subss	%xmm8, %xmm6
     2ac: f3 0f 59 de                  	mulss	%xmm6, %xmm3
     2b0: f3 0f 59 ee                  	mulss	%xmm6, %xmm5
     2b4: f3 0f 59 f7                  	mulss	%xmm7, %xmm6
     2b8: 44 0f 28 c0                  	movaps	%xmm0, %xmm8
     2bc: f3 44 0f 59 c3               	mulss	%xmm3, %xmm8
     2c1: 0f 28 f8                     	movaps	%xmm0, %xmm7
     2c4: f3 0f 59 fd                  	mulss	%xmm5, %xmm7
     2c8: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
     2cc: f3 44 0f 59 ce               	mulss	%xmm6, %xmm9
     2d1: f3 46 0f 58 04 13            	addss	(%rbx,%r10), %xmm8
     2d7: f3 42 0f 58 3c 11            	addss	(%rcx,%r10), %xmm7
     2dd: f3 47 0f 58 0c 14            	addss	(%r12,%r10), %xmm9
     2e3: 0f 2e e7                     	ucomiss	%xmm7, %xmm4
     2e6: 44 0f 28 d5                  	movaps	%xmm5, %xmm10
     2ea: 44 0f 57 d2                  	xorps	%xmm2, %xmm10
     2ee: 48 8b 94 24 40 01 00 00      	movq	0x140(%rsp), %rdx
     2f6: f3 46 0f 59 14 12            	mulss	(%rdx,%r10), %xmm10
     2fc: 0f 86 8e fe ff ff            	jbe	0x190 <tide_rows_Shift_6+0x190>
     302: 44 0f 28 dc                  	movaps	%xmm4, %xmm11
     306: f3 44 0f 58 dc               	addss	%xmm4, %xmm11
     30b: f3 44 0f 5c df               	subss	%xmm7, %xmm11
     310: e9 7f fe ff ff               	jmp	0x194 <tide_rows_Shift_6+0x194>
     315: 0f 28 74 24 20               	movaps	0x20(%rsp), %xmm6
     31a: 0f 28 7c 24 30               	movaps	0x30(%rsp), %xmm7
     31f: 44 0f 28 44 24 40            	movaps	0x40(%rsp), %xmm8
     325: 44 0f 28 4c 24 50            	movaps	0x50(%rsp), %xmm9
     32b: 44 0f 28 54 24 60            	movaps	0x60(%rsp), %xmm10
     331: 44 0f 28 5c 24 70            	movaps	0x70(%rsp), %xmm11
     337: 48 81 c4 88 00 00 00         	addq	$0x88, %rsp
     33e: 5b                           	popq	%rbx
     33f: 5d                           	popq	%rbp
     340: 5f                           	popq	%rdi
     341: 5e                           	popq	%rsi
     342: 41 5c                        	popq	%r12
     344: 41 5d                        	popq	%r13
     346: 41 5e                        	popq	%r14
     348: 41 5f                        	popq	%r15
     34a: c3                           	retq
