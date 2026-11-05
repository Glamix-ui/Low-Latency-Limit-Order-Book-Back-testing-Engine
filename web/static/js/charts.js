// Small Canvas chart helpers (no libraries).
const C = { up: "#26d07c", down: "#ff4d5e", grid: "#1d2533", text: "#667186", accent: "#4c8dff", muted: "#8a94a6" };

function prep(canvas) {
  const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== w * dpr || canvas.height !== h * dpr) { canvas.width = w * dpr; canvas.height = h * dpr; }
  const ctx = canvas.getContext("2d"); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, w, h);
  ctx.font = "10px ui-monospace, Consolas, monospace"; return { ctx, w, h };
}

function yGrid(ctx, w, h, lo, hi, padR, fmt) {
  ctx.strokeStyle = C.grid; ctx.fillStyle = C.text; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = 6 + (h - 24) * i / 4, v = hi - (hi - lo) * i / 4;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w - padR, y); ctx.stroke();
    ctx.fillText(fmt(v), w - padR + 4, y + 3);
  }
}

function drawCandles(canvas, candles) {
  const { ctx, w, h } = prep(canvas); if (!candles.length) return;
  const padR = 48, lo0 = Math.min(...candles.map(c => c.l)), hi0 = Math.max(...candles.map(c => c.h));
  const m = (hi0 - lo0) * 0.1 || 0.05, lo = lo0 - m, hi = hi0 + m, ph = h - 24, pw = w - padR;
  yGrid(ctx, w, h, lo, hi, padR, v => v.toFixed(2));
  const Y = v => 6 + ph * (1 - (v - lo) / (hi - lo)), step = pw / Math.max(candles.length, 30), bw = Math.max(2, step * 0.62);
  candles.forEach((c, i) => {
    const x = i * step + step / 2, col = c.c >= c.o ? C.up : C.down;
    ctx.strokeStyle = ctx.fillStyle = col;
    ctx.beginPath(); ctx.moveTo(x, Y(c.h)); ctx.lineTo(x, Y(c.l)); ctx.stroke();
    ctx.fillRect(x - bw / 2, Math.min(Y(c.o), Y(c.c)), bw, Math.max(1, Math.abs(Y(c.o) - Y(c.c))));
  });
  const last = candles[candles.length - 1].c; ctx.fillStyle = C.accent;
  ctx.fillRect(pw, Y(last) - 8, padR, 16); ctx.fillStyle = "#fff"; ctx.fillText(last.toFixed(2), pw + 4, Y(last) + 3);
}

function drawDepth(canvas, bids, asks) {
  const { ctx, w, h } = prep(canvas); if (!bids.length || !asks.length) return;
  const cum = arr => { let s = 0; return arr.map(l => ({ p: l.p, c: (s += l.q) })); };
  const B = cum(bids), A = cum(asks), maxC = Math.max(B[B.length - 1].c, A[A.length - 1].c);
  const lo = B[B.length - 1].p, hi = A[A.length - 1].p, ph = h - 20;
  const X = p => (p - lo) / (hi - lo) * w, Y = c => 4 + ph * (1 - c / maxC);
  const area = (pts, col, fill) => {
    ctx.beginPath(); ctx.moveTo(X(pts[0].p), Y(0));
    pts.forEach((pt, i) => { ctx.lineTo(X(pt.p), Y(i ? pts[i - 1].c : 0)); ctx.lineTo(X(pt.p), Y(pt.c)); });
    ctx.lineTo(X(pts[pts.length - 1].p), Y(0)); ctx.closePath(); ctx.fillStyle = fill; ctx.fill();
    ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
  };
  area(B, C.up, "rgba(38,208,124,.18)"); area(A, C.down, "rgba(255,77,94,.18)");
  ctx.fillStyle = C.text; ctx.fillText(lo.toFixed(2), 4, h - 4); ctx.fillText(hi.toFixed(2), w - 40, h - 4);
  ctx.fillText(((bids[0].p + asks[0].p) / 2).toFixed(2), w / 2 - 14, h - 4);
}

function drawLines(canvas, series) {
  const { ctx, w, h } = prep(canvas), all = series.flatMap(s => s.data); if (!all.length) return;
  const padR = 52, lo = Math.min(...all), hi = Math.max(...all), pw = w - padR;
  yGrid(ctx, w, h, lo, hi, padR, v => (v / 1000).toFixed(1) + "k");
  series.forEach(s => {
    ctx.beginPath(); ctx.strokeStyle = s.color; ctx.lineWidth = 1.6;
    s.data.forEach((v, i) => { const x = i / (s.data.length - 1) * pw, y = 6 + (h - 24) * (1 - (v - lo) / (hi - lo || 1)); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
    ctx.stroke();
  });
}
