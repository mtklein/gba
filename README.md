# GBA Hello World

This repository builds a minimal Game Boy Advance ROM using C and Ninja.

## Building

Run `ninja` to produce `hello.gba`. The build uses Clang for compilation and
`ld.lld` for linking to target the `armv4t-none-eabi` architecture. It requires
`llvm-objcopy-19` and a recent `lld`.

### macOS

On macOS the system `clang` does not ship with `lld`. Install the `llvm`
formula from Homebrew and ensure `ld.lld` is in your `PATH`:

```sh
brew install llvm
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

After this, running `ninja` should succeed.
