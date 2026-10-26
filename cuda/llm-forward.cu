// SPDX-License-Identifier: MIT
// Fused Transformer forward for Karpathy's exact TinyStories 260K checkpoint.
// Tokenization and sampling remain in the pinned MIT-licensed llama2.c engine.
// Layout/equations adapted from llama2.c; see LLAMA2-LICENSE.txt.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>

static float *device_weights,*device_cache,*device_logits;
static int enabled,validate;
static unsigned long long calls,checked,errors;
static float maximum_error;
static const unsigned weight_bytes=1056540;

static int check(cudaError_t error,const char *operation)
{
    if(error==cudaSuccess)return 0;
    std::fprintf(stderr,"LLM CUDA %s failed: %d %s\n",operation,(int)error,cudaGetErrorString(error));
    return 1;
}
#define CUDA_TRY(operation) do { if(check((operation),#operation))return 1; } while(0)

__global__ void transformer_forward(const float *checkpoint,float *cache,
                                    float *logits,int token,int position)
{
    const unsigned tid=threadIdx.x;
    const float *embedding=checkpoint+7;
    const float *rms_att=embedding+512*64;
    const float *wq=rms_att+5*64;
    const float *wk=wq+5*64*64;
    const float *wv=wk+5*32*64;
    const float *wo=wv+5*32*64;
    const float *rms_ffn=wo+5*64*64;
    const float *w1=rms_ffn+5*64;
    const float *w2=w1+5*172*64;
    const float *w3=w2+5*64*172;
    const float *rms_final=w3+5*172*64;
    float *key_cache=cache,*value_cache=cache+5*512*32;
    __shared__ float x[64],norm[64],q[64],k[32],v[32],hidden[172];
    __shared__ float attention[8*512],scale;
    if(tid<64)x[tid]=embedding[token*64+tid];
    __syncthreads();
    for(int layer=0;layer<5;layer++){
        if(!tid){float sum=0;for(int i=0;i<64;i++)sum+=x[i]*x[i];scale=rsqrtf(sum/64+1e-5f);}
        __syncthreads();
        if(tid<64)norm[tid]=x[tid]*scale*rms_att[layer*64+tid];
        __syncthreads();
        if(tid<64){
            float sum=0;
            for(int j=0;j<64;j++)sum+=wq[layer*64*64+tid*64+j]*norm[j];
            q[tid]=sum;
        }
        if(tid<32){
            float a=0,b=0;
            for(int j=0;j<64;j++){
                a+=wk[layer*32*64+tid*64+j]*norm[j];
                b+=wv[layer*32*64+tid*64+j]*norm[j];
            }
            k[tid]=a;v[tid]=b;
        }
        __syncthreads();
        if(tid<32){
            unsigned i=tid*2;
            float frequency=(tid%4==0)?1.0f:((tid%4==1)?0.1f:((tid%4==2)?0.01f:0.001f));
            float sine,cosine;__sincosf(position*frequency,&sine,&cosine);
            float a=q[i],b=q[i+1];q[i]=a*cosine-b*sine;q[i+1]=a*sine+b*cosine;
            if(i<32){
                a=k[i];b=k[i+1];
                key_cache[layer*512*32+position*32+i]=a*cosine-b*sine;
                key_cache[layer*512*32+position*32+i+1]=a*sine+b*cosine;
            }
            value_cache[layer*512*32+position*32+tid]=v[tid];
        }
        __syncthreads();
        if(tid<8){
            const unsigned head=tid,kv_head=head/2;
            float largest=-FLT_MAX,total=0;
            float *att=attention+head*512;
            for(int t=0;t<=position;t++){
                float score=0;
                for(int i=0;i<8;i++)score+=q[head*8+i]*key_cache[layer*512*32+t*32+kv_head*8+i];
                score*=0.3535533905932738f;att[t]=score;
                if(score>largest)largest=score;
            }
            for(int t=0;t<=position;t++){att[t]=__expf(att[t]-largest);total+=att[t];}
            for(int t=0;t<=position;t++)att[t]/=total;
            for(int i=0;i<8;i++){
                float sum=0;
                for(int t=0;t<=position;t++)sum+=att[t]*value_cache[layer*512*32+t*32+kv_head*8+i];
                norm[head*8+i]=sum;
            }
        }
        __syncthreads();
        if(tid<64){
            float sum=0;for(int j=0;j<64;j++)sum+=wo[layer*64*64+tid*64+j]*norm[j];x[tid]+=sum;
        }
        __syncthreads();
        if(!tid){float sum=0;for(int j=0;j<64;j++)sum+=x[j]*x[j];scale=rsqrtf(sum/64+1e-5f);}
        __syncthreads();
        if(tid<64)norm[tid]=x[tid]*scale*rms_ffn[layer*64+tid];
        __syncthreads();
        for(unsigned row=tid;row<172;row+=128){
            float a=0,b=0;
            for(int j=0;j<64;j++){
                a+=w1[layer*172*64+row*64+j]*norm[j];
                b+=w3[layer*172*64+row*64+j]*norm[j];
            }
            hidden[row]=(a/(1+__expf(-a)))*b;
        }
        __syncthreads();
        if(tid<64){
            float sum=0;for(int j=0;j<172;j++)sum+=w2[layer*64*172+tid*172+j]*hidden[j];x[tid]+=sum;
        }
        __syncthreads();
    }
    if(!tid){float sum=0;for(int j=0;j<64;j++)sum+=x[j]*x[j];scale=rsqrtf(sum/64+1e-5f);}
    __syncthreads();
    if(tid<64)norm[tid]=x[tid]*scale*rms_final[tid];
    __syncthreads();
    for(unsigned row=tid;row<512;row+=128){
        float sum=0;for(int j=0;j<64;j++)sum+=embedding[row*64+j]*norm[j];logits[row]=sum;
    }
}

