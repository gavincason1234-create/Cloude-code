import { Connector, isSecure } from './base.js';

/**
 * WebUSB.
 *
 * `getDevices()` returns only devices this origin was already granted, so it
 * runs without a prompt; `requestDevice()` opens the browser chooser for a new
 * grant. Both are strictly opt-in per device — there is no way to enumerate the
 * bus at large.
 */
export class UsbConnector extends Connector {
  static id = 'usb';
  static name = 'USB Devices';
  static icon = '🔌';
  static kind = 'usb';
  static description =
    'Lists USB devices you have granted this page, with vendor/product IDs, class codes, serial and configuration count.';
  static action = 'Grant';

  support() {
    if (!('usb' in navigator)) return { level: 'no', note: 'WebUSB unavailable' };
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const publish = (d) => {
      const detail = [
        `VID 0x${hex(d.vendorId)}`,
        `PID 0x${hex(d.productId)}`,
        `class ${d.deviceClass ?? '?'}/${d.deviceSubclass ?? '?'}`,
        `USB ${d.usbVersionMajor ?? '?'}.${d.usbVersionMinor ?? 0}`,
        d.serialNumber ? `sn ${d.serialNumber}` : null,
        `${d.configurations?.length ?? 0} config(s)`,
      ].filter(Boolean);

      this.emit({
        id: `${UsbConnector.id}:${hex(d.vendorId)}:${hex(d.productId)}:${d.serialNumber ?? ''}`,
        name: d.productName || `USB ${hex(d.vendorId)}:${hex(d.productId)}`,
        identifier: d.manufacturerName || '(no manufacturer string)',
        signal: null,
        detail: detail.join(' · '),
      });
    };

    for (const d of await navigator.usb.getDevices()) publish(d);

    this.listen(navigator.usb, 'connect', (e) => {
      publish(e.device);
      this.log('ok', `attached ${e.device.productName ?? 'device'}`);
    });
    this.listen(navigator.usb, 'disconnect', (e) => {
      this.log('warn', `detached ${e.device.productName ?? 'device'}`);
    });

    const device = await navigator.usb.requestDevice({ filters: [] });
    publish(device);

    this.active = true;
    this.log('ok', `granted ${device.productName ?? 'USB device'}`);
  }
}

/**
 * WebHID — gamepads, keyboards, sensors, custom peripherals.
 */
export class HidConnector extends Connector {
  static id = 'hid';
  static name = 'HID Devices';
  static icon = '🎛️';
  static kind = 'usb';
  static description =
    'Human-interface devices you have granted, including usage page, usage ID and report collection counts.';
  static action = 'Grant';

  support() {
    if (!('hid' in navigator)) return { level: 'no', note: 'WebHID unavailable' };
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const publish = (d) => {
      const collections = d.collections ?? [];
      const top = collections[0];
      const inputs = collections.reduce((n, c) => n + (c.inputReports?.length ?? 0), 0);
      const outputs = collections.reduce((n, c) => n + (c.outputReports?.length ?? 0), 0);

      this.emit({
        id: `${HidConnector.id}:${hex(d.vendorId)}:${hex(d.productId)}`,
        name: d.productName || `HID ${hex(d.vendorId)}:${hex(d.productId)}`,
        identifier: `VID 0x${hex(d.vendorId)} PID 0x${hex(d.productId)}`,
        signal: null,
        detail: [
          top ? `usage 0x${hex(top.usagePage)}:0x${hex(top.usage)} (${usageLabel(top.usagePage)})` : 'no collections',
          `${inputs} in / ${outputs} out reports`,
          d.opened ? 'open' : 'closed',
        ].join(' · '),
      });
    };

    for (const d of await navigator.hid.getDevices()) publish(d);

    this.listen(navigator.hid, 'connect', (e) => publish(e.device));

    const devices = await navigator.hid.requestDevice({ filters: [] });
    devices.forEach(publish);

    this.active = true;
    this.log('ok', `granted ${devices.length} HID device(s)`);
  }
}

