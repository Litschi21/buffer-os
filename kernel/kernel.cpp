#include <stdint.h>

#define KB_BUFFER_SIZE 256
#define GDT_SIZE       3
#define IDT_SIZE       256

struct __attribute__((packed)) gdt_entry {
	uint16_t lim_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;
	uint8_t  gran;
	uint8_t  base_high;
};

struct __attribute__((packed)) gdt_ptr {
	uint16_t lim;
	uint64_t base;
};

struct gdt_entry gdt[GDT_SIZE];
struct gdt_ptr   gdt_ptr;


struct __attribute__((packed)) idt_entry {
	uint16_t isr_low;
	uint16_t kernel_cs;
	uint8_t  ist;
	uint8_t  attributes;
	uint16_t isr_mid;
	uint32_t isr_high;
	uint32_t reserved;
};

struct __attribute__((packed)) idtr_t {
	uint16_t lim;
	uint64_t base;
};

static idt_entry idt[IDT_SIZE];
static idtr_t    idtr;


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
	gdt_ptr.lim  = (sizeof(struct gdt_entry) * 3) - 1;
	gdt_ptr.base = reinterpret_cast<uint64_t>(&gdt);

	gdt_set_gate(0, 0, 0, 0, 0);
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

	gdt_flush(reinterpret_cast<uint64_t>(&gdt_ptr));
}

extern "C" void exception_handler() {
	__asm__ __volatile__ ("cli; hlt");
}

extern "C" void remap_pic();
void idt_set_descriptor(uint8_t v, void *isr, uint8_t flags) {
	idt_entry *descriptor{ &idt[v] };

	descriptor->isr_low    = reinterpret_cast<uint64_t>(isr) & 0xFFFF;
	descriptor->kernel_cs  = 0x08;
	descriptor->ist        = 0;
	descriptor->attributes = flags;
	descriptor->isr_mid    = (reinterpret_cast<uint64_t>(isr) >> 16) & 0xFFFF;
	descriptor->isr_high   = (reinterpret_cast<uint64_t>(isr) >> 32) & 0xFFFFFFFF;
	descriptor->reserved   = 0;
}

static bool vectors[IDT_SIZE];
extern "C" void *isr_stub_table[];

extern "C" void timer_isr();
extern "C" void keyboard_isr();

void init_idt() {
	idtr.lim  = static_cast<uint16_t>(sizeof(idt_entry) * IDT_SIZE - 1);
	idtr.base = reinterpret_cast<uintptr_t>(&idt[0]);

	for (uint8_t v{}; v < 32; ++v) {
		idt_set_descriptor(v, isr_stub_table[v], 0x8E);
		vectors[v] = true;
	}

	__asm__ __volatile__ ("lidt %0" : : "m"(idtr));
	
	idt_set_descriptor(32, reinterpret_cast<void*>(timer_isr), 0x8E);
	idt_set_descriptor(33, reinterpret_cast<void*>(keyboard_isr), 0x8E);

	__asm__ __volatile__ ("sti");
}

static inline void outb(uint16_t port, uint8_t val) {
	__asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	__asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

volatile uint64_t timer_ticks{};
extern "C" void timer_handler() { outb(0x20, 0x20); ++timer_ticks; }

void pit_set_freq(uint32_t freq) {
	uint32_t div{ 1'193'182 / freq };

	outb(0x43, 0x36);
	outb(0x40, div & 0xFF);
	outb(0x40, (div >> 8) & 0xFF);
}

void sleep(uint64_t ms) {
	uint64_t target{ timer_ticks + (ms / 10) };
	while (timer_ticks < target)
		__asm__ __volatile__("hlt");
}

static const char scancode_ascii[128]{
	0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0
};

static char kb_buffer[KB_BUFFER_SIZE];
static volatile int kb_head{};
static volatile int kb_tail{};

bool kb_empty() { return kb_head == kb_tail; }
void kb_push(const char c) {
	int next{ (kb_head + 1) % KB_BUFFER_SIZE };
	if (next != kb_tail) {
		kb_buffer[kb_head] = c;
		kb_head = next;
	}
}

char kb_read() {
	char c{ kb_buffer[kb_tail] };
	kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
	return c;
}

extern "C" void keyboard_handler() {
	uint8_t scancode{ inb(0x60) };
	if (!(scancode & 0x80)) {
		char c{ scancode_ascii[scancode & 0x7F] };
		if (c != 0)
			kb_push(c);
	}

	outb(0x20, 0x20);
}

extern "C" void kernel_main() {
	init_gdt();
	remap_pic();
	init_idt();
	pit_set_freq(200);

	volatile char* video{ reinterpret_cast<volatile char*>(0xB8000) };
	const char* msg{ "Buffer OS" };
	for (int i{}; msg[i] != '\0'; ++i) {
		video[i * 2]     = msg[i];
		video[i * 2 + 1] = 0x0F;
	}

	while (true) {
		__asm__ __volatile__("hlt");
	}
}
