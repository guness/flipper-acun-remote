"""Exercise installer orchestration without network access or a USB device."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/install_acun_remote.py'
SPEC = importlib.util.spec_from_file_location('installer', SCRIPT)
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.app = self.root / 'flipper_apps/acun_remote'
        self.app.mkdir(parents=True)
        (self.app / 'application.fam').write_text('test manifest')
        self.calls = []
        self.fail_on = None
        self.source = self.root / '.install/roguemaster/RM-test'
        self.status = {'sdk': {'version': 'test-release'}}
        self.patch_root = patch.object(installer, 'ROOT', self.root)
        self.patch_root.start()
        self.addCleanup(self.patch_root.stop)
        self.patch_venv = patch.object(installer.venv.EnvBuilder, 'create')
        self.create = self.patch_venv.start()
        self.addCleanup(self.patch_venv.stop)
        self.patch_run = patch.object(installer.subprocess, 'run', side_effect=self.run_command)
        self.patch_run.start()
        self.addCleanup(self.patch_run.stop)

    def run_command(self, command, **kwargs):
        self.calls.append((command, kwargs))
        self.assertTrue(kwargs['check'])
        if command[0] == 'git':
            if command[1] == 'config':
                output = 'submodule.core.path lib/core\nsubmodule.proto.path assets/protobuf\nsubmodule.app.path applications/external/unused\n'
            elif command[1] == 'rev-parse':
                output = 'test-commit\n'
            else:
                output = ''
            return subprocess.CompletedProcess(command, 0, stdout=output)
        if command[0] == './fbt':
            arguments = command[1:]
            if arguments == self.fail_on:
                raise subprocess.CalledProcessError(1, command)
            if arguments == ['fap_acun_remote']:
                binary = self.source / 'build/f7-firmware-C/.extapps/acun_remote.fap'
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b'test FAP')
            return subprocess.CompletedProcess(command, 0)
        if command[2] == 'pip':
            return subprocess.CompletedProcess(command, 0)
        self.assertEqual(command[1:3], ['-m', 'ufbt'])
        self.assertEqual(kwargs['cwd'], self.app)
        arguments = command[3:]
        if arguments == self.fail_on:
            raise subprocess.CalledProcessError(1, command)
        if not arguments:
            (self.app / 'dist').mkdir(exist_ok=True)
            (self.app / 'dist/acun_remote.fap').write_bytes(b'test FAP')
        if arguments == ['launch']:
            self.assertTrue((self.app / 'dist/build_info.json').is_file())
        return subprocess.CompletedProcess(command, 0, stdout=json.dumps(self.status))

    def ufbt_commands(self):
        return [command[3:] for command, _ in self.calls if command[2] == 'ufbt']

    def test_official_build_only_records_metadata_without_upload(self):
        installer.main(['--firmware', 'official', '--build-only'])
        self.assertEqual(self.ufbt_commands(), [
            ['update', '--index-url', installer.INDEXES['official'], '--channel', 'release'],
            [], ['status', '--json'],
        ])
        metadata = json.loads((self.app / 'dist/build_info.json').read_text())
        self.assertEqual(metadata['sdk_status'], self.status)
        self.assertEqual(metadata['firmware'], 'official')
        self.assertEqual(metadata['fap_sha256'], hashlib.sha256(b'test FAP').hexdigest())
        self.assertEqual(metadata['fap_bytes'], 8)
        self.assertIs(metadata['hardware_tested'], False)

    def test_unleashed_installs_after_successful_build(self):
        installer.main(['--firmware', 'unleashed'])
        commands = self.ufbt_commands()
        self.assertIn(installer.INDEXES['unleashed'], commands[0])
        self.assertEqual(commands[1:], [[], ['status', '--json'], ['launch']])
        env = self.calls[-1][1]['env']
        self.assertEqual(env['UFBT_HOME'], str(self.root / '.install/unleashed'))
        self.assertEqual(env['FBT_TOOLCHAIN_PATH'], str(self.root / '.install'))

    def test_momentum_selects_its_own_sdk(self):
        installer.main(['--firmware', 'momentum', '--build-only'])
        self.assertIn(installer.INDEXES['momentum'], self.ufbt_commands()[0])
        self.assertNotIn(['launch'], self.ufbt_commands())

    def test_alternate_app_directory_receives_build_output(self):
        self.app = self.root / 'tagged-source'
        self.app.mkdir()
        (self.app / 'application.fam').write_text('tagged manifest')
        installer.main(['--firmware', 'official', '--app-dir', str(self.app), '--build-only'])
        self.assertTrue((self.app / 'dist/build_info.json').exists())

    def prepare_roguemaster(self):
        (self.source / '.git').mkdir(parents=True)
        symbols = self.source / 'targets/f7/api_symbols.csv'
        symbols.parent.mkdir(parents=True)
        symbols.write_text('Version,+,88.4,,\n')

    def test_roguemaster_source_build_records_revision_and_installs(self):
        self.prepare_roguemaster()
        installer.main(['--firmware', 'roguemaster', '--roguemaster-tag', 'RM-test'])
        metadata = json.loads((self.app / 'dist/build_info.json').read_text())
        self.assertEqual(metadata['firmware_tag'], 'RM-test')
        self.assertEqual(metadata['firmware_commit'], 'test-commit')
        self.assertIs(metadata['hardware_tested'], False)
        commands = [command for command, _ in self.calls]
        self.assertIn(['./fbt', 'fap_acun_remote'], commands)
        self.assertEqual(commands[-1], ['./fbt', 'launch', 'APPSRC=acun_remote'])
        self.assertFalse(any('applications/external/unused' in command for command in commands))
        self.create.assert_not_called()

    def test_roguemaster_latest_release_build_only_never_uploads(self):
        self.prepare_roguemaster()
        with patch.object(installer, 'urlopen', return_value=io.BytesIO(b'{"tag_name":"RM-test"}')):
            installer.main(['--firmware', 'roguemaster', '--build-only'])
        self.assertFalse(any('launch' in command for command, _ in self.calls))

    def test_roguemaster_failed_build_never_uploads_or_publishes_metadata(self):
        self.prepare_roguemaster()
        self.fail_on = ['fap_acun_remote']
        with self.assertRaises(subprocess.CalledProcessError):
            installer.main(['--firmware', 'roguemaster', '--roguemaster-tag', 'RM-test'])
        self.assertFalse((self.app / 'dist/build_info.json').exists())
        self.assertFalse(any('launch' in command for command, _ in self.calls))

    def test_invalid_roguemaster_tag_is_rejected_before_clone(self):
        with self.assertRaises(ValueError):
            installer.main(['--firmware', 'roguemaster', '--roguemaster-tag', '../outside'])
        self.assertFalse(self.calls)

    def test_local_sdk_path_with_spaces_is_passed_as_one_argument(self):
        sdk = self.root / 'custom SDK.zip'
        sdk.write_bytes(b'fixture')
        installer.main(['--firmware', 'unleashed', '--sdk', str(sdk), '--build-only'])
        self.assertEqual(self.ufbt_commands()[0], ['update', '--local', str(sdk.resolve())])

    def test_missing_sdk_fails_before_installing_dependencies(self):
        with self.assertRaises(SystemExit):
            installer.main(['--firmware', 'official', '--sdk', str(self.root / 'missing.zip')])
        self.assertFalse(self.calls)
        self.create.assert_not_called()

    def test_failed_sdk_update_never_builds_or_uploads(self):
        self.fail_on = ['update', '--index-url', installer.INDEXES['official'], '--channel', 'release']
        with self.assertRaises(subprocess.CalledProcessError):
            installer.main(['--firmware', 'official'])
        self.assertEqual(self.ufbt_commands(), [self.fail_on])

    def test_failed_build_never_uploads_or_writes_success_metadata(self):
        self.fail_on = []
        with self.assertRaises(subprocess.CalledProcessError):
            installer.main(['--firmware', 'official'])
        self.assertNotIn(['launch'], self.ufbt_commands())
        self.assertFalse((self.app / 'dist/build_info.json').exists())

    def test_failed_upload_is_reported(self):
        self.fail_on = ['launch']
        with self.assertRaises(subprocess.CalledProcessError):
            installer.main(['--firmware', 'official'])


if __name__ == '__main__':
    unittest.main()
