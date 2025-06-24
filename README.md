# GBA Bouncing Ball

This repository builds a simple Game Boy Advance ROM that displays a ball bouncing around the screen.

Run `ninja` to produce `hello.gba`.
On Mac first `brew install llvm lld`.

## Stack Setup

The linker script defines three symbols related to the stack:

```
__stack_top    = 0x03007FE0;
__stack_size   = 0x1000;    # 4 KiB stack
__stack_bottom = __stack_top - __stack_size;
```

`crt0.s` initializes `sp` with `__stack_top`. Adjust `__stack_size` in
`src/linker.ld` to change the reserved stack space. A linker assertion warns if
the `.bss` section ever grows beyond `__stack_bottom`.
