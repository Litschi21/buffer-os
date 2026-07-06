bits 64
global switch_context

section .text

switch_context:
	test rdi, rdi
    jz .load_new_context

    mov [rdi + 8],  rbx
    mov [rdi + 16], rcx
    mov [rdi + 24], rdx
    mov [rdi + 32], rsi
    mov [rdi + 40], rdi
    mov [rdi + 48], rbp

    lea rax, [rsp + 8]
    mov [rdi + 56], rax

    mov rax, [rsp]
    mov [rdi + 64], rax

    pushfq
    pop qword [rdi + 72]

.load_new_context:
    push qword [rsi + 72]
    popfq

    mov rbx, [rsi + 8]
    mov rcx, [rsi + 16]
    mov rdx, [rsi + 24]
    mov rdi, [rsi + 40]
    mov rbp, [rsi + 48]

    mov rsp, [rsi + 56]
    push qword [rsi + 64]
    
    mov rsi, [rsi + 32]
    mov rax, [rsi + 0]
    ret
