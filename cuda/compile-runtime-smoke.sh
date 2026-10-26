#!/bin/sh
# Isolated NVIDIA CUDA 6.5 compiler and Ubuntu GCC 4.8 cross sysroot.
set -eu
if [ "$#" -ne 4 ]; then
    echo "usage: $0 CUDA_6_5_ROOT OLD_TOOLCHAIN_ROOT RUNTIME_BUILD OUTPUT" >&2
    exit 2
fi
cuda=$(realpath "$1")
old=$(realpath "$2")
build=$(realpath "$3")
output=$(realpath -m "$4")
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
inc="$old/usr/arm-linux-gnueabihf/include"
LD_LIBRARY_PATH="$old/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
"$cuda/bin/nvcc" -target-cpu-arch ARM -m32 \
    -ccbin "$old/usr/bin/arm-linux-gnueabihf-g++-4.8" \
    -Xcompiler=-B/usr/arm-linux-gnueabihf/bin/,-ffunction-sections,-fdata-sections \
    -I"$inc/c++/4.8.2" -I"$inc/c++/4.8.2/arm-linux-gnueabihf" \
    -I"$inc/c++/4.8.2/backward" -I"$inc" \
    -I"$cuda/targets/armv7-linux-gnueabihf/include" \
    --cudart=none -arch=sm_32 -c "$here/runtime-smoke.cu" -o "$output.o"
arm-linux-gnueabihf-g++ -no-pie -Wl,--gc-sections "$output.o" \
    -L"$build/cuda" -L"$build/lib" -Wl,-rpath-link,"$build/lib" -lucuda -o "$output"
