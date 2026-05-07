section .data
str_0: db 102,97,99,116,40,54,41,61,55,50,48,10
str_1: db 102,105,98,40,49,48,41,61,53,53,10
section .text
global _start
_start:
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_0]
	mov rdx, 12
	syscall
	mov rax, 1
	mov rdi, 1
	lea rsi, [rel str_1]
	mov rdx, 11
	syscall
	mov rax, 60
	mov rdi, 55
	syscall
