// SVG figures for the midterm 2 cheatsheet
const f2 = (v) => (Math.round(v * 100) / 100).toString();
const txt = (x, y, s, o = {}) =>
  `<text x="${f2(x)}" y="${f2(y)}" font-size="${o.fs || 5}" fill="${o.c || '#222'}" text-anchor="${o.a || 'start'}" ${o.it ? 'font-style="italic"' : ''} ${o.b ? 'font-weight="bold"' : ''}>${s}</text>`;
const line = (x1, y1, x2, y2, c = '#555', w = 0.4, extra = '') => `<line x1="${f2(x1)}" y1="${f2(y1)}" x2="${f2(x2)}" y2="${f2(y2)}" stroke="${c}" stroke-width="${w}" ${extra}/>`;
const arrow = (x1, y1, x2, y2, c = '#222', w = 0.7, hs = 1.8) => {
  const a = Math.atan2(y2 - y1, x2 - x1);
  const p1 = [x2 - hs * Math.cos(a - 0.45), y2 - hs * Math.sin(a - 0.45)];
  const p2 = [x2 - hs * Math.cos(a + 0.45), y2 - hs * Math.sin(a + 0.45)];
  return line(x1, y1, x2, y2, c, w) + `<polygon points="${f2(x2)},${f2(y2)} ${f2(p1[0])},${f2(p1[1])} ${f2(p2[0])},${f2(p2[1])}" fill="${c}"/>`;
};
function path(fn, a, b, n, X, Y, clip) {
  let d = '', pen = false;
  for (let i = 0; i <= n; i++) {
    const t = a + ((b - a) * i) / n; const y = fn(t);
    if (!isFinite(y) || (clip && (y > clip[1] || y < clip[0]))) { pen = false; continue; }
    d += (pen ? 'L' : 'M') + f2(X(t)) + ' ' + f2(Y(y)); pen = true;
  }
  return d;
}

export function dopplerFig() {
  const w = 100, h = 50, cy = 24;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  // source moving right at vs = 0.5 v; emitted at x_i, radius v(t - t_i)
  const xs = 46, vs = 0.5;
  for (let i = 1; i <= 5; i++) {
    const r = i * 4.2; const x = xs - vs * r;
    s += `<circle cx="${f2(x)}" cy="${cy}" r="${f2(r)}" fill="none" stroke="#1f5fbf" stroke-width="0.45"/>`;
  }
  s += `<circle cx="${xs}" cy="${cy}" r="1.6" fill="#c4161c"/>`;
  s += arrow(xs + 2, cy, xs + 9, cy, '#c4161c', 0.7);
  s += txt(xs + 10, cy + 1.5, 'v<tspan font-size="3.4">s</tspan>', { fs: 4.4, c: '#c4161c', it: 1 });
  s += txt(w - 1, 7, 'ahead: λ squeezed', { fs: 4.2, a: 'end', c: '#1f5fbf' });
  s += txt(w - 1, 12, 'f ↑ (higher pitch)', { fs: 4.2, a: 'end', c: '#1f5fbf', b: 1 });
  s += txt(1, 7, 'behind: λ stretched', { fs: 4.2, c: '#1f5fbf' });
  s += txt(1, 12, 'f ↓ (lower pitch)', { fs: 4.2, c: '#1f5fbf', b: 1 });
  s += txt(w / 2, h - 1.5, 'wavefronts emitted at earlier source positions', { fs: 3.8, a: 'middle', c: '#555' });
  s += '</svg>';
  return s;
}

