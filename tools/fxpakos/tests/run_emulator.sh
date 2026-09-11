#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
source_dir=${BSNES_SOURCE:-"$root/.tools/bsnes-plus"}
revision=a9789fab9a26859153c2963defe186fcaaa80ca2
if [[ ! -d "$source_dir" ]]; then
  git clone https://github.com/devinacker/bsnes-plus.git "$source_dir"
  git -C "$source_dir" checkout --detach "$revision"
fi
[[ $(git -C "$source_dir" rev-parse HEAD) == "$revision" ]] || { echo "Expected bsnes-plus $revision" >&2; exit 1; }
source_dir=$(cd "$source_dir" && pwd)
mkdir -p "$source_dir/bsnes/obj/headless" "$root/.build/tests"
make -C "$source_dir/bsnes" -f "$root/tools/fxpakos/tests/Makefile.bsnes" all -j"${JOBS:-2}"
g++ -std=gnu++11 -O2 -DPROFILE_ACCURACY -DDEBUGGER -include cstdint \
  -I"$source_dir/bsnes" -I"$source_dir/common" -I"$source_dir/bsnes/snes" \
  "$root/tools/fxpakos/tests/menu_harness.cpp" "$source_dir/bsnes/obj/headless/"*.o \
  -ldl -o "$root/.build/tests/menu_harness"
cd "$root"
export PATH="$root/.tools/bin:$PATH"
make -C snes art
make -C snes
awk '$1 ~ /^[0-9A-F]+$/ {print $1,$2}' "$root/snes/"*.map > "$root/.build/tests/symbols"
tools/fxpakos/tests/test_build_errors.sh
python3 -m unittest discover -s tools/fxpakos/tests -v
for region in NTSC PAL; do
  out="$root/.build/tests/$region"
  mkdir -p "$out"
  args=()
  [[ "$region" != PAL ]] || args+=(PAL)
  timeout 120 .build/tests/menu_harness snes/m3nu.bin .build/tests/symbols "$out" "${args[@]}" | tee "$out/run.log"
  python3 tools/fxpakos/tests/verify_render.py "$out" .build/art/starfall | tee "$out/render.log"
done
