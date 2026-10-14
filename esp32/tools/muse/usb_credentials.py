"""USB credential diagnostics. Never echo tokens or raw serial logs."""
import argparse
import json
from pathlib import Path
import re
import time
import uuid
import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('port')
    parser.add_argument('operation', choices=['board', 'status', 'save', 'clear', 'restart', 'tts.selftest', 'tts.clear', 'tts.test'])
    parser.add_argument('--sdkconfig', type=Path, help='local development sdkconfig holding the token to save')
    parser.add_argument('--watch-boot', action='store_true', help='after restart, watch 12 seconds without printing raw logs')
    args = parser.parse_args()
    request = {'id': uuid.uuid4().hex, 'op': args.operation}
    if args.operation == 'save':
        if not args.sdkconfig: parser.error('save requires a local --sdkconfig file')
        match = re.search(r'^CONFIG_GADGET_SDK_TOKEN="(mgst_[A-Za-z0-9_-]{43})"$',
                          args.sdkconfig.read_text(encoding='utf-8'), re.M)
        if not match: raise SystemExit('No SDK token found in the selected local config')
        request['token'] = match.group(1)
    with serial.Serial() as port:
        port.port = args.port
        port.baudrate = 115200
        port.timeout = 0.5
        port.write_timeout = 5
        port.dtr = port.rts = False
        port.open()  # Do not reset the S3 by asserting USB control lines.
        port.reset_input_buffer()
        wire = '>status\n' if args.operation == 'board' else '>sdk.config=' + json.dumps(request) + '\n'
        port.write(wire.encode())
        wire = None
        request.pop('token', None)
        deadline = time.monotonic() + 45
        prefix = '@status ' if args.operation == 'board' else '@sdk.config '
        while time.monotonic() < deadline:
            line = port.readline(8192).decode(errors='replace').strip()
            start = line.find(prefix)
            if start < 0: continue
            line = line[start:]
            try: response = json.loads(line[len(prefix):])
            except ValueError: continue
            if args.operation != 'board' and response.get('id') != request['id']: continue
            # Allowlisted metadata only. Raw logs and credentials are never printed.
            visible = {key: response[key] for key in
                ('board', 'device', 'ok', 'v', 'runtime_only', 'configured', 'active', 'restart_required', 'error',
                 'tts_supported', 'tts_configured', 'tts_active', 'tts_restart_required', 'tts_storage_ok',
                 'tts_voice', 'tts_language', 'tts_state', 'tts_error')
                if key in response}
            if isinstance(visible.get('device'), dict):
                device = visible['device']
                visible['device'] = {key: device.get(key) for key in ('name', 'fw')}
                visible['wifi_state'] = device.get('wifi', {}).get('state')
                visible['link_state'] = device.get('link', {}).get('state')
                visible['paired'] = device.get('link', {}).get('paired')
            print(json.dumps(visible, ensure_ascii=False))
            if response.get('ok') is False: raise SystemExit(1)
            if args.operation == 'restart' and args.watch_boot:
                end = time.monotonic() + 12
                startup = bytearray()
                try:
                    while time.monotonic() < end:
                        startup.extend(port.read(4096))
                        if len(startup) > 131072: del startup[:65536]
                except serial.SerialException:
                    print('USB disconnected during restart; reconnect for status')
                print(json.dumps({'boot_starting_seen': b'starting' in startup.lower(),
                    'panic_seen': bool(re.search(rb'Guru Meditation|panic\(|Backtrace:|abort\(\)', startup))}))
            return
    raise SystemExit('No matching USB response within 45 seconds. On configured K10 devices, enter USB mode from the settings menu first.')


if __name__ == '__main__': main()
