#!/usr/bin/env python3
"""Inventory and visually test FH4's loaded shaders using the existing shim."""
import argparse
import base64
import csv
from datetime import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import signal
import sys
import tempfile
import threading

HERE = Path(__file__).resolve().parent
DEFAULT_HB = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/highball'
STAGES = ('vs', 'ps', 'gs', 'hs', 'ds', 'cs')
KNOWN = {'a99ec1d9': 'distant trees', 'c8ca6104': 'leaves', 'c58458c3': 'stones',
         'daebf740': 'grass', 'dd05ba81': 'stone walls', '46c59935': 'water'}
REGISTER = re.compile(r'REGISTER_PSO\[(\d+)\].*?type=(\S+)')
HASHES = re.compile(r'\b(VS|PS|GS|HS|DS|CS)=(\d+)/([0-9a-fA-F]{8})')
DRAW = re.compile(r'^ord\s+\d+.*?\| pso\s+(\d+) \| vs ([0-9a-f]{8}) \| ps ([0-9a-f]{8})')
COMPUTE = re.compile(r'^# DISPATCH: pso (\d+) \| cs ([0-9a-f]{8})')
MAX_RULES = 128


def atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.shader-lab-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
        if path.exists():
            os.chmod(name, path.stat().st_mode & 0o777)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def bounded_lines(path, limit=None):
    """Read a fixed snapshot with bounded line memory; never follow a growing file."""
    with Path(path).open('rb') as f:
        remaining = Path(path).stat().st_size if limit is None else limit
        while remaining > 0:
            data = f.readline(min(16384, remaining))
            if not data:
                break
            remaining -= len(data)
            yield data.decode('utf-8', errors='replace')


def scan_log(path):
    pipelines, startup = {}, ''
    for line in bounded_lines(path):
        if 'SHIM_BUILD ' in line:
            # Wine reuses PIDs and appends to existing files across launches.
            pipelines, startup = {}, line.strip()
        match = REGISTER.search(line)
        if match:
            shaders = {stage.lower(): value.lower() for stage, length, value in HASHES.findall(line)
                       if int(length) and value != '00000000'}
            pipelines[int(match[1])] = {'type': match[2], 'shaders': shaders}
    return pipelines, startup


def choose_log(hb, explicit=None):
    if explicit:
        return Path(explicit).resolve()
    # Game pipelines vastly outnumber utility-test pipelines. Only inspect a bounded prefix
    # and suffix to find candidate logs, then fully stream one selected game log.
    for path in sorted((hb / 'logs').glob('tsshim-*.log'), key=lambda p: p.stat().st_mtime, reverse=True):
        with path.open('rb') as f:
            start = f.read(128 * 1024)
            f.seek(max(0, path.stat().st_size - 512 * 1024))
            tail = f.read(512 * 1024)
        if b'WORLD_LOAD_DETECTED' in start + tail or b'HB_TSSHIM_SKIP SUMMARY' in tail or b'ExecuteIndirect sig=' in tail:
            return path.resolve()
    raise RuntimeError('No game log found. Enter the outdoor scene, then run inventory again; or supply --log.')