extern "C" int mocha_llm_cuda_init(const void *base,size_t bytes,int capacity)
{
    const char *backend=std::getenv("MOCHA_LLM_BACKEND");
    enabled=!(backend && !std::strcmp(backend,"cpu"));
    if(!enabled){std::fprintf(stderr,"LLM_BACKEND=cpu\n");return 0;}
    const int expected[7]={64,172,5,8,4,512,512};
    if(!base || bytes!=weight_bytes || std::memcmp(base,expected,sizeof(expected)) || capacity!=512)return 1;
    validate=std::getenv("MOCHA_LLM_CHECK")!=NULL;
    cudaDeviceProp prop;std::memset(&prop,0,sizeof(prop));
    CUDA_TRY(cudaGetDeviceProperties(&prop,0));
    if(prop.major!=3 || prop.minor!=2)return 1;
    CUDA_TRY(cudaMalloc((void **)&device_weights,bytes));
    CUDA_TRY(cudaMalloc((void **)&device_cache,2*5*512*32*sizeof(float)));
    CUDA_TRY(cudaMalloc((void **)&device_logits,512*sizeof(float)));
    CUDA_TRY(cudaMemcpy(device_weights,base,bytes,cudaMemcpyHostToDevice));
    std::fprintf(stderr,"LLM_BACKEND=cuda OFFLOAD=transformer-forward DEVICE=%s CC=%d.%d GPU_WEIGHT_BYTES=%u GPU_CACHE_BYTES=%u CHECK=%d\n",
        prop.name,prop.major,prop.minor,(unsigned)bytes,2*5*512*32*4,validate);
    return 0;
}
extern "C" int mocha_llm_cuda_enabled(void){return enabled;}
extern "C" int mocha_llm_cuda_check_enabled(void){return validate;}
extern "C" int mocha_llm_cuda_forward(float *out,int token,int position)
{
    if(!enabled || token<0 || token>=512 || position<0 || position>=512)return 1;
    transformer_forward<<<1,128>>>(device_weights,device_cache,device_logits,token,position);
    CUDA_TRY(cudaGetLastError());
    CUDA_TRY(cudaDeviceSynchronize());
    CUDA_TRY(cudaMemcpy(out,device_logits,512*sizeof(float),cudaMemcpyDeviceToHost));
    calls++;
    return 0;
}
extern "C" int mocha_llm_cuda_compare(const float *actual,const float *reference)
{
    for(int i=0;i<512;i++){
        float difference=std::fabs(actual[i]-reference[i]);
        if(difference>maximum_error)maximum_error=difference;
        if(!(actual[i]<=FLT_MAX && actual[i]>=-FLT_MAX) || difference>5e-4f*(1+std::fabs(reference[i])))errors++;
        checked++;
    }
    if(errors)std::fprintf(stderr,"LLM CUDA logits differ from CPU: max abs %.9g errors=%llu\n",maximum_error,errors);
    return errors!=0;
}
extern "C" int mocha_llm_cuda_finish(void)
{
    unsigned long resident=0,peak=0;char line[256];
    FILE *status=std::fopen("/proc/self/status","r");
    if(status){
        while(std::fgets(line,sizeof(line),status)){
            std::sscanf(line,"VmRSS: %lu kB",&resident);
            std::sscanf(line,"VmHWM: %lu kB",&peak);
        }
        std::fclose(status);
    }
    std::fprintf(stderr,"LLM_PROCESS_RSS_KIB=%lu PEAK_RSS_KIB=%lu\n",resident,peak);
    if(enabled){CUDA_TRY(cudaFree(device_logits));CUDA_TRY(cudaFree(device_cache));CUDA_TRY(cudaFree(device_weights));}
    std::fprintf(stderr,"LLM_CUDA_KERNEL_CALLS=%llu CHECKED_LOGITS=%llu ERRORS=%llu MAX_ABS_ERROR=%.9g\n",calls,checked,errors,maximum_error);
    return enabled && (!calls || errors) ? 1 : 0;
}
