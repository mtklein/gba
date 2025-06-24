# GBA Hello World

This repository builds a minimal Game Boy Advance ROM using C and Ninja.

## Building

Run `ninja` to produce `hello.gba`. The build uses Clang and LLD to target the
`armv4t-none-eabi` architecture and requires `llvm-objcopy-19`.
