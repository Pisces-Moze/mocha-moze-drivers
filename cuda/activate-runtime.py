#!/usr/bin/env python3
"""Activate an already tested private Mocha CUDA Runtime prefix on the tablet."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--bin-dir', type=Path, default=Path('/srv/mocha-data/codex-debian-desktop/local/bin'))
    parser.add_argument('--link-dir', type=Path, default=Path('/usr/local/bin'))
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    manifest = json.loads(args.manifest.read_text())
    for relative, expected in manifest.items():
        target = (prefix/relative).resolve(strict=True)
        if not target.is_relative_to(prefix):
            raise ValueError('Manifest path escapes the Runtime prefix')
        assert target.stat().st_size == expected['bytes']
        assert hashlib.file_digest(target.open('rb'), 'sha256').hexdigest() == expected['sha256']
    results = json.loads((prefix/'runtime-api-result.json').read_text())
    assert results['returncode'] == 0
    assert {r['elements'] for r in results['kernel_tests'] if r['returncode'] == 0} >= {257, 8193}
    assert all(r['returncode'] == 0 for r in results['kernel_tests'])
    assert results['library_sha256'] == manifest['lib/libucuda.so.1.0.0']['sha256']
    here = Path(__file__).resolve().parent
    bindir = args.bin_dir.resolve()
    bindir.mkdir(parents=True, exist_ok=True)
    backup = prefix/'activation-backup'
    backup.mkdir(exist_ok=True)
    def preserve(path, group='local'):
        (backup/group).mkdir(exist_ok=True)
        dest = backup/group/path.name
        if os.path.lexists(path) and not os.path.lexists(dest):
            if path.is_symlink():
                dest.symlink_to(os.readlink(path))
            else:
                shutil.copy2(path, dest)
    def symlink(target, path):
        temp = path.with_name(path.name+'.incoming')
        if os.path.lexists(temp):
            raise FileExistsError(temp)
        temp.symlink_to(target)
        temp.replace(path)
    current = prefix.parent/'cuda-runtime-current'
    preserve(current)
    symlink(prefix.name, current)
    for name in ['mocha-cuda-runtime-run', 'mocha-cuda-runtime-selftest']:
        target = bindir/name
        preserve(target)
        temp = target.with_suffix('.incoming')
        if temp.exists():
            raise FileExistsError(temp)
        temp.write_bytes((here/name).read_bytes())
        temp.chmod(0o755)
        temp.replace(target)
    for name, target in [('mocha-cuda-run', 'mocha-cuda-runtime-run'),
                         ('mocha-cuda-selftest', 'mocha-cuda-runtime-selftest')]:
        preserve(bindir/name)
        symlink(target, bindir/name)
    linkdir = args.link_dir.resolve()
    linkdir.mkdir(parents=True, exist_ok=True)
    if linkdir != bindir:
        for name in ['mocha-cuda-runtime-run', 'mocha-cuda-runtime-selftest',
                     'mocha-cuda-run', 'mocha-cuda-selftest']:
            preserve(linkdir/name, 'system-links')
            symlink(bindir/name, linkdir/name)
    report = {'prefix': str(prefix), 'library_sha256': results['library_sha256'],
              'bin_dir': str(bindir), 'backup': str(backup)}
    (prefix/'activation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
