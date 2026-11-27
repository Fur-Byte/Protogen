const names = ['Happy', 'Surprised', 'Angry', 'vwv', 'Sleepy', 'Wink'],
  symbols = ['^‿^', 'O_O', '>_<', 'vωv', '–_–', '^_–'];
const keys = [
  'brightness',
  'startup_expression',
  'mouth_mode',
  'sensitivity',
  'noise_margin',
  'noise_floor',
  'boop_trigger',
  'boop_release',
  'blink_interval_ms',
  'blink_jitter_ms',
  'transition_ms',
  'attack_ms',
  'release_ms',
  'natural_blinks',
  'idle_motion',
  'boop_mode',
  'boop_duration_ms',
];
const $ = (id) => document.getElementById(id);
let state = null,
  brightnessDirty = false,
  busy = false,
  presets = [],
  booping = false,
  visorHidden = false;
let actionVersion = 0,
  pendingEdits = 0;
let acknowledgedWithoutRefresh = false;
function message(text, error = false) {
  const notice = $('message');
  notice.textContent = text;
  notice.className = 'notice ' + (error ? 'error' : 'good');
}

function fill(config) {
  for (const key of keys) {
    if (!(key in config)) continue;
    const input = $(key);
    if (input.type === 'checkbox') input.checked = Boolean(config[key]);
    else input.value = config[key];
  }
}

function collectChanges() {
  const config = collect();
  if (!brightnessDirty) delete config.brightness;
  return config;
}

