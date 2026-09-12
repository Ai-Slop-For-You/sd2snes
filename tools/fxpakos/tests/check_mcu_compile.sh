#!/usr/bin/env bash
# Compile every changed MCU translation unit using its real platform headers.
# This is deliberately not a firmware link: the FPGA mini bitstreams must be
# built/provided separately. None of these units includes generated cfgware.h.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
variants=("$@")
((${#variants[@]})) || variants=(mk3)
for variant in "${variants[@]}"; do
  case "$variant" in mk2|mk3|mk3-stm32) ;; *) echo "Unknown MCU variant: $variant" >&2; exit 2 ;; esac
  make -C "$root/src" CONFIG="config-$variant" -o "obj-$variant/cfgware.h" \
    "obj-$variant/fxpak_art.o" "obj-$variant/fxpak_meta.o" "obj-$variant/main.o" \
    "obj-$variant/snes.o" "obj-$variant/filetypes.o"
done
