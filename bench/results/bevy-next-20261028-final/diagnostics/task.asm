
build/release/bench/versus/CMakeFiles/versus_wide.dir/tide/versus_wide/wide.c.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <tide_task_Shift>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 48 81 ec 98 00 00 00         	subq	$0x98, %rsp
      13: 41 83 f8 01                  	cmpl	$0x1, %r8d
      17: 0f 85 a8 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      1d: 3b 51 54                     	cmpl	0x54(%rcx), %edx
      20: 0f 83 9f 02 00 00            	jae	0x2c5 <tide_task_Shift+0x2c5>
      26: 48 c7 84 24 90 00 00 00 0e 00 00 00  	movq	$0xe, 0x90(%rsp)
      32: 48 8b 81 a0 00 00 00         	movq	0xa0(%rcx), %rax
      39: 48 85 c0                     	testq	%rax, %rax
      3c: 0f 84 83 02 00 00            	je	0x2c5 <tide_task_Shift+0x2c5>
      42: 83 38 01                     	cmpl	$0x1, (%rax)
      45: 0f 85 7a 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      4b: 83 78 04 00                  	cmpl	$0x0, 0x4(%rax)
      4f: 0f 84 70 02 00 00            	je	0x2c5 <tide_task_Shift+0x2c5>
      55: 48 8b 40 08                  	movq	0x8(%rax), %rax
      59: 83 38 01                     	cmpl	$0x1, (%rax)
      5c: 0f 85 63 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      62: 83 78 04 0b                  	cmpl	$0xb, 0x4(%rax)
      66: 0f 85 59 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      6c: 83 78 08 01                  	cmpl	$0x1, 0x8(%rax)
      70: 0f 85 4f 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      76: 3b 50 0c                     	cmpl	0xc(%rax), %edx
      79: 0f 83 46 02 00 00            	jae	0x2c5 <tide_task_Shift+0x2c5>
      7f: 4c 8b 48 10                  	movq	0x10(%rax), %r9
      83: 4d 8b 09                     	movq	(%r9), %r9
      86: 4c 3b 8c 24 90 00 00 00      	cmpq	0x90(%rsp), %r9
      8e: 0f 85 31 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      94: 4c 8b 48 20                  	movq	0x20(%rax), %r9
      98: 41 89 d3                     	movl	%edx, %r11d
      9b: 4d 69 d3 0c 01 00 00         	imulq	$0x10c, %r11, %r10      # imm = 0x10C
      a2: 43 80 7c 11 08 01            	cmpb	$0x1, 0x8(%r9,%r10)
      a8: 0f 85 17 02 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      ae: 4d 01 d1                     	addq	%r10, %r9
      b1: 45 8b 11                     	movl	(%r9), %r10d
      b4: 8b 71 50                     	movl	0x50(%rcx), %esi
      b7: 89 d7                        	movl	%edx, %edi
      b9: c1 e7 0b                     	shll	$0xb, %edi
      bc: 29 fe                        	subl	%edi, %esi
      be: 81 fe 00 08 00 00            	cmpl	$0x800, %esi            # imm = 0x800
      c4: bf 00 08 00 00               	movl	$0x800, %edi            # imm = 0x800
      c9: 0f 42 fe                     	cmovbl	%esi, %edi
      cc: 41 39 fa                     	cmpl	%edi, %r10d
      cf: 0f 85 f0 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      d5: 48 8b 40 18                  	movq	0x18(%rax), %rax
      d9: 4a 8b 04 d8                  	movq	(%rax,%r11,8), %rax
      dd: 49 c1 e3 07                  	shlq	$0x7, %r11
      e1: 4c 8b 61 60                  	movq	0x60(%rcx), %r12
      e5: 4b 3b 44 1c 70               	cmpq	0x70(%r12,%r11), %rax
      ea: 0f 85 d5 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
      f0: 41 8b 41 04                  	movl	0x4(%r9), %eax
      f4: 41 83 c2 0f                  	addl	$0xf, %r10d
      f8: 41 c1 ea 04                  	shrl	$0x4, %r10d
      fc: 44 39 d0                     	cmpl	%r10d, %eax
      ff: 0f 83 c0 01 00 00            	jae	0x2c5 <tide_task_Shift+0x2c5>
     105: 85 c0                        	testl	%eax, %eax
     107: 0f 84 a4 01 00 00            	je	0x2b1 <tide_task_Shift+0x2b1>
     10d: 4b 8b 44 1c 10               	movq	0x10(%r12,%r11), %rax
     112: 83 38 01                     	cmpl	$0x1, (%rax)
     115: 0f 85 aa 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     11b: 4f 8b 54 1c 18               	movq	0x18(%r12,%r11), %r10
     120: 41 83 3a 01                  	cmpl	$0x1, (%r10)
     124: 0f 85 9b 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     12a: 4b 8b 74 1c 20               	movq	0x20(%r12,%r11), %rsi
     12f: 83 3e 01                     	cmpl	$0x1, (%rsi)
     132: 0f 85 8d 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     138: 4b 8b 7c 1c 28               	movq	0x28(%r12,%r11), %rdi
     13d: 83 3f 01                     	cmpl	$0x1, (%rdi)
     140: 0f 85 7f 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     146: 4b 8b 5c 1c 30               	movq	0x30(%r12,%r11), %rbx
     14b: 83 3b 01                     	cmpl	$0x1, (%rbx)
     14e: 0f 85 71 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     154: 4f 8b 74 1c 38               	movq	0x38(%r12,%r11), %r14
     159: 41 83 3e 01                  	cmpl	$0x1, (%r14)
     15d: 0f 85 62 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     163: 4f 8b 7c 1c 40               	movq	0x40(%r12,%r11), %r15
     168: 41 83 3f 01                  	cmpl	$0x1, (%r15)
     16c: 0f 85 53 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     172: 4f 8b 6c 1c 48               	movq	0x48(%r12,%r11), %r13
     177: 41 83 7d 00 01               	cmpl	$0x1, (%r13)
     17c: 0f 85 43 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     182: 4b 8b 6c 1c 50               	movq	0x50(%r12,%r11), %rbp
     187: 48 89 ac 24 88 00 00 00      	movq	%rbp, 0x88(%rsp)
     18f: 83 7d 00 01                  	cmpl	$0x1, (%rbp)
     193: 0f 85 2c 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     199: 4b 8b 6c 1c 58               	movq	0x58(%r12,%r11), %rbp
     19e: 48 89 ac 24 80 00 00 00      	movq	%rbp, 0x80(%rsp)
     1a6: 83 7d 00 01                  	cmpl	$0x1, (%rbp)
     1aa: 0f 85 15 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     1b0: 4b 8b 6c 1c 60               	movq	0x60(%r12,%r11), %rbp
     1b5: 83 7d 00 01                  	cmpl	$0x1, (%rbp)
     1b9: 0f 85 06 01 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     1bf: 4f 8b 5c 1c 68               	movq	0x68(%r12,%r11), %r11
     1c4: 41 83 3b 01                  	cmpl	$0x1, (%r11)
     1c8: 0f 85 f7 00 00 00            	jne	0x2c5 <tide_task_Shift+0x2c5>
     1ce: c7 40 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rax)  # imm = 0xFFFFFFFF
     1d5: 48 83 c0 20                  	addq	$0x20, %rax
     1d9: 41 c7 42 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r10)  # imm = 0xFFFFFFFF
     1e1: 49 83 c2 20                  	addq	$0x20, %r10
     1e5: c7 46 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rsi)  # imm = 0xFFFFFFFF
     1ec: 48 83 c6 20                  	addq	$0x20, %rsi
     1f0: c7 47 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdi)  # imm = 0xFFFFFFFF
     1f7: 48 83 c7 20                  	addq	$0x20, %rdi
     1fb: c7 43 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbx)  # imm = 0xFFFFFFFF
     202: 48 83 c3 20                  	addq	$0x20, %rbx
     206: 41 c7 46 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r14)  # imm = 0xFFFFFFFF
     20e: 49 83 c6 20                  	addq	$0x20, %r14
     212: 41 c7 47 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r15)  # imm = 0xFFFFFFFF
     21a: 49 83 c7 20                  	addq	$0x20, %r15
     21e: 41 c7 45 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r13)  # imm = 0xFFFFFFFF
     226: 49 83 c5 20                  	addq	$0x20, %r13
     22a: 4c 8b a4 24 88 00 00 00      	movq	0x88(%rsp), %r12
     232: 41 c7 44 24 0c ff ff ff ff   	movl	$0xffffffff, 0xc(%r12)  # imm = 0xFFFFFFFF
     23b: 49 83 c4 20                  	addq	$0x20, %r12
     23f: 48 8b 94 24 80 00 00 00      	movq	0x80(%rsp), %rdx
     247: c7 42 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdx)  # imm = 0xFFFFFFFF
     24e: 48 83 c2 20                  	addq	$0x20, %rdx
     252: c7 45 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbp)  # imm = 0xFFFFFFFF
     259: 48 83 c5 20                  	addq	$0x20, %rbp
     25d: 41 c7 43 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r11)  # imm = 0xFFFFFFFF
     265: 49 83 c3 20                  	addq	$0x20, %r11
     269: 45 8b 41 04                  	movl	0x4(%r9), %r8d
     26d: 4c 89 5c 24 78               	movq	%r11, 0x78(%rsp)
     272: 48 89 6c 24 70               	movq	%rbp, 0x70(%rsp)
     277: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
     27c: 4c 89 64 24 60               	movq	%r12, 0x60(%rsp)
     281: 4c 89 6c 24 58               	movq	%r13, 0x58(%rsp)
     286: 4c 89 7c 24 50               	movq	%r15, 0x50(%rsp)
     28b: 4c 89 74 24 48               	movq	%r14, 0x48(%rsp)
     290: 48 89 5c 24 40               	movq	%rbx, 0x40(%rsp)
     295: 48 89 7c 24 38               	movq	%rdi, 0x38(%rsp)
     29a: 48 89 74 24 30               	movq	%rsi, 0x30(%rsp)
     29f: 4c 89 54 24 28               	movq	%r10, 0x28(%rsp)
     2a4: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     2a9: 31 d2                        	xorl	%edx, %edx
     2ab: e8 00 00 00 00               	callq	0x2b0 <tide_task_Shift+0x2b0>
     2b0: 90                           	nop
     2b1: 48 81 c4 98 00 00 00         	addq	$0x98, %rsp
     2b8: 5b                           	popq	%rbx
     2b9: 5d                           	popq	%rbp
     2ba: 5f                           	popq	%rdi
     2bb: 5e                           	popq	%rsi
     2bc: 41 5c                        	popq	%r12
     2be: 41 5d                        	popq	%r13
     2c0: 41 5e                        	popq	%r14
     2c2: 41 5f                        	popq	%r15
     2c4: c3                           	retq
     2c5: 48 81 c4 98 00 00 00         	addq	$0x98, %rsp
     2cc: 5b                           	popq	%rbx
     2cd: 5d                           	popq	%rbp
     2ce: 5f                           	popq	%rdi
     2cf: 5e                           	popq	%rsi
     2d0: 41 5c                        	popq	%r12
     2d2: 41 5d                        	popq	%r13
     2d4: 41 5e                        	popq	%r14
     2d6: 41 5f                        	popq	%r15
     2d8: e9 00 00 00 00               	jmp	0x2dd <tide_task_Shift+0x2dd>
