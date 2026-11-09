(function () {
'use strict';
/* ===== Chimera web UI. Classic script, no imports. ===== */
const LAYER_COL = ['#40c4ff', '#ff9f0a', '#bf5af2', '#34c759'];
const ENGINES = ['Off', 'Analog', 'Wavetable', 'FM', 'Sampler', 'Pluck'];
const WAVES = ['Sine', 'Triangle', 'Saw', 'Square', 'Noise'];
const FILTERS = ['Off', 'LP 12', 'LP 24', 'HP 12', 'HP 24', 'BP', 'Notch', 'Ladder', 'Comb'];
const LFO_SHAPES = ['Sine', 'Triangle', 'Saw up', 'Saw down', 'Square', 'Sample & hold', 'Smooth random'];
const MOD_SRCS = ['None', 'LFO 1', 'LFO 2', 'Global LFO 1', 'Global LFO 2', 'Amp env', 'Filter env', 'Mod env', 'Velocity', 'Key track', 'Mod wheel', 'Aftertouch', 'Pitch bend', 'Random', 'Macro 1', 'Macro 2', 'Macro 3', 'Macro 4'];
const MOD_DSTS = ['None', 'Pitch', 'Cutoff', 'Resonance', 'Drive', 'Level', 'Pan', 'Pulse width', 'Osc mix', 'Osc 2 pitch', 'Noise', 'Wave position', 'FM amount', 'Sample start', 'Pluck damping', 'LFO 1 rate', 'LFO 2 rate', 'Filter env amount'];
const VOICE_MODES = ['Poly', 'Mono', 'Legato'];
const LOOPS = ['Off', 'Forward', 'Ping-pong'];
const WT_TABLES = ['Basic Shapes', 'PWM', 'Harmonics', 'Formant', 'Digital', 'Organ', 'Bell', 'Sync Sweep'];
const OUTPUTS = ['Main', 'Out 2', 'Out 3', 'Out 4'];
const KB_CURVES = ['-LIN', '-EXP', '+EXP', '+LIN'];
const BEATS = [0.0625, 0.125, 0.25, 0.375, 0.5, 0.75, 1, 1.5, 2, 3, 4, 8, 16];
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const noteName = n => NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 2);
// DX7 algorithms: [modulatorMask per operator (bit i = op i+1 modulates this op), carrierMask, feedback dst, feedback src] (0-based ops)
const ALGS=[[[2,0,8,16,32,0],5,5,5],[[2,0,8,16,32,0],5,1,1],[[2,4,0,16,32,0],9,5,5],[[2,4,0,16,32,0],9,5,3],[[2,0,8,0,32,0],21,5,5],[[2,0,8,0,32,0],21,5,4],[[2,0,8,48,0,0],5,5,5],[[2,0,8,48,0,0],5,3,3],[[2,0,8,48,0,0],5,1,1],[[2,4,0,48,0,0],9,2,2],[[2,4,0,48,0,0],9,5,5],[[2,0,56,0,0,0],5,1,1],[[2,0,56,0,0,0],5,5,5],[[2,0,24,0,32,0],5,5,5],[[2,0,24,0,32,0],5,1,1],[[22,0,8,0,32,0],1,5,5],[[22,0,8,0,32,0],1,1,1],[[14,0,0,16,32,0],1,2,2],[[2,4,0,32,32,0],25,5,5],[[4,4,0,48,0,0],11,2,2],[[4,4,0,32,32,0],27,2,2],[[2,0,32,32,32,0],29,5,5],[[0,4,0,32,32,0],27,5,5],[[0,0,32,32,32,0],31,5,5],[[0,0,0,32,32,0],31,5,5],[[0,4,0,48,0,0],11,5,5],[[0,4,0,48,0,0],11,2,2],[[2,0,8,16,0,0],37,4,4],[[0,0,8,0,32,0],23,5,5],[[0,0,8,16,0,0],39,4,4],[[0,0,0,0,32,0],31,5,5],[[0,0,0,0,0,0],63,5,5]];
/* ---------- state ---------- */
let patch = null, presets = [], presetIndex = -1;
let layer = 0, page = 'sound', fmOp = 0, lastVoice = [-1, -1, -1, -1];
let audioCtx = null, node = null;
let dirty = false, dragging = 0, pendingPatch = null;
const lastEngine = [1, 1, 1, 1];
const sampleViews = [null, null, null, null];
let clipboardLayer = null;
const redraws = [];            // canvas/diagram redraw callbacks for the current page
let refreshers = [];         // control refresh callbacks for the current page
let cart32 = null;

const $ = (s, r) => (r || document).querySelector(s);
function el(tag, cls, parent, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}
const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
const L = () => patch.layers[layer];
function toast(msg) {
  const t = $('#toast'); t.textContent = msg; t.style.display = 'block';
  clearTimeout(toast.h); toast.h = setTimeout(() => t.style.display = 'none', 4000);
}

/* ---------- messaging ---------- */
function post(msg, transfer) { if (node) try { node.port.postMessage(msg, transfer || []); } catch (e) { node.port.postMessage(msg); } }
function flushPatch() { if (dirty && patch) { dirty = false; post({ type: 'patch', json: JSON.stringify(patch) }); } }
function markDirty() {
  dirty = true;
  if (!markDirty.sched) { markDirty.sched = true; requestAnimationFrame(() => { markDirty.sched = false; flushPatch(); }); }
}
function edited() { markDirty(); redraws.forEach(f => f()); }

/* ---------- controls ---------- */
// bar: opts {label,get,set,min,max,curve:'lin'|'exp',step,fmt,def,bipolar,tip,int}
function bar(parent, o) {
  const d = el('div', 'bar', parent);
  d.tabIndex = 0; d.setAttribute('role', 'slider'); d.setAttribute('aria-label', o.label);
  if (o.tip) d.title = o.tip; else d.title = o.label + ' (double-click to reset, shift = fine, wheel to nudge)';
  const fill = el('i', '', d), lab = el('label', '', d, o.label), out = el('output', '', d);
  const exp = o.curve === 'exp';
  const sq = o.curve === 'sq';
  const toT = v => sq ? Math.sqrt(clamp((v - o.min) / (o.max - o.min), 0, 1)) : exp ? Math.log(clamp(v, o.min, o.max) / o.min) / Math.log(o.max / o.min) : (v - o.min) / (o.max - o.min);
  const fromT = t => { t = clamp(t, 0, 1); let v = sq ? o.min + t * t * (o.max - o.min) : exp ? o.min * Math.pow(o.max / o.min, t) : o.min + t * (o.max - o.min);
    if (o.int) v = Math.round(v); else if (o.step) v = Math.round(v / o.step) * o.step; return clamp(v, o.min, o.max); };
  const fmt = o.fmt || (v => String(Math.round(v * 100) / 100));
  function refresh() {
    if (!patch) return; const v = +o.get(); const t = toT(isFinite(v) ? v : o.min);
    if (o.bipolar) { fill.style.left = Math.min(t, .5) * 100 + '%'; fill.style.width = Math.abs(t - .5) * 100 + '%'; }
    else { fill.style.left = 0; fill.style.width = t * 100 + '%'; }
    out.textContent = fmt(v); d.setAttribute('aria-valuenow', v); d.setAttribute('aria-valuetext', fmt(v));
  }
  function setv(v) { o.set(v); refresh(); edited(); }
  let drag = false;
  const at = e => { const r = d.getBoundingClientRect(); return (e.clientX - r.left) / r.width; };
  let baseT = 0, baseX = 0;
  d.addEventListener('pointerdown', e => {
    if (e.button !== 0) return; d.setPointerCapture(e.pointerId); drag = true; dragging++;
    baseX = e.clientX; baseT = e.shiftKey ? toT(+o.get()) : at(e); if (!e.shiftKey) setv(fromT(baseT)); d.focus(); e.preventDefault();
  });
  d.addEventListener('pointermove', e => {
    if (!drag) return;
    if (e.shiftKey) setv(fromT(baseT + (e.clientX - baseX) / d.getBoundingClientRect().width / 8)); else setv(fromT(at(e)));
  });
  const up = () => { if (drag) { drag = false; dragging--; if (!dragging && pendingPatch) applyIncoming(pendingPatch); } };
  d.addEventListener('pointerup', up); d.addEventListener('pointercancel', up);
  d.addEventListener('dblclick', () => { if (o.def != null) setv(o.def); });
  d.addEventListener('wheel', e => { e.preventDefault(); nudge(e.deltaY < 0 ? 1 : -1, e.shiftKey ? .2 : 1); }, { passive: false });
  const nudge = (dir, k) => { const stepT = o.int ? 1 / (o.max - o.min) : (o.step ? o.step / (o.max - o.min) : .01);
    if (exp && !o.int) setv(fromT(toT(+o.get()) + dir * .01 * k)); else setv(fromT(toT(+o.get()) + dir * stepT * (o.int || o.step ? 1 : k))); };
  d.addEventListener('keydown', e => {
    if (e.key === 'ArrowRight' || e.key === 'ArrowUp') { nudge(1, e.shiftKey ? .2 : 1); e.preventDefault(); }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') { nudge(-1, e.shiftKey ? .2 : 1); e.preventDefault(); }
  });
  refresh(); refreshers.push(refresh);
  return d;
}
// property bar: obj() returns the object (or array) holding key
function pbar(parent, label, objFn, key, o) {
  const f = typeof objFn === 'function' ? objFn : () => objFn;
  return bar(parent, Object.assign({ label, get: () => f()[key], set: v => { f()[key] = v; } }, o));
}
function select(parent, label, options, get, set, tip) {
  const w = el('label', 'lbl', parent); if (label) el('span', '', w, label);
  const s = el('select', '', w); s.title = tip || label || ''; s.setAttribute('aria-label', label || tip || 'select');
  options.forEach((n, i) => { const o = el('option', '', s, n); o.value = i; });
  const refresh = () => { s.value = String(get()); };
  s.addEventListener('change', () => { set(+s.value); edited(); });
  refresh(); refreshers.push(refresh); s._set = (opts) => {}; return s;
}
function toggle(parent, label, get, set, tip) {
  const b = el('button', 'tog', parent, label); b.title = tip || label; b.setAttribute('aria-pressed', 'false');
  const refresh = () => { const on = !!get(); b.classList.toggle('on', on); b.setAttribute('aria-pressed', on); };
  b.addEventListener('click', () => { set(!get()); refresh(); edited(); });
  refresh(); refreshers.push(refresh); return b;
}
// preserves boolean-vs-number representation of a flag
function flag(obj, key) {
  return { get: () => !!obj()[key], set: v => { obj()[key] = typeof obj()[key] === 'number' ? (v ? 1 : 0) : !!v; } };
}
function panel(parent, title, cls) {
  const p = el('div', 'pnl' + (cls ? ' ' + cls : ''), parent);
  if (title) el('h3', '', p, title);
  return p;
}
const pct = v => Math.round(v * 100) + '%';
const secFmt = v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(v < 10 ? 2 : 1) + ' s';
const panFmt = v => Math.abs(v) < .005 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v) * 100);
const hzFmt = v => v >= 1000 ? (v / 1000).toFixed(1) + ' kHz' : Math.round(v) + ' Hz';
const beatLabel = b => b < 1 ? '1/' + Math.round(1 / b) + (Math.abs(1 / b - Math.round(1 / b)) > .01 ? '*' : '') : b + (b === 1 ? ' beat' : ' beats');
const beatName = b => { const m = { 0.0625: '1/16', 0.125: '1/8', 0.25: '1/4', 0.375: '3/16', 0.5: '1/2', 0.75: '3/4', 1: '1 beat', 1.5: '1.5 beats', 2: '2 beats', 3: '3 beats', 4: '4 beats', 8: '8 beats', 16: '16 beats' }; return m[b] || b + ' beats'; };
function canvas(parent, h, draw) {
  const c = el('canvas', 'canvas', parent); c.style.height = h + 'px';
  const render = () => {
    const w = c.clientWidth || 200, dpr = window.devicePixelRatio || 1;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
    const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
    draw(g, w, h, c);
  };
  redraws.push(render); requestAnimationFrame(render); return c;
}
function accent() { return LAYER_COL[layer]; }

