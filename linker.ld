/* Source: http://wiki.osdev.org/Bare_Bones */

ENTRY(_entrypoint)

SECTIONS
{
	. = 0xc0100000;

	.text BLOCK(4K) : AT(ADDR(.text) - 0xc0000000) ALIGN(4K)
	{
		*(.multiboot)
		*(.text*)
	}

	.rodata BLOCK(4K) : AT(ADDR(.rodata) - 0xc0000000) ALIGN(4K)
	{
		*(.rodata)
	}

	.data BLOCK(4K) : AT(ADDR(.data) - 0xc0000000) ALIGN(4K)
	{
		*(.data*)
	}

	.bss BLOCK(4K) : AT(ADDR(.bss) - 0xc0000000) ALIGN(4K)
	{
		*(COMMON)
		*(.bss*)
		*(.bootstrap_stack)
	}

	__end_of_binary = .;
}
