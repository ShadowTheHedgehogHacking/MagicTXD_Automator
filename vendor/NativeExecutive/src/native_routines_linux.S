.intel_syntax noprefix

#ifdef NATEXEC_FIRST_ENTRY_POINT

.global _start_natexec
.global _init_natexec

_start_natexec:
    mov r12,rdx
    mov r13,rax
    call _init_natexec
    mov rdx,r12
    mov rax,r13
    jmp _start

#endif //NATEXEC_FIRST_ENTRY_POINT
