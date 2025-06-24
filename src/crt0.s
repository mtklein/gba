    .section .text
    .global _start
_start:
    ldr sp, =__stack_top
    ldr r0, =main
    bx  r0
1:  b 1b
