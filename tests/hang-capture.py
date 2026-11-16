"""Collector contract only: no real commands, login service, GPU, or reports."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / 'tools/hang-capture.sh'
body = script.read_text().split("<<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]


class CaptureContract(unittest.TestCase):
    def invoke(self, repo, arguments, runner):
        with patch.object(sys, 'argv', ['-', str(repo)] + arguments), \
             patch('subprocess.run', side_effect=runner), \
             patch.object(Path, 'iterdir', return_value=iter(())), \
             contextlib.redirect_stdout(io.StringIO()):
            exec(compile(body, str(script), 'exec'), {'__name__': '__main__'})

    def test_dry_run_creates_nothing_and_runs_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            with self.assertRaises(SystemExit) as result:
                self.invoke(repo, ['--dry-run'], lambda *a, **k: self.fail('dry-run executed a command'))
            self.assertEqual(result.exception.code, 0)
            self.assertFalse((repo / 'local').exists())

    def test_numeric_pids_timeout_and_permission_failure(self):
        calls = []

        def runner(command, **kwargs):
            calls.append(command)
            self.assertNotIn('--display', command)
            self.assertNotIn('--probe', command)
            if command[0] == '/bin/ps':
                kwargs['stdout'].write(b'123 1 2.0 100 00:04 /X/WindowServer\n'
                                       b'456 1 0.5 200 00:02 /Applications/Windows App\n')
            if '/usr/bin/footprint' in command:
                self.assertIn(command[-1], ['123', '456'])
                if command[-1] == '123':
                    raise subprocess.TimeoutExpired(command, kwargs['timeout'])
                return subprocess.CompletedProcess(command, 77)
            if command[0] == '/usr/sbin/ioreg':
                raise PermissionError('fixture: registry unavailable')
            return subprocess.CompletedProcess(command, 0)

        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            self.invoke(repo, [], runner)
            out, = (repo / 'local').glob('hang-*')
            manifest = json.loads((out / 'manifest.json').read_text())
            results = {item['name']: item for item in manifest['commands']}
            self.assertEqual(results['footprint-123']['state'], 'timeout')
            self.assertEqual(results['footprint-456']['state'], 'unavailable')
            self.assertEqual(results['registry-root']['state'], 'unavailable')
            self.assertIn('finished_at', manifest)
            self.assertEqual(out.stat().st_mode & 0o777, 0o700)
            self.assertEqual((out / 'manifest.json').stat().st_mode & 0o777, 0o600)
            self.assertTrue(all(command[:3] != ['/usr/bin/sudo', '-n', 'systemsetup'] for command in calls))


if __name__ == '__main__':
    unittest.main()
