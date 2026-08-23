import { Connector } from './base.js';

/**
 * Live link telemetry.
 *
 * Worth stating plainly: no browser can enumerate nearby WiFi networks. There
 * is no SSID scanning API on the web platform — it was rejected as a
 * fingerprinting and location-inference vector. What the platform does expose
 * is the properties of the link *this* device is already on, which is what this
 * connector reports: bearer type, effective class, throughput estimate, RTT,
 * and every transition between them.
 */
export class NetworkLinkConnector extends Connector {
  static id = 'net-link';
  static name = 'Network Link';
  static icon = '🌐';
  static kind = 'net';
  static description =
    'Bearer type, effective class, downlink estimate and round-trip time for the current connection, updated on every change.';
  static action = 'Monitor';

  support() {
    if (!('connection' in navigator)) {
      return { level: 'flag', note: 'no Network Information API — online state only' };
    }
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const conn = navigator.connection ?? navigator.mozConnection ?? navigator.webkitConnection;

    const publish = (reason) => {
      const detail = [];
      let signal = null;

      if (conn) {
        if (conn.effectiveType) detail.push(`class ${conn.effectiveType}`);
        if (conn.downlink != null) detail.push(`${conn.downlink} Mb/s down`);
        if (conn.downlinkMax != null && Number.isFinite(conn.downlinkMax)) {
          detail.push(`max ${conn.downlinkMax} Mb/s`);
        }
        if (conn.rtt != null) detail.push(`${conn.rtt} ms rtt`);
        if (conn.saveData) detail.push('data-saver on');
        signal = qualityToRssi(conn);
      }
      detail.push(navigator.onLine ? 'online' : 'OFFLINE');

      this.emit({
        id: `${NetworkLinkConnector.id}:primary`,
        name: bearerLabel(conn),
        identifier: conn?.type ?? 'unknown bearer',
        signal,
        detail: detail.join(' · '),
      });

      if (reason) this.log('info', `link ${reason}: ${detail.join(' · ')}`);
    };

    if (conn) this.listen(conn, 'change', () => publish('changed'));
    this.listen(window, 'online', () => publish('online'));
    this.listen(window, 'offline', () => publish('offline'));
    this.every(5000, () => publish(null));

    publish('acquired');
    this.active = true;
    this.log('ok', 'link monitor running');
  }
}

/**
 * Local interface enumeration via WebRTC host candidates.
 *
 * Deliberately configured with zero ICE servers, so candidate gathering never
 * leaves the machine — no STUN, no TURN, no packets. Modern browsers replace
 * private addresses with per-origin mDNS hostnames, so this counts and
 * fingerprints your interfaces without exposing routable addresses.
 */
export class LocalInterfaceConnector extends Connector {
  static id = 'net-iface';
  static name = 'Local Interfaces';
  static icon = '🧭';
  static kind = 'net';
  static description =
    'Enumerates this machine\'s network interfaces from WebRTC host candidates. No STUN/TURN servers — nothing is sent off-device.';
  static action = 'Enumerate';

  support() {
    if (typeof RTCPeerConnection !== 'function') {
      return { level: 'no', note: 'WebRTC unavailable' };
    }
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const pc = new RTCPeerConnection({ iceServers: [] });
    this.onStop(() => pc.close());

    const seen = new Set();
    let index = 0;

    pc.addEventListener('icecandidate', (e) => {
      if (!e.candidate?.candidate) return;
      const c = e.candidate;
      const address = c.address || parseAddress(c.candidate);
      if (!address || seen.has(address)) return;
      seen.add(address);

      const isMdns = address.endsWith('.local');
      this.emit({
        id: `${LocalInterfaceConnector.id}:${address}`,
        name: `Interface ${String(++index).padStart(2, '0')}`,
        identifier: address,
        signal: null,
        detail: [
          c.protocol ? c.protocol.toUpperCase() : 'udp',
          `type ${c.type ?? 'host'}`,
          isMdns ? 'mDNS-masked' : 'raw address',
        ].join(' · '),
      });
    });

    pc.createDataChannel('probe');
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);

    this.active = true;
    this.log('ok', 'gathering host candidates (no ICE servers configured)');

    // Gathering settles quickly with no servers to contact.
    setTimeout(() => {
      this.log('info', `${seen.size} interface${seen.size === 1 ? '' : 's'} found`);
    }, 1200);
  }
}

function bearerLabel(conn) {
  const type = conn?.type;
  const map = {
    wifi: 'WiFi Link',
    ethernet: 'Ethernet Link',
    cellular: 'Cellular Link',
    bluetooth: 'Bluetooth Tether',
    wimax: 'WiMAX Link',
    none: 'No Link',
    other: 'Network Link',
    unknown: 'Network Link',
  };
  if (type && map[type]) return map[type];
  // Firefox/Safari omit `type`; fall back to the effective class.
  return conn?.effectiveType ? `Network Link (${conn.effectiveType})` : 'Network Link';
}

/**
 * Map link quality onto the same dBm scale the radar uses for BLE, so a good
 * connection plots near the centre alongside strong Bluetooth contacts.
 */
function qualityToRssi(conn) {
  const byClass = { 'slow-2g': -95, '2g': -88, '3g': -72, '4g': -50 };
  let base = byClass[conn.effectiveType] ?? -70;
  if (conn.rtt != null) base -= Math.min(20, conn.rtt / 25);
  return Math.round(Math.max(-100, Math.min(-30, base)));
}

function parseAddress(candidate) {
  const parts = candidate.split(' ');
  return parts[4] ?? null;
}
