// SPDX-License-Identifier: MIT
// Runtime API only: real GPU floating-point work, shared memory and streams.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

#define CUDA(call) do { cudaError_t e=(call); if(e!=cudaSuccess) { \
    std::fprintf(stderr,"%s:%d %s: %d %s\n",__FILE__,__LINE__,#call, \
        (int)e,cudaGetErrorString(e)); return 1; } } while(0)

__global__ void saxpy(float *out, const float *a, const float *b, unsigned n)
{
    unsigned i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) out[i]=1.5f*a[i]+b[i];
}

__global__ void block_sum(float *out, const float *in, unsigned n)
{
    __shared__ float values[128];
    unsigned tid=threadIdx.x, i=blockIdx.x*128+tid;
    values[tid]=i<n?in[i]:0.0f;
    __syncthreads();
    for(unsigned step=64;step;step>>=1) {
        if(tid<step) values[tid]+=values[tid+step];
        __syncthreads();
    }
    if(!tid) out[blockIdx.x]=values[0];
}

struct MixedValue { unsigned x; double y; };
__global__ void mixed_arguments(unsigned *out, unsigned char tag,
    unsigned long long large, unsigned short small, MixedValue value)
{
    out[0]=tag+small+value.x;
    out[1]=(unsigned)large;
    out[2]=(unsigned)(large>>32);
    out[3]=(unsigned)value.y;
}

__global__ void dynamic_atomic_sum(unsigned *out, unsigned n)
{
    extern __shared__ unsigned data[];
    unsigned tid=threadIdx.x, i=blockIdx.x*128+tid;
    data[tid]=i<n?i:0;
    __syncthreads();
    for(unsigned step=64;step;step>>=1) {
        if(tid<step)data[tid]+=data[tid+step];
        __syncthreads();
    }
    if(!tid)atomicAdd(out,data[0]);
}

