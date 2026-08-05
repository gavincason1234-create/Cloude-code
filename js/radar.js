/**
 * Canvas radar plot.
 *
 * Bearing is a stable hash of the contact id (so a device keeps its slot across
 * re-renders), and range maps signal strength to distance from centre: strong
 * signal sits close in. A contact only lights up as the sweep line crosses its
 * bearing, then decays — the same read as a real PPI scope.
 */

const TAU = Math.PI * 2;

const PALETTE = {
  ble: '#37e8ff',
  net: '#8b5cff',
  usb: '#ffca57',
  env: '#43f5a5',
};

export class Radar {
  constructor(canvas, opts = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.contacts = [];
    this.blips = new Map();   // id -> { angle, radius, lit, x, y }
    this.sweepAngle = -Math.PI / 2;
    this.sweepSpeed = opts.sweepSpeed ?? 0.9; // rad/s
    this.showSweep = true;
    this.showLabels = true;
    this.showTrails = true;
    this.pointer = null;
    this.hovered = null;
    this.onHover = opts.onHover ?? (() => {});
    this.running = false;

    this._tick = this._tick.bind(this);
    this._resize = this._resize.bind(this);

    window.addEventListener('resize', this._resize);
    canvas.addEventListener('pointermove', (e) => {
      const r = canvas.getBoundingClientRect();
      this.pointer = {
        x: (e.clientX - r.left) * (canvas.width / r.width),
        y: (e.clientY - r.top) * (canvas.height / r.height),
        cx: e.clientX - r.left,
        cy: e.clientY - r.top,
      };
    });
    canvas.addEventListener('pointerleave', () => {
      this.pointer = null;
      this.hovered = null;
      this.onHover(null, null);
    });

    this._resize();
  }

