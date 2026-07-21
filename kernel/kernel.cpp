#include "ahci.hpp"
#include "elf.hpp"
#include "fat32.hpp"
#include "kernel.hpp"
#include <stdint.h>

volatile char *video{ reinterpret_cast<volatile char*>(0xB8000) };
int v_pos{};

struct tss_entry tss;
struct gdt_entry gdt[GDT_SIZE];
struct gdt_ptr   gdt_ptr;

void gdt_set_tss(int32_t idx, uint64_t base, uint32_t lim) {
	uint8_t access{ 0x89 };
	uint8_t flags { 0x00 };

	gdt[idx].lim_low   = lim  & 0xFFFF;
	gdt[idx].base_low  = base & 0xFFF;
	gdt[idx].base_mid  = (base >> 16) & 0xFF;
	gdt[idx].base_high = (base >> 24) & 0xFF;
	
	gdt[idx].access   = access;
	gdt[idx].gran     = ((lim >> 16) & 0x0F) | (flags & 0x0F);

	uint32_t *upper_gdt_entry{ reinterpret_cast<uint32_t*>(&gdt[idx + 1]) };
	upper_gdt_entry[0] = base >> 32;
	upper_gdt_entry[1] = 0;
}

void gdt_set_gate(int32_t idx, uint32_t base, uint32_t lim, uint8_t access, uint8_t flags) {
	if (idx >= GDT_SIZE) return;

	gdt[idx].base_low  = (base & 0xFFFF);
	gdt[idx].base_mid  = (base >> 16) & 0xFF;
	gdt[idx].base_high = (base >> 24) & 0xFF;

	gdt[idx].lim_low   = (lim & 0xFFFF);
	
	gdt[idx].access    = access;
	gdt[idx].gran      = ((lim >> 16) & 0x0F) | (flags & 0xF0);
}

void init_gdt() {
	gdt_ptr.lim  = (sizeof(struct gdt_entry) * GDT_SIZE) - 1;
	gdt_ptr.base = reinterpret_cast<uint64_t>(&gdt);

	gdt_set_gate(0, 0, 0, 0, 0);
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, CODE_SEG);
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, DATA_SEG);

	gdt_set_gate(3, 0, 0XFFFFFFFF, 0xFA, CODE_SEG);
	gdt_set_gate(4, 0, 0XFFFFFFFF, 0xF2, DATA_SEG);

	gdt_set_gate(5, 0, 0xFFFFFFFF, 0xFA, CODE_SEG);

	gdt_set_tss(6, reinterpret_cast<uint64_t>(&tss), sizeof(tss_entry) - 1);
	gdt_flush(reinterpret_cast<uint64_t>(&gdt_ptr));
	__asm__ __volatile__("ltr %%ax" :: "a"(TSS_DESCRIPTOR));
}


// --- IDT SECTION ---
static bool      vectors[IDT_SIZE];
static idt_entry idt[IDT_SIZE];
static idtr_t    idtr;

extern "C" void exception_handler(uint64_t *frame) {
	uint64_t error_code{ frame[15] };
	uint64_t rip       { frame[16] };
	uint64_t cs        { frame[17] };

	if ((cs & 3) == 3) {
		print("EXITING TASK");
		task_exit();
		return;
	}

	for (int i{}; i < VID_BFR_ROW_LEN * VID_ROWS; i += 2) {
		video[i]      = ' ';
		video[i + 1]  = 0x1F;
	}
	
	uint64_t faulty_addr;
	__asm__ __volatile__("mov %%cr2, %0" : "=r"(faulty_addr));

	char buf[20];
	print("*** EXCEPTION CALLED FROM RING 0 ***\n", false, 0x1F);

	print("Address 0x", false, 0x1F);
	print(to_string(rip, buf), false, 0x1F);
	printCh('\n');

	print("Faulty Address 0x", false, 0x1F);
	print(to_string(faulty_addr, buf), false, 0x1F);
	printCh('\n');

	print("Error Code ", false, 0x1F);
	print(to_string(error_code, buf), false, 0x1F);
	printCh('\n');

	print(to_string(cs, buf), false, 0x1F);

	__asm__ __volatile__ ("cli; hlt");
}

