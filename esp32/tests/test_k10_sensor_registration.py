"""K10's advertised command and dispatch must use the same board gate."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class K10SensorRegistration(unittest.TestCase):
    def test_command_is_advertised_and_dispatched_only_for_k10(self):
        pattern = r'#if CONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10\n(.*?)#endif'
        registration = (ROOT / 'main/noise_control.cpp').read_text(encoding='utf-8')
        dispatch = (ROOT / 'main/app.c').read_text(encoding='utf-8')
        advertised = [b for b in re.findall(pattern, registration, re.S) if '"sensors.read"' in b]
        handled = [b for b in re.findall(pattern, dispatch, re.S) if '"sensors.read"' in b]
        self.assertEqual(len(advertised), 1)
        self.assertEqual(len(handled), 1)
        self.assertIn('add_command(commands, "sensors.read"', advertised[0])
        self.assertIn('k10_sensors_command()', handled[0])
        self.assertIn('command_error(', handled[0])


if __name__ == '__main__':
    unittest.main()
