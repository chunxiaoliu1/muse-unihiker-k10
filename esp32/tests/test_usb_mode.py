"""Run the real USB mode policy with transport/task failure injection on the host."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\n#define ESP_ERR_INVALID_STATE 2\n',
    'freertos/FreeRTOS.h': '#pragma once\n#include <stddef.h>\n#define pdPASS 1\n',
    'freertos/task.h': '#pragma once\ntypedef void (*TaskFunction_t)(void *);\n',
    'freertos/idf_additions.h': '#include "freertos/task.h"\nint xTaskCreateWithCaps(TaskFunction_t, const char*, unsigned, void*, unsigned, void*, unsigned);\n',
    'esp_heap_caps.h': '#define MALLOC_CAP_INTERNAL 1\n#define MALLOC_CAP_SPIRAM 2\n#define MALLOC_CAP_8BIT 4\nunsigned heap_caps_get_free_size(unsigned);\n',
    'esp_log.h': '#define ESP_LOGI(tag, ...) ((void)(tag))\n',
    'driver/usb_serial_jtag.h': '#include <stdbool.h>\nbool usb_serial_jtag_is_driver_installed(void);\nint usb_serial_jtag_driver_uninstall(void);\n',
    'muse_console.h': '#include <stddef.h>\n#include "esp_err.h"\nesp_err_t muse_console_install(size_t);\nvoid muse_console_close(void);\n',
    'muse_link.h': '#include <stdbool.h>\nbool muse_link_usb_mode_request(void);\n',
}
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "usb_mode.h"
static int requests, installed, tasks, uninstalls;
static bool fail_request, fail_driver, fail_task;
bool muse_link_usb_mode_request(void) { requests++; return !fail_request; }
bool usb_serial_jtag_is_driver_installed(void) { return false; }
int muse_console_install(size_t bytes) { assert(bytes == 1024); if (fail_driver) return ESP_ERR_NO_MEM; installed++; return ESP_OK; }
void muse_console_close(void) { assert(installed == 1); installed--; uninstalls++; }
unsigned heap_caps_get_free_size(unsigned caps) { (void)caps; return 20000; }
static void serial_entry(void *arg) { (void)arg; }
int xTaskCreateWithCaps(TaskFunction_t fn, const char *name, unsigned size, void *arg, unsigned priority, void *handle, unsigned caps) {
    (void)arg; (void)handle; (void)caps;
    assert(fn == serial_entry && !strcmp(name, "muse_serial") && size == 3584 && priority == 5);
    if (fail_task) return 0;
    tasks++; return 1;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    bool token = !strcmp(argv[1], "configured");
    muse_usb_mode_init(serial_entry, token);
    assert(installed == 0 && tasks == 0); // no resources allocated at init
    if (token) {
        assert(!muse_usb_mode_enabled() && requests == 0);
        fail_request = true;
        assert(!muse_usb_mode_request() && !muse_usb_mode_enabled());
        fail_request = false;
        assert(muse_usb_mode_request());
    } else {
        assert(muse_usb_mode_enabled() && requests == 1); // indefinite wait, no timer
    }
    int pending = requests;
    assert(muse_usb_mode_request() && requests == pending); // repeated entry coalesces
    assert(muse_usb_mode_start(false) == ESP_ERR_NO_MEM);
    assert(!muse_usb_mode_enabled() && installed == 0 && tasks == 0);
    assert(muse_usb_mode_request());
    fail_driver = true;
    assert(muse_usb_mode_start(true) == ESP_ERR_NO_MEM);
    assert(installed == 0 && tasks == 0);
    fail_driver = false; fail_task = true;
    assert(muse_usb_mode_request());
    assert(muse_usb_mode_start(true) == ESP_ERR_NO_MEM);
    assert(installed == 0 && tasks == 0 && uninstalls == 1); // full transport rollback
    fail_task = false;
    assert(muse_usb_mode_request());
    assert(muse_usb_mode_start(true) == ESP_OK);
    assert(muse_usb_mode_enabled() && installed == 1 && tasks == 1);
    pending = requests;
    assert(muse_usb_mode_request() && requests == pending);
    assert(muse_usb_mode_start(true) == ESP_OK && installed == 1 && tasks == 1);
    return 0;
}
'''


class USBModeTest(unittest.TestCase):
    def test_boot_policy_idempotence_and_allocation_failure_rollback(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('host C compiler not available')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, content in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding='utf-8')
            (directory / 'harness.c').write_text(HARNESS, encoding='utf-8')
            binary = directory / ('usb-mode.exe' if os.name == 'nt' else 'usb-mode')
            source = ROOT / 'components/muse/usb'
            subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(directory), '-I', str(source), str(directory / 'harness.c'),
                str(source / 'usb_mode.c'), '-o', str(binary)], check=True, capture_output=True)
            # Separate processes model a reboot: no persisted flag carries over.
            for case in ('configured', 'missing', 'configured'):
                subprocess.run([str(binary), case], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
