import { Connector, isSecure } from './base.js';

/**
 * Arduino UNO R4 2.4 GHz scanner, over Web Serial.
 *
 * This is the interesting one architecturally: the board does what the browser
 * is forbidden to do, and hands the results back over a port the user has
 * explicitly granted. No native shell involved — this connector works in a
 * plain browser, which means the browser build gets real RF scanning after all.
 *
 * The firmware in `firmware/aegis_scanner/` streams newline-delimited JSON.
 * Three record types become contacts:
 *
 *   wifi  2.4 GHz access points seen by the ESP32-S3
 *   ble   BLE advertisements from the same radio
 *   rf    raw band occupancy per 1 MHz channel from a stacked nRF24L01+
 *
 * Unlike the Serial Ports connector — which lists ports and deliberately never
 * opens them — this one opens the port, because reading the stream is the
 * entire point. It only ever reads and sends the firmware's own short commands.
 */
export class ArduinoScannerConnector extends Connector {
  static id = 'r4';
  static name = 'Arduino 2.4 GHz Scanner';
  static icon = '🛰️';
  static kind = 'ble';
  static description =
    'Reads an UNO R4 running the AEGIS firmware: WiFi APs, BLE advertisements and raw band occupancy from a stacked nRF24L01+.';
  static action = 'Connect';

  static BAUD = 115200;

  /** Ignore raw-band channels quieter than this, or the radar fills with noise. */
  static RF_FLOOR = 0.08;

  support() {
    if (!('serial' in navigator)) {
      return { level: 'no', note: 'Web Serial unavailable — use Chrome, Edge or Opera' };
    }
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready — flash firmware/aegis_scanner first' };
  }

  async start() {
    // Prefer a port this origin was already granted; otherwise prompt.
    const granted = await navigator.serial.getPorts();
    const port = granted[0] ?? await navigator.serial.requestPort();

    await port.open({ baudRate: ArduinoScannerConnector.BAUD });
    this.port = port;

    this.onStop(async () => {
      try { await this.reader?.cancel(); } catch { /* already closed */ }
      try { await this.streamClosed?.catch(() => {}); } catch { /* noop */ }
      try { this.writer?.releaseLock(); } catch { /* noop */ }
      try { await port.close(); } catch { /* noop */ }
    });

    const decoder = new TextDecoderStream();
    this.streamClosed = port.readable.pipeTo(decoder.writable).catch(() => {});
    this.reader = decoder.readable.getReader();

    this.active = true;
    this.log('ok', `port open at ${ArduinoScannerConnector.BAUD} baud`);

    // Read in the background; a scanner that never stops talking should not
    // block start() from returning.
    this.pump();

    // Nudge the board in case it booted before the port was opened.
    await this.send('id');
  }

