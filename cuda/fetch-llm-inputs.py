#!/usr/bin/env python3
"""Fetch only the pinned MIT-licensed engine source, model and tokenizer."""
from pathlib import Path
import argparse
import hashlib
import json
import urllib.request

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    lock=Path(__file__).with_name('llm-inputs-2026-10-25.json')
    config=json.loads(lock.read_text(encoding='utf-8'))
    args.directory.mkdir(parents=True,exist_ok=True)
    for name,item in config['inputs'].items():
        target=args.directory/name
        if target.exists():
            assert hashlib.file_digest(target.open('rb'),'sha256').hexdigest()==item['sha256'],target
            continue
        request=urllib.request.Request(item['url'],headers={'User-Agent':'Mocha-CUDA-LLM'})
        with urllib.request.urlopen(request,timeout=60) as response:
            data=response.read(item['bytes']+1)
        assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],name
        target.write_bytes(data)
        print(name,len(data))
    (args.directory/'inputs.json').write_bytes(lock.read_bytes())

if __name__=='__main__':main()