/**
 * Web Serial — USB-serial bridges, dev boards, radios with a UART.
 * Ports are reported, never opened: no data is read from or written to them.
 */
export class SerialConnector extends Connector {
  static id = 'serial';
  static name = 'Serial Ports';
  static icon = '⌁';
  static kind = 'usb';
  static description =
    'Serial ports you have granted, identified by their USB vendor/product IDs. Ports are listed only — never opened or read.';
  static action = 'Grant';

  support() {
    if (!('serial' in navigator)) return { level: 'no', note: 'Web Serial unavailable' };
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    let index = 0;
    const publish = (port) => {
      const info = port.getInfo?.() ?? {};
      const vid = info.usbVendorId;
      const pid = info.usbProductId;
      this.emit({
        id: `${SerialConnector.id}:${vid ?? 'x'}:${pid ?? 'x'}:${++index}`,
        name: vid != null ? `Serial ${hex(vid)}:${hex(pid)}` : `Serial port ${index}`,
        identifier: vid != null ? `VID 0x${hex(vid)} PID 0x${hex(pid)}` : 'platform port',
        signal: null,
        detail: 'granted · not opened',
      });
    };

    for (const p of await navigator.serial.getPorts()) publish(p);

    const port = await navigator.serial.requestPort();
    publish(port);

    this.active = true;
    this.log('ok', 'serial port granted (left closed)');
  }
}

/**
 * Media device enumeration.
 *
 * Labels are blank until the user has granted camera/microphone access at least
 * once — that is the platform's anti-fingerprinting rule, and this connector
 * respects it rather than requesting a stream to unlock the names.
 */
export class MediaConnector extends Connector {
  static id = 'media';
  static name = 'Media Devices';
  static icon = '🎥';
  static kind = 'usb';
  static description =
    'Cameras, microphones and speakers visible to the browser. Labels stay hidden until you have granted media access.';
  static action = 'Enumerate';

  support() {
    if (!navigator.mediaDevices?.enumerateDevices) {
      return { level: 'no', note: 'enumerateDevices unavailable' };
    }
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const publish = async () => {
      const devices = await navigator.mediaDevices.enumerateDevices();
      let unlabelled = 0;
      devices.forEach((d, i) => {
        if (!d.label) unlabelled++;
        this.emit({
          id: `${MediaConnector.id}:${d.deviceId || i}:${d.kind}`,
          name: d.label || `${kindLabel(d.kind)} ${i + 1}`,
          identifier: d.deviceId ? `${d.deviceId.slice(0, 16)}…` : '(no id)',
          signal: null,
          detail: [kindLabel(d.kind), d.groupId ? `group ${d.groupId.slice(0, 8)}` : null,
            d.label ? 'labelled' : 'label withheld'].filter(Boolean).join(' · '),
        });
      });
      if (unlabelled) {
        this.log('info', `${unlabelled} device label(s) withheld until media permission is granted`);
      }
    };

    this.listen(navigator.mediaDevices, 'devicechange', () => {
      this.log('info', 'device list changed');
      publish();
    });

    await publish();
    this.active = true;
    this.log('ok', 'media enumeration complete');
  }
}

const hex = (n) => (n ?? 0).toString(16).padStart(4, '0');

const kindLabel = (kind) => ({
  videoinput: 'Camera',
  audioinput: 'Microphone',
  audiooutput: 'Speaker',
}[kind] ?? kind);

/** HID usage pages worth naming. */
const usageLabel = (page) => ({
  0x01: 'generic desktop',
  0x02: 'simulation',
  0x03: 'VR',
  0x04: 'sport',
  0x05: 'game',
  0x07: 'keyboard',
  0x08: 'LED',
  0x09: 'button',
  0x0c: 'consumer',
  0x0d: 'digitizer',
  0x20: 'sensor',
}[page] ?? 'vendor-defined');
