; https://chromium.googlesource.com/chromiumos/docs/+
; /master/constants/syscalls.md
; exit
section .text
    global _start

_start:
    mov rax, 0x3c   ; 60
    mov rdi, 255   ; rdi carries arg (for exit it's int error code)
    syscall