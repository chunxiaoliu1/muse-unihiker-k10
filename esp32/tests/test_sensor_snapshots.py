import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SensorSnapshots(unittest.TestCase):
    def test_generic_lookup_expiry_boundary_and_invalid_state(self):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(compiler[0]):
            self.skipTest('Host C compiler unavailable; same test can run on K10')
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            source = work / 'snapshots.c'
            source.write_text('#include "sensor_selftest.h"\n'
                              'int main(void) { return sensor_snapshot_selftest() ? 0 : 1; }\n', encoding='utf-8')
            binary = work / ('snapshots.exe' if os.name == 'nt' else 'snapshots')
            subprocess.run([*compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'components/muse/sensors'), str(source), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)
