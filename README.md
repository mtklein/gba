# GBA Hello World

This repository builds a minimal Game Boy Advance ROM using C and Ninja.

## Building

Run `ninja` to produce `hello.gba`. The build uses Clang for compilation and
`ld.lld` for linking to target the `armv4t-none-eabi` architecture. It requires
`llvm-objcopy` (version 19 on Linux) and `lld`.

### macOS

On macOS the system `clang` does not ship with `lld`. Install the `llvm` and
`lld` formulas from Homebrew:

```sh
brew install llvm lld
```
The build script automatically searches Homebrew's directories, so no further
setup is required. After this, running `ninja` should succeed.
