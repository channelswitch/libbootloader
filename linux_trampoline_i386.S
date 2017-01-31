/* Absolute label macro. (The following is more data than code, really, so
 * ld's relocations are not very helpful.) */
.macro l name
.equ \name, . - .data
.endm

.data
linux_trampoline_code:
.code32
	cli
	movl	$0xf000, %esp

	/* Find out where we are. This should become a relative call. */
	call	1f
1:
	popl	%ebp
	subl	$(1b - .data), %ebp

	/* Load 32-bit flat data segments */
	leal	gdt(%ebp), %eax
	movl	%eax, gdtd_start(%ebp)
	lgdt	gdtd(%ebp)
	movl	$(gdt_data - gdt), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

	/* Disable paging */
	movl	%cr0, %eax
	andl	$0x7fffffff, %eax
	movl	%eax, %cr0

	/* Do a far return to realise the changes (maybe not neccesary) */
	pushl	$(gdt_code - gdt)
	leal	prot_mode(%ebp), %ecx
	pushl	%ecx
	retf
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
