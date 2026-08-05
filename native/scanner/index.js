import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import os from 'node:os';

import { parseNmcli, parseNetsh, parseSystemProfiler } from './parsers.js';

const run = promisify(execFile);

/** Scans can be slow on a busy adapter; well under Electron's IPC patience. */
const TIMEOUT_MS = 20000;

export class ScanError extends Error {
  constructor(code, message, hint) {
    super(message);
    this.name = 'ScanError';
    this.code = code;
    this.hint = hint ?? null;
  }
}

/**
 * Run a real WiFi scan using the platform's own tooling.
 *
 * @returns {Promise<{platform:string, tool:string, networks:Array, scannedAt:string}>}
 */
export async function scanWifi() {
  switch (process.platform) {
    case 'linux': return scanLinux();
    case 'darwin': return scanDarwin();
    case 'win32': return scanWindows();
    default:
      throw new ScanError(
        'UNSUPPORTED_PLATFORM',
        `No WiFi scan backend for ${process.platform}.`,
      );
  }
}

/** What the UI can show before anyone presses anything. */
export function describeBackend() {
  const backends = {
    linux: { tool: 'nmcli', note: 'NetworkManager must be running.' },
    darwin: {
      tool: 'system_profiler SPAirPortDataType',
      note: 'macOS 14.4+ removed `airport -s`; Location Services permission is required for SSIDs, and BSSIDs are not exposed at all.',
    },
    win32: {
      tool: 'netsh wlan show networks',
      note: 'Windows requires precise-location consent for BSSID data; without it the call is denied.',
    },
  };
  return {
    platform: process.platform,
    release: os.release(),
    ...(backends[process.platform] ?? { tool: null, note: 'Unsupported platform.' }),
  };
}

/* ------------------------------------------------------------------ Linux --- */

async function scanLinux() {
  // A rescan is best-effort: NetworkManager rate-limits it, and a fresh cached
  // list is better than failing the whole call.
  try {
    await run('nmcli', ['device', 'wifi', 'rescan'], { timeout: TIMEOUT_MS });
  } catch {
    /* rate-limited or already scanning — fall through to the cached list */
  }

  let stdout;
  try {
    ({ stdout } = await run(
      'nmcli',
      ['-t', '-f', 'IN-USE,SSID,BSSID,SIGNAL,CHAN,FREQ,SECURITY', 'device', 'wifi', 'list'],
      { timeout: TIMEOUT_MS },
    ));
  } catch (err) {
    if (err.code === 'ENOENT') {
      throw new ScanError('NO_TOOL', 'nmcli is not installed.', 'Install NetworkManager, or use `iw dev <iface> scan` as root.');
    }
    const text = `${err.stderr ?? ''}${err.stdout ?? ''}`;
    if (/NetworkManager is not running/i.test(text)) {
      throw new ScanError('SERVICE_DOWN', 'NetworkManager is not running.', 'systemctl start NetworkManager');
    }
    throw new ScanError('SCAN_FAILED', text.trim() || err.message);
  }

  const networks = parseNmcli(stdout);
  if (!networks.length) {
    throw new ScanError('NO_ADAPTER', 'No wireless networks returned.', 'This machine may have no WiFi adapter, or it is soft-blocked (check `rfkill list`).');
  }
  return result('nmcli', networks);
}

/* ------------------------------------------------------------------ macOS --- */

async function scanDarwin() {
  let stdout;
  try {
    ({ stdout } = await run(
      '/usr/sbin/system_profiler',
      ['SPAirPortDataType', '-json'],
      { timeout: TIMEOUT_MS, maxBuffer: 8 * 1024 * 1024 },
    ));
  } catch (err) {
    throw new ScanError('SCAN_FAILED', `${err.stderr ?? err.message}`.trim());
  }

  let networks;
  try {
    networks = parseSystemProfiler(stdout);
  } catch {
    throw new ScanError('PARSE_FAILED', 'system_profiler returned output this build cannot read.');
  }

  if (!networks.length) {
    throw new ScanError(
      'PERMISSION_OR_EMPTY',
      'macOS returned no networks.',
      'Grant Location Services to this app in System Settings → Privacy & Security → Location Services. Since Sonoma, WiFi names are withheld without it.',
    );
  }
  return result('system_profiler', networks);
}

/* ---------------------------------------------------------------- Windows --- */

async function scanWindows() {
  let stdout;
  try {
    ({ stdout } = await run('netsh', ['wlan', 'show', 'networks', 'mode=bssid'], { timeout: TIMEOUT_MS }));
  } catch (err) {
    throw new ScanError('SCAN_FAILED', `${err.stderr ?? err.message}`.trim());
  }

  if (/access is denied|ERROR_ACCESS_DENIED/i.test(stdout)) {
    throw new ScanError(
      'PERMISSION_DENIED',
      'Windows denied access to WiFi scan results.',
      'Enable precise location for this app in Settings → Privacy & security → Location. Since the fall 2024 release, BSSID data requires that consent.',
    );
  }
  if (/wireless.*not (running|available)|no wireless interface/i.test(stdout)) {
    throw new ScanError('NO_ADAPTER', 'The WLAN AutoConfig service is not running or there is no wireless interface.');
  }

  const networks = parseNetsh(stdout);
  if (!networks.length) {
    throw new ScanError('NO_NETWORKS', 'netsh reported no visible networks.');
  }
  return result('netsh', networks);
}

/* ---------------------------------------------------------------- shared --- */

function result(tool, networks) {
  return {
    platform: process.platform,
    tool,
    scannedAt: new Date().toISOString(),
    networks: networks.sort((a, b) => (b.signal ?? -999) - (a.signal ?? -999)),
  };
}
