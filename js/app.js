import { Text3D } from './text3d.js';
import { Radar } from './radar.js';
import { ContactStore } from './store.js';
import { CONNECTORS } from './connectors/index.js';
import { installDevicePicker } from './picker.js';

const $ = (id) => document.getElementById(id);

const el = {
  logo: $('logo3d'),
  radar: $('radar'),
  readout: $('radar-readout'),
  connectorList: $('connector-list'),
  contactsBody: $('contacts-body'),
  contactsEmpty: $('contacts-empty'),
  log: $('log'),
  filter: $('filter'),
  statContacts: $('stat-contacts'),
  statConnectors: $('stat-connectors'),
  statUptime: $('stat-uptime'),
  statState: $('stat-state'),
  consent: $('consent-banner'),
};

const store = new ContactStore();
const instances = new Map(); // connector id -> { Ctor, instance, node }

let startedAt = null;
let renderQueued = false;

/* ------------------------------------------------------------------ log --- */

function log(level, message) {
  const li = document.createElement('li');
  const time = document.createElement('time');
  time.textContent = new Date().toLocaleTimeString([], { hour12: false });
  const lvl = document.createElement('span');
  lvl.className = `lvl lvl-${level}`;
  lvl.textContent = level.toUpperCase();
  const msg = document.createElement('span');
  msg.className = 'msg';
  msg.textContent = message;
  li.append(time, lvl, msg);
  el.log.prepend(li);
  while (el.log.children.length > 200) el.log.lastElementChild.remove();
}

const bus = {
  emit: (contact) => store.upsert(contact),
  drop: (id) => store.drop(id),
  log,
};

/* ------------------------------------------------------------- 3D title --- */

const title = new Text3D(el.logo, 'AEGIS', {
  depth: 24,
  fontStack: '900 92px "Inter", ui-sans-serif, system-ui, sans-serif',
});
title.start();

/* ----------------------------------------------------------------- radar --- */

const radar = new Radar(el.radar, {
  onHover: (contact, pointer) => {
    if (!contact || !pointer) {
      el.readout.hidden = true;
      return;
    }
    el.readout.hidden = false;
    el.readout.style.left = `${pointer.cx}px`;
    el.readout.style.top = `${pointer.cy}px`;
    el.readout.innerHTML = '';
    for (const [k, v] of [
      ['', contact.name],
      ['source', contact.source],
      ['id', contact.identifier],
      ['signal', contact.signal != null ? `${contact.signal} dBm` : 'n/a'],
      ['seen', `${contact.hits}×`],
    ]) {
      const line = document.createElement('div');
      if (k) {
        const b = document.createElement('b');
        b.textContent = `${k} `;
        line.append(b);
      }
      line.append(document.createTextNode(String(v)));
      el.readout.append(line);
    }
  },
});
radar.start();

/* ------------------------------------------------------------ connectors --- */

function buildConnectorList() {
  const frag = document.createDocumentFragment();
  let supported = 0;

  for (const Ctor of CONNECTORS) {
    const instance = new Ctor(bus);
    const status = instance.support();
    if (status.level === 'ok') supported++;

    const li = document.createElement('li');
    li.className = 'connector';
    if (status.level === 'no') li.classList.add('is-unsupported');

    const icon = document.createElement('div');
    icon.className = 'connector-icon';
    icon.textContent = Ctor.icon;

    const body = document.createElement('div');
    const name = document.createElement('p');
    name.className = 'connector-name';
    name.textContent = Ctor.name;

    const badge = document.createElement('span');
    badge.className = `connector-badge badge-${status.level === 'ok' ? 'ok' : status.level === 'flag' ? 'flag' : 'no'}`;
    badge.textContent = status.level === 'ok' ? 'available' : status.level === 'flag' ? 'flag needed' : 'unavailable';
    name.append(badge);

    const desc = document.createElement('p');
    desc.className = 'connector-desc';
    desc.textContent = `${Ctor.description} — ${status.note}`;

    body.append(name, desc);

    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'connector-action';
    button.textContent = Ctor.action;
    button.disabled = status.level === 'no';
    button.addEventListener('click', () => toggleConnector(Ctor.id));

    li.append(icon, body, button);
    frag.append(li);

    instances.set(Ctor.id, { Ctor, instance, node: li, button });
  }

  el.connectorList.append(frag);
  el.statConnectors.innerHTML = `0<span class="dim">/${supported}</span>`;
  return supported;
}