export function sphereFig() {
  const w = 100, h = 46;
  const pl = 9, pr = 4, pt = 4, pb = 7;
  const xr = [0, 4], X = (x) => pl + (x / xr[1]) * (w - pl - pr), Y = (y) => pt + (1 - y / 1.1) * (h - pt - pb);
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  s += `<rect x="${X(0)}" y="${pt}" width="${X(1) - X(0)}" height="${h - pt - pb}" fill="#eef4fb"/>`;
  s += line(X(0), Y(0), X(4), Y(0)) + line(X(0), Y(0), X(0), Y(1.1));
  s += line(X(1), Y(0), X(1), Y(1.1), '#999', 0.3, 'stroke-dasharray="1 0.8"');
  // E: 0 inside, 1/r^2 outside ; V: 1 inside, 1/r outside
  s += `<path d="M${X(0)} ${Y(0)} L${X(1)} ${Y(0)}" stroke="#c4161c" stroke-width="1" fill="none"/>`;
  s += `<path d="${path((r) => 1 / (r * r), 1, 4, 100, X, Y)}" stroke="#c4161c" stroke-width="1" fill="none"/>`;
  s += line(X(1), Y(0), X(1), Y(1), '#c4161c', 0.5, 'stroke-dasharray="0.8 0.6"');
  s += `<path d="M${X(0)} ${Y(1) - 0.0} L${X(1)} ${Y(1)}" stroke="#1f5fbf" stroke-width="1" fill="none" stroke-dasharray="2.2 0.9"/>`;
  s += `<path d="${path((r) => 1 / r, 1, 4, 100, X, Y)}" stroke="#1f5fbf" stroke-width="1" fill="none" stroke-dasharray="2.2 0.9"/>`;
  s += txt(X(0.5), Y(0.16), 'E = 0', { fs: 4.4, a: 'middle', c: '#c4161c', b: 1 });
  s += txt(X(0.5), Y(1) + 4.5, 'V = kQ/R', { fs: 3.9, a: 'middle', c: '#1f5fbf', b: 1 });
  s += txt(X(1.55), Y(0.62), 'E = kQ/r²', { fs: 4.2, c: '#c4161c' });
  s += txt(X(2.6), Y(0.42), 'V = kQ/r', { fs: 4.2, c: '#1f5fbf' });
  s += txt(X(1), h - 1.5, 'R', { fs: 4.6, a: 'middle', it: 1 });
  s += txt(X(4), h - 1.5, 'r', { fs: 4.6, a: 'end', it: 1 });
  s += txt(X(0.5), h - 1.5, 'inside metal', { fs: 3.6, a: 'middle', c: '#555' });
  s += txt(X(1) + 2.2, Y(1.02) + 1, '← E jumps to V/R = σ/ε₀ at R', { fs: 3.7, c: '#c4161c' });
  s += '</svg>';
  return s;
}

export function pot2DFig() {
  const w = 100, h = 46, pl = 8, pr = 3, pt = 3, pb = 6;
  const xr = [0.15, 5], yr = [-2.6, 2];
  const X = (x) => pl + ((x - xr[0]) / (xr[1] - xr[0])) * (w - pl - pr), Y = (y) => pt + (1 - (y - yr[0]) / (yr[1] - yr[0])) * (h - pt - pb);
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  s += line(X(xr[0]), Y(0), X(xr[1]), Y(0)) + line(X(xr[0]), Y(yr[0]), X(xr[0]), Y(yr[1]));
  s += `<path d="${path((r) => -1 / r, 0.15, 5, 200, X, Y, yr)}" stroke="#1f5fbf" stroke-width="1" fill="none"/>`;
  s += `<path d="${path((r) => Math.log(r), 0.15, 5, 200, X, Y, yr)}" stroke="#c4161c" stroke-width="1" fill="none"/>`;
  const E = 0.9, R = Math.exp(E);
  s += line(X(0.3), Y(E), X(4.6), Y(E), '#1d8f3a', 0.55);
  s += line(X(R), Y(E), X(R), Y(0), '#1d8f3a', 0.35, 'stroke-dasharray="0.8 0.6"');
  s += `<circle cx="${X(R)}" cy="${Y(E)}" r="0.9" fill="#1d8f3a"/>`;
  s += txt(X(R) + 1.2, Y(E) + 4, 'turning pt R: K=0', { fs: 3.8, c: '#1d8f3a' });
  s += txt(X(0.35), Y(E) - 1.2, 'E_total', { fs: 3.8, c: '#1d8f3a' });
  s += txt(X(5), Y(1.75), '2D: U = C ln(r/r₀) → ∞', { fs: 4.2, c: '#c4161c', b: 1, a: 'end' });
  s += txt(X(1.5), Y(-0.95), '3D: U = −ke²/r → 0', { fs: 4.2, c: '#1f5fbf', b: 1 });
  s += txt(X(1) - 1, Y(0) + 4.2, 'r₀', { fs: 4, a: 'middle', c: '#c4161c' });
  s += `<circle cx="${X(1)}" cy="${Y(0)}" r="0.7" fill="#c4161c"/>`;
  s += txt(X(5), Y(0) - 1, 'r', { fs: 4.6, a: 'end', it: 1 });
  s += txt(X(xr[0]) + 1.2, Y(yr[1]) + 3.4, 'U', { fs: 4.6, it: 1 });
  s += txt(X(2.2), h - 1, 'any E > 0 escapes in 3D; 2D always turns back', { fs: 3.6, a: 'middle', c: '#555' });
  s += '</svg>';
  return s;
}

