import { Connector, isSecure } from './base.js';

/**
 * Passive BLE advertisement scanning.
 *
 * `requestLEScan` is the only web API that observes nearby Bluetooth devices
 * without the user hand-picking each one, and it is deliberately hard to reach:
 * Chromium only, secure context, and gated behind
 * chrome://flags#enable-experimental-web-platform-features. The permission
 * prompt is still shown and the scan stops when the tab is closed.
 */
export class BleScanConnector extends Connector {
  static id = 'ble-scan';
  static name = 'Bluetooth LE Scan';
  static icon = '📶';
  static kind = 'ble';
  static description =
    'Listens for BLE advertisements — name, RSSI, TX power, manufacturer and service data. Needs an experimental Chromium flag.';
  static action = 'Scan';

  support() {
    if (!('bluetooth' in navigator)) {
      return { level: 'no', note: 'Web Bluetooth unavailable' };
    }
    if (!isSecure()) {
      return { level: 'no', note: 'needs https or localhost' };
    }
    if (typeof navigator.bluetooth.requestLEScan !== 'function') {
      return { level: 'flag', note: 'enable experimental web platform features' };
    }
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const available = await navigator.bluetooth.getAvailability?.();
    if (available === false) {
      this.log('warn', 'no Bluetooth adapter reported by the OS');
    }

    const onAdvert = (event) => {
      const d = event.device;
      const id = d.id || d.name || 'unknown';
      const detail = [];

      if (event.txPower != null) detail.push(`tx ${event.txPower}dBm`);
      if (event.appearance != null) detail.push(`appearance ${event.appearance}`);

      const uuids = event.uuids ?? [];
      if (uuids.length) detail.push(`svc ${uuids.slice(0, 2).join(', ')}`);

      for (const [company] of event.manufacturerData ?? new Map()) {
        detail.push(`mfr 0x${company.toString(16).padStart(4, '0')}${vendorName(company)}`);
        break;
      }

      this.emit({
        id: `${BleScanConnector.id}:${id}`,
        name: d.name || event.name || '(unnamed device)',
        identifier: d.id ?? '—',
        signal: event.rssi ?? null,
        detail: detail.join(' · ') || 'advertisement',
      });
    };

    this.listen(navigator.bluetooth, 'advertisementreceived', onAdvert);

    const scan = await navigator.bluetooth.requestLEScan({
      acceptAllAdvertisements: true,
      keepRepeatedDevices: true,
    });
    this.onStop(() => { try { scan.stop(); } catch { /* already stopped */ } });

    this.active = true;
    this.log('ok', 'advertisement scan running');
  }
}

/**
 * User-picked Bluetooth device, with an optional GATT read.
 *
 * This is the widely-supported path: the browser shows its own chooser, the
 * page only ever sees the single device the user selected.
 */
export class BleDeviceConnector extends Connector {
  static id = 'ble-device';
  static name = 'Bluetooth Device Pair';
  static icon = '🔗';
  static kind = 'ble';
  static description =
    'Opens the browser chooser, then reads the GATT Device Information service (manufacturer, model, firmware) from the device you select.';
  static action = 'Pick';

  support() {
    if (!('bluetooth' in navigator)) return { level: 'no', note: 'Web Bluetooth unavailable' };
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: ['device_information', 'battery_service'],
    });

    const contactId = `${BleDeviceConnector.id}:${device.id}`;
    const detail = [];

    this.emit({
      id: contactId,
      name: device.name || '(unnamed device)',
      identifier: device.id,
      signal: null,
      detail: 'paired — reading GATT…',
    });

    this.listen(device, 'gattserverdisconnected', () => {
      this.log('warn', `${device.name || device.id} disconnected`);
    });

    try {
      const server = await device.gatt.connect();
      this.onStop(() => { try { device.gatt.disconnect(); } catch { /* noop */ } });

      const info = await readDeviceInformation(server);
      detail.push(...info);

      const battery = await readBatteryLevel(server);
      if (battery != null) detail.push(`battery ${battery}%`);
    } catch (err) {
      detail.push(`GATT unavailable (${err.name})`);
    }

    this.emit({
      id: contactId,
      name: device.name || '(unnamed device)',
      identifier: device.id,
      signal: null,
      detail: detail.join(' · ') || 'paired',
    });

    this.active = true;
    this.log('ok', `paired with ${device.name || device.id}`);
  }
}

const DEVICE_INFO_CHARS = [
  ['manufacturer_name_string', 'mfr'],
  ['model_number_string', 'model'],
  ['firmware_revision_string', 'fw'],
  ['hardware_revision_string', 'hw'],
  ['serial_number_string', 'sn'],
];

async function readDeviceInformation(server) {
  const out = [];
  let service;
  try {
    service = await server.getPrimaryService('device_information');
  } catch {
    return out;
  }
  const decoder = new TextDecoder();
  for (const [uuid, label] of DEVICE_INFO_CHARS) {
    try {
      const ch = await service.getCharacteristic(uuid);
      const value = await ch.readValue();
      const text = decoder.decode(value).trim();
      if (text) out.push(`${label} ${text}`);
    } catch {
      // Characteristic is optional — a device may expose only some of these.
    }
  }
  return out;
}

async function readBatteryLevel(server) {
  try {
    const service = await server.getPrimaryService('battery_service');
    const ch = await service.getCharacteristic('battery_level');
    const value = await ch.readValue();
    return value.getUint8(0);
  } catch {
    return null;
  }
}

/** A few Bluetooth SIG company identifiers worth naming inline. */
const VENDORS = new Map([
  [0x004c, 'Apple'], [0x0006, 'Microsoft'], [0x00e0, 'Google'],
  [0x0075, 'Samsung'], [0x000f, 'Broadcom'], [0x0059, 'Nordic'],
  [0x0087, 'Garmin'], [0x0157, 'Huawei'], [0x038f, 'Xiaomi'],
  [0x0499, 'Ruuvi'], [0x0171, 'Amazon'], [0x02e5, 'Espressif'],
]);

function vendorName(company) {
  const name = VENDORS.get(company);
  return name ? ` (${name})` : '';
}