def inventory(hb, output, log=None, census=None):
    source = choose_log(hb, log)
    pipelines, startup = scan_log(source)
    if not pipelines:
        raise RuntimeError('Selected log has no registered pipelines.')
    entries = {}
    for pso, pipeline in pipelines.items():
        for stage, value in pipeline['shaders'].items():
            key = stage + ':' + value
            item = entries.setdefault(key, {'id': key, 'stage': stage, 'hash': value,
                                           'label': KNOWN.get(value, '') if stage == 'vs' else '',
                                           'pipelines': [], 'observed_calls': 0,
                                           'indexed_calls': 0, 'screen_triangle_calls': 0})
            item['pipelines'].append(pso)
    census_path = Path(census) if census else hb / 'census_draws.txt'
    # Counts refer only to the selected recording interval. It may predate the latest launch.
    # Direct hashes are stable, so avoid using stale PSO indices to derive stage counts.
    if census_path.exists():
        for line in bounded_lines(census_path):
            m = DRAW.search(line)
            pairs = [('vs', m[2]), ('ps', m[3])] if m else []
            if m and 'vs:' + m[2] in entries:
                entry = entries['vs:' + m[2]]
                if '| DrawIndexed' in line:
                    entry['indexed_calls'] += 1
                else:
                    count = re.search(r'\| count\s+(\d+)', line)
                    instances = re.search(r'\| inst\s+(\d+)', line)
                    if '| Draw ' in line and count and instances and int(count[1]) <= 6 and int(instances[1]) == 1:
                        entry['screen_triangle_calls'] += 1
            m = COMPUTE.search(line)
            if m:
                pairs.append(('cs', m[2]))
            for stage, value in pairs:
                if stage + ':' + value in entries:
                    entries[stage + ':' + value]['observed_calls'] += 1
    for item in entries.values():
        item['object_candidate'] = item['stage'] == 'vs' and item['indexed_calls'] > 0 and item['screen_triangle_calls'] == 0
    ordered = sorted(entries.values(), key=lambda e: (STAGES.index(e['stage']), -e['observed_calls'], e['hash']))
    for item in ordered:
        item['pipelines'].sort()
    result = {'created': datetime.now().astimezone().isoformat(), 'source_log': str(source),
              'source_log_bytes': source.stat().st_size, 'startup': startup,
              'census': str(census_path) if census_path.exists() else None,
              'coverage': 'Shaders registered in this game process; unloaded game variants are not included.',
              'pipeline_count': len(pipelines), 'shaders': ordered}
    atomic(output / 'catalog.json', (json.dumps(result, indent=2) + '\n').encode())
    with (output / 'inventory.csv').open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'stage', 'hash', 'known_label', 'pipeline_count', 'observed_calls_in_selected_census', 'pipeline_ids', 'indexed_calls', 'screen_triangle_calls', 'object_bisection_candidate'])
        for item in ordered:
            writer.writerow([item['id'], item['stage'], item['hash'], item['label'], len(item['pipelines']), item['observed_calls'], ' '.join(map(str, item['pipelines'])), item['indexed_calls'], item['screen_triangle_calls'], item['object_candidate']])
    for stage in ('vs', 'cs'):
        atomic(output / ('all-' + stage + '.txt'), ('# All loaded ' + stage.upper() + ' hashes; use shader_lab for lists over 128 entries.\n' +
               ''.join(stage + ' ' + item['hash'] + '\n' for item in ordered if item['stage'] == stage)).encode())
    counts = {stage: sum(item['stage'] == stage for item in ordered) for stage in STAGES}
    print('Inventory: ' + ', '.join(stage.upper() + '=' + str(n) for stage, n in counts.items()))
    print(str(len(pipelines)) + ' registered pipelines; ' + str(len(ordered)) + ' unique stage/hash pairs.')
    print('Saved ' + str(output / 'inventory.csv'))
    return result


def plans(item):
    if item['stage'] == 'vs':
        return [['vs ' + item['hash']]]
    # Suppress the matching whole pipelines. All their shader stages stop together.
    rules = ['pso ' + str(pso) for pso in item['pipelines']]
    return [rules[i:i+MAX_RULES] for i in range(0, len(rules), MAX_RULES)]


