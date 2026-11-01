#!/usr/bin/env python3
"""Build into an ignored directory; never overwrite the archived/installed DLL."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('x86_64-w64-mingw32-clang') or 'x86_64-w64-mingw32-gcc')
    parser.add_argument('--out', type=Path, default=ROOT / '09-memory-leak/build')
    args = parser.parse_args()
    out = args.out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / '02-rendering/source'
    tests = ROOT / '09-memory-leak/verification'
    jobs = [
        ('d3d12.dll', ['-shared', '-O2', '-Wall'], [source / 'd3d12.c', source / 'd3d12.def']),
        ('test_aligned_heap.exe', ['-O2', '-Wall', '-Wextra', '-Werror'], [tests / 'test_aligned_heap.c']),
        ('cow_fixture.dll', ['-shared', '-O2', '-Wall', '-Wextra', '-Werror'], [tests / 'cow_fixture.c']),
        ('smoke.exe', ['-O2', '-Wall', '-Wextra', '-Werror'], [tests / 'smoke.c']),
        ('test_input_offsets.exe', ['-O2', '-Wall'], [ROOT / '02-rendering/verification/test_input_offsets.c']),
        ('test_event_gate.exe', ['-O2', '-Wall', '-Wl,--image-base,0x180000000'],
         [ROOT / '08-drift-zone/verification/test_event_gate.c', ROOT / '08-drift-zone/verification/test_gate_abi.S']),
    ]
    for name, flags, files in jobs:
        subprocess.run([args.cc, *flags, '-o', str(out / name), *map(str, files)], check=True)
    subprocess.run([os.sys.executable, str(source / 'mark-builtin.py'), str(out / 'd3d12.dll')], check=True)
    records = {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name, _, _ in jobs}
    (out / 'build-manifest.json').write_text(json.dumps({'artifacts': records, 'installation': 'not performed'}, indent=2) + '\n')
    print('Built:', out)
    print('Rebuilt DLL SHA-256:', records['d3d12.dll'])
    print('Rebuilt hashes may differ; the released binary remains unchanged.')


if __name__ == '__main__':
    main()
