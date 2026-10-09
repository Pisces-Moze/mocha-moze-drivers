#!/bin/bash
set -euo pipefail
export LOCALVERSION=
src=$(realpath "${1:?kernel source}");out=$(realpath "${2:?kernel output}")
repo=$(cd "$(dirname "$0")/.." && pwd)
test -f "$out/Module.symvers"
# Kbuild can otherwise reuse an external .mod.o from a different O= tree.
make -C "$src" O="$out" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M="$repo/backlight" clean
make -C "$src" O="$out" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M="$repo/backlight" modules
release=$(cat "$out/include/config/kernel.release")
vermagic=$(modinfo -F vermagic "$repo/backlight/mocha_miui_backlight.ko")
test "${vermagic%% *}" = "$release" || { echo "Module release mismatch: $vermagic (expected $release)" >&2; exit 1; }
printf 'Backlight module built for %s\n' "$release"
