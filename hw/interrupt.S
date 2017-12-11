.global idt_load
idt_load:
	mov 4(%esp), %eax
	lidt (%eax)
	ret

isr_common:
	/* store registers */
	pusha
	/* store segment selectors */
	mov %fs, %ax
	push %eax
	mov %ds, %ax
	push %eax
	/* switch to kernel segments */
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	/* clear DF flag */
	cld
	/* call into C */
	push %esp /* ptr to interrupt_state_t */
	call isr_handler
	add $4, %esp
.global do_iret
do_iret:
	/* switch back to original segments */
	pop %eax
	mov %ax, %ds
	mov %ax, %es
	pop %eax
	mov %ax, %fs
	mov %ax, %gs
	/* restore registers */
	popa
	/* remove int num and error code */
	add $8, %esp
	iret

.macro isr_without_errorcode num
.global isr\num
isr\num:
	push $0
	push $\num
	jmp isr_common
.endm

.macro isr_with_errorcode num
.global isr\num
isr\num:
	push $\num
	jmp isr_common
.endm

/* Interrupts thrown by the processor */
isr_without_errorcode 0
isr_without_errorcode 1
isr_without_errorcode 2
isr_without_errorcode 3
isr_without_errorcode 4
isr_without_errorcode 5
isr_without_errorcode 6
isr_without_errorcode 7
isr_with_errorcode 8
isr_without_errorcode 9
isr_with_errorcode 10
isr_with_errorcode 11
isr_with_errorcode 12
isr_with_errorcode 13
isr_with_errorcode 14
isr_without_errorcode 15
isr_without_errorcode 16
isr_with_errorcode 17
isr_without_errorcode 18
isr_without_errorcode 19
isr_without_errorcode 20
isr_without_errorcode 21
isr_without_errorcode 22
isr_without_errorcode 23
isr_without_errorcode 24
isr_without_errorcode 25
isr_without_errorcode 26
isr_without_errorcode 27
isr_without_errorcode 28
isr_without_errorcode 29
isr_without_errorcode 30
isr_without_errorcode 31

/* Interrupts thrown by the PIC */
isr_without_errorcode 32
isr_without_errorcode 33
isr_without_errorcode 34
isr_without_errorcode 35
isr_without_errorcode 36
isr_without_errorcode 37
isr_without_errorcode 38
isr_without_errorcode 39
isr_without_errorcode 40
isr_without_errorcode 41
isr_without_errorcode 42
isr_without_errorcode 43
isr_without_errorcode 44
isr_without_errorcode 45
isr_without_errorcode 46
isr_without_errorcode 47

/* Software interrupts */
isr_without_errorcode 128
