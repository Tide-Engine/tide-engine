
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/cached/wide.obj:	file format coff-x86-64

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
       c: 48 83 ec 68                  	subq	$0x68, %rsp
      10: 44 0f 29 5c 24 50            	movaps	%xmm11, 0x50(%rsp)
      16: 44 0f 29 54 24 40            	movaps	%xmm10, 0x40(%rsp)
      1c: 44 0f 29 4c 24 30            	movaps	%xmm9, 0x30(%rsp)
      22: 44 0f 29 44 24 20            	movaps	%xmm8, 0x20(%rsp)
      28: 0f 29 7c 24 10               	movaps	%xmm7, 0x10(%rsp)
      2d: 0f 29 34 24                  	movaps	%xmm6, (%rsp)
      31: 44 39 c2                     	cmpl	%r8d, %edx
      34: 0f 83 7a 01 00 00            	jae	0x1b4 <tide_rows_Shift_6+0x1b4>
      3a: 4c 8b 9c 24 18 01 00 00      	movq	0x118(%rsp), %r11
      42: 48 8b b4 24 10 01 00 00      	movq	0x110(%rsp), %rsi
      4a: 48 8b bc 24 08 01 00 00      	movq	0x108(%rsp), %rdi
      52: 48 8b 9c 24 00 01 00 00      	movq	0x100(%rsp), %rbx
      5a: 4c 8b b4 24 f8 00 00 00      	movq	0xf8(%rsp), %r14
      62: 4c 8b bc 24 f0 00 00 00      	movq	0xf0(%rsp), %r15
      6a: 4c 8b a4 24 e8 00 00 00      	movq	0xe8(%rsp), %r12
      72: 4c 8b ac 24 e0 00 00 00      	movq	0xe0(%rsp), %r13
      7a: 48 8b ac 24 d8 00 00 00      	movq	0xd8(%rsp), %rbp
      82: 48 8b 84 24 d0 00 00 00      	movq	0xd0(%rsp), %rax
      8a: f3 0f 10 01                  	movss	(%rcx), %xmm0
      8e: 89 d2                        	movl	%edx, %edx
      90: 45 89 c0                     	movl	%r8d, %r8d
      93: f3 0f 10 0d 00 00 00 00      	movss	(%rip), %xmm1           # 0x9b <tide_rows_Shift_6+0x9b>
      9b: 0f 28 15 00 00 00 00         	movaps	(%rip), %xmm2           # 0xa2 <tide_rows_Shift_6+0xa2>
      a2: e9 aa 00 00 00               	jmp	0x151 <tide_rows_Shift_6+0x151>
      a7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
      b0: 0f 28 f4                     	movaps	%xmm4, %xmm6
      b3: f3 0f 10 3c 0b               	movss	(%rbx,%rcx), %xmm7
      b8: f3 0f 59 f8                  	mulss	%xmm0, %xmm7
      bc: f3 44 0f 10 04 0e            	movss	(%rsi,%rcx), %xmm8
      c2: f3 44 0f 59 c0               	mulss	%xmm0, %xmm8
      c7: f3 41 0f 58 3c 0c            	addss	(%r12,%rcx), %xmm7
      cd: f3 45 0f 58 04 0e            	addss	(%r14,%rcx), %xmm8
      d3: f3 41 0f 59 f9               	mulss	%xmm9, %xmm7
      d8: f3 45 0f 59 c1               	mulss	%xmm9, %xmm8
      dd: 44 0f 28 c8                  	movaps	%xmm0, %xmm9
      e1: f3 44 0f 59 cf               	mulss	%xmm7, %xmm9
      e6: 44 0f 28 d0                  	movaps	%xmm0, %xmm10
      ea: f3 44 0f 58 0c 08            	addss	(%rax,%rcx), %xmm9
      f0: f3 45 0f 59 d0               	mulss	%xmm8, %xmm10
      f5: f3 45 0f 58 54 0d 00         	addss	(%r13,%rcx), %xmm10
      fc: 44 0f 28 db                  	movaps	%xmm3, %xmm11
     100: 44 0f 57 da                  	xorps	%xmm2, %xmm11
     104: 4c 8b 94 24 20 01 00 00      	movq	0x120(%rsp), %r10
     10c: f3 45 0f 59 1c 0a            	mulss	(%r10,%rcx), %xmm11
     112: f3 0f c2 e5 01               	cmpltss	%xmm5, %xmm4
     117: 0f 28 ec                     	movaps	%xmm4, %xmm5
     11a: 0f 55 eb                     	andnps	%xmm3, %xmm5
     11d: 41 0f 54 e3                  	andps	%xmm11, %xmm4
     121: 0f 56 e5                     	orps	%xmm5, %xmm4
     124: f3 44 0f 11 0c 08            	movss	%xmm9, (%rax,%rcx)
     12a: f3 0f 11 74 0d 00            	movss	%xmm6, (%rbp,%rcx)
     130: f3 45 0f 11 54 0d 00         	movss	%xmm10, (%r13,%rcx)
     137: f3 41 0f 11 3c 0c            	movss	%xmm7, (%r12,%rcx)
     13d: f3 41 0f 11 24 0f            	movss	%xmm4, (%r15,%rcx)
     143: f3 45 0f 11 04 0e            	movss	%xmm8, (%r14,%rcx)
     149: 48 ff c2                     	incq	%rdx
     14c: 49 39 d0                     	cmpq	%rdx, %r8
     14f: 74 63                        	je	0x1b4 <tide_rows_Shift_6+0x1b4>
     151: 41 0f b7 0c 51               	movzwl	(%r9,%rdx,2), %ecx
     156: c1 e1 02                     	shll	$0x2, %ecx
     159: f3 0f 10 1c 0f               	movss	(%rdi,%rcx), %xmm3
     15e: f3 0f 59 d8                  	mulss	%xmm0, %xmm3
     162: f3 41 0f 58 1c 0f            	addss	(%r15,%rcx), %xmm3
     168: 4c 8b 94 24 28 01 00 00      	movq	0x128(%rsp), %r10
     170: f3 41 0f 10 2c 0a            	movss	(%r10,%rcx), %xmm5
     176: f3 41 0f 10 24 0b            	movss	(%r11,%rcx), %xmm4
     17c: f3 0f 59 e0                  	mulss	%xmm0, %xmm4
     180: 44 0f 28 c9                  	movaps	%xmm1, %xmm9
     184: f3 44 0f 5c cc               	subss	%xmm4, %xmm9
     189: f3 41 0f 59 d9               	mulss	%xmm9, %xmm3
     18e: 0f 28 e0                     	movaps	%xmm0, %xmm4
     191: f3 0f 59 e3                  	mulss	%xmm3, %xmm4
     195: f3 0f 58 64 0d 00            	addss	(%rbp,%rcx), %xmm4
     19b: 0f 2e ec                     	ucomiss	%xmm4, %xmm5
     19e: 0f 86 0c ff ff ff            	jbe	0xb0 <tide_rows_Shift_6+0xb0>
     1a4: 0f 28 f5                     	movaps	%xmm5, %xmm6
     1a7: f3 0f 58 f5                  	addss	%xmm5, %xmm6
     1ab: f3 0f 5c f4                  	subss	%xmm4, %xmm6
     1af: e9 ff fe ff ff               	jmp	0xb3 <tide_rows_Shift_6+0xb3>
     1b4: 0f 28 34 24                  	movaps	(%rsp), %xmm6
     1b8: 0f 28 7c 24 10               	movaps	0x10(%rsp), %xmm7
     1bd: 44 0f 28 44 24 20            	movaps	0x20(%rsp), %xmm8
     1c3: 44 0f 28 4c 24 30            	movaps	0x30(%rsp), %xmm9
     1c9: 44 0f 28 54 24 40            	movaps	0x40(%rsp), %xmm10
     1cf: 44 0f 28 5c 24 50            	movaps	0x50(%rsp), %xmm11
     1d5: 48 83 c4 68                  	addq	$0x68, %rsp
     1d9: 5b                           	popq	%rbx
     1da: 5d                           	popq	%rbp
     1db: 5f                           	popq	%rdi
     1dc: 5e                           	popq	%rsi
     1dd: 41 5c                        	popq	%r12
     1df: 41 5d                        	popq	%r13
     1e1: 41 5e                        	popq	%r14
     1e3: 41 5f                        	popq	%r15
     1e5: c3                           	retq
