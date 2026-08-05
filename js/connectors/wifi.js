import { Connector } from './base.js';

/**
 * Real WiFi scanning, via the desktop shell.
 *
 * This is the one capability the web platform genuinely cannot provide (see the
 * README), so it is delegated to the host OS: nmcli on Linux, system_profiler
 * on macOS, netsh on Windows. In a plain browser the bridge is absent and the
 * connector reports itself unavailable rather than pretending.
 *
 * Each BSSID is its own contact, so a multi-radio AP shows every radio and you
 * can watch one band's signal move independently of the other.
 */
export class WifiScanConnector extends Connector {
  static id = 'wifi';
  static name = 'WiFi Scan';
  static icon = '📡';
  static kind = 'net';
  static description =
    'Nearby access points with SSID, BSSID, signal, channel, band and security — through the host OS, not the browser.';
  static action = 'Scan';

  /** How often to re-scan. Adapters rate-limit well below this. */
  static INTERVAL_MS = 15000;

  support() {
    const bridge = globalThis.aegisNative?.wifi;
    if (!bridge) {
      return { level: 'flag', note: 'desktop shell required — run `npm start`' };
    }
    return { level: 'ok', note: 'native backend attached' };
  }

  async start() {
    const bridge = globalThis.aegisNative?.wifi;
    if (!bridge) {
      throw new Error('no native bridge — run the desktop shell with `npm start`');
    }

    const backend = await bridge.backend();
    this.log('info', `backend: ${backend.tool} on ${backend.platform} ${backend.release}`);
    if (backend.note) this.log('info', backend.note);

    // The first scan is awaited so a denied permission surfaces as a failed
    // start() rather than a connector that looks enabled but never reports.
    await this.sweep(bridge, true);
    this.every(WifiScanConnector.INTERVAL_MS, () => this.sweep(bridge, false));

    this.active = true;
  }

  async sweep(bridge, isFirst) {
    let res;
    try {
      res = await bridge.scan();
    } catch (err) {
      if (isFirst) throw err;
      this.log('err', `scan failed: ${err.message}`);
      return;
    }

    if (!res.ok) {
      const detail = res.hint ? `${res.message} — ${res.hint}` : res.message;
      if (isFirst) throw new Error(detail);
      this.log('warn', detail);
      return;
    }

    const seen = new Set();
    for (const net of res.networks) {
      const key = net.bssid ?? net.ssid ?? 'unknown';
      if (seen.has(key)) continue;               // same AP twice in one sweep
      seen.add(key);

      this.emit({
        id: `${WifiScanConnector.id}:${key}`,
        name: net.ssid ?? '(hidden network)',
        identifier: net.bssid ?? '(BSSID withheld by OS)',
        signal: net.signal,
        detail: [
          net.connected ? '◆ CONNECTED' : null,
          net.band,
          net.channel != null ? `ch ${net.channel}` : null,
          net.phy,
          net.security,
          net.quality != null ? `${net.quality}%` : null,
        ].filter(Boolean).join(' · '),
      });
    }

    if (isFirst) {
      this.log('ok', `${res.networks.length} access point(s) via ${res.tool}`);
    }
  }
}
