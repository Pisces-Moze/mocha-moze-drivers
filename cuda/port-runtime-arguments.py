#!/usr/bin/env python3
"""Preserve NVCC cubin argument offsets instead of rebuilding a PTX layout."""
def write(path, text):
    if path.read_text() != text:
        path.write_text(text)

def apply(root):
    p = root/'cuda/runtime/ocelot/ir/implementation/Module.cpp'
    s = p.read_text()
    old = '\t\t\t\tkernel->arguments.push_back(argument);'
    new = '''                // MOCHA_CUBIN_ARGUMENT_OFFSETS: metadata is authoritative.
                if (param_data->idx < 0 || param_data->idx >= 256 ||
                    param_data->offset > func->param_size ||
                    param_data->size > func->param_size - param_data->offset)
                    throw hydrazine::Exception("Invalid cubin parameter metadata");
                argument.offset = param_data->offset;
                if (kernel->arguments.size() <= (unsigned)param_data->idx)
                    kernel->arguments.resize(param_data->idx + 1);
                kernel->arguments[param_data->idx] = argument;'''
    assert old in s or 'MOCHA_CUBIN_ARGUMENT_OFFSETS' in s
    write(p, s.replace(old, new, 1))

    p = root/'cuda/runtime/ocelot/executive/implementation/ExecutableKernel.cpp'
    s = p.read_text()
    if '#include <ocelot/ir/interface/Module.h>' not in s:
        s = s.replace('#include <memory.h>', '#include <memory.h>\n#include <ocelot/ir/interface/Module.h>')
    old = '\t\tunsigned int misAlignment = size % it->getAlignment();'
    new = '''        // MOCHA_PRESERVE_NATIVE_LAYOUT: includes padding, 64-bit and struct arguments.
        if (module && module->getCubin()) {
            unsigned int end = it->offset + it->getSize();
            if (end > size) size = end;
            continue;
        }
'''+old
    if 'MOCHA_PRESERVE_NATIVE_LAYOUT' not in s:
        assert old in s
        s = s.replace(old, new, 1)
    write(p, s)

    p = root/'cuda/runtime/ocelot/cuda/implementation/CudaRuntime.cpp'
    s = p.read_text()
    old = '\t\t\tunsigned int misalignment = dst % parameter->getAlignment();'
    new = '''            // MOCHA_HOST_TO_CUBIN_LAYOUT
            if (kernel->module && kernel->module->getCubin()) {
                dst = parameter->offset;
                if (dst > parameterBlockSize || parameter->getSize() > parameterBlockSize - dst ||
                    *offset > parameterBlockSize || *size > parameterBlockSize - *offset ||
                    *size != parameter->getSize()) {
                    free(temp);
                    throw hydrazine::Exception("Invalid native kernel argument size");
                }
            }
'''+old
    if 'MOCHA_HOST_TO_CUBIN_LAYOUT' not in s:
        assert old in s
        s = s.replace(old, new, 1)
        s = s.replace('dst += alignmentOffset;',
            'if (!(kernel->module && kernel->module->getCubin())) dst += alignmentOffset;', 1)
    marker = '\treport("cudaSetupArgument() - offset " << offset << ", size " << size);'
    if 'MOCHA_SETUP_ARGUMENT_BOUNDS' not in s:
        s = s.replace(marker, marker+'''
    // MOCHA_SETUP_ARGUMENT_BOUNDS
    if ((!arg && size) || offset > thread.parameterBlockSize ||
        size > thread.parameterBlockSize - offset) {
        _unlock();
        return _setLastError(cudaErrorInvalidValue);
    }
''', 1)
    marker = 'cudaError_t cuda::CudaRuntime::cudaMalloc(void **devPtr, size_t size) {'
    if 'MOCHA_MALLOC_ARGUMENTS' not in s:
        s = s.replace(marker, marker+'''
    // MOCHA_MALLOC_ARGUMENTS
    if (!devPtr) return _setLastError(cudaErrorInvalidValue);
    *devPtr = 0;
    if (!size) return _setLastError(cudaSuccess);
''', 1)
    write(p, s)
