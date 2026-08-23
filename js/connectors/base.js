/**
 * Connector contract.
 *
 * A connector wraps exactly one browser capability. Every one of them is
 * permission-gated by the platform: `start()` is only ever called from a real
 * user gesture, and if the user denies the prompt the connector reports the
 * denial and stays off. Connectors never poll for data they were not granted.
 */
export class Connector {
  /** @param {{emit:Function, drop:Function, log:Function}} bus */
  constructor(bus) {
    this.bus = bus;
    this.active = false;
    this.cleanup = [];
  }

  /* --- static metadata, overridden per connector ------------------------- */
  static id = 'base';
  static name = 'Connector';
  static icon = '•';
  /** Radar colour group: ble | net | usb | env */
  static kind = 'env';
  static description = '';
  /** Label for the enable button. */
  static action = 'Enable';

  /**
   * @returns {{level:'ok'|'flag'|'no', note:string}}
   * `flag` means the API exists but needs a browser flag or secure context.
   */
  support() {
    return { level: 'no', note: 'unimplemented' };
  }

  async start() { throw new Error('not implemented'); }

  async stop() {
    for (const fn of this.cleanup.splice(0)) {
      try { fn(); } catch { /* teardown is best-effort */ }
    }
    this.active = false;
  }

  /* --- helpers ----------------------------------------------------------- */

  emit(contact) {
    this.bus.emit({ source: this.constructor.id, kind: this.constructor.kind, ...contact });
  }

  drop(id) { this.bus.drop(`${this.constructor.id}:${id}`); }

  log(level, message) { this.bus.log(level, `[${this.constructor.id}] ${message}`); }

  /**
   * Side-channel for data that is not a contact — spectrum frames, board
   * status. Optional: connectors that never call it need no bus support.
   */
  publish(topic, payload) { this.bus.publish?.(this.constructor.id, topic, payload); }

  /** Register a teardown function to run on stop(). */
  onStop(fn) { this.cleanup.push(fn); }

  /** addEventListener that auto-unbinds on stop(). */
  listen(target, type, handler, opts) {
    target.addEventListener(type, handler, opts);
    this.onStop(() => target.removeEventListener(type, handler, opts));
  }

  /** setInterval that auto-clears on stop(). */
  every(ms, fn) {
    const h = setInterval(fn, ms);
    this.onStop(() => clearInterval(h));
    return h;
  }
}

/** True on https:// or localhost — required by most of these APIs. */
export const isSecure = () => window.isSecureContext === true;
