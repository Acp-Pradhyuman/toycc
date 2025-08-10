section .text
global _start
_start:
	mov rax, 60
	mov rdi, 1073741825
	syscall
