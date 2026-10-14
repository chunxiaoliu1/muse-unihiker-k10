import test from 'node:test';
import assert from 'node:assert/strict';
import { SerialClient, validToken, validTtsConfig, saveConfiguration } from '../tools/muse/usb-config/serial-client.mjs';

const TOKEN = 'mgst_' + 'A'.repeat(43); // Synthetic fixture, never a service credential.

test('connection deadline also covers a USB write stalled by normal mode', async () => {
  const { port, client } = create(undefined, undefined, 25);
  port.writable = new WritableStream({ write: () => new Promise(() => {}) });
  await assert.rejects(client.connect(), /timeout/);
  assert.equal(port.closed, true);
  assert.equal(client.verified, false);
});
class Port {
  constructor(handler, board = 'DFRobot UNIHIKER K10') {
    this.handler = handler;
    this.board = board;
    this.writes = [];
    this.stored = false;
    this.active = false;
    this.closed = false;
    this.readable = new ReadableStream({ start: control => { this.input = control; } });
    this.writable = new WritableStream({ write: bytes => {
      const line = new TextDecoder().decode(bytes);
      this.writes.push(line);
      assert.ok(line.startsWith('>sdk.config='));
      const request = JSON.parse(line.slice(12));
      if (this.handler?.(request, this) === false) return;
      if (request.op === 'save') this.stored = true;
      if (request.op === 'clear' || request.op === 'factory') this.stored = false;
      if (request.op === 'restart') this.active = this.stored;
      this.respond({ v: 1, id: request.id, ok: true, board: this.board, device: 'homelink-test',
        runtime_only: true,
        configured: this.stored, active: this.active, restart_required: this.stored !== this.active });
    } });
  }
  async open(options) { assert.equal(options.baudRate, 115200); }
  async setSignals() { assert.fail('configuration must not change reset control lines'); }
  async close() { this.closed = true; }
  respond(json) {
    const bytes = new TextEncoder().encode('ignored device logs\n@sdk.config ' + JSON.stringify(json) + '\n');
    // Split mid-frame, as USB commonly does.
    this.input.enqueue(bytes.slice(0, 31));
    this.input.enqueue(bytes.slice(31));
  }
}
function create(handler, board, timeout = 100) {
  const port = new Port(handler, board);
  const client = new SerialClient({ requestPort: async options => {
    assert.equal(options.filters[0].usbVendorId, 0x303a); return port;
  } }, () => {}, timeout);
  return { port, client };
}

test('SDK token form matches canonical base64url lengths and rejects command injection', () => {
  assert.equal(validToken(TOKEN), true);
  for (const value of [null, '', TOKEN.slice(1), TOKEN + 'A', TOKEN.slice(0, -1) + 'B', TOKEN + '\n>restart', 'refresh_token'])
    assert.equal(validToken(value), false);
});
test('connect verifies board; save and clear wait for correlated device acknowledgement', async () => {
  const { port, client } = create();
  assert.equal((await client.connect()).configured, false);
  const saved = await client.request('save', TOKEN);
  assert.equal(saved.configured, true);
  assert.equal(saved.restart_required, true);
  await client.request('restart');
  assert.equal((await client.request('status')).restart_required, false);
  assert.equal((await client.request('clear')).configured, false);
  await client.disconnect();
  assert.equal(port.closed, true);
});
test('wrong board disconnects before any credential write', async () => {
  const { port, client } = create(undefined, 'Another ESP32 board');
  await assert.rejects(client.connect(), /wrong_board/);
  await assert.rejects(client.request('save', TOKEN), /disconnected/);
  assert.equal(port.writes.length, 1);
  assert.equal(port.closed, true);
});
test('development firmware with an embedded token is rejected before configuration', async () => {
  const { port, client } = create((request, port) => {
    port.respond({ v: 1, id: request.id, ok: true, board: port.board, runtime_only: false });
    return false;
  });
  await assert.rejects(client.connect(), /private_firmware/);
  assert.equal(port.writes.length, 1);
  assert.equal(port.closed, true);
});
test('unrelated IDs and malformed replies cannot acknowledge a save', async () => {
  const { client } = create((request, port) => {
    if (request.op !== 'save') return;
    port.respond({ v: 1, id: 'unrelated', ok: true });
    port.input.enqueue(new TextEncoder().encode('@sdk.config invalid-json\n'));
    return false;
  }, undefined, 25);
  await client.connect();
  await assert.rejects(client.request('save', TOKEN), /timeout/);
  await client.disconnect();
});
test('storage error is propagated and a following status request still works', async () => {
  const { client } = create((request, port) => {
    if (request.op !== 'save') return;
    port.respond({ v: 1, id: request.id, ok: false, error: 'storage_error' });
    return false;
  });
  await client.connect();
  await assert.rejects(client.request('save', TOKEN), /storage_error/);
  assert.equal((await client.request('status')).configured, false);
  await client.disconnect();
});
test('initial connection tolerates a temporary device setup lock', async () => {
  let locked = true;
  const { client } = create((request, port) => {
    if (request.op !== 'status' || !locked) return;
    locked = false;
    port.respond({ v: 1, id: request.id, ok: false, error: 'busy' });
    return false;
  }, undefined, 3000);
  await client.connect();
  assert.equal(client.verified, true);
  await client.disconnect();
});
test('USB loss rejects outstanding save instead of reporting success', async () => {
  const { client } = create((request, port) => {
    if (request.op !== 'save') return;
    port.input.error(new Error('physical USB disconnected')); return false;
  });
  await client.connect();
  await assert.rejects(client.request('save', TOKEN), /disconnected/);
  await client.disconnect();
});
test('concurrent operations cannot overwrite the pending request', async () => {
  const { client } = create(request => request.op !== 'save', undefined, 25);
  await client.connect();
  const save = client.request('save', TOKEN);
  await assert.rejects(client.request('clear'), /busy/);
  await assert.rejects(save, /timeout/);
  await client.disconnect();
});

