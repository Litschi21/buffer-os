#pragma once

#include <stdint.h>

#define GDT_SIZE                  9
#define IDT_SIZE                  256

#define PIT_FREQ                  200

#define VID_BFR_ROW_LEN           160
#define VID_ROWS                  25

#define KB_BUFFER_SIZE            256

#define MAX_ARG_CNT               16

#define INIT_HEAP_FRAMES          1000
#define MIN_MALLOC_SIZE           40
#define MAX_64_BIT_DIG            20

#define THREAD_STACK_SIZE         4096
#define TASK_STATE_READY          0
#define TASK_STATE_RUNNING        1
#define TASK_STATE_DEAD           2

#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_IDX(addr) (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr)   (((addr) >> 12) & 0x1FF)

#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_WRITE     (1ULL << 1)
#define PAGE_USER      (1ULL << 2)
#define PAGE_BASE_MASK 0x000FFFFFFFFFF000ULL

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

struct __attribute__((packed)) tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

extern volatile char *video;
extern int v_pos;

extern struct tss_entry tss;
extern struct gdt_entry gdt[GDT_SIZE];
extern struct gdt_ptr   gdt_ptr;

void gdt_set_tss(int32_t idx, uint64_t base, uint32_t lim);
void gdt_set_gate(int32_t idx, uint32_t base, uint32_t lim, uint8_t access, uint8_t flags);
extern "C" void gdt_flush(uint32_t gdt_ptr_addr);
void init_gdt();


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

extern "C" void *isr_stub_table[];
extern "C" void timer_isr();
extern "C" void keyboard_isr();
extern "C" void handle_page_fault();

extern "C" void exception_handler();
void idt_set_descriptor(uint8_t v, void *isr, uint8_t flags);
void init_idt();


// --- PIC SECTION ---
extern "C" void remap_pic();


// --- TIMER & KB SECTION ---
extern volatile uint64_t timer_ticks;

extern "C" void timer_handler();
void pit_set_freq(uint32_t freq);
void sleep(uint64_t ms);

bool kb_empty();
void kb_push(char c);
char kb_read();
extern "C" void keyboard_handler();


// --- SHELL SECTION ---
extern char curr_input[256];
extern int line_len;

bool strEqual(const char *a, const char *b);
int strlen(const char *str);
char *to_string(int64_t n, char *buf);
void printCh(char c, bool await_input=false, int color_code=0x0F);
void print(const char *msg, bool await_input=false, int color_code=0x0F);
[[noreturn]] void panic(const char *msg);
void handleCmd(const char *cmd, const char *args[MAX_ARG_CNT]);
void parseCmd();
void shell();


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

extern uint8_t _kernel_end;
extern uint8_t *bitmap;

void parse_memmap(multiboot_info* mbi);
uint64_t alloc_frame();
void free_frame(uint64_t addr);
uint64_t next_header(uint64_t size);
chunk_footer *write_footer(uint64_t header_addr, uint64_t size);
chunk_header *write_header(uint64_t addr, uint64_t size, bool is_free);

void init_bitmap();
void init_heap();
uint64_t kmalloc(uint64_t size);
void kfree(uint64_t addr);
extern void memcpy(void *dest, const void *src, uint64_t n);
extern "C" void user_enter();


// Scheduling
struct cpu_context {
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
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

// From Syscalls
struct cpu_data {
	uint64_t user_rsp;
	uint64_t kernel_rsp;
};

extern struct cpu_data cpu_data;


extern int next_pid;
extern task_t *curr_task;
extern task_t *task_list_head;
extern task_t *task_list_tail;
extern task_t *idle_task;

void idle_task_func();
extern "C" void swtch_ctx(struct cpu_context *old_ctx, struct cpu_context *new_ctx);
task_t *get_next_task();
void schedule();
void task_exit();
void map_page(uint64_t *pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
uint64_t *get_pml4();
void set_task_vals(task_t *task, void *stack_bot);
void push_stack_vals(uint64_t *stack_ptr, uint64_t user_stack_virt, void (*entry_point)());
void set_ctx_regs(task_t *task, uint64_t *stack_ptr);
task_t *create_task(void (*entry_point)(), bool is_kernel=false);
void register_task(task_t *task);


// Paging
extern "C" void handle_page_fault();
uint64_t create_page();
bool check_subtable(const uint64_t *virt, uint64_t idx);
inline uint64_t create_bitmasks();
uint64_t *create_pt_entry(const uint64_t *virt, uint64_t idx);
void subtable_bitmask_check(uint64_t *pt, uint64_t idx);
void map_page(uint64_t *pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);


// --- SYSCALLS SECTION ---
struct syscall_frame {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rdx, rcx, rbx, rax;
	uint64_t rbp;
	uint64_t rdi;
	uint64_t rsi;
};

enum class SYS {
	exit = 0,
	write,
	exec,
	sleep,
	getpid,
	uptime
};

extern "C" uint64_t handle_syscall(struct syscall_frame *f);
extern "C" void syscall_entry();
void init_syscalls();
extern "C" void kernel_main(uint32_t multiboot_info_addr);