/* ---------- waveform helpers ---------- */
function waveShape(w, t, pw) {
  t -= Math.floor(t);
  switch (w) {
    case 0: return Math.sin(t * 2 * Math.PI);
    case 1: return t < .25 ? 4 * t : t < .75 ? 2 - 4 * t : 4 * t - 4;
    case 2: return 2 * t - 1;
    case 3: return t < pw ? 1 : -1;
    default: return Math.sin(t * 9301.7 + 3.3) * Math.sin(t * 713.1) * 1.4;
  }
}
function drawWave(g, w, h, fn, col) {
  g.strokeStyle = '#222'; g.beginPath(); g.moveTo(0, h / 2); g.lineTo(w, h / 2); g.stroke();
  g.strokeStyle = col; g.lineWidth = 1.5; g.beginPath();
  for (let x = 0; x <= w; x += 1) { const y = h / 2 - clamp(fn(x / w), -1, 1) * (h / 2 - 6); x ? g.lineTo(x, y) : g.moveTo(x, y); }
  g.stroke(); g.lineWidth = 1;
}
function drawEnv(g, w, h, e) {
  const tw = s => Math.log10(1 + s * 200) / Math.log10(1 + 200 * 10); // 0..1 log-ish width
  const a = tw(e.attack), d = tw(e.decay), r = tw(e.release), hold = .25, tot = a + d + hold + r;
  const x1 = a / tot * (w - 8), x2 = x1 + d / tot * (w - 8), x3 = x2 + hold / tot * (w - 8), x4 = w - 8;
  const top = 6, bot = h - 4, sy = bot - e.sustain * (bot - top);
  const grad = g.createLinearGradient(0, 0, 0, h); grad.addColorStop(0, accent() + '88'); grad.addColorStop(1, accent() + '11');
  g.beginPath(); g.moveTo(4, bot);
  const N = 14;
  for (let i = 1; i <= N; i++) { const t = i / N; g.lineTo(4 + t * x1, bot - (bot - top) * (1 - Math.pow(1 - t, 2.2))); }
  g.lineTo(4 + x1, top);
  for (let i = 1; i <= N; i++) { const t = i / N; g.lineTo(4 + x1 + t * (x2 - x1), sy + (top - sy) * Math.exp(-4 * t)); }
  g.lineTo(4 + x3, sy);
  for (let i = 1; i <= N; i++) { const t = i / N; g.lineTo(4 + x3 + t * (x4 - x3), bot + (sy - bot) * Math.exp(-4 * t)); }
  g.strokeStyle = accent(); g.lineWidth = 1.5; g.stroke(); g.lineWidth = 1;
  g.lineTo(4 + x4, bot); g.lineTo(4, bot); g.fillStyle = grad; g.fill();
}

/* ---------- top bar ---------- */
let topRef = [], macroRow = null;
function userPresets() { try { return JSON.parse(localStorage.getItem('chimeraUser') || '[]'); } catch (e) { return []; } }
function saveUserPresets(l) { try { localStorage.setItem('chimeraUser', JSON.stringify(l)); } catch (e) { toast('Could not store preset in this browser'); } }
// Inside the claude.ai artifact viewer a page can't start downloads itself: it asks the viewer
// through the `downloads` capability. Elsewhere (GitHub Pages, a local file) a plain link works.
let viewerDownloads;
if (window.claude && typeof window.claude.use === 'function')
  viewerDownloads = window.claude.use('downloads').catch(() => null);
async function download(name, data, type) {
  const blob = new Blob([data], { type: type || 'application/octet-stream' });
  const dl = viewerDownloads ? await viewerDownloads : null;
  if (dl) {
    try { await dl.save({ filename: name, data: blob }); }
    catch (e) { toast('Download not saved: ' + (e && e.message || e)); }
    return;
  }
  const a = el('a'); a.href = URL.createObjectURL(blob);
  a.download = name; document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500);
}
function loadPatchObj(obj) {
  patch = obj; dirty = true; flushPatch(); renderAll();
}
const topEls = {};
const DICE_SVG = '<svg width="15" height="15" viewBox="0 0 16 16" aria-hidden="true" style="vertical-align:-3px;margin-right:4px"><rect x="1.5" y="1.5" width="13" height="13" rx="3" fill="none" stroke="currentColor" stroke-width="1.5"/><g fill="currentColor"><circle cx="5" cy="5" r="1.2"/><circle cx="11" cy="5" r="1.2"/><circle cx="8" cy="8" r="1.2"/><circle cx="5" cy="11" r="1.2"/><circle cx="11" cy="11" r="1.2"/></g></svg>';
function buildTop() {
  const t = $('#top'); t.innerHTML = '';
  const r1 = el('div', 'row', t);
  const wm = el('div', 'wordmark', r1, 'CHIMERA'); const ln = el('span', 'wmLine', wm); for (let i = 0; i < 4; i++) el('i', '', ln);
  const pw = el('button', 'pwr' + (powered ? ' on' : ''), r1); pw.id = 'power'; pw.title = 'Power: turn Chimera on/off'; pw.setAttribute('aria-label', 'Power'); pw.setAttribute('aria-pressed', String(powered));
  pw.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" aria-hidden="true"><path d="M7.5 5.6A8.5 8.5 0 1 0 16.5 5.6" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"/><path d="M12 2.5v9" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"/></svg>';
  pw.onclick = () => setPower(!powered);
  const prev = el('button', '', r1, '<'); prev.title = 'Previous preset'; prev.setAttribute('aria-label', 'Previous preset');
  const sel = el('select', '', r1); sel.id = 'preset'; sel.title = 'Preset browser'; sel.setAttribute('aria-label', 'Preset');
  const next = el('button', '', r1, '>'); next.title = 'Next preset'; next.setAttribute('aria-label', 'Next preset');
  const dice = el('button', 'dice', r1); dice.innerHTML = DICE_SVG + 'DICE'; dice.title = 'Click: random factory preset. Shift-click: brand-new random patch.';
  const save = el('button', '', r1, 'Save'); save.title = 'Save the patch to this browser’s user presets';
  const exp = el('button', '', r1, 'Export'); exp.title = 'Download the patch as a .chimera.json file';
  const load = el('button', '', r1, 'Load'); load.title = 'Load a .chimera.json patch file';
  const usr = el('select', '', r1); usr.style.width = '120px'; usr.title = 'User presets saved in this browser'; usr.setAttribute('aria-label', 'User presets');
  const del = el('button', '', r1, 'Del'); del.title = 'Delete the selected user preset';
  const mw = el('div', '', r1); mw.style.width = '170px';
  const old = refreshers; refreshers = topRef = [];
  bar(mw, { label: 'Master', get: () => patch.masterVolume, set: v => patch.masterVolume = v, min: 0, max: 1, def: .7, fmt: pct });
  refreshers = old;
  const mt = el('div', 'meterWrap', r1); mt.innerHTML = 'Voices <b id="voices">0</b><div class="meter" title="Output level"><i id="peak"></i></div>';
  const panic = el('button', '', r1, 'Panic'); panic.title = 'All notes off'; panic.style.color = 'var(--rec)';
  macroRow = el('div', '', t); macroRow.id = 'macros';
  Object.assign(topEls, { sel, usr });
  sel.addEventListener('change', () => { if (+sel.value >= 0) post({ type: 'preset', index: +sel.value }); });
  const step = d => { if (!presets.length) return; const i = (Math.max(presetIndex, 0) + d + presets.length) % presets.length; post({ type: 'preset', index: i }); };
  prev.onclick = () => step(-1); next.onclick = () => step(1);
  // Shift-click (or a long press on a phone) makes a brand-new random patch.
  let diceHold = null, diceLong = false;
  dice.addEventListener('pointerdown', () => { diceLong = false; diceHold = setTimeout(() => { diceLong = true; post({ type: 'randomPatch' }); }, 550); });
  ['pointerup', 'pointerleave', 'pointercancel'].forEach(t => dice.addEventListener(t, () => clearTimeout(diceHold)));
  dice.addEventListener('contextmenu', e => e.preventDefault());
  dice.onclick = e => { if (diceLong) { diceLong = false; return; } post({ type: e.shiftKey ? 'randomPatch' : 'randomPreset' }); };
  dice.title = 'Click: random preset. Shift-click or long-press: brand-new random patch.';
  panic.onclick = () => { post({ type: 'panic' }); heldClear(); };
  save.onclick = () => {
    if (!patch) return; const name = (window.prompt && window.prompt('Preset name', patch.name || 'My Patch')) || null; if (!name) return;
    const p = JSON.parse(JSON.stringify(patch)); p.name = name; const l = userPresets().filter(x => x.name !== name); l.push({ name, patch: p });
    saveUserPresets(l); buildUserList(); usr.value = name;
  };
  exp.onclick = () => patch && download((patch.name || 'chimera').replace(/[^\w\- ]+/g, '_') + '.chimera.json', JSON.stringify(patch, null, 1), 'application/json');
  load.onclick = () => $('#filePatch').click();
  usr.onchange = () => { const e = userPresets().find(x => x.name === usr.value); if (e) loadPatchObj(JSON.parse(JSON.stringify(e.patch))); };
  del.onclick = () => { if (usr.value) { saveUserPresets(userPresets().filter(x => x.name !== usr.value)); buildUserList(); } };
  buildUserList();
}
function buildUserList() {
  const u = topEls.usr; u.innerHTML = ''; const o = el('option', '', u, 'User…'); o.value = '';
  userPresets().forEach(p => { const o2 = el('option', '', u, p.name); o2.value = p.name; });
}
function buildPresetList() {
  const s = topEls.sel; s.innerHTML = ''; const groups = {}; const cu = el('option', '', s, patch ? patch.name : 'Patch'); cu.value = '-1';
  presets.forEach((p, i) => { const c = p.category || 'Other'; if (!groups[c]) groups[c] = el('optgroup', '', s); groups[c].label = c; const o = el('option', '', groups[c], p.name); o.value = i; });
  s.value = String(presetIndex);
}
function buildMacros() {
  macroRow.innerHTML = '';
  const old = refreshers; refreshers = topRef;
  for (let i = 0; i < 4; i++) {
    macroVals[i] = macroVals[i] || 0;
    const b = bar(macroRow, { label: (patch.macroNames && patch.macroNames[i]) || 'Macro ' + (i + 1), min: 0, max: 1, def: 0, fmt: pct,
      get: () => macroVals[i], set: v => { macroVals[i] = v; post({ type: 'macro', index: i, value: v }); }, tip: 'Macro ' + (i + 1) + ' (double-click to reset)' });
  }
  refreshers = old;
}
const macroVals = [0, 0, 0, 0];

