#include <stdint.h>

#define KB_BUFFER_SIZE  256
#define GDT_SIZE        3
#define IDT_SIZE        256
#define PIT_FREQ        200
#define VID_BFR_ROW_LEN 160
#define MAX_ARG_CNT     16
#define VID_ROWS        25

volatile char *video{ reinterpret_cast<volatile char*>(0xB8000) };
int v_pos{};

// --- GDT SECTION ---
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


// --- IDT SECTION ---
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

extern "C" void exception_handler() {
	__asm__ __volatile__ ("cli; hlt");
}

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
}


// --- PIC SECTION ---
extern "C" void remap_pic();


// --- TIMER & KB SECTION ---
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


// --- SHELL SECTION ---
static const char *commands[]{
	"help", "clear", "echo", "uptime", "ver", "panic"
};

char curr_input[256]{};
int line_len{};

bool strEqual(const char *a, const char *b) {
	while (*a && *b) {
		if (*a != *b) return false;
		++a; ++b;
	}

	return *a == *b;
}

int strlen(const char *str) {
	int len{};
	while (str[len] != '\0') ++len;
	return len;
}

char *to_string(int n, char *buf) {
	bool neg{ n < 0 };
	if (neg) n = -n;

	int i{};
	do {
		buf[i++] = '0' + (n % 10);
		n /= 10;
	} while (n > 0);

	if (neg) buf[i++] = '-';
	buf[i] = '\0';

	for (int a{}, b{ i - 1 }; a < b; ++a, --b) {
		char tmp{ buf[a] };
		buf[a] = buf[b];
		buf[b] = tmp;
	}

	return buf;
}

void printCh(char c, bool await_input=false, bool check_cmd=true, const int color_code=0x0F);
void print(const char *msg, bool await_input=false, bool check_cmd=true, const int color_code=0x0F);
[[noreturn]] void panic(const char *msg) {
	__asm__ __volatile__("cli");

	for (int i{}; i < VID_BFR_ROW_LEN * 25; i += 2) {
		video[i]     = ' ';
		video[i + 1] = 0x1F;
	}
	v_pos = 0;

	print("*** KERNEL PANIC ***\n\n", false, true, 0x1F);
	print(msg, false, true, 0x1F);

	char buf[12];
	print("\n\nSystem halted at tick ", false, true, 0x1F);
	print(to_string(timer_ticks, buf), false, false, 0x1F);
	
	while (true) {
		__asm__ __volatile__("hlt");
	}
}

void handleCmd(const char *cmd, const char *args[MAX_ARG_CNT]) {
	if (strEqual(cmd, "help")) {
		print("\nThis is BufferOS, a small hobby OS designed for Programmers.\n", false, false);
		print("help     Prints all commands along with their descriptions.\n", false, false);
		print("clear    Clears the shell.\n");
		print("echo     Prints all given arguments.\n");
		print("uptime   Prints the uptime of the OS in seconds.\n");
		print("ver      Prints the current version of BufferOS.\n");
		print("panic    Deliberately causes a Kernel Panic.\n");
	}
	else if (strEqual(cmd, "clear")) {
		for (int i{}; i < v_pos; ++i)
			video[i] = 0;
		
		v_pos = 0;
		print("BufferOS", true, false);
	}
	else if (strEqual(cmd, "echo")) {
		for (uint64_t i{ 1 }; i < MAX_ARG_CNT && args[i] != nullptr; ++i) {
			printCh('\n', false, false);
			print(args[i], false, false);
			printCh(' ', false, false);
		}
	}
	else if (strEqual(cmd, "uptime")) {
		char buf[12];

		printCh('\n', false, false);
		print(to_string(timer_ticks / PIT_FREQ, buf), false, false);
		printCh('s', true, false);
	}
	else if (strEqual(cmd, "ver"))
		print("\nBufferOS Pre-Alpha", false, false);
	else if (strEqual(cmd, "panic")) {
		panic("Manually triggered by User.");
	}
}

