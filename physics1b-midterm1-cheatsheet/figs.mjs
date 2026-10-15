// SVG figure generators for the cheatsheet
const f2 = (v) => (Math.round(v * 100) / 100).toString();

function frame({ w, h, xr, yr, pad = [6, 4, 4, 8] }) {
  // pad: top right bottom left
  const [pt, pr, pb, pl] = pad;
  const X = (x) => pl + ((x - xr[0]) / (xr[1] - xr[0])) * (w - pl - pr);
  const Y = (y) => pt + (1 - (y - yr[0]) / (yr[1] - yr[0])) * (h - pt - pb);
  return { X, Y };
}
function path(fn, a, b, n, X, Y, clip) {
  let d = '';
  let pen = false;
  for (let i = 0; i <= n; i++) {
    const t = a + ((b - a) * i) / n;
    let y = fn(t);
    if (!isFinite(y) || (clip && (y > clip[1] || y < clip[0]))) { pen = false; continue; }
    d += (pen ? 'L' : 'M') + f2(X(t)) + ' ' + f2(Y(y));
    pen = true;
  }
  return d;
}
const txt = (x, y, s, o = {}) =>
  `<text x="${f2(x)}" y="${f2(y)}" font-size="${o.fs || 5}" fill="${o.c || '#222'}" text-anchor="${o.a || 'start'}" ${o.it ? 'font-style="italic"' : ''} ${o.b ? 'font-weight="bold"' : ''}>${s}</text>`;
const axes = (X, Y, xr, yr, w, h) =>
  `<line x1="${X(xr[0])}" y1="${Y(0)}" x2="${X(xr[1])}" y2="${Y(0)}" stroke="#555" stroke-width="0.4"/>` +
  `<line x1="${X(xr[0])}" y1="${Y(yr[0])}" x2="${X(xr[0])}" y2="${Y(yr[1])}" stroke="#555" stroke-width="0.4"/>`;

export function dampFig() {
  const w = 160, h = 62, xr = [0, 15], yr = [-0.8, 1.05];
  const { X, Y } = frame({ w, h, xr, yr, pad: [3, 3, 3, 6] });
  const w0 = 1;
  const under = (z) => (t) => { const g = z * w0, wd = Math.sqrt(w0 * w0 - g * g); return Math.exp(-g * t) * (Math.cos(wd * t) + (g / wd) * Math.sin(wd * t)); };
  const crit = (t) => (1 + t) * Math.exp(-t);
  const over = (z) => (t) => { const g = z, a = Math.sqrt(g * g - 1), r1 = g + a, r2 = g - a; const C2 = r1 / (r1 - r2), C1 = -r2 / (r1 - r2); return C1 * Math.exp(-r1 * t) + C2 * Math.exp(-r2 * t); };
  const g = 0.12;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  s += axes(X, Y, xr, yr, w, h);
  s += `<path d="${path((t) => Math.exp(-g * t), 0, 15, 120, X, Y)}" stroke="#1f5fbf" stroke-width="0.5" stroke-dasharray="1.5 1" fill="none"/>`;
  s += `<path d="${path((t) => -Math.exp(-g * t), 0, 15, 120, X, Y)}" stroke="#1f5fbf" stroke-width="0.5" stroke-dasharray="1.5 1" fill="none"/>`;
  s += `<path d="${path(under(g), 0, 15, 400, X, Y)}" stroke="#1f5fbf" stroke-width="0.8" fill="none"/>`;
  s += `<path d="${path(crit, 0, 15, 200, X, Y)}" stroke="#d1262c" stroke-width="1" fill="none"/>`;
  s += `<path d="${path(over(3), 0, 15, 200, X, Y)}" stroke="#1d8f3a" stroke-width="1" fill="none"/>`;
  const lg = [['#1f5fbf','underdamped b²<4mk: oscillates in envelope'],['#d1262c','critical b²=4mk: fastest, no overshoot'],['#1d8f3a','overdamped b²>4mk: slow creep']];
  lg.forEach(([c,t],i)=>{ const yy=Y(1.0)+1+i*5.2; s+=`<line x1="${X(5.6)}" y1="${yy-1.3}" x2="${X(6.6)}" y2="${yy-1.3}" stroke="${c}" stroke-width="1"/>`; s+=txt(X(6.8),yy,t.replace('<','&lt;').replace('>','&gt;'),{c,fs:4.4}); });
  s += txt(X(14.8), Y(0) - 1, 't', { it: 1, fs: 5 });
  s += txt(X(0) + 1.5, Y(1.0) + 2, 'x', { it: 1, fs: 5 });
  s += txt(X(14.9), Y(-0.8)+2.5, 'all start at x₀ from rest, same ω₀', { fs: 3.9, c: '#555', a:'end' });
  s += '</svg>';
  return s;
}

