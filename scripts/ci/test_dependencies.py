#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline real-CMake discovery, provider isolation and cache regressions."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='AI dependencies with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source with spaces'
        shutil.copytree(ROOT, self.source, symlinks=True, ignore=shutil.ignore_patterns(
            '.git', 'build', '.cache', '__pycache__', 'compile_commands.json'))
        self.prefix = ROOT / 'build/deps/prefix'
        self.empty = self.root / 'empty internal prefix'
        self.empty.mkdir()
        self.env = dict(os.environ)
        for name in ('HOLONIGHT_EXTERNAL_PREFIX', 'HOLONIGHT_DEPENDENCY_PREFIX', 'PKG_CONFIG_PATH',
                     'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR', 'HOLONIGHT_BUILD_FEATURES'):
            self.env.pop(name, None)
        self.env['HOLONIGHT_DEPENDENCY_PREFIX'] = str(self.prefix)

    def command(self, args, env=None):
        return subprocess.run(args, cwd=self.source, env=env or self.env, text=True, capture_output=True)

    def configure(self, *options):
        return self.command(['cmake', '-S', '.', '-B', 'build/direct', '-G', 'Ninja',
                             f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.prefix}',
                             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', *options])

    def assert_pass(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_failure(self, result, message):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(message, result.stdout + result.stderr)

    def test_missing_qt_rejected(self):
        self.assert_failure(self.configure('-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE'), 'Qt6')

    def test_incompatible_qt_rejected(self):
        directory = self.root / 'old Qt'
        directory.mkdir()
        (directory / 'Qt6Config.cmake').write_text('')
        (directory / 'Qt6ConfigVersion.cmake').write_text(
            'set(PACKAGE_VERSION 6.10.0)\nset(PACKAGE_VERSION_COMPATIBLE FALSE)\n')
        hook = self.root / 'isolate Qt.cmake'
        hook.write_text('macro(find_package)\nif("${ARGV0}" STREQUAL "Qt6")\n'
                        f'set(Qt6_DIR "{directory}")\n_find_package(${{ARGV}} PATHS "{directory}" NO_DEFAULT_PATH)\n'
                        'else()\n_find_package(${ARGV})\nendif()\nendmacro()\n')
        self.assert_failure(self.configure(f'-DCMAKE_PROJECT_INCLUDE={hook}'), '6.11')


    def test_testing_off_without_qt_test(self):
        hook = self.root / 'reject Qt Test.cmake'
        hook.write_text('macro(find_package)\nif("${ARGV0}" STREQUAL "Qt6" AND "${ARGV}" MATCHES "(;|^)Test(;|$)")\n'
                        'message(FATAL_ERROR "Qt Test unavailable")\nendif()\n_find_package(${ARGV})\nendmacro()\n')
        self.assert_pass(self.configure('-DBUILD_TESTS=OFF', f'-DCMAKE_PROJECT_INCLUDE={hook}'))

    def test_testing_on_requires_qt_test(self):
        hook = self.root / 'reject Qt Test.cmake'
        hook.write_text('macro(find_package)\nif("${ARGV0}" STREQUAL "Qt6" AND "${ARGV}" MATCHES "(;|^)Test(;|$)")\n'
                        'message(FATAL_ERROR "Qt Test unavailable")\nendif()\n_find_package(${ARGV})\nendmacro()\n')
        self.assert_failure(self.configure('-DBUILD_TESTS=ON', f'-DCMAKE_PROJECT_INCLUDE={hook}'), 'Qt Test unavailable')

    def test_missing_gtest_rejected_without_download(self):
        self.assert_failure(self.configure('-DBUILD_TESTS=ON', '-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE'), 'GTest')
        self.assertFalse((self.source / 'build/direct/_deps').exists())

    def test_old_gtest_rejected(self):
        directory = self.root / 'old GTest'
        directory.mkdir()
        (directory / 'GTestConfig.cmake').write_text('')
        (directory / 'GTestConfigVersion.cmake').write_text(
            'set(PACKAGE_VERSION 1.13.0)\nset(PACKAGE_VERSION_COMPATIBLE FALSE)\n')
        hook = self.root / 'isolate GTest.cmake'
        hook.write_text('macro(find_package)\nif("${ARGV0}" STREQUAL "GTest")\n'
                        f'set(GTest_DIR "{directory}")\n_find_package(${{ARGV}} PATHS "{directory}" NO_DEFAULT_PATH)\n'
                        'else()\n_find_package(${ARGV})\nendif()\nendmacro()\n')
        self.assert_failure(self.configure('-DBUILD_TESTS=ON', f'-DCMAKE_PROJECT_INCLUDE={hook}'), '1.14')

    def test_missing_gmock_rejected(self):
        directory = self.root / 'GTest without GMock'
        directory.mkdir()
        (directory / 'GTestConfig.cmake').write_text('add_library(GTest::gtest INTERFACE IMPORTED)\n')
        (directory / 'GTestConfigVersion.cmake').write_text(
            'set(PACKAGE_VERSION 1.14.0)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
        self.assert_failure(self.configure('-DBUILD_TESTS=ON', f'-DGTest_DIR={directory}'), 'GTest::gmock')


    def test_internal_providers_cannot_leak(self):
        for location in ('external', 'system', 'cached'):
            with self.subTest(location=location):
                options = [f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.empty}']
                if location == 'external':
                    options += [f'-DHOLONIGHT_EXTERNAL_PREFIX={self.prefix}', f'-DCMAKE_PREFIX_PATH={self.prefix}']
                elif location == 'system':
                    options += [f'-DCMAKE_SYSTEM_PREFIX_PATH={self.prefix}']
                else:
                    options += [f'-DHoloNightConfig_DIR={self.prefix}/lib/cmake/HoloNightConfig']
                self.assert_failure(self.configure(*options), 'Missing HoloNightConfig in approved internal')

    def test_provider_prefix_spaces(self):
        internal = self.root / 'installed providers with spaces'
        shutil.copytree(self.prefix, internal)
        self.assert_pass(self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={internal}',
                                        f'-DQML_IMPORT_PATH={internal}/lib/qt6/qml'))


    def test_required_external_libraries(self):
        for package in ('libsecret-1', 'md4c'):
            with self.subTest(package=package):
                wrapper = self.root / 'pkg-config filter'
                wrapper.write_text('#!/bin/sh\ncase "$*" in\n'
                                   f'  *{package}*) echo "Missing {package}" >&2; exit 1 ;;\n'
                                   'esac\nexec /usr/bin/pkg-config "$@"\n')
                wrapper.chmod(0o755)
                # A separate build avoids cached pkg-config results from the previous package.
                self.assert_failure(self.configure(f'-B{self.source / "build" / package}',
                                                    f'-DPKG_CONFIG_EXECUTABLE={wrapper}'), package)
        self.assert_failure(self.configure('-DCMAKE_DISABLE_FIND_PACKAGE_KF6SyntaxHighlighting=TRUE'),
                            'KF6SyntaxHighlighting')

    def test_controls_and_wayland_required(self):
        for component in ('Controls', 'Wayland'):
            with self.subTest(component=component):
                internal = self.root / component
                shutil.copytree(self.prefix, internal)
                exports = internal / 'lib/cmake/HolonightQt'
                if component == 'Controls':
                    targets = exports / 'HolonightQtTargets.cmake'
                    text = targets.read_text()
                    start = text.index('# Create imported target HolonightQt::Controls')
                    end = text.index('# Load information', start)
                    text = (text[:start] + text[end:]).replace(' HolonightQt::Controls)', ')')
                    targets.write_text(text)
                else:
                    for path in exports.glob('*Wayland*'):
                        path.unlink()
                self.assert_failure(self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={internal}'), 'HolonightQt')

    def test_tests_off_without_gtest(self):
        self.assert_pass(self.configure('-DBUILD_TESTS=OFF', '-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE'))

    def test_coverage_configuration(self):
        self.assert_pass(self.configure('-DBUILD_TESTS=ON', '-DENABLE_COVERAGE=ON'))
        database = json.loads((self.source / 'build/direct/compile_commands.json').read_text())
        production = [entry for entry in database if '/src/' in entry['file']]
        self.assertTrue(production)
        self.assertTrue(all('--coverage' in entry['command'] for entry in production))
        self.assert_pass(self.command(['cmake', '--build', 'build/direct', '--target', 'holonight_domain']))

    def test_ordinary_commands_validate_without_preparation(self):
        env = dict(self.env, HOLONIGHT_DEPENDENCY_PREFIX=str(self.empty))
        for command in ('configure', 'build', 'test'):
            self.assert_failure(self.command(['python3', 'tooling/workflow.py', command, '--preset', 'test'], env), 'HoloNightConfig')
            self.assertFalse((self.source / 'build/deps').exists())
            self.assertFalse((self.source / 'build/external').exists())
            self.assertFalse((self.source / 'build/ci-prepared').exists())

    def test_external_prefix_spaces_compatible_reuse_and_changed_cache_archival(self):
        directory = self.root / 'external prefix with spaces' / 'lib/cmake/Qt6'
        directory.mkdir(parents=True)
        qt = subprocess.check_output(['pkg-config', '--variable=libdir', 'Qt6Core'], text=True).strip()
        installed = Path(qt) / 'cmake/Qt6'
        (directory / 'Qt6Config.cmake').write_text(f'include("{installed}/Qt6Config.cmake")\n')
        shutil.copyfile(installed / 'Qt6ConfigVersion.cmake', directory / 'Qt6ConfigVersion.cmake')
        for extra in installed.glob('*VersionImpl.cmake'):
            shutil.copyfile(extra, directory / extra.name)
        env = dict(self.env, HOLONIGHT_EXTERNAL_PREFIX=str(directory.parents[2]))
        args = ['python3', 'tooling/workflow.py', 'configure', '--preset', 'test']
        self.assert_pass(self.command(args, env))
        marker = self.source / 'build/test/marker'
        marker.write_text('preserved')
        self.assert_pass(self.command(args, env))
        self.assertTrue(marker.exists())
        self.assertIn(str(directory), (self.source / 'build/test/CMakeCache.txt').read_text())
        second = self.root / 'second external prefix'
        shutil.copytree(directory.parents[2], second)
        env['HOLONIGHT_EXTERNAL_PREFIX'] = str(second)
        self.assert_pass(self.command(args, env))
        self.assertEqual((self.source / 'build/test.previous-1/marker').read_text(), 'preserved')
        self.assertFalse(marker.exists())
        self.assertFalse((self.source / 'build/deps').exists())
        marker.write_text('feature change')
        env['HOLONIGHT_BUILD_FEATURES'] = 'ENABLE_COVERAGE=ON'
        self.assert_pass(self.command(args, env))
        self.assertEqual((self.source / 'build/test.previous-2/marker').read_text(), 'feature change')
        marker.write_text('toolchain change')
        env['CXX'] = shutil.which('c++')
        self.assert_pass(self.command(args, env))
        self.assertEqual((self.source / 'build/test.previous-3/marker').read_text(), 'toolchain change')


if __name__ == '__main__':
    unittest.main()
