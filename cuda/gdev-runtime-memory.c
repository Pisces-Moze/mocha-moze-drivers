/* SPDX-License-Identifier: MIT */
/* Real memory fills through the verified Gdev transfer/cache path. */
#include "cuda.h"
#include <stdint.h>
#include <string.h>

static CUresult mocha_fill(CUdeviceptr address, uint32_t value,
                          unsigned int count, unsigned int width)
{
    unsigned char buffer[4096];
    CUcontext context;
    CUresult result = cuCtxGetCurrent(&context);
    uint64_t total = (uint64_t)count * width;
    if (result != CUDA_SUCCESS) return result;
    if (!count) return CUDA_SUCCESS;
    if (!address || address % width || address + total < address)
        return CUDA_ERROR_INVALID_VALUE;
    for (unsigned int i = 0; i < sizeof(buffer); i += width)
        memcpy(buffer + i, &value, width);
    while (total) {
        unsigned int amount = total > sizeof(buffer) ? sizeof(buffer) : total;
        result = cuMemcpyHtoD(address, buffer, amount);
        if (result != CUDA_SUCCESS) return result;
        address += amount;
        total -= amount;
    }
    return CUDA_SUCCESS;
}

CUresult cuMemsetD8(CUdeviceptr p, unsigned char v, unsigned int n)
{ return mocha_fill(p, v, n, 1); }
CUresult cuMemsetD16(CUdeviceptr p, unsigned short v, unsigned int n)
{ return mocha_fill(p, v, n, 2); }
CUresult cuMemsetD32(CUdeviceptr p, unsigned int v, unsigned int n)
{ return mocha_fill(p, v, n, 4); }
CUresult cuMemsetD8_v2(CUdeviceptr p, unsigned char v, unsigned int n)
{ return cuMemsetD8(p, v, n); }
CUresult cuMemsetD16_v2(CUdeviceptr p, unsigned short v, unsigned int n)
{ return cuMemsetD16(p, v, n); }
CUresult cuMemsetD32_v2(CUdeviceptr p, unsigned int v, unsigned int n)
{ return cuMemsetD32(p, v, n); }
