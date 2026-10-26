#!/usr/bin/env python3
"""Cross-build the pinned open Gdev Runtime into a private ARMhf prefix."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile

ARCHIVE_SHA256 = 'c54f137616525e8b0357328688043f7bc5766e236f5dd6f4a89e38e873561d8b'

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('work', type=Path, help='new directory; existing directories are rejected')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if hashlib.file_digest(args.archive.open('rb'), 'sha256').hexdigest() != ARCHIVE_SHA256:
        parser.error('Gdev archive SHA256 mismatch')
    here = Path(__file__).resolve().parent
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    with tarfile.open(args.archive) as archive:
        archive.extractall(work, filter='data')
    source = work/'gdev-master'
    with (here/'gdev-mocha.patch').open('rb') as patch:
        subprocess.run(['patch', '-p1', '--batch'], cwd=source, stdin=patch, check=True)
    run('python3', here/'apply-runtime-port.py', source)
    build = work/'build'
    run('cmake', '-S', source, '-B', build, '-Ddriver=nouveau', '-Duser=ON',
        '-Druntime=ON', '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_SYSTEM_PROCESSOR=armv7',
        '-DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc',
        '-DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++',
        '-DCMAKE_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf',
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
    run('cmake', '--build', build, '-j', args.jobs)
    prefix = work/'prefix'
    (prefix/'lib').mkdir(parents=True)
    (prefix/'bin').mkdir()
    run('arm-linux-gnueabihf-gcc', '-O2', '-Wall', '-Wextra',
        here/'probe-runtime-api.c', '-ldl', '-o', prefix/'bin/probe-runtime-api')
    pending = [build/'cuda/libucuda.so.1.0.0', build/'lib/libgdev.so.1.0.0']
    copied = set()
    while pending:
        library = pending.pop()
        if library.name in copied:
            continue
        copied.add(library.name)
        shutil.copyfile(library, prefix/'lib'/library.name)
        dynamic = subprocess.check_output(['arm-linux-gnueabihf-readelf', '-d', str(library)], text=True)
        for name in re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic):
            if name.startswith(('libboost_', 'libdrm')):
                pending.append(Path('/usr/lib/arm-linux-gnueabihf')/name)
    for name, target in {
        'libgdev.so.1': 'libgdev.so.1.0.0', 'libucuda.so.1': 'libucuda.so.1.0.0',
        'libcuda.so.1': 'libucuda.so.1.0.0', 'libcudart.so': 'libucuda.so.1.0.0',
        'libcudart.so.3': 'libucuda.so.1.0.0',
    }.items():
        (prefix/'lib'/name).symlink_to(target)
    manifest = {str(p.relative_to(prefix)): {
        'bytes': p.stat().st_size,
        'sha256': hashlib.file_digest(p.open('rb'), 'sha256').hexdigest(),
    } for p in prefix.rglob('*') if p.is_file() and not p.is_symlink()}
    (work/'runtime-build-files.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(prefix)

if __name__ == '__main__':
    main()