export function cubeFig() {
  // two panels: (a) charge at centre ; (b) corner charge with 2x2x2 block
  const w = 100, h = 44;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  const iso = (ox, oy, L) => (x, y, z) => [ox + (x - z * 0.5) * L, oy - (y + z * 0.38) * L];
  const cube = (P, x0, y0, z0, n, col, wd, dash) => {
    const v = (a, b, c) => P(x0 + a * n, y0 + b * n, z0 + c * n);
    const E = [[[0, 0, 0], [1, 0, 0]], [[1, 0, 0], [1, 1, 0]], [[1, 1, 0], [0, 1, 0]], [[0, 1, 0], [0, 0, 0]], [[0, 0, 1], [1, 0, 1]], [[1, 0, 1], [1, 1, 1]], [[1, 1, 1], [0, 1, 1]], [[0, 1, 1], [0, 0, 1]], [[0, 0, 0], [0, 0, 1]], [[1, 0, 0], [1, 0, 1]], [[1, 1, 0], [1, 1, 1]], [[0, 1, 0], [0, 1, 1]]];
    return E.map(([a, b]) => { const p = v(...a), q = v(...b); return line(p[0], p[1], q[0], q[1], col, wd, dash ? 'stroke-dasharray="1 0.7"' : ''); }).join('');
  };
  // (a)
  const Pa = iso(8, 34, 22);
  s += `<polygon points="${[Pa(0, 0, 0), Pa(1, 0, 0), Pa(1, 1, 0), Pa(0, 1, 0)].map((p) => p.map(f2).join(',')).join(' ')}" fill="#e9f0fc"/>`;
  s += cube(Pa, 0, 0, 0, 1, '#1f5fbf', 0.6);
  const c = Pa(0.5, 0.5, 0.5);
  s += `<circle cx="${f2(c[0])}" cy="${f2(c[1])}" r="1.3" fill="#c4161c"/>`;
  s += txt(c[0] + 1.6, c[1] - 0.5, 'Q', { fs: 4.4, c: '#c4161c', b: 1 });
  s += txt(1, 42.5, 'centre: each face Q/6ε₀', { fs: 4, b: 1 });
  // (b) corner: big 2x2x2 dashed, highlighted small cube
  const Pb = iso(60, 37, 13.5);
  s += cube(Pb, -1, -1, -1, 2, '#aaa', 0.35, true);
  s += `<polygon points="${[Pb(0, 0, 0), Pb(1, 0, 0), Pb(1, 1, 0), Pb(0, 1, 0)].map((p) => p.map(f2).join(',')).join(' ')}" fill="#fdeceb"/>`;
  s += cube(Pb, 0, 0, 0, 1, '#c4161c', 0.7);
  const o = Pb(0, 0, 0);
  s += `<circle cx="${f2(o[0])}" cy="${f2(o[1])}" r="1.3" fill="#c4161c"/>`;
  s += txt(o[0] - 2, o[1] + 1.5, 'Q', { fs: 4.4, c: '#c4161c', b: 1, a: 'end' });
  s += txt(w - 1, 4.5, 'corner: 8 cubes surround Q', { fs: 3.9, a: 'end' });
  s += txt(w - 1, 42.5, 'far faces Q/24ε₀ · near 0', { fs: 3.9, a: 'end', b: 1 });
  s += '</svg>';
  return s;
}

