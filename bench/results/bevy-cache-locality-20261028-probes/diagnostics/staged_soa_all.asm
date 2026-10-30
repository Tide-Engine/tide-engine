
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/staged_soa_all/wide.obj:	file format coff-x86-64

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
       c: b8 78 18 00 00               	movl	$0x1878, %eax           # imm = 0x1878
      11: e8 00 00 00 00               	callq	0x16 <tide_rows_Shift_6+0x16>
      16: 48 29 c4                     	subq	%rax, %rsp
      19: 41 29 d0                     	subl	%edx, %r8d
      1c: 0f 85 a0 00 00 00            	jne	0xc2 <tide_rows_Shift_6+0xc2>
      22: 48 8d 84 24 78 16 00 00      	leaq	0x1678(%rsp), %rax
      2a: 48 8d 94 24 78 14 00 00      	leaq	0x1478(%rsp), %rdx
      32: 4c 8d 8c 24 78 12 00 00      	leaq	0x1278(%rsp), %r9
      3a: 4c 8d 94 24 78 10 00 00      	leaq	0x1078(%rsp), %r10
      42: 4c 8d 9c 24 78 0e 00 00      	leaq	0xe78(%rsp), %r11
      4a: 48 8d b4 24 78 0c 00 00      	leaq	0xc78(%rsp), %rsi
      52: 48 8d bc 24 78 0a 00 00      	leaq	0xa78(%rsp), %rdi
      5a: 48 8d 9c 24 78 08 00 00      	leaq	0x878(%rsp), %rbx
      62: 4c 8d b4 24 78 06 00 00      	leaq	0x678(%rsp), %r14
      6a: 4c 8d bc 24 78 04 00 00      	leaq	0x478(%rsp), %r15
      72: 4c 8d a4 24 78 02 00 00      	leaq	0x278(%rsp), %r12
      7a: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
      7f: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
      84: 4c 89 4c 24 60               	movq	%r9, 0x60(%rsp)
      89: 4c 89 54 24 58               	movq	%r10, 0x58(%rsp)
      8e: 4c 89 5c 24 50               	movq	%r11, 0x50(%rsp)
      93: 48 89 74 24 48               	movq	%rsi, 0x48(%rsp)
      98: 48 89 7c 24 40               	movq	%rdi, 0x40(%rsp)
      9d: 48 89 5c 24 38               	movq	%rbx, 0x38(%rsp)
      a2: 4c 89 74 24 30               	movq	%r14, 0x30(%rsp)
      a7: 4c 89 7c 24 28               	movq	%r15, 0x28(%rsp)
      ac: 4c 89 64 24 20               	movq	%r12, 0x20(%rsp)
      b1: 4c 8d 4c 24 78               	leaq	0x78(%rsp), %r9
      b6: 31 d2                        	xorl	%edx, %edx
      b8: e8 00 00 00 00               	callq	0xbd <tide_rows_Shift_6+0xbd>
      bd: e9 af 02 00 00               	jmp	0x371 <tide_rows_Shift_6+0x371>
      c2: 4c 8b 94 24 48 19 00 00      	movq	0x1948(%rsp), %r10
      ca: 4c 8b 9c 24 40 19 00 00      	movq	0x1940(%rsp), %r11
      d2: 48 8b ac 24 38 19 00 00      	movq	0x1938(%rsp), %rbp
      da: 48 8b b4 24 10 19 00 00      	movq	0x1910(%rsp), %rsi
      e2: 48 8b bc 24 08 19 00 00      	movq	0x1908(%rsp), %rdi
      ea: 48 8b 9c 24 00 19 00 00      	movq	0x1900(%rsp), %rbx
      f2: 4c 8b b4 24 f8 18 00 00      	movq	0x18f8(%rsp), %r14
      fa: 4c 8b bc 24 f0 18 00 00      	movq	0x18f0(%rsp), %r15
     102: 45 89 c4                     	movl	%r8d, %r12d
     105: 41 89 d5                     	movl	%edx, %r13d
     108: 31 c0                        	xorl	%eax, %eax
     10a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     110: 42 8d 14 28                  	leal	(%rax,%r13), %edx
     114: 4c 8b 8c 24 e8 18 00 00      	movq	0x18e8(%rsp), %r9
     11c: 41 0f b7 14 51               	movzwl	(%r9,%rdx,2), %edx
     121: c1 e2 02                     	shll	$0x2, %edx
     124: 45 8b 0c 17                  	movl	(%r15,%rdx), %r9d
     128: 44 89 4c 84 78               	movl	%r9d, 0x78(%rsp,%rax,4)
     12d: 45 8b 0c 16                  	movl	(%r14,%rdx), %r9d
     131: 44 89 8c 84 78 02 00 00      	movl	%r9d, 0x278(%rsp,%rax,4)
     139: 44 8b 0c 13                  	movl	(%rbx,%rdx), %r9d
     13d: 44 89 8c 84 78 04 00 00      	movl	%r9d, 0x478(%rsp,%rax,4)
     145: 44 8b 0c 17                  	movl	(%rdi,%rdx), %r9d
     149: 44 89 8c 84 78 06 00 00      	movl	%r9d, 0x678(%rsp,%rax,4)
     151: 44 8b 0c 16                  	movl	(%rsi,%rdx), %r9d
     155: 44 89 8c 84 78 08 00 00      	movl	%r9d, 0x878(%rsp,%rax,4)
     15d: 4c 8b 8c 24 18 19 00 00      	movq	0x1918(%rsp), %r9
     165: 45 8b 0c 11                  	movl	(%r9,%rdx), %r9d
     169: 44 89 8c 84 78 0a 00 00      	movl	%r9d, 0xa78(%rsp,%rax,4)
     171: 4c 8b 8c 24 20 19 00 00      	movq	0x1920(%rsp), %r9
     179: 45 8b 0c 11                  	movl	(%r9,%rdx), %r9d
     17d: 44 89 8c 84 78 0c 00 00      	movl	%r9d, 0xc78(%rsp,%rax,4)
     185: 4c 8b 8c 24 28 19 00 00      	movq	0x1928(%rsp), %r9
     18d: 45 8b 0c 11                  	movl	(%r9,%rdx), %r9d
     191: 44 89 8c 84 78 0e 00 00      	movl	%r9d, 0xe78(%rsp,%rax,4)
     199: 4c 8b 8c 24 30 19 00 00      	movq	0x1930(%rsp), %r9
     1a1: 45 8b 0c 11                  	movl	(%r9,%rdx), %r9d
     1a5: 44 89 8c 84 78 10 00 00      	movl	%r9d, 0x1078(%rsp,%rax,4)
     1ad: 44 8b 4c 15 00               	movl	(%rbp,%rdx), %r9d
     1b2: 44 89 8c 84 78 12 00 00      	movl	%r9d, 0x1278(%rsp,%rax,4)
     1ba: 45 8b 0c 13                  	movl	(%r11,%rdx), %r9d
     1be: 44 89 8c 84 78 14 00 00      	movl	%r9d, 0x1478(%rsp,%rax,4)
     1c6: 41 8b 14 12                  	movl	(%r10,%rdx), %edx
     1ca: 89 94 84 78 16 00 00         	movl	%edx, 0x1678(%rsp,%rax,4)
     1d1: 48 ff c0                     	incq	%rax
     1d4: 49 39 c4                     	cmpq	%rax, %r12
     1d7: 0f 85 33 ff ff ff            	jne	0x110 <tide_rows_Shift_6+0x110>
     1dd: 48 8d 84 24 78 16 00 00      	leaq	0x1678(%rsp), %rax
     1e5: 48 8d 94 24 78 14 00 00      	leaq	0x1478(%rsp), %rdx
     1ed: 4c 8d 8c 24 78 12 00 00      	leaq	0x1278(%rsp), %r9
     1f5: 4c 8d 94 24 78 10 00 00      	leaq	0x1078(%rsp), %r10
     1fd: 4c 8d 9c 24 78 0e 00 00      	leaq	0xe78(%rsp), %r11
     205: 48 8d ac 24 78 0c 00 00      	leaq	0xc78(%rsp), %rbp
     20d: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     212: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
     217: 4c 89 4c 24 60               	movq	%r9, 0x60(%rsp)
     21c: 4c 89 54 24 58               	movq	%r10, 0x58(%rsp)
     221: 4c 89 5c 24 50               	movq	%r11, 0x50(%rsp)
     226: 48 89 6c 24 48               	movq	%rbp, 0x48(%rsp)
     22b: 48 8d 84 24 78 0a 00 00      	leaq	0xa78(%rsp), %rax
     233: 48 89 44 24 40               	movq	%rax, 0x40(%rsp)
     238: 48 8d 84 24 78 08 00 00      	leaq	0x878(%rsp), %rax
     240: 48 89 44 24 38               	movq	%rax, 0x38(%rsp)
     245: 48 8d 84 24 78 06 00 00      	leaq	0x678(%rsp), %rax
     24d: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     252: 48 8d 84 24 78 04 00 00      	leaq	0x478(%rsp), %rax
     25a: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
     25f: 48 8d 84 24 78 02 00 00      	leaq	0x278(%rsp), %rax
     267: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     26c: 31 ed                        	xorl	%ebp, %ebp
     26e: 4c 8d 4c 24 78               	leaq	0x78(%rsp), %r9
     273: 31 d2                        	xorl	%edx, %edx
     275: e8 00 00 00 00               	callq	0x27a <tide_rows_Shift_6+0x27a>
     27a: 4c 8b 84 24 40 19 00 00      	movq	0x1940(%rsp), %r8
     282: 4c 8b 8c 24 30 19 00 00      	movq	0x1930(%rsp), %r9
     28a: 4c 8b 94 24 28 19 00 00      	movq	0x1928(%rsp), %r10
     292: 4c 8b 9c 24 20 19 00 00      	movq	0x1920(%rsp), %r11
     29a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     2a0: 41 8d 44 2d 00               	leal	(%r13,%rbp), %eax
     2a5: 48 8b 8c 24 e8 18 00 00      	movq	0x18e8(%rsp), %rcx
     2ad: 0f b7 04 41                  	movzwl	(%rcx,%rax,2), %eax
     2b1: c1 e0 02                     	shll	$0x2, %eax
     2b4: 8b 4c ac 78                  	movl	0x78(%rsp,%rbp,4), %ecx
     2b8: 41 89 0c 07                  	movl	%ecx, (%r15,%rax)
     2bc: 8b 8c ac 78 02 00 00         	movl	0x278(%rsp,%rbp,4), %ecx
     2c3: 41 89 0c 06                  	movl	%ecx, (%r14,%rax)
     2c7: 8b 8c ac 78 04 00 00         	movl	0x478(%rsp,%rbp,4), %ecx
     2ce: 89 0c 03                     	movl	%ecx, (%rbx,%rax)
     2d1: 8b 8c ac 78 06 00 00         	movl	0x678(%rsp,%rbp,4), %ecx
     2d8: 89 0c 07                     	movl	%ecx, (%rdi,%rax)
     2db: 8b 8c ac 78 08 00 00         	movl	0x878(%rsp,%rbp,4), %ecx
     2e2: 89 0c 06                     	movl	%ecx, (%rsi,%rax)
     2e5: 8b 8c ac 78 0a 00 00         	movl	0xa78(%rsp,%rbp,4), %ecx
     2ec: 4c 89 fa                     	movq	%r15, %rdx
     2ef: 4d 89 f7                     	movq	%r14, %r15
     2f2: 49 89 de                     	movq	%rbx, %r14
     2f5: 48 89 fb                     	movq	%rdi, %rbx
     2f8: 48 89 f7                     	movq	%rsi, %rdi
     2fb: 48 8b b4 24 18 19 00 00      	movq	0x1918(%rsp), %rsi
     303: 89 0c 06                     	movl	%ecx, (%rsi,%rax)
     306: 48 89 fe                     	movq	%rdi, %rsi
     309: 48 89 df                     	movq	%rbx, %rdi
     30c: 4c 89 f3                     	movq	%r14, %rbx
     30f: 4d 89 fe                     	movq	%r15, %r14
     312: 49 89 d7                     	movq	%rdx, %r15
     315: 8b 8c ac 78 0c 00 00         	movl	0xc78(%rsp,%rbp,4), %ecx
     31c: 41 89 0c 03                  	movl	%ecx, (%r11,%rax)
     320: 8b 8c ac 78 0e 00 00         	movl	0xe78(%rsp,%rbp,4), %ecx
     327: 41 89 0c 02                  	movl	%ecx, (%r10,%rax)
     32b: 8b 8c ac 78 10 00 00         	movl	0x1078(%rsp,%rbp,4), %ecx
     332: 41 89 0c 01                  	movl	%ecx, (%r9,%rax)
     336: 8b 8c ac 78 12 00 00         	movl	0x1278(%rsp,%rbp,4), %ecx
     33d: 48 8b 94 24 38 19 00 00      	movq	0x1938(%rsp), %rdx
     345: 89 0c 02                     	movl	%ecx, (%rdx,%rax)
     348: 8b 8c ac 78 14 00 00         	movl	0x1478(%rsp,%rbp,4), %ecx
     34f: 41 89 0c 00                  	movl	%ecx, (%r8,%rax)
     353: 8b 8c ac 78 16 00 00         	movl	0x1678(%rsp,%rbp,4), %ecx
     35a: 48 8b 94 24 48 19 00 00      	movq	0x1948(%rsp), %rdx
     362: 89 0c 02                     	movl	%ecx, (%rdx,%rax)
     365: 48 ff c5                     	incq	%rbp
     368: 49 39 ec                     	cmpq	%rbp, %r12
     36b: 0f 85 2f ff ff ff            	jne	0x2a0 <tide_rows_Shift_6+0x2a0>
     371: 48 81 c4 78 18 00 00         	addq	$0x1878, %rsp           # imm = 0x1878
     378: 5b                           	popq	%rbx
     379: 5d                           	popq	%rbp
     37a: 5f                           	popq	%rdi
     37b: 5e                           	popq	%rsi
     37c: 41 5c                        	popq	%r12
     37e: 41 5d                        	popq	%r13
     380: 41 5e                        	popq	%r14
     382: 41 5f                        	popq	%r15
     384: c3                           	retq
