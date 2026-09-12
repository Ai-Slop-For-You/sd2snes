#!/usr/bin/env bash
# Default: use a locally synthesized mini core. Explicit alternative: --release-mini.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
mini="$root/verilog/sd2snes_mini/fpga_mini.bi3"
case "${1:-}" in
  '') ;;
  --release-mini)
    python3 tools/fxpakos/extract_release_mini.py
    mini="$root/.build/upstream/fpga_mini.bi3"
    ;;
  *) echo "Usage: $0 [--release-mini]" >&2; exit 2 ;;
esac
test -s "$mini"
version=1.11.0-fxpak-p3
mkdir -p .build/mk3-firmware
# A compiler/verifier failure must not leave a manifest from an older build.
rm -f .build/mk3-firmware/SHA256SUMS .build/mk3-firmware/source-revision.log \
  .build/mk3-firmware/compiler-version.log
make -C utils bin2c
make -C src/utils
# Both Mk.III MCU variants use the same mini core. Mk.II is not built.
for board in mk3 mk3-stm32; do
  extension=im3
  [[ "$board" != mk3-stm32 ]] || extension=stm
  # Regenerate cfgware.h when switching asset paths; do not reuse preflight objects.
  make -C src "CONFIG=config-$board" clean
  make -C src "CONFIG=config-$board" "CONFIG_CFGWARE=$mini" "VERSION=$version" all \
    2>&1 | tee ".build/mk3-firmware/$board-build.log"
  python3 tools/fxpakos/verify_mk3_firmware.py "src/obj-$board/firmware.$extension" \
    "src/obj-$board/sd2snes.elf" "$mini" --version "$version" --board "$board" \
    2>&1 | tee ".build/mk3-firmware/$board-verify.log"
done
git rev-parse HEAD > .build/mk3-firmware/source-revision.log
arm-none-eabi-gcc --version > .build/mk3-firmware/compiler-version.log
sha256sum "$mini" src/obj-mk3/firmware.im3 src/obj-mk3-stm32/firmware.stm > .build/mk3-firmware/SHA256SUMS