  _resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const cssW = this.canvas.clientWidth || 520;
    const size = Math.max(240, cssW);
    this.canvas.width = Math.round(size * dpr);
    this.canvas.height = Math.round(size * dpr);
    this.canvas.style.height = `${size}px`;
    this.dpr = dpr;
    this.size = this.canvas.width;
    this.cx = this.size / 2;
    this.cy = this.size / 2;
    this.radius = this.size / 2 - 26 * dpr;
  }

  setContacts(contacts) {
    this.contacts = contacts;
    const live = new Set();
    for (const c of contacts) {
      live.add(c.id);
      const existing = this.blips.get(c.id);
      const radius = signalToRange(c.signal);
      if (existing) {
        existing.radius += (radius - existing.radius) * 0.25;
        existing.contact = c;
      } else {
        this.blips.set(c.id, {
          angle: hashAngle(c.id),
          radius,
          lit: 1,
          contact: c,
          born: performance.now(),
        });
      }
    }
    for (const id of [...this.blips.keys()]) {
      if (!live.has(id)) this.blips.delete(id);
    }
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.last = performance.now();
    requestAnimationFrame(this._tick);
  }

  stop() { this.running = false; }

  _tick(now) {
    if (!this.running) return;
    const dt = Math.min((now - this.last) / 1000, 1 / 30);
    this.last = now;

    const prev = this.sweepAngle;
    if (this.showSweep) this.sweepAngle = (this.sweepAngle + this.sweepSpeed * dt) % TAU;

    // Light any blip the sweep just crossed.
    for (const b of this.blips.values()) {
      if (this.showSweep && crossed(prev, this.sweepAngle, b.angle)) b.lit = 1;
      b.lit = Math.max(0, b.lit - dt * 0.42);
    }

    this._draw(now / 1000);
    requestAnimationFrame(this._tick);
  }

  _draw(t) {
    const { ctx, size, cx, cy, radius, dpr } = this;
    ctx.clearRect(0, 0, size, size);

    // --- backdrop ----------------------------------------------------------
    const bg = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius);
    bg.addColorStop(0, 'rgba(55,232,255,0.055)');
    bg.addColorStop(0.65, 'rgba(20,30,60,0.22)');
    bg.addColorStop(1, 'rgba(5,8,16,0.35)');
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, TAU);
    ctx.fillStyle = bg;
    ctx.fill();

    // --- range rings -------------------------------------------------------
    ctx.lineWidth = 1 * dpr;
    for (let i = 1; i <= 4; i++) {
      const r = (radius / 4) * i;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, TAU);
      ctx.strokeStyle = i === 4 ? 'rgba(120,170,255,0.28)' : 'rgba(120,170,255,0.11)';
      ctx.stroke();
    }

    // --- bearing spokes + cardinal labels ----------------------------------
    ctx.strokeStyle = 'rgba(120,170,255,0.09)';
    for (let i = 0; i < 12; i++) {
      const a = (i / 12) * TAU;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + Math.cos(a) * radius, cy + Math.sin(a) * radius);
      ctx.stroke();
    }
    ctx.font = `${10 * dpr}px ui-monospace, monospace`;
    ctx.fillStyle = 'rgba(138,151,184,0.65)';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const [label, a] of [['N', -90], ['E', 0], ['S', 90], ['W', 180]]) {
      const rad = (a * Math.PI) / 180;
      ctx.fillText(
        label,
        cx + Math.cos(rad) * (radius + 13 * dpr),
        cy + Math.sin(rad) * (radius + 13 * dpr),
      );
    }

    // --- sweep -------------------------------------------------------------
    if (this.showSweep) {
      ctx.save();
      ctx.translate(cx, cy);
      ctx.rotate(this.sweepAngle);
      const wedge = ctx.createConicGradient
        ? ctx.createConicGradient(0, 0, 0)
        : null;
      if (wedge) {
        // Stops run clockwise from the sweep line, so the afterglow has to be
        // laid down at the *end* of the sweep to trail behind the beam.
        wedge.addColorStop(0, 'rgba(55,232,255,0)');
        wedge.addColorStop(0.82, 'rgba(55,232,255,0)');
        wedge.addColorStop(0.95, 'rgba(55,232,255,0.09)');
        wedge.addColorStop(1, 'rgba(55,232,255,0.30)');
        ctx.fillStyle = wedge;
      } else {
        ctx.fillStyle = 'rgba(55,232,255,0.12)';
      }
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.arc(0, 0, radius, wedge ? 0 : -0.7, wedge ? TAU : 0);
      ctx.closePath();
      ctx.fill();

      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.lineTo(radius, 0);
      ctx.strokeStyle = 'rgba(120,244,255,0.75)';
      ctx.lineWidth = 1.4 * dpr;
      ctx.stroke();
      ctx.restore();
    }

    // --- contacts ----------------------------------------------------------
    this.hovered = null;
    let hoveredBlip = null;

    for (const b of this.blips.values()) {
      const x = cx + Math.cos(b.angle) * b.radius * radius;
      const y = cy + Math.sin(b.angle) * b.radius * radius;
      b.x = x;
      b.y = y;

      const colour = PALETTE[b.contact.kind] ?? PALETTE.env;
      const alpha = 0.30 + b.lit * 0.70;
      const age = (performance.now() - b.born) / 1000;
      const pop = Math.min(1, age * 3.5);

      if (this.showTrails) {
        ctx.beginPath();
        ctx.arc(x, y, (5 + b.lit * 17) * dpr * pop, 0, TAU);
        ctx.fillStyle = withAlpha(colour, 0.10 * b.lit);
        ctx.fill();
      }

      ctx.beginPath();
      ctx.arc(x, y, 3.6 * dpr * pop, 0, TAU);
      ctx.fillStyle = withAlpha(colour, alpha);
      ctx.shadowColor = colour;
      ctx.shadowBlur = (6 + b.lit * 14) * dpr;
      ctx.fill();
      ctx.shadowBlur = 0;

      // hit test
      if (this.pointer) {
        const d = Math.hypot(this.pointer.x - x, this.pointer.y - y);
        if (d < 16 * dpr && (!hoveredBlip || d < hoveredBlip.d)) {
          hoveredBlip = { d, blip: b };
        }
      }

      if (this.showLabels) {
        ctx.font = `${10 * dpr}px ui-monospace, monospace`;
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = withAlpha(colour, 0.35 + b.lit * 0.5);
        const label = truncate(b.contact.name, 16);
        ctx.fillText(label, x + 9 * dpr, y);
      }
    }

    if (hoveredBlip) {
      const b = hoveredBlip.blip;
      this.hovered = b.contact;
      ctx.beginPath();
      ctx.arc(b.x, b.y, 11 * dpr, 0, TAU);
      ctx.strokeStyle = 'rgba(255,255,255,0.85)';
      ctx.lineWidth = 1.2 * dpr;
      ctx.stroke();
      this.onHover(b.contact, this.pointer);
    } else if (this.pointer) {
      this.onHover(null, null);
    }

    // --- centre marker ------------------------------------------------------
    const pulse = 3 + Math.sin(t * 2.4) * 1.2;
    ctx.beginPath();
    ctx.arc(cx, cy, pulse * dpr, 0, TAU);
    ctx.fillStyle = 'rgba(230,237,255,0.9)';
    ctx.fill();
    ctx.beginPath();
    ctx.arc(cx, cy, (pulse + 5) * dpr, 0, TAU);
    ctx.strokeStyle = 'rgba(230,237,255,0.22)';
    ctx.lineWidth = 1 * dpr;
    ctx.stroke();

    // --- count -------------------------------------------------------------
    ctx.font = `${10 * dpr}px ui-monospace, monospace`;
    ctx.fillStyle = 'rgba(138,151,184,0.7)';
    ctx.textAlign = 'left';
    ctx.fillText(`CONTACTS ${String(this.blips.size).padStart(3, '0')}`, 10 * dpr, 14 * dpr);
  }
}

/** Deterministic bearing from an arbitrary id string. */
function hashAngle(id) {
  let h = 2166136261;
  for (let i = 0; i < id.length; i++) {
    h ^= id.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return ((h >>> 0) % 3600) / 3600 * TAU;
}

/**
 * Map a signal reading to a normalised range (0 = centre, 1 = edge).
 * Bluetooth RSSI runs roughly -100..-30 dBm; anything else gets a mid ring.
 */
function signalToRange(signal) {
  if (signal == null || Number.isNaN(signal)) return 0.62;
  const clamped = Math.max(-100, Math.min(-30, signal));
  return 0.12 + (1 - (clamped + 100) / 70) * 0.8;
}

function crossed(prev, next, target) {
  if (next >= prev) return target > prev && target <= next;
  return target > prev || target <= next; // wrapped past TAU
}

function withAlpha(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${Math.max(0, Math.min(1, a))})`;
}

function truncate(s, n) {
  return s.length > n ? `${s.slice(0, n - 1)}…` : s;
}
