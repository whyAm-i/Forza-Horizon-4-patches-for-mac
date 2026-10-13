import base64
import csv
import json
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import shader_lab as lab


class LabTests(unittest.TestCase):
    def test_catalog_latest_launch_and_all_stages(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            log = root / 'test.log'
            log.write_text('SHIM_BUILD old\nREGISTER_PSO[1] type=graphics VS=50/11111111\n'
                           'SHIM_BUILD binding-capture-v4\n'
                           'REGISTER_PSO[2] type=graphics VS=20/a99ec1d9 PS=50/22222222 GS=4/33333333 HS=4/44444444 DS=4/55555555\n'
                           'REGISTER_PSO[3] type=compute CS=50/66666666\n'
                           'REGISTER_PSO[4] type=graphics VS=20/a99ec1d9 PS=0/00000000\n')
            census = root / 'census_draws.txt'
            census.write_text('ord 1 | Draw | pso 2 | vs a99ec1d9 | ps 22222222\n# DISPATCH: pso 3 | cs 66666666\n')
            result = lab.inventory(root, root, log)
            self.assertEqual(result['pipeline_count'], 3)
            self.assertEqual(len(result['shaders']), 6)
            tree = next(e for e in result['shaders'] if e['stage'] == 'vs')
            self.assertEqual(tree['pipelines'], [2, 4])
            self.assertEqual(tree['label'], 'distant trees')
            self.assertEqual(tree['observed_calls'], 1)
            self.assertEqual(len(lab.select(result, 'all', True)), 3)

    def test_batching_covers_every_pipeline_no_truncation(self):
        item = {'stage': 'ps', 'hash': '12345678', 'pipelines': list(range(1, 302))}
        plans = lab.plans(item)
        self.assertEqual([len(p) for p in plans], [128, 128, 45])
        self.assertEqual([r for p in plans for r in p], ['pso ' + str(i) for i in range(1, 302)])
        self.assertEqual(lab.plans(dict(item, stage='vs')), [['vs 12345678']])

    def test_exception_restores_exact_original_and_rule_limits(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            path = root / 'skip.txt'
            original = b'# custom original\r\nvs aabbccdd\r\n'
            path.write_bytes(original)
            with self.assertRaises(KeyboardInterrupt):
                with lab.SkipSession(root, root / 'data') as s:
                    s.write(['vs %08x' % i for i in range(128)])
                    self.assertTrue(s.testing)
                    with self.assertRaises(ValueError):
                        s.write(['pso 1'] * 129)
                    with self.assertRaises(ValueError):
                        s.write(['ps abcd1234'])
                    s.baseline()
                    self.assertFalse(s.testing)
                    raise KeyboardInterrupt()
            self.assertEqual(path.read_bytes(), original)
            self.assertFalse((root / 'data/active-session.json').exists())

    def test_missing_original_and_hard_stop_recovery(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            session = lab.SkipSession(root, root / 'data')
            session.__enter__()
            session.write(['cs aabbccdd'])
            lab.restore(root / 'data')
            self.assertFalse((root / 'skip.txt').exists())

    def test_recovery_between_journal_and_file_write(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            path = root / 'skip.txt'
            path.write_bytes(b'# original\n')
            s = lab.SkipSession(root, root / 'data')
            s.__enter__()
            old = path.read_bytes()
            s.write(['vs aabbccdd'])
            # Emulate interruption after next journal update but before file replacement.
            path.write_bytes(old)
            lab.restore(root / 'data')
            self.assertEqual(path.read_bytes(), b'# original\n')

    def test_external_edits_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            path = root / 'skip.txt'
            path.write_bytes(b'# original\n')
            s = lab.SkipSession(root, root / 'data')
            s.__enter__()
            path.write_bytes(b'# another tool\n')
            with self.assertRaises(RuntimeError):
                s.write(['vs aabbccdd'])
            with self.assertRaises(RuntimeError):
                lab.restore(root / 'data')
            self.assertEqual(path.read_bytes(), b'# another tool\n')
            self.assertTrue(s.journal.exists())

    def test_baseline_does_not_record_as_a_test(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with lab.SkipSession(root, root / 'data') as s:
                item = {'stage': 'vs', 'hash': 'aabbccdd', 'id': 'vs:aabbccdd', 'label': '', 'pipelines': [1]}
                with patch('builtins.input', side_effect=['b', '', 't', '', 'leaves disappeared']):
                    result, details, done, total = lab.inspect(s, item)
                self.assertEqual((result, done, total), ('tested', 1, 1))
                self.assertIn('leaves disappeared', details[0])
                self.assertFalse(s.testing)

    def test_quit_and_partial_batch_notes(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with lab.SkipSession(root, root / 'data') as s:
                item = {'stage': 'ps', 'hash': 'aabbccdd', 'id': 'ps:aabbccdd', 'label': '', 'pipelines': list(range(200))}
                with patch('builtins.input', side_effect=['', 'sky disappeared', 'q']):
                    result, details, done, total = lab.inspect(s, item)
                self.assertEqual((result, done, total), ('q', 1, 2))
                self.assertEqual(details, ['batch 1: sky disappeared'])
                self.assertFalse(s.testing)

class ObservationTests(unittest.TestCase):
    def test_csv_shader_details_notes_and_snapshot(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d)
            item = {'id': 'ps:12345678', 'stage': 'ps', 'hash': '12345678', 'label': 'sky',
                    'pipelines': [17, 29], 'observed_calls': 3}
            catalog = {'shaders': [item], 'source_log': 'game-1.log', 'startup': 'launch 1'}
            note = 'sky, clouds "blue"\nsecond line'
            lab.record_observation(output, catalog, item, 'tested', [note], 1, 1, 'bisect')
            with (output / 'observations.csv').open(newline='') as f:
                row = next(csv.DictReader(f))
            self.assertEqual(row['shader_stage'], 'ps')
            self.assertEqual(row['shader_hash'], '12345678')
            self.assertEqual(row['pipeline_count'], '2')
            self.assertEqual(row['pipeline_ids'], '17 29')
            self.assertEqual(json.loads(row['skip_rule_batches']), [['pso 17', 'pso 29']])
            self.assertEqual(row['notes'], note)
            self.assertEqual(row['test_mode'], 'bisect')
            # Export after another launch must retain values used during the original test.
            item['pipelines'] = [999]
            catalog['source_log'] = 'game-2.log'
            notes = json.loads((output / 'observations.json').read_text())
            lab.save_notes(output, notes, catalog)
            with (output / 'observations.csv').open(newline='') as f:
                row = next(csv.DictReader(f))
            self.assertEqual(row['pipeline_ids'], '17 29')
            self.assertEqual(row['source_log'], 'game-1.log')

    def test_existing_notes_are_preserved_and_enriched(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d)
            item = {'id': 'vs:a99ec1d9', 'stage': 'vs', 'hash': 'a99ec1d9', 'label': 'distant trees',
                    'pipelines': [5], 'observed_calls': 10}
            notes = {item['id']: {'label': 'distant trees', 'result': 'tested', 'notes': 'my old note', 'time': 'then'}}
            lab.save_notes(output, notes, {'shaders': [item]})
            with (output / 'observations.csv').open(newline='') as f:
                row = next(csv.DictReader(f))
            self.assertEqual(row['shader_hash'], 'a99ec1d9')
            self.assertEqual(row['notes'], 'my old note')

class RecoveryTests(unittest.TestCase):
    def test_timer_restores_while_main_thread_waits(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            original = b'# exact original\r\n'
            (root / 'skip.txt').write_bytes(original)
            event = threading.Event()
            with lab.SkipSession(root, root / 'data', lease_seconds=0.02) as session:
                expire = session._expire
                def callback(generation):
                    expire(generation)
                    event.set()
                session._expire = callback
                session.write(['vs aabbccdd'])
                self.assertTrue(event.wait(1), 'Recovery must run independently of the input prompt.')
                self.assertTrue(session.expired)
                self.assertFalse(session.testing)
                self.assertNotIn(b'vs aabbccdd', (root / 'skip.txt').read_bytes())
            self.assertEqual((root / 'skip.txt').read_bytes(), original)

    def test_stale_timer_cannot_cancel_new_test(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with lab.SkipSession(root, root / 'data') as session:
                session.write(['vs aabbccdd'])
                old = session._generation
                session.write(['vs 11223344'])
                session._expire(old)
                self.assertTrue(session.testing)
                self.assertIn(b'vs 11223344', (root / 'skip.txt').read_bytes())
                session.baseline()
                session._expire(session._generation - 1)
                self.assertFalse(session.expired)

    def test_blank_view_is_inconclusive_not_a_pass(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with lab.SkipSession(root, root / 'data') as session:
                item = {'id': 'vs:11223344', 'stage': 'vs', 'hash': '11223344', 'label': '', 'pipelines': [1]}
                with patch('builtins.input', side_effect=['f']):
                    result, notes, done, total = lab.inspect(session, item)
                self.assertEqual((result, done, total), ('inconclusive', 0, 1))
                self.assertIn('blank/frozen', notes[0])
                self.assertFalse(session.testing)

    def test_fullscreen_and_unobserved_excluded_from_object_search(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            log = root / 'test.log'
            log.write_text('SHIM_BUILD new\nREGISTER_PSO[1] type=graphics VS=50/11111111\n'
                           'REGISTER_PSO[2] type=graphics VS=50/22222222\n'
                           'REGISTER_PSO[3] type=graphics VS=50/33333333\n')
            (root / 'census_draws.txt').write_text(
                'ord 1 | DrawIndexed | pso 1 | vs 11111111 | ps 00000000 | count 600 | inst 1\n'
                'ord 2 | DrawIndexed | pso 2 | vs 22222222 | ps 00000000 | count 60 | inst 1\n'
                'ord 3 | Draw | pso 2 | vs 22222222 | ps 00000000 | count 3 | inst 1\n')
            catalog = lab.inventory(root, root, log)
            self.assertEqual([e['hash'] for e in lab.object_candidates(catalog)], ['11111111'])

    def test_frozen_group_subdivides_without_eliminating_candidates(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            output = root / 'data'
            output.mkdir()
            shaders = [{'id': 'vs:%08x' % i, 'stage': 'vs', 'hash': '%08x' % i, 'label': '',
                        'pipelines': [i], 'object_candidate': True} for i in range(1, 11)]
            args = SimpleNamespace(stage='vs', shader=None, group_size=4, pulse_seconds=0)
            with patch('builtins.input', side_effect=['f', 'y', 'y', '', 'target removed']):
                lab.bisect(root, output, {'shaders': shaders}, args)
            rounds = json.loads((output / 'bisection-rounds.json').read_text())
            self.assertEqual([len(r['hashes']) for r in rounds], [4, 2, 1])
            self.assertEqual(rounds[1]['hashes'], rounds[0]['hashes'][:2])
            result = json.loads((output / 'last-bisection.json').read_text())
            self.assertEqual(result['candidate']['hash'], '00000001')
            with (output / 'bisection-rounds.csv').open(newline='') as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(rows[0]['notes'], 'Blank/frozen; inconclusive')
            self.assertEqual(len(rows), 3)

if __name__ == '__main__':
    unittest.main()
