/**
 * Extruded 3D text renderer on a 2D canvas.
 *
 * There is no WebGL here — depth comes from stacking many slightly offset
 * copies of the glyphs along a light-aware extrusion vector, then shading each
 * slice. Yaw/pitch follow the pointer through a critically-damped spring so the
 * lettering feels like a solid object rather than a parallax trick.
 */

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

export class Text3D {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {string} text
   * @param {object} [opts]
   */
  constructor(canvas, text, opts = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.text = text;

    this.depth = opts.depth ?? 26;          // extrusion slices
    this.faceTop = opts.faceTop ?? '#f4f9ff';
    this.faceBottom = opts.faceBottom ?? '#7fd8ff';
    this.sideNear = opts.sideNear ?? '#37e8ff';
    this.sideFar = opts.sideFar ?? '#4b2a9c';
    this.glow = opts.glow ?? '#37e8ff';
    this.fontStack = opts.fontStack ?? '900 96px "Inter", system-ui, sans-serif';

    // spring state: [current, target, velocity]
    this.yaw = { v: 0, t: 0, vel: 0 };
    this.pitch = { v: 0, t: 0, vel: 0 };
    this.hover = { v: 0, t: 0, vel: 0 };

    this.dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.frame = 0;
    this.running = false;

    this._onPointer = this._onPointer.bind(this);
    this._onLeave = this._onLeave.bind(this);
    this._tick = this._tick.bind(this);

    this._resize();
    this._bind();
  }

  _bind() {
    const target = window;
    target.addEventListener('pointermove', this._onPointer, { passive: true });
    this.canvas.addEventListener('pointerleave', this._onLeave);
    window.addEventListener('resize', () => this._resize());
  }

  _resize() {
    const { canvas } = this;
    const cssW = canvas.clientWidth || canvas.width;
    const cssH = cssW * (canvas.height / canvas.width) || canvas.height;
    this.w = canvas.width;
    this.h = canvas.height;
    canvas.width = Math.round(this.w);
    canvas.height = Math.round(this.h);
    this.cssW = cssW;
    this.cssH = cssH;
  }

  _onPointer(e) {
    const r = this.canvas.getBoundingClientRect();
    const cx = r.left + r.width / 2;
    const cy = r.top + r.height / 2;
    // Normalised offset from the logo centre, softened well past its bounds so
    // the lettering keeps tracking the cursor across the whole header.
    const nx = clamp((e.clientX - cx) / (r.width * 1.6), -1, 1);
    const ny = clamp((e.clientY - cy) / (r.height * 3.2), -1, 1);
    this.yaw.t = nx * 0.42;
    this.pitch.t = -ny * 0.26;

    const inside =
      e.clientX >= r.left && e.clientX <= r.right &&
      e.clientY >= r.top && e.clientY <= r.bottom;
    this.hover.t = inside ? 1 : 0;
  }

  _onLeave() {
    this.yaw.t = 0;
    this.pitch.t = 0;
    this.hover.t = 0;
  }

  /** Critically-damped spring step. */
  _spring(s, dt, stiffness = 90, damping = 14) {
    const a = stiffness * (s.t - s.v) - damping * s.vel;
    s.vel += a * dt;
    s.v += s.vel * dt;
    return s.v;
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
    this.frame++;

    this._spring(this.yaw, dt);
    this._spring(this.pitch, dt);
    this._spring(this.hover, dt, 60, 12);

    this.render(now / 1000);
    requestAnimationFrame(this._tick);
  }

