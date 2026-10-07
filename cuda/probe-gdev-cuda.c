#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "DLOPEN_ERROR=%s\n", dlerror()); return 3; }
    int (*init)(unsigned int) = dlsym(library, "cuInit");
    int (*count_devices)(int *) = dlsym(library, "cuDeviceGetCount");
    int (*version)(int *) = dlsym(library, "cuDriverGetVersion");
    if (!init || !count_devices || !version) return 4;
    int api = 0, count = 0;
    int result = version(&api);
    printf("IMPLEMENTATION=Gdev EXPERIMENTAL_DRIVER_API=%d STATUS=%d\n", api, result);
    result = init(0);
    printf("CUDA_INIT_STATUS=%d\n", result);
    if (result) return 5;
    result = count_devices(&count);
    printf("CUDA_DEVICE_COUNT=%d STATUS=%d\n", count, result);
    if (result || count != 1) return 6;
    if (argc > 2) {
        int (*create)(void **, unsigned int, int) = dlsym(library, "cuCtxCreate_v2");
        int (*destroy)(void *) = dlsym(library, "cuCtxDestroy");
        if (!create || !destroy) return 7;
        void *context = NULL;
        result = create(&context, 0, 0);
        printf("CUDA_CONTEXT_CREATE_STATUS=%d\n", result);
        if (result) return 8;
        if (strcmp(argv[2], "--memory") == 0) {
            int (*allocate)(uint64_t *, unsigned int) = dlsym(library, "cuMemAlloc_v2");
            int (*release)(uint64_t) = dlsym(library, "cuMemFree_v2");
            int (*upload)(uint64_t, const void *, unsigned int) = dlsym(library, "cuMemcpyHtoD_v2");
            int (*download)(void *, uint64_t, unsigned int) = dlsym(library, "cuMemcpyDtoH_v2");
            if (!allocate || !release || !upload || !download) return 10;
            uint64_t address = 0;
            unsigned int input[64], output[64];
            for (unsigned int i = 0; i < 64; ++i) input[i] = 0x5a000000u + i;
            memset(output, 0, sizeof(output));
            result = allocate(&address, sizeof(input));
            printf("CUDA_MEMORY_ALLOC_STATUS=%d\n", result);
            if (result) return 11;
            result = upload(address, input, sizeof(input));
            printf("CUDA_MEMORY_UPLOAD_STATUS=%d\n", result);
            if (result) return 12;
            result = download(output, address, sizeof(output));
            printf("CUDA_MEMORY_DOWNLOAD_STATUS=%d\n", result);
            if (result) return 13;
            int matches = memcmp(input, output, sizeof(input)) == 0;
            printf("CUDA_MEMORY_MATCH=%s\n", matches ? "YES" : "NO");
            result = release(address);
            printf("CUDA_MEMORY_FREE_STATUS=%d\n", result);
            if (result || !matches) return 14;
        }
        result = destroy(context);
        printf("CUDA_CONTEXT_DESTROY_STATUS=%d\n", result);
        if (result) return 9;
    }
    puts("KERNEL_EXECUTION_NOT_TESTED");
    return 0;
}
