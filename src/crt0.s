    .section .text
    .global _start
_start:
    ldr sp, =0x03007FE0
    ldr r0, =main
    bx  r0
1:  b 1b