  render(t = 0) {
    const { ctx, w, h } = this;
    const yaw = this.yaw.v;
    const pitch = this.pitch.v;
    const hover = this.hover.v;

    ctx.clearRect(0, 0, w, h);
    ctx.save();
    ctx.translate(w / 2, h / 2 + 6);

    // Perspective approximation: yaw squeezes horizontally, pitch shears.
    const sx = Math.cos(yaw) * 0.92 + 0.08;
    const shear = Math.sin(pitch) * 0.55;
    ctx.transform(sx, shear * 0.14, Math.sin(yaw) * -0.16, 1 - Math.abs(pitch) * 0.12, 0, 0);

    ctx.font = this.fontStack;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.letterSpacing = '10px';

    const metrics = ctx.measureText(this.text);
    const glyphH =
      (metrics.actualBoundingBoxAscent || 48) + (metrics.actualBoundingBoxDescent || 12);

    // Extrusion direction: away from the virtual key light, amplified by yaw.
    const ex = -Math.sin(yaw) * 1.9 - 0.35;
    const ey = Math.sin(pitch) * 1.9 + 0.75;

    const slices = this.depth;

    // --- back slab: far → near, darkest first -------------------------------
    for (let i = slices; i >= 1; i--) {
      const k = i / slices;
      const g = ctx.createLinearGradient(0, -glyphH / 2, 0, glyphH / 2);
      g.addColorStop(0, mix(this.sideFar, this.sideNear, 1 - k));
      g.addColorStop(1, mix('#180b3a', this.sideNear, (1 - k) * 0.7));
      ctx.fillStyle = g;
      ctx.fillText(this.text, ex * i, ey * i);
    }

    // --- ambient bloom behind the face --------------------------------------
    ctx.save();
    ctx.shadowColor = this.glow;
    ctx.shadowBlur = 26 + hover * 26 + Math.sin(t * 1.6) * 4;
    ctx.fillStyle = 'rgba(55,232,255,0.14)';
    ctx.fillText(this.text, 0, 0);
    ctx.restore();

    // --- front face ---------------------------------------------------------
    const face = ctx.createLinearGradient(0, -glyphH / 2, 0, glyphH / 2);
    face.addColorStop(0, this.faceTop);
    face.addColorStop(0.55, mix(this.faceTop, this.faceBottom, 0.5));
    face.addColorStop(1, this.faceBottom);
    ctx.fillStyle = face;
    ctx.fillText(this.text, 0, 0);

    // --- specular sweep: a moving band clipped to the glyphs -----------------
    ctx.save();
    ctx.beginPath();
    ctx.rect(-w, -h, w * 2, h * 2);
    ctx.clip();
    const period = 5.5;
    const phase = ((t % period) / period) * 2.4 - 0.7;
    const bandX = phase * (metrics.width || w) - (metrics.width || w) / 2;
    const spec = ctx.createLinearGradient(bandX - 70, 0, bandX + 70, 0);
    spec.addColorStop(0, 'rgba(255,255,255,0)');
    spec.addColorStop(0.5, `rgba(255,255,255,${0.32 + hover * 0.22})`);
    spec.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.globalCompositeOperation = 'source-atop';
    ctx.fillStyle = spec;
    ctx.fillText(this.text, 0, 0);
    ctx.restore();

    // --- chromatic edge: cyan/violet fringe on the top bevel ----------------
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    ctx.lineWidth = 1.2;
    ctx.strokeStyle = 'rgba(139,92,255,0.55)';
    ctx.strokeText(this.text, -1.1, -1.1);
    ctx.strokeStyle = 'rgba(55,232,255,0.5)';
    ctx.strokeText(this.text, 1.1, 0.8);
    ctx.restore();

    ctx.restore();
  }
}

/** Blend two #rrggbb colours. */
function mix(a, b, k) {
  const pa = parseInt(a.slice(1), 16);
  const pb = parseInt(b.slice(1), 16);
  const r = Math.round((pa >> 16 & 255) * (1 - k) + (pb >> 16 & 255) * k);
  const g = Math.round((pa >> 8 & 255) * (1 - k) + (pb >> 8 & 255) * k);
  const bl = Math.round((pa & 255) * (1 - k) + (pb & 255) * k);
  return `rgb(${r},${g},${bl})`;
}
