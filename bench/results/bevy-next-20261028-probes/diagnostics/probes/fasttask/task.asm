
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-next-probes/fasttask/wide.obj:	file format coff-x86-64

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
       c: 48 81 ec 88 00 00 00         	subq	$0x88, %rsp
      13: 41 83 f8 01                  	cmpl	$0x1, %r8d
      17: 0f 85 be 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      1d: 3b 51 54                     	cmpl	0x54(%rcx), %edx
      20: 0f 83 b5 02 00 00            	jae	0x2db <tide_task_Shift+0x2db>
      26: 48 c7 84 24 80 00 00 00 0e 00 00 00  	movq	$0xe, 0x80(%rsp)
      32: 48 8b 81 a0 00 00 00         	movq	0xa0(%rcx), %rax
      39: 48 85 c0                     	testq	%rax, %rax
      3c: 0f 84 99 02 00 00            	je	0x2db <tide_task_Shift+0x2db>
      42: 83 38 01                     	cmpl	$0x1, (%rax)
      45: 0f 85 90 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      4b: 83 78 04 00                  	cmpl	$0x0, 0x4(%rax)
      4f: 0f 84 86 02 00 00            	je	0x2db <tide_task_Shift+0x2db>
      55: 4c 8b 50 08                  	movq	0x8(%rax), %r10
      59: 41 83 3a 01                  	cmpl	$0x1, (%r10)
      5d: 0f 85 78 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      63: 41 83 7a 04 0b               	cmpl	$0xb, 0x4(%r10)
      68: 0f 85 6d 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      6e: 41 83 7a 08 01               	cmpl	$0x1, 0x8(%r10)
      73: 0f 85 62 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      79: 41 3b 52 0c                  	cmpl	0xc(%r10), %edx
      7d: 0f 83 58 02 00 00            	jae	0x2db <tide_task_Shift+0x2db>
      83: 49 8b 42 10                  	movq	0x10(%r10), %rax
      87: 48 8b 00                     	movq	(%rax), %rax
      8a: 48 3b 84 24 80 00 00 00      	cmpq	0x80(%rsp), %rax
      92: 0f 85 43 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      98: 4d 8b 4a 20                  	movq	0x20(%r10), %r9
      9c: 89 d0                        	movl	%edx, %eax
      9e: 4c 69 d8 0c 01 00 00         	imulq	$0x10c, %rax, %r11      # imm = 0x10C
      a5: 43 80 7c 19 08 01            	cmpb	$0x1, 0x8(%r9,%r11)
      ab: 0f 85 2a 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      b1: 4d 01 d9                     	addq	%r11, %r9
      b4: 45 8b 19                     	movl	(%r9), %r11d
      b7: 8b 71 50                     	movl	0x50(%rcx), %esi
      ba: 89 d7                        	movl	%edx, %edi
      bc: c1 e7 0b                     	shll	$0xb, %edi
      bf: 29 fe                        	subl	%edi, %esi
      c1: 81 fe 00 08 00 00            	cmpl	$0x800, %esi            # imm = 0x800
      c7: bf 00 08 00 00               	movl	$0x800, %edi            # imm = 0x800
      cc: 0f 42 fe                     	cmovbl	%esi, %edi
      cf: 41 39 fb                     	cmpl	%edi, %r11d
      d2: 0f 85 03 02 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      d8: 4d 8b 52 18                  	movq	0x18(%r10), %r10
      dc: 49 8b 34 c2                  	movq	(%r10,%rax,8), %rsi
      e0: 48 c1 e0 07                  	shlq	$0x7, %rax
      e4: 4c 8b 51 60                  	movq	0x60(%rcx), %r10
      e8: 49 3b 74 02 70               	cmpq	0x70(%r10,%rax), %rsi
      ed: 0f 85 e8 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
      f3: 41 8b 71 04                  	movl	0x4(%r9), %esi
      f7: 41 83 c3 0f                  	addl	$0xf, %r11d
      fb: 41 c1 eb 04                  	shrl	$0x4, %r11d
      ff: 44 39 de                     	cmpl	%r11d, %esi
     102: 0f 83 d3 01 00 00            	jae	0x2db <tide_task_Shift+0x2db>
     108: 85 f6                        	testl	%esi, %esi
     10a: 0f 84 b7 01 00 00            	je	0x2c7 <tide_task_Shift+0x2c7>
     110: 4d 8b 5c 02 10               	movq	0x10(%r10,%rax), %r11
     115: 41 83 3b 01                  	cmpl	$0x1, (%r11)
     119: 0f 85 bc 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     11f: 49 8b 74 02 18               	movq	0x18(%r10,%rax), %rsi
     124: 83 3e 01                     	cmpl	$0x1, (%rsi)
     127: 0f 85 ae 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     12d: 49 8b 74 02 20               	movq	0x20(%r10,%rax), %rsi
     132: 83 3e 01                     	cmpl	$0x1, (%rsi)
     135: 0f 85 a0 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     13b: 49 8b 74 02 28               	movq	0x28(%r10,%rax), %rsi
     140: 83 3e 01                     	cmpl	$0x1, (%rsi)
     143: 0f 85 92 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     149: 49 8b 74 02 30               	movq	0x30(%r10,%rax), %rsi
     14e: 83 3e 01                     	cmpl	$0x1, (%rsi)
     151: 0f 85 84 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     157: 49 8b 74 02 38               	movq	0x38(%r10,%rax), %rsi
     15c: 83 3e 01                     	cmpl	$0x1, (%rsi)
     15f: 0f 85 76 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     165: 49 8b 74 02 40               	movq	0x40(%r10,%rax), %rsi
     16a: 83 3e 01                     	cmpl	$0x1, (%rsi)
     16d: 0f 85 68 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     173: 49 8b 74 02 48               	movq	0x48(%r10,%rax), %rsi
     178: 83 3e 01                     	cmpl	$0x1, (%rsi)
     17b: 0f 85 5a 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     181: 49 8b 74 02 50               	movq	0x50(%r10,%rax), %rsi
     186: 83 3e 01                     	cmpl	$0x1, (%rsi)
     189: 0f 85 4c 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     18f: 49 8b 74 02 58               	movq	0x58(%r10,%rax), %rsi
     194: 83 3e 01                     	cmpl	$0x1, (%rsi)
     197: 0f 85 3e 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     19d: 49 8b 74 02 60               	movq	0x60(%r10,%rax), %rsi
     1a2: 83 3e 01                     	cmpl	$0x1, (%rsi)
     1a5: 0f 85 30 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     1ab: 49 8b 74 02 68               	movq	0x68(%r10,%rax), %rsi
     1b0: 83 3e 01                     	cmpl	$0x1, (%rsi)
     1b3: 0f 85 22 01 00 00            	jne	0x2db <tide_task_Shift+0x2db>
     1b9: 41 c7 43 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r11)  # imm = 0xFFFFFFFF
     1c1: 49 83 c3 20                  	addq	$0x20, %r11
     1c5: 4d 8b 44 02 18               	movq	0x18(%r10,%rax), %r8
     1ca: 41 c7 40 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r8)   # imm = 0xFFFFFFFF
     1d2: 49 83 c0 20                  	addq	$0x20, %r8
     1d6: 49 8b 74 02 20               	movq	0x20(%r10,%rax), %rsi
     1db: c7 46 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rsi)  # imm = 0xFFFFFFFF
     1e2: 48 83 c6 20                  	addq	$0x20, %rsi
     1e6: 49 8b 7c 02 28               	movq	0x28(%r10,%rax), %rdi
     1eb: c7 47 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdi)  # imm = 0xFFFFFFFF
     1f2: 48 83 c7 20                  	addq	$0x20, %rdi
     1f6: 49 8b 5c 02 30               	movq	0x30(%r10,%rax), %rbx
     1fb: c7 43 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbx)  # imm = 0xFFFFFFFF
     202: 48 83 c3 20                  	addq	$0x20, %rbx
     206: 4d 8b 74 02 38               	movq	0x38(%r10,%rax), %r14
     20b: 41 c7 46 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r14)  # imm = 0xFFFFFFFF
     213: 49 83 c6 20                  	addq	$0x20, %r14
     217: 4d 8b 7c 02 40               	movq	0x40(%r10,%rax), %r15
     21c: 41 c7 47 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r15)  # imm = 0xFFFFFFFF
     224: 49 83 c7 20                  	addq	$0x20, %r15
     228: 4d 8b 64 02 48               	movq	0x48(%r10,%rax), %r12
     22d: 41 c7 44 24 0c ff ff ff ff   	movl	$0xffffffff, 0xc(%r12)  # imm = 0xFFFFFFFF
     236: 49 83 c4 20                  	addq	$0x20, %r12
     23a: 4d 8b 6c 02 50               	movq	0x50(%r10,%rax), %r13
     23f: 41 c7 45 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r13)  # imm = 0xFFFFFFFF
     247: 49 83 c5 20                  	addq	$0x20, %r13
     24b: 49 8b 6c 02 58               	movq	0x58(%r10,%rax), %rbp
     250: c7 45 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rbp)  # imm = 0xFFFFFFFF
     257: 48 83 c5 20                  	addq	$0x20, %rbp
     25b: 49 8b 54 02 60               	movq	0x60(%r10,%rax), %rdx
     260: c7 42 0c ff ff ff ff         	movl	$0xffffffff, 0xc(%rdx)  # imm = 0xFFFFFFFF
     267: 48 83 c2 20                  	addq	$0x20, %rdx
     26b: 4d 8b 54 02 68               	movq	0x68(%r10,%rax), %r10
     270: 41 c7 42 0c ff ff ff ff      	movl	$0xffffffff, 0xc(%r10)  # imm = 0xFFFFFFFF
     278: 49 83 c2 20                  	addq	$0x20, %r10
     27c: 41 8b 41 04                  	movl	0x4(%r9), %eax
     280: 4c 89 54 24 78               	movq	%r10, 0x78(%rsp)
     285: 48 89 54 24 70               	movq	%rdx, 0x70(%rsp)
     28a: 48 89 6c 24 68               	movq	%rbp, 0x68(%rsp)
     28f: 4c 89 6c 24 60               	movq	%r13, 0x60(%rsp)
     294: 4c 89 64 24 58               	movq	%r12, 0x58(%rsp)
     299: 4c 89 7c 24 50               	movq	%r15, 0x50(%rsp)
     29e: 4c 89 74 24 48               	movq	%r14, 0x48(%rsp)
     2a3: 48 89 5c 24 40               	movq	%rbx, 0x40(%rsp)
     2a8: 48 89 7c 24 38               	movq	%rdi, 0x38(%rsp)
     2ad: 48 89 74 24 30               	movq	%rsi, 0x30(%rsp)
     2b2: 4c 89 44 24 28               	movq	%r8, 0x28(%rsp)
     2b7: 4c 89 5c 24 20               	movq	%r11, 0x20(%rsp)
     2bc: 31 d2                        	xorl	%edx, %edx
     2be: 41 89 c0                     	movl	%eax, %r8d
     2c1: e8 00 00 00 00               	callq	0x2c6 <tide_task_Shift+0x2c6>
     2c6: 90                           	nop
     2c7: 48 81 c4 88 00 00 00         	addq	$0x88, %rsp
     2ce: 5b                           	popq	%rbx
     2cf: 5d                           	popq	%rbp
     2d0: 5f                           	popq	%rdi
     2d1: 5e                           	popq	%rsi
     2d2: 41 5c                        	popq	%r12
     2d4: 41 5d                        	popq	%r13
     2d6: 41 5e                        	popq	%r14
     2d8: 41 5f                        	popq	%r15
     2da: c3                           	retq
     2db: 48 81 c4 88 00 00 00         	addq	$0x88, %rsp
     2e2: 5b                           	popq	%rbx
     2e3: 5d                           	popq	%rbp
     2e4: 5f                           	popq	%rdi
     2e5: 5e                           	popq	%rsi
     2e6: 41 5c                        	popq	%r12
     2e8: 41 5d                        	popq	%r13
     2ea: 41 5e                        	popq	%r14
     2ec: 41 5f                        	popq	%r15
     2ee: e9 00 00 00 00               	jmp	0x2f3 <tide_task_Shift+0x2f3>
