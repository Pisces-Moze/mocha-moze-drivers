#!/bin/sh
# Build a minimal llama2.c/CUDA engine without cuBLAS or NVIDIA libcudart.
set -eu
if [ "$#" -ne 5 ]; then
    echo "usage: $0 INPUT_DIRECTORY CUDA_6_5_ROOT OLD_TOOLCHAIN_ROOT RUNTIME_BUILD OUTPUT_DIRECTORY" >&2
    exit 2
fi
input=$(realpath "$1")
cuda=$(realpath "$2")
old=$(realpath "$3")
runtime=$(realpath "$4")
mkdir -p "$5"
output=$(realpath "$5")
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$here/prepare-llm.py" "$input/run.c" "$input/inputs.json" "$output/run-cuda.c"
arm-linux-gnueabihf-gcc -std=gnu99 -O3 -ffp-contract=off \
    -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
    -ffunction-sections -fdata-sections -c "$output/run-cuda.c" -o "$output/run.o"
inc="$old/usr/arm-linux-gnueabihf/include"
LD_LIBRARY_PATH="$old/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
"$cuda/bin/nvcc" -target-cpu-arch ARM -m32 -O3 --fmad=false \
    -ccbin "$old/usr/bin/arm-linux-gnueabihf-g++-4.8" \
    -Xcompiler=-B/usr/arm-linux-gnueabihf/bin/,-ffunction-sections,-fdata-sections \
    -I"$inc/c++/4.8.2" -I"$inc/c++/4.8.2/arm-linux-gnueabihf" \
    -I"$inc/c++/4.8.2/backward" -I"$inc" \
    -I"$cuda/targets/armv7-linux-gnueabihf/include" \
    --cudart=none -arch=sm_32 -c "$here/llm-forward.cu" -o "$output/llm-cuda.o"
arm-linux-gnueabihf-g++ -no-pie -Wl,--gc-sections "$output/run.o" "$output/llm-cuda.o" \
    -L"$runtime/cuda" -L"$runtime/lib" -Wl,-rpath-link,"$runtime/lib" -lucuda -lm \
    -o "$output/mocha-cuda-llm-engine"
