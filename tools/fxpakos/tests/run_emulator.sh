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
gcc -std=c99 -Wall -Wextra -Werror -DFXART_HOST_TEST -I"$root/tools/fxpakos/tests" -I"$root/src" \
  -c "$root/src/fxpak_art.c" -o "$root/.build/tests/fxpak_art.o"
gcc -std=c99 -Wall -Wextra -Werror -DFXART_HOST_TEST -I"$root/tools/fxpakos/tests" -I"$root/src" \
  -c "$root/src/fxpak_meta.c" -o "$root/.build/tests/fxpak_meta.o"
g++ -std=gnu++11 -O2 -DPROFILE_ACCURACY -DDEBUGGER -include cstdint \
  -I"$source_dir/bsnes" -I"$source_dir/common" -I"$source_dir/bsnes/snes" \
  -I"$root/src" -I"$root/tools/fxpakos/tests" \
  "$root/tools/fxpakos/tests/fxpak_art_host.cpp" "$root/.build/tests/fxpak_art.o" "$root/.build/tests/fxpak_meta.o" \
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

python3 tools/fxpakos/tests/make_art_fixtures.py .build/art-sd .build/art/starfall
python3 tools/fxpakos/tests/make_metadata_fixtures.py .build/art-sd
g++ -std=c++11 -Wall -Wextra -Werror -Isrc -Itools/fxpakos/tests \
  tools/fxpakos/tests/test_art_worker.cpp tools/fxpakos/tests/fxpak_art_host.cpp \
  .build/tests/fxpak_art.o -o .build/tests/test_art_worker
.build/tests/test_art_worker "$root/.build/art-sd"
g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Itools/fxpakos/tests \
  tools/fxpakos/tests/test_meta_worker.cpp tools/fxpakos/tests/fxpak_art_host.cpp \
  .build/tests/fxpak_art.o .build/tests/fxpak_meta.o -Wl,--wrap=f_open,--wrap=f_read \
  -o .build/tests/test_meta_worker
.build/tests/test_meta_worker "$root/.build/tests/meta-worker-sd"
for region in NTSC PAL; do
  out="$root/.build/tests/art-$region"
  mkdir -p "$out"
  timeout 180 .build/tests/menu_harness snes/m3nu.bin .build/tests/symbols "$out" "$region" "$root/.build/art-sd" | tee "$out/run.log"
  FXART_EDGE_TESTS=1 timeout 90 .build/tests/menu_harness snes/m3nu.bin .build/tests/symbols "$out" "$region" "$root/.build/art-sd" | tee "$out/edge.log"
  python3 tools/fxpakos/tests/verify_dynamic_render.py "$out" .build/art-sd .build/art/starfall | tee "$out/render.log"
done

for region in NTSC PAL; do
  out="$root/.build/tests/meta-$region"
  mkdir -p "$out"
  FXMETA_TESTS=1 timeout 240 .build/tests/menu_harness snes/m3nu.bin .build/tests/symbols "$out" "$region" "$root/.build/art-sd" | tee "$out/run.log"
  python3 tools/fxpakos/tests/verify_dynamic_render.py "$out" .build/art-sd .build/art/starfall | tee "$out/cover-render.log"
  python3 tools/fxpakos/tests/verify_metadata_render.py "$out" snes/m3nu.bin .build/tests/symbols | tee "$out/metadata-render.log"
done