export function resFig() {
  const w = 90, h = 54, xr = [0, 2.2], yr = [0, 5.4];
  const { X, Y } = frame({ w, h, xr, yr, pad: [3, 3, 7, 6] });
  const Af = (z) => (r) => 1 / Math.sqrt((1 - r * r) ** 2 + (2 * z * r) ** 2);
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  s += axes(X, Y, xr, yr, w, h);
  const cols = ['#7a1fa2', '#d1262c', '#e08a00'];
  [0.1, 0.2, 0.5].forEach((z, i) => { s += `<path d="${path(Af(z), 0, 2.2, 300, X, Y, [0, 5.4])}" stroke="${cols[i]}" stroke-width="0.8" fill="none"/>`; });
  s += `<line x1="${X(1)}" y1="${Y(0)}" x2="${X(1)}" y2="${Y(5.4)}" stroke="#999" stroke-width="0.3" stroke-dasharray="1 1"/>`;
  s += txt(X(1), h - 1.5, 'ω = ω₀', { a: 'middle', fs: 4.4 });
  s += txt(X(2.2), h - 1.5, 'ω', { a: 'end', it: 1, fs: 5 });
  s += txt(X(1.06), Y(5.0), 'small b', { c: cols[0], fs: 4.2 });
  s += txt(X(1.12), Y(2.6), 'medium', { c: cols[1], fs: 4.2 });
  s += txt(X(0.08), Y(1.45), 'large b', { c: cols[2], fs: 4.2 });
  s += txt(X(0) + 1, Y(5.1), 'A', { it: 1, fs: 5 });
  s += txt(X(0.02), Y(0.2) + 4.5, 'F₀/k', { fs: 3.8 });
  s += '</svg>';
  return s;
}

export function ljFig() {
  const w = 100, h = 58, xr = [0.85, 2.6], yr = [-1.25, 1.6];
  const { X, Y } = frame({ w, h, xr, yr, pad: [3, 3, 3, 8] });
  const U = (x) => 4 * (x ** -12 - x ** -6);
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  s += `<line x1="${X(0.85)}" y1="${Y(0)}" x2="${X(2.6)}" y2="${Y(0)}" stroke="#555" stroke-width="0.4"/>`;
  s += `<line x1="${X(0.85)}" y1="${Y(-1.25)}" x2="${X(0.85)}" y2="${Y(1.6)}" stroke="#555" stroke-width="0.4"/>`;
  s += `<path d="${path(U, 0.85, 2.6, 400, X, Y, [-1.25, 1.6])}" stroke="#7a1fa2" stroke-width="1" fill="none"/>`;
  const x0 = 2 ** (1 / 6);
  // energy line E=-0.5
  const E = -0.5;
  // turning points: solve 4(u^2-u)=E with u=x^-6 -> u = (1±sqrt(1+E))/2
  const ua = (1 + Math.sqrt(1 + E)) / 2, ub = (1 - Math.sqrt(1 + E)) / 2;
  const xa = ua ** (-1 / 6), xb = ub ** (-1 / 6);
  s += `<line x1="${X(0.95)}" y1="${Y(E)}" x2="${X(2.3)}" y2="${Y(E)}" stroke="#0a8" stroke-width="0.6"/>`;
  s += txt(X(2.3), Y(E) - 1.2, 'E&lt;0: bound', { c: '#0a8', fs: 4.2, a: 'end' });
  [xa, xb].forEach((xx) => { s += `<line x1="${X(xx)}" y1="${Y(E)}" x2="${X(xx)}" y2="${Y(0)}" stroke="#0a8" stroke-width="0.35" stroke-dasharray="1 0.8"/>`; });
  s += txt(X(xa) - 1, Y(0) - 1.2, 'A₁', { c: '#0a8', fs: 4, a: 'end' });
  s += txt(X(xb) + 0.6, Y(0) - 1.2, 'A₂ turning pts', { c: '#0a8', fs: 4 });
  s += `<circle cx="${X(x0)}" cy="${Y(-1)}" r="0.9" fill="#000"/>`;
  s += `<line x1="${X(x0)}" y1="${Y(-1)}" x2="${X(x0)}" y2="${Y(0)}" stroke="#555" stroke-width="0.3" stroke-dasharray="0.8 0.8"/>`;
  s += txt(X(x0) + 1.5, Y(-1) + 1.5, 'x₀=2^(1/6)σ≈1.12σ, U=−ε', { fs: 4.2 });
  s += `<circle cx="${X(1)}" cy="${Y(0)}" r="0.7" fill="#d1262c"/>`;
  s += txt(X(1) - 1.2, Y(0) + 4.5, 'σ', { fs: 4.6, c: '#d1262c', a: 'end' });
  s += txt(X(1.0) + 3, Y(1.35), 'repulsive (σ/x)¹² · steep', { fs: 4.2, c: '#555' });
  s += txt(X(1.75), Y(0.35), 'attractive −(σ/x)⁶', { fs: 4.2, c: '#555' });
  s += txt(X(0.85) + 1, Y(1.6) + 4, 'U', { it: 1, fs: 5 });
  s += txt(X(2.6), Y(0) - 1, 'x', { it: 1, fs: 5, a: 'end' });
  s += '</svg>';
  return s;
}

