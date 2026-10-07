#!/bin/bash
set -euo pipefail
export LOCALVERSION=
src=$(realpath "${1:?kernel source}");out=$(realpath "${2:?kernel output}")
repo=$(cd "$(dirname "$0")/.." && pwd)
test -f "$out/Module.symvers"
make -C "$src" O="$out" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M="$repo/backlight" modules
