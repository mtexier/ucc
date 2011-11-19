section .bss
	errno resq 1

section .text
	global __syscall
__syscall:
	push rbp
	mov rbp, rsp
	mov rax, [rbp + 16] ; eax
	mov rdi, [rbp + 24] ; ebx
	mov rsi, [rbp + 32] ; ecx
	mov rdx, [rbp + 40] ; edx
	mov rcx, [rbp + 48] ; edi
	mov r8,  [rbp + 56] ; esi
	mov r9,  [rbp + 64] ; e8?
	syscall
#include "syscall_err.s"
	neg rax
	mov [qword errno], rax
	mov rax, -1
.fin:
	leave
	ret