#!/usr/bin/env python3
"""Dry-run installation or rollback of the released FH4 aligned-heap shim.

Only --apply --game-closed permits writes to the selected installation.
No processes are started or stopped; no registry/settings/provider are changed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
PATCH = 'fh4-aligned-heap-v1.1'
RELEASE_SHA = 'f3038fdc146ff740fb2070d39b16945623fae3958aab963f995ae13f79d25b51'
GAME_SHA = 'b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe'
PROVIDER_SHA = '32c942ed368a45e58d35f4da3de3776d1f74e785731de9cf6a5e705a2925488a'
PRIOR_SHAS = {
    '833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66',  # V3
    'bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44',  # road/speed
}
CX_BUILD = '26.3.0.39832'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def regular(path):
    require(path.is_file() and not path.is_symlink(), f'Expected a regular, non-symlink file: {path}')


def targets(cx, bottle):
    return [cx / 'lib64/apple_gptk/wine/x86_64-windows/d3d12.dll',
            bottle / 'drive_c/windows/system32/d3d12.dll']


def check_closed():
    result = subprocess.run(['ps', '-axo', 'pid=,comm='], text=True, capture_output=True, check=True)
    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        name = parts[1].replace('\\', '/')
        require(name.rsplit('/', 1)[-1].lower() != 'forzahorizon4.exe' and
                '/ForzaHorizon4.app/Contents/MacOS/Menu Helper' not in name,
                'FH4 is running. Close it before applying or rolling back.')


def atomic_copy(source, target):
    regular(target)
    before = digest(target)
    fd, temporary = tempfile.mkstemp(prefix='.fh4-memory-', dir=target.parent)
    temporary = Path(temporary)
    try:
        with os.fdopen(fd, 'wb') as output, source.open('rb') as stream:
            shutil.copyfileobj(stream, output)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, target.stat().st_mode & 0o777)
        require(digest(temporary) == digest(source), 'Staged copy checksum mismatch')
        regular(target)
        require(digest(target) == before, f'Target changed during copy: {target}')
        os.replace(temporary, target)
        require(digest(target) == digest(source), f'Installed checksum mismatch: {target}')
    finally:
        if temporary.exists():
            temporary.unlink()


def write_record(path, record):
    fd, temporary = tempfile.mkstemp(prefix='.manifest-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as output:
            json.dump(record, output, indent=2)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def game_file(value):
    value = value.expanduser().resolve()
    return value / 'ForzaHorizon4.exe' if value.is_dir() else value


def release():
    source = ROOT / '02-rendering/bottle/d3d12.dll'
    regular(source)
    require(digest(source) == RELEASE_SHA, 'Released shim checksum mismatch; verify the archive')
    duplicate = ROOT / '02-rendering/source/d3d12.dll'
    regular(duplicate)
    require(digest(duplicate) == RELEASE_SHA, 'The two archived shim copies differ')
    require(source.read_bytes()[0x40:0x51] == b'Wine builtin DLL\0', 'Missing Wine builtin marker')
    return source


def environment(cx, bottle, game):
    regular(game)
    require(digest(game) == GAME_SHA, 'Unsupported FH4 executable; this patch is build-specific')
    require((bottle / 'cxbottle.conf').is_file(), 'Select an existing CrossOver bottle directory')
    info = cx.parents[1] / 'Info.plist'
    regular(info)
    require(plistlib.loads(info.read_bytes()).get('CFBundleVersion') == CX_BUILD,
            f'This package was tested with CrossOver {CX_BUILD}; other builds are refused')
    providers = {}
    for target in targets(cx, bottle):
        regular(target)
        provider = target.with_name('apd12.dll')
        regular(provider)
        require(digest(provider) == PROVIDER_SHA,
                f'Provider differs from the tested GPTK 4.0 beta 3 setup: {provider}; do not overwrite it with the older archived provider')
        providers[str(provider)] = PROVIDER_SHA
    return providers


def unchanged(record):
    regular(Path(record['game']))
    require(digest(Path(record['game'])) == record['game_sha256'], 'Game EXE changed')
    for name, expected in record['providers'].items():
        regular(Path(name))
        require(digest(Path(name)) == expected, f'Provider changed: {name}')


def transact(record, manifest, sources, expected, undo, final_status):
    changed = []
    wanted = ([record['installed_sha256']] * 2 if final_status == 'installed' else
              [e['original_sha256'] for e in record['files']])
    try:
        for index, entry in enumerate(record['files']):
            check_closed()
            target = Path(entry['target'])
            regular(target)
            require(digest(target) == expected[index], f'Target changed during preparation: {target}')
            require(digest(sources[index]) == wanted[index], 'Source changed during preparation')
            changed.append(index)  # Includes failures occurring after replacement.
            atomic_copy(sources[index], target)
            require(digest(target) == wanted[index], 'Target does not match the pinned source checksum')
        for index, entry in enumerate(record['files']):
            require(digest(Path(entry['target'])) == wanted[index], 'Installed target changed before final verification')
        unchanged(record)
        record['status'] = final_status
        record['updated_at'] = datetime.now(timezone.utc).isoformat()
        write_record(manifest, record)
    except Exception as error:
        failures = []
        for index in reversed(changed):
            try:
                target = Path(record['files'][index]['target'])
                regular(target)
                require(digest(target) in {expected[index], wanted[index]},
                        f'Target changed externally; refusing to overwrite it during recovery: {target}')
                require(digest(undo[index]) == expected[index], 'Recovery backup checksum mismatch')
                atomic_copy(undo[index], target)
            except Exception as restore_error:
                failures.append(str(restore_error))
        record['status'] = 'recovery_required' if failures else 'transaction_undone'
        record['error'] = str(error)
        record['recovery_errors'] = failures
        write_record(manifest, record)
        raise RuntimeError(f'{error}; transaction recovery: {record["status"]}; retain {manifest}') from error


def install(args):
    require(args.bottle is not None and args.game is not None, '--bottle and --game are required')
    cx = args.cx.expanduser().resolve()
    bottle = args.bottle.expanduser().resolve()
    game = game_file(args.game)
    source = release()
    providers = environment(cx, bottle, game)
    destinations = targets(cx, bottle)
    hashes = [digest(p) for p in destinations]
    for path, checksum in zip(destinations, hashes):
        require(checksum in PRIOR_SHAS | {RELEASE_SHA}, f'Unknown installed shim: {path} ({checksum})')
        print(f'{path}\n  current: {checksum}\n  release: {RELEASE_SHA}')
    if all(checksum == RELEASE_SHA for checksum in hashes):
        print('Already installed at both locations. No files changed.')
        return
    if not args.apply:
        print('DRY RUN: eligible. Close FH4/bottle apps, then add --apply --game-closed.')
        return
    require(args.game_closed, '--apply requires --game-closed after closing FH4 and bottle applications')
    check_closed()
    parent = (args.backup_dir or bottle / 'fh4-patch-backups').expanduser().resolve()
    backup = parent / ('memory-v1.1-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    backup.mkdir(parents=True)
    entries = []
    for index, (target, checksum) in enumerate(zip(destinations, hashes)):
        regular(target)
        require(digest(target) == checksum, f'Target changed before backup: {target}')
        name = 'engine-d3d12.dll' if index == 0 else 'bottle-d3d12.dll'
        shutil.copy2(target, backup / name)
        require(digest(backup / name) == checksum, 'Backup checksum mismatch; targets unchanged')
        entries.append({'target': str(target), 'backup': name, 'original_sha256': checksum})
    record = {'schema': 1, 'patch': PATCH, 'status': 'prepared', 'installed_sha256': RELEASE_SHA,
              'cx': str(cx), 'bottle': str(bottle), 'game': str(game), 'game_sha256': GAME_SHA,
              'providers': providers, 'files': entries}
    manifest = backup / 'manifest.json'
    write_record(manifest, record)
    transact(record, manifest, [source] * 2, hashes,
             [backup / e['backup'] for e in entries], 'installed')
    print('INSTALLED:', RELEASE_SHA)
    print('Rollback manifest:', manifest)


def rollback(args):
    manifest = args.rollback.expanduser().resolve()
    regular(manifest)
    record = json.loads(manifest.read_text())
    require(record.get('schema') == 1 and record.get('patch') == PATCH and record.get('status') == 'installed',
            'Expected an installed manifest produced by this installer')
    require(record.get('installed_sha256') == RELEASE_SHA and record.get('game_sha256') == GAME_SHA,
            'Manifest does not describe this release/build')
    destinations = targets(Path(record['cx']), Path(record['bottle']))
    require([str(p) for p in destinations] == [e['target'] for e in record['files']], 'Unexpected rollback targets')
    require([e['backup'] for e in record['files']] == ['engine-d3d12.dll', 'bottle-d3d12.dll'], 'Unexpected backup names')
    sources = []
    for target, entry in zip(destinations, record['files']):
        regular(target)
        source = manifest.parent / entry['backup']
        regular(source)
        require(digest(target) == RELEASE_SHA, f'Target changed since installation: {target}')
        require(entry['original_sha256'] in PRIOR_SHAS | {RELEASE_SHA} and digest(source) == entry['original_sha256'],
                f'Backup checksum mismatch: {source}')
        sources.append(source)
    unchanged(record)
    if not args.apply:
        print('DRY RUN: rollback eligible. Add --apply --game-closed to restore both exact backups.')
        return
    require(args.game_closed, 'Rollback requires --apply --game-closed')
    check_closed()
    # Retain the currently installed files so failed rollback is itself undone.
    safety = manifest.parent / ('rollback-safety-' + uuid.uuid4().hex[:8])
    safety.mkdir()
    undo = []
    for index, target in enumerate(destinations):
        copy = safety / ('engine.dll' if index == 0 else 'bottle.dll')
        shutil.copy2(target, copy)
        require(digest(copy) == RELEASE_SHA, 'Rollback safety-copy mismatch')
        undo.append(copy)
    transact(record, manifest, sources, [RELEASE_SHA] * 2, undo, 'restored')
    print('RESTORED both pre-install files. Manifest:', manifest)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cx', type=Path, default=Path('/Applications/CrossOver.app/Contents/SharedSupport/CrossOver'))
    parser.add_argument('--bottle', type=Path, help='Existing bottle DIRECTORY, not just its name')
    parser.add_argument('--game', type=Path, help='Game directory or ForzaHorizon4.exe')
    parser.add_argument('--backup-dir', type=Path, help='Parent for unique backup folders (default: bottle/fh4-patch-backups)')
    parser.add_argument('--rollback', type=Path, metavar='MANIFEST', help='Restore exact backups from an installation manifest')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--game-closed', action='store_true', help='Assert FH4 and bottle applications are closed; never stops them')
    args = parser.parse_args(argv)
    if args.rollback:
        rollback(args)
    else:
        install(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print('REFUSED:', error, file=sys.stderr)
        sys.exit(1)
