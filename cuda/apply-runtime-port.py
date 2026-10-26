#!/usr/bin/env python3
"""Apply the Runtime follow-up to the pinned, Mocha-patched Gdev source."""
from pathlib import Path
import argparse
import importlib.util

def write(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

def apply(root):
    p = root/'cuda/CMakeLists.txt'
    s = p.read_text()
    if 'ADD_DEFINITIONS(-DENABLE_CUBIN_MODULE)' not in s:
        s = s.replace('IF(runtime)\n', 'IF(runtime)\n    ADD_DEFINITIONS(-DENABLE_CUBIN_MODULE)\n', 1)
    write(p, s)
    write(root/'cuda/driver/mocha_runtime_memory.c', Path(__file__).with_name('gdev-runtime-memory.c').read_text())
    p = root/'cuda/driver/dummy.c'
    s = p.read_text()
    s = s.replace('return CUDA_SUCCESS;', 'return CUDA_ERROR_UNKNOWN; /* unsupported: never report fake success */')
    write(p, s)
    edits = {
        'cuda/runtime/ocelot/analysis/implementation/ControlTree.cpp':
            ('boost::ref(this)', 'this'),
        'cuda/runtime/hydrazine/interface/ELFFile.h':
            ('sectionTypeToString(int type)', 'sectionTypeToString(unsigned int type)'),
        'cuda/runtime/hydrazine/implementation/ELFFile.cpp':
            ('sectionTypeToString(int type)', 'sectionTypeToString(unsigned int type)'),
    }
    for relative, (old, new) in edits.items():
        p = root/relative
        s = p.read_text()
        assert old in s or new in s, relative
        write(p, s.replace(old, new))
    # Gdev's Driver ABI always uses 64-bit device pointers, including ARMhf.
    # Ocelot's historical SDK headers selected 32-bit CUdeviceptr on ARM and
    # therefore corrupted both output buffers and AAPCS argument placement.
    old = '''#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
typedef unsigned long long CUdeviceptr;
#else
typedef unsigned int CUdeviceptr;
#endif'''
    for name in ['cuda.h', 'cuda_internal.h']:
        p = root/'cuda/runtime/ocelot/cuda/interface'/name
        s = p.read_text()
        new = '/* Gdev device addresses retain 64 bits on every host ABI. */\ntypedef unsigned long long CUdeviceptr;'
        assert old in s or new in s, name
        write(p, s.replace(old, new, 1))
    p = root/'cuda/driver/device.c'
    s = p.read_text()
    begin = s.index('CUresult cuDeviceGetAttribute(')
    prefix, body = s[:begin], s[begin:]
    if 'MOCHA_RUNTIME_ATTRIBUTES' not in body:
        marker = '\tswitch (attrib) {'
        assert marker in body and 'chipset == 0xea' in body
        block = '''    /* MOCHA_RUNTIME_ATTRIBUTES: GK20A is an integrated SoC, not PCI.
     * The K1 whitepaper documents 128 KiB L2; its memory interface is 64 bit.
     * Optional operations not implemented by this backend stay disabled.
     * Clock metadata is unavailable through Gdev; report zero, not garbage.
     */
    if (chipset == 0xea) {
        int handled = 1;
        switch (attrib) {
        case CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH: *pi = 64; break;
        case CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE: *pi = 128 * 1024; break;
        case CU_DEVICE_ATTRIBUTE_INTEGRATED: *pi = 1; break;
        case CU_DEVICE_ATTRIBUTE_ECC_ENABLED:
        case CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING:
        case CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY:
        case CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS:
        case CU_DEVICE_ATTRIBUTE_GPU_OVERLAP:
        case CU_DEVICE_ATTRIBUTE_CLOCK_RATE:
        case CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE: *pi = 0; break;
        default: handled = 0; break;
        }
        if (handled) { __gclose(handle); return CUDA_SUCCESS; }
    }
'''
        body = body.replace(marker, block+marker, 1)
        body = body.replace('\tuint64_t chipset;\n', '\tuint64_t chipset;\n\n\tif (!pi) return CUDA_ERROR_INVALID_VALUE;\n\t*pi = 0;\n', 1)
    write(p, prefix+body)
    p = root/'cuda/driver/device.c'
    s = p.read_text()
    begin = s.index('CUresult cuDeviceGetName(')
    end = s.index('\n/**', begin)
    body = s[begin:end]
    body = body.replace('GDEV_NVIDIA_QUERY_CHIPSET', 'GDEV_QUERY_CHIPSET')
    if 'MOCHA_SOC_NAME' not in body:
        body = body.replace('if (!name)', 'if (!name || len <= 0)')
        marker = '\tif (gquery(handle, GDEV_QUERY_PCI_VENDOR, &pci_vendor)) {'
        body = body.replace(marker, '''    /* MOCHA_SOC_NAME: integrated GK20A has no PCI identity. */
    {
        uint64_t chip = 0;
        if (!gquery(handle, GDEV_QUERY_CHIPSET, &chip) && chip == 0xea) {
            strncpy(name, "NVIDIA GK20A (Tegra K1)", len - 1);
            name[len - 1] = '\\0';
            __gclose(handle);
            return CUDA_SUCCESS;
        }
    }
'''+marker, 1)
    write(p, s[:begin]+body+s[end:])
    # Required attributes must contain real cubin metadata, not untouched output.
    p = root/'cuda/driver/execution.c'
    s = p.read_text()
    old = '\tGDEV_PRINT("cuFuncGetAttribute: Not Implemented Yet\\n");\n\treturn CUDA_SUCCESS;'
    new = '''    /* MOCHA_FUNCTION_METADATA */
    struct CUctx_st *ctx = NULL;
    struct gdev_cuda_raw_func *raw;
    CUresult result;
    unsigned int i, total = 0;
    if (!gdev_initialized) return CUDA_ERROR_NOT_INITIALIZED;
    if (!pi || !hfunc || !hfunc->mod) return CUDA_ERROR_INVALID_VALUE;
    *pi = 0;
    result = cuCtxGetCurrent(&ctx);
    if (result != CUDA_SUCCESS) return result;
    if (!ctx || hfunc->mod->ctx != ctx) return CUDA_ERROR_INVALID_CONTEXT;
    raw = &hfunc->raw_func;
    switch (attrib) {
    case CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK:
        /* Backend's supported per-block ceiling, also bounded by registers. */
        total = ctx->cuda_info.warp_size * ctx->cuda_info.warp_count;
        if (total > 1024) total = 1024;
        if (ctx->cuda_info.chipset == 0xea && raw->reg_count) {
            unsigned int by_regs = (65536 / raw->reg_count) & ~31U;
            if (total > by_regs) total = by_regs;
        }
        *pi = total;
        break;
    case CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES: *pi = raw->shared_size; break;
    case CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES: *pi = raw->local_size; break;
    case CU_FUNC_ATTRIBUTE_NUM_REGS: *pi = raw->reg_count; break;
    case CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES:
        for (i = 0; i < GDEV_NVIDIA_CONST_SEGMENT_MAX_COUNT; ++i)
            total += raw->cmem[i].size;
        *pi = total;
        break;
    default: return CUDA_ERROR_INVALID_VALUE;
    }
    return CUDA_SUCCESS;'''
    assert old in s or 'MOCHA_FUNCTION_METADATA' in s
    s = s.replace(old, new, 1)
    # Unsupported operations outside dummy.c also used to claim success.
    s = s.replace('GDEV_PRINT("cuLaunchGridAsync: Not Implemented Yet\\n");\n\treturn CUDA_SUCCESS;',
        'GDEV_PRINT("cuLaunchGridAsync: Not Implemented Yet\\n");\n\treturn CUDA_ERROR_UNKNOWN;')
    s = s.replace('GDEV_PRINT("cuParamSetTexRef: Not Implemented Yet\\n");\n\treturn CUDA_SUCCESS;',
        'GDEV_PRINT("cuParamSetTexRef: Not Implemented Yet\\n");\n\treturn CUDA_ERROR_UNKNOWN;')
    write(p, s)
    p = root/'cuda/runtime/ocelot/executive/implementation/NVIDIAGPUDevice.cpp'
    s = p.read_text()
    marker = '\t\tcheckError(driver::cuMemAlloc(&_devicePointer, size));'
    if 'MOCHA_HOST_POINTER_RANGE' not in s:
        s = s.replace(marker, marker+'''
        // MOCHA_HOST_POINTER_RANGE: Runtime void* must represent the whole allocation.
        const CUdeviceptr maximum = static_cast<CUdeviceptr>(static_cast<size_t>(-1));
        if (_devicePointer > maximum || (size && size - 1 > maximum - _devicePointer)) {
            driver::cuMemFree(_devicePointer);
            _devicePointer = 0;
            Throw("GPU allocation does not fit the host pointer ABI");
        }
''', 1)
    write(p, s)
    p = root/'cuda/runtime/ocelot/executive/implementation/NVIDIAExecutableKernel.cpp'
    s = p.read_text()
    s = s.replace('\t_sharedMemorySize = bytes;', '\t_externSharedMemorySize = bytes;')
    marker = '\t_argumentMemorySize = mapArgumentOffsets();'
    if 'MOCHA_KERNEL_THREAD_LIMIT' not in s:
        s = s.replace(marker, marker+'''
    // MOCHA_KERNEL_THREAD_LIMIT: replace the emulator's 16384-thread default.
    cuda::CudaDriver::cuFuncGetAttribute((int*)&_maxThreadsPerBlock,
        CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, cuFunction);
''', 1)
    end = '\t\t\t<< "- failed to set parameter data");\n\t}\n}'
    s = s.replace(end, end[:-1]+'\tdelete [] argBuffer; // argument data has been copied by cuParamSetv\n}', 1)
    write(p, s)
    p = root/'cuda/runtime/ocelot/executive/implementation/NVIDIAGPUDevice.cpp'
    s = p.read_text()
    s = s.replace('\t\tattributes.ptxVersion = 21;\n\t\tattributes.binaryVersion = 21;', '''        // MOCHA_NATIVE_ARCH_METADATA: never report the historical sm_21 default.
        attributes.ptxVersion = 0;
        attributes.binaryVersion = 0;
        const unsigned char* image = (const unsigned char*)module->second.ir->getCubin();
        if (image && !std::memcmp(image, "\\177ELF", 4) && (image[4] == 1 || image[4] == 2)) {
            unsigned int flags = 0;
            std::memcpy(&flags, image + (image[4] == 1 ? 36 : 48), sizeof(flags));
            attributes.binaryVersion = flags & 0xff;
            attributes.ptxVersion = (flags >> 16) & 0xff;
        }''')
    write(p, s)
    # An idle stream can still contain a newly recorded event. Complete it.
    p = root/'cuda/driver/stream.c'
    s = p.read_text()
    s = s.replace('\tif (gdev_list_empty(&stream->sync_list))\n\t\treturn CUDA_SUCCESS;\n', '')
    s = s.replace('\tGDEV_PRINT("cuStreamQuery: Not Implemented Yet\\n");\n\treturn CUDA_SUCCESS;', '''    struct CUctx_st *ctx = NULL;
    CUresult result;
    if (!gdev_initialized) return CUDA_ERROR_NOT_INITIALIZED;
    if (!hStream) return CUDA_ERROR_INVALID_HANDLE;
    result = cuCtxGetCurrent(&ctx);
    if (result != CUDA_SUCCESS) return result;
    if (!ctx || hStream->ctx != ctx) return CUDA_ERROR_INVALID_CONTEXT;
    /* Conservative nonblocking status: explicit synchronize retires fences. */
    return gdev_list_empty(&hStream->sync_list) ? CUDA_SUCCESS : CUDA_ERROR_NOT_READY;''')
    write(p, s)
    p = root/'cuda/runtime/ocelot/cuda/implementation/CudaRuntime.cpp'
    s = p.read_text()
    s = s.replace('\t\t_getDevice().queryStream(stream);\n\t\tresult = cudaSuccess;',
        '\t\tresult = _getDevice().queryStream(stream) ? cudaSuccess : cudaErrorNotReady;')
    write(p, s)
    spec = importlib.util.spec_from_file_location('mocha_elf32_port', Path(__file__).with_name('port-elf32.py'))
    port = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(port)
    port.apply(root)
    spec = importlib.util.spec_from_file_location('mocha_runtime_arguments', Path(__file__).with_name('port-runtime-arguments.py'))
    arguments = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(arguments)
    arguments.apply(root)
    spec = importlib.util.spec_from_file_location('mocha_runtime_events', Path(__file__).with_name('port-runtime-events.py'))
    events = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(events)
    events.apply(root)
    print('MOCHA_RUNTIME_PORT_APPLIED')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    apply(parser.parse_args().source)
