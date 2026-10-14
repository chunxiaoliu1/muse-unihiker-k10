import { SerialClient, validToken, validTtsConfig, saveConfiguration } from './serial-client.mjs';

const $ = id => document.getElementById(id);
const messages = {
  busy: 'The device is busy. Please try again shortly.',
  invalid_token: 'Invalid SDK Token. Copy the complete 48-character token from gadgets.muse.ai.',
  invalid_request: 'Unable to complete this action. Update the K10 firmware and try again.',
  storage_error: 'Settings could not be saved. Reconnect and try again.',
  timeout: 'K10 is not responding. Wait for startup, or enter USB mode if it is already set up. Close other connected pages or serial tools and try again.',
  disconnected: 'Device disconnected. To continue setup, enter USB mode in the K10 menu and reconnect.',
  wrong_board: 'UNIHIKER K10 was not detected. Setup has stopped.',
  private_firmware: 'This firmware does not support web setup. Update the K10 firmware.',
  write_error: 'USB write failed. Reconnect your device.',
  invalid_tts: 'Enter a valid Cartesia API Key, 36-character Voice ID, and language.',
  tts_not_configured: 'Voice is not enabled. Enter your voice settings, choose Save all settings, and reconnect after K10 restarts to test.',
  tts_unsupported: 'This firmware does not support voice settings. Update the K10 firmware.',
  wifi_offline: 'K10 is offline. Set up Wi-Fi in the Muse app first.',
  restart_required: 'Restart K10 to apply the voice settings first.',
  speaker_off: 'The speaker is off. Enable it in the K10 menu first.',
  no_changes: 'Enter an SDK Token or voice settings to save.',
};
let busy = false;
let client;
let deviceStatus;
function notice(text, error = false) {
  $('notice').textContent = text;
  $('notice').classList.toggle('error', error);
}
function controls() {
  const connected = !!client?.verified;
  $('connect').disabled = busy || connected || !navigator.serial || !isSecureContext;
  $('disconnect').disabled = busy || !connected;
  for (const id of ['save', 'clear', 'factory', 'restart', 'status', 'token']) $(id).disabled = busy || !connected;
  $('tts-language').disabled = busy;
  for (const id of ['tts-key', 'tts-voice', 'tts-clear', 'tts-test']) $(id).disabled = busy || !connected || !client.ttsSupported;
}
function showStatus(status, fillInputs = false) {
  deviceStatus = status;
  $('device').textContent = `Connected · ${status.board}`;
  $('configuration').textContent = status.configured
    ? (status.restart_required ? 'SDK Token saved. Restart K10 to apply it.' : 'SDK Token is set. You can pair K10 in the Muse app.')
    : (status.restart_required ? 'SDK Token cleared. Restart K10 to apply the change.' : 'No SDK Token is set.');
  const voiceStates = { idle: 'Voice replies are enabled.', connecting: 'Connecting to the voice service…', buffering: 'Preparing audio…',
    playing: 'Playing audio.', done: 'Audio playback finished.', error: 'Voice playback failed. Check your connection, API Key, and service credits.' };
  $('tts-configuration').textContent = status.tts_supported !== true ? 'This firmware does not support voice settings. Update the K10 firmware.'
    : status.tts_storage_ok === false ? 'Unable to read voice settings. Save them again.'
    : status.tts_restart_required ? 'Voice settings changed. Restart K10 to apply them.'
    : status.tts_configured ? (voiceStates[status.tts_state] || 'Voice replies are enabled.')
    : 'Voice is off. Replies are shown as text only.';
  if (fillInputs && status.tts_voice) $('tts-voice').value = status.tts_voice;
  if (fillInputs && status.tts_language) $('tts-language').value = status.tts_language;
}
async function operation(fn) {
  if (busy) return;
  busy = true; controls();
  notice('Communicating with K10. Keep USB connected…');
  try { await fn(); }
  catch (error) {
    notice(error.name === 'NotFoundError' ? 'Device selection cancelled.' : (messages[error.message] || 'Unable to connect. Check the USB data cable and close other connected pages or serial tools.'), true);
  } finally { busy = false; controls(); }
}
client = new SerialClient(navigator.serial, () => {
  deviceStatus = null;
  $('device').textContent = 'Device disconnected';
  $('configuration').textContent = 'Reconnect to check your settings.';
  $('token').value = '';
  controls();
  $('tts-key').value = '';
  $('tts-configuration').textContent = 'Reconnect to check voice settings.';
});
$('connect').onclick = () => operation(async () => {
  showStatus(await client.connect(), true); notice('Connected to K10.');
});
$('disconnect').onclick = () => operation(async () => {
  await client.disconnect(); notice('Disconnected.');
});
$('status').onclick = () => operation(async () => {
  showStatus(await client.request('status')); notice('Device status updated.');
});
$('config-form').onsubmit = event => {
  event.preventDefault();
  if (busy || !client.verified) return;
  let token = $('token').value.trim();
  let tts = { api_key: $('tts-key').value.trim(), voice: $('tts-voice').value.trim(), language: $('tts-language').value };
  if (token && !validToken(token)) { notice(messages.invalid_token, true); return; }
  if (tts.api_key && !validTtsConfig(tts)) { notice(messages.invalid_tts, true); return; }
  if (!tts.api_key && deviceStatus?.tts_configured &&
      (tts.voice !== deviceStatus.tts_voice || tts.language !== deviceStatus.tts_language)) {
    notice('To change the voice or language, enter your Cartesia API Key again, then save all settings.', true);
    return;
  }
  if (!token && !tts.api_key) { notice(messages.no_changes, true); return; }
  if (!tts.api_key) tts = null;
  $('token').value = '';
  $('tts-key').value = '';
  operation(async () => {
    const pending = saveConfiguration(client, { token, tts }); token = ''; tts = null;
    try {
      const saved = await pending;
      $('configuration').textContent = 'Your settings have been saved.';
      notice(`${saved.join(' and ')} saved. K10 is restarting. For first-time setup, enable Developer mode in the Muse app, add K10, and set up Wi-Fi.`);
    } catch (error) {
      if (error.restartFailed) {
        notice('All settings were saved, but the restart could not be confirmed. If K10 has not restarted, press RESET once.', true);
      } else if (error.saved?.length) {
        notice(`${error.saved.join(' and ')} saved. The remaining settings could not be confirmed, so no restart was requested. Reconnect, check the status, and save the remaining settings.`, true);
      } else throw error;
    }
  });
};
$('restart').onclick = () => operation(async () => {
  await client.request('restart'); await client.disconnect();
  notice('K10 is restarting. To continue setup, enter USB mode again and reconnect.');
});
$('tts-clear').onclick = () => operation(async () => {
  showStatus(await client.request('tts.clear')); $('tts-key').value = '';
  notice('Voice settings cleared. Choose Restart device to apply the change. Pairing and Wi-Fi are kept.');
});
$('tts-test').onclick = () => operation(async () => {
  showStatus(await client.request('tts.test'));
  notice('Preparing the voice test. Listen to K10, then choose Refresh status to check the result.');
});
$('clear').onclick = () => {
  if (!confirm('Clear the SDK Token on this device? Muse pairing and Wi-Fi will be kept. Restart K10 to apply the change.')) return;
  operation(async () => { showStatus(await client.request('clear')); notice('SDK Token cleared. Restart K10 to apply the change.'); });
};
$('factory').onclick = () => {
  if (!confirm('Clear the SDK Token, Cartesia key, Muse pairing, and Wi-Fi, then restart? You will need to set up K10 again.')) return;
  operation(async () => {
    await client.request('factory'); await client.disconnect();
    notice('All user settings were cleared and a restart was requested. Reconnect and enter your SDK Token.');
  });
};
if (!isSecureContext) notice('Open this page from an HTTPS website or localhost.', true);
else if (!navigator.serial) notice('This browser does not support USB serial connections. Use Chrome or Edge on a computer.', true);
controls();