void idt_set_descriptor(uint8_t v, void *isr, uint8_t flags) {
	idt_entry *descriptor{ &idt[v] };

	descriptor->isr_low    = reinterpret_cast<uint64_t>(isr) & 0xFFFF;
	descriptor->kernel_cs  = KERNEL_CS;
	descriptor->ist        = 0;
	descriptor->attributes = flags;
	descriptor->isr_mid    = (reinterpret_cast<uint64_t>(isr) >> 16) & 0xFFFF;
	descriptor->isr_high   = (reinterpret_cast<uint64_t>(isr) >> 32) & 0xFFFFFFFF;
	descriptor->reserved   = 0;
}

void init_idt() {
	idtr.lim  = static_cast<uint16_t>(sizeof(idt_entry) * IDT_SIZE - 1);
	idtr.base = reinterpret_cast<uintptr_t>(&idt[0]);

	for (uint8_t v{}; v < 32; ++v) {
		idt_set_descriptor(v, isr_stub_table[v], IDT_INT_GATE);
		vectors[v] = true;
	}

	__asm__ __volatile__ ("lidt %0" : : "m"(idtr));
	
	idt_set_descriptor(32, reinterpret_cast<void*>(timer_isr), IDT_INT_GATE);
	idt_set_descriptor(33, reinterpret_cast<void*>(keyboard_isr), IDT_INT_GATE);
}


// --- TIMER & KB SECTION ---
void outb(uint16_t port, uint8_t val) {
	__asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
	uint8_t ret;
	__asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(uint16_t port, uint16_t val) {
	__asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

uint16_t inw(uint16_t port) {
	uint16_t ret;
	__asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outl(uint16_t port, uint32_t val) {
	__asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t inl(uint16_t port) {
	uint32_t ret;
	__asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
	outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC));
	return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
	outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC));
	outl(0xCFC, val);
}

void pci_scan() {
	for (uint8_t dev{}; dev < 32; ++ dev) {
		for (uint8_t func{}; func < 8; ++func) {
			uint32_t ven{ pci_read(0, dev, func, 0) & 0xFFFF };
			if (ven == 0xFFFF) continue;

			uint32_t pci     { pci_read(0, dev, func, 8) };
			uint32_t clss    {  pci >> 24 };
			uint32_t subclass{ (pci >> 16) & 0xFF };
			uint32_t prog_if { (pci >>  8) & 0xFF };
			
			if (clss == 0x01 && subclass == 0x06 && prog_if == 0x01) {
				if (ahci_init(pci_read(0, dev, func, 0x24)))
					fat32_init();
			}
		}
	}
}

volatile uint64_t timer_ticks{};
extern "C" void timer_handler() {
	outb(0x20, 0x20);
	timer_ticks += 1;
	schedule();
}

void pit_set_freq(uint32_t freq) {
	uint32_t div{ 1'193'182 / freq };

	outb(0x43, 0x36);
	outb(0x40, div & 0xFF);
	outb(0x40, (div >> 8) & 0xFF);
}

void sleep(uint64_t ms) {
	uint64_t target{ timer_ticks + (ms / (1000 / PIT_FREQ)) };
	while (timer_ticks < target)
		__asm__ __volatile__("hlt");
}

