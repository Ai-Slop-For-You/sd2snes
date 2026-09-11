#!/usr/bin/env bash
# Exercise the legacy tools' real zero-exit-status error cases through make.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
export PATH="$root/.tools/bin:$PATH"
mkdir -p "$root/.build"
scratch=$(mktemp -d "$root/.build/error-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
printf '.text\nstz @$123456\n' > "$scratch/probe.a65"
if make -C "$scratch" -f "$root/snes/Makefile" probe.o65 > "$scratch/assembler.log" 2>&1; then
  echo 'FAIL: invalid STZ accepted' >&2; exit 1
fi
grep -q '^Error:' "$scratch/assembler.log"
[[ ! -e "$scratch/probe.o65" ]]
printf '.text\n.link page $c0\njsl @missing_symbol_for_test\n' > "$scratch/probe.a65"
if make -C "$scratch" -f "$root/snes/Makefile" menu.bin IPS= O65=probe.o65 > "$scratch/linker.log" 2>&1; then
  echo 'FAIL: undefined symbol accepted' >&2; exit 1
fi
grep -q 'still undefined' "$scratch/linker.log"
[[ ! -e "$scratch/menu.bin" && ! -e "$scratch/m3nu.bin" ]]
echo 'PASS: assembler and linker diagnostic failures stop make and remove invalid outputs'
