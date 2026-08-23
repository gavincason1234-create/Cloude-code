/**
 * 2.4 GHz spectrum display.
 *
 * Two stacked views sharing one frequency axis:
 *
 *   bars       current occupancy per 1 MHz channel, with a decaying peak-hold
 *   waterfall   the last N sweeps scrolling downward, so intermittent emitters
 *               (a mouse dongle, a video sender keying up) leave a visible trace
 *
 * Occupancy is not power. The nRF24's RPD is a one-bit comparator, so a bar at
 * 80% means "carrier was above the detection floor in 80% of samples", and the
 * floor itself depends on whether the module has an LNA. The panel prints the
 * floor the firmware reports rather than assuming one.
 */

const WIFI_CHANNELS = [1, 6, 11];       // the non-overlapping set worth marking
const WATERFALL_ROWS = 48;

export class Spectrum {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');

    this.channelCount = 126;
    this.current = new Float32Array(this.channelCount);
    this.peak = new Float32Array(this.channelCount);
    this.pending = new Float32Array(this.channelCount);
    this.history = [];
    this.floorDbm = -64;
    this.hasData = false;
    this.sweeps = 0;

    this.running = false;
    this._tick = this._tick.bind(this);
    this._resize = this._resize.bind(this);
    window.addEventListener('resize', this._resize);
    this._resize();
  }

  /** Firmware told us how the band is being sampled. */
  configure({ channels, floorDbm }) {
    if (channels && channels !== this.channelCount) {
      this.channelCount = channels;
      this.current = new Float32Array(channels);
      this.peak = new Float32Array(channels);
      this.pending = new Float32Array(channels);
      this.history = [];
    }
    if (floorDbm != null) this.floorDbm = floorDbm;
  }

  /** One channel reading from the current sweep. */
  setChannel(channel, occupancy) {
    if (channel < 0 || channel >= this.channelCount) return;
    this.pending[channel] = occupancy;
    this.hasData = true;
  }

  /**
   * Sweep finished: promote the pending row.
   *
   * Channels the firmware omitted are silent, not unknown — it only sends
   * non-zero readings — so the pending buffer is zeroed rather than carried
   * over, or a one-off spike would stick forever.
   */
  commit() {
    this.current.set(this.pending);
    for (let i = 0; i < this.channelCount; i++) {
      this.peak[i] = Math.max(this.peak[i] * 0.94, this.current[i]);
    }
    this.history.push(Float32Array.from(this.current));
    if (this.history.length > WATERFALL_ROWS) this.history.shift();
    this.pending.fill(0);
    this.sweeps++;
  }

  clear() {
    this.current.fill(0);
    this.peak.fill(0);
    this.pending.fill(0);
    this.history = [];
    this.hasData = false;
    this.sweeps = 0;
  }

  _resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const cssW = this.canvas.clientWidth || 640;
    const cssH = 300;
    this.canvas.width = Math.round(cssW * dpr);
    this.canvas.height = Math.round(cssH * dpr);
    this.canvas.style.height = `${cssH}px`;
    this.dpr = dpr;
  }

  start() {
    if (this.running) return;
    this.running = true;
    requestAnimationFrame(this._tick);
  }

  stop() { this.running = false; }

  _tick() {
    if (!this.running) return;
    this.draw();
    requestAnimationFrame(this._tick);
  }

  draw() {
    const { ctx, dpr } = this;
    const w = this.canvas.width;
    const h = this.canvas.height;

    const padL = 40 * dpr;
    const padR = 10 * dpr;
    const padT = 8 * dpr;
    const padB = 26 * dpr;
    const plotW = w - padL - padR;

    const barsH = (h - padT - padB) * 0.46;
    const gap = 10 * dpr;
    const fallY = padT + barsH + gap;
    const fallH = h - padB - fallY;

    ctx.clearRect(0, 0, w, h);

    if (!this.hasData) {
      ctx.font = `${11 * dpr}px ui-monospace, monospace`;
      ctx.fillStyle = 'rgba(86,98,128,0.9)';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('AWAITING SWEEP — CONNECT THE BOARD', w / 2, h / 2);
      return;
    }

    const colW = plotW / this.channelCount;

    // --- WiFi channel bands behind everything ---------------------------
    for (const ch of WIFI_CHANNELS) {
      const centre = 2407 + ch * 5;
      const lo = this._x(centre - 11 - 2400, padL, plotW);
      const hi = this._x(centre + 11 - 2400, padL, plotW);
      ctx.fillStyle = 'rgba(139,92,255,0.07)';
      ctx.fillRect(lo, padT, hi - lo, barsH);
      ctx.font = `${9 * dpr}px ui-monospace, monospace`;
      ctx.fillStyle = 'rgba(139,92,255,0.55)';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.fillText(`ch${ch}`, (lo + hi) / 2, padT + 2 * dpr);
    }

    // --- occupancy bars --------------------------------------------------
    for (let i = 0; i < this.channelCount; i++) {
      const x = padL + i * colW;
      const value = this.current[i];
      if (value > 0) {
        const barH = value * barsH;
        const grad = ctx.createLinearGradient(0, padT + barsH - barH, 0, padT + barsH);
        grad.addColorStop(0, heat(value));
        grad.addColorStop(1, 'rgba(55,232,255,0.25)');
        ctx.fillStyle = grad;
        ctx.fillRect(x, padT + barsH - barH, Math.max(colW - 0.5 * dpr, 1), barH);
      }
      // peak-hold tick
      if (this.peak[i] > 0.02) {
        ctx.fillStyle = 'rgba(255,255,255,0.42)';
        ctx.fillRect(x, padT + barsH - this.peak[i] * barsH, Math.max(colW - 0.5 * dpr, 1), 1 * dpr);
      }
    }

    // baseline
    ctx.strokeStyle = 'rgba(120,170,255,0.22)';
    ctx.lineWidth = 1 * dpr;
    ctx.beginPath();
    ctx.moveTo(padL, padT + barsH + 0.5);
    ctx.lineTo(padL + plotW, padT + barsH + 0.5);
    ctx.stroke();

    // --- waterfall -------------------------------------------------------
    // Rows stretch to fill the region until the buffer is full, then settle to
    // their final height. A fixed height would leave the panel looking broken
    // for the several minutes it takes to accumulate WATERFALL_ROWS sweeps.
    const rowH = fallH / Math.min(WATERFALL_ROWS, Math.max(this.history.length, 1));
    for (let r = 0; r < this.history.length; r++) {
      // Newest at the top, scrolling down.
      const row = this.history[this.history.length - 1 - r];
      const y = fallY + r * rowH;
      for (let i = 0; i < this.channelCount; i++) {
        const value = row[i];
        if (value <= 0) continue;
        ctx.fillStyle = heat(value, 0.9);
        ctx.fillRect(padL + i * colW, y, Math.max(colW, 1), Math.ceil(rowH));
      }
    }
    ctx.strokeStyle = 'rgba(120,170,255,0.14)';
    ctx.strokeRect(padL + 0.5, fallY + 0.5, plotW - 1, fallH - 1);

    // --- axes ------------------------------------------------------------
    ctx.font = `${9 * dpr}px ui-monospace, monospace`;
    ctx.fillStyle = 'rgba(138,151,184,0.75)';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (let mhz = 2400; mhz <= 2400 + this.channelCount; mhz += 25) {
      const x = this._x(mhz - 2400, padL, plotW);
      ctx.fillText(String(mhz), x, h - padB + 6 * dpr);
      ctx.strokeStyle = 'rgba(120,170,255,0.10)';
      ctx.beginPath();
      ctx.moveTo(x, padT);
      ctx.lineTo(x, h - padB);
      ctx.stroke();
    }

    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = 'rgba(138,151,184,0.75)';
    ctx.fillText('100%', padL - 6 * dpr, padT + 4 * dpr);
    ctx.fillText('0%', padL - 6 * dpr, padT + barsH - 4 * dpr);

    ctx.save();
    ctx.translate(12 * dpr, fallY + fallH / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.textAlign = 'center';
    ctx.fillStyle = 'rgba(138,151,184,0.6)';
    ctx.fillText('← time', 0, 0);
    ctx.restore();

    // --- footer ----------------------------------------------------------
    // Right-aligned so it never collides with the leftmost frequency label.
    ctx.textAlign = 'right';
    ctx.textBaseline = 'bottom';
    ctx.fillStyle = 'rgba(138,151,184,0.7)';
    ctx.fillText(
      `floor ${this.floorDbm} dBm · ${this.channelCount} ch · sweep ${this.sweeps}`,
      padL + plotW, h - 2 * dpr,
    );
  }

  _x(offsetMhz, padL, plotW) {
    return padL + (offsetMhz / this.channelCount) * plotW;
  }
}

/** Cyan → violet → amber as occupancy rises. */
function heat(value, alpha = 1) {
  const v = Math.max(0, Math.min(1, value));
  if (v < 0.5) {
    const k = v / 0.5;
    return `rgba(${Math.round(55 + k * 84)},${Math.round(232 - k * 140)},${Math.round(255 - k * 0)},${alpha})`;
  }
  const k = (v - 0.5) / 0.5;
  return `rgba(${Math.round(139 + k * 116)},${Math.round(92 + k * 110)},${Math.round(255 - k * 168)},${alpha})`;
}