export function dipoleFig() {
  const w = 100, h = 50;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  const xm = 30, xp = 70, yb = 42, P = [50, 12];
  s += line(xm, yb, xp, yb, '#bbb', 0.4, 'stroke-dasharray="1 0.8"');
  s += line(xm, yb, P[0], P[1], '#999', 0.35, 'stroke-dasharray="1 0.8"') + line(xp, yb, P[0], P[1], '#999', 0.35, 'stroke-dasharray="1 0.8"');
  s += line(50, yb, 50, P[1], '#999', 0.35, 'stroke-dasharray="1 0.8"');
  s += `<circle cx="${xm}" cy="${yb}" r="2.2" fill="#1f5fbf"/>` + txt(xm, yb + 1.4, '−', { fs: 5, a: 'middle', c: '#fff', b: 1 });
  s += `<circle cx="${xp}" cy="${yb}" r="2.2" fill="#c4161c"/>` + txt(xp, yb + 1.6, '+', { fs: 5, a: 'middle', c: '#fff', b: 1 });
  s += txt(xm, yb + 7, '−q', { fs: 4.4, a: 'middle' }) + txt(xp, yb + 7, '+q', { fs: 4.4, a: 'middle' });
  s += arrow(42, yb + 4.5, 58, yb + 4.5, '#1d8f3a', 0.7);
  s += txt(50, yb + 7.5, 'p = qd (− → +)', { fs: 3.9, a: 'middle', c: '#1d8f3a', b: 1 });
  s += txt(52, 30, 'z', { fs: 4.4, it: 1 }); s += txt(40.5, 30, 'r', { fs: 4.2, it: 1 }); s += txt(58.5, 30, 'r', { fs: 4.2, it: 1 });
  s += txt(50, yb - 1.5, 'd', { fs: 4, a: 'middle', it: 1 });
  // E+ away from +q (from (70,42) to P direction reversed): direction P - (+q)
  const u = (a, b) => { const dx = b[0] - a[0], dy = b[1] - a[1], n = Math.hypot(dx, dy); return [dx / n, dy / n]; };
  const ep = u([xp, yb], P), em = u(P, [xm, yb]); const L = 13;
  s += arrow(P[0], P[1], P[0] + ep[0] * L, P[1] + ep[1] * L, '#c4161c', 0.7);
  s += arrow(P[0], P[1], P[0] + em[0] * L, P[1] + em[1] * L, '#1f5fbf', 0.7);
  s += arrow(P[0], P[1], P[0] + (ep[0] + em[0]) * L, P[1] + (ep[1] + em[1]) * L, '#111', 1.0);
  s += txt(P[0] - 13, P[1] - 4.5, 'E₊ away from +q', { fs: 3.6, c: '#c4161c', a: 'end' });
  s += txt(P[0] + 15, P[1] + 7, 'E₋ toward −q', { fs: 3.6, c: '#1f5fbf' });
  s += txt(P[0] - 22, P[1] + 3.5, 'E_net ∥ −p', { fs: 4, b: 1, a: 'end' });
  s += `<circle cx="${P[0]}" cy="${P[1]}" r="0.8" fill="#111"/>` + txt(P[0] + 1.5, P[1] - 1.5, 'P', { fs: 4.4, b: 1 });
  s += txt(w - 1, 5, 'z-parts cancel,', { fs: 3.8, a: 'end', c: '#555' }) + txt(w - 1, 9.5, 'x-parts add', { fs: 3.8, a: 'end', c: '#555' });
  s += '</svg>';
  return s;
}

export function pipesFig() {
  const w = 100, h = 30;
  let s = `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg">`;
  const draw = (ox, oy, L, k, closed, lab) => {
    const amp = 3;
    // displacement: open end = antinode. open-open: cos(kx) ; closed at x=L end? use closed at right: sin
    const f = closed ? (x) => Math.cos(k * x) : (x) => Math.cos(k * x);
    const X = (x) => ox + x * L, Y = (y) => oy - y * amp;
    s += `<rect x="${ox}" y="${oy - 4}" width="${L}" height="8" fill="#f2f6fb" stroke="#999" stroke-width="0.3"/>`;
    if (closed) s += `<rect x="${ox + L}" y="${oy - 4.4}" width="1.2" height="8.8" fill="#555"/>`;
    s += `<path d="${path(f, 0, 1, 80, X, Y)}" stroke="#c45a00" stroke-width="0.6" fill="none"/>`;
    s += `<path d="${path((x) => -f(x), 0, 1, 80, X, Y)}" stroke="#c45a00" stroke-width="0.45" stroke-dasharray="1 0.7" fill="none"/>`;
    s += txt(ox + L / 2, oy + 7.6, lab, { fs: 3.7, a: 'middle' });
  };
  s += txt(24, 4, 'OPEN–OPEN (all n)', { fs: 4, a: 'middle', b: 1, c: '#c45a00' });
  s += txt(76, 4, 'CLOSED–OPEN (odd n)', { fs: 4, a: 'middle', b: 1, c: '#c45a00' });
  draw(3, 13, 20, Math.PI, false, 'n=1: λ=2L'); draw(27, 13, 20, 2 * Math.PI, false, 'n=2: λ=L');
  draw(54, 13, 20, Math.PI / 2, true, 'n=1: λ=4L'); draw(77, 13, 20, (3 * Math.PI) / 2, true, 'n=3: λ=4L/3');
  s += txt(w / 2, h - 0.8, 'displacement antinode at open end, node at closed end', { fs: 3.4, a: 'middle', c: '#555' });
  s += '</svg>';
  return s;
}
