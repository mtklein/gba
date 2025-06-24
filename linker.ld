ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .gbaheader : {
        KEEP(*(.gbaheader))
    }
    .text : {
        *(.text*)
    }
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }
    .bss : {
        *(.bss*)
    }
}
