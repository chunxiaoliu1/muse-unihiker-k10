"""Print K10 sensor snapshots without resetting it: sensors.py COM10 [seconds]."""
import json
import sys
import time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else 'COM10'
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
with serial.Serial(port, 115200, timeout=0.3) as console:
    console.write(b'>sensors\n')
    end = time.monotonic() + seconds
    pending = bytearray()
    while time.monotonic() < end:
        pending.extend(console.read(4096))
        while b'\n' in pending:
            raw, _, rest = pending.partition(b'\n')
            pending = bytearray(rest)
            if raw.startswith(b'@sensors '):
                try:
                    print(json.dumps(json.loads(raw[9:]), ensure_ascii=False), flush=True)
                except (ValueError, UnicodeError):
                    pass
