"""Moving-average crossover backtest. Every trade is executed THROUGH the C++ engine."""
import numpy as np
from engine_binding import Engine, SIDE_BUY, SIDE_SELL, LIMIT, MARKET, to_ticks, to_price
import analytics


def synthetic_prices(bars=500, seed=1, start=100.0, drift=0.0004, vol=0.012):
    rng = np.random.default_rng(seed)
    return start * np.exp(np.cumsum(rng.normal(drift, vol, bars)))


def moving_average(x, n):
    out = np.full(len(x), np.nan)
    if n <= len(x):
        c = np.cumsum(np.insert(x, 0, 0.0))
        out[n - 1:] = (c[n:] - c[:-n]) / n
    return out


def run_backtest(prices, fast=10, slow=30, qty=100, cash=100_000.0):
    prices = np.asarray(prices, dtype=float)
    f, s = moving_average(prices, fast), moving_average(prices, slow)
    engine = Engine(expected_orders=4096)
    next_id, pos, equity, markers = 1, 0, [], []
    for t, px in enumerate(prices):
        want = qty if (not np.isnan(s[t]) and f[t] > s[t]) else 0   # long `qty` shares when fast > slow, else flat
        if want != pos and not np.isnan(s[t]):
            # Seed liquidity around this bar's price so the order has something to hit.
            seeded, mid = [], to_ticks(px)
            for k in range(1, 6):
                for side, price in ((SIDE_SELL, mid + k), (SIDE_BUY, mid - k)):
                    engine.submit(next_id, side, LIMIT, price, qty); seeded.append(next_id); next_id += 1
            side = SIDE_BUY if want > pos else SIDE_SELL
            engine.submit(next_id, side, MARKET, 0, abs(want - pos)); next_id += 1
            fills = engine.last_trades()
            filled = sum(x["qty"] for x in fills)
            if filled:
                avg = sum(x["price"] * x["qty"] for x in fills) / filled
                cash += (-1 if side == SIDE_BUY else 1) * filled * to_price(avg)
                pos += filled if side == SIDE_BUY else -filled
                markers.append({"i": t, "side": "BUY" if side == SIDE_BUY else "SELL", "price": to_price(avg)})
            for oid in seeded:
                engine.cancel(oid)
        equity.append(cash + pos * px)
    equity = np.array(equity)
    result = analytics.summarize(equity, trades=len(markers))
    bh = (100_000.0 / prices[0]) * prices   # buy & hold benchmark with the same starting capital
    result["buy_hold_return"] = float(bh[-1] / bh[0] - 1)
    return {"prices": prices.tolist(), "fast_ma": np.nan_to_num(f, nan=0).tolist(),
            "slow_ma": np.nan_to_num(s, nan=0).tolist(), "equity": equity.tolist(),
            "buy_hold": bh.tolist(), "markers": markers, "metrics": result}


if __name__ == "__main__":
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    r = run_backtest(synthetic_prices())
    print(r["metrics"])
    fig, ax = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    ax[0].plot(r["prices"], label="price"); ax[0].plot(r["fast_ma"], label="fast MA"); ax[0].plot(r["slow_ma"], label="slow MA")
    ax[0].legend(); ax[1].plot(r["equity"], label="strategy"); ax[1].plot(r["buy_hold"], label="buy & hold"); ax[1].legend()
    plt.savefig("backtest.png", dpi=120); print("saved backtest.png")