export function standFig() {
  // fixed-fixed n=1..4 and fixed-free n=1,3,5
  const w = 160, rowH = 9.2, L = 52;
  const rows = 4;
  const h = rows * rowH + 4;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  const drawMode = (ox, oy, k, type, label, lab2) => {
    const amp = 3.1;
    const f = (x) => Math.sin(k * x);
    const X = (x) => ox + x * L, Y = (y) => oy - y * amp;
    s += `<path d="${path(f, 0, 1, 120, X, Y)}" stroke="#c45a00" stroke-width="0.7" fill="none"/>`;
    s += `<path d="${path((x) => -f(x), 0, 1, 120, X, Y)}" stroke="#c45a00" stroke-width="0.5" stroke-dasharray="1.2 0.8" fill="none"/>`;
    s += `<line x1="${X(0)}" y1="${oy}" x2="${X(1)}" y2="${oy}" stroke="#bbb" stroke-width="0.25"/>`;
    s += `<rect x="${X(0) - 1.2}" y="${oy - 4}" width="1.2" height="8" fill="#555"/>`;
    if (type === 'ff') s += `<rect x="${X(1)}" y="${oy - 4}" width="1.2" height="8" fill="#555"/>`;
    else s += `<circle cx="${X(1)}" cy="${oy - amp * Math.sin(k)}" r="0.8" fill="none" stroke="#555" stroke-width="0.4"/>`;
    // nodes
    for (let m = 0; m * Math.PI / k <= 1.0001; m++) { const xn = (m * Math.PI) / k; s += `<circle cx="${X(xn)}" cy="${oy}" r="0.7" fill="#1f5fbf"/>`; }
    s += txt(X(1) + 3, oy + 1.5, label, { fs: 4.4 });
    if (lab2) s += txt(X(1) + 3, oy + 1.5 + 0, '', {});
  };
  const ox1 = 4, ox2 = 84;
  s += txt(ox1, 3.3, 'FIXED–FIXED: L = nλ/2, all n', { fs: 4.4, b: 1, c: '#c45a00' });
  s += txt(ox2, 3.3, 'FIXED–FREE: L = nλ/4, n odd', { fs: 4.4, b: 1, c: '#c45a00' });
  for (let n = 1; n <= 4; n++) drawMode(ox1, 4 + rowH * (n - 1) + 5.2, n * Math.PI, 'ff', `n=${n}: λ=2L/${n}`);
  [1, 3, 5].forEach((n, i) => drawMode(ox2, 4 + rowH * i + 5.2, (n * Math.PI) / 2, 'fr', `n=${n}: λ=4L/${n}`));
  s += txt(ox2, 4 + rowH * 3 + 6.5, '● = node (blue). free end = antinode', { fs: 4, c: '#1f5fbf' });
  s += '</svg>';
  return s;
}
