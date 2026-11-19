#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Standalone regressions for the independently usable quality helper."""
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from threading import Barrier
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('quality', Path(__file__).with_name('quality.py'))
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


class QualityTests(unittest.TestCase):
    def test_translation_and_identity(self):
        with tempfile.TemporaryDirectory(prefix='quality spaces ') as tmp:
            root = Path(tmp)
            for field in ('command', 'arguments'):
                database = root / field / 'compile_commands.json'
                database.parent.mkdir()
                tokens = ['g++', '-Irelative include', '-DNAME=a b', '-mno-direct-extern-access',
                          '-DKEEP=-mno-direct-extern-access', '-mno-direct-extern-access-suffix', '@relative.rsp', '-o', 'relative.o', 'a file.cpp']
                entry = {'directory': '../work dir', 'file': 'a file.cpp', 'output': 'relative.o',
                         field: tokens if field == 'arguments' else shlex.join(tokens)}
                unchanged = {'directory': '../other dir', 'file': 'other.cpp',
                             'arguments': ['g++', '-Irelative', 'other.cpp']}
                database.write_text(json.dumps([entry, unchanged]))
                original = database.read_bytes()
                with ExitStack() as stack:
                    translated = quality.analysis_database(database, stack)
                    self.assertNotEqual(translated, database)
                    results = json.loads(translated.read_text())
                    result = results[0]
                    self.assertEqual(results[1]['arguments'], unchanged['arguments'])
                    self.assertEqual(Path(results[1]['directory']).resolve(), (root / 'other dir').resolve())
                    actual = result.get('arguments') or shlex.split(result['command'])
                    expected = tokens.copy()
                    expected[3] = '-fno-direct-access-external-data'
                    self.assertEqual(actual, expected)
                    self.assertEqual(Path(result['directory']).resolve(), (root / 'work dir').resolve())
                    self.assertEqual(Path(result['file']), (root / 'work dir/a file.cpp').resolve())
                    self.assertEqual(result['output'], entry['output'])
                self.assertFalse(translated.exists())
                self.assertEqual(database.read_bytes(), original)
                entry[field] = ['g++', '-unrelated-invalid'] if field == 'arguments' else 'g++ -unrelated-invalid'
                database.write_text(json.dumps([entry]))
                with ExitStack() as stack:
                    self.assertEqual(quality.analysis_database(database, stack), database)

    def test_nested_workers_and_failure_cleanup(self):
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source = root / 'source.cpp'
                source.touch()
                build = root / 'build'
                databases = [build / 'compile_commands.json', build / 'consumer/compile_commands.json']
                originals = []
                for db in databases:
                    db.parent.mkdir(parents=True, exist_ok=True)
                    db.write_text(json.dumps([{'directory': str(root), 'file': source.name,
                                               'arguments': ['g++', '-mno-direct-extern-access', source.name]}]))
                    originals.append(db.read_bytes())
                seen = []
                barrier = Barrier(2)

                def worker(*args):
                    db = Path(args[args.index('-p') + 1]) / 'compile_commands.json'
                    self.assertTrue(db.exists())
                    seen.append(db)
                    barrier.wait(timeout=10)
                    if fail:
                        raise subprocess.CalledProcessError(7, args)

                with patch.object(quality, 'ROOT', root), patch.object(quality, 'owned', return_value=[source]), \
                     patch.object(quality, 'run', side_effect=worker), patch.dict(os.environ, JOBS='2'), \
                     patch.object(sys, 'argv', ['quality.py', 'tidy', '--build', str(build)]):
                    if fail:
                        with self.assertRaises(subprocess.CalledProcessError):
                            quality.main()
                    else:
                        quality.main()
                self.assertEqual(len(set(seen)), 2)
                self.assertTrue(all(not db.exists() for db in seen))
                self.assertEqual([db.read_bytes() for db in databases], originals)


if __name__ == '__main__':
    unittest.main()