int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    unsigned n=argc==2?(unsigned)std::strtoul(argv[1],NULL,10):8193;
    if(!n||n>1048576) return 2;
    int count=0,version=0;
    CUDA(cudaRuntimeGetVersion(&version));
    CUDA(cudaGetDeviceCount(&count));
    if(count!=1) return 3;
    cudaDeviceProp prop;std::memset(&prop,0,sizeof(prop));
    CUDA(cudaGetDeviceProperties(&prop,0));
    std::printf("RUNTIME=%d DEVICE=%s CC=%d.%d ELEMENTS=%u\n",version,prop.name,prop.major,prop.minor,n);
    if(prop.major!=3||prop.minor!=2) return 4;
    std::vector<float> a(n),b(n),out(n),sums((n+127)/128);
    for(unsigned i=0;i<n;i++){a[i]=(i%97)*0.25f;b[i]=-(int)(i%31)*0.125f;}
    float *da=NULL,*db=NULL,*dc=NULL,*ds=NULL;
    CUDA(cudaMalloc((void **)&da,n*sizeof(float)));
    CUDA(cudaMalloc((void **)&db,n*sizeof(float)));
    CUDA(cudaMalloc((void **)&dc,n*sizeof(float)));
    CUDA(cudaMalloc((void **)&ds,sums.size()*sizeof(float)));
    CUDA(cudaMemcpy(da,&a[0],n*sizeof(float),cudaMemcpyHostToDevice));
    CUDA(cudaMemcpy(db,&b[0],n*sizeof(float),cudaMemcpyHostToDevice));
    CUDA(cudaMemset(dc,0,n*sizeof(float)));
    saxpy<<<(n+127)/128,128>>>(dc,da,db,n);
    CUDA(cudaGetLastError());
    CUDA(cudaThreadSynchronize());
    CUDA(cudaMemcpy(&out[0],dc,n*sizeof(float),cudaMemcpyDeviceToHost));
    unsigned errors=0;
    for(unsigned i=0;i<n;i++) if(out[i]!=1.5f*a[i]+b[i]) errors++;
    std::printf("SAXPY_ERRORS=%u\n",errors);
    if(errors) return 5;
    block_sum<<<(n+127)/128,128>>>(ds,dc,n);
    CUDA(cudaGetLastError());
    CUDA(cudaThreadSynchronize());
    CUDA(cudaMemcpy(&sums[0],ds,sums.size()*sizeof(float),cudaMemcpyDeviceToHost));
    for(unsigned block=0;block<sums.size();block++){
        float expected=0;
        for(unsigned i=block*128;i<n&&i<(block+1)*128;i++)expected+=out[i];
        if(sums[block]!=expected)errors++;
    }
    std::printf("SHARED_REDUCTION_ERRORS=%u\n",errors);
    if(errors)return 6;
    cudaFuncAttributes attributes;
    std::memset(&attributes,0,sizeof(attributes));
    CUDA(cudaFuncGetAttributes(&attributes,block_sum));
    std::printf("KERNEL_REGISTERS=%d STATIC_SHARED=%u MAX_THREADS=%d\n",
        attributes.numRegs,(unsigned)attributes.sharedSizeBytes,attributes.maxThreadsPerBlock);
    if(attributes.numRegs<=0 || attributes.sharedSizeBytes!=128*sizeof(float) ||
        attributes.maxThreadsPerBlock>1024 || attributes.binaryVersion!=32)return 7;
    MixedValue mixed; mixed.x=12345; mixed.y=17.0;
    mixed_arguments<<<1,1>>>((unsigned *)dc,7,0x123456789abcdef0ULL,1000,mixed);
    CUDA(cudaGetLastError());
    CUDA(cudaDeviceSynchronize());
    unsigned mixed_out[4];
    CUDA(cudaMemcpy(mixed_out,dc,sizeof(mixed_out),cudaMemcpyDeviceToHost));
    if(mixed_out[0]!=13352 || mixed_out[1]!=0x9abcdef0U ||
        mixed_out[2]!=0x12345678U || mixed_out[3]!=17)return 8;
    std::puts("MIXED_8_16_64_BIT_STRUCT_ARGUMENTS=PASS");
    CUDA(cudaMemset(dc,0,sizeof(unsigned)));
    dynamic_atomic_sum<<<(n+127)/128,128,128*sizeof(unsigned)>>>((unsigned *)dc,n);
    CUDA(cudaGetLastError());
    CUDA(cudaDeviceSynchronize());
    unsigned total=0;
    CUDA(cudaMemcpy(&total,dc,sizeof(total),cudaMemcpyDeviceToHost));
    if(total!=(unsigned)((unsigned long long)n*(n-1)/2))return 9;
    std::puts("DYNAMIC_SHARED_AND_ATOMICS=PASS");
    cudaStream_t stream;
    cudaEvent_t done,started;
    CUDA(cudaStreamCreate(&stream));
    CUDA(cudaEventCreate(&done));
    CUDA(cudaEventCreate(&started));
    CUDA(cudaEventRecord(started,stream));
    usleep(20000);
    CUDA(cudaMemset(dc,0,n*sizeof(float)));
    CUDA(cudaMemcpyAsync(da,&a[0],n*sizeof(float),cudaMemcpyHostToDevice,stream));
    saxpy<<<(n+127)/128,128,0,stream>>>(dc,da,db,n);
    CUDA(cudaGetLastError());
    CUDA(cudaEventRecord(done,stream));
    CUDA(cudaEventSynchronize(done));
    CUDA(cudaEventQuery(done));
    CUDA(cudaStreamWaitEvent(stream,done,0));
    float elapsed=0;
    CUDA(cudaEventElapsedTime(&elapsed,started,done));
    std::printf("SERIAL_EVENT_HOST_ELAPSED_MS=%.3f\n",elapsed);
    if(!(elapsed>=15.0f && elapsed<10000.0f))return 10;
    CUDA(cudaMemcpyAsync(&out[0],dc,n*sizeof(float),cudaMemcpyDeviceToHost,stream));
    CUDA(cudaStreamSynchronize(stream));
    CUDA(cudaStreamQuery(stream));
    for(unsigned i=0;i<n;i++) if(out[i]!=1.5f*a[i]+b[i]) errors++;
    CUDA(cudaEventDestroy(done));
    CUDA(cudaEventDestroy(started));
    CUDA(cudaStreamDestroy(stream));
    std::printf("STREAM_EVENT_COPY_ERRORS=%u\n",errors);
    float *pinned=NULL;
    CUDA(cudaMallocHost((void **)&pinned,n*sizeof(float)));
    std::memcpy(pinned,&a[0],n*sizeof(float));
    CUDA(cudaMemcpyAsync(da,pinned,n*sizeof(float),cudaMemcpyHostToDevice,0));
    CUDA(cudaMemcpyAsync(&out[0],da,n*sizeof(float),cudaMemcpyDeviceToHost,0));
    CUDA(cudaDeviceSynchronize());
    if(std::memcmp(&a[0],&out[0],n*sizeof(float)))return 11;
    CUDA(cudaFreeHost(pinned));
    std::puts("PINNED_HOST_ALLOCATION_COPY=PASS");
    unsigned matrix[3][13],copied[3][13];
    for(unsigned row=0;row<3;row++)for(unsigned col=0;col<13;col++)matrix[row][col]=row*100+col;
    std::memset(copied,0xa5,sizeof(copied));
    void *pitched=NULL;size_t pitch=0;
    CUDA(cudaMallocPitch(&pitched,&pitch,9*sizeof(unsigned),3));
    CUDA(cudaMemcpy2D(pitched,pitch,matrix,sizeof(matrix[0]),9*sizeof(unsigned),3,cudaMemcpyHostToDevice));
    CUDA(cudaMemcpy2D(copied,sizeof(copied[0]),pitched,pitch,9*sizeof(unsigned),3,cudaMemcpyDeviceToHost));
    for(unsigned row=0;row<3;row++)for(unsigned col=0;col<13;col++)
        if(copied[row][col]!=(col<9?matrix[row][col]:0xa5a5a5a5U))return 12;
    CUDA(cudaFree(pitched));
    std::puts("PITCHED_2D_COPY_AND_PADDING=PASS");
    CUDA(cudaFree(ds));CUDA(cudaFree(dc));CUDA(cudaFree(db));CUDA(cudaFree(da));
    if(errors)return 6;
    std::puts("CUDA_RUNTIME_FLOAT_SHARED_STREAM_EVENT=PASS");
    return 0;
}
