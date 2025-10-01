	.file	"decomment.c"
	.text
	.section	.rodata
	.align 8
.LC0:
	.string	"Error: line %d: unterminated comment\n"
.LC1:
	.string	" "
	.text
	.globl	main
	.type	main, @function
main:
.LFB0:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	movl	$0, -24(%rbp)
	movl	$1, -16(%rbp)
	movl	$-1, -28(%rbp)
	movl	$0, -20(%rbp)
.L23:
	call	getchar@PLT
	movl	%eax, -12(%rbp)
	movl	-12(%rbp), %eax
	movb	%al, -29(%rbp)
	cmpl	$-1, -12(%rbp)
	jne	.L2
	movl	-20(%rbp), %eax
	cmpl	$5, %eax
	je	.L3
	movl	-20(%rbp), %eax
	cmpl	$6, %eax
	jne	.L26
.L3:
	movl	-28(%rbp), %edx
	movq	stderr(%rip), %rax
	leaq	.LC0(%rip), %rcx
	movq	%rcx, %rsi
	movq	%rax, %rdi
	movl	$0, %eax
	call	fprintf@PLT
	jmp	.L26
.L2:
	movl	-20(%rbp), %eax
	cmpl	$6, %eax
	ja	.L27
	movl	%eax, %eax
	leaq	0(,%rax,4), %rdx
	leaq	.L8(%rip), %rax
	movl	(%rdx,%rax), %eax
	cltq
	leaq	.L8(%rip), %rdx
	addq	%rdx, %rax
	notrack jmp	*%rax
	.section	.rodata
	.align 4
	.align 4
.L8:
	.long	.L13-.L8
	.long	.L12-.L8
	.long	.L27-.L8
	.long	.L11-.L8
	.long	.L10-.L8
	.long	.L9-.L8
	.long	.L7-.L8
	.text
.L13:
	cmpl	$47, -12(%rbp)
	jne	.L14
	movl	$3, -20(%rbp)
	jmp	.L18
.L14:
	cmpl	$34, -12(%rbp)
	je	.L16
	cmpl	$39, -12(%rbp)
	jne	.L17
.L16:
	movl	-12(%rbp), %eax
	movb	%al, -30(%rbp)
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$1, -20(%rbp)
	jmp	.L18
.L17:
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	jmp	.L18
.L12:
	movsbl	-30(%rbp), %edx
	leaq	-24(%rbp), %rcx
	movl	-12(%rbp), %esi
	leaq	-20(%rbp), %rax
	movq	%rax, %rdi
	call	handleQuote
	jmp	.L18
.L11:
	leaq	-28(%rbp), %rcx
	movl	-16(%rbp), %edx
	movl	-12(%rbp), %esi
	leaq	-20(%rbp), %rax
	movq	%rax, %rdi
	call	handleWaitComment
	jmp	.L18
.L10:
	cmpl	$10, -12(%rbp)
	jne	.L28
	leaq	.LC1(%rip), %rax
	movq	%rax, %rdi
	call	puts@PLT
	movl	$0, -20(%rbp)
	jmp	.L28
.L9:
	cmpl	$42, -12(%rbp)
	jne	.L20
	movl	$6, -20(%rbp)
	jmp	.L29
.L20:
	cmpl	$10, -12(%rbp)
	jne	.L29
	movl	$10, %edi
	call	putchar@PLT
	jmp	.L29
.L7:
	movl	-12(%rbp), %edx
	leaq	-20(%rbp), %rax
	movl	%edx, %esi
	movq	%rax, %rdi
	call	handleWaitEnd
	jmp	.L18
.L27:
	nop
	jmp	.L18
.L28:
	nop
	jmp	.L18
.L29:
	nop
.L18:
	cmpb	$10, -29(%rbp)
	jne	.L23
	addl	$1, -16(%rbp)
	jmp	.L23
.L26:
	nop
	movl	$0, %eax
	movq	-8(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L25
	call	__stack_chk_fail@PLT
.L25:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	main, .-main
	.type	handleQuote, @function
handleQuote:
.LFB1:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	movl	%edx, %eax
	movq	%rcx, -24(%rbp)
	movb	%al, -16(%rbp)
	movsbl	-16(%rbp), %eax
	cmpl	%eax, -12(%rbp)
	jne	.L31
	movq	-24(%rbp), %rax
	movl	(%rax), %eax
	testl	%eax, %eax
	jne	.L31
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movq	-8(%rbp), %rax
	movl	$0, (%rax)
	jmp	.L35
.L31:
	cmpl	$92, -12(%rbp)
	jne	.L33
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movq	-24(%rbp), %rax
	movl	$1, (%rax)
	jmp	.L35
.L33:
	movq	-24(%rbp), %rax
	movl	(%rax), %eax
	testl	%eax, %eax
	je	.L34
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movq	-24(%rbp), %rax
	movl	$0, (%rax)
	jmp	.L35
.L34:
	movl	-12(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
.L35:
	nop
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE1:
	.size	handleQuote, .-handleQuote
	.section	.rodata
.LC2:
	.string	"/%c"
	.text
	.type	handleWaitComment, @function
handleWaitComment:
.LFB2:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	movl	%edx, -16(%rbp)
	movq	%rcx, -24(%rbp)
	cmpl	$47, -12(%rbp)
	jne	.L37
	movq	-8(%rbp), %rax
	movl	$4, (%rax)
	movl	$32, %edi
	call	putchar@PLT
	jmp	.L40
.L37:
	cmpl	$42, -12(%rbp)
	jne	.L39
	movq	-8(%rbp), %rax
	movl	$5, (%rax)
	movl	$32, %edi
	call	putchar@PLT
	movq	-24(%rbp), %rax
	movl	-16(%rbp), %edx
	movl	%edx, (%rax)
	jmp	.L40
.L39:
	movq	-8(%rbp), %rax
	movl	$0, (%rax)
	movl	-12(%rbp), %eax
	movl	%eax, %esi
	leaq	.LC2(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
.L40:
	nop
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE2:
	.size	handleWaitComment, .-handleWaitComment
	.type	handleWaitEnd, @function
handleWaitEnd:
.LFB3:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	cmpl	$47, -12(%rbp)
	jne	.L42
	movq	-8(%rbp), %rax
	movl	$0, (%rax)
	jmp	.L45
.L42:
	cmpl	$42, -12(%rbp)
	jne	.L44
	movq	-8(%rbp), %rax
	movl	$6, (%rax)
	jmp	.L45
.L44:
	movq	-8(%rbp), %rax
	movl	$5, (%rax)
.L45:
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE3:
	.size	handleWaitEnd, .-handleWaitEnd
	.ident	"GCC: (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:
