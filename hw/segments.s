.section .text
.global gdt_load
.type gdt_load, @function
gdt_load:
	mov 4(%esp), %eax
	lgdt (%eax)

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	ljmp $0x08, $gdt_flush

gdt_flush:
	/* load tss as well */
	mov $0x2B, %ax;
	ltr %ax;

	ret