async function toggleConnector(id) {
  const entry = instances.get(id);
  if (!entry) return;
  const { Ctor, instance, node, button } = entry;

  if (instance.active) {
    await instance.stop();
    store.dropSource(Ctor.id);
    node.classList.remove('is-active');
    button.classList.remove('is-stop');
    button.textContent = Ctor.action;
    log('info', `${Ctor.name} stopped`);
    refreshStats();
    scheduleRender();
    return;
  }

  button.disabled = true;
  const original = button.textContent;
  button.textContent = '…';

  try {
    await instance.start();
    node.classList.add('is-active');
    button.classList.add('is-stop');
    button.textContent = 'Stop';
    if (!startedAt) startedAt = Date.now();
  } catch (err) {
    button.textContent = original;
    node.classList.remove('is-active');
    log(err?.name === 'NotFoundError' ? 'warn' : 'err', describeError(Ctor, err));
  } finally {
    button.disabled = false;
    refreshStats();
  }
}

/**
 * Permission denial is an expected outcome here, not a crash — say so in plain
 * language instead of dumping a DOMException.
 */
function describeError(Ctor, err) {
  const name = err?.name ?? 'Error';
  const map = {
    NotFoundError: `${Ctor.name}: no device selected (chooser dismissed)`,
    NotAllowedError: `${Ctor.name}: permission denied — nothing was accessed`,
    SecurityError: `${Ctor.name}: blocked by the browser's security policy`,
    NotSupportedError: `${Ctor.name}: not supported on this platform`,
    NetworkError: `${Ctor.name}: device unreachable`,
    InvalidStateError: `${Ctor.name}: adapter unavailable or already in use`,
  };
  return map[name] ?? `${Ctor.name}: ${err?.message || name}`;
}

/* ------------------------------------------------------------- rendering --- */

function scheduleRender() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => {
    renderQueued = false;
    renderContacts();
  });
}

function renderContacts() {
  const rows = store.filtered(el.filter.value);
  radar.setContacts(store.list());

  el.contactsEmpty.hidden = rows.length > 0;
  el.contactsBody.replaceChildren(...rows.map(rowFor));
  el.statContacts.textContent = String(store.size);
}

function rowFor(c) {
  const tr = document.createElement('tr');
  if (Date.now() - c.firstSeen < 900) tr.className = 'is-new';

  tr.append(
    cell('cell-name', c.name),
    sourceCell(c),
    cell('cell-id', c.identifier),
    signalCell(c),
    cell('cell-detail', c.detail),
    cell('cell-seen', `${relative(c.lastSeen)} · ${c.hits}×`),
  );
  return tr;
}

function cell(className, text) {
  const td = document.createElement('td');
  td.className = className;
  td.textContent = text;
  return td;
}

function sourceCell(c) {
  const td = document.createElement('td');
  const span = document.createElement('span');
  span.className = `src src-${c.kind}`;
  span.textContent = c.source;
  td.append(span);
  return td;
}

function signalCell(c) {
  const td = document.createElement('td');
  if (c.signal == null) {
    td.className = 'cell-id';
    td.textContent = '—';
    return td;
  }
  const wrap = document.createElement('div');
  wrap.className = 'signal';
  const bar = document.createElement('div');
  bar.className = 'signal-bar';
  const fill = document.createElement('i');
  const pct = Math.round(Math.max(0, Math.min(1, (c.signal + 100) / 70)) * 100);
  fill.style.width = `${pct}%`;
  fill.style.background = pct > 60 ? 'var(--ok)' : pct > 30 ? 'var(--accent)' : 'var(--warn)';
  bar.append(fill);
  const val = document.createElement('span');
  val.className = 'signal-val';
  val.textContent = `${c.signal} dBm`;
  wrap.append(bar, val);
  td.append(wrap);
  return td;
}

