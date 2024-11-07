global _start
extern _memset
_start:
  call main
  mov ebx, eax
  mov eax, 1
  int 0x80


main:
push rbp
mov rbp, rsp
L1:
lea rsi, qword [G_94879652890344]
mov rdx, rsi
add rdx, 8
mov dword [rdx+4], 6
lea rcx, qword [G_94879652890344]
mov dword [rcx+8], 5
mov eax, dword [rdx+4]
cmp eax, 6
jne L3
jmp L2
L2:
mov eax, 0
jmp L4
L3:
mov eax, 1
jmp L4
L4:
mov rsp, rbp
pop rbp
ret

SECTION .data
G_94879652890344:
DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
