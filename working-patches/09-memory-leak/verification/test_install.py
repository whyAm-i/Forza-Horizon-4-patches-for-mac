"""Installer safety/transaction tests on temporary files; never a real bottle."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'install.py'
spec = importlib.util.spec_from_file_location('memory_installer', SCRIPT)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fh4-package-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.cx = self.root / 'CrossOver.app/Contents/SharedSupport/CrossOver'
        self.bottle = self.root / 'Bottle with spaces'
        self.game = self.root / 'Game with spaces/ForzaHorizon4.exe'
        self.game.parent.mkdir()
        self.game.write_bytes(b'fake supported game')
        self.cx.mkdir(parents=True)
        self.info = self.cx.parents[1] / 'Info.plist'
        self.info.write_bytes(plistlib.dumps({'CFBundleVersion': installer.CX_BUILD}))
        self.bottle.mkdir()
        (self.bottle / 'cxbottle.conf').write_text('unchanged configuration')
        self.old = b'fake previously reviewed V3 shim'
        self.provider = b'fake matched provider'
        self.targets = installer.targets(self.cx, self.bottle)
        for target in self.targets:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(self.old)
            target.with_name('apd12.dll').write_bytes(self.provider)
        self.args = ['--cx', str(self.cx), '--bottle', str(self.bottle), '--game', str(self.game.parent)]
        self.addCleanup(patch.stopall)
        patch.object(installer, 'GAME_SHA', installer.digest(self.game)).start()
        patch.object(installer, 'PROVIDER_SHA', hashlib.sha256(self.provider).hexdigest()).start()
        patch.object(installer, 'PRIOR_SHAS', {hashlib.sha256(self.old).hexdigest()}).start()
        patch.object(installer, 'check_closed', lambda: None).start()
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def call(self, *extra):
        installer.main(self.args + list(extra))

    def manifests(self):
        return list((self.bottle / 'fh4-patch-backups').glob('*/manifest.json'))

    def assert_old(self):
        for target in self.targets:
            self.assertEqual(target.read_bytes(), self.old)

    def test_dry_run_has_no_writes(self):
        self.call()
        self.assert_old()
        self.assertFalse((self.bottle / 'fh4-patch-backups').exists())

    def test_explicit_closed_assertion_required(self):
        with self.assertRaisesRegex(ValueError, 'game-closed'):
            self.call('--apply')
        self.assert_old()

    def test_unknown_target_refused(self):
        self.targets[1].write_bytes(b'unknown shim')
        with self.assertRaisesRegex(ValueError, 'Unknown installed shim'):
            self.call('--apply', '--game-closed')
        self.assertFalse(self.manifests())

    def test_wrong_game_and_provider_refused(self):
        self.game.write_bytes(b'different game build')
        with self.assertRaisesRegex(ValueError, 'Unsupported FH4'):
            self.call()
        self.game.write_bytes(b'fake supported game')
        self.targets[0].with_name('apd12.dll').write_bytes(b'wrong provider')
        with self.assertRaisesRegex(ValueError, 'Provider differs'):
            self.call()
        self.assert_old()

    def test_wrong_crossover_refused(self):
        self.info.write_bytes(plistlib.dumps({'CFBundleVersion': 'different'}))
        with self.assertRaisesRegex(ValueError, 'other builds are refused'):
            self.call()

    def test_symlink_target_refused(self):
        self.targets[0].unlink()
        real = self.root / 'real.dll'
        real.write_bytes(self.old)
        self.targets[0].symlink_to(real)
        with self.assertRaisesRegex(ValueError, 'non-symlink'):
            self.call()
        self.assertEqual(real.read_bytes(), self.old)

    def test_install_idempotence_and_rollback(self):
        self.call('--apply', '--game-closed')
        manifest = self.manifests()[0]
        record = json.loads(manifest.read_text())
        self.assertEqual(record['status'], 'installed')
        for entry in record['files']:
            self.assertEqual(installer.digest(Path(entry['target'])), installer.RELEASE_SHA)
            self.assertEqual((manifest.parent / entry['backup']).read_bytes(), self.old)
        self.call('--apply', '--game-closed')
        self.assertEqual(len(self.manifests()), 1)
        installer.main(['--rollback', str(manifest)])  # read-only
        self.assertEqual(json.loads(manifest.read_text())['status'], 'installed')
        installer.main(['--rollback', str(manifest), '--apply', '--game-closed'])
        self.assert_old()
        self.assertEqual(json.loads(manifest.read_text())['status'], 'restored')
        self.assertEqual((self.bottle / 'cxbottle.conf').read_text(), 'unchanged configuration')
        for target in self.targets:
            self.assertEqual(target.with_name('apd12.dll').read_bytes(), self.provider)

    def test_second_target_failure_undoes_both(self):
        real_copy = installer.atomic_copy
        failed = [False]

        def fail_after_replace(source, target):
            real_copy(source, target)
            if target == self.targets[1] and not failed[0]:
                failed[0] = True
                raise OSError('injected post-replacement failure')

        with patch.object(installer, 'atomic_copy', fail_after_replace):
            with self.assertRaisesRegex(RuntimeError, 'transaction_undone'):
                self.call('--apply', '--game-closed')
        self.assert_old()
        self.assertEqual(json.loads(self.manifests()[0].read_text())['status'], 'transaction_undone')

    def test_changed_backup_and_destination_refuse_rollback(self):
        self.call('--apply', '--game-closed')
        manifest = self.manifests()[0]
        backup = manifest.parent / 'engine-d3d12.dll'
        backup.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'Backup checksum'):
            installer.main(['--rollback', str(manifest)])
        backup.write_bytes(self.old)
        self.targets[1].write_bytes(b'later desired version')
        with self.assertRaisesRegex(ValueError, 'changed since installation'):
            installer.main(['--rollback', str(manifest), '--apply', '--game-closed'])
        self.assertEqual(self.targets[1].read_bytes(), b'later desired version')

    def test_source_changed_after_validation_is_not_installed(self):
        staged = self.root / 'staged.dll'
        shutil.copy2(installer.release(), staged)
        calls = [0]

        def checkpoint():
            calls[0] += 1
            if calls[0] == 2:
                staged.write_bytes(b'changed source')

        with patch.object(installer, 'release', lambda: staged), patch.object(installer, 'check_closed', checkpoint):
            with self.assertRaisesRegex(RuntimeError, 'Source changed'):
                self.call('--apply', '--game-closed')
        self.assert_old()

    def test_external_target_change_is_not_overwritten_by_recovery(self):
        real_copy = installer.atomic_copy
        failed = [False]

        def external_change(source, target):
            if target == self.targets[1] and not failed[0]:
                failed[0] = True
                target.write_bytes(b'external edit')
                raise OSError('injected external edit')
            real_copy(source, target)

        with patch.object(installer, 'atomic_copy', external_change):
            with self.assertRaisesRegex(RuntimeError, 'recovery_required'):
                self.call('--apply', '--game-closed')
        self.assertEqual(self.targets[0].read_bytes(), self.old)
        self.assertEqual(self.targets[1].read_bytes(), b'external edit')


class ProcessGuardTests(unittest.TestCase):
    def test_known_game_process_names(self):
        for name in [r'Y:\Games\ForzaHorizon4.exe', '/tmp/ForzaHorizon4.app/Contents/MacOS/Menu Helper']:
            result = type('Result', (), {'stdout': '123 ' + name + '\n'})()
            with patch.object(installer.subprocess, 'run', return_value=result):
                with self.assertRaisesRegex(ValueError, 'FH4 is running'):
                    installer.check_closed()


if __name__ == '__main__':
    unittest.main(verbosity=2)
