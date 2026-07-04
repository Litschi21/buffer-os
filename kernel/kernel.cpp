#include <stdint.h>

struct gdt_entry_bits {
	uint16_t lim_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;
	uint8_t  gran;
	uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr_bits {
	uint16_t lim;
	uint64_t base;
} __attribute__((packed));

struct gdt_entry_bits gdt[3];
struct gdt_ptr_bits   gdt_ptr;

void gdt_set_gate(int32_t idx, uint32_t base, uint32_t lim, uint8_t access, uint8_t flags) {
	if (idx >= 3) return;

	gdt[idx].base_low  = (base & 0xFFFF);
	gdt[idx].base_mid  = (base >> 16) & 0xFF;
	gdt[idx].base_high = (base >> 24) & 0xFF;

	gdt[idx].lim_low   = (lim & 0xFFFF);
	
	gdt[idx].gran      = (lim >> 16) & 0x0F;
	gdt[idx].gran     |= (flags & 0xF0);
	gdt[idx].access    = access;
}


extern "C" void gdt_flush(uint64_t gdt_ptr_addr);

void init_gdt() {
	gdt_ptr.lim  = (sizeof(struct gdt_entry_bits) * 3) - 1;
	gdt_ptr.base = reinterpret_cast<uint64_t>(&gdt);

	gdt_set_gate(0, 0, 0, 0, 0);
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

	gdt_flush(reinterpret_cast<uint64_t>(&gdt_ptr));
}

extern "C" void kernel_main() {
	init_gdt();

	volatile char* video{ reinterpret_cast<volatile char*>(0xB8000) };
	const char* msg{ "Buffer OS" };
	for (int i{}; msg[i] != '\0'; ++i) {
		video[i * 2]     = msg[i];
		video[i * 2 + 1] = 0x0F;
	}

	while (true) { asm volatile("hlt"); }
}