function relative(ts) {
  const s = Math.round((Date.now() - ts) / 1000);
  if (s < 2) return 'now';
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  return `${Math.floor(s / 3600)}h ago`;
}

function refreshStats() {
  const active = [...instances.values()].filter((e) => e.instance.active).length;
  const supported = [...instances.values()].filter((e) => e.instance.support().level === 'ok').length;
  el.statConnectors.innerHTML = `${active}<span class="dim">/${supported}</span>`;
  el.statState.textContent = active ? 'SCANNING' : 'IDLE';
  el.statState.className = `stat-value ${active ? 'is-live' : 'is-idle'}`;
}

function tickUptime() {
  if (!startedAt) return;
  const s = Math.floor((Date.now() - startedAt) / 1000);
  const mm = String(Math.floor(s / 60)).padStart(2, '0');
  const ss = String(s % 60).padStart(2, '0');
  el.statUptime.textContent = `${mm}:${ss}`;
}

/* ----------------------------------------------------------------- wiring --- */

store.addEventListener('change', scheduleRender);
el.filter.addEventListener('input', scheduleRender);

$('btn-sweep').addEventListener('click', (e) => togglePressed(e.currentTarget, (on) => { radar.showSweep = on; }));
$('btn-labels').addEventListener('click', (e) => togglePressed(e.currentTarget, (on) => { radar.showLabels = on; }));
$('btn-trails').addEventListener('click', (e) => togglePressed(e.currentTarget, (on) => { radar.showTrails = on; }));

function togglePressed(button, apply) {
  const next = button.getAttribute('aria-pressed') !== 'true';
  button.setAttribute('aria-pressed', String(next));
  apply(next);
}

$('btn-probe').addEventListener('click', () => {
  log('info', '--- capability probe ---');
  for (const { Ctor, instance } of instances.values()) {
    const s = instance.support();
    log(s.level === 'ok' ? 'ok' : s.level === 'flag' ? 'warn' : 'err', `${Ctor.name}: ${s.note}`);
  }
});

$('btn-export').addEventListener('click', () => {
  const snapshot = store.snapshot({
    activeConnectors: [...instances.values()]
      .filter((e) => e.instance.active)
      .map((e) => e.Ctor.id),
    userAgent: navigator.userAgent,
  });
  const blob = new Blob([JSON.stringify(snapshot, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `aegis-scan-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
  a.click();
  URL.revokeObjectURL(url);
  log('ok', `exported ${snapshot.contactCount} contact(s)`);
});

$('btn-clear').addEventListener('click', () => {
  store.clear();
  log('info', 'contact list cleared');
});

$('btn-log-clear').addEventListener('click', () => el.log.replaceChildren());

$('consent-dismiss').addEventListener('click', () => { el.consent.hidden = true; });

/* ------------------------------------------------------------------ boot --- */

installDevicePicker(log);

const supportedCount = buildConnectorList();
refreshStats();
renderContacts();

setInterval(() => { tickUptime(); scheduleRender(); }, 1000);

const shell = globalThis.aegisNative ? 'desktop shell' : 'browser';
log('ok', `AEGIS ready — ${supportedCount}/${CONNECTORS.length} connectors available in this ${shell}`);
if (!window.isSecureContext) {
  log('warn', 'insecure context: most connectors need https:// or localhost');
}
if (globalThis.aegisNative) {
  log('info', 'native bridge attached — WiFi scanning runs through the host OS');
} else {
  log('info', 'no WiFi SSID scanning exists on the web platform — run `npm start` for the desktop shell');
}