/* ---------- tabs ---------- */
function buildTabs() {
  const t = $('#tabs'); t.innerHTML = '';
  document.documentElement.style.setProperty('--acc', accent());
  for (let i = 0; i < 4; i++) {
    const lp = patch.layers[i], on = lp.engine !== 0;
    const d = el('div', 'ltab' + (i === layer ? ' sel' : ''), t); d.style.setProperty('--lc', LAYER_COL[i]); d.setAttribute('role', 'tab'); d.tabIndex = 0;
    d.setAttribute('aria-label', 'Layer ' + (i + 1));
    el('b', '', d, String(i + 1)); el('span', '', d, on ? (ENGINES[lp.engine] || '?') : 'Off');
    const dot = el('button', 'dot' + (on ? ' on' : ''), d); dot.title = on ? 'Switch layer ' + (i + 1) + ' off' : 'Switch layer ' + (i + 1) + ' on'; dot.setAttribute('aria-label', dot.title);
    dot.onclick = e => { e.stopPropagation();
      if (lp.engine !== 0) { lastEngine[i] = lp.engine; lp.engine = 0; } else lp.engine = lastEngine[i] || 1;
      markDirty(); buildTabs(); if (i === layer) renderMain(); };
    d.onclick = () => { layer = i; fmOp = 0; buildTabs(); renderMain(); };
    d.onkeydown = e => { if (e.key === 'Enter') d.click(); };
  }
  const pt = el('div', 'ptabs', t);
  [['sound', 'SOUND'], ['mod', 'MODULATION'], ['fx', 'EFFECTS']].forEach(([k, n]) => {
    const b = el('button', k === page ? 'sel' : '', pt, n); b.onclick = () => { page = k; buildTabs(); renderMain(); };
  });
}

/* ---------- pages ---------- */
function renderAll() { if (!patch) return; buildMacros(); topRef.forEach(f => f()); if (topEls.sel.options.length) { topEls.sel.options[0].textContent = patch.name || 'Patch'; topEls.sel.value = String(presetIndex); } buildTabs(); renderMain(); }
function renderMain() {
  const m = $('#main'); m.innerHTML = ''; redraws.length = 0; refreshers = []; if (!patch) return;
  if (page === 'sound') pageSound(m); else if (page === 'mod') pageMod(m); else pageFx(m);
  requestAnimationFrame(() => redraws.forEach(f => f()));
}
function structural(sel) { sel.addEventListener('change', () => { buildTabs(); renderMain(); }); }

function layerColumn(parent) {
  const p = panel(parent, 'Layer ' + (layer + 1), 'stack');
  const eng = select(p, '', ENGINES.slice(1).map(n => 'Engine: ' + n), () => Math.max(L().engine - 1, 0), v => { L().engine = v + 1; lastEngine[layer] = v + 1; });
  structural(eng);
  const F = (...a) => pbar(p, a[0], L, a[1], a[2]);
  F('Level', 'level', { min: 0, max: 1, def: .75, fmt: pct });
  F('Pan', 'pan', { min: -1, max: 1, def: 0, bipolar: true, fmt: panFmt });
  F('Transpose', 'transpose', { min: -48, max: 48, def: 0, int: true, fmt: v => v + ' st' });
  F('Fine', 'fine', { min: -100, max: 100, def: 0, bipolar: true, int: true, fmt: v => v + ' ct' });
  select(p, '', VOICE_MODES.map(n => 'Mode: ' + n), () => L().mode, v => L().mode = v);
  F('Polyphony', 'polyphony', { min: 1, max: 32, def: 16, int: true });
  F('Glide', 'glide', { min: 0, max: 10, curve: 'sq', def: 0, fmt: secFmt });
  F('Unison', 'unison', { min: 1, max: 7, def: 1, int: true });
  F('Detune', 'unisonDetune', { min: 0, max: 1, def: .2, fmt: pct });
  F('Spread', 'unisonSpread', { min: 0, max: 1, def: .6, fmt: pct });
  F('Vel sens', 'velocitySens', { min: 0, max: 1, def: .6, fmt: pct });
  const kz = el('div', 'g2', p); const nf = v => noteName(v);
  pbar(kz, 'Key lo', L, 'keyLo', { min: 0, max: 127, int: true, def: 0, fmt: nf }); pbar(kz, 'Key hi', L, 'keyHi', { min: 0, max: 127, int: true, def: 127, fmt: nf });
  pbar(kz, 'Vel lo', L, 'velLo', { min: 1, max: 127, int: true, def: 1 }); pbar(kz, 'Vel hi', L, 'velHi', { min: 1, max: 127, int: true, def: 127 });
  select(p, '', OUTPUTS.map(n => 'Output: ' + n), () => L().output, v => L().output = v);
  const cp = el('div', 'g2', p);
  const bc = el('button', '', cp, 'Copy'); bc.title = 'Copy this layer’s settings';
  const bp = el('button', '', cp, 'Paste'); bp.title = 'Paste copied settings into this layer';
  bc.onclick = () => { clipboardLayer = JSON.stringify(L()); };
  bp.onclick = () => { if (clipboardLayer) { patch.layers[layer] = JSON.parse(clipboardLayer); markDirty(); buildTabs(); renderMain(); } };
}

function pageSound(m) {
  const eb = el('div', 'engBar', m); el('span', 'engLbl', eb, 'ENGINE'); eb.style.setProperty('--lc', LAYER_COL[layer]);
  const eg = el('div', 'engBtns', eb);
  [1, 2, 3, 4, 5, 0].forEach(v => {
    const b = el('button', 'engBtn' + (v === 0 ? ' off' : '') + (L().engine === v ? ' sel' : ''), eg, v === 0 ? 'Off' : ENGINES[v]); b.dataset.engine = v;
    b.onclick = () => { if (L().engine === v) return; if (L().engine) lastEngine[layer] = L().engine; L().engine = v; if (v) lastEngine[layer] = v; markDirty(); buildTabs(); renderMain(); };
  });
  const cols = el('div', 'cols', m);
  layerColumn(cols);
  const right = el('div', 'stack', cols);
  const eng = L().engine;
  if (eng === 0) { const p = panel(right, 'Engine - Off'); el('div', 'cap', p, 'This layer is switched off. Use the dot in the layer tab or pick an engine.'); }
  else if (eng === 1) engAnalog(right);
  else if (eng === 2) engWavetable(right);
  else if (eng === 3) engFm(right);
  else if (eng === 4) engSampler(right);
  else engPluck(right);
  const row = el('div', 'g4', right); row.style.gridTemplateColumns = '1.2fr 1fr 1fr 1fr';
  const fp = panel(row, 'Filter');
  select(fp, '', FILTERS.map(n => 'Type: ' + n), () => L().filter, v => L().filter = v);
  pbar(fp, 'Cutoff', L, 'cutoff', { min: 20, max: 20000, curve: 'exp', def: 12000, fmt: hzFmt });
  pbar(fp, 'Resonance', L, 'resonance', { min: 0, max: 1, def: .15, fmt: pct });
  pbar(fp, 'Drive', L, 'drive', { min: 0, max: 1, def: 0, fmt: pct });
  pbar(fp, 'Key track', L, 'keyTrack', { min: 0, max: 1, def: 0, fmt: pct });
  pbar(fp, 'Env amount', L, 'filterEnvAmount', { min: -1, max: 1, def: 0, bipolar: true, fmt: v => Math.round(v * 60) + ' st' });
  [['Amp envelope', 'ampEnv'], ['Filter envelope', 'filterEnv'], ['Mod envelope', 'modEnv']].forEach(([t, k]) => {
    const p = panel(row, t);
    canvas(p, 40, (g, w, h) => drawEnv(g, w, h, L()[k]));
    const E = () => L()[k];
    pbar(p, 'Attack', E, 'attack', { min: .001, max: 10, curve: 'exp', def: .003, fmt: secFmt });
    pbar(p, 'Decay', E, 'decay', { min: .001, max: 10, curve: 'exp', def: .4, fmt: secFmt });
    pbar(p, 'Sustain', E, 'sustain', { min: 0, max: 1, def: .8, fmt: pct });
    pbar(p, 'Release', E, 'release', { min: .001, max: 10, curve: 'exp', def: .25, fmt: secFmt });
  });
}