class SkipSession:
    def __init__(self, hb, output, lease_seconds=0):
        self.path = hb / 'skip.txt'
        self.output = output
        self.journal = output / 'active-session.json'
        self.last = None
        self.original = None
        self.testing = False
        self.expired = False
        self.lease_seconds = lease_seconds
        self._lock = threading.RLock()
        self._timer = None
        self._generation = 0

    def __enter__(self):
        if self.journal.exists():
            raise RuntimeError('An interrupted session exists. Run restore before starting another test.')
        self.original = self.path.read_bytes() if self.path.exists() else None
        journal = {'path': str(self.path), 'original': None if self.original is None else base64.b64encode(self.original).decode(),
                   'last_sha256': None, 'started': datetime.now().astimezone().isoformat(), 'owner_pid': os.getpid()}
        # Exclusive create prevents two tools from owning the same restoration journal.
        self.output.mkdir(parents=True, exist_ok=True)
        with self.journal.open('x') as f:
            json.dump(journal, f, indent=2)
        self.write([])
        return self

    def write(self, rules):
        with self._lock:
            self._write_locked(rules)

    def _write_locked(self, rules):
        if len(rules) > MAX_RULES or any(not re.fullmatch(r'(?:vs|cs) [0-9a-f]{8}|pso \d+', rule) for rule in rules):
            raise ValueError('Invalid skip rules or more than 128 rules.')
        if self.last is not None and (not self.path.exists() or self.path.read_bytes() != self.last):
            raise RuntimeError('Another tool changed skip.txt. Stop that tool before continuing.')
        data = ('# Shader Lab temporary test. Quit or run restore to return to the original.\n' + '\n'.join(rules) + '\n').encode()
        journal = json.loads(self.journal.read_text())
        journal['previous_sha256'] = journal.get('last_sha256')
        journal['last_sha256'] = hashlib.sha256(data).hexdigest()
        atomic(self.journal, json.dumps(journal, indent=2).encode())
        atomic(self.path, data)
        self.last = data
        self.testing = bool(rules)
        self.expired = False
        self._generation += 1
        if self._timer:
            self._timer.cancel()
            self._timer = None
        if rules and self.lease_seconds:
            self._timer = threading.Timer(self.lease_seconds, self._expire, args=(self._generation,))
            self._timer.daemon = True
            self._timer.start()

    def _expire(self, generation):
        with self._lock:
            if generation != self._generation or not self.testing:
                return
            try:
                self._write_locked([])
                self.expired = True
                print('\nAuto-restored full scene. Record what you saw, or use t to repeat the pulse.', flush=True)
            except (OSError, ValueError, RuntimeError) as error:
                print('\nAutomatic restoration failed: ' + str(error) + '. Quit and use restore.', flush=True)

    def baseline(self):
        self.write([])

    def __exit__(self, *exc):
        with self._lock:
            self._generation += 1
            if self._timer:
                self._timer.cancel()
                self._timer = None
            try:
                restore(self.output, quiet=True)
            finally:
                print('Original skip.txt restored.' if not self.journal.exists() else 'Restoration pending; run restore.')


def restore(output, quiet=False):
    journal_path = output / 'active-session.json'
    if not journal_path.exists():
        if not quiet:
            print('No interrupted session; skip.txt left unchanged.')
        return
    journal = json.loads(journal_path.read_text())
    owner = journal.get('owner_pid')
    if owner and owner != os.getpid():
        try:
            os.kill(owner, 0)
        except ProcessLookupError:
            pass
        except PermissionError:
            raise RuntimeError('The session owner may still be running. Quit its testing menu first.')
        else:
            raise RuntimeError('Shader Lab is still running. Quit its testing menu before restoring.')
    path = Path(journal['path'])
    original = None if journal['original'] is None else base64.b64decode(journal['original'])
    current = path.read_bytes() if path.exists() else None
    acceptable = {journal.get('last_sha256'), journal.get('previous_sha256')}
    if current != original and (current is None or hashlib.sha256(current).hexdigest() not in acceptable):
        raise RuntimeError('skip.txt was changed outside Shader Lab; refusing to overwrite that change. Journal preserved.')
    if original is None:
        path.unlink(missing_ok=True)
    else:
        atomic(path, original)
    journal_path.unlink()
    if not quiet:
        print('Original skip.txt restored from interrupted session.')


def select(catalog, stage, observed=False, wanted=None):
    items = [item for item in catalog['shaders'] if (stage == 'all' or item['stage'] == stage) and
             (not observed or item['observed_calls'])]
    if wanted:
        items = [item for item in items if item['id'] == wanted or item['hash'] == wanted]
        if not items:
            raise RuntimeError('Shader not found in the selected stage: ' + wanted)
    return items


def shader_details(catalog, item):
    return {'stage': item['stage'], 'hash': item['hash'], 'label': item.get('label', ''),
            'pipelines': list(item['pipelines']), 'skip_rule_batches': plans(item),
            'observed_calls': item.get('observed_calls', 0),
            'source_log': catalog.get('source_log', ''), 'source_startup': catalog.get('startup', '')}


