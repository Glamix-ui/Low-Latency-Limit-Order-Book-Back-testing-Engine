"""Flask + WebSocket gateway.  Run:  python server/app.py   ->  http://localhost:5000"""
import collections, json, threading, time
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory
from flask_sock import Sock

import backtester
from engine_binding import Engine, SIDE_BUY, SIDE_SELL, LIMIT, MARKET, IOC, FOK, to_ticks, to_price
from simulator import MarketSimulator

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
app = Flask(__name__, static_folder=str(WEB / "static"), static_url_path="/static")
sock = Sock(app)

SIDES = {"BUY": SIDE_BUY, "SELL": SIDE_SELL}
TYPES = {"LIMIT": LIMIT, "MARKET": MARKET, "IOC": IOC, "FOK": FOK}
CANDLE_SECONDS = 2


class Exchange:
    def __init__(self):
        self.lock = threading.RLock()
        self.engine = Engine(expected_orders=2_000_000)
        self.sim = MarketSimulator(self.engine, start_price=100.0)
        self.tape = collections.deque(maxlen=40)
        self.candles = collections.deque(maxlen=120)
        self.mine = {}                       # my resting orders: id -> info
        self.latency_ns = collections.deque(maxlen=1000)
        self.next_id = 9_000_000_000

    # ---- bookkeeping ----
    def record(self, trades):
        now = time.time()
        bucket = int(now // CANDLE_SECONDS * CANDLE_SECONDS)
        for t in trades:
            p, q = to_price(t["price"]), t["qty"]
            self.tape.appendleft({"t": now, "price": p, "qty": q, "side": t["taker_side"]})
            if self.candles and self.candles[-1]["t"] == bucket:
                c = self.candles[-1]
                c["h"], c["l"], c["c"], c["v"] = max(c["h"], p), min(c["l"], p), p, c["v"] + q
            else:
                self.candles.append({"t": bucket, "o": p, "h": p, "l": p, "c": p, "v": q})
            m = self.mine.get(t["maker"])
            if m:
                m["remaining"] -= q
                if m["remaining"] <= 0:
                    del self.mine[t["maker"]]

    def tick(self):
        with self.lock:
            t0 = time.perf_counter_ns()
            trades = self.sim.step(8)
            self.latency_ns.append((time.perf_counter_ns() - t0) / 8)
            self.record(trades)

    # ---- user actions ----
    def place(self, side, otype, price, qty):
        side, otype = SIDES.get(str(side).upper()), TYPES.get(str(otype).upper())
        try:
            price, qty = float(price or 0), int(qty)
        except (TypeError, ValueError):
            return {"ok": False, "error": "price/qty must be numbers"}
        if side is None or otype is None or qty <= 0 or (otype != MARKET and price <= 0):
            return {"ok": False, "error": "invalid order"}
        with self.lock:
            oid, self.next_id = self.next_id, self.next_id + 1
            t0 = time.perf_counter_ns()
            res = self.engine.submit(oid, side, otype, to_ticks(price), qty)
            self.latency_ns.append(time.perf_counter_ns() - t0)
            fills = self.engine.last_trades()
            self.record(fills)
            if res["status"] in ("RESTING", "PARTIAL_RESTING"):
                self.mine[oid] = {"id": oid, "side": "BUY" if side == SIDE_BUY else "SELL",
                                  "price": price, "remaining": res["remaining"]}
            avg = sum(f["price"] * f["qty"] for f in fills) / sum(f["qty"] for f in fills) if fills else None
            return {"ok": res["status"] != "REJECTED", "id": oid, **res,
                    "avg_price": to_price(avg) if avg else None}

    def cancel(self, oid):
        with self.lock:
            ok = self.engine.cancel(int(oid))
            self.mine.pop(int(oid), None)
            return {"ok": ok, "id": int(oid)}

    def snapshot(self):
        with self.lock:
            lv = lambda side: [{"p": to_price(x["price"]), "q": x["qty"], "n": x["orders"]}
                               for x in self.engine.depth(side, 14)]
            lat = sorted(self.latency_ns)
            pct = lambda q: lat[min(len(lat) - 1, int(len(lat) * q))] / 1000 if lat else None
            st = self.engine.stats()
            bb, ba = self.engine.best_bid(), self.engine.best_ask()
            return {"kind": "snapshot", "bids": lv(SIDE_BUY), "asks": lv(SIDE_SELL),
                    "best_bid": to_price(bb) if bb else None, "best_ask": to_price(ba) if ba else None,
                    "last": to_price(st["last_price"]) if st["last_price"] else None,
                    "stats": st, "tape": list(self.tape)[:25], "candles": list(self.candles)[-60:],
                    "mine": list(self.mine.values()),
                    "latency_us": {"p50": pct(0.5), "p99": pct(0.99)}}


ex = Exchange()


def _sim_loop():
    while True:
        ex.tick()
        time.sleep(0.05)


threading.Thread(target=_sim_loop, daemon=True).start()


# ---- routes ----
@app.get("/")
def index():
    return send_from_directory(WEB, "index.html")


@app.get("/api/snapshot")
def api_snapshot():
    return jsonify(ex.snapshot())


@app.post("/api/order")
def api_order():
    d = request.get_json(force=True, silent=True) or {}
    return jsonify(ex.place(d.get("side"), d.get("type"), d.get("price"), d.get("qty")))


@app.post("/api/cancel")
def api_cancel():
    d = request.get_json(force=True, silent=True) or {}
    return jsonify(ex.cancel(d.get("id", -1)))


@app.post("/api/backtest")
def api_backtest():
    d = request.get_json(force=True, silent=True) or {}
    fast, slow = int(d.get("fast", 10)), int(d.get("slow", 30))
    bars = min(int(d.get("bars", 500)), 2000)
    if not (1 <= fast < slow < bars):
        return jsonify({"error": "need 1 <= fast < slow < bars"}), 400
    prices = backtester.synthetic_prices(bars=bars, seed=int(d.get("seed", 1)))
    return jsonify(backtester.run_backtest(prices, fast=fast, slow=slow))


@app.get("/api/benchmark")
def api_benchmark():
    path = ROOT / "docs" / "benchmark.json"      # produced by Google Benchmark (Step 2)
    if not path.exists():
        return jsonify({"available": False, "results": []})
    data = json.loads(path.read_text())
    rows = [{"name": b["name"], "ns_per_op": b["real_time"], "ops_per_sec": b.get("items_per_second")}
            for b in data.get("benchmarks", [])]
    return jsonify({"available": True, "results": rows})


@sock.route("/ws")
def ws(ws):
    """Bi-directional: client sends orders/cancels, server streams book snapshots."""
    while True:
        msg = ws.receive(timeout=0.25)
        if msg:
            try:
                d = json.loads(msg)
                if d.get("type") == "order":
                    ws.send(json.dumps({"kind": "ack", **ex.place(d.get("side"), d.get("otype"), d.get("price"), d.get("qty"))}))
                elif d.get("type") == "cancel":
                    ws.send(json.dumps({"kind": "ack", "cancel": True, **ex.cancel(d.get("id", -1))}))
            except Exception as e:
                ws.send(json.dumps({"kind": "ack", "ok": False, "error": str(e)}))
        ws.send(json.dumps(ex.snapshot()))


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, threaded=True)
