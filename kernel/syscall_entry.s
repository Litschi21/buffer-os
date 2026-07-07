bits 64
section .text
global syscall_entry
extern handle_syscall

syscall_entry:
	swapgs

	mov [gs:0x0], rsp
	mov rsp, [gs:0x8]

	push rsi
	push rdi
	push rbp
	push rax
	push rbx
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov rdi, rsp
	sub rsp, 8
	call handle_syscall
	add rsp, 8

	mov [rsp + 88], rax

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rbx
	pop rax
	pop rbp
	pop rdi
	pop rsi

	mov rsp, [gs:0x0]

	swapgs
	sysretq
