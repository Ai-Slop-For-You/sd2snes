#!/usr/bin/env bash
# User-local build; no root, system installation, or Boost required by 1.8.1.1.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
dest=${1:-"$root/.tools"}
mkdir -p "$dest"
dest=$(cd "$dest" && pwd)
archive="$dest/snescom-1.8.1.1.tar.gz"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 https://bisqwit.iki.fi/src/arch/snescom-1.8.1.1.tar.gz -o "$archive"
fi
echo "11583f960e217ddc672848979797d62106d40a284e0c17b8acf75d504ba1dbaa  $archive" | sha256sum --check
if [[ ! -d "$dest/snescom-1.8.1.1" ]]; then
  tar -xzf "$archive" -C "$dest"
fi
make -C "$dest/snescom-1.8.1.1" -j"${JOBS:-2}" snescom sneslink
mkdir -p "$dest/bin"
install -m755 "$dest/snescom-1.8.1.1/snescom" "$dest/snescom-1.8.1.1/sneslink" "$dest/bin/"
printf '\nBuild the menu with:\n  PATH="%s/bin:$PATH" make -C "%s/snes"\n' "$dest" "$root"
