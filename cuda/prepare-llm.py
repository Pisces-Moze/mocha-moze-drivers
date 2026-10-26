#!/usr/bin/env python3
"""Add the small CUDA backend to the pinned upstream llama2.c run.c."""
from pathlib import Path
import argparse
import hashlib
import json

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('lock',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    raw=args.source.read_bytes()
    lock=json.loads(args.lock.read_text())
    assert hashlib.sha256(raw).hexdigest()==lock['inputs']['run.c']['sha256']
    s=raw.decode('utf-8')
    marker='float* forward(Transformer* transformer, int token, int pos) {'
    assert s.count(marker)==1
    begin=s.index(marker);end=s.index('\n}',begin)+2
    s=s[:begin]+'''extern int mocha_llm_cuda_init(const void *, size_t, int);
extern int mocha_llm_cuda_enabled(void);
extern int mocha_llm_cuda_check_enabled(void);
extern int mocha_llm_cuda_forward(float *,int,int);
extern int mocha_llm_cuda_compare(const float *,const float *);
extern int mocha_llm_cuda_finish(void);
'''+s[begin:end].replace(marker,'float* forward_cpu(Transformer* transformer, int token, int pos) {',1)+'''
float* forward(Transformer* transformer, int token, int pos) {
    if(!mocha_llm_cuda_enabled())return forward_cpu(transformer,token,pos);
    float *output=transformer->state.logits;
    if(mocha_llm_cuda_forward(output,token,pos))exit(EXIT_FAILURE);
    if(mocha_llm_cuda_check_enabled()){
        float actual[512];memcpy(actual,output,sizeof(actual));
        float *reference=forward_cpu(transformer,token,pos);
        if(mocha_llm_cuda_compare(actual,reference))exit(EXIT_FAILURE);
        memcpy(output,actual,sizeof(actual));
    }
    return output;
}
'''+s[end:]
    marker='    build_transformer(&transformer, checkpoint_path);'
    assert s.count(marker)==1
    s=s.replace(marker,marker+'''
    int capacity=transformer.config.vocab_size;
    if(capacity<transformer.config.hidden_dim)capacity=transformer.config.hidden_dim;
    if(capacity<transformer.config.dim)capacity=transformer.config.dim;
    if(mocha_llm_cuda_init(transformer.data,transformer.file_size,capacity))return EXIT_FAILURE;
''',1)
    marker='    free_transformer(&transformer);'
    assert s.count(marker)==1
    s=s.replace(marker,'    if(mocha_llm_cuda_finish())return EXIT_FAILURE;\n'+marker,1)
    args.output.write_text(s,encoding='utf-8')

if __name__=='__main__':main()
