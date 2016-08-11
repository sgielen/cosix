.section .text
.global gdt_load
.type gdt_load, @function
gdt_load:
	mov 4(%esp), %eax
	lgdt (%eax)

	/* load index 1 from the GDT into cs */
	ljmp $0x08, $gdt_flush

gdt_flush:
	/* load index 6 from the GDT into fs/gs */
	mov $0x30, %cx
	mov %cx, %fs
	mov %cx, %gs

	/* load index 2 from the GDT into all other segment registers */
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss

	/* load tss as well */
	mov $0x2B, %ax;
	ltr %ax;

	ret
