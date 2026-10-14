"""Keep camera capability discovery and asynchronous dispatch consistent."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class K10CameraRegistration(unittest.TestCase):
    def test_camera_available_only_when_enabled(self):
        registration = (ROOT / 'main/noise_control.cpp').read_text(encoding='utf-8')
        blocks = re.findall(r'#if CONFIG_MUSE_K10_CAMERA\n(.*?)#endif', registration, re.S)
        commands = [b for b in blocks if '"camera.capture"' in b]
        self.assertEqual(len(commands), 1)
        self.assertIn('"timeout_ms", K10_CAMERA_COMMAND_TIMEOUT_MS', commands[0])
        app = (ROOT / 'main/app.c').read_text(encoding='utf-8')
        self.assertRegex(app, r'#if CONFIG_MUSE_WATCHER_CAMERA \|\| CONFIG_MUSE_K10_CAMERA\s+if \(strcmp\(command, "camera.capture"\)')
        dispatch = app.split('if (strcmp(command, "camera.capture") == 0) {', 1)[1].split('#if CONFIG_HOMEHUB_VOICE', 1)[0]
        self.assertIn('xTaskCreateWithCaps(camera_capture_task', dispatch)
        self.assertIn('"_async", true', dispatch)
        self.assertRegex(app, r'#if CONFIG_MUSE_K10_CAMERA\s+cJSON \*result = k10_camera_command\(\);')


if __name__ == '__main__':
    unittest.main()
