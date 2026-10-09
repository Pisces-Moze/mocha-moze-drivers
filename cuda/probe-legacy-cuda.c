/* SPDX-License-Identifier: GPL-2.0-only */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { printf("CUDA_LIBRARY_LOAD=FAIL %s\n", dlerror()); return 1; }
    int (*initialize)(unsigned) = dlsym(library, "cuInit");
    int (*version)(int *) = dlsym(library, "cuDriverGetVersion");
    int (*count)(int *) = dlsym(library, "cuDeviceGetCount");
    int v = 0, n = 0;
    if (version) { int r = version(&v); printf("CUDA_DRIVER_VERSION=%d status=%d\n", v, r); }
    if (!initialize) { puts("CUDA_API_SYMBOL=FAIL"); return 1; }
    int result = initialize(0);
    printf("CUDA_INITIALIZATION=%s status=%d\n", result ? "FAIL" : "PASS", result);
    if (!result && count) { int r = count(&n); printf("CUDA_DEVICE_COUNT=%d status=%d\n", n, r); }
    dlclose(library);
    return result || n == 0 ? 1 : 0;
}
