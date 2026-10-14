#!/usr/bin/env python3
"""Regression checks for source isolation and required lane failures."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('launcher', Path(__file__).with_name('run.py'))
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'repo'
        self.root.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        for name in ('edited', 'deleted', 'space name', 'executable'):
            (self.root / name).write_text('original')
        (self.root / 'executable').chmod(0o755)
        (self.root / 'link').symlink_to('edited')
        (self.root / '.gitignore').write_text('ignored\nbuild/\n')
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test', '-c', 'user.email=test@example.org',
                        'commit', '-qm', 'fixture'], check=True)
        self.patcher = patch.object(launcher, 'ROOT', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def prepared_fixture(self):
        scripts = self.root / 'scripts/ci'
        scripts.mkdir(parents=True, exist_ok=True)
        images = {'build': 'build@sha256:fixture', 'licensing': 'reuse@sha256:fixture'}
        (scripts / 'images.json').write_text(json.dumps(images))
        for name in launcher.PREPARATION_INPUTS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture')
        prepared = self.root / 'build/ci-prepared'
        prepared.mkdir(parents=True)
        (prepared / 'evidence.txt').write_text('accepted')
        (prepared / 'manifest.json').write_text(json.dumps(
            {'identity': launcher.preparation_identity(images), 'hashes': launcher.prepared_hashes(prepared)}))
        return prepared, launcher.preparation_identity(images)


    def test_snapshot_preserves_current_inputs_and_omits_deleted_and_ignored(self):
        (self.root / 'edited').write_text('edited content')
        (self.root / 'deleted').unlink()
        (self.root / 'new space name').write_text('new content')
        (self.root / 'ignored').write_text('ignored content')
        target = Path(self.temporary.name) / 'snapshot'
        target.mkdir()
        untracked = launcher.snapshot(target)
        self.assertEqual(untracked, ['new space name'])
        self.assertEqual((target / 'edited').read_text(), 'edited content')
        self.assertEqual((target / 'new space name').read_text(), 'new content')
        self.assertEqual((target / 'space name').read_text(), 'original')
        self.assertFalse((target / 'deleted').exists())
        self.assertFalse((target / 'ignored').exists())
        self.assertTrue((target / 'executable').stat().st_mode & 0o111)
        self.assertTrue((target / 'link').is_symlink())
        self.assertEqual(os.readlink(target / 'link'), 'edited')
        head = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'])
        self.assertEqual(head, launcher.git('rev-parse', 'HEAD'))
        inventory = subprocess.check_output(['git', '-C', str(target), 'ls-files', '-z']).split(b'\0')
        self.assertIn(b'new space name', inventory)
        self.assertNotIn(b'deleted', inventory)
        self.assertTrue(subprocess.check_output(['git', '-C', str(target), 'diff', '--cached', 'HEAD']))

    def test_rootless_podman_preserves_user_mapping_and_read_only_input(self):
        self.prepared_fixture()
        runtime = Path(self.temporary.name) / 'podman'
        arguments = Path(self.temporary.name) / 'podman-arguments.json'
        runtime.write_text('#!/usr/bin/env python3\nimport json, sys\nfrom pathlib import Path\n'
                           'if sys.argv[1] == "--version": print("fake-podman")\n'
                           'else:\n'
                           ' accounts = {}\n'
                           ' for argument in sys.argv[1:]:\n'
                           '  if "dst=/etc/passwd," in argument or "dst=/etc/group," in argument:\n'
                           '   fields = dict(part.split("=", 1) for part in argument.split(",") if "=" in part)\n'
                           '   assert argument.endswith(",readonly")\n'
                           '   accounts[fields["dst"]] = Path(fields["src"]).read_text()\n'
                           '  if "dst=/output" in argument:\n'
                           '   assert not argument.endswith(",readonly")\n'
                           '   fields = dict(part.split("=", 1) for part in argument.split(",") if "=" in part)\n'
                           '   Path(fields["src"], "fixture-artifact").write_text("generated")\n'
                           f' Path({str(arguments)!r}).write_text(json.dumps([sys.argv[1:], accounts]))\n')
        runtime.chmod(0o755)
        with patch.object(launcher.shutil, 'which', side_effect=lambda name: str(runtime) if name == 'podman' else None), \
             patch.object(launcher.os, 'getuid', return_value=1000), \
             patch.object(launcher.os, 'getgid', return_value=1000), \
             patch('sys.argv', ['ci', '--lane', launcher.LANES[0]]):
            self.assertEqual(launcher.main(), 0)
        command, accounts = json.loads(arguments.read_text())
        self.assertIn('--userns=keep-id', command)
        self.assertEqual(command[command.index('--user') + 1], '1000:1000')
        self.assertNotIn('--privileged', command)
        self.assertIn('--pull=never', command)
        self.assertIn('--network=none', command)
        self.assertTrue(any(a.endswith('dst=/prepared/deps/prefix,readonly') for a in command))
        self.assertFalse(any('dst=/prepared,' in a or 'dst=/prepared/sources' in a for a in command))
        self.assertIn('/input/scripts/ci/lane.sh', command)
        self.assertTrue(command[command.index('--mount') + 1].endswith(',dst=/input,readonly'))
        self.assertIn('ai-test:x:1000:1000:CI:/work/build/home:/bin/sh\n', accounts['/etc/passwd'])
        self.assertIn('ai-test:x:1000:\n', accounts['/etc/group'])
        artifact = next((self.root / 'build/ci').glob('*/artifacts/*/fixture-artifact'))
        self.assertEqual(artifact.read_text(), 'generated')
        self.assertEqual(artifact.stat().st_uid, os.getuid())

    def test_compatible_preparation_remains_valid(self):
        prepared, identity = self.prepared_fixture()
        before = (prepared / 'evidence.txt').read_bytes()
        launcher.validate_prepared(prepared, identity)
        launcher.validate_prepared(prepared, identity)
        self.assertEqual((prepared / 'evidence.txt').read_bytes(), before)
        self.assertFalse(prepared.with_name('ci-prepared.previous-1').exists())

    def test_missing_preparation_rejected(self):
        prepared, identity = self.prepared_fixture()
        (prepared / 'manifest.json').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing prepared CI'):
            launcher.validate_prepared(prepared, identity)

    def test_stale_preparation_rejected_before_running_acceptance(self):
        prepared, identity = self.prepared_fixture()
        identity['platform'] = 'different'
        with self.assertRaisesRegex(RuntimeError, 'Stale'):
            launcher.validate_prepared(prepared, identity)

    def test_changed_installed_payload_and_symlink_rejected(self):
        prepared, identity = self.prepared_fixture()
        launcher.validate_prepared(prepared, identity)
        (prepared / 'evidence.txt').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'Tampered'):
            launcher.validate_prepared(prepared, identity)
        (prepared / 'evidence.txt').write_text('accepted')
        (prepared / 'new-link').symlink_to('evidence.txt')
        with self.assertRaisesRegex(RuntimeError, 'Tampered'):
            launcher.validate_prepared(prepared, identity)

    def test_changed_installed_permissions_rejected(self):
        prepared, identity = self.prepared_fixture()
        (prepared / 'evidence.txt').chmod(0o700)
        with self.assertRaisesRegex(RuntimeError, 'Tampered'):
            launcher.validate_prepared(prepared, identity)

    def test_incompatible_preparation_archived_without_removing_evidence(self):
        prepared, identity = self.prepared_fixture()
        launcher.archive_preparation(prepared)
        self.assertFalse(prepared.exists())
        self.assertEqual((prepared.with_name('ci-prepared.previous-1') / 'evidence.txt').read_text(), 'accepted')

    def test_exact_provider_revision_is_cloned_even_from_dirty_local_checkout(self):
        checkout = self.root / 'provider'
        subprocess.run(['git', 'clone', '-q', str(self.root), str(checkout)], check=True)
        revision = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        (checkout / 'edited').write_text('dirty source')
        prepared = self.root / 'build/prepared'
        prepared.mkdir(parents=True)
        with patch.object(launcher, 'PROVIDERS', {'provider': revision}):
            launcher.prepare_sources(prepared, self.root)
        self.assertEqual((prepared / 'sources/provider/edited').read_text(), 'original')
        with patch.object(launcher, 'PROVIDERS', {'missing': '0' * 40}):
            with self.assertRaises(subprocess.CalledProcessError):
                launcher.prepare_sources(prepared, self.root)

    def test_licensing_does_not_require_provider_preparation(self):
        prepared, _ = self.prepared_fixture()
        (prepared / 'manifest.json').unlink()
        runtime = Path(self.temporary.name) / 'docker'
        runtime.write_text('#!/bin/sh\nif [ "$1" = --version ]; then echo fake-runtime; fi\nexit 0\n')
        runtime.chmod(0o755)
        with patch.object(launcher.shutil, 'which', return_value=str(runtime)), \
             patch.object(launcher, 'validate_prepared', side_effect=AssertionError('unneeded preparation')), \
             patch('sys.argv', ['ci', '--lane', 'licensing']):
            self.assertEqual(launcher.main(), 0)

    def test_missing_runtime_fails_and_records_reason(self):
        with patch.object(launcher.shutil, 'which', return_value=None), patch('sys.argv', ['ci']):
            self.assertEqual(launcher.main(), 1)
        result = json.loads(next((self.root / 'build/ci').glob('*/results.json')).read_text())
        self.assertIn('Neither Docker nor Podman', result['error'])

    def test_every_failed_lane_fails_task_and_remaining_lanes_run(self):
        self.prepared_fixture()
        runtime = Path(self.temporary.name) / 'docker'
        runtime.write_text('#!/bin/sh\nif [ "$1" = --version ]; then echo fake-runtime; exit 0; fi\nexit 17\n')
        runtime.chmod(0o755)
        with patch.object(launcher.shutil, 'which', return_value=str(runtime)), patch('sys.argv', ['ci']):
            self.assertEqual(launcher.main(), 1)
        result = json.loads(next((self.root / 'build/ci').glob('*/results.json')).read_text())
        self.assertEqual(set(result['lanes']), set(launcher.LANES))
        self.assertTrue(all(lane['exit_code'] == 17 for lane in result['lanes'].values()))
        self.assertEqual((self.root / 'edited').read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
