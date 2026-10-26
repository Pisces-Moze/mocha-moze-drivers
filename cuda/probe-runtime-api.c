/* SPDX-License-Identifier: MIT */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD(name) do { *(void **)(&name)=dlsym(lib,#name); if(!name){fprintf(stderr,"Missing %s\n",#name);return 3;} }while(0)
#define CHECK(call) do { int e=(call);printf("%s=%d\n",#call,e);if(e)return 4; }while(0)
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc!=2)return 2;
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"%s\n",dlerror());return 3;}
    int (*cudaRuntimeGetVersion)(int *),(*cudaGetDeviceCount)(int *);
    int (*cudaMalloc)(void **,size_t),(*cudaFree)(void *);
    int (*cudaMemcpy)(void *,const void *,size_t,int);
    int (*cudaMemset)(void *,int,size_t),(*cudaThreadSynchronize)(void);
    int (*cudaGetLastError)(void),(*cudaPeekAtLastError)(void);
    LOAD(cudaRuntimeGetVersion);LOAD(cudaGetDeviceCount);LOAD(cudaMalloc);
    LOAD(cudaFree);LOAD(cudaMemcpy);LOAD(cudaMemset);LOAD(cudaThreadSynchronize);
    LOAD(cudaGetLastError);LOAD(cudaPeekAtLastError);
    int version=0,count=0;void *a=NULL,*b=NULL;
    uint32_t input[257],output[257];
    for(unsigned i=0;i<257;i++)input[i]=0x5a230000u+i*31;
    CHECK(cudaRuntimeGetVersion(&version));CHECK(cudaGetDeviceCount(&count));
    printf("RUNTIME_VERSION=%d DEVICE_COUNT=%d\n",version,count);
    if(count!=1)return 5;
    /* Runtime cudaErrorInvalidValue=11; Driver CUDA_ERROR_INVALID_VALUE=1. */
    if(cudaMalloc(NULL,16)!=11 || cudaPeekAtLastError()!=11 ||
        cudaGetLastError()!=11 || cudaGetLastError()!=0)return 8;
    void *zero=(void *)(uintptr_t)1;
    CHECK(cudaMalloc(&zero,0));
    if(zero)return 9;
    CHECK(cudaFree(NULL));
    puts("CUDA_RUNTIME_INVALID_ARGUMENT_AND_LAST_ERROR=PASS");
    CHECK(cudaMalloc(&a,sizeof(input)));CHECK(cudaMalloc(&b,sizeof(input)));
    CHECK(cudaMemcpy(a,input,sizeof(input),1));
    CHECK(cudaMemcpy(b,a,sizeof(input),3));
    CHECK(cudaThreadSynchronize());
    CHECK(cudaMemcpy(output,b,sizeof(output),2));
    if(memcmp(input,output,sizeof(input)))return 6;
    CHECK(cudaMemset(b,0,sizeof(input)));
    CHECK(cudaMemcpy(output,b,sizeof(output),2));
    for(unsigned i=0;i<257;i++)if(output[i])return 7;
    CHECK(cudaFree(b));CHECK(cudaFree(a));
    puts("CUDA_RUNTIME_ALLOCATION_COPY_MEMSET=PASS");
    return 0;
}
