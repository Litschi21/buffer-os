#pragma once

#include "kernel.h"
#include <stdint.h>

struct __attribute__((packed)) Ehdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_ver;
	uint64_t e_entry;
	uint64_t e_phoff;     // Program Header Offset in File, what a name
	uint64_t e_shoff;     // Same thing, but Section Header
	uint32_t e_flags;
	uint16_t e_header_sz;
	uint16_t e_phentsz;   // Program Header Entry Size
	uint16_t e_phnum;
	uint16_t e_shentsz;   // Section Header Entry Size
	uint16_t e_shnum;
	uint16_t e_shstrndx;  // Section Header Table Entry Index (for section names)
};

struct __attribute__((packed)) Phdr {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

constexpr uint8_t PF_W   { 2 };
constexpr uint8_t PT_LOAD{ 1 };
constexpr uint8_t ET_EXEC{ 2 };

uint64_t load_elf(void *base, uint64_t *out_top);
task_t *create_elf_task(void *binary);
