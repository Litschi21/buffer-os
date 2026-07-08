#include "elf.h"
#include "kernel.h"
#include <stdint.h>

uint64_t load_elf(void *base, uint64_t *out_top) {
	Ehdr *eh{ reinterpret_cast<Ehdr*>(base) };

	// Reject 32-bit for now
	if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' &&
			eh->e_ident[3] == 'F' && eh->e_ident[4] == 2 && eh->e_ident[5] == 1 &&
			eh->e_machine == 0x3E && eh->e_type == ET_EXEC))
		return 0;

	uint64_t top{};
	Phdr *phdr{ reinterpret_cast<Phdr*>(reinterpret_cast<uintptr_t>(base) + eh->e_phoff) };
	for (uint16_t i{}; i < eh->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;

		uint64_t start_page{ phdr[i].p_vaddr & ~0xFFF };
		uint64_t end_page  { (phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFF) & ~0xFFF };

		uint64_t *pml4{ get_pml4() };
		for (uint64_t addr{ start_page }; addr < end_page; addr += 0x1000) {
			uint64_t phys{ alloc_frame() };
			uint64_t flags{ PAGE_PRESENT | PAGE_USER };
			if (phdr[i].p_flags & PF_W) flags |= PAGE_WRITE;

			map_page(pml4, addr, phys, flags);
		}

		void *src{ reinterpret_cast<uint8_t*>(base) + phdr[i].p_offset };
		memcpy(reinterpret_cast<void*>(phdr[i].p_vaddr), src, phdr[i].p_filesz);

		uint64_t bss_addr{ phdr[i].p_vaddr + phdr[i].p_filesz };
		uint64_t bss_size{ phdr[i].p_memsz - phdr[i].p_filesz };
		for (uint64_t byte{}; byte < bss_size; ++byte)
			reinterpret_cast<uint8_t*>(bss_addr)[byte] = 0;
		
		if (end_page > top) top = end_page;
	}

	if (out_top) *out_top = top;
	return eh->e_entry;
}

task_t *create_elf_task(void *binary) {
	uint64_t top;
	uint64_t entry{ load_elf(binary, &top) };
	if (!entry) return nullptr;

	uint64_t user_stack_virt{ top + 0x1000 };
	task_t *task{ reinterpret_cast<task_t*>(kmalloc(sizeof(task_t))) };
	if (!task) return nullptr;

	void *stack_bot{ reinterpret_cast<void*>(kmalloc(THREAD_STACK_SIZE)) };
	if (!stack_bot) {
		kfree(reinterpret_cast<uint64_t>(task));
		return nullptr;
	}

	set_task_vals(task, stack_bot);
	map_page(get_pml4(), user_stack_virt, alloc_frame(), PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

	uint64_t *stack_ptr{ reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(stack_bot) + THREAD_STACK_SIZE) };
	push_stack_vals(stack_ptr, user_stack_virt, reinterpret_cast<void(*)()>(entry));
	set_ctx_regs(task, stack_ptr);

	return task;
}