const TTS = { api_key: 'synthetic_test_key_only', voice: 'a0e99841-438c-4a64-b679-ae501e7d6091', language: 'zh' };
test('combined save validates voice settings before writing any SDK token', async () => {
  const calls = [];
  const client = { ttsSupported: true, request: async op => calls.push(op) };
  await assert.rejects(saveConfiguration(client, { token: TOKEN, tts: { ...TTS, voice: 'invalid' } }), /invalid_tts/);
  assert.deepEqual(calls, []);
});
test('combined save waits for both acknowledgements before requesting restart', async () => {
  const calls = [];
  let acknowledgeVoice;
  const client = { ttsSupported: true, request: async op => {
    calls.push(op);
    if (op === 'tts.save') await new Promise(resolve => { acknowledgeVoice = resolve; });
  }, disconnect: async () => calls.push('disconnect') };
  const saving = saveConfiguration(client, { token: TOKEN, tts: { ...TTS } });
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(calls, ['save', 'tts.save']);
  acknowledgeVoice();
  assert.deepEqual(await saving, ['SDK Token', 'voice settings']);
  assert.deepEqual(calls, ['save', 'tts.save', 'restart', 'disconnect']);
});
test('a failed second save reports the confirmed first save and never restarts', async () => {
  const calls = [];
  const client = { ttsSupported: true, request: async op => {
    calls.push(op); if (op === 'tts.save') throw new Error('storage_error');
  } };
  await assert.rejects(saveConfiguration(client, { token: TOKEN, tts: { ...TTS } }), error => {
    assert.deepEqual(error.saved, ['SDK Token']); return error.message === 'storage_error';
  });
  assert.deepEqual(calls, ['save', 'tts.save']);
});
test('voice-only save preserves the SDK token and distinguishes restart failure', async () => {
  const calls = [];
  const client = { ttsSupported: true, request: async op => {
    calls.push(op); if (op === 'restart') throw new Error('disconnected');
  } };
  await assert.rejects(saveConfiguration(client, { tts: { ...TTS } }), error => {
    assert.deepEqual(error.saved, ['voice settings']); return error.restartFailed === true;
  });
  assert.deepEqual(calls, ['tts.save', 'restart']);
});
test('TTS rejects header/command injection and invalid voice IDs before USB writes', () => {
  assert.equal(validTtsConfig(TTS), true);
  for (const config of [null, { ...TTS, api_key: TTS.api_key + '\r\nX-API-Key: another' },
    { ...TTS, voice: '../other-host' }, { ...TTS, language: 'zh\n' }, { ...TTS, api_key: 'A'.repeat(256) }])
    assert.equal(validTtsConfig(config), false);
});
test('an older public firmware cannot receive unsupported TTS credentials', async () => {
  const { client, port } = create();
  await client.connect();
  await assert.rejects(client.request('tts.save', TTS), /tts_unsupported/);
  assert.equal(port.writes.length, 1);
  await client.disconnect();
});
test('TTS save waits for ACK, preserves SDK state, and cannot override request identity', async () => {
  const { client, port } = create((request, port) => {
    if (request.op === 'tts.save') {
      assert.equal(request.api_key, TTS.api_key);
      assert.equal(request.voice, TTS.voice);
      assert.equal(request.language, 'zh');
      assert.notEqual(request.id, 'injected');
      assert.equal(request.op, 'tts.save');
    }
    port.respond({ v: 1, id: request.id, ok: true, board: port.board, runtime_only: true,
      configured: true, active: true, tts_supported: true, tts_configured: request.op === 'tts.save',
      tts_restart_required: request.op !== 'status' });
    return false;
  });
  await client.connect();
  const reply = await client.request('tts.save', { ...TTS, id: 'injected', op: 'factory' });
  assert.equal(reply.configured, true);
  assert.equal(reply.tts_configured, true);
  assert.equal(reply.tts_restart_required, true);
  assert.equal(JSON.stringify(reply).includes(TTS.api_key), false);
  const cleared = await client.request('tts.clear');
  assert.equal(cleared.configured, true);
  assert.equal(cleared.tts_configured, false);
  await client.disconnect();
});
test('TTS transport propagates offline and persistence errors without retrying synthesis', async () => {
  const { client, port } = create((request, port) => {
    port.respond({ v: 1, id: request.id, ok: request.op === 'status', board: port.board,
      runtime_only: true, tts_supported: true, error: request.op === 'tts.test' ? 'wifi_offline' : 'storage_error' });
    return false;
  });
  await client.connect();
  await assert.rejects(client.request('tts.test'), /wifi_offline/);
  await assert.rejects(client.request('tts.save', TTS), /storage_error/);
  assert.equal(port.writes.filter(line => JSON.parse(line.slice(12)).op === 'tts.test').length, 1);
  await client.disconnect();
});