def save_notes(output, notes, catalog=None):
    # Preserve the pipeline IDs from the test's game launch, even after inventory refreshes.
    indexed = {item['id']: item for item in catalog['shaders']} if catalog else {}
    for key, record in notes.items():
        if 'stage' not in record:
            if key in indexed:
                record.update(shader_details(catalog, indexed[key]))
            else:
                stage, separator, value = key.partition(':')
                record['stage'], record['hash'] = stage, value if separator else ''
    atomic(output / 'observations.json', (json.dumps(notes, indent=2) + '\n').encode())
    text = io.StringIO(newline='')
    writer = csv.writer(text)
    writer.writerow(['id', 'shader_stage', 'shader_hash', 'known_label', 'pipeline_count',
                     'pipeline_ids', 'skip_rule_batches', 'observed_calls_in_selected_census',
                     'test_mode', 'result', 'notes', 'tested_batches', 'total_batches', 'time',
                     'source_log', 'source_startup'])
    for key, record in sorted(notes.items()):
        pipelines = record.get('pipelines')
        writer.writerow([key, record.get('stage', ''), record.get('hash', ''), record.get('label', ''),
                         len(pipelines) if pipelines is not None else '',
                         ' '.join(map(str, pipelines)) if pipelines is not None else '',
                         json.dumps(record.get('skip_rule_batches', [])), record.get('observed_calls', ''),
                         record.get('test_mode', 'review'), record['result'], record['notes'],
                         record.get('tested_batches', 1), record.get('total_batches', 1), record['time'],
                         record.get('source_log', ''), record.get('source_startup', '')])
    atomic(output / 'observations.csv', text.getvalue().encode('utf-8'))


def record_observation(output, catalog, item, result, details, done, total, mode):
    path = output / 'observations.json'
    notes = json.loads(path.read_text()) if path.exists() else {}
    record = shader_details(catalog, item)
    record.update({'result': result, 'notes': '\n'.join(details), 'tested_batches': done,
                   'total_batches': total, 'test_mode': mode,
                   'time': datetime.now().astimezone().isoformat()})
    notes[item['id']] = record
    save_notes(output, notes, catalog)


def prompt(message):
    try:
        return input(message).strip()
    except EOFError:
        return 'q'


def inspect(session, item):
    batches = plans(item)
    details = []
    completed = 0
    for number, rules in enumerate(batches, 1):
        session.write(rules)
        print('\nSkipping ' + item['id'] + (' (' + item['label'] + ')' if item['label'] else '') +
              '; batch ' + str(number) + '/' + str(len(batches)) + '; ' + str(len(rules)) + ' rule(s).')
        if item['stage'] != 'vs':
            print('This hides matching whole pipelines, including their other stages.')
        print('Return to Forza and wait about 2 seconds with the game unpaused.')
        if session.lease_seconds:
            print('Full scene returns automatically after ' + str(session.lease_seconds) + ' seconds; t reapplies the test.')
        else:
            print('Test stays active until your command. b restores the scene; t reapplies the test.')
        while True:
            answer = prompt('[Enter] record result; [b] baseline; [t] test again; [f] frozen/blank; [s] skip; [q] quit: ').lower()
            if answer == 'b':
                session.baseline()
                print('Full scene restored. Use t to reapply this shader test.')
            elif answer == 't':
                session.write(rules)
                print('Test reapplied; wait for the live reload in Forza.')
            elif answer == 'f':
                session.baseline()
                details.append('batch ' + str(number) + ': blank/frozen view; inconclusive')
                return 'inconclusive', details, completed, len(batches)
            elif answer in ('s', 'q'):
                session.baseline()
                return answer, details, completed, len(batches)
            elif answer == '':
                if not session.testing and not session.expired:
                    print('Baseline is active. Use t before recording a test result.')
                    continue
                note = prompt('Describe what changed (objects/effects, or "no visible change"); q to quit: ')
                if note == 'q':
                    session.baseline()
                    return 'q', details, completed, len(batches)
                details.append('batch ' + str(number) + ': ' + (note or 'No observation recorded'))
                completed += 1
                session.baseline()
                break
            else:
                print('Use Enter, b, t, f, s, or q.')
    return 'tested', details, completed, len(batches)


