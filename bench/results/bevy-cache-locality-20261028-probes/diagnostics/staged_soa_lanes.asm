
C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7\build\bevy-cache-locality-probe/staged_soa_lanes/wide.obj:	file format coff-x86-64

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
       c: b8 e8 18 00 00               	movl	$0x18e8, %eax           # imm = 0x18E8
      11: e8 00 00 00 00               	callq	0x16 <tide_rows_Shift_6+0x16>
      16: 48 29 c4                     	subq	%rax, %rsp
      19: 48 8b 84 24 88 19 00 00      	movq	0x1988(%rsp), %rax
      21: 4c 8b 9c 24 80 19 00 00      	movq	0x1980(%rsp), %r11
      29: 4c 8b 8c 24 78 19 00 00      	movq	0x1978(%rsp), %r9
      31: 4c 8b 94 24 70 19 00 00      	movq	0x1970(%rsp), %r10
      39: 4c 8b bc 24 68 19 00 00      	movq	0x1968(%rsp), %r15
      41: 4c 8b a4 24 60 19 00 00      	movq	0x1960(%rsp), %r12
      49: 0f 28 84 24 b0 19 00 00      	movaps	0x19b0(%rsp), %xmm0
      51: 0f 28 8c 24 a0 19 00 00      	movaps	0x19a0(%rsp), %xmm1
      59: 0f 28 94 24 90 19 00 00      	movaps	0x1990(%rsp), %xmm2
      61: 4c 89 a4 24 88 00 00 00      	movq	%r12, 0x88(%rsp)
      69: 4c 89 bc 24 90 00 00 00      	movq	%r15, 0x90(%rsp)
      71: 4c 89 94 24 98 00 00 00      	movq	%r10, 0x98(%rsp)
      79: 4c 89 8c 24 a0 00 00 00      	movq	%r9, 0xa0(%rsp)
      81: 4c 89 9c 24 a8 00 00 00      	movq	%r11, 0xa8(%rsp)
      89: 48 89 84 24 b0 00 00 00      	movq	%rax, 0xb0(%rsp)
      91: 0f 11 94 24 b8 00 00 00      	movups	%xmm2, 0xb8(%rsp)
      99: 0f 11 8c 24 c8 00 00 00      	movups	%xmm1, 0xc8(%rsp)
      a1: 0f 11 84 24 d8 00 00 00      	movups	%xmm0, 0xd8(%rsp)
      a9: 41 29 d0                     	subl	%edx, %r8d
      ac: 0f 85 a3 00 00 00            	jne	0x155 <tide_rows_Shift_6+0x155>
      b2: 48 8d 84 24 e8 16 00 00      	leaq	0x16e8(%rsp), %rax
      ba: 48 8d 94 24 e8 14 00 00      	leaq	0x14e8(%rsp), %rdx
      c2: 4c 8d 8c 24 e8 12 00 00      	leaq	0x12e8(%rsp), %r9
      ca: 4c 8d 94 24 e8 10 00 00      	leaq	0x10e8(%rsp), %r10
      d2: 4c 8d 9c 24 e8 0e 00 00      	leaq	0xee8(%rsp), %r11
      da: 48 8d b4 24 e8 0c 00 00      	leaq	0xce8(%rsp), %rsi
      e2: 48 8d bc 24 e8 0a 00 00      	leaq	0xae8(%rsp), %rdi
      ea: 48 8d 9c 24 e8 08 00 00      	leaq	0x8e8(%rsp), %rbx
      f2: 4c 8d b4 24 e8 06 00 00      	leaq	0x6e8(%rsp), %r14
      fa: 4c 8d bc 24 e8 04 00 00      	leaq	0x4e8(%rsp), %r15
     102: 4c 8d a4 24 e8 02 00 00      	leaq	0x2e8(%rsp), %r12
     10a: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     10f: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
     114: 4c 89 4c 24 60               	movq	%r9, 0x60(%rsp)
     119: 4c 89 54 24 58               	movq	%r10, 0x58(%rsp)
     11e: 4c 89 5c 24 50               	movq	%r11, 0x50(%rsp)
     123: 48 89 74 24 48               	movq	%rsi, 0x48(%rsp)
     128: 48 89 7c 24 40               	movq	%rdi, 0x40(%rsp)
     12d: 48 89 5c 24 38               	movq	%rbx, 0x38(%rsp)
     132: 4c 89 74 24 30               	movq	%r14, 0x30(%rsp)
     137: 4c 89 7c 24 28               	movq	%r15, 0x28(%rsp)
     13c: 4c 89 64 24 20               	movq	%r12, 0x20(%rsp)
     141: 4c 8d 8c 24 e8 00 00 00      	leaq	0xe8(%rsp), %r9
     149: 31 d2                        	xorl	%edx, %edx
     14b: e8 00 00 00 00               	callq	0x150 <tide_rows_Shift_6+0x150>
     150: e9 0f 02 00 00               	jmp	0x364 <tide_rows_Shift_6+0x364>
     155: 48 89 8c 24 80 00 00 00      	movq	%rcx, 0x80(%rsp)
     15d: 45 89 c5                     	movl	%r8d, %r13d
     160: 44 89 e8                     	movl	%r13d, %eax
     163: 83 e0 07                     	andl	$0x7, %eax
     166: 41 83 f8 08                  	cmpl	$0x8, %r8d
     16a: 72 24                        	jb	0x190 <tide_rows_Shift_6+0x190>
     16c: 45 89 e9                     	movl	%r13d, %r9d
     16f: 41 83 e1 f8                  	andl	$-0x8, %r9d
     173: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     180: 49 83 c1 f8                  	addq	$-0x8, %r9
     184: 75 fa                        	jne	0x180 <tide_rows_Shift_6+0x180>
     186: 48 85 c0                     	testq	%rax, %rax
     189: 74 0a                        	je	0x195 <tide_rows_Shift_6+0x195>
     18b: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     190: 48 ff c8                     	decq	%rax
     193: 75 fb                        	jne	0x190 <tide_rows_Shift_6+0x190>
     195: 48 8b ac 24 58 19 00 00      	movq	0x1958(%rsp), %rbp
     19d: 44 89 e8                     	movl	%r13d, %eax
     1a0: 83 e0 fe                     	andl	$-0x2, %eax
     1a3: 4c 8d 8c 24 ec 00 00 00      	leaq	0xec(%rsp), %r9
     1ab: 44 8d 52 01                  	leal	0x1(%rdx), %r10d
     1af: 89 d6                        	movl	%edx, %esi
     1b1: 45 31 db                     	xorl	%r11d, %r11d
     1b4: eb 3c                        	jmp	0x1f2 <tide_rows_Shift_6+0x1f2>
     1b6: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     1c0: 4c 89 d9                     	movq	%r11, %rcx
     1c3: 48 c1 e1 09                  	shlq	$0x9, %rcx
     1c7: 48 01 e1                     	addq	%rsp, %rcx
     1ca: 48 81 c1 e8 00 00 00         	addq	$0xe8, %rcx
     1d1: 44 8d 34 1a                  	leal	(%rdx,%rbx), %r14d
     1d5: 46 0f b7 74 75 00            	movzwl	(%rbp,%r14,2), %r14d
     1db: 42 8b 3c b7                  	movl	(%rdi,%r14,4), %edi
     1df: 89 3c 99                     	movl	%edi, (%rcx,%rbx,4)
     1e2: 49 ff c3                     	incq	%r11
     1e5: 49 81 c1 00 02 00 00         	addq	$0x200, %r9             # imm = 0x200
     1ec: 49 83 fb 0c                  	cmpq	$0xc, %r11
     1f0: 74 4f                        	je	0x241 <tide_rows_Shift_6+0x241>
     1f2: 4a 8b bc dc 88 00 00 00      	movq	0x88(%rsp,%r11,8), %rdi
     1fa: 31 db                        	xorl	%ebx, %ebx
     1fc: 41 83 f8 01                  	cmpl	$0x1, %r8d
     200: 74 be                        	je	0x1c0 <tide_rows_Shift_6+0x1c0>
     202: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     210: 8d 0c 1e                     	leal	(%rsi,%rbx), %ecx
     213: 0f b7 4c 4d 00               	movzwl	(%rbp,%rcx,2), %ecx
     218: 8b 0c 8f                     	movl	(%rdi,%rcx,4), %ecx
     21b: 41 89 4c 99 fc               	movl	%ecx, -0x4(%r9,%rbx,4)
     220: 41 8d 0c 1a                  	leal	(%r10,%rbx), %ecx
     224: 0f b7 4c 4d 00               	movzwl	(%rbp,%rcx,2), %ecx
     229: 8b 0c 8f                     	movl	(%rdi,%rcx,4), %ecx
     22c: 41 89 0c 99                  	movl	%ecx, (%r9,%rbx,4)
     230: 48 83 c3 02                  	addq	$0x2, %rbx
     234: 48 39 d8                     	cmpq	%rbx, %rax
     237: 75 d7                        	jne	0x210 <tide_rows_Shift_6+0x210>
     239: 41 f6 c5 01                  	testb	$0x1, %r13b
     23d: 75 81                        	jne	0x1c0 <tide_rows_Shift_6+0x1c0>
     23f: eb a1                        	jmp	0x1e2 <tide_rows_Shift_6+0x1e2>
     241: 48 8d 84 24 e8 16 00 00      	leaq	0x16e8(%rsp), %rax
     249: 48 8d 94 24 e8 14 00 00      	leaq	0x14e8(%rsp), %rdx
     251: 4c 8d 8c 24 e8 12 00 00      	leaq	0x12e8(%rsp), %r9
     259: 4c 8d 94 24 e8 10 00 00      	leaq	0x10e8(%rsp), %r10
     261: 4c 8d 9c 24 e8 0e 00 00      	leaq	0xee8(%rsp), %r11
     269: 48 8d bc 24 e8 0c 00 00      	leaq	0xce8(%rsp), %rdi
     271: 48 8d 9c 24 e8 0a 00 00      	leaq	0xae8(%rsp), %rbx
     279: 48 8d 8c 24 e8 08 00 00      	leaq	0x8e8(%rsp), %rcx
     281: 4c 8d b4 24 e8 06 00 00      	leaq	0x6e8(%rsp), %r14
     289: 48 89 44 24 70               	movq	%rax, 0x70(%rsp)
     28e: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
     293: 4c 89 4c 24 60               	movq	%r9, 0x60(%rsp)
     298: 4c 89 54 24 58               	movq	%r10, 0x58(%rsp)
     29d: 4c 89 5c 24 50               	movq	%r11, 0x50(%rsp)
     2a2: 48 89 7c 24 48               	movq	%rdi, 0x48(%rsp)
     2a7: 48 89 5c 24 40               	movq	%rbx, 0x40(%rsp)
     2ac: 48 89 4c 24 38               	movq	%rcx, 0x38(%rsp)
     2b1: 4c 89 74 24 30               	movq	%r14, 0x30(%rsp)
     2b6: 48 8d 84 24 e8 04 00 00      	leaq	0x4e8(%rsp), %rax
     2be: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
     2c3: 48 8d 84 24 e8 02 00 00      	leaq	0x2e8(%rsp), %rax
     2cb: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
     2d0: 31 ff                        	xorl	%edi, %edi
     2d2: 4c 8d 8c 24 e8 00 00 00      	leaq	0xe8(%rsp), %r9
     2da: 48 8b 8c 24 80 00 00 00      	movq	0x80(%rsp), %rcx
     2e2: 31 d2                        	xorl	%edx, %edx
     2e4: e8 00 00 00 00               	callq	0x2e9 <tide_rows_Shift_6+0x2e9>
     2e9: 48 8b 94 24 88 19 00 00      	movq	0x1988(%rsp), %rdx
     2f1: 4c 8b 84 24 80 19 00 00      	movq	0x1980(%rsp), %r8
     2f9: 4c 8b 8c 24 78 19 00 00      	movq	0x1978(%rsp), %r9
     301: 4c 8b 94 24 70 19 00 00      	movq	0x1970(%rsp), %r10
     309: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     310: 8d 04 3e                     	leal	(%rsi,%rdi), %eax
     313: 0f b7 44 45 00               	movzwl	(%rbp,%rax,2), %eax
     318: c1 e0 02                     	shll	$0x2, %eax
     31b: 8b 8c bc e8 00 00 00         	movl	0xe8(%rsp,%rdi,4), %ecx
     322: 41 89 0c 04                  	movl	%ecx, (%r12,%rax)
     326: 8b 8c bc e8 02 00 00         	movl	0x2e8(%rsp,%rdi,4), %ecx
     32d: 41 89 0c 07                  	movl	%ecx, (%r15,%rax)
     331: 8b 8c bc e8 04 00 00         	movl	0x4e8(%rsp,%rdi,4), %ecx
     338: 41 89 0c 02                  	movl	%ecx, (%r10,%rax)
     33c: 8b 8c bc e8 06 00 00         	movl	0x6e8(%rsp,%rdi,4), %ecx
     343: 41 89 0c 01                  	movl	%ecx, (%r9,%rax)
     347: 8b 8c bc e8 08 00 00         	movl	0x8e8(%rsp,%rdi,4), %ecx
     34e: 41 89 0c 00                  	movl	%ecx, (%r8,%rax)
     352: 8b 8c bc e8 0a 00 00         	movl	0xae8(%rsp,%rdi,4), %ecx
     359: 89 0c 02                     	movl	%ecx, (%rdx,%rax)
     35c: 48 ff c7                     	incq	%rdi
     35f: 49 39 fd                     	cmpq	%rdi, %r13
     362: 75 ac                        	jne	0x310 <tide_rows_Shift_6+0x310>
     364: 48 81 c4 e8 18 00 00         	addq	$0x18e8, %rsp           # imm = 0x18E8
     36b: 5b                           	popq	%rbx
     36c: 5d                           	popq	%rbp
     36d: 5f                           	popq	%rdi
     36e: 5e                           	popq	%rsi
     36f: 41 5c                        	popq	%r12
     371: 41 5d                        	popq	%r13
     373: 41 5e                        	popq	%r14
     375: 41 5f                        	popq	%r15
     377: c3                           	retq
