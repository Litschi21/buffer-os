bits 64
global swtch_ctx
global user_enter

section .text

user_enter:
	iretq

swtch_ctx:
	mov r8, rdi
	mov r9, rsi

	test r8, r8
    jz .load_new_ctx

 	mov [r8 + 8],  rbx
    mov [r8 + 16], rcx
    mov [r8 + 24], rdx
    mov [r8 + 32], rsi
    mov [r8 + 40], rdi
    mov [r8 + 48], rbp

    lea rax, [rsp + 8]
    mov [r8 + 56], rax

    mov rax, [rsp]
    mov [r8 + 64], rax

    pushfq
    pop qword [r8 + 72]

.load_new_ctx:
    push qword [r9 + 72]
    popfq

    mov rbx, [r9 + 8]
    mov rcx, [r9 + 16]
    mov rdx, [r9 + 24]
    mov rdi, [r9 + 40]
    mov rbp, [r9 + 48]

    mov rsp, [r9 + 56]
    push qword [r9 + 64]
    
    mov rax, [r9 + 0]
    mov rsi, [r9 + 32]
    ret
