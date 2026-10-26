#!/usr/bin/env python3
"""Correct event completion for the serialized Nouveau Runtime backend."""
def write(path, text):
    if path.read_text() != text:
        path.write_text(text)

def apply(root):
    p = root/'cuda/driver/event.c'
    s = p.read_text()
    begin = s.index('CUresult cuEventRecord(')
    end = s.index('\n/**', begin)
    body = '''CUresult cuEventRecord(CUevent event, CUstream stream)
{
    /* MOCHA_SERIAL_EVENT: take a host timestamp after preceding work finishes.
     * This backend serializes Runtime execution and advertises no overlap.
     * Never timestamp both ends only at a later synchronize operation.
     */
    CUcontext ctx = NULL;
    CUresult result;
    if (!gdev_initialized) return CUDA_ERROR_NOT_INITIALIZED;
    if (!event) return CUDA_ERROR_INVALID_HANDLE;
    result = cuCtxGetCurrent(&ctx);
    if (result != CUDA_SUCCESS) return result;
    if (!ctx || event->ctx != ctx || (stream && stream->ctx != ctx))
        return CUDA_ERROR_INVALID_CONTEXT;
    result = cuCtxSynchronize();
    if (result != CUDA_SUCCESS) return result;
    if (stream) {
        result = cuStreamSynchronize(stream);
        if (result != CUDA_SUCCESS) return result;
    }
    if (event->record) gdev_list_del(&event->list_entry);
    GETTIME(&event->time);
    event->stream = NULL;
    event->record = 0;
    event->complete = 1;
    return CUDA_SUCCESS;
}
'''
    s = s[:begin]+body+s[end:]
    begin = s.index('CUresult cuEventElapsedTime(')
    end = s.index('\n/**', begin)
    body = s[begin:end]
    marker = '\tif (!gdev_initialized)'
    if 'MOCHA_EVENT_TIMING_ARGUMENTS' not in body:
        body = body.replace(marker, '''    /* MOCHA_EVENT_TIMING_ARGUMENTS */
    if (!pMilliseconds) return CUDA_ERROR_INVALID_VALUE;
    if (hStart && hEnd && ((hStart->flags | hEnd->flags) & CU_EVENT_DISABLE_TIMING))
        return CUDA_ERROR_INVALID_HANDLE;
'''+marker, 1)
    s = s[:begin]+body+s[end:]
    write(p, s)

    p = root/'cuda/runtime/ocelot/cuda/interface/CudaRuntime.h'
    s = p.read_text()
    marker = '\t\tvirtual cudaError_t  cudaStreamCreate(cudaStream_t *pStream);'
    if 'cudaStreamWaitEvent(cudaStream_t stream' not in s:
        assert marker in s
        s = s.replace(marker, marker+'\n\t\tvirtual cudaError_t cudaStreamWaitEvent(cudaStream_t stream, cudaEvent_t event, unsigned int flags);', 1)
    write(p, s)
    p = root/'cuda/runtime/ocelot/cuda/implementation/CudaRuntime.cpp'
    s = p.read_text()
    marker = 'cudaError_t cuda::CudaRuntime::cudaStreamCreate(cudaStream_t *pStream) {'
    if 'CudaRuntime::cudaStreamWaitEvent(' not in s:
        s = s.replace(marker, '''// Serialized host barrier; GPU-side concurrent wait is not implemented.
cudaError_t cuda::CudaRuntime::cudaStreamWaitEvent(cudaStream_t stream,
    cudaEvent_t event, unsigned int flags) {
    if (flags) return _setLastError(cudaErrorInvalidValue);
    if (stream) {
        cudaError_t result = cudaStreamSynchronize(stream);
        if (result != cudaSuccess) return result;
    }
    return cudaEventSynchronize(event);
}

'''+marker, 1)
    write(p, s)
