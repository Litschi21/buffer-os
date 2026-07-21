ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

PHDRS
{
	text   PT_LOAD FLAGS(5);
	rodata PT_LOAD FLAGS(4);
	data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;
    .text   BLOCK(4K) : ALIGN(4K) { *(.multiboot) *(.text) } : text
    .rodata BLOCK(4K) : ALIGN(4K) { *(.rodata) }             : rodata
    .data   BLOCK(4K) : ALIGN(4K) { *(.data) }               : data
    .bss    BLOCK(4K) : ALIGN(4K) { *(.bss) }                : data
	_kernel_end = .;
}
