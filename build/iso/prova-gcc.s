	.file	"prova-gcc.c"
	.text
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"La catena intera dentro %s\n\n"
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align 4
.LC1:
	.string	"  somma dei quadrati 1..10 : %lld   (atteso 385)\n"
	.align 4
.LC2:
	.string	"  lunghezza del nome       : %d     (atteso 5)\n"
	.align 4
.LC3:
	.string	"  divisione a 64 bit       : %lld   (atteso 64)\n"
	.align 4
.LC4:
	.string	"\nCompilato, assemblato e collegato qui dentro."
	.section	.text.startup,"ax",@progbits
	.p2align 2
	.globl	main
	.type	main, @function
main:
	leal	4(%esp), %ecx
	andl	$-16, %esp
	pushl	-4(%ecx)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ecx
	subl	$28, %esp
	movl	saluto, %eax
	movl	%eax, -24(%ebp)
	movl	saluto+4, %eax
	movw	%ax, -20(%ebp)
	leal	-24(%ebp), %eax
	pushl	%eax
	pushl	$.LC0
	call	printf
	addl	$12, %esp
	pushl	$0
	pushl	$385
	pushl	$.LC1
	call	printf
	popl	%eax
	popl	%edx
	pushl	$5
	pushl	$.LC2
	call	printf
	addl	$12, %esp
	pushl	$0
	pushl	$64
	pushl	$.LC3
	call	printf
	movl	$.LC4, (%esp)
	call	puts
	xorl	%eax, %eax
	movl	-4(%ebp), %ecx
	leave
	leal	-4(%ecx), %esp
	ret
	.size	main, .-main
	.section	.rodata
	.align 4
	.type	saluto, @object
	.size	saluto, 6
saluto:
	.string	"EX-OS"
	.ident	"GCC: (GNU) 17.0.0 20260801 (experimental)"
	.section	.note.GNU-stack,"",@progbits
