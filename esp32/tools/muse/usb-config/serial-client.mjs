export function validToken(token) {
  return typeof token === 'string' && /^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/.test(token);
}

export function validTtsConfig(config) {
  return !!config && typeof config.api_key === 'string' && /^[A-Za-z0-9_-]{16,255}$/.test(config.api_key)
    && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(config.voice)
    && /^[a-z]{2}$/.test(config.language);
}

// Validate every field before the first write; restart only after every ACK.
export async function saveConfiguration(client, { token = '', tts = null }) {
  const saved = [];
  try {
    if (token && !validToken(token)) throw new Error('invalid_token');
    if (tts && !client.ttsSupported) throw new Error('tts_unsupported');
    if (tts && !validTtsConfig(tts)) throw new Error('invalid_tts');
    if (!token && !tts) throw new Error('no_changes');
    if (token) {
      const pending = client.request('save', token); token = '';
      await pending; saved.push('SDK Token');
    }
    if (tts) {
      const pending = client.request('tts.save', tts); tts.api_key = '';
      await pending; saved.push('voice settings');
    }
    try { await client.request('restart'); }
    catch (error) { error.restartFailed = true; throw error; }
    try { await client.disconnect(); } catch { /* Restart was already accepted. */ }
    return saved;
  } catch (error) {
    error.saved = saved;
    throw error;
  } finally {
    token = '';
    if (tts) tts.api_key = '';
  }
}

// This protocol never returns credentials. Non-protocol logs are discarded.
export class SerialClient {
  constructor(serial, onDisconnect = () => {}, timeoutMs = 45000) {
    this.serial = serial;
    this.onDisconnect = onDisconnect;
    this.timeoutMs = timeoutMs;
    this.port = null;
    this.reader = null;
    this.pending = null;
    this.counter = 0;
    this.ttsSupported = false;
    this.verified = false;
  }

  async connect() {
    if (this.port) throw new Error('already_connected');
    const port = await this.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
    await port.open({ baudRate: 115200, flowControl: 'none' });
    // Leave DTR/RTS untouched: the S3 uses CDC control-line transitions to reset.
    this.port = port;
    this.readTask = this.readLoop(port);
    try {
      const deadline = Date.now() + this.timeoutMs;
      let status;
      for (;;) {
        try { status = await this.request('status'); break; }
        catch (error) {
          if (error.message !== 'busy' || Date.now() >= deadline) throw error;
          // Boot/network work may briefly own the setup gate. No write is retried.
          await new Promise(resolve => setTimeout(resolve, 1000));
        }
      }
      if (!/UNIHIKER K10/.test(status.board || '')) throw new Error('wrong_board');
      if (status.runtime_only !== true) throw new Error('private_firmware');
      this.verified = true;
      this.ttsSupported = status.tts_supported === true;
      return status;
    } catch (error) {
      await this.disconnect();
      throw error;
    }
  }

  abort(error) {
    if (this.pending) {
      const pending = this.pending;
      this.pending = null;
      clearTimeout(pending.timer);
      pending.reject(new Error(error));
    }
  }

  acceptLine(line) {
    if (!line.startsWith('@sdk.config ')) return;
    let response;
    try { response = JSON.parse(line.slice(12)); } catch { return; }
    if (response.v !== 1 || !this.pending || response.id !== this.pending.id ||
        typeof response.ok !== 'boolean') return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timer);
    if (response.ok) pending.resolve(response);
    else pending.reject(new Error(['busy', 'invalid_token', 'invalid_request', 'storage_error', 'invalid_tts', 'tts_not_configured', 'wifi_offline', 'restart_required', 'speaker_off'].includes(response.error)
      ? response.error : 'device_error'));
  }

  async readLoop(port) {
    const reader = port.readable.getReader();
    this.reader = reader;
    const decoder = new TextDecoder();
    let buffer = '';
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let end;
        while ((end = buffer.indexOf('\n')) >= 0) {
          this.acceptLine(buffer.slice(0, end).replace(/\r$/, ''));
          buffer = buffer.slice(end + 1);
        }
        if (buffer.length > 8192) buffer = ''; // Bound camera/log lines.
      }
    } catch { /* No raw log or platform error gets displayed. */ }
    finally {
      reader.releaseLock();
      this.reader = null;
      this.verified = false;
      this.ttsSupported = false;
      if (this.port === port) this.port = null;
      this.abort('disconnected');
      try { await port.close(); } catch { /* USB may already have disappeared. */ }
      this.onDisconnect();
    }
  }

  async request(op, token) {
    if (!this.port?.writable) throw new Error('disconnected');
    if (op !== 'status' && !this.verified) throw new Error('wrong_board');
    if (this.pending) throw new Error('busy');
    if (op === 'save' && !validToken(token)) throw new Error('invalid_token');
    if (op.startsWith('tts.') && !this.ttsSupported) throw new Error('tts_unsupported');
    if (op === 'tts.save' && !validTtsConfig(token)) throw new Error('invalid_tts');
    const id = `${Date.now().toString(36)}_${++this.counter}`;
    const fields = op === 'tts.save' ? { api_key: token.api_key, voice: token.voice, language: token.language } : {};
    let payload = `>sdk.config=${JSON.stringify({ id, op, ...(op === 'save' ? { token } : {}), ...fields })}\n`;
    fields.api_key = undefined;
    const port = this.port;
    const promise = new Promise((resolve, reject) => {
      this.pending = { id, resolve, reject, timer: setTimeout(() => this.abort('timeout'), this.timeoutMs) };
    });
    // Attach a handler immediately: disconnect may happen before writer.write returns.
    promise.catch(() => {});
    let writer;
    try {
      writer = port.writable.getWriter();
      // In normal mode the K10 has no RX task; even the OS write may stall.
      // The protocol deadline must cover the write as well as the reply.
      await Promise.race([writer.write(new TextEncoder().encode(payload)), promise]);
    } catch { this.abort('write_error'); }
    finally { writer?.releaseLock(); payload = ''; token = undefined; }
    return promise;
  }

  async disconnect() {
    this.abort('disconnected');
    if (this.reader) {
      try { await this.reader.cancel(); } catch { /* Already gone. */ }
    }
    await this.readTask;
  }
}
