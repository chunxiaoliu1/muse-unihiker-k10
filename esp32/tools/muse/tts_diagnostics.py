"""Capture TTS/USB/crash metadata without resetting or logging credentials/chat."""
import argparse
import json
from pathlib import Path
import re
import time

import serial


def safe_line(line):
    line = re.sub(r'\x1b\[[0-9;]*m', '', line).strip()
    if re.search(r'mgst_|sk_car_|Bearer|Authorization|X-API-Key|password|refresh_token|access_token', line, re.I):
        return None
    if line.startswith('@tts.test '):
        try:
            result = json.loads(line[len('@tts.test '):])
            return '@tts.test ' + json.dumps({k: result[k] for k in ('ok', 'frames', 'error') if k in result})
        except ValueError:
            return None
    if line.startswith('@sdk.config '):
        try:
            result = json.loads(line[len('@sdk.config '):])
            if result.get('id') == 'diag':
                return '@sdk.config ' + json.dumps({k: result[k] for k in ('ok', 'error', 'configured', 'active', 'runtime_only') if k in result})
        except ValueError:
            pass
        return None
    # Explicit tags/phrases only: never save raw @chat or reply text.
    if re.search(r'link\.heartbeat:|link\.stack:|tts\.stream:|link\.main:|muse\.usb:|'
                 r'PCM streaming TTS requested|Streaming TTS failed|muse reply:|reply interrupted|'
                 r'ESP-ROM:|rst:0x|Saved PC:|Guru Meditation|Backtrace:|Stack canary|'
                 r'stack overflow|CORRUPT HEAP|Brownout|watchdog|Task watchdog|'
                 r'assert failed:|abort\(\)|Rebooting|ELF file SHA256|'
                 r'Core\s+\d+ register dump|^(?:PC|PS|A\d+|SAR|EXCCAUSE|EXCVADDR|LBEG|LEND|LCOUNT)\s+:', line, re.I):
        return line
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('--seconds', type=float, default=180)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--test', action='store_true', help='Synthesize the built-in test once (uses user quota)')
    parser.add_argument('--long', action='store_true', help='Use the fixed long test passage with --test')
    parser.add_argument('--selftest', action='store_true', help='Run buffer/heap tests without a network call')
    parser.add_argument('--status', action='store_true', help='Probe the configuration channel without changing settings')
    parser.add_argument('--status-on-entry', action='store_true', help='Probe status once USB mode reports On')
    args = parser.parse_args()
    with serial.Serial() as port, args.output.open('w', encoding='utf-8') as log:
        port.port = args.port
        port.baudrate = 115200
        port.timeout = 0.2
        port.write_timeout = 3
        port.dtr = port.rts = False
        port.open()
        print('CAPTURE_READY', flush=True)
        if args.test or args.selftest or args.status:
            request = {'id': 'diag', 'op': 'status' if args.status else 'tts.selftest' if args.selftest else 'tts.test', 'long': args.long}
            try:
                port.write(('>sdk.config=' + json.dumps(request) + '\n').encode())
            except (serial.SerialException, OSError):
                print('PROBE_WRITE_FAILED; continuing read-only capture', flush=True)
        end = time.monotonic() + args.seconds
        probe_on_entry = args.status_on_entry
        pending = b''
        while time.monotonic() < end:
            try:
                pending += port.read(4096)
            except (serial.SerialException, OSError):
                port.close()
                pending = b''
                print('USB_RECONNECTING', flush=True)
                while time.monotonic() < end:
                    try:
                        port.open()
                        print('USB_RECONNECTED', flush=True)
                        break
                    except (serial.SerialException, OSError):
                        time.sleep(0.25)
                continue
            while b'\n' in pending:
                raw, pending = pending.split(b'\n', 1)
                line = safe_line(raw.decode('utf-8', 'replace'))
                if line:
                    log.write(line + '\n'); log.flush()
                    print(line, flush=True)
                    if probe_on_entry and 'muse.usb: configuration On:' in line:
                        probe_on_entry = False
                        try:
                            port.write(b'>sdk.config={"id":"diag","op":"status"}\n')
                        except (serial.SerialException, OSError):
                            print('PROBE_WRITE_FAILED; continuing read-only capture', flush=True)
                    if args.test and line.startswith('@tts.test '):
                        end = min(end, time.monotonic() + 8) # include memory after teardown
                    if args.selftest and line.startswith('@sdk.config '):
                        end = min(end, time.monotonic() + 8)
            if len(pending) > 16384:
                pending = b''
        print('CAPTURE_FINISHED', flush=True)


if __name__ == '__main__':
    main()
