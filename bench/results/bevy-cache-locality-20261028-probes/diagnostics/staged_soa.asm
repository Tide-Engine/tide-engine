
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/staged_soa/wide.obj:	file format coff-x86-64

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
       c: b8 88 18 00 00               	movl	$0x1888, %eax           # imm = 0x1888
      11: e8 00 00 00 00               	callq	0x16 <tide_rows_Shift_6+0x16>
      16: 48 29 c4                     	subq	%rax, %rsp
      19: 41 29 d0                     	subl	%edx, %r8d
      1c: 0f 85 a3 00 00 00            	jne	0xc5 <tide_rows_Shift_6+0xc5>
      22: 48 8d 84 24 88 16 00 00      	leaq	0x1688(%rsp), %rax
      2a: 4c 8d a4 24 88 14 00 00      	leaq	0x1488(%rsp), %r12
      32: 48 8d 94 24 88 12 00 00      	leaq	0x1288(%rsp), %rdx
      3a: 4c 8d bc 24 88 10 00 00      	leaq	0x1088(%rsp), %r15
      42: 4c 8d 8c 24 88 0e 00 00      	leaq	0xe88(%rsp), %r9
      4a: 4c 8d 94 24 88 0c 00 00      	leaq	0xc88(%rsp), %r10
      52: 4c 8d 9c 24 88 0a 00 00      	leaq	0xa88(%rsp), %r11
      5a: 48 8d b4 24 88 08 00 00      	leaq	0x888(%rsp), %rsi
      62: 48 8d bc 24 88 06 00 00      	leaq	0x688(%rsp), %rdi
      6a: 48 8d 9c 24 88 04 00 00      	leaq	0x488(%rsp), %rbx
      72: 4c 8d b4 24 88 02 00 00      	leaq	0x288(%rsp), %r14
      7a: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
      7f: 4c 89 64 24 68               	movq	%r12, 0x68(%rsp)
      84: 48 89 54 24 60               	movq	%rdx, 0x60(%rsp)
      89: 4c 89 7c 24 58               	movq	%r15, 0x58(%rsp)
      8e: 4c 89 4c 24 50               	movq	%r9, 0x50(%rsp)
      93: 4c 89 54 24 48               	movq	%r10, 0x48(%rsp)
      98: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
      9d: 48 89 74 24 38               	movq	%rsi, 0x38(%rsp)
      a2: 48 89 7c 24 30               	movq	%rdi, 0x30(%rsp)
      a7: 48 89 5c 24 28               	movq	%rbx, 0x28(%rsp)
      ac: 4c 89 74 24 20               	movq	%r14, 0x20(%rsp)
      b1: 4c 8d 8c 24 88 00 00 00      	leaq	0x88(%rsp), %r9
      b9: 31 d2                        	xorl	%edx, %edx
      bb: e8 00 00 00 00               	callq	0xc0 <tide_rows_Shift_6+0xc0>
      c0: e9 3f 02 00 00               	jmp	0x304 <tide_rows_Shift_6+0x304>
      c5: 48 89 8c 24 80 00 00 00      	movq	%rcx, 0x80(%rsp)
      cd: 4c 8b 8c 24 50 19 00 00      	movq	0x1950(%rsp), %r9
      d5: 4c 8b 94 24 48 19 00 00      	movq	0x1948(%rsp), %r10
      dd: 4c 8b 9c 24 40 19 00 00      	movq	0x1940(%rsp), %r11
      e5: 48 8b 84 24 38 19 00 00      	movq	0x1938(%rsp), %rax
      ed: 48 8b 8c 24 30 19 00 00      	movq	0x1930(%rsp), %rcx
      f5: 48 8b 9c 24 18 19 00 00      	movq	0x1918(%rsp), %rbx
      fd: 4c 8b b4 24 10 19 00 00      	movq	0x1910(%rsp), %r14
     105: 4c 8b bc 24 08 19 00 00      	movq	0x1908(%rsp), %r15
     10d: 4c 8b a4 24 00 19 00 00      	movq	0x1900(%rsp), %r12
     115: 4c 8b ac 24 f8 18 00 00      	movq	0x18f8(%rsp), %r13
     11d: 44 89 44 24 7c               	movl	%r8d, 0x7c(%rsp)
     122: 44 89 c5                     	movl	%r8d, %ebp
     125: 89 d6                        	movl	%edx, %esi
     127: 31 d2                        	xorl	%edx, %edx
     129: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     130: 8d 3c 16                     	leal	(%rsi,%rdx), %edi
     133: 41 0f b7 7c 7d 00            	movzwl	(%r13,%rdi,2), %edi
     139: c1 e7 02                     	shll	$0x2, %edi
     13c: 45 8b 04 3c                  	movl	(%r12,%rdi), %r8d
     140: 44 89 84 94 88 00 00 00      	movl	%r8d, 0x88(%rsp,%rdx,4)
     148: 45 8b 04 3f                  	movl	(%r15,%rdi), %r8d
     14c: 44 89 84 94 88 02 00 00      	movl	%r8d, 0x288(%rsp,%rdx,4)
     154: 45 8b 04 3e                  	movl	(%r14,%rdi), %r8d
     158: 44 89 84 94 88 04 00 00      	movl	%r8d, 0x488(%rsp,%rdx,4)
     160: 44 8b 04 3b                  	movl	(%rbx,%rdi), %r8d
     164: 44 89 84 94 88 06 00 00      	movl	%r8d, 0x688(%rsp,%rdx,4)
     16c: 4c 8b 84 24 20 19 00 00      	movq	0x1920(%rsp), %r8
     174: 45 8b 04 38                  	movl	(%r8,%rdi), %r8d
     178: 44 89 84 94 88 08 00 00      	movl	%r8d, 0x888(%rsp,%rdx,4)
     180: 4c 8b 84 24 28 19 00 00      	movq	0x1928(%rsp), %r8
     188: 45 8b 04 38                  	movl	(%r8,%rdi), %r8d
     18c: 44 89 84 94 88 0a 00 00      	movl	%r8d, 0xa88(%rsp,%rdx,4)
     194: 44 8b 04 39                  	movl	(%rcx,%rdi), %r8d
     198: 44 89 84 94 88 0c 00 00      	movl	%r8d, 0xc88(%rsp,%rdx,4)
     1a0: 44 8b 04 38                  	movl	(%rax,%rdi), %r8d
     1a4: 44 89 84 94 88 0e 00 00      	movl	%r8d, 0xe88(%rsp,%rdx,4)
     1ac: 45 8b 04 3b                  	movl	(%r11,%rdi), %r8d
     1b0: 44 89 84 94 88 10 00 00      	movl	%r8d, 0x1088(%rsp,%rdx,4)
     1b8: 45 8b 04 3a                  	movl	(%r10,%rdi), %r8d
     1bc: 44 89 84 94 88 12 00 00      	movl	%r8d, 0x1288(%rsp,%rdx,4)
     1c4: 45 8b 04 39                  	movl	(%r9,%rdi), %r8d
     1c8: 44 89 84 94 88 14 00 00      	movl	%r8d, 0x1488(%rsp,%rdx,4)
     1d0: 4c 8b 84 24 58 19 00 00      	movq	0x1958(%rsp), %r8
     1d8: 45 8b 04 38                  	movl	(%r8,%rdi), %r8d
     1dc: 44 89 84 94 88 16 00 00      	movl	%r8d, 0x1688(%rsp,%rdx,4)
     1e4: 48 ff c2                     	incq	%rdx
     1e7: 48 39 d5                     	cmpq	%rdx, %rbp
     1ea: 0f 85 40 ff ff ff            	jne	0x130 <tide_rows_Shift_6+0x130>
     1f0: 48 8d 84 24 88 16 00 00      	leaq	0x1688(%rsp), %rax
     1f8: 48 8d 8c 24 88 14 00 00      	leaq	0x1488(%rsp), %rcx
     200: 48 8d 94 24 88 12 00 00      	leaq	0x1288(%rsp), %rdx
     208: 4c 8d 84 24 88 10 00 00      	leaq	0x1088(%rsp), %r8
     210: 4c 8d 8c 24 88 0e 00 00      	leaq	0xe88(%rsp), %r9
     218: 4c 8d 94 24 88 0c 00 00      	leaq	0xc88(%rsp), %r10
     220: 4c 8d 9c 24 88 0a 00 00      	leaq	0xa88(%rsp), %r11
     228: 48 8d bc 24 88 08 00 00      	leaq	0x888(%rsp), %rdi
     230: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     235: 48 89 4c 24 68               	movq	%rcx, 0x68(%rsp)
     23a: 48 89 54 24 60               	movq	%rdx, 0x60(%rsp)
     23f: 4c 89 44 24 58               	movq	%r8, 0x58(%rsp)
     244: 4c 89 4c 24 50               	movq	%r9, 0x50(%rsp)
     249: 4c 89 54 24 48               	movq	%r10, 0x48(%rsp)
     24e: 4c 89 5c 24 40               	movq	%r11, 0x40(%rsp)
     253: 48 89 7c 24 38               	movq	%rdi, 0x38(%rsp)
     258: 48 8d 84 24 88 06 00 00      	leaq	0x688(%rsp), %rax
     260: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     265: 48 8d 84 24 88 04 00 00      	leaq	0x488(%rsp), %rax
     26d: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
     272: 48 8d 84 24 88 02 00 00      	leaq	0x288(%rsp), %rax
     27a: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     27f: 31 ff                        	xorl	%edi, %edi
     281: 4c 8d 8c 24 88 00 00 00      	leaq	0x88(%rsp), %r9
     289: 48 8b 8c 24 80 00 00 00      	movq	0x80(%rsp), %rcx
     291: 31 d2                        	xorl	%edx, %edx
     293: 44 8b 44 24 7c               	movl	0x7c(%rsp), %r8d
     298: e8 00 00 00 00               	callq	0x29d <tide_rows_Shift_6+0x29d>
     29d: 48 8b 94 24 28 19 00 00      	movq	0x1928(%rsp), %rdx
     2a5: 4c 8b 84 24 20 19 00 00      	movq	0x1920(%rsp), %r8
     2ad: 0f 1f 00                     	nopl	(%rax)
     2b0: 8d 04 3e                     	leal	(%rsi,%rdi), %eax
     2b3: 41 0f b7 44 45 00            	movzwl	(%r13,%rax,2), %eax
     2b9: c1 e0 02                     	shll	$0x2, %eax
     2bc: 8b 8c bc 88 00 00 00         	movl	0x88(%rsp,%rdi,4), %ecx
     2c3: 41 89 0c 04                  	movl	%ecx, (%r12,%rax)
     2c7: 8b 8c bc 88 02 00 00         	movl	0x288(%rsp,%rdi,4), %ecx
     2ce: 41 89 0c 07                  	movl	%ecx, (%r15,%rax)
     2d2: 8b 8c bc 88 04 00 00         	movl	0x488(%rsp,%rdi,4), %ecx
     2d9: 41 89 0c 06                  	movl	%ecx, (%r14,%rax)
     2dd: 8b 8c bc 88 06 00 00         	movl	0x688(%rsp,%rdi,4), %ecx
     2e4: 89 0c 03                     	movl	%ecx, (%rbx,%rax)
     2e7: 8b 8c bc 88 08 00 00         	movl	0x888(%rsp,%rdi,4), %ecx
     2ee: 41 89 0c 00                  	movl	%ecx, (%r8,%rax)
     2f2: 8b 8c bc 88 0a 00 00         	movl	0xa88(%rsp,%rdi,4), %ecx
     2f9: 89 0c 02                     	movl	%ecx, (%rdx,%rax)
     2fc: 48 ff c7                     	incq	%rdi
     2ff: 48 39 fd                     	cmpq	%rdi, %rbp
     302: 75 ac                        	jne	0x2b0 <tide_rows_Shift_6+0x2b0>
     304: 48 81 c4 88 18 00 00         	addq	$0x1888, %rsp           # imm = 0x1888
     30b: 5b                           	popq	%rbx
     30c: 5d                           	popq	%rbp
     30d: 5f                           	popq	%rdi
     30e: 5e                           	popq	%rsi
     30f: 41 5c                        	popq	%r12
     311: 41 5d                        	popq	%r13
     313: 41 5e                        	popq	%r14
     315: 41 5f                        	popq	%r15
     317: c3                           	retq