function engAnalog(parent) {
  const p = panel(parent, 'Engine - Analog');
  const A = () => L().analog;
  const g = el('div', 'g3', p);
  const osc = (n, wk, pwk) => {
    const c = el('div', 'stack', g); el('h4', '', c, 'Osc ' + n);
    select(c, '', WAVES.map(w => 'Wave: ' + w), () => A()[wk], v => A()[wk] = v);
    pbar(c, 'Pulse width', A, pwk, { min: .05, max: .95, def: .5, fmt: pct });
    return c;
  };
  const c1 = osc(1, 'wave1', 'pw1'); const c2 = osc(2, 'wave2', 'pw2');
  pbar(c2, 'Semitones', A, 'osc2Semi', { min: -24, max: 24, def: 0, int: true, bipolar: true, fmt: v => v + ' st' });
  pbar(c2, 'Fine', A, 'osc2Fine', { min: -100, max: 100, def: 0, bipolar: true, int: true, fmt: v => v + ' ct' });
  const f = flag(A, 'sync'); toggle(c2, 'Hard sync to osc 1', f.get, f.set);
  const c3 = el('div', 'stack', g); el('h4', '', c3, 'Mix');
  pbar(c3, 'Osc 1 / 2', A, 'mix', { min: 0, max: 1, def: 0, fmt: pct });
  pbar(c3, 'Sub', A, 'sub', { min: 0, max: 1, def: 0, fmt: pct });
  pbar(c3, 'Noise', A, 'noise', { min: 0, max: 1, def: 0, fmt: pct });
  const g2 = el('div', 'g3', p);
  canvas(g2, 80, (c, w, h) => drawWave(c, w, h, t => waveShape(A().wave1, t * 2, A().pw1), accent()));
  canvas(g2, 80, (c, w, h) => drawWave(c, w, h, t => waveShape(A().wave2, t * 2 * Math.pow(2, A().osc2Semi / 12), A().pw2), accent()));
  canvas(g2, 80, (c, w, h) => { const a = A(), m = a.mix * Math.PI / 2;
    drawWave(c, w, h, t => (Math.cos(m) * waveShape(a.wave1, t * 2, a.pw1) + Math.sin(m) * waveShape(a.wave2, t * 2 * Math.pow(2, a.osc2Semi / 12), a.pw2) + a.sub * .5 * waveShape(3, t, .5) + a.noise * .3 * waveShape(4, t)) * .8, accent()); });
}
function engWavetable(parent) {
  const p = panel(parent, 'Engine - Wavetable');
  select(p, '', WT_TABLES.map(n => 'Table: ' + n), () => L().wtTable, v => L().wtTable = v);
  pbar(p, 'Position', L, 'wtPosition', { min: 0, max: 1, def: 0, fmt: pct });
  canvas(p, 80, (g, w, h) => {
    const pos = L().wtPosition, n = 16; g.strokeStyle = '#222'; g.strokeRect(.5, .5, w - 1, h - 1);
    for (let i = 0; i < n; i++) { const act = Math.abs(i / (n - 1) - pos) < .5 / (n - 1); g.fillStyle = act ? accent() : '#1d2a30'; g.fillRect(8 + i * (w - 16) / n, h - 16, (w - 16) / n - 3, 8); }
    const tb = L().wtTable, mix = pos * 3; g.strokeStyle = accent(); g.lineWidth = 1.5; g.beginPath();
    for (let x = 0; x <= w; x++) { const t = x / w * 2; const s = Math.sin(2 * Math.PI * t) * (1 - Math.min(mix, 1)) + (2 * (t % 1) - 1) * Math.min(Math.max(mix - 1, 0), 1) * .8 + Math.sin(2 * Math.PI * t * (tb + 2)) * .35 * Math.min(mix, 1);
      const y = (h - 18) / 2 - clamp(s, -1, 1) * ((h - 18) / 2 - 6) ; x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke(); g.lineWidth = 1;
  }).title = 'Schematic view of the table position';
}
function engPluck(parent) {
  const p = panel(parent, 'Engine - Pluck'); const g = el('div', 'g2', p);
  const PL = () => L().pluck;
  pbar(g, 'Decay', PL, 'decay', { min: 0, max: 1, def: .5, fmt: pct });
  pbar(g, 'Damping', PL, 'damping', { min: 0, max: 1, def: .5, fmt: pct });
  pbar(g, 'Pick position', PL, 'pickPos', { min: 0, max: .5, def: .2, fmt: pct });
  pbar(g, 'Noise colour', PL, 'noiseColor', { min: 0, max: 1, def: .5, fmt: pct });
  canvas(p, 70, (g2, w, h) => { const d = PL(); g2.strokeStyle = accent(); g2.beginPath(); const ring = .2 + d.decay * 5;
    for (let x = 0; x < w; x++) { const t = x / w * 3; const y = h / 2 - Math.sin(t * 60) * Math.exp(-t / ring * 1.5) * (h / 2 - 6) * (1 - d.damping * .3); x ? g2.lineTo(x, y) : g2.moveTo(x, y); } g2.stroke(); });
}

/* ---------- sampler ---------- */
function engSampler(parent) {
  const p = panel(parent, 'Engine - Sampler');
  const top = el('div', 'row', p);
  const lb = el('button', '', top, 'Load sample'); lb.title = 'Load an audio file (WAV, AIFF, FLAC, MP3, Ogg). You can also drop files on the page.';
  lb.onclick = () => $('#fileSample').click();
  const names = patch.sampleNames || [], roots = patch.sampleRoots || [];
  const nm = names[layer], v = sampleViews[layer];
  const info = el('div', 'cap b', top, nm ? nm + (v ? '   ' + v.dur.toFixed(2) + ' s  ' + (v.stereo ? 'stereo' : 'mono') : '') + '   root ' + noteName(roots[layer] != null ? roots[layer] : 60) : 'No sample loaded');
  info.style.flex = '1';
  const dz = el('div', 'drop', p, 'Drop an audio file here to load it into layer ' + (layer + 1));
  dz.addEventListener('click', () => $('#fileSample').click());
  const g = el('div', 'g5', p); const S = () => L().sampler;
  select(g, '', LOOPS.map(n => 'Loop: ' + n), () => S().loop, x => S().loop = x);
  const rv = flag(S, 'reverse'); toggle(g, 'Reverse', rv.get, rv.set);
  pbar(g, 'Start', S, 'start', { min: 0, max: 1, def: 0, fmt: x => (x * 100).toFixed(1) + '%' });
  pbar(g, 'Loop start', S, 'loopStart', { min: 0, max: 1, def: 0, fmt: x => (x * 100).toFixed(1) + '%' });
  pbar(g, 'Loop end', S, 'loopEnd', { min: 0, max: 1, def: 1, fmt: x => (x * 100).toFixed(1) + '%' });
  canvas(p, 90, (c, w, h) => {
    const pk = sampleViews[layer]; c.strokeStyle = '#222'; c.beginPath(); c.moveTo(0, h / 2); c.lineTo(w, h / 2); c.stroke();
    if (pk) { c.fillStyle = accent(); const n = pk.peaks.length; for (let x = 0; x < w; x++) { const a = pk.peaks[Math.floor(x / w * n)] * (h / 2 - 4); c.fillRect(x, h / 2 - a, 1, a * 2 + 1); } }
    const s = S(); const mk = (f, col) => { c.strokeStyle = col; c.beginPath(); c.moveTo(Math.round(f * w) + .5, 0); c.lineTo(Math.round(f * w) + .5, h); c.stroke(); };
    mk(s.start, '#ffffff'); if (s.loop) { mk(s.loopStart, '#ffd60a'); mk(s.loopEnd, '#ffd60a'); }
  });
}
async function loadAudioFile(file) {
  if (!audioCtx) { toast('Start audio first'); return; }
  try {
    const ab = await file.arrayBuffer(); const buf = await audioCtx.decodeAudioData(ab);
    const left = new Float32Array(buf.getChannelData(0)); const right = buf.numberOfChannels > 1 ? new Float32Array(buf.getChannelData(1)) : null;
    const n = 600, peaks = new Float32Array(n), step = Math.max(1, Math.floor(left.length / n));
    for (let i = 0; i < n; i++) { let m = 0; for (let j = i * step, e = Math.min(left.length, j + step); j < e; j += Math.max(1, step >> 5)) m = Math.max(m, Math.abs(left[j])); peaks[i] = m; }
    sampleViews[layer] = { peaks, dur: buf.duration, stereo: !!right };
    const ly = layer;
    if (patch.layers[ly].engine !== 4) { patch.layers[ly].engine = 4; lastEngine[ly] = 4; dirty = true; }
    flushPatch();
    const tr = [left.buffer]; if (right) tr.push(right.buffer);
    post({ type: 'sample', layer: ly, left, right, sampleRate: buf.sampleRate, name: file.name }, tr);
    buildTabs(); renderMain();
  } catch (e) { toast('Could not decode audio file: ' + (e && e.message || e)); }
}

/* ---------- FM ---------- */
const OP_X = [0, 1, 2, 3, 4, 5];
function algLevels(a) {
  const lv = [0, 0, 0, 0, 0, 0];
  for (let pass = 0; pass < 6; pass++) for (let j = 0; j < 6; j++) for (let i = 0; i < 6; i++) if (a[0][j] & (1 << i)) lv[i] = Math.max(lv[i], lv[j] + 1);
  return lv;
}
function drawAlgo(svg, ai, sel, onPick) {
  const a = ALGS[clamp(ai, 0, 31)], lv = algLevels(a), W = 220, H = 190; const maxL = Math.max(...lv);
  const pos = [];
  for (let i = 0; i < 6; i++) { let cnt = 0, rank = 0; for (let k = 0; k < 6; k++) if (lv[k] === lv[i]) { cnt++; if (k < i) rank++; }
    pos.push([12 + (W - 24) * (rank + .5) / cnt, 12 + (H - 24) * (maxL - lv[i] + .5) / (maxL + 1)]); }
  let s = '';
  for (let j = 0; j < 6; j++) for (let i = 0; i < 6; i++) if (a[0][j] & (1 << i)) s += `<line x1="${pos[i][0]}" y1="${pos[i][1]}" x2="${pos[j][0]}" y2="${pos[j][1]}" stroke="#8a8a92" stroke-width="1.3"/>`;
  const col = accent();
  for (let i = 0; i < 6; i++) {
    const [x, y] = pos[i], car = a[1] & (1 << i);
    if (a[3] === i) { s += `<path d="M${x + 6} ${y - 12} a7 7 0 1 1 7 7" fill="none" stroke="#ff9f0a" stroke-width="1.4"/>`;
      if (a[2] !== i) s += `<line x1="${x}" y1="${y}" x2="${pos[a[2]][0]}" y2="${pos[a[2]][1]}" stroke="#ff9f0a" stroke-dasharray="3 3"/>`; }
    s += `<g data-op="${i}" style="cursor:pointer"><rect x="${x - 11}" y="${y - 11}" width="22" height="22" rx="4" fill="${car ? col + 'dd' : '#17171b'}" stroke="${i === sel ? '#fff' : car ? col : '#8a8a92'}" stroke-width="${i === sel ? 2 : 1}"/><text x="${x}" y="${y + 4.5}" text-anchor="middle" font-size="12" font-weight="700" fill="${car ? '#000' : '#e8e8ea'}">${i + 1}</text></g>`;
  }
  svg.innerHTML = s;
  svg.querySelectorAll('[data-op]').forEach(g => g.addEventListener('click', () => onPick(+g.dataset.op)));
}
function drawDxEg(g, w, h, op) {
  const lv = [op.egLevel[0], op.egLevel[1], op.egLevel[2], op.egLevel[3]], r = op.egRate;
  const seg = (a, b, rt) => (Math.abs(a - b) / 99 + .03) * Math.pow(1.05, 99 - rt);
  const d = [seg(lv[3], lv[0], r[0]), seg(lv[0], lv[1], r[1]), seg(lv[1], lv[2], r[2])], hold = Math.max(.15, d.reduce((x, y) => x + y, 0) * .2), d4 = seg(lv[2], lv[3], r[3]);
  const tot = d[0] + d[1] + d[2] + hold + d4, X = t => 6 + t / tot * (w - 12), Y = v => h - 6 - v / 99 * (h - 14);
  const pts = [[0, lv[3]], [d[0], lv[0]], [d[0] + d[1], lv[1]], [d[0] + d[1] + d[2], lv[2]], [d[0] + d[1] + d[2] + hold, lv[2]], [tot, lv[3]]];
  g.beginPath(); pts.forEach((p, i) => i ? g.lineTo(X(p[0]), Y(p[1])) : g.moveTo(X(p[0]), Y(p[1])));
  g.strokeStyle = accent(); g.lineWidth = 1.5; g.stroke(); g.lineWidth = 1;
  g.fillStyle = '#e8e8ea'; pts.slice(1, 4).forEach(p => { g.beginPath(); g.arc(X(p[0]), Y(p[1]), 2.5, 0, 7); g.fill(); });
}
function engFm(parent) {
  const wrap = panel(parent, 'Engine - FM'); const grid = el('div', '', wrap);
  grid.style.cssText = 'display:grid;grid-template-columns:230px 1fr;gap:10px;';
  const left = el('div', 'stack', grid), right = el('div', 'stack', grid);
  const FM = () => L().fm; const OP = () => FM().op[fmOp];
  const svgW = el('div', '', left); svgW.style.cssText = 'background:#000;border:1px solid var(--outline);border-radius:4px';
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); svg.setAttribute('viewBox', '0 0 220 190'); svg.style.cssText = 'width:100%;display:block';
  svg.setAttribute('aria-label', 'Algorithm diagram'); svgW.appendChild(svg);
  const pick = i => { fmOp = i; renderMain(); };
  const redrawAlgo = () => drawAlgo(svg, FM().algorithm, fmOp, pick); redraws.push(redrawAlgo); redrawAlgo();
  const ar = el('div', 'row', left); const b1 = el('button', '', ar, '<'), b2 = el('button', '', ar, '>'); b1.title = 'Previous algorithm'; b2.title = 'Next algorithm';
  const abar = pbar(ar, 'Algorithm', FM, 'algorithm', { min: 0, max: 31, int: true, def: 0, fmt: v => v + 1 }); abar.style.flex = '1';
  b1.onclick = () => { FM().algorithm = (FM().algorithm + 31) % 32; refreshers.forEach(f => f()); edited(); };
  b2.onclick = () => { FM().algorithm = (FM().algorithm + 1) % 32; refreshers.forEach(f => f()); edited(); };
  pbar(left, 'Feedback', FM, 'feedback', { min: 0, max: 7, int: true, def: 0 });
  pbar(left, 'FM amount', L, 'fmAmount', { min: 0, max: 2, def: 1, fmt: v => v.toFixed(2) });
  const ua = flag(L, 'useAmpEnv'); toggle(left, 'Use amp envelope', ua.get, ua.set, 'FM layers usually let the operator envelopes shape the sound');
  const br = el('div', 'g2', left);
  const lc = el('button', '', br, 'Load cart (.syx)'); lc.onclick = () => $('#fileCart').click();
  const sc = el('button', '', br, 'Save cart'); sc.onclick = () => post({ type: 'saveCart', layer }); sc.title = 'Download the loaded cart as a .syx file';
  const cc = el('button', '', left, 'Chimera cart'); cc.title = 'Load Chimera’s own 32-voice cart'; cc.onclick = () => { if (L().engine !== 3) { L().engine = 3; flushOnce(); } post({ type: 'builtinCart', layer }); };
  el('div', 'cap', left, (patch.cartNames && patch.cartNames[layer]) ? 'Cart: ' + patch.cartNames[layer] : 'No cart loaded');
  el('div', 'cap b', left, (FM().name || '').toString().trim());
  // operator editor
  const tabs = el('div', 'opTabs', right);
  for (let i = 0; i < 6; i++) { const b = el('button', i === fmOp ? 'sel' : '', tabs, String(i + 1)); b.onclick = () => pick(i); b.title = 'Operator ' + (i + 1); }
  const isCar = !!(ALGS[FM().algorithm][1] & (1 << fmOp));
  el('div', 'cap', right, 'OPERATOR ' + (fmOp + 1) + (isCar ? '  (carrier)' : '  (modulator)'));
  const eg = el('div', '', right); eg.style.cssText = 'display:grid;grid-template-columns:1.1fr 1fr 1fr;gap:10px';
  const c1 = el('div', 'stack', eg), c2 = el('div', 'stack', eg), c3 = el('div', 'stack', eg);
  select(c1, '', ['Mode: Ratio', 'Mode: Fixed'], () => OP().oscMode, v => OP().oscMode = v);
  const O = (p, l, k, o) => pbar(p, l, OP, k, o);
  O(c1, 'Coarse', 'freqCoarse', { min: 0, max: 31, int: true, def: 1 });
  O(c1, 'Fine', 'freqFine', { min: 0, max: 99, int: true, def: 0 });
  O(c1, 'Detune', 'detune', { min: 0, max: 14, int: true, def: 7, bipolar: true, fmt: v => (v - 7 > 0 ? '+' : '') + (v - 7) });
  O(c1, 'Level', 'outputLevel', { min: 0, max: 99, int: true, def: 99 });
  O(c1, 'Vel sens', 'keyVelSens', { min: 0, max: 7, int: true, def: 0 });
  O(c1, 'Amp mod sens', 'ampModSens', { min: 0, max: 3, int: true, def: 0 });
  const cv = canvas(c2, 110, (g, w, h) => drawDxEg(g, w, h, OP())); cv.title = 'Operator envelope';
  for (let i = 0; i < 4; i++) {
    const r = el('div', 'g2', c2);
    pbar(r, 'R' + (i + 1), () => OP().egRate, i, { min: 0, max: 99, int: true, def: 99 });
    pbar(r, 'L' + (i + 1), () => OP().egLevel, i, { min: 0, max: 99, int: true, def: i === 3 ? 0 : 99 });
  }
  el('h4', '', c3, 'Key scaling');
  O(c3, 'Break point', 'kbdBreakPoint', { min: 0, max: 99, int: true, def: 39, fmt: v => noteName(v + 21) });
  O(c3, 'Left depth', 'kbdLeftDepth', { min: 0, max: 99, int: true, def: 0 });
  O(c3, 'Right depth', 'kbdRightDepth', { min: 0, max: 99, int: true, def: 0 });
  select(c3, 'L', KB_CURVES, () => OP().kbdLeftCurve, v => OP().kbdLeftCurve = v, 'Left curve');
  select(c3, 'R', KB_CURVES, () => OP().kbdRightCurve, v => OP().kbdRightCurve = v, 'Right curve');
  O(c3, 'Rate scaling', 'kbdRateScaling', { min: 0, max: 7, int: true, def: 0 });
  // cart grid
  const vp = panel(parent, 'Cart voices'); const hd = vp.firstChild;
  el('span', 'sp', hd);
  const dice = el('button', '', hd); dice.innerHTML = DICE_SVG + 'DICE'; dice.title = 'Click: random voice from the cart. Shift-click: pick a random .syx from a folder you choose, then a random voice.';
  dice.onclick = e => { if (e.shiftKey) $('#fileDir').click(); else post({ type: 'randomCartVoice', layer }); };
  const grid2 = el('div', 'cartGrid', vp); const vs = (patch.cartVoices && patch.cartVoices[layer]) || [];
  const cur = (FM().name || '').toString().trim();
  for (let i = 0; i < 32; i++) {
    const nmv = (vs[i] || '').toString().trim(); const b = el('button', (lastVoice[layer] === i || (lastVoice[layer] < 0 && nmv && nmv === cur)) ? 'sel' : '', grid2);
    el('small', '', b, String(i + 1).padStart(2, '0')); b.appendChild(document.createTextNode(nmv || '-')); b.title = nmv || 'empty';
    b.onclick = () => { if (!vs.length) { toast('Load a cart first'); return; } lastVoice[layer] = i; post({ type: 'cartVoice', layer, index: i }); };
  }
}
function flushOnce() { dirty = true; flushPatch(); buildTabs(); }
async function loadCartFile(file) {
  const bytes = await file.arrayBuffer(); const ly = layer;
  if (patch.layers[ly].engine !== 3) { patch.layers[ly].engine = 3; lastEngine[ly] = 3; dirty = true; flushPatch(); buildTabs(); }
  lastVoice[ly] = -1; post({ type: 'cart', layer: ly, bytes, name: file.name }, [bytes]);
}
async function randomFromDir(files) {
  const syx = Array.from(files).filter(f => /\.syx$/i.test(f.name));
  if (!syx.length) { toast('No .syx files in that folder'); return; }
  const f = syx[Math.floor(Math.random() * syx.length)]; await loadCartFile(f); post({ type: 'randomCartVoice', layer });
  toast('Random cart: ' + f.name);
}

