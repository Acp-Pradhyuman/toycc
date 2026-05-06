section .data
str_0: db 48,44,32
str_1: db 49,44,32
str_2: db 50,44,32
str_3: db 52,44,32
str_4: db 10
section .text
global _start
_start:
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_0]
	mov rdx, 3
	syscall
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_1]
	mov rdx, 3
	syscall
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_2]
	mov rdx, 3
	syscall
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_3]
	mov rdx, 3
	syscall
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_4]
	mov rdx, 1
	syscall
	mov rax, 60
	mov rdi, 0
	syscall
