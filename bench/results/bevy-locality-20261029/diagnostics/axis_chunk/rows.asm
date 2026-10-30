
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-locality-20261029/axis_chunk/wide.obj:	file format coff-x86-64

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
       c: 44 39 c2                     	cmpl	%r8d, %edx
       f: 0f 83 ab 01 00 00            	jae	0x1c0 <tide_rows_Shift_6+0x1c0>
      15: 48 8b bc 24 c0 00 00 00      	movq	0xc0(%rsp), %rdi
      1d: 48 8b 84 24 b0 00 00 00      	movq	0xb0(%rsp), %rax
      25: 4c 8b b4 24 a0 00 00 00      	movq	0xa0(%rsp), %r14
      2d: 4c 8b ac 24 98 00 00 00      	movq	0x98(%rsp), %r13
      35: 4c 8b 9c 24 90 00 00 00      	movq	0x90(%rsp), %r11
      3d: 4c 8b bc 24 88 00 00 00      	movq	0x88(%rsp), %r15
      45: 48 8b ac 24 80 00 00 00      	movq	0x80(%rsp), %rbp
      4d: 48 8b 74 24 78               	movq	0x78(%rsp), %rsi
      52: 4c 8b 64 24 70               	movq	0x70(%rsp), %r12
      57: 48 8b 5c 24 68               	movq	0x68(%rsp), %rbx
      5c: f3 0f 10 01                  	movss	(%rcx), %xmm0
      60: 89 d1                        	movl	%edx, %ecx
      62: 44 89 c2                     	movl	%r8d, %edx
      65: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x6d <tide_rows_Shift_6+0x6d>
      6d: 49 89 c8                     	movq	%rcx, %r8
      70: 47 0f b7 54 41 0a            	movzwl	0xa(%r9,%r8,2), %r10d
      76: 41 c1 e2 02                  	shll	$0x2, %r10d
      7a: f3 43 0f 10 54 15 00         	movss	(%r13,%r10), %xmm2
      81: f3 0f 59 d0                  	mulss	%xmm0, %xmm2
      85: f3 42 0f 58 54 15 00         	addss	(%rbp,%r10), %xmm2
      8c: f3 42 0f 10 1c 10            	movss	(%rax,%r10), %xmm3
      92: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
      96: 0f 28 e1                     	movaps	%xmm1, %xmm4
      99: f3 0f 5c e3                  	subss	%xmm3, %xmm4
      9d: f3 0f 59 e2                  	mulss	%xmm2, %xmm4
      a1: 0f 28 d0                     	movaps	%xmm0, %xmm2
      a4: f3 0f 59 d4                  	mulss	%xmm4, %xmm2
      a8: f3 42 0f 58 14 13            	addss	(%rbx,%r10), %xmm2
      ae: f3 42 0f 11 14 13            	movss	%xmm2, (%rbx,%r10)
      b4: f3 42 0f 11 64 15 00         	movss	%xmm4, (%rbp,%r10)
      bb: 49 ff c0                     	incq	%r8
      be: 4c 39 c2                     	cmpq	%r8, %rdx
      c1: 75 ad                        	jne	0x70 <tide_rows_Shift_6+0x70>
      c3: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0xca <tide_rows_Shift_6+0xca>
      ca: 49 89 c8                     	movq	%rcx, %r8
      cd: 48 8b 9c 24 b8 00 00 00      	movq	0xb8(%rsp), %rbx
      d5: eb 25                        	jmp	0xfc <tide_rows_Shift_6+0xfc>
      d7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
      e0: 46 8d 14 ad 00 00 00 00      	leal	(,%r13,4), %r10d
      e8: f3 43 0f 11 24 14            	movss	%xmm4, (%r12,%r10)
      ee: f3 43 0f 11 1c 17            	movss	%xmm3, (%r15,%r10)
      f4: 49 ff c0                     	incq	%r8
      f7: 4c 39 c2                     	cmpq	%r8, %rdx
      fa: 74 60                        	je	0x15c <tide_rows_Shift_6+0x15c>
      fc: 47 0f b7 6c 41 0a            	movzwl	0xa(%r9,%r8,2), %r13d
     102: 45 89 ea                     	movl	%r13d, %r10d
     105: 41 c1 e2 02                  	shll	$0x2, %r10d
     109: f3 43 0f 10 24 16            	movss	(%r14,%r10), %xmm4
     10f: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     113: f3 43 0f 58 24 17            	addss	(%r15,%r10), %xmm4
     119: f3 42 0f 10 2c 10            	movss	(%rax,%r10), %xmm5
     11f: f3 0f 59 e8                  	mulss	%xmm0, %xmm5
     123: 0f 28 d9                     	movaps	%xmm1, %xmm3
     126: f3 0f 5c dd                  	subss	%xmm5, %xmm3
     12a: f3 0f 59 dc                  	mulss	%xmm4, %xmm3
     12e: 0f 28 e0                     	movaps	%xmm0, %xmm4
     131: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     135: f3 43 0f 58 24 14            	addss	(%r12,%r10), %xmm4
     13b: f3 42 0f 10 2c 17            	movss	(%rdi,%r10), %xmm5
     141: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     144: 76 9a                        	jbe	0xe0 <tide_rows_Shift_6+0xe0>
     146: f3 0f 58 ed                  	addss	%xmm5, %xmm5
     14a: f3 0f 5c ec                  	subss	%xmm4, %xmm5
     14e: 0f 57 da                     	xorps	%xmm2, %xmm3
     151: f3 42 0f 59 1c ab            	mulss	(%rbx,%r13,4), %xmm3
     157: 0f 28 e5                     	movaps	%xmm5, %xmm4
     15a: eb 84                        	jmp	0xe0 <tide_rows_Shift_6+0xe0>
     15c: 4c 8b 94 24 a8 00 00 00      	movq	0xa8(%rsp), %r10
     164: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
     170: 45 0f b7 44 49 0a            	movzwl	0xa(%r9,%rcx,2), %r8d
     176: 41 c1 e0 02                  	shll	$0x2, %r8d
     17a: f3 43 0f 10 14 02            	movss	(%r10,%r8), %xmm2
     180: f3 0f 59 d0                  	mulss	%xmm0, %xmm2
     184: f3 43 0f 58 14 03            	addss	(%r11,%r8), %xmm2
     18a: f3 42 0f 10 1c 00            	movss	(%rax,%r8), %xmm3
     190: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     194: 0f 28 e1                     	movaps	%xmm1, %xmm4
     197: f3 0f 5c e3                  	subss	%xmm3, %xmm4
     19b: f3 0f 59 e2                  	mulss	%xmm2, %xmm4
     19f: 0f 28 d0                     	movaps	%xmm0, %xmm2
     1a2: f3 0f 59 d4                  	mulss	%xmm4, %xmm2
     1a6: f3 42 0f 58 14 06            	addss	(%rsi,%r8), %xmm2
     1ac: f3 42 0f 11 14 06            	movss	%xmm2, (%rsi,%r8)
     1b2: f3 43 0f 11 24 03            	movss	%xmm4, (%r11,%r8)
     1b8: 48 ff c1                     	incq	%rcx
     1bb: 48 39 ca                     	cmpq	%rcx, %rdx
     1be: 75 b0                        	jne	0x170 <tide_rows_Shift_6+0x170>
     1c0: 5b                           	popq	%rbx
     1c1: 5d                           	popq	%rbp
     1c2: 5f                           	popq	%rdi
     1c3: 5e                           	popq	%rsi
     1c4: 41 5c                        	popq	%r12
     1c6: 41 5d                        	popq	%r13
     1c8: 41 5e                        	popq	%r14
     1ca: 41 5f                        	popq	%r15
     1cc: c3                           	retq
