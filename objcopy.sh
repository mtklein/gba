#!/bin/sh
if command -v llvm-objcopy-19 >/dev/null 2>&1; then
  exec llvm-objcopy-19 "$@"
elif command -v llvm-objcopy >/dev/null 2>&1; then
  exec llvm-objcopy "$@"
else
  echo "llvm-objcopy not found" >&2
  exit 1
fi