/* ---------- modulation page ---------- */
function lfoPanel(parent, title, objFn, isLayer) {
  const p = panel(parent, title); const O = objFn;
  select(p, '', LFO_SHAPES.map(n => 'Shape: ' + n), () => O().shape, v => O().shape = v);
  canvas(p, 44, (g, w, h) => { const sh = O().shape; const rnd = t => Math.sin(Math.floor(t * 8) * 78.233) * 43758.5453 % 1;
    drawWave(g, w, h, t => { t *= 2; switch (sh) { case 0: return Math.sin(t * 2 * Math.PI); case 1: return waveShape(1, t); case 2: return waveShape(2, t); case 3: return -waveShape(2, t);
      case 4: return waveShape(3, t, .5); case 5: return rnd(t); default: return Math.sin(t * 5.1) * .6 + Math.sin(t * 11.7) * .3; } }, accent()); });
  const sync = flag(O, 'sync'); const sb = toggle(p, 'Tempo sync', sync.get, sync.set); sb.addEventListener('click', () => renderMain());
  if (O().sync) select(p, '', BEATS.map(beatName), () => { let bi = 0; BEATS.forEach((b, i) => { if (Math.abs(b - O().beats) < Math.abs(BEATS[bi] - O().beats)) bi = i; }); return bi; }, v => O().beats = BEATS[v], 'Cycle length');
  else pbar(p, 'Rate', O, 'rateHz', { min: .01, max: 40, curve: 'exp', def: 2, fmt: v => (v < 10 ? v.toFixed(2) : v.toFixed(1)) + ' Hz' });
  if (isLayer) { const rt = flag(O, 'retrigger'); toggle(p, 'Retrigger on note', rt.get, rt.set); pbar(p, 'Fade in', O, 'fadeIn', { min: 0, max: 10, curve: 'sq', def: 0, fmt: secFmt }); }
}
function pageMod(m) {
  const row = el('div', 'g4', m);
  for (let i = 0; i < 2; i++) lfoPanel(row, 'Layer ' + (layer + 1) + ' - LFO ' + (i + 1), () => L().lfo[i], true);
  for (let i = 0; i < 2; i++) lfoPanel(row, 'Global LFO ' + (i + 1), () => patch.globalLfo[i], false);
  const mp = panel(m, 'Mod matrix - layer ' + (layer + 1));
  const hd = mp.firstChild; el('span', 'sp', hd);
  const g = el('div', '', mp); g.style.cssText = 'display:grid;grid-template-columns:repeat(2,1fr);gap:6px 14px';
  for (let i = 0; i < 8; i++) {
    const r = el('div', '', g); r.style.cssText = 'display:grid;grid-template-columns:1fr 1fr 1.1fr;gap:5px';
    const S = () => L().mods[i];
    select(r, '', MOD_SRCS, () => S().src, v => S().src = v, 'Slot ' + (i + 1) + ' source');
    select(r, '', MOD_DSTS, () => S().dst, v => S().dst = v, 'Slot ' + (i + 1) + ' destination');
    pbar(r, 'Amount', S, 'amount', { min: -1, max: 1, def: 0, bipolar: true, fmt: v => (v > 0 ? '+' : '') + Math.round(v * 100) + '%' });
  }
  const bp = panel(m, 'Pitch bend'); bp.style.maxWidth = '320px';
  pbar(bp, 'Bend range', () => patch, 'bendRange', { min: 0, max: 24, int: true, def: 2, fmt: v => v + ' st' }); 
}

