"""Compile the real credential module against a queue/NVS/gate fake on the host."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'sdkconfig.h': '#define CONFIG_MUSE_ENABLED 1\n#define CONFIG_GADGET_SDK_RUNTIME_ONLY 1\n',
    'app.h': '#include <stdbool.h>\nbool app_credentials_begin(void);\nvoid app_credentials_end(void);\n',
    'identity.h': 'const char *identity_node_id(void);\n',
    'muse_settings.h': '#include <stdbool.h>\nbool muse_settings_clear_credentials(void);\n',
    'muse_board.h': 'typedef struct { const char *name; } board_t;\nextern const board_t *muse_board;\n',
    'esp_system.h': 'void esp_restart(void);\n',
    'freertos/FreeRTOS.h': '#define pdTRUE 1\n#define pdMS_TO_TICKS(x) (x)\n',
    'freertos/task.h': 'void vTaskDelay(int ticks);\n',
    'freertos/queue.h': '''#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueSend(QueueHandle_t queue, const void *item, int ticks);
int xQueueReceive(QueueHandle_t queue, void *item, int ticks);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
''',
}
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "config_store.h"
#include "credentials/sdk_credentials.h"
#include "credentials/sdk_token_format.h"
#include "muse_board.h"

static board_t board = {"UNIHIKER K10"};
const board_t *muse_board = &board;
static char stored[64];
static bool locked, busy, storage_failure, manual_cleared;
static unsigned reads, writes;
static unsigned queued, item_size;
static unsigned char queue_memory[256];
const char *identity_node_id(void) { return "homelink-test"; }
bool app_credentials_begin(void) { if (busy) return false; locked = true; return true; }
void app_credentials_end(void) { assert(locked); locked = false; }
void esp_restart(void) { assert(locked); }
void vTaskDelay(int ticks) { (void)ticks; }
void *xQueueCreate(unsigned count, unsigned size) { assert(count == 2); item_size = size; queued = 0; return queue_memory; }
int xQueueSend(void *q, const void *p, int ticks) { (void)q; (void)ticks; if (queued) return 0; memcpy(queue_memory, p, item_size); queued = 1; return 1; }
int xQueueReceive(void *q, void *p, int ticks) { (void)q; (void)ticks; if (!queued) return 0; memcpy(p, queue_memory, item_size); queued = 0; return 1; }
unsigned uxQueueMessagesWaiting(void *q) { (void)q; return queued; }
bool config_get_str(const char *key, char *out, size_t cap) { (void)key; reads++; if (!stored[0] || strlen(stored) >= cap) return false; strcpy(out, stored); return true; }
config_key_lookup_t config_key_lookup(const char *key) { (void)key; reads++; return stored[0] ? CONFIG_KEY_FOUND : CONFIG_KEY_NOT_FOUND; }
bool config_set_str(const char *key, const char *value) { assert(locked); assert(!strcmp(key, SDK_TOKEN_KEY)); writes++; if (storage_failure) return false; strcpy(stored, value); return true; }
bool config_erase_key(const char *key) { (void)key; assert(locked); writes++; stored[0] = 0; return true; }
bool config_clear_setup(void) { assert(locked); return true; } // Pairing rollback preserves SDK.
bool muse_settings_clear_credentials(void) { assert(locked); manual_cleared = true; return true; }

int main(void) {
    const char *token = "mgst_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    assert(sdk_token_valid(token));
    assert(!sdk_token_valid("mgst_short"));
    sdk_credentials_init(); assert(!sdk_credentials_active());
    reads = writes = 0;
    char command[200];
    snprintf(command, sizeof(command), "sdk.config={\"id\":\"save_1\",\"op\":\"save\",\"token\":\"%s\"}", token);
    assert(sdk_credentials_submit(command, true));
    assert(reads == 0 && writes == 0); // Serial/PSRAM task performs no NVS I/O.
    sdk_credentials_process();
    assert(!strcmp(stored, token)); assert(!sdk_credentials_active()); // Pending restart.
    sdk_credentials_init(); assert(!strcmp(sdk_credentials_active(), token)); // Emulated reboot.
    assert(!sdk_credentials_submit("sdk.config={\"id\":\"bad_1\",\"op\":\"save\",\"token\":\"short\"}", true));
    assert(!sdk_credentials_submit(command, false)); // Truncated line cannot commit.
    busy = true;
    assert(sdk_credentials_submit("sdk.config={\"id\":\"busy_1\",\"op\":\"clear\"}", true));
    sdk_credentials_process(); assert(!strcmp(stored, token)); busy = false;
    storage_failure = true;
    assert(sdk_credentials_submit(command, true)); sdk_credentials_process(); storage_failure = false;
    assert(sdk_credentials_submit("sdk.config={\"id\":\"clear_1\",\"op\":\"clear\"}", true));
    sdk_credentials_process(); assert(!stored[0]); assert(sdk_credentials_active());
    sdk_credentials_init(); assert(!sdk_credentials_active());
    assert(sdk_credentials_submit("sdk.config={\"id\":\"fresh_1\",\"op\":\"status\"}", true));
    sdk_credentials_process();
    assert(sdk_credentials_submit(command, true)); sdk_credentials_process();
    sdk_credentials_init(); assert(sdk_credentials_active());
    assert(sdk_credentials_submit("sdk.config={\"id\":\"factory_1\",\"op\":\"factory\"}", true));
    sdk_credentials_process(); assert(!stored[0]); assert(manual_cleared);
    return 0;
}
'''


class SDKCredentialsTest(unittest.TestCase):
    def test_nvs_queue_gate_and_restart_contract(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not cc or not shutil.which(cc[0]): self.skipTest('host C compiler not available')
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        if not (cjson / 'cJSON.c').is_file(): self.skipTest('build firmware to download cJSON')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, text in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            harness = directory / 'harness.c'
            harness.write_text(HARNESS)
            binary = directory / ('credentials.exe' if os.name == 'nt' else 'credentials')
            result = subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(directory), '-I', str(cjson), '-I', str(ROOT / 'main'),
                str(harness), str(ROOT / 'main/credentials/sdk_credentials.c'), str(cjson / 'cJSON.c'),
                '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('mgst_', result.stdout)
            responses = [json.loads(line[12:]) for line in result.stdout.splitlines() if line.startswith('@sdk.config ')]
            self.assertTrue(responses[0]['ok'])
            self.assertTrue(responses[0]['restart_required'])
            self.assertIn('invalid_token', [item.get('error') for item in responses])
            self.assertIn('busy', [item.get('error') for item in responses])
            self.assertIn('storage_error', [item.get('error') for item in responses])
            self.assertFalse(responses[-1]['configured'])


if __name__ == '__main__': unittest.main()