function collect() {
  const config = {};
  for (const key of keys) {
    if (state && !(key in state.config)) continue;
    const input = $(key);
    if (!input.checkValidity()) throw Error('Check ' + input.previousElementSibling.textContent);
    const value = input.type === 'checkbox' ? Number(input.checked) : Number(input.value);
    if (!Number.isInteger(value)) throw Error('Settings must be whole numbers');
    config[key] = value;
  }
  return config;
}
function usbCredential() {
  const fragment = /^#usb=([A-Za-z0-9_-]{43})$/.exec(location.hash);
  if (fragment) history.replaceState(null, '', location.pathname);
  try {
    if (fragment) sessionStorage.setItem('protogen-usb', fragment[1]);
    return sessionStorage.getItem('protogen-usb');
  } catch {
    return fragment ? fragment[1] : null;
  }
}
const usbToken = usbCredential();
async function api(path, body) {
  const options = { headers: {}, redirect: 'error' };
  if (usbToken) options.headers.Authorization = 'Bearer ' + usbToken;
  if (body) {
    options.method = 'POST';
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  const response = await fetch(path, options);
  const value = await response.json();
  if (!response.ok) throw Error(value.error || 'Request failed');
  return value;
}
function draw(display) {
  const canvas = $('face');
  const ctx = canvas.getContext('2d');
  const bytes = display.frame.match(/.{2}/g).map((byte) => parseInt(byte, 16));
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  for (let tile = 0; tile < 14; tile++) {
    ctx.strokeStyle = '#1c2a1b';
    ctx.lineWidth = 1;
    ctx.strokeRect(tile * 80 + 1, 9, 78, 82);
  }
  for (let y = 0; y < 8; y++) {
    for (let x = 0; x < 112; x++) {
      const lit = (bytes[y * 14 + (x >> 3)] & (128 >> (x & 7))) !== 0;
      ctx.beginPath();
      ctx.arc(x * 10 + 5, y * 10 + 20, 2.8, 0, Math.PI * 2);
      ctx.fillStyle = lit ? '#b6f597' : '#1c291b';
      ctx.shadowColor = '#83e965';
      ctx.shadowBlur = lit ? 7 : 0;
      ctx.fill();
    }
  }
  ctx.shadowBlur = 0;
}
function renderConnection(label, online) {
  const connection = $('connection');
  if (connection.textContent !== label) connection.textContent = label;
  connection.className = online ? 'tag online' : 'tag';
}

function renderConnectionAndFaceControls({ simulation, status, config }) {
  renderConnection(simulation ? 'Desktop simulation' : 'USB connected', true);
  $('simulation').hidden = !simulation;
  $('display-label').textContent = simulation
    ? 'Same firmware · simulated hardware'
    : 'Live helmet frame';
  $('save').textContent = simulation ? 'Save in simulation' : 'Save to helmet';

  const brightness = status.brightness;
  if (brightness) {
    const approval =
      typeof brightness.approved === 'number' ? ` · Approved ${brightness.approved}` : '';
    $('brightness-report').textContent =
      `Configured ${brightness.configured} · Active ${brightness.effective} · Firmware ceiling ${brightness.maximum}${approval}` +
      '. Set brightness explicitly to approve a new level; Save records the approved base level.';
  } else {
    $('brightness-report').textContent = 'This firmware does not report its brightness ceiling.';
  }

  booping = status.boop_reading <= config.boop_trigger;
  visorHidden = !status.visible;
  $('boop').textContent = booping ? 'Release boop' : 'Boop';
  $('visor').textContent = visorHidden ? 'Show visor' : 'Hide visor';
  $('sleep').textContent = status.sleeping ? 'Wake display' : 'Sleep display';

  let reaction = names[status.expression];
  if (status.sleeping) reaction = 'Sleeping';
  else if (visorHidden) reaction = 'Visor hidden';
  else if (status.boop) reaction = 'Booped';
  $('reaction').textContent = reaction;
  document.querySelectorAll('.expression').forEach((button, index) => {
    button.setAttribute('aria-pressed', String(index === status.expression));
  });
}

function renderAudioAndTelemetry({ simulation, status }) {
  const hasMicrophone = Boolean(status.features & 4);
  $('audio-status').textContent = status.calibrating
    ? 'Calibrating…'
    : status.speaking
      ? 'Speaking'
      : 'Quiet';
  $('audio-help').textContent = hasMicrophone
    ? 'Calibrate while quiet for half a second. Save to keep the measured floor.'
    : 'Enable the connected microphone in the sketch to use audio.';
  $('microphone').disabled =
    busy || !hasMicrophone || typeof status.microphone_enabled !== 'boolean';
  $('microphone').textContent = status.microphone_enabled ? 'Mute microphone' : 'Enable microphone';
  $('calibrate').disabled =
    busy ||
    !hasMicrophone ||
    !status.microphone_enabled ||
    status.sleeping ||
    !status.visible ||
    status.boop ||
    Boolean(status.calibrating);

  $('envelope').textContent = status.envelope;
  $('gate').textContent = status.audio_gate;
  $('drops').textContent = status.audio_drops;
  $('level').style.width = Math.min(100, (status.envelope / 255) * 100) + '%';
  $('boop-reading').textContent =
    status.features & 2 ? `Boop sensor: ${status.boop_reading} / 1023` : '';

  const source = simulation
    ? 'Preview only · no helmet connected'
    : 'USB settings · changes stay local';
  const paw = status.paw_connected ? 'connected' : 'offline';
  $('footer').textContent =
    `${source} · Paw ${paw} · Faults ${status.sensor_faults} · SPI ${status.spi_hz / 1000000} MHz`;
}

function update(value) {
  state = value;
  renderConnectionAndFaceControls(value);
  renderAudioAndTelemetry(value);
  if (!pendingEdits) fill(value.config);
  draw(value.display);
}

async function action(payload, notice) {
  if (busy) return;
  actionVersion++;
  acknowledgedWithoutRefresh = false;
  const editsAtStart = pendingEdits;
  busy = true;
  $('settings').disabled = true;
  $('apply').disabled = true;
  try {
    let value;
    try {
      value = await api('/api/action', payload);
    } finally {
      busy = false;
      $('settings').disabled = false;
      $('apply').disabled = false;
    }
    const sameEdits = pendingEdits === editsAtStart;
    if (
      sameEdits &&
      (payload.action === 'set' || payload.action === 'load' || payload.action === 'defaults')
    ) {
      pendingEdits = 0;
      brightnessDirty = false;
    }
    if (value.action_acknowledged) {
      acknowledgedWithoutRefresh = true;
      const confirmation = sameEdits && notice ? notice : 'Helmet acknowledged the command.';
      message(`${confirmation} Status refresh failed. ${value.refresh_error}`, true);
      return;
    }
    update(value);
    if (notice && sameEdits) message(notice);
  } catch (error) {
    message(error.message, true);
  }
}
for (let i = 0; i < names.length; i++) {
  const b = document.createElement('button');
  b.className = 'expression';
  b.innerHTML = '<span aria-hidden="true">' + symbols[i] + '</span>' + names[i];
  b.setAttribute('aria-pressed', 'false');
  b.onclick = () => action({ action: 'expression', value: i });
  $('expressions').append(b);
  const option = document.createElement('option');
  option.value = i;
  option.textContent = names[i];
  $('startup_expression').append(option);
}
$('settings').addEventListener('input', (event) => {
  pendingEdits++;
  if (event.target.id === 'brightness') brightnessDirty = true;
  message('Changes ready. Apply settings to preview them.');
});
$('apply').onclick = () => {
  try {
    action(
      { action: 'set', config: collectChanges() },
      'Settings applied temporarily. Save explicitly to keep them.',
    );
  } catch (error) {
    message(error.message, true);
  }
};
$('save').onclick = () => {
  if (pendingEdits) {
    message('Apply your edited settings before saving.', true);
    return;
  }
  action(
    { action: 'save' },
    state?.simulation
      ? 'Saved for this preview session. Download a profile to keep it.'
      : 'Settings saved on the helmet.',
  );
};
$('load').onclick = () => action({ action: 'load' }, 'Saved settings loaded.');
$('defaults').onclick = () => action({ action: 'defaults' }, 'Defaults applied temporarily.');
$('calibrate').onclick = () =>
  action({ action: 'calibrate' }, 'Measuring the quiet noise floor for half a second.');
$('sleep').onclick = () => action({ action: 'sleep', value: state?.status.sleeping ? 0 : 1 });
$('microphone').onclick = () =>
  action({ action: 'microphone', value: Number(!state.status.microphone_enabled) });
$('boop').onclick = () => {
  action({ action: 'simulate', sensor: 'boop', value: Number(!booping) });
};
$('visor').onclick = () => {
  action({ action: 'simulate', sensor: 'visor', value: Number(!visorHidden) });
};
$('sim-audio').onchange = () =>
  action({ action: 'simulate', sensor: 'audio', value: Number($('sim-audio').value) });
$('tap').onclick = () => action({ action: 'simulate', sensor: 'tap', value: 1 });
$('hold').onclick = () => action({ action: 'simulate', sensor: 'hold', value: 1 });
function useProfile(profile) {
  if (
    ![1, 2].includes(profile.schema) ||
    !profile.config ||
    Object.keys(profile.config).some((key) => !keys.includes(key))
  )
    throw Error('Expected a schema 1 or 2 helmet profile');
  fill({
    ...profile.config,
    ...(profile.schema === 1 ? { boop_mode: 0, boop_duration_ms: 6000 } : {}),
  });
  brightnessDirty = Object.prototype.hasOwnProperty.call(profile.config, 'brightness');
  $('profile-name').value = typeof profile.name === 'string' ? profile.name : 'Imported profile';
  pendingEdits++;
  message('Profile ready. Apply settings to preview it.');
}
$('use-profile').onclick = () => {
  try {
    useProfile(presets[Number($('profiles').value)]);
  } catch (error) {
    message(error.message, true);
  }
};
$('export').onclick = () => {
  try {
    const profile = {
        schema: 2,
        name: $('profile-name').value.trim() || 'My helmet',
        config: collect(),
      },
      a = document.createElement('a'),
      url = URL.createObjectURL(
        new Blob([JSON.stringify(profile, null, 2) + '\n'], { type: 'application/json' }),
      );
    a.href = url;
    a.download = profile.name.replace(/[^a-z0-9_-]/gi, '-') + '.json';
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    message('Profile downloaded.');
  } catch (error) {
    message(error.message, true);
  }
};
$('import').onclick = () => $('profile-file').click();
$('profile-file').onchange = async () => {
  try {
    const file = $('profile-file').files[0];
    if (!file) return;
    if (file.size > 4096) throw Error('Profile is too large');
    useProfile(JSON.parse(await file.text()));
  } catch (error) {
    message(error.message, true);
  } finally {
    $('profile-file').value = '';
  }
};
async function poll() {
  if (!busy) {
    const version = actionVersion;
    try {
      const value = await api('/api/state');
      if (version === actionVersion) update(value);
    } catch (error) {
      if (version === actionVersion) {
        renderConnection('Disconnected', false);
        if (!acknowledgedWithoutRefresh) message(error.message, true);
      }
    }
  }
  setTimeout(poll, 100);
}
api('/api/profiles')
  .then((value) => {
    presets = value;
    for (let i = 0; i < value.length; i++) {
      const option = document.createElement('option');
      option.value = i;
      option.textContent = value[i].name;
      $('profiles').append(option);
    }
  })
  .catch((error) => message(error.message, true));
poll();
