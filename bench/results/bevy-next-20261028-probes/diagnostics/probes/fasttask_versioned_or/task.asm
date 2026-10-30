
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/fasttask_versioned_or/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec b8 00 00 00         	subq	$0xb8, %rsp
      13: 41 83 f8 01                  	cmpl	$0x1, %r8d
      17: 0f 85 bd 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      1d: 3b 51 54                     	cmpl	0x54(%rcx), %edx
      20: 0f 83 b4 02 00 00            	jae	0x2da <tide_task_Shift+0x2da>
      26: 48 c7 84 24 b0 00 00 00 0e 00 00 00  	movq	$0xe, 0xb0(%rsp)
      32: 48 8b 81 a0 00 00 00         	movq	0xa0(%rcx), %rax
      39: 48 85 c0                     	testq	%rax, %rax
      3c: 0f 84 98 02 00 00            	je	0x2da <tide_task_Shift+0x2da>
      42: 83 38 01                     	cmpl	$0x1, (%rax)
      45: 0f 85 8f 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      4b: 83 78 04 00                  	cmpl	$0x0, 0x4(%rax)
      4f: 0f 84 85 02 00 00            	je	0x2da <tide_task_Shift+0x2da>
      55: 48 8b 40 08                  	movq	0x8(%rax), %rax
      59: 83 38 01                     	cmpl	$0x1, (%rax)
      5c: 0f 85 78 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      62: 83 78 04 0b                  	cmpl	$0xb, 0x4(%rax)
      66: 0f 85 6e 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      6c: 83 78 08 01                  	cmpl	$0x1, 0x8(%rax)
      70: 0f 85 64 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      76: 3b 50 0c                     	cmpl	0xc(%rax), %edx
      79: 0f 83 5b 02 00 00            	jae	0x2da <tide_task_Shift+0x2da>
      7f: 4c 8b 48 10                  	movq	0x10(%rax), %r9
      83: 4d 8b 09                     	movq	(%r9), %r9
      86: 4c 3b 8c 24 b0 00 00 00      	cmpq	0xb0(%rsp), %r9
      8e: 0f 85 46 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      94: 4c 8b 48 20                  	movq	0x20(%rax), %r9
      98: 41 89 d6                     	movl	%edx, %r14d
      9b: 4d 69 d6 0c 01 00 00         	imulq	$0x10c, %r14, %r10      # imm = 0x10C
      a2: 43 80 7c 11 08 01            	cmpb	$0x1, 0x8(%r9,%r10)
      a8: 0f 85 2c 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      ae: 4d 01 d1                     	addq	%r10, %r9
      b1: 45 8b 19                     	movl	(%r9), %r11d
      b4: 44 8b 51 50                  	movl	0x50(%rcx), %r10d
      b8: 89 d6                        	movl	%edx, %esi
      ba: c1 e6 0b                     	shll	$0xb, %esi
      bd: 41 29 f2                     	subl	%esi, %r10d
      c0: 41 81 fa 00 08 00 00         	cmpl	$0x800, %r10d           # imm = 0x800
      c7: be 00 08 00 00               	movl	$0x800, %esi            # imm = 0x800
      cc: 41 0f 42 f2                  	cmovbl	%r10d, %esi
      d0: 41 39 f3                     	cmpl	%esi, %r11d
      d3: 0f 85 01 02 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      d9: 48 8b 40 18                  	movq	0x18(%rax), %rax
      dd: 4a 8b 04 f0                  	movq	(%rax,%r14,8), %rax
      e1: 49 c1 e6 07                  	shlq	$0x7, %r14
      e5: 4c 8b 51 60                  	movq	0x60(%rcx), %r10
      e9: 4b 3b 44 32 70               	cmpq	0x70(%r10,%r14), %rax
      ee: 0f 85 e6 01 00 00            	jne	0x2da <tide_task_Shift+0x2da>
      f4: 41 8b 41 04                  	movl	0x4(%r9), %eax
      f8: 41 83 c3 0f                  	addl	$0xf, %r11d
      fc: 41 c1 eb 04                  	shrl	$0x4, %r11d
     100: 44 39 d8                     	cmpl	%r11d, %eax
     103: 0f 83 d1 01 00 00            	jae	0x2da <tide_task_Shift+0x2da>
     109: 85 c0                        	testl	%eax, %eax
     10b: 0f 84 b5 01 00 00            	je	0x2c6 <tide_task_Shift+0x2c6>
     111: 4b 8b 74 32 10               	movq	0x10(%r10,%r14), %rsi
     116: 4f 8b 5c 32 18               	movq	0x18(%r10,%r14), %r11
     11b: 4b 8b 7c 32 20               	movq	0x20(%r10,%r14), %rdi
     120: 4b 8b 5c 32 28               	movq	0x28(%r10,%r14), %rbx
     125: 4f 8b 7c 32 30               	movq	0x30(%r10,%r14), %r15
     12a: 4f 8b 64 32 38               	movq	0x38(%r10,%r14), %r12
     12f: 4f 8b 6c 32 40               	movq	0x40(%r10,%r14), %r13
     134: 4b 8b 6c 32 48               	movq	0x48(%r10,%r14), %rbp
     139: 4b 8b 44 32 50               	movq	0x50(%r10,%r14), %rax
     13e: 48 89 84 24 a8 00 00 00      	movq	%rax, 0xa8(%rsp)
     146: 4b 8b 44 32 58               	movq	0x58(%r10,%r14), %rax
     14b: 48 89 84 24 88 00 00 00      	movq	%rax, 0x88(%rsp)
     153: 4b 8b 44 32 60               	movq	0x60(%r10,%r14), %rax
     158: 48 89 84 24 90 00 00 00      	movq	%rax, 0x90(%rsp)
     160: 4c 89 9c 24 98 00 00 00      	movq	%r11, 0x98(%rsp)
     168: 45 8b 1b                     	movl	(%r11), %r11d
     16b: 48 89 b4 24 a0 00 00 00      	movq	%rsi, 0xa0(%rsp)
     173: 44 0b 1e                     	orl	(%rsi), %r11d
     176: 48 89 fe                     	movq	%rdi, %rsi
     179: 44 0b 1f                     	orl	(%rdi), %r11d
     17c: 48 89 df                     	movq	%rbx, %rdi
     17f: 44 0b 1b                     	orl	(%rbx), %r11d
     182: 4c 89 fb                     	movq	%r15, %rbx
     185: 45 0b 1f                     	orl	(%r15), %r11d
     188: 4d 89 e7                     	movq	%r12, %r15
     18b: 45 0b 1c 24                  	orl	(%r12), %r11d
     18f: 4d 89 ec                     	movq	%r13, %r12
     192: 45 0b 5d 00                  	orl	(%r13), %r11d
     196: 49 89 ed                     	movq	%rbp, %r13
     199: 44 0b 5d 00                  	orl	(%rbp), %r11d
     19d: 48 8b 84 24 a8 00 00 00      	movq	0xa8(%rsp), %rax
     1a5: 48 89 c5                     	movq	%rax, %rbp
     1a8: 44 0b 18                     	orl	(%rax), %r11d
     1ab: 48 8b 84 24 88 00 00 00      	movq	0x88(%rsp), %rax
     1b3: 44 0b 18                     	orl	(%rax), %r11d
     1b6: 48 8b 84 24 90 00 00 00      	movq	0x90(%rsp), %rax
     1be: 44 0b 18                     	orl	(%rax), %r11d
     1c1: 4f 8b 54 32 68               	movq	0x68(%r10,%r14), %r10
     1c6: 45 0b 1a                     	orl	(%r10), %r11d
     1c9: 41 83 fb 01                  	cmpl	$0x1, %r11d
     1cd: 0f 85 07 01 00 00            	jne	0x2da <tide_task_Shift+0x2da>
     1d3: 48 8b 94 24 a0 00 00 00      	movq	0xa0(%rsp), %rdx
     1db: c7 42 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdx)  # imm = 0xFFFFFFFF
     1e2: 48 83 c2 20                  	addq	$0x20, %rdx
     1e6: 4c 8b 9c 24 98 00 00 00      	movq	0x98(%rsp), %r11
     1ee: 41 c7 43 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r11)  # imm = 0xFFFFFFFF
     1f6: 49 83 c3 20                  	addq	$0x20, %r11
     1fa: c7 46 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rsi)  # imm = 0xFFFFFFFF
     201: 48 83 c6 20                  	addq	$0x20, %rsi
     205: c7 47 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdi)  # imm = 0xFFFFFFFF
     20c: 48 83 c7 20                  	addq	$0x20, %rdi
     210: c7 43 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbx)  # imm = 0xFFFFFFFF
     217: 48 83 c3 20                  	addq	$0x20, %rbx
     21b: 41 c7 47 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r15)  # imm = 0xFFFFFFFF
     223: 49 83 c7 20                  	addq	$0x20, %r15
     227: 41 c7 44 24 0c ff ff ff ff   	movl	$0xffffffff, 0xc(%r12)  # imm = 0xFFFFFFFF
     230: 49 83 c4 20                  	addq	$0x20, %r12
     234: 41 c7 45 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r13)  # imm = 0xFFFFFFFF
     23c: 49 83 c5 20                  	addq	$0x20, %r13
     240: c7 45 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbp)  # imm = 0xFFFFFFFF
     247: 48 83 c5 20                  	addq	$0x20, %rbp
     24b: 48 8b 84 24 88 00 00 00      	movq	0x88(%rsp), %rax
     253: c7 40 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rax)  # imm = 0xFFFFFFFF
     25a: 48 83 c0 20                  	addq	$0x20, %rax
     25e: 4c 8b b4 24 90 00 00 00      	movq	0x90(%rsp), %r14
     266: 41 c7 46 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r14)  # imm = 0xFFFFFFFF
     26e: 49 83 c6 20                  	addq	$0x20, %r14
     272: 41 c7 42 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r10)  # imm = 0xFFFFFFFF
     27a: 49 83 c2 20                  	addq	$0x20, %r10
     27e: 45 8b 41 04                  	movl	0x4(%r9), %r8d
     282: 4c 89 54 24 78               	movq	%r10, 0x78(%rsp)
     287: 4c 89 74 24 70               	movq	%r14, 0x70(%rsp)
     28c: 48 89 44 24 68               	movq	%rax, 0x68(%rsp)
     291: 48 89 6c 24 60               	movq	%rbp, 0x60(%rsp)
     296: 4c 89 6c 24 58               	movq	%r13, 0x58(%rsp)
     29b: 4c 89 64 24 50               	movq	%r12, 0x50(%rsp)
     2a0: 4c 89 7c 24 48               	movq	%r15, 0x48(%rsp)
     2a5: 48 89 5c 24 40               	movq	%rbx, 0x40(%rsp)
     2aa: 48 89 7c 24 38               	movq	%rdi, 0x38(%rsp)
     2af: 48 89 74 24 30               	movq	%rsi, 0x30(%rsp)
     2b4: 4c 89 5c 24 28               	movq	%r11, 0x28(%rsp)
     2b9: 48 89 54 24 20               	movq	%rdx, 0x20(%rsp)
     2be: 31 d2                        	xorl	%edx, %edx
     2c0: e8 00 00 00 00               	callq	0x2c5 <tide_task_Shift+0x2c5>
     2c5: 90                           	nop
     2c6: 48 81 c4 b8 00 00 00         	addq	$0xb8, %rsp
     2cd: 5b                           	popq	%rbx
     2ce: 5d                           	popq	%rbp
     2cf: 5f                           	popq	%rdi
     2d0: 5e                           	popq	%rsi
     2d1: 41 5c                        	popq	%r12
     2d3: 41 5d                        	popq	%r13
     2d5: 41 5e                        	popq	%r14
     2d7: 41 5f                        	popq	%r15
     2d9: c3                           	retq
     2da: 48 81 c4 b8 00 00 00         	addq	$0xb8, %rsp
     2e1: 5b                           	popq	%rbx
     2e2: 5d                           	popq	%rbp
     2e3: 5f                           	popq	%rdi
     2e4: 5e                           	popq	%rsi
     2e5: 41 5c                        	popq	%r12
     2e7: 41 5d                        	popq	%r13
     2e9: 41 5e                        	popq	%r14
     2eb: 41 5f                        	popq	%r15
     2ed: e9 00 00 00 00               	jmp	0x2f2 <tide_task_Shift+0x2f2>
