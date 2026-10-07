#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define LOAD(name, type) type = dlsym(library, name); if (!type) return 3
#define CHECK(label, call) do { int status = (call); printf(label "=%d\n", status); if (status) return 4; } while (0)

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3 && argc != 4) return 2;
    unsigned long requested = argc == 4 ? strtoul(argv[3], NULL, 10) : 64;
    if (!requested || requested > 65536) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { puts(dlerror()); return 3; }
    int (*init)(unsigned int);
    int (*create)(void **, unsigned int, int);
    int (*destroy)(void *);
    int (*allocate)(uint64_t *, unsigned int);
    int (*release)(uint64_t);
    int (*upload)(uint64_t, const void *, unsigned int);
    int (*download)(void *, uint64_t, unsigned int);
    int (*module_load)(void **, const char *);
    int (*module_function)(void **, void *, const char *);
    int (*module_unload)(void *);
    int (*launch)(void *, unsigned int, unsigned int, unsigned int,
                  unsigned int, unsigned int, unsigned int,
                  unsigned int, void *, void **, void **);
    int (*synchronize)(void);
    LOAD("cuInit", init);
    LOAD("cuCtxCreate_v2", create);
    LOAD("cuCtxDestroy", destroy);
    LOAD("cuMemAlloc_v2", allocate);
    LOAD("cuMemFree_v2", release);
    LOAD("cuMemcpyHtoD_v2", upload);
    LOAD("cuMemcpyDtoH_v2", download);
    LOAD("cuModuleLoad", module_load);
    LOAD("cuModuleGetFunction", module_function);
    LOAD("cuModuleUnload", module_unload);
    LOAD("cuLaunchKernel", launch);
    LOAD("cuCtxSynchronize", synchronize);
    void *context = NULL, *module = NULL, *function = NULL;
    uint64_t device_input = 0, device_output = 0;
    unsigned int count = requested, bytes = count * sizeof(unsigned int);
    unsigned int *input = malloc(bytes), *output = calloc(count, sizeof(unsigned int));
    if (!input || !output) return 6;
    for (unsigned int i = 0; i < count; ++i) input[i] = 3 * i + 11;
    CHECK("CUDA_INIT", init(0));
    CHECK("CUDA_CONTEXT", create(&context, 0, 0));
    CHECK("CUDA_MODULE", module_load(&module, argv[2]));
    CHECK("CUDA_FUNCTION", module_function(&function, module, "add_seven"));
    CHECK("CUDA_INPUT_ALLOC", allocate(&device_input, bytes));
    CHECK("CUDA_OUTPUT_ALLOC", allocate(&device_output, bytes));
    CHECK("CUDA_UPLOAD", upload(device_input, input, bytes));
    CHECK("CUDA_OUTPUT_CLEAR", upload(device_output, output, bytes));
    void *parameters[] = {&device_input, &device_output, &count};
    CHECK("CUDA_LAUNCH", launch(function, (count + 63) / 64, 1, 1, 64, 1, 1, 0, NULL, parameters, NULL));
    CHECK("CUDA_SYNC", synchronize());
    CHECK("CUDA_DOWNLOAD", download(output, device_output, bytes));
    unsigned int errors = 0;
    for (unsigned int i = 0; i < count; ++i) {
        if (output[i] != input[i] + 7) {
            if (errors < 4) printf("MISMATCH[%u]=%u expected=%u\n", i, output[i], input[i] + 7);
            ++errors;
        }
    }
    printf("GPU_CALCULATED_ELEMENTS=%u ERRORS=%u\n", count, errors);
    CHECK("CUDA_INPUT_FREE", release(device_input));
    CHECK("CUDA_OUTPUT_FREE", release(device_output));
    CHECK("CUDA_MODULE_UNLOAD", module_unload(module));
    CHECK("CUDA_CONTEXT_DESTROY", destroy(context));
    free(input);
    free(output);
    puts(errors ? "CUDA_KERNEL_TEST=FAIL" : "CUDA_KERNEL_TEST=PASS");
    return errors ? 5 : 0;
}