static const char scancode_ascii[128] {
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
	"help", "clear", "echo", "uptime", "ver", "ls", "panic"
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

void panic(const char *msg) {
	__asm__ __volatile__("cli");

	for (int i{}; i < VID_BFR_ROW_LEN * 25; i += 2) {
		video[i]     = ' ';
		video[i + 1] = 0x1F;
	}
	v_pos = 0;

	print("*** KERNEL PANIC ***\n\n", false, 0x1F);
	print(msg, false, 0x1F);

	char buf[MAX_64_BIT_DIG];
	print("\n\nSystem halted at tick ", false, 0x1F);
	print(to_string(timer_ticks, buf), false, 0x1F);
	
	while (true) {
		__asm__ __volatile__("hlt");
	}
}

void handleCmd(const char *cmd, const char *args[MAX_ARG_CNT]) {
	if (strEqual(cmd, "help")) {
		print("This is BufferOS, a small hobby OS designed for Programmers.\n");
		print("help      Prints all commands along with their descriptions.\n");
		print("clear     Clears the shell.\n");
		print("echo      Prints all given arguments.\n");
		print("uptime    Prints the uptime of the OS in seconds.\n");
		print("ver       Prints the current version of BufferOS.\n");
		print("ls        Prints \n");
		print("panic     Deliberately causes a Kernel Panic.\n");
	} else if (strEqual(cmd, "clear")) {
		for (int i{}; i < v_pos; ++i)
			video[i] = 0;
		
		v_pos = 0;
		print("BufferOS\n");
	} else if (strEqual(cmd, "echo")) {
		for (uint64_t i{ 1 }; i < MAX_ARG_CNT && args[i] != nullptr; ++i) {
			print(args[i]);
			printCh('\n');
		}
	} else if (strEqual(cmd, "uptime")) {
		char buf[MAX_64_BIT_DIG];
		print(to_string(timer_ticks / PIT_FREQ, buf));
		print("s\n");
	} else if (strEqual(cmd, "ver"))
		print("BufferOS Pre-Alpha\n");
	else if (strEqual(cmd, "panic"))
		panic("Manually triggered by User.");
	else if (strEqual(cmd, "ls"))
		read_dir(partitions[0].bpb.cls_num);

	if (!strEqual(cmd, "clear"))
		printCh('\n');
}

void parseCmd() {
	const char *args[MAX_ARG_CNT]{};
	char word_buffer[MAX_ARG_CNT][256];
	uint64_t arg_idx{};
	uint64_t char_idx{};

	for (int i{}; curr_input[i] != '\0'; ++i) {
		char curr_ch{ curr_input[i] };

		if (curr_ch != ' ') {
			if (char_idx < sizeof(word_buffer[0]) - 1)
				word_buffer[arg_idx][char_idx++] = curr_ch;
		} else {
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
		print("' does not match any command. Check 'help' to see all commands.\n\n", false);
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
	} else if (c == '\b') {
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
	} else {
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

void shell() {
	uint16_t cs_val;
	__asm__ __volatile__("mov %%cs, %0" : "=r"(cs_val));

	__asm__ __volatile__("sti");

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
			} else if (c == '\b')
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


// --- MALLOC & FREE SECTION ---
static uint64_t highest_addr;
static uint64_t total_frames;
static uint64_t bitmap_size_bytes;
static uint64_t heap_start;
static uint64_t heap_end;

extern uint8_t _kernel_end;
uint8_t *bitmap{ &_kernel_end };
AHCI_INFO drives[32];

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

	total_frames      = highest_addr / 4096;
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

uint64_t next_header(const uint64_t size) { return size + sizeof(chunk_header) + sizeof(chunk_footer); }
chunk_footer *write_footer(const uint64_t header_addr, const uint64_t size) {
	const uint64_t f_addr{ header_addr + size + sizeof(chunk_header) };
	chunk_footer *f{ reinterpret_cast<chunk_footer*>(f_addr) };
	f->size = size;

	return f;
}

chunk_header *write_header(const uint64_t addr, const uint64_t size, const bool is_free) {
	chunk_header *h{ reinterpret_cast<chunk_header*>(addr) };
	h->size    = size;
	h->is_free = is_free;

	return h;
}

void init_bitmap() {
	for (uint64_t i{}; i < bitmap_size_bytes; ++i)
		bitmap[i] = 0;

	uint64_t bitmap_end_addr   { reinterpret_cast<uint64_t>(&_kernel_end) + bitmap_size_bytes };
	uint64_t total_reserved    { (bitmap_end_addr + 4095) / 4096 };

	for (uint64_t frame{}; frame < total_reserved; ++frame)
		bitmap[frame / 8] |= (1 << (frame % 8));
}

void init_heap() {
	heap_start = alloc_frame();
	for (int i{}; i < INIT_HEAP_FRAMES - 1; ++i)
		alloc_frame();

	heap_end = heap_start + INIT_HEAP_FRAMES * 4096;

	const uint64_t size{ (INIT_HEAP_FRAMES * 4096) - sizeof(chunk_header) - sizeof(chunk_footer) };
	write_header(heap_start, size, true);
	write_footer(heap_start, size);
}

uint64_t kmalloc(const uint64_t size, const uint64_t alignment) {
	uint64_t addr{ heap_start };
	while (addr < heap_end) {
		chunk_header *curr{ reinterpret_cast<chunk_header*>(addr) };
		if (!curr->is_free) {
			addr += next_header(curr->size);
			continue;
		}

		// Set return address to the address of usable space and add padding to size
		uint64_t return_addr{ addr + sizeof(chunk_header) };
		uint64_t padding    { return_addr % alignment };
		curr->size += padding;

		if (curr->size >= size) {
			if (curr->size - size < MIN_MALLOC_SIZE) {
				curr->is_free = false;
				return return_addr;
			} else {
				// Segment into 2 pieces, first is what we'll use, second is leftovers

				// First we take the new size, which is the size of the entire chunk (curr->size)
				// - the size we're going to keep - the header the leftover piece is going to use
				// - the footer of our piece
				uint64_t new_size{ curr->size - size - sizeof(chunk_header) - sizeof(chunk_footer) };
				curr->size = size;

				// The new header is placed after our current piece + our header and footer
				uint64_t new_header_addr{ addr + next_header(curr->size) };
				chunk_header *new_header{ write_header(new_header_addr, new_size, true) };

				// New footer is placed at the header address + header size + size
				// Remove footer since next_header adds it
				uint64_t new_footer_addr{ new_header_addr + next_header(new_header->size) - sizeof(chunk_footer) };
				write_footer(new_footer_addr, new_size);

				// Our footer is placed at the address + our size + the size of the header
				uint64_t curr_footer_addr{ addr + curr->size + sizeof(chunk_header) };
				write_footer(curr_footer_addr, curr->size);

				curr->is_free = false;
				return return_addr;
			}
		}

		addr += next_header(curr->size);
	}

	panic("Out of heap space");
}

void kfree(const uint64_t addr) {
	// Subtract size of chunk header from addr to get the header
	chunk_header *curr{ reinterpret_cast<chunk_header*>(addr - sizeof(chunk_header)) };
	if (curr->is_free)
		panic("Attempted to double free heap space");

	curr->is_free = true;
	uint64_t next_addr{ addr + next_header(curr->size) };
	if (next_addr < heap_end) {
		chunk_header *next{ reinterpret_cast<chunk_header*>(next_addr) };
		if (next->is_free) {
			curr->size += next->size + sizeof(chunk_header) + sizeof(chunk_footer);
			write_footer(addr + curr->size, curr->size);
		}
	}

	uint64_t prev_f_addr{ addr - sizeof(chunk_header) - sizeof(chunk_footer) };
	if (prev_f_addr <= heap_start) return;

	chunk_footer *prev_f{ reinterpret_cast<chunk_footer*>(prev_f_addr) };
	uint64_t prev_addr{ prev_f_addr - prev_f->size - sizeof(chunk_header) };

	chunk_header *prev{ reinterpret_cast<chunk_header*>(prev_addr) };
	if (prev->is_free) {
		prev->size += curr->size + sizeof(chunk_header) + sizeof(chunk_footer);
		write_footer(addr + curr->size, prev->size);
	}
}

void memcpy(void *dest, const void *src, const uint64_t n) {
	for (uint64_t i{}; i < n; ++i)
		reinterpret_cast<volatile uint8_t*>(dest)[i] = reinterpret_cast<const uint8_t*>(src)[i];
}

void memset(void *addr, const uint64_t val, const uint64_t count) {
	for (uint64_t i{}; i < count; ++i)
		reinterpret_cast<volatile uint8_t*>(addr)[i] = val;
}


// From Syscalls
struct cpu_data cpu_data;

// Scheduling
int next_pid{};
task_t *curr_task{};
task_t *task_list_head{};
task_t *task_list_tail{};
task_t *idle_task{};

void idle_task_func() {
	__asm__ __volatile__("sti");
	while (true)
		__asm__ __volatile__("hlt");
}

task_t *get_next_task() {
	if (curr_task && curr_task->next)
		return curr_task->next;

	return task_list_head;
}

void schedule() {
	if (!task_list_head) return;

	task_t *old_task{ curr_task };
	task_t *next_task{ get_next_task() };
	while (next_task->state == TASK_STATE_DEAD) {
		curr_task = next_task;
		next_task = get_next_task();

		if (next_task == old_task && next_task->state == TASK_STATE_DEAD) {
			next_task = idle_task;
			break;
		}
	}

	curr_task = next_task;
	uint64_t stack_top{ reinterpret_cast<uint64_t>(next_task->stack_top) + THREAD_STACK_SIZE };
	cpu_data.kernel_rsp = stack_top;
	tss.rsp0 = stack_top;

	if (!old_task) {
		swtch_ctx(nullptr, &(next_task->ctx));
	} else {
		if (old_task->state == TASK_STATE_RUNNING)
			old_task->state = TASK_STATE_READY;

		swtch_ctx(&(old_task->ctx), &(next_task->ctx));
	}

	__asm__ __volatile__("sti");
}

void task_exit() {
	curr_task->state = TASK_STATE_DEAD;
	schedule();
}

uint64_t *get_pml4() {
	uint64_t *pml4;
	__asm__ __volatile__("mov %%cr3, %0" : "=r"(pml4));
	return pml4;
}

void set_task_vals(task_t *task, void *stack_bot) {
	task->pid = next_pid++;
	task->state = TASK_STATE_READY;
	task->next = nullptr;
	task->stack_top = stack_bot;

	task->ctx.rax = 0;
	task->ctx.rbx = 0;
	task->ctx.rcx = 0;
	task->ctx.rdx = 0;
	task->ctx.rsi = 0;
	task->ctx.rdi = 0;
	task->ctx.rbp = 0;
	task->ctx.rflags = 0;
}

void push_stack_vals(uint64_t *&stack_ptr, uint64_t user_stack_virt, void (*entry_point)()) {
	*(--stack_ptr) = 0x23;
	*(--stack_ptr) = user_stack_virt + 4096;
	*(--stack_ptr) = 0x202;
	*(--stack_ptr) = 0x1B;
	*(--stack_ptr) = reinterpret_cast<uint64_t>(entry_point);
}

void set_ctx_regs(task_t *task, uint64_t *stack_ptr) {
	task->ctx.rsp = reinterpret_cast<uint64_t>(stack_ptr);
	task->ctx.rip = reinterpret_cast<uint64_t>(user_enter);
	task->ctx.rflags = 0x202;
};

task_t *create_task(void (*entry_point)(), const bool is_kernel) {
	task_t *task{ reinterpret_cast<task_t*>(kmalloc(sizeof(task_t))) };
	if (!task) return nullptr;

	void *stack_bot{ reinterpret_cast<void*>(kmalloc(THREAD_STACK_SIZE)) };
	if (!stack_bot) {
		kfree(reinterpret_cast<uint64_t>(task));
		return nullptr;
	}

	set_task_vals(task, stack_bot);

	if (is_kernel) {
		uint64_t *stack_ptr{ reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(stack_bot) + THREAD_STACK_SIZE) };
		*(--stack_ptr) = 0;

		task->ctx.rsp = reinterpret_cast<uint64_t>(stack_ptr);
		task->ctx.rip = reinterpret_cast<uint64_t>(entry_point);
	} else {
		uint64_t *pml4{ get_pml4() };
		uint64_t user_stack_virt{ 0x00007FFFFFFFE000 };
		map_page(pml4, user_stack_virt, alloc_frame(), PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

		uint64_t phys_entry{ reinterpret_cast<uint64_t>(entry_point) };
		map_page(pml4, phys_entry, phys_entry, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

		uint64_t *stack_ptr{ reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(stack_bot) + THREAD_STACK_SIZE) };
		push_stack_vals(stack_ptr, user_stack_virt, entry_point);
		set_ctx_regs(task, stack_ptr);
	}

	return task;
}

void register_task(task_t *task) {
	if (!task) return;

	if (task_list_head == nullptr) {
		task_list_head = task;
		task_list_tail = task;
		curr_task = task;
		task->next = task;
	} else {
		task_list_tail->next = task;
		task->next = task_list_head;
		task_list_tail = task;
	}
}


// --- PAGING SECTION ---
uint64_t create_page() {
	uint64_t phys_table{ alloc_frame() };
	uint64_t *virt_table{ reinterpret_cast<uint64_t*>(phys_table) };
	for (int i{}; i < 512; ++i) virt_table[i] = 0;

	return phys_table;
}

bool check_subtable(const uint64_t *virt, const uint64_t idx) {
	return !(virt[idx] & PAGE_PRESENT);
}

inline uint64_t create_bitmasks() {
	return create_page() | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
}

uint64_t *create_pt_entry(const uint64_t *virt, const uint64_t idx) {
	return reinterpret_cast<uint64_t*>(virt[idx] & PAGE_BASE_MASK);
}

void subtable_bitmask_check(uint64_t *pt, const uint64_t idx) {
	if (check_subtable(pt, idx))
		pt[idx] = create_bitmasks();
}

void map_page(uint64_t *pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
	flags |= PAGE_PRESENT;

	uint64_t pml4_idx{ PML4_IDX(virt_addr) };
	subtable_bitmask_check(pml4_virt, pml4_idx);

	uint64_t *pdpt{ create_pt_entry(pml4_virt, pml4_idx) };
	uint64_t pdpt_idx{ PDPT_IDX(virt_addr) };
	subtable_bitmask_check(pdpt, pdpt_idx);

	uint64_t *pd{ create_pt_entry(pdpt, pdpt_idx) };
	uint64_t pd_idx = PD_IDX(virt_addr);
	if (pd[pd_idx] & PAGE_SIZE_BIT) {
		uint64_t *new_pt{ reinterpret_cast<uint64_t*>(create_page()) };
		uint64_t new_flags{ (pd[pd_idx] & 0xFFF) & ~(1 << 7) };

		uint64_t base{ pd[pd_idx] & PAGE_BASE_MASK };
		for (int i{}; i < 512; ++i) {
			new_pt[i] = (base + i * 0x1000) | new_flags;
		}

		pd[pd_idx] = reinterpret_cast<uint64_t>(new_pt) | new_flags;
	}

	subtable_bitmask_check(pd, pd_idx);

	uint64_t *pt{ create_pt_entry(pd, pd_idx) };
	uint64_t pt_idx{ PT_IDX(virt_addr) };
    pt[pt_idx] = (phys_addr & PAGE_BASE_MASK) | flags;

	__asm__ __volatile__("invlpg (%0)" :: "r"(virt_addr) : "memory");
}


// --- SYSCALLS SECTION ---
extern "C" uint64_t handle_syscall(struct syscall_frame *f) {
	switch (static_cast<SYS>(f->rax)) {
	case SYS::exit:
		task_exit();
		break;
	case SYS::write:
		print(reinterpret_cast<const char*>(f->rdi));
		break;
	case SYS::exec: {
		task_t *t{ create_elf_task(reinterpret_cast<void*>(f->rdi)) };
		if (t) register_task(t);
		return t ? t->pid : -1;
	}
	case SYS::sleep:
		sleep(f->rdi);
		break;
	case SYS::getpid:
		return curr_task->pid;
	case SYS::uptime:
		return timer_ticks;
	case SYS::video_write:
		print(reinterpret_cast<const char*>(f->rdi));
		break;
	case SYS::video_clear: {
		const char *args[MAX_ARG_CNT]{};
		args[0] = "clear";

		handleCmd(args[0], args);
		break;
	}
	case SYS::kb_read:
		return kb_read();
	case SYS::kb_empty:
		return kb_empty();
	default:
		return -1;
	}

	return 0;
}

static void write_msr(uint32_t msr, uint64_t val) {
	uint32_t low { static_cast<uint32_t>(val & 0xFFFFFFFF) };
	uint32_t high{ static_cast<uint32_t>((val >> 32) & 0xFFFFFFFF) };
	__asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

void init_syscalls() {
	write_msr(0xC0000081, (static_cast<uint64_t>(0x08) << 32) | static_cast<uint64_t>(0x08));
	write_msr(0xC0000082, reinterpret_cast<uint64_t>(syscall_entry));
	write_msr(0xC0000084, 0x200);
	write_msr(0xC0000102, reinterpret_cast<uint64_t>(&cpu_data));
}


// --- BLOCK DEVICE SECTION ---
bool block_op(block_device *dev, uint64_t sec, void *buf, uint64_t count, bool write) {
	return ahci_op(dev->drive, dev->lba_start + sec, buf, count, write);
}

extern "C" void kernel_main(uint32_t multiboot_info_addr) {
	parse_memmap(reinterpret_cast<multiboot_info*>(multiboot_info_addr));
	init_bitmap();
	init_heap();

	init_gdt();
	init_syscalls();
	remap_pic();
	init_idt();

	pit_set_freq(PIT_FREQ);
	__asm__ __volatile__ ("sti");
	pci_scan();

	idle_task = create_task(idle_task_func, true);
	register_task(idle_task);
	
	task_t *shell_task{ create_task(shell, true) };
	register_task(shell_task);

	while (true) {
		__asm__ __volatile__("hlt");
	}
}