/* ---------- effects ---------- */
function pageFx(m) {
  const row = el('div', 'g3', m);
  const fxPanel = (title, objFn, build) => {
    const p = panel(row, title); const h = p.firstChild; el('span', 'sp', h);
    const f = flag(objFn, 'on'); const on = el('button', 'tog', h); on.setAttribute('aria-label', title + ' on/off');
    const r = () => { on.textContent = f.get() ? 'ON' : 'OFF'; on.classList.toggle('on', f.get()); }; on.onclick = () => { f.set(!f.get()); r(); edited(); }; r(); refreshers.push(r);
    build(p, objFn); return p;
  };
  fxPanel('Chorus', () => patch.chorus, (p, O) => {
    pbar(p, 'Rate', O, 'rate', { min: .05, max: 5, curve: 'exp', def: .4, fmt: v => v.toFixed(2) + ' Hz' });
    pbar(p, 'Depth', O, 'depth', { min: 0, max: 1, def: .5, fmt: pct }); pbar(p, 'Mix', O, 'mix', { min: 0, max: 1, def: .35, fmt: pct });
  });
  fxPanel('Delay', () => patch.delay, (p, O) => {
    const sy = flag(O, 'sync'); const sb = toggle(p, 'Tempo sync', sy.get, sy.set); sb.addEventListener('click', () => renderMain());
    if (O().sync) select(p, '', BEATS.slice(0, 11).map(beatName), () => { let bi = 0; BEATS.slice(0, 11).forEach((b, i) => { if (Math.abs(b - O().beats) < Math.abs(BEATS[bi] - O().beats)) bi = i; }); return bi; }, v => O().beats = BEATS[v], 'Delay time');
    else pbar(p, 'Time', O, 'ms', { min: 1, max: 2000, curve: 'exp', def: 375, fmt: v => Math.round(v) + ' ms' });
    pbar(p, 'Feedback', O, 'feedback', { min: 0, max: .95, def: .35, fmt: pct }); pbar(p, 'Tone', O, 'tone', { min: 0, max: 1, def: .6, fmt: pct });
    pbar(p, 'Mix', O, 'mix', { min: 0, max: 1, def: .25, fmt: pct });
    const pp = flag(O, 'pingPong'); toggle(p, 'Ping-pong', pp.get, pp.set);
  });
  fxPanel('Reverb', () => patch.reverb, (p, O) => {
    pbar(p, 'Size', O, 'size', { min: 0, max: 1, def: .6, fmt: pct }); pbar(p, 'Damping', O, 'damping', { min: 0, max: 1, def: .5, fmt: pct });
    pbar(p, 'Pre-delay', O, 'predelayMs', { min: 0, max: 200, def: 10, fmt: v => Math.round(v) + ' ms' });
    pbar(p, 'Width', O, 'width', { min: 0, max: 1, def: 1, fmt: pct }); pbar(p, 'Mix', O, 'mix', { min: 0, max: 1, def: .25, fmt: pct });
  });
  const np = panel(m, 'Master'); np.style.maxWidth = '320px';
  pbar(np, 'Volume', () => patch, 'masterVolume', { min: 0, max: 1, def: .7, fmt: pct });
}