  async send(command) {
    if (!this.port?.writable) return;
    const writer = this.port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(`${command}\n`));
    } finally {
      writer.releaseLock();
    }
  }

  async pump() {
    let buffer = '';
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        buffer += value;

        // Serial delivers arbitrary chunks, so hold the partial trailing line.
        const lines = buffer.split('\n');
        buffer = lines.pop() ?? '';

        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed) this.handleLine(trimmed);
        }

        // A board stuck mid-line must not grow the buffer without bound.
        if (buffer.length > 4096) buffer = '';
      }
    } catch (err) {
      if (this.active) this.log('err', `serial read ended: ${err.message}`);
    }
    if (this.active) this.log('warn', 'board disconnected');
  }

  handleLine(line) {
    let record;
    try {
      record = JSON.parse(line);
    } catch {
      // Boot banners and stray prints are normal; surface them, don't crash.
      this.log('info', `raw: ${line.slice(0, 120)}`);
      return;
    }

    switch (record.t) {
      case 'hello': return this.onHello(record);
      case 'wifi': return this.onWifi(record);
      case 'ble': return this.onBle(record);
      case 'rf': return this.onRf(record);
      case 'sweep': return this.onSweep(record);
      case 'err': return this.log('err', `board: ${record.msg}`);
      default: return;
    }
  }

  onHello(r) {
    const caps = (r.caps ?? []).join(', ') || 'none';
    this.log('ok', `${r.board} running ${r.fw} — sources: ${caps}`);
    if (r.radio) this.log('info', `radio firmware ${r.radio}`);
    if (!(r.caps ?? []).includes('rf')) {
      this.log('warn', 'no nRF24L01+ detected — raw band sweep unavailable');
    }
    if (r.lna) {
      this.log('info', `+LNA module: ${r.lna} dB gain puts the detection floor at ${r.floorDbm} dBm`);
    }

    this.board = r;
    this.publish('board', r);
    this.publish('spectrum:configure', {
      channels: r.channels ?? 126,
      floorDbm: r.floorDbm ?? -64,
    });
  }

  onWifi(r) {
    this.emit({
      id: `${ArduinoScannerConnector.id}:wifi:${r.bssid}`,
      name: r.ssid || '(hidden network)',
      identifier: r.bssid,
      signal: r.rssi,
      detail: [
        'WiFi AP',
        r.ch != null ? `ch ${r.ch}` : null,
        r.mhz != null ? `${r.mhz} MHz` : null,
        r.enc,
      ].filter(Boolean).join(' · '),
    });
  }

  onBle(r) {
    this.emit({
      id: `${ArduinoScannerConnector.id}:ble:${r.addr}`,
      name: r.name || '(unnamed BLE device)',
      identifier: r.addr,
      signal: r.rssi,
      detail: 'BLE advertisement',
    });
  }

  onRf(r) {
    const occupancy = r.max ? r.hits / r.max : 0;

    // The spectrum panel wants every reading, including the weak ones the
    // radar deliberately drops — that texture is the whole point of a
    // waterfall.
    this.publish('spectrum:channel', { channel: r.ch, occupancy });

    if (occupancy < ArduinoScannerConnector.RF_FLOOR) return;

    this.emit({
      id: `${ArduinoScannerConnector.id}:rf:${r.ch}`,
      name: `${r.mhz} MHz`,
      identifier: `raw ch ${r.ch}`,
      // The nRF24's RPD is a one-bit comparator at roughly -64 dBm, not a
      // meter. Occupancy is mapped onto the radar's scale so these plot
      // sensibly next to real RSSI readings — it is not a measured dBm.
      signal: Math.round(-70 + occupancy * 35),
      detail: [
        'raw 2.4 GHz energy',
        `${Math.round(occupancy * 100)}% occupancy`,
        `${r.hits}/${r.max} samples ≥ -64 dBm`,
        overlappingWifiChannels(r.ch),
      ].filter(Boolean).join(' · '),
    });
  }

  onSweep(r) {
    if (r.phase === 'begin') {
      this.publish('sweep', { state: 'begin' });
      return;
    }
    if (r.phase === 'end') {
      this.publish('sweep', { state: 'end' });
      return;
    }
    if (r.src === 'rf') this.publish('spectrum:commit', null);
    this.publish('sweep', { state: 'source', src: r.src, n: r.n, ms: r.ms });
    this.log('info', `${r.src} sweep: ${r.n} result(s) in ${r.ms} ms`);
  }
}

/**
 * Map an nRF24 1 MHz channel back to the 22 MHz-wide WiFi channels that
 * overlap it — the reason a "quiet" WiFi channel can still be unusable.
 */
function overlappingWifiChannels(nrfChannel) {
  const mhz = 2400 + nrfChannel;
  const overlapping = [];
  for (let ch = 1; ch <= 14; ch++) {
    const centre = ch === 14 ? 2484 : 2407 + ch * 5;
    if (Math.abs(mhz - centre) <= 11) overlapping.push(ch);
  }
  return overlapping.length ? `overlaps WiFi ch ${overlapping.join('/')}` : null;
}
