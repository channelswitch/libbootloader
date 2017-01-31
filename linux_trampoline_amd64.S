/* Absolute label macro. (The following is more data than code, really, so
 * ld's relocations are not very helpful.) */
.macro l name
.equ \name, . - .data
.endm

.data
linux_trampoline_code:
.code64
	cli
	movq	$0xf000, %rsp

	/* Find out where we are */
	call	1f
1:
	popq	%rbp
	subq	$(1b - .data), %rbp

	/* Load 32-bit flat data segments */
	leal	gdt(%rbp), %eax
	movl	%eax, gdtd_start(%rbp)
	lgdt	gdtd(%rbp)
	movl	$(gdt_data - gdt), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

	/* Disable paging */
	movq	%cr0, %rax
	andl	$0x7fffffff, %eax
	movq	%rax, %cr0

	/* Disable long mode */
	movl	$0xc0000080, %ecx
	rdmsr
	andl	$~0x10, %eax
	wrmsr

	/* Do a far return to realise the change */
	pushq	$(gdt_code - gdt)
	leaq	prot_mode(%rbp), %rcx
	pushq	%rcx
	retf
.code32
l prot_mode
	movl	params(%ebp), %esi
	movl	%ebp, %eax
	movl	$0, %ebx
	movl	$0, %ebp
	movl	$0, %edi
	jmp	*kernel(%eax)

/* GDT */

l gdtd
	.word	gdt_end - gdt - 1
l gdtd_start
	.long	0
l gdt
	/* Nulldesc */
	.quad	0
	/* Unused. Linux wants code at 0x10 */
	.quad	0
l gdt_code
	.word	0xffff		/* Limit 0:15 */
	.word	0		/* Base 0:15 */
	.byte	0		/* Base 16:23 */
	.byte	0b10011010	/* Access */
	.byte	0b11001111	/* Limit 16:19 in low 4 bits, flags in high */
	.byte	0		/* Base 24:31 */
l gdt_data
	.word	0xffff
	.word	0
	.byte	0
	.byte	0b10010010
	.byte	0b11001111
	.byte	0
l gdt_end

l kernel
	.long	0
l params
	.long	0

linux_trampoline_end:

linux_trampoline_size:
	.quad	linux_trampoline_end - linux_trampoline_code
linux_trampoline_params_offset:
	.quad	params
linux_trampoline_kernel_offset:
	.quad	kernel

.globl linux_trampoline_code
.globl linux_trampoline_size
.globl linux_trampoline_params_offset
.globl linux_trampoline_kernel_offset
