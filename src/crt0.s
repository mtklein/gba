    .section .text
    .global _start
_start:
    ldr sp, =0x03007FE0
    bl main
1:  b 1b
