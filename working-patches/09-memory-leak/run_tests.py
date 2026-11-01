#!/usr/bin/env python3
"""Run isolated fixture EXEs; does not launch/open/patch the game or install DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def windows_path(path, bottle):
    choices = []
    for drive in (bottle / 'dosdevices').iterdir():
        if not re.fullmatch('[a-zA-Z]:', drive.name):
            continue
        try:
            mapped = drive.resolve(strict=True)
            relative = path.resolve().relative_to(mapped)
        except (OSError, ValueError):
            continue
        choices.append((len(str(mapped)), drive.name.upper() + '\\' + str(relative).replace('/', '\\')))
    if not choices:
        raise ValueError(f'No existing bottle drive maps {path}; no mappings were changed')
    return max(choices)[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cx', type=Path, default=Path('/Applications/CrossOver.app/Contents/SharedSupport/CrossOver'))
    parser.add_argument('--bottle', type=Path, required=True, help='Existing bottle directory')
    parser.add_argument('--build-dir', type=Path, default=ROOT / '09-memory-leak/build')
    parser.add_argument('--provider', help='Optional Windows path to the existing apd12.dll')
    args = parser.parse_args()
    bottle = args.bottle.expanduser().resolve()
    out = args.build_dir.expanduser().resolve()
    wine = args.cx.expanduser().resolve() / 'bin/wine'
    manifest = json.loads((out / 'build-manifest.json').read_text())
    for name, checksum in manifest['artifacts'].items():
        if sha(out / name) != checksum:
            raise ValueError(f'Build artifact changed: {name}')
    win = lambda p: windows_path(p, bottle)
    released = ROOT / '02-rendering/bottle/d3d12.dll'
    released_sha = sha(released)
    provider = [args.provider] if args.provider else []
    cases = [
        ('allocator-readonly', 'test_aligned_heap.exe', [], 0),
        ('allocator-cow-image', 'test_aligned_heap.exe', ['--cow-image', win(out / 'cow_fixture.dll')], 0),
        ('process-name-guard', 'test_aligned_heap.exe', ['--start-guard'], 0),
        ('invalid-original', 'test_aligned_heap.exe', ['--invalid-original'], 77),
        ('invalid-patched', 'test_aligned_heap.exe', ['--invalid-patched'], 77),
        ('released-provider-exports', 'smoke.exe', [win(released), *provider], 0),
        ('rebuilt-provider-exports', 'smoke.exe', [win(out / 'd3d12.dll'), *provider], 0),
        ('render-input-regression', 'test_input_offsets.exe', [], 0),
        ('drift-abi-regression', 'test_event_gate.exe', [], 0),
    ]
    results = []
    for name, program, arguments, expected in cases:
        result = subprocess.run([str(wine), '--bottle', str(bottle), '--no-update', '--cx-app',
                                 win(out / program), *arguments], text=True, capture_output=True, timeout=90)
        entry = {'name': name, 'returncode': result.returncode, 'expected': expected,
                 'stdout': result.stdout, 'stderr': result.stderr}
        results.append(entry)
        print(json.dumps(entry), flush=True)
        (out / 'test-results.json').write_text(json.dumps(results, indent=2) + '\n')
        if result.returncode != expected:
            raise RuntimeError(f'{name} failed; inspect {out / "test-results.json"}')
    for name, checksum in manifest['artifacts'].items():
        if sha(out / name) != checksum:
            raise RuntimeError('An artifact changed while tests ran')
    if sha(released) != released_sha:
        raise RuntimeError('Released DLL changed while tests ran')
    print('PASS all isolated checks. No installation performed.')


if __name__ == '__main__':
    main()