/* ---------- keyboard / MIDI ---------- */
let kbBase = 60, kbVel = 100, held = {}; const keyEls = {};
var powered = true;
function midiOut(a) { if (!powered) return; if (node) post({ type: 'midi', data: a }); }
function vis(n, on) { const k = keyEls[n]; if (k) k.classList.toggle('down', on); }
var kbOctaves = 4;
function noteOn(src, n) { if (!powered) return; if (n < 0 || n > 127) return; if (held[src] === n) return; if (held[src] != null) noteOff(src); held[src] = n; vis(n, true); midiOut([0x90, n, kbVel]); }
function noteOff(src) { const n = held[src]; if (n == null) return; delete held[src]; if (!Object.values(held).includes(n)) vis(n, false); midiOut([0x80, n, 0]); }
function setPower(on) {
  powered = on;
  const b = $('#power'); if (b) { b.classList.toggle('on', on); b.setAttribute('aria-pressed', String(on)); }
  if (on) { unlockAudio(); return; }
  post({ type: 'panic' }); releaseAllKeys();
  const v = $('#voices'); if (v) v.textContent = '0'; const p = $('#peak'); if (p) p.style.width = '0%';
  try { if (silentEl) silentEl.pause(); } catch (e) {}
  if (audioCtx && audioCtx.state === 'running') { try { const pr = audioCtx.suspend(); if (pr && pr.catch) pr.catch(() => {}); } catch (e) {} }
}
function releaseAllKeys() { Object.keys(held).forEach(k => vis(held[k], false)); held = {}; Object.keys(kbPtr).forEach(k => delete kbPtr[k]); document.querySelectorAll('.key.down').forEach(k => k.classList.remove('down')); }
var kbPtr = {};
function heldClear() { Object.keys(held).forEach(k => vis(held[k], false)); held = {}; }
function buildKeys() {
  const f = $('#keys'); f.innerHTML = '';
  const ctl = el('div', '', f); ctl.id = 'kbCtl';
  const dn = el('button', '', ctl, 'Oct −'), up = el('button', '', ctl, 'Oct +'); dn.title = 'Octave down (Z)'; up.title = 'Octave up (X)';
  const oc = el('span', '', ctl); oc.id = 'octLabel';
  const vl = el('label', '', ctl, 'Velocity'); const vs = el('input', '', vl); vs.type = 'range'; vs.min = 1; vs.max = 127; vs.value = kbVel; vs.title = 'Velocity for on-screen and computer keyboard';
  const vo = el('span', '', vl, String(kbVel)); vs.oninput = () => { kbVel = +vs.value; vo.textContent = kbVel; };
  const ml = el('label', '', ctl, 'Mod'); const ms = el('input', '', ml); ms.type = 'range'; ms.min = 0; ms.max = 127; ms.value = 0; ms.title = 'Mod wheel (CC1)';
  ms.oninput = () => midiOut([0xB0, 1, +ms.value]);
  const pl = el('label', '', ctl, 'Bend'); const ps = el('input', '', pl); ps.type = 'range'; ps.min = -8192; ps.max = 8191; ps.value = 0; ps.title = 'Pitch bend (springs back)';
  let bendV = 0; const sendBend = v => { const u = Math.round(v) + 8192; midiOut([0xE0, u & 127, (u >> 7) & 127]); };
  ps.oninput = () => { bendV = +ps.value; sendBend(bendV); };
  const spring = () => { const t0 = performance.now(), v0 = +ps.value; const step = () => { const k = Math.min(1, (performance.now() - t0) / 150); const v = v0 * (1 - k); ps.value = v; sendBend(v); if (k < 1) requestAnimationFrame(step); }; step(); };
  ps.addEventListener('pointerup', spring); ps.addEventListener('pointercancel', spring); ps.addEventListener('keyup', spring);
  const mi = el('select', '', ctl); mi.id = 'midiSel'; mi.title = 'MIDI input device'; mi.setAttribute('aria-label', 'MIDI input'); el('option', '', mi, 'MIDI: not available').value = '';
  const kb = el('div', '', f); kb.id = 'kb'; kb.setAttribute('aria-label', 'On-screen keyboard');
  function rebuild() {
    kb.innerHTML = ''; Object.keys(keyEls).forEach(k => delete keyEls[k]);
    // fewer, bigger keys on narrow screens (phones): 2 octaves under 560 px, 3 under 900, else 4
    const w = kb.clientWidth || window.innerWidth, oct = w < 560 ? 2 : w < 900 ? 3 : 4;
    kbOctaves = oct;
    const start = kbBase - 12, W = 7 * oct; let wi = 0; oc.textContent = noteName(start) + ' – ' + noteName(start + oct * 12 - 1);
    const blacks = [];
    for (let n = start; n < start + oct * 12; n++) {
      const pc = n % 12, black = [1, 3, 6, 8, 10].includes(pc);
      if (!black) { const k = el('div', 'key' + (pc === 0 ? ' c' : ''), kb); k.style.left = wi / W * 100 + '%'; k.style.width = 100 / W + '%'; k.dataset.note = n; if (pc === 0) k.dataset.n = noteName(n); keyEls[n] = k; wi++; }
      else blacks.push([n, wi]);
    }
    blacks.forEach(([n, w]) => { const k = el('div', 'key blk', kb); k.style.left = (w / W * 100 - 100 / W * .3) + '%'; k.style.width = 100 / W * .6 + '%'; k.dataset.note = n; keyEls[n] = k; });
    Object.values(held).forEach(n => vis(n, true));
  }
  rebuild();
  let resizeT = null;
  window.addEventListener('resize', () => { clearTimeout(resizeT); resizeT = setTimeout(() => {
    const w = kb.clientWidth || window.innerWidth, oct = w < 560 ? 2 : w < 900 ? 3 : 4;
    if (oct !== kbOctaves) rebuild(); }, 150); });
  const octave = d => { heldClear(); Object.keys(held).forEach(s => noteOff(s)); kbBase = clamp(kbBase + d * 12, 24, 96); rebuild(); };
  dn.onclick = () => octave(-1); up.onclick = () => octave(1); buildKeys.octave = octave;
  const ptr = kbPtr;
  const noteAt = e => { const t = document.elementFromPoint(e.clientX, e.clientY); return t && t.dataset && t.dataset.note != null && kb.contains(t) ? +t.dataset.note : null; };
  const stop = e => { if (e.cancelable) e.preventDefault(); };
  ['touchstart', 'touchmove', 'contextmenu', 'selectstart', 'dragstart'].forEach(t => kb.addEventListener(t, stop, { passive: false }));
  ['selectstart', 'dragstart', 'contextmenu'].forEach(t => f.addEventListener(t, ev => { if (!['INPUT', 'SELECT', 'TEXTAREA'].includes(ev.target.tagName)) stop(ev); }, { passive: false }));
  const rel = id => { if (ptr[id]) { delete ptr[id]; noteOff('p' + id); } };
  kb.addEventListener('pointerdown', e => { const n = noteAt(e); if (n == null) return; try { kb.setPointerCapture(e.pointerId); } catch (x) {} ptr[e.pointerId] = 1; noteOn('p' + e.pointerId, n); e.preventDefault(); });
  kb.addEventListener('pointermove', e => { if (!ptr[e.pointerId]) return; const n = noteAt(e); if (n != null) noteOn('p' + e.pointerId, n); else rel(e.pointerId); });
  const pu = e => rel(e.pointerId);
  kb.addEventListener('pointerup', pu); kb.addEventListener('pointercancel', pu); kb.addEventListener('lostpointercapture', pu);
  window.addEventListener('pointerup', pu, true); window.addEventListener('pointercancel', pu, true);
  const relAll = () => Object.keys(ptr).forEach(rel);
  window.addEventListener('blur', relAll);
  document.addEventListener('visibilitychange', () => { if (document.hidden) relAll(); });
}
const KEYMAP = { KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7, KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14, KeyP: 15, Semicolon: 16 };
function typing(t) { return t && (t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || (t.tagName === 'INPUT' && !['range', 'checkbox', 'button', 'file'].includes(t.type)) || t.isContentEditable); }
window.addEventListener('keydown', e => {
  if (typing(e.target) || e.ctrlKey || e.metaKey || e.altKey || $('#startOverlay').style.display !== 'none') return;
  if (e.code === 'KeyZ') { if (!e.repeat) buildKeys.octave(-1); return; }
  if (e.code === 'KeyX') { if (!e.repeat) buildKeys.octave(1); return; }
  if (e.code in KEYMAP) { if (!e.repeat) noteOn(e.code, kbBase + KEYMAP[e.code]); e.preventDefault(); }
});
window.addEventListener('keyup', e => { if (e.code in KEYMAP) noteOff(e.code); });
window.addEventListener('blur', () => { Object.keys(held).forEach(noteOff); });

let midiAccess = null, midiSel = 'all';
function setupMidi() {
  const sel = $('#midiSel'); if (!navigator.requestMIDIAccess) return;
  navigator.requestMIDIAccess().then(acc => {
    midiAccess = acc;
    const fill = () => {
      sel.innerHTML = ''; let o = el('option', '', sel, 'MIDI: all inputs'); o.value = 'all';
      acc.inputs.forEach(inp => { o = el('option', '', sel, inp.name || inp.id); o.value = inp.id; });
      if (!acc.inputs.size) sel.firstChild.textContent = 'MIDI: no devices'; sel.value = [...sel.options].some(x => x.value === midiSel) ? midiSel : 'all';
      acc.inputs.forEach(inp => { inp.onmidimessage = ev => {
        if (midiSel !== 'all' && midiSel !== inp.id) return; const d = Array.from(ev.data); if (d[0] === 0xF8 || d[0] === 0xFE) return;
        midiOut(d); const s = d[0] & 0xF0; if (s === 0x90 && d[2] > 0) vis(d[1], true); else if (s === 0x80 || s === 0x90) vis(d[1], false);
      }; });
    };
    sel.onchange = () => { midiSel = sel.value; }; acc.onstatechange = fill; fill();
  }, () => { sel.firstChild.textContent = 'MIDI: denied'; });
}

/* ---------- drag and drop & file inputs ---------- */
function handleFiles(files) {
  const f = files && files[0]; if (!f) return;
  if (/\.syx$/i.test(f.name)) loadCartFile(f);
  else if (/^audio\//.test(f.type) || /\.(wav|aif|aiff|flac|mp3|ogg|m4a|opus)$/i.test(f.name)) loadAudioFile(f);
  else toast('Unsupported file: ' + f.name);
}
let dragDepth = 0;
window.addEventListener('dragenter', e => { if (e.dataTransfer && [...e.dataTransfer.types].includes('Files')) { dragDepth++; document.body.classList.add('dragging'); } });
window.addEventListener('dragleave', () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) document.body.classList.remove('dragging'); });
window.addEventListener('dragover', e => e.preventDefault());
window.addEventListener('drop', e => { e.preventDefault(); dragDepth = 0; document.body.classList.remove('dragging'); handleFiles(e.dataTransfer.files); });
function wireInputs() {
  $('#fileCart').onchange = e => { handleFiles(e.target.files); e.target.value = ''; };
  $('#fileSample').onchange = e => { handleFiles(e.target.files); e.target.value = ''; };
  $('#filePatch').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (!f) return;
    try { const o = JSON.parse(await f.text()); if (!o.layers) throw new Error('not a Chimera patch'); loadPatchObj(o); } catch (er) { toast('Could not load patch: ' + er.message); } };
  $('#fileDir').onchange = e => { const fs = e.target.files; randomFromDir(fs).then(() => { e.target.value = ''; }); };
}

/* ---------- worklet messages ---------- */
function applyIncoming(obj) { pendingPatch = null; patch = obj; renderAll(); }
function onMsg(ev) {
  const m = ev.data || ev; if (!m || !m.type) return;
  switch (m.type) {
    case 'ready': engineReady = true; clearTimeout(engineWatchdog); presets = m.presets || []; buildPresetList(); if (m.json) applyIncoming(JSON.parse(m.json)); break;
    case 'patch': { const o = JSON.parse(m.json); if (dragging) pendingPatch = o; else applyIncoming(o); break; }
    case 'presetIndex': presetIndex = m.index; if (topEls.sel) topEls.sel.value = String(presetIndex); break;
    case 'meter': { if (!powered) m = { voices: 0, peak: 0 }; const v = $('#voices'), p = $('#peak'); if (v) v.textContent = m.voices; if (p) { const pk = clamp(m.peak, 0, 1.2); p.style.width = Math.min(pk, 1) * 100 + '%'; p.style.background = pk > .95 ? 'var(--rec)' : pk > .7 ? 'var(--warn)' : 'var(--ok)'; } break; }
    case 'cartFile': download(m.name || 'chimera-cart.syx', m.bytes, 'application/octet-stream'); break;
    case 'error': toast(m.message || 'Error'); break;
  }
}

