const $ = id => document.getElementById(id);
const fmt = (n, d = 2) => (n == null ? "—" : Number(n).toLocaleString(undefined, { minimumFractionDigits: d, maximumFractionDigits: d }));
let ws, side = "BUY", prevQty = {}, last = null, lastSnap = null;

// ---------- WebSocket ----------
function connect() {
  ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws");
  ws.onopen = () => { $("dot").classList.add("on"); $("conn").textContent = "live"; };
  ws.onclose = () => { $("dot").classList.remove("on"); $("conn").textContent = "reconnecting…"; setTimeout(connect, 1000); };
  ws.onmessage = e => { const m = JSON.parse(e.data); m.kind === "snapshot" ? render(m) : ack(m); };
}

// ---------- Rendering ----------
function ladder(el, levels, cls, maxQ) {
  el.innerHTML = levels.map(l => {
    const key = cls + l.p, old = prevQty[key], flash = old == null ? "" : l.q > old ? "flash-up" : l.q < old ? "flash-down" : "";
    prevQty[key] = l.q;
    return `<div class="row ${cls} ${flash}" data-p="${l.p}"><i class="bar" style="width:${(l.q / maxQ * 100).toFixed(0)}%"></i>
      <span class="p">${fmt(l.p)}</span><span>${l.q}</span><span>${l.n}</span></div>`;
  }).join("");
}

function render(s) {
  lastSnap = s;
  const dir = last != null && s.last != null ? (s.last > last ? "up" : s.last < last ? "down" : "") : "";
  $("t-last").textContent = fmt(s.last); $("t-last").className = "val text-lg " + dir;
  $("t-bid").textContent = fmt(s.best_bid); $("t-ask").textContent = fmt(s.best_ask);
  $("t-spread").textContent = s.best_bid && s.best_ask ? fmt(s.best_ask - s.best_bid) : "—";
  $("t-vol").textContent = fmt(s.stats.volume, 0); $("t-trades").textContent = fmt(s.stats.trades, 0);
  if (s.last != null) last = s.last;

  const maxQ = Math.max(1, ...s.asks.map(l => l.q), ...s.bids.map(l => l.q));
  ladder($("asks"), [...s.asks].reverse(), "ask", maxQ); ladder($("bids"), s.bids, "bid", maxQ);
  $("mid").textContent = fmt(s.last); $("mid").className = "midrow " + dir;

  $("tape").innerHTML = s.tape.map(t => `<div class="trow"><span class="${t.side === "BUY" ? "up" : "down"}">${fmt(t.price)}</span>
    <span>${t.qty}</span><span class="text-slate-500">${new Date(t.t * 1000).toLocaleTimeString([], { hour12: false })}</span></div>`).join("");

  $("mine").innerHTML = s.mine.length ? s.mine.map(o => `<div class="orow"><span class="${o.side === "BUY" ? "up" : "down"}">${o.side}</span>
    <span>${o.remaining} @ ${fmt(o.price)}</span><button class="xbtn" data-id="${o.id}">cancel</button></div>`).join("")
    : '<div class="text-slate-500 text-xs">No open orders. Place a limit order away from the market to see it here.</div>';

  $("e-p50").textContent = s.latency_us.p50 != null ? s.latency_us.p50.toFixed(1) + " µs" : "—";
  $("e-p99").textContent = s.latency_us.p99 != null ? s.latency_us.p99.toFixed(1) + " µs" : "—";
  $("e-orders").textContent = fmt(s.stats.orders, 0); $("e-trades").textContent = fmt(s.stats.trades, 0);

  drawCandles($("candles"), s.candles); drawDepth($("depth"), s.bids, s.asks);
}

// ---------- Order entry ----------
function setSide(s) {
  side = s;
  $("side-buy").className = "side" + (s === "BUY" ? " active-buy" : ""); $("side-sell").className = "side" + (s === "SELL" ? " active-sell" : "");
  $("submit").className = "submit mt-3" + (s === "SELL" ? " sell" : ""); $("submit").textContent = "Place " + (s === "BUY" ? "Buy" : "Sell") + " Order";
}
$("side-buy").onclick = () => setSide("BUY"); $("side-sell").onclick = () => setSide("SELL");
$("otype").onchange = () => { $("price").disabled = $("otype").value === "MARKET"; };
document.addEventListener("click", e => {
  const row = e.target.closest(".row"); if (row) $("price").value = row.dataset.p;
  const x = e.target.closest(".xbtn"); if (x && ws.readyState === 1) ws.send(JSON.stringify({ type: "cancel", id: Number(x.dataset.id) }));
});
$("submit").onclick = () => {
  if (ws.readyState !== 1) return;
  ws.send(JSON.stringify({ type: "order", side, otype: $("otype").value, price: $("price").value, qty: $("qty").value }));
};
function ack(m) {
  const el = $("msg");
  if (m.cancel) { el.className = "text-xs mt-2 " + (m.ok ? "up" : "down"); el.textContent = m.ok ? "Order cancelled" : "Order not found"; return; }
  if (!m.ok) { el.className = "text-xs mt-2 down"; el.textContent = "Rejected: " + (m.error || m.status); return; }
  el.className = "text-xs mt-2 " + (m.filled ? "up" : "text-slate-400");
  el.textContent = `${m.status.replace(/_/g, " ")}${m.filled ? ` · filled ${m.filled}` + (m.avg_price ? ` @ ${fmt(m.avg_price)}` : "") : ""}${m.remaining ? ` · ${m.remaining} resting` : ""}`;
}

// ---------- Backtest ----------
$("bt-run").onclick = async () => {
  $("bt-run").textContent = "Running…";
  const r = await fetch("/api/backtest", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fast: +$("bt-fast").value, slow: +$("bt-slow").value, seed: +$("bt-seed").value }) });
  const d = await r.json(); $("bt-run").textContent = "Run backtest";
  if (d.error) { $("bt-stats").innerHTML = `<div class="down text-xs col-span-3">${d.error}</div>`; return; }
  drawLines($("bt-chart"), [{ data: d.buy_hold, color: "#8a94a6" }, { data: d.equity, color: "#4c8dff" }]);
  const m = d.metrics, pct = v => (v * 100).toFixed(1) + "%";
  const stat = (l, v, c = "") => `<div class="stat"><div class="lbl">${l}</div><div class="val ${c}">${v}</div></div>`;
  $("bt-stats").innerHTML = stat("Return", pct(m.total_return), m.total_return >= 0 ? "up" : "down") + stat("Sharpe", m.sharpe.toFixed(2))
    + stat("Max drawdown", pct(m.max_drawdown), "down") + stat("Buy & hold", pct(m.buy_hold_return)) + stat("Volatility", pct(m.volatility)) + stat("Trades", m.trades);
};

// ---------- Benchmarks ----------
fetch("/api/benchmark").then(r => r.json()).then(b => {
  $("bench").innerHTML = b.available ? b.results.map(x => `<div class="flex justify-between border-b border-line py-1"><span>${x.name}</span>
    <span class="val text-slate-200">${x.ns_per_op.toFixed(0)} ns · ${x.ops_per_sec ? (x.ops_per_sec / 1e6).toFixed(1) + " M/s" : ""}</span></div>`).join("")
    : "Run the benchmark suite (Step 2) and the results appear here.";
});

window.addEventListener("resize", () => lastSnap && render(lastSnap));
connect();
