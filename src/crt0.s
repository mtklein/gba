    .section .text
    .global _start
_start:
    ldr sp, =0x03007FE0
    ldr r0, =main
    bx  r0
1:  b 1b

    .thumb_func
    .global __aeabi_memcpy
__aeabi_memcpy:
    cmp r2, #0
    beq 2f
1:
    ldrb r3, [r1]
    adds r1, #1
    strb r3, [r0]
    adds r0, #1
    subs r2, #1
    bne 1b
2:
    bx lr
