#!/bin/sh
# Explicit external-module build against the exact running kernel's output.
# Nothing is installed or loaded. Keep DEST outside this source checkout.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 KERNEL_SOURCE KERNEL_OUTPUT NEW_DESTINATION" >&2
    exit 2
fi
src=$(realpath "$1")
out=$(realpath "$2")
dest=$(realpath -m "$3")
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -f "$out/Module.symvers"
test -f "$out/.config"
if [ -e "$dest" ]; then
    echo "destination already exists: $dest" >&2
    exit 2
fi
mkdir -p "$dest"
for name in mocha_rt5671_board mocha_audio_i2s_probe mocha_audio_runtime_card mocha_audio_data_pin mocha_miui_tfa98xx; do
    cp "$here/$name.c" "$dest/"
done
cp "$here/tfa98xx.h" "$dest/"
for name in rt5670.c rt5670.h rt5670-dsp.h rl6231.h; do
    cp "$src/sound/soc/codecs/$name" "$dest/"
done
patch --directory="$dest" --strip=4 < "$here/rt5670-no-jack-remove.patch"
cat > "$dest/Makefile" <<'EOF'
obj-m += snd-soc-rt5670.o
snd-soc-rt5670-y := rt5670.o
obj-m += mocha_rt5671_board.o
obj-m += mocha_audio_i2s_probe.o
obj-m += mocha_audio_runtime_card.o
obj-m += mocha_miui_tfa98xx.o
obj-m += mocha_audio_data_pin.o
ccflags-y += -I$(srctree)/drivers/pinctrl/tegra
EOF
make -C "$src" O="$out" M="$dest" ARCH=arm \
    CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}" \
    LOCALVERSION= -j"${JOBS:-2}" W=1 modules
sha256sum "$dest"/*.ko
