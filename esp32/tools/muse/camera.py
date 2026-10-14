"""Capture a K10 diagnostic photo without resetting: camera.py COM10 output.jpg.

This uses the same capture/result code as Muse's camera.capture, but over USB.
It saves the photo locally and does not upload it to Muse.
"""
import argparse
import base64
import json
import pathlib
import time

import serial


def save_photo(result, path):
    if not result.get('ok'):
        raise ValueError(result.get('error', {}).get('message', 'capture failed'))
    payload = result['payload']
    if payload.get('format') != 'jpeg-base64' or payload.get('mime_type') != 'image/jpeg':
        raise ValueError('unexpected camera format')
    jpeg = base64.b64decode(payload['data_base64'], validate=True)
    if len(jpeg) != payload['bytes'] or not jpeg.startswith(b'\xff\xd8') or not jpeg.endswith(b'\xff\xd9'):
        raise ValueError('incomplete JPEG')
    if len(jpeg) > 96 * 1024:
        raise ValueError('JPEG exceeds firmware limit')
    path.write_bytes(jpeg)
    return {k: v for k, v in payload.items() if k != 'data_base64'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--timeout', type=float, default=35)
    args = parser.parse_args()
    with serial.Serial() as console:
        console.port, console.baudrate, console.timeout = args.port, 115200, 0.2
        console.dtr = console.rts = False
        console.open()
        console.reset_input_buffer()
        console.write(b'>camera.capture\n')
        pending = bytearray()
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            pending.extend(console.read(4096))
            while b'\n' in pending:
                raw, _, rest = pending.partition(b'\n')
                pending = bytearray(rest)
                marker = b'@camera.capture '
                if marker in raw:
                    result = json.loads(raw.split(marker, 1)[1])
                    info = save_photo(result, args.output)
                    print(json.dumps({'path': str(args.output.resolve()), **info}, ensure_ascii=False))
                    return
                if b'k10.camera:' in raw or b'cam_hal:' in raw or b'camera:' in raw or b'Guru Meditation' in raw:
                    print(raw.decode('utf-8', errors='replace'), flush=True)
            if len(pending) > 256 * 1024:
                raise ValueError('serial response exceeds limit')
    raise TimeoutError('K10 did not return a camera result')


if __name__ == '__main__':
    main()
