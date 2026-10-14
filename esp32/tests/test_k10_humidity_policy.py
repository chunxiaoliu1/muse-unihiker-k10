import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HumidityPolicy(unittest.TestCase):
    def test_threshold_repeat_recovery_and_invalid_samples(self):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(compiler[0]):
            self.skipTest('Host C compiler unavailable; same policy test runs on K10')
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            source = work / 'policy.c'
            source.write_text('#include "k10_humidity_policy.h"\n'
                              'int main(void) { return k10_humidity_policy_test() ? 0 : 1; }\n', encoding='utf-8')
            binary = work / ('policy.exe' if os.name == 'nt' else 'policy')
            subprocess.run([*compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'components/muse/sensors/events'), str(source),
                            '-o', str(binary)], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