void printCh(char c, const bool await_input, const bool check_cmd, const int color_code) {
	if (c == '\n') {
		if (check_cmd) {
			for (const char *cmd : commands) {
				const char *args[MAX_ARG_CNT];
				uint64_t arg_idx{};

				char word_buffer[MAX_ARG_CNT][256];
				uint64_t char_idx{};

				for (int i{}; curr_input[i] != '\0'; ++i) {
					char c{ curr_input[i] };

					if (c != ' ') {
						if (char_idx < sizeof(word_buffer[0]) - 1)
							word_buffer[arg_idx][char_idx++] = c;
					}
					else {
						if (char_idx > 0) {
							word_buffer[arg_idx][char_idx] = '\0';
							args[arg_idx] = word_buffer[arg_idx];
							++arg_idx;

							char_idx = 0;
							if (arg_idx >= MAX_ARG_CNT) break;
						}
					}
				}

				if (char_idx > 0 && arg_idx < MAX_ARG_CNT) {
					word_buffer[arg_idx][char_idx] = '\0';
					args[arg_idx] = word_buffer[arg_idx];
					++arg_idx;
				}

				if (arg_idx < MAX_ARG_CNT) {
					args[arg_idx] = nullptr;
				}

				if (arg_idx > 0 && strEqual(args[0], cmd)) {
					handleCmd(args[0], args);
					break;
				}
			}
		}

		v_pos += VID_BFR_ROW_LEN - v_pos % VID_BFR_ROW_LEN;
		if (v_pos > VID_BFR_ROW_LEN * (VID_ROWS - 1)) {
			for (int i{ 1 }; i < VID_ROWS; ++i) {
				volatile char row_buffer[VID_BFR_ROW_LEN]{};
				for (int j{}; j < VID_BFR_ROW_LEN; ++j)
					row_buffer[j] = video[i * VID_BFR_ROW_LEN + j];
				
				for (int j{}; j < VID_BFR_ROW_LEN; ++j)
					video[(i - 1) * VID_BFR_ROW_LEN + j] = row_buffer[j];
			}

			for (int j{}; j < VID_BFR_ROW_LEN; ++j)
				video[(VID_ROWS - 1) * VID_BFR_ROW_LEN + j] = 0;

			v_pos -= VID_BFR_ROW_LEN;
		}

		if (await_input) {
			print("> ", false, false);

			line_len = 0;
			for (int i{}; i < strlen(curr_input); ++i)
				curr_input[i] = '\0';
		}
	}
	else if (c == '\b') {
		if ((v_pos - 2) % VID_BFR_ROW_LEN < 3)
			return;
		else
			v_pos -= 2;

		video[v_pos + 1] = 0;
		video[v_pos]     = 0;

		if (line_len > 0) {
			--line_len;
			curr_input[line_len] = '\0';
		}
	}
	else {
		curr_input[line_len] = c;
		++line_len;
		curr_input[line_len] = '\0';

		video[v_pos]     = c;
		video[v_pos + 1] = color_code;
		v_pos += 2;
	}
}

void print(const char *msg, const bool await_input, const bool check_cmd, const int color_code) {
	for (int i{}; msg[i] != '\0'; ++i) {
		printCh(msg[i], await_input, check_cmd, color_code);
	}
}


// --- MALLOC & FREE SECTION ---
struct __attribute__((packed)) multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
};

struct __attribute__((packed)) mmap_entry {
    uint32_t size;
	uint64_t addr;
    uint64_t len;
    uint32_t type;
};

void parse_memmap(multiboot_info* mbi) {
	if (!(mbi->flags & (1 << 6)))
		panic("No memory map provided by bootloader.");

	uint32_t addr{ mbi->mmap_addr };
	uint32_t end { addr + mbi->mmap_length };

	while (addr < end) {
		mmap_entry* entry{ reinterpret_cast<mmap_entry*>(addr) };
		if (entry->type == 1) {}

		addr += entry->size + sizeof(entry->size);
	}
}

extern "C" void kernel_main(uint32_t multiboot_info_addr) {
	parse_memmap(reinterpret_cast<multiboot_info*>(multiboot_info_addr));

	init_gdt();
	remap_pic();
	init_idt();
	pit_set_freq(PIT_FREQ);
	__asm__ __volatile__ ("sti");

	print("BufferOS\n", true);
	while (true) {
		if (!kb_empty()) {
			const char c{ kb_read() };
			printCh(c, true);
		}

		__asm__ __volatile__("hlt");
	}
}
