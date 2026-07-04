.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 4096
p4_table: .skip 4096
p3_table: .skip 4096
p2_table: .skip 4096
.align 16
stack_bottom: .skip 16384
stack_top:

.section .rodata
gdt64:
    .quad 0
	.set code_seg, . - gdt64
    .quad (1<<43) | (1<<44) | (1<<47) | (1<<53)
gdt64_ptr:
    .word . - gdt64 - 1
    .quad gdt64

.section .text
.global _start
.code32
_start:
    mov $stack_top, %esp

    call setup_page_tables
    call enable_paging

    lgdt (gdt64_ptr)
    jmp $code_seg, $long_mode_start

setup_page_tables:
    mov $p3_table, %eax
    or $0b11, %eax
    mov %eax, (p4_table)

    mov $p2_table, %eax
    or $0b11, %eax
    mov %eax, (p3_table)

    mov $0, %ecx
.map_p2_table:
    mov $0x200000, %eax
    mul %ecx
    or $0b10000011, %eax
    mov %eax, p2_table(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne .map_p2_table
    ret

enable_paging:
    mov $p4_table, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $(1<<5), %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $(1<<8), %eax
    wrmsr

    mov %cr0, %eax
    or $(1<<31), %eax
    mov %eax, %cr0
    ret

.code64
long_mode_start:
    mov $0, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    call kernel_main
    cli
1:  hlt
    jmp 1b
