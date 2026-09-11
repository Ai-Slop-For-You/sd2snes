#!/usr/bin/env bash
# Build only the FXPAK Pro Mk.III mini core with pinned Quartus Lite 21.1.1/850.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
: "${QUARTUS_ROOTDIR:?Set QUARTUS_ROOTDIR to the installed 21.1.1/quartus directory}"
intel_bin="$QUARTUS_ROOTDIR/bin"
for tool in quartus_sh quartus_map quartus_fit quartus_sta quartus_asm; do
  [[ -x "$intel_bin/$tool" ]] || { echo "Missing $intel_bin/$tool" >&2; exit 2; }
done
version=$("$intel_bin/quartus_sh" --version)
[[ "$version" == *'Version 21.1.1 Build 850'* && "$version" == *'Lite Edition'* ]] || {
  echo "Expected Quartus Lite 21.1.1 Build 850; got: $version" >&2; exit 2;
}
mkdir -p "$root/.build/mk3-mini"
printf '%s\n' "$version" > "$root/.build/mk3-mini/toolchain-version.log"
git -C "$root" rev-parse HEAD > "$root/.build/mk3-mini/source-revision.log"
make -C "$root/utils" rle derle
# Keep machine-specific settings out of version control; do not invoke mk2/all.
make -C "$root/verilog/sd2snes_mini" HOST=LINUX "INTEL_BIN=$intel_bin" mk3_clean
make -C "$root/verilog/sd2snes_mini" HOST=LINUX "INTEL_BIN=$intel_bin" mk3 \
  2>&1 | tee "$root/.build/mk3-mini/build.log"
mini="$root/verilog/sd2snes_mini"
test -s "$mini/fpga_mini.bi3"
"$root/utils/derle" "$mini/fpga_mini.bi3" "$root/.build/mk3-mini/roundtrip.rbf"
cmp "$mini/output_files/main.rbf" "$root/.build/mk3-mini/roundtrip.rbf"
sha256sum "$mini/fpga_mini.bi3" "$mini/output_files/main.rbf" | tee "$root/.build/mk3-mini/SHA256SUMS"
cp "$mini/output_files/main.sta.summary" "$root/.build/mk3-mini/"
echo 'Mk.III mini core built and RLE round trip verified. Next: make -C src CONFIG=config-mk3 all'
