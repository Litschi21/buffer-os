#include <stdint.h>

#define KB_BUFFER_SIZE            256
#define GDT_SIZE                  3
#define IDT_SIZE                  256
#define PIT_FREQ                  200
#define VID_BFR_ROW_LEN           160
#define MAX_ARG_CNT               16
#define VID_ROWS                  25
#define INIT_HEAP_FRAMES          1000
#define MIN_MALLOC_SIZE           40
#define THREAD_STACK_SIZE         4096

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
extern "C" void timer_handler() { outb(0x20, 0x20); timer_ticks += 1; }

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

char *to_string(int64_t n, char *buf) {
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

void printCh(char c, bool await_input=false, const int color_code=0x0F);
void print(const char *msg, bool await_input=false, const int color_code=0x0F);
[[noreturn]] void panic(const char *msg) {
	__asm__ __volatile__("cli");

	for (int i{}; i < VID_BFR_ROW_LEN * 25; i += 2) {
		video[i]     = ' ';
		video[i + 1] = 0x1F;
	}
	v_pos = 0;

	print("*** KERNEL PANIC ***\n\n", false, 0x1F);
	print(msg, false, 0x1F);

	char buf[12];
	print("\n\nSystem halted at tick ", false, 0x1F);
	print(to_string(timer_ticks, buf), false, 0x1F);
	
	while (true) {
		__asm__ __volatile__("hlt");
	}
}

void handleCmd(const char *cmd, const char *args[MAX_ARG_CNT]) {
	if (strEqual(cmd, "help")) {
		print("This is BufferOS, a small hobby OS designed for Programmers.\n");
		print("help     Prints all commands along with their descriptions.\n");
		print("clear    Clears the shell.\n");
		print("echo     Prints all given arguments.\n");
		print("uptime   Prints the uptime of the OS in seconds.\n");
		print("ver      Prints the current version of BufferOS.\n");
		print("panic    Deliberately causes a Kernel Panic.\n\n");
	}
	else if (strEqual(cmd, "clear")) {
		for (int i{}; i < v_pos; ++i)
			video[i] = 0;
		
		v_pos = 0;
		print("BufferOS\n");
	}
	else if (strEqual(cmd, "echo")) {
		for (uint64_t i{ 1 }; i < MAX_ARG_CNT && args[i] != nullptr; ++i) {
			print(args[i]);
			printCh('\n');
		}
	}
	else if (strEqual(cmd, "uptime")) {
		char buf[12];

		print(to_string(timer_ticks / PIT_FREQ, buf));
		print("s\n");
	}
	else if (strEqual(cmd, "ver"))
		print("BufferOS Pre-Alpha\n");
	else if (strEqual(cmd, "panic")) {
		panic("Manually triggered by User.");
	}
}

void parseCmd() {
	const char *args[MAX_ARG_CNT];
	char word_buffer[MAX_ARG_CNT][256];
	uint64_t arg_idx{};
	uint64_t char_idx{};

	for (int i{}; curr_input[i] != '\0'; ++i) {
		char curr_ch{ curr_input[i] };

		if (curr_ch != ' ') {
			if (char_idx < sizeof(word_buffer[0]) - 1)
				word_buffer[arg_idx][char_idx++] = curr_ch;
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

	if (arg_idx == 0) return;

	bool found{ false };
	for (const char *cmd : commands) {
		if (strEqual(args[0], cmd)) {
			handleCmd(args[0], args);
			found = true;
			break;
		}
	}

	if (!found) {
		printCh('\'', false);
		print(args[0], false);
		print("' does not match any command. Check 'help' to see all commands.\n", false);
	}
}

void printCh(char c, const bool await_input, const int color_code) {
	if (c == '\n') {
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
			print("> ", false);
		}
	}
	else if (c == '\b') {
		if ((v_pos - 2) % VID_BFR_ROW_LEN < 3)
			return;
		else
			v_pos -= 2;

		video[v_pos]     = ' ';
		video[v_pos + 1] = 0x0F;

		if (line_len > 0) {
			--line_len;
			curr_input[line_len] = '\0';
		}
	}
	else {
		video[v_pos]     = c;
		video[v_pos + 1] = color_code;
		v_pos += 2;
	}
}

void print(const char *msg, const bool await_input, const int color_code) {
	for (int i{}; msg[i] != '\0'; ++i) {
		printCh(msg[i], await_input, color_code);
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

struct chunk_header {
	uint64_t size;
	bool     is_free;
};

struct chunk_footer {
	uint64_t size;
};

static uint64_t highest_addr{};
static uint64_t total_frames;
static uint64_t bitmap_size_bytes;
static uint64_t heap_start;
static uint64_t heap_end;

extern uint8_t _kernel_end;
uint8_t* bitmap{ &_kernel_end };

void parse_memmap(multiboot_info* mbi) {
	if (!(mbi->flags & (1 << 6)))
		panic("No memory map provided by bootloader.");

	uint32_t addr{ mbi->mmap_addr };
	uint32_t end { addr + mbi->mmap_length };

	while (addr < end) {
		mmap_entry* entry{ reinterpret_cast<mmap_entry*>(addr) };
		if (entry->type == 1) {
			uint64_t entry_end{ entry->addr + entry->len };
			if (entry_end > highest_addr)
				highest_addr = entry_end;
		}
		
		addr += entry->size + sizeof(entry->size);
	}

	total_frames =      highest_addr / 4096;
	bitmap_size_bytes = total_frames / 8;
}

uint64_t alloc_frame() {
	for (uint64_t frame{}; frame < total_frames; ++frame) {
		uint64_t byte_idx{ frame / 8 };
		uint8_t  bit_idx { static_cast<uint8_t>(frame % 8) };
		
		uint8_t bit_val{ static_cast<uint8_t>((bitmap[byte_idx] >> bit_idx) & 1) };
		if (bit_val == 0) {
			bitmap[byte_idx] |= (1 << bit_idx);
			return frame * 4096;
		}
	}

	panic("Out of physical memory");
}

void free_frame(const uint64_t addr) {
	uint64_t frame{ addr / 4096 };
	uint64_t byte_idx{ frame / 8 };
	uint8_t  bit_idx { static_cast<uint8_t>(frame % 8) };

	if (!((bitmap[byte_idx] >> bit_idx) & 1))
		panic("Attempted to double free memory");

	bitmap[byte_idx] &= ~(1 << bit_idx);
}

uint64_t next_header(const uint64_t size) { return size + sizeof(chunk_header); }
void write_footer(const uint64_t header_addr, const uint64_t size) {
	chunk_footer *f{ reinterpret_cast<chunk_footer*>(size + header_addr + sizeof(chunk_header)) };
	f->size = size;
}

void init_bitmap() {
	for (uint64_t i{}; i < bitmap_size_bytes; ++i) {
		bitmap[i] = 0;
	}

	uint64_t bitmap_end_addr   { reinterpret_cast<uint64_t>(&_kernel_end) + bitmap_size_bytes };
	uint64_t total_reserved    { (bitmap_end_addr + 4095) / 4096 };

	for (uint64_t frame{}; frame < total_reserved; ++frame) {
		bitmap[frame / 8] |= (1 << (frame % 8));
	}
}

void init_heap() {
	heap_start = alloc_frame();
	for (int i{}; i < INIT_HEAP_FRAMES - 1; ++i) {
		alloc_frame();
	}

	heap_end = heap_start + INIT_HEAP_FRAMES * 4096;
	chunk_header *first{ reinterpret_cast<chunk_header*>(heap_start) };
	first->size = (INIT_HEAP_FRAMES * 4096) - sizeof(chunk_header);
	first->is_free = true;

	write_footer(heap_start, first->size);
}

uint64_t kmalloc(const uint64_t size) {
	uint64_t addr{ heap_start };
	while (addr < heap_end) {
		chunk_header *curr{ reinterpret_cast<chunk_header*>(addr) };
		if (!curr->is_free) {
			addr += sizeof(chunk_footer) + next_header(curr->size);
			continue;
		}

		if (curr->size >= size) {
			if (curr->size - size < MIN_MALLOC_SIZE) {
				curr->is_free = false;
				return addr + sizeof(chunk_header);
			}
			else {
				uint64_t new_size{ curr->size - size - sizeof(chunk_header) - sizeof(chunk_footer) };
				curr->size = size;

				uint64_t new_header_addr{ addr + next_header(curr->size) + sizeof(chunk_footer) };
				chunk_header *new_header{ reinterpret_cast<chunk_header*>(new_header_addr) };
				new_header->size = new_size;
				new_header->is_free = true;

				uint64_t new_footer_addr{ new_header_addr + next_header(new_header->size) };
				chunk_footer *new_footer{ reinterpret_cast<chunk_footer*>(new_footer_addr) };
				new_footer->size = new_size;

				chunk_footer *curr_footer{ reinterpret_cast<chunk_footer*>(addr + curr->size + sizeof(chunk_header)) };
				curr_footer->size = curr->size;

				curr->is_free = false;
				return addr + sizeof(chunk_header);
			}
		}

		addr += next_header(curr->size);
	}

	panic("Out of heap space");
}

void kfree(const uint64_t addr) {
	chunk_header *header{ reinterpret_cast<chunk_header*>(addr - sizeof(chunk_header)) };
	if (header->is_free)
		panic("Attempted to double free heap space");
	else {
		header->is_free = true;
		
		uint64_t next_addr{ addr + header->size + sizeof(chunk_footer) };
		if (next_addr < heap_end) {
			chunk_header *next{ reinterpret_cast<chunk_header*>(next_addr) };
			if (next->is_free) {
				header->size += next->size + sizeof(chunk_header) + sizeof(chunk_footer);
				chunk_footer *new_f{ reinterpret_cast<chunk_footer*>(addr + header->size) };
				new_f->size = header->size;
			}
		}

		uint64_t prev_f_addr{ addr - sizeof(chunk_header) - sizeof(chunk_footer) };
		if (prev_f_addr <= heap_start) return;

		chunk_footer *prev_f{ reinterpret_cast<chunk_footer*>(prev_f_addr) };
		uint64_t prev_addr{ prev_f_addr - prev_f->size - sizeof(chunk_header) };

		chunk_header *prev{ reinterpret_cast<chunk_header*>(prev_addr) };
		if (prev->is_free) {
			prev->size = prev->size + next_header(header->size) + sizeof(chunk_footer);
			chunk_footer *new_prev_f{ reinterpret_cast<chunk_footer*>(addr + header->size) };
			new_prev_f->size = prev->size;
		}
	}
}


// Kernel-mode Structs & Scheduling
int next_pid{};

struct cpu_context {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t rsp;
    uint64_t rip;
    uint64_t rflags;
};

struct task_t {
	int pid;
	uint8_t state;
	void *stack_top;
	struct cpu_context ctx;
	struct task_t *next;
};

task_t *create_task(void (*entry_point)()) {
	task_t *task{ reinterpret_cast<task_t*>(kmalloc(sizeof(task_t))) };
	if (!task) return nullptr;

	void *stack_bot{ reinterpret_cast<void*>(kmalloc(THREAD_STACK_SIZE)) };
	if (!stack_bot) {
		kfree(reinterpret_cast<uint64_t>(task));
		return nullptr;
	}
	
	task->stack_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(stack_bot) + THREAD_STACK_SIZE);
	task->pid = next_pid++;
	task->state = 0;
	task->next = nullptr;

	task->ctx.rip = reinterpret_cast<uintptr_t>(entry_point);
	task->ctx.rsp = reinterpret_cast<uintptr_t>(task->stack_top);

	task->ctx.rflags = 0x0202;

	task->ctx.rax = 0;
    task->ctx.rbx = 0;
    task->ctx.rcx = 0;
    task->ctx.rdx = 0;
    task->ctx.rsi = 0;
    task->ctx.rdi = 0;
    task->ctx.rbp = reinterpret_cast<uintptr_t>(task->stack_top);

	return task;
}

extern "C" void switch_context(struct cpu_context *old_ctx, struct cpu_context *new_ctx);

extern "C" void kernel_main(uint32_t multiboot_info_addr) {
	parse_memmap(reinterpret_cast<multiboot_info*>(multiboot_info_addr));
	init_bitmap();
	init_heap();

	init_gdt();
	remap_pic();
	init_idt();
	pit_set_freq(PIT_FREQ);
	__asm__ __volatile__ ("sti");

	print("BufferOS\n", true);
	while (true) {
		if (!kb_empty()) {
			const char c{ kb_read() };
			if (c == '\n') {
				printCh('\n');
				curr_input[line_len] = '\0';
				parseCmd();

				for (int i{}; i < 256; ++i) {
					curr_input[i] = '\0';
				}

				line_len = 0;
				print("> ");
			}
			else if (c == '\b')
				printCh('\b');
			else {
				if (line_len < 255) {
					curr_input[line_len++] = c;
				}

				printCh(c);
			}
		}

		__asm__ __volatile__("hlt");
	}
}