/* ---------- audio start ---------- */
// ---- making sure sound actually comes out ----
// iPhone/iPad: Web Audio is muted by the silent switch unless the page also plays an <audio>
// element, and every browser only lets audio start inside a tap/click. So on every gesture we
// (re)start a silent looping <audio> element, ask for the "playback" audio session, and resume
// the AudioContext synchronously, before any await.
var silentEl = null;
function silentWavUrl() {
  const n = 4000, b = new DataView(new ArrayBuffer(44 + n * 2));
  const str = (o, t) => { for (let i = 0; i < t.length; ++i) b.setUint8(o + i, t.charCodeAt(i)); };
  str(0, 'RIFF'); b.setUint32(4, 36 + n * 2, true); str(8, 'WAVE'); str(12, 'fmt ');
  b.setUint32(16, 16, true); b.setUint16(20, 1, true); b.setUint16(22, 1, true); b.setUint32(24, 8000, true);
  b.setUint32(28, 16000, true); b.setUint16(32, 2, true); b.setUint16(34, 16, true); str(36, 'data'); b.setUint32(40, n * 2, true);
  let bin = ''; const u = new Uint8Array(b.buffer); for (let i = 0; i < u.length; ++i) bin += String.fromCharCode(u[i]);
  return 'data:audio/wav;base64,' + btoa(bin);
}
function unlockAudio() {
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch (e) {}
  try {
    if (!silentEl) { silentEl = new Audio(silentWavUrl()); silentEl.loop = true; silentEl.setAttribute('playsinline', ''); silentEl.volume = 0.01; }
    if (silentEl.paused) { const pr = silentEl.play(); if (pr && pr.catch) pr.catch(() => {}); }
  } catch (e) {}
  if (audioCtx && audioCtx.state !== 'running') { try { const pr = audioCtx.resume(); if (pr && pr.catch) pr.catch(() => {}); } catch (e) {} }
}
['pointerdown', 'touchend', 'keydown'].forEach(t => document.addEventListener(t, () => { if (audioCtx && powered) unlockAudio(); }, true));

async function startAudio() {
  if (node || blockedShown) return;
  const AC = window.AudioContext || window.webkitAudioContext; audioCtx = new AC({ latencyHint: 'interactive' });
  unlockAudio();   // inside the click, before any await
  try {
    node = await window.ChimeraAudio.create(audioCtx, onMsg);
    node.port.onmessage = onMsg; node.connect(audioCtx.destination);
    if (audioCtx.state !== 'running') await audioCtx.resume();
    $('#startOverlay').style.display = 'none'; setupMidi();
    engineWatchdog = setTimeout(() => { if (!engineReady) engineBlocked('the sound engine did not start'); }, 10000);
  } catch (e) { node = null; engineBlocked(e && e.message || String(e)); }
}

// Some embedded viewers block WebAssembly or audio worklets. Say so plainly and point at the
// stand-alone copy of this page, which runs in any desktop browser when opened from disk.
let engineReady = false, engineWatchdog = null, blockedShown = false;
const STANDALONE_URL = 'https://github.com/emac87/ELOOPER/releases/download/latest/chimera.html';
function engineBlocked(why) {
  blockedShown = true;
  const o = $('#startOverlay');
  o.style.display = '';
  o.innerHTML = '';
  const box = el('div', 'blocked');
  box.style.cssText = 'max-width:520px;padding:24px;background:var(--raised);border:1px solid var(--outline);border-radius:10px;text-align:left;line-height:1.5';
  const h = el('div', '', box, 'This browser view blocked the audio engine');
  h.style.cssText = 'font-weight:700;font-size:18px;margin-bottom:8px';
  el('div', '', box, 'Chimera runs its real synth engine as WebAssembly in an audio worklet, and this view does not allow it (' + why + ').');
  const p = el('div', '', box, 'Download the stand-alone page and open it in Chrome, Edge, Firefox or Safari: ');
  p.style.marginTop = '8px';
  const a = el('a', '', p, 'chimera.html');
  a.href = STANDALONE_URL; a.target = '_blank'; a.rel = 'noopener'; a.style.color = 'var(--acc)';
  const u = el('div', '', box, STANDALONE_URL);
  u.style.cssText = 'margin-top:6px;font-size:12px;color:var(--dim);word-break:break-all';
  o.appendChild(box);
  o.onclick = null;
}
window.startAudio = startAudio;

/* ---------- development stub (only when the real worklet loader is absent) ---------- */
if (!window.ChimeraAudio) {
  const mkEnv = (a, d, s, r) => ({ attack: a, decay: d, sustain: s, release: r });
  const mkOp = (i) => ({ egRate: [99, 99, 99, 99], egLevel: [99, 99, 99, 0], kbdBreakPoint: 39, kbdLeftDepth: 0, kbdRightDepth: 0, kbdLeftCurve: 0, kbdRightCurve: 0, kbdRateScaling: 0, ampModSens: 0, keyVelSens: i ? 0 : 2, outputLevel: i ? 80 - i * 6 : 99, oscMode: 0, freqCoarse: i ? i + 1 : 1, freqFine: 0, detune: 7 });
  const mkLfo = () => ({ shape: 0, rateHz: 2, sync: false, beats: 1, retrigger: true, fadeIn: 0 });
  const mkLayer = eng => ({ engine: eng, name: '', analog: { wave1: 2, wave2: 2, pw1: .5, pw2: .5, osc2Semi: 0, osc2Fine: 0, mix: 0, sub: 0, noise: 0, sync: false }, wtTable: 0, wtPosition: 0,
    fm: { op: [0, 1, 2, 3, 4, 5].map(mkOp), pitchEgRate: [99, 99, 99, 99], pitchEgLevel: [50, 50, 50, 50], algorithm: 4, feedback: 2, oscKeySync: 1, lfoSpeed: 35, lfoDelay: 0, lfoPitchModDepth: 0, lfoAmpModDepth: 0, lfoKeySync: 1, lfoWave: 0, lfoPitchModSens: 3, transpose: 24, name: 'FM BELL' },
    fmAmount: 1, sampler: { start: 0, loop: 0, loopStart: 0, loopEnd: 1, reverse: false }, pluck: { decay: .5, damping: .5, pickPos: .2, noiseColor: .5 },
    filter: 2, cutoff: 12000, resonance: .15, drive: 0, keyTrack: 0, filterEnvAmount: 0, ampEnv: mkEnv(.003, .4, .8, .25), filterEnv: mkEnv(.003, .5, 0, .3), modEnv: mkEnv(.003, .5, 0, .3), useAmpEnv: true,
    lfo: [mkLfo(), mkLfo()], level: .75, pan: 0, transpose: 0, fine: 0, keyLo: 0, keyHi: 127, velLo: 1, velHi: 127, velocitySens: .6, unison: 1, unisonDetune: .2, unisonSpread: .6, glide: 0, mode: 0, polyphony: 16, output: 0,
    mods: Array.from({ length: 8 }, (_, i) => ({ src: i === 0 ? 1 : 0, dst: i === 0 ? 2 : 0, amount: i === 0 ? .3 : 0 })) });
  const defaultPatch = () => ({ name: 'Init', category: 'Init', layers: [mkLayer(1), mkLayer(0), mkLayer(0), mkLayer(0)], globalLfo: [mkLfo(), Object.assign(mkLfo(), { shape: 1, rateHz: .25, retrigger: false })],
    macroNames: ['Brightness', 'Clang', 'Macro 3', 'Macro 4'], chorus: { on: false, rate: .4, depth: .5, mix: .35 }, delay: { on: false, sync: true, beats: .75, ms: 375, feedback: .35, mix: .25, tone: .6, pingPong: true },
    reverb: { on: false, size: .6, damping: .5, mix: .25, predelayMs: 10, width: 1 }, masterVolume: .7, bendRange: 2, sampleNames: ['', '', '', ''], sampleRoots: [60, 60, 60, 60], cartVoices: [[], [], [], []], cartNames: ['', '', '', ''] });
  window.ChimeraAudio = { create: async () => {
    let p = defaultPatch(); const cats = ['Bass', 'Lead', 'Pad', 'Keys', 'FM', 'Pluck'];
    const list = []; cats.forEach(c => { for (let i = 1; i <= 3; i++) list.push({ name: c + ' ' + i, category: c }); });
    const port = { postMessage(m) { const back = () => port.onmessage && port.onmessage({ data: { type: 'patch', json: JSON.stringify(p) } }); const say = o => setTimeout(() => port.onmessage && port.onmessage({ data: o }), 5);
      if (m.type === 'patch') p = JSON.parse(m.json);
      else if (m.type === 'preset' || m.type === 'randomPreset') { const i = m.type === 'preset' ? m.index : Math.floor(Math.random() * list.length); p = defaultPatch(); p.name = list[i].name; p.layers[0].engine = (i % 5) + 1; say({ type: 'presetIndex', index: i }); back(); }
      else if (m.type === 'builtinCart' || m.type === 'cart') { p.cartVoices[m.layer] = Array.from({ length: 32 }, (_, i) => 'VOICE ' + (i + 1)); p.cartNames[m.layer] = m.name || 'Chimera Voices'; back(); }
      else if (m.type === 'cartVoice') { p.layers[m.layer].fm.name = p.cartVoices[m.layer][m.index]; p.layers[m.layer].fm.algorithm = m.index % 32; back(); }
      else if (m.type === 'sample') { p.sampleNames[m.layer] = m.name; back(); }
      else if (m.type === 'saveCart') say({ type: 'cartFile', layer: m.layer, bytes: new ArrayBuffer(4104), name: 'cart.syx' }); },
      onmessage: null };
    setTimeout(() => port.onmessage && port.onmessage({ data: { type: 'ready', presets: list, json: JSON.stringify(p) } }), 20);
    setInterval(() => port.onmessage && port.onmessage({ data: { type: 'meter', voices: 0, peak: 0 } }), 100);
    return { port, connect() {}, disconnect() {} };
  } };
}

/* ---------- init ---------- */
buildTop(); buildKeys(); wireInputs();
$('#startOverlay').addEventListener('click', startAudio);
$('#startOverlay').addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') startAudio(); });
window.__chimeraState = () => ({ ctx: audioCtx && audioCtx.state, silentPaused: silentEl ? silentEl.paused : null, powered });
})();