def review(hb, output, catalog, args):
    items = select(catalog, args.stage, args.observed, args.shader)
    notes = json.loads((output / 'observations.json').read_text()) if (output / 'observations.json').exists() else {}
    if not args.retest:
        items = [item for item in items if notes.get(item['id'], {}).get('result') != 'tested']
    print(str(len(items)) + ' shaders pending; previous completed observations are preserved.')
    if not items:
        return
    with SkipSession(hb, output, lease_seconds=args.pulse_seconds) as session:
        for position, item in enumerate(items, 1):
            print('\nTest ' + str(position) + '/' + str(len(items)))
            result, details, done, total = inspect(session, item)
            if details or result != 'q':
                record_observation(output, catalog, item, result, details, done, total, 'review')
            if result == 'q':
                return


def object_candidates(catalog):
    # Census-based heuristic: indexed geometry, excluding hashes also observed drawing
    # single-instance triangles/quads commonly used by fullscreen or UI passes.
    return [item for item in catalog['shaders'] if item.get('object_candidate')]


def bisect(hb, output, catalog, args):
    if args.stage != 'vs':
        raise RuntimeError('Object bisection supports --stage vs. Use individual shader reviews for other stages.')
    items = object_candidates(catalog)
    if not items:
        raise RuntimeError('No indexed-geometry candidates in this census. Capture the outdoor scene, then refresh inventory.')
    print(str(len(items)) + ' observed indexed-geometry candidates; fullscreen/unobserved hashes excluded.')
    if args.shader:
        items = [item for item in items if item['hash'] == args.shader or item['id'] == args.shader]
        if not items:
            raise RuntimeError('Selected shader is not an object-bisection candidate; use an individual review.')
    limit = args.group_size
    blocked = []
    rounds = []
    def save_round(chosen, answer):
        rounds.append({'hashes': [item['hash'] for item in chosen], 'answer': answer,
                       'time': datetime.now().astimezone().isoformat()})
        atomic(output / 'bisection-rounds.json', (json.dumps(rounds, indent=2) + '\n').encode())
        text = io.StringIO(newline='')
        writer = csv.writer(text)
        writer.writerow(['round', 'shader_stage', 'shader_hashes', 'skip_rules', 'answer', 'notes', 'time', 'source_log'])
        descriptions = {'y': 'Target changed', 'n': 'Target unchanged', 'f': 'Blank/frozen; inconclusive'}
        for index, entry in enumerate(rounds, 1):
            writer.writerow([index, 'vs', ' '.join(entry['hashes']),
                             json.dumps(['vs ' + value for value in entry['hashes']]), entry['answer'],
                             descriptions[entry['answer']], entry['time'], catalog.get('source_log', '')])
        atomic(output / 'bisection-rounds.csv', text.getvalue().encode())
    with SkipSession(hb, output, lease_seconds=args.pulse_seconds) as session:
        print('Keep ONE target object and camera position fixed. Blank/frozen is inconclusive, not yes/no.')
        while len(items) > 1:
            n = min(limit, max(1, len(items) // 2))
            chosen = items[:n]
            rules = ['vs ' + item['hash'] for item in chosen]
            session.write(rules)
            print('\nSkipping ' + str(n) + ' of ' + str(len(items)) + ' remaining object shaders.')
            if args.pulse_seconds:
                print('Observe in Forza now; full scene returns automatically after ' + str(args.pulse_seconds) + ' seconds.')
            else:
                print('Test stays active until your answer or command. b restores the scene; t reapplies the test.')
            while True:
                answer = prompt('Target changed? [y] yes / [n] no / [f] frozen/blank / [t] repeat / [b] baseline / [q] quit: ').lower()
                if answer == 'q':
                    return
                if answer == 'b':
                    session.baseline()
                    print('Full scene restored; t reapplies the test.')
                    continue
                if answer == 't':
                    session.write(rules)
                    print('Test reapplied. Return to Forza now.')
                    continue
                if answer not in ('y', 'n', 'f'):
                    print('Use y, n, f, t, b or q.')
                    continue
                if not session.testing and not session.expired:
                    print('Baseline is active; use t before judging the test.')
                    continue
                break
            session.baseline()
            save_round(chosen, answer)
            if answer == 'y':
                items = chosen
            elif answer == 'n':
                items = items[n:]
            elif n > 1:
                limit = max(1, n // 2)
                print('Inconclusive group; retrying smaller subsets without eliminating candidates.')
            else:
                blocked.append(chosen[0]['id'])
                items = items[1:]
                print('Individual test inconclusive; retained in the blocked list, not ruled out.')
        print('\nCandidate: ' + items[0]['id'] + '. Confirm with an individual test:')
        if blocked:
            print('Additional inconclusive candidates: ' + ', '.join(blocked))
        result, details, done, total = inspect(session, items[0])
        if details or result != 'q':
            record_observation(output, catalog, items[0], result, details, done, total, 'bisect')
        atomic(output / 'last-bisection.json', (json.dumps({'candidate': items[0], 'result': result, 'notes': details,
                                                        'inconclusive_candidates': blocked, 'rounds': rounds,
                                                        'tested_batches': done, 'total_batches': total}, indent=2) + '\n').encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hb-dir', type=Path, default=DEFAULT_HB)
    parser.add_argument('--output', type=Path, default=HERE / 'data')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('inventory', help='Stream the game log into a complete loaded-shader inventory.')
    p.add_argument('--log', type=Path)
    p.add_argument('--census', type=Path)
    for command in ('review', 'bisect'):
        p = sub.add_parser(command)
        p.add_argument('--stage', choices=STAGES + ('all',), default='vs')
        p.add_argument('--observed', action='store_true', help='Limit to hashes seen in the selected census (VS/PS/CS).')
        p.add_argument('--refresh', action='store_true', help='Rebuild the inventory before testing.')
        p.add_argument('--shader', help='Review one hash or stage:hash.')
        p.add_argument('--retest', action='store_true', help='Include shaders already marked tested.')
        p.add_argument('--pulse-seconds', type=float, default=0, help='Optional automatic restore (1-60 seconds); default 0 keeps the test active until your command.')
        p.add_argument('--group-size', type=int, default=4, help='Maximum object-bisection group size (1-8).')
    sub.add_parser('restore', help='Recover skip.txt after a hard interruption or reboot.')
    args = parser.parse_args()
    hb, output = args.hb_dir.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.command == 'restore':
        restore(output)
        return
    if args.command == 'inventory':
        inventory(hb, output, args.log, args.census)
        return
    if not 0 <= args.pulse_seconds <= 60 or not 1 <= args.group_size <= 8:
        raise RuntimeError('Use pulse-seconds 0-60 (0 means manual restoration) and group-size 1-8.')
    if not sys.stdin.isatty():
        raise RuntimeError('Run interactive testing in Terminal; inventory needs no terminal.')
    path = output / 'catalog.json'
    if args.refresh or not path.exists():
        catalog = inventory(hb, output)
    else:
        catalog = json.loads(path.read_text())
    if args.command == 'review' and args.stage != 'vs':
        current = choose_log(hb)
        if str(current) != catalog['source_log'] or current.stat().st_size < catalog['source_log_bytes']:
            raise RuntimeError('Pipeline IDs belong to a previous game launch. Run inventory or use --refresh.')
        # PIDs can be reused. A bounded streaming pass verifies the latest startup marker.
        _, startup = scan_log(current)
        if startup != catalog['startup']:
            raise RuntimeError('Game restarted since inventory. Refresh before testing pipeline IDs.')
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    (review if args.command == 'review' else bisect)(hb, output, catalog, args)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped.')
    except (RuntimeError, ValueError, OSError) as error:
        print('Error: ' + str(error), file=sys.stderr)
        sys.exit(1)
