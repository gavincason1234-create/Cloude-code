import { app, BrowserWindow, ipcMain, protocol, net, shell } from 'electron';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { scanWifi, describeBackend, ScanError } from './scanner/index.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');
const SCHEME = 'aegis';

/**
 * The renderer is served from a custom scheme rather than file://, for two
 * reasons: file:// blocks ES modules under CORS, and it is not a secure
 * context — which would disable Web Bluetooth, WebUSB, WebHID and Web Serial,
 * i.e. most of the app. Registering the scheme as standard + secure restores
 * both without weakening webSecurity.
 */
protocol.registerSchemesAsPrivileged([{
  scheme: SCHEME,
  privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true },
}]);

let mainWindow = null;

/** Pending device-chooser callback, keyed by nothing — only one can be open. */
let pendingPick = null;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 960,
    minWidth: 380,
    backgroundColor: '#05070d',
    title: 'AEGIS',
    webPreferences: {
      preload: path.join(HERE, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  mainWindow.loadURL(`${SCHEME}://app/index.html`);
  wireDeviceChoosers(mainWindow);

  // External links open in the real browser, never in the app frame.
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });

  mainWindow.on('closed', () => { mainWindow = null; });
}

/**
 * Electron has no built-in chooser for Bluetooth/USB/HID/Serial — without a
 * handler these requests hang or fail outright. Each event carries the same
 * (event, deviceList, callback) shape, so they all feed one renderer overlay.
 * Cancelling calls back with an empty id, which surfaces in the page as the
 * ordinary NotFoundError a browser chooser produces.
 */
function wireDeviceChoosers(win) {
  const { webContents, webContents: { session } } = win;

  const handler = (kind, normalise) => (event, deviceList, callback) => {
    event.preventDefault();
    finishPick('');                                   // supersede any open pick
    pendingPick = callback;
    webContents.send('picker:show', { kind, devices: deviceList.map(normalise) });
  };

  webContents.on('select-bluetooth-device', handler('Bluetooth', (d) => ({
    id: d.deviceId,
    name: d.deviceName || '(unnamed device)',
    detail: d.deviceId,
  })));

  webContents.on('select-usb-device', (event, details, callback) => {
    handler('USB', (d) => ({
      id: d.deviceId,
      name: d.productName || `USB ${hex(d.vendorId)}:${hex(d.productId)}`,
      detail: `VID 0x${hex(d.vendorId)} PID 0x${hex(d.productId)}${d.serialNumber ? ` · sn ${d.serialNumber}` : ''}`,
    }))(event, details.deviceList, callback);
  });

  webContents.on('select-hid-device', (event, details, callback) => {
    handler('HID', (d) => ({
      id: d.deviceId,
      name: d.name || `HID ${hex(d.vendorId)}:${hex(d.productId)}`,
      detail: `VID 0x${hex(d.vendorId)} PID 0x${hex(d.productId)}`,
    }))(event, details.deviceList, callback);
  });

  webContents.on('select-serial-port', (event, portList, _webContents, callback) => {
    handler('Serial', (p) => ({
      id: p.portId,
      name: p.displayName || p.portName || p.portId,
      detail: p.vendorId ? `VID 0x${p.vendorId} PID 0x${p.productId}` : p.portName ?? '',
    }))(event, portList, callback);
  });

  // A device the user just picked through our own overlay is, by definition,
  // one they consented to. Nothing is granted that did not go through it.
  session.setDevicePermissionHandler(() => true);
  session.setPermissionCheckHandler((_wc, permission) =>
    ['usb', 'hid', 'serial', 'clipboard-sanitized-write'].includes(permission));

  // Everything else still has to ask.
  session.setPermissionRequestHandler((_wc, permission, callback) => {
    callback(['geolocation', 'media', 'notifications'].includes(permission));
  });
}

function finishPick(id) {
  if (!pendingPick) return;
  const callback = pendingPick;
  pendingPick = null;
  try { callback(id); } catch { /* window may have closed mid-pick */ }
}

const hex = (n) => (n ?? 0).toString(16).padStart(4, '0');

/* ------------------------------------------------------------------- IPC --- */

ipcMain.handle('wifi:backend', () => describeBackend());

ipcMain.handle('wifi:scan', async () => {
  try {
    return { ok: true, ...(await scanWifi()) };
  } catch (err) {
    if (err instanceof ScanError) {
      return { ok: false, code: err.code, message: err.message, hint: err.hint };
    }
    return { ok: false, code: 'UNKNOWN', message: err?.message ?? String(err), hint: null };
  }
});

ipcMain.on('picker:choose', (_event, id) => finishPick(typeof id === 'string' ? id : ''));

/* ------------------------------------------------------------------ boot --- */

app.whenReady().then(() => {
  protocol.handle(SCHEME, (request) => {
    const { pathname } = new URL(request.url);
    // Resolve inside ROOT and reject anything that escapes it.
    const target = path.join(ROOT, decodeURIComponent(pathname));
    if (!target.startsWith(ROOT + path.sep)) {
      return new Response('Forbidden', { status: 403 });
    }
    return net.fetch(pathToFileURL(target).toString());
  });

  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
