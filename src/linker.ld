ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .gbaheader : {
        KEEP(*(.gbaheader))
    }
    .text : {
        *(.text*)
    }
    .ARM.exidx : {
        *(.ARM.exidx*)
    }
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }

    . = 0x03000000;
    .bss : {
        __bss_start = .;
        *(.bss*)
        __bss_end = .;
    }
    __stack_top = 0x03007FE0;
    __stack_size = 0x1000;
    __stack_bottom = __stack_top - __stack_size;
    /* Ensure there is room for the stack below .bss */
    ASSERT(__bss_end <= __stack_bottom, ".bss overlaps stack")
}
