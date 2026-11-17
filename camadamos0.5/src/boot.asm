; CamadamOS boot entry: Multiboot header, stack, GDT/IDT helpers, interrupt stubs
bits 32

MB_ALIGN    equ 1<<0
MB_MEMINFO  equ 1<<1
MB_VIDEO    equ 1<<2
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO | MB_VIDEO
MB_MAGIC    equ 0x1BADB002
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd 0, 0, 0, 0, 0        ; address fields (unused, we are ELF)
    dd 0                    ; mode_type: linear graphics
    dd 1024                 ; width
    dd 768                  ; height
    dd 32                   ; bits per pixel

section .bss
align 16
stack_bottom:
    resb 262144
stack_top:

section .text
global _start
extern kmain
_start:
    cli
    mov esp, stack_top
    push 0
    popf
    push ebx                ; multiboot info pointer
    push eax                ; multiboot magic
    call kmain
.hang:
    cli
    hlt
    jmp .hang

global gdt_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ---- interrupt stubs ----
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
%assign i 22
%rep 26
ISR_NOERR i
%assign i i+1
%endrep

extern isr_handler
isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

global isr_table
section .data
isr_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep
