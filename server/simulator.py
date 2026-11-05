"""Market-maker / noise-trader bots that keep the order book alive for visitors."""
import random
from engine_binding import SIDE_BUY, SIDE_SELL, LIMIT, IOC, to_ticks


class MarketSimulator:
    def __init__(self, engine, start_price=100.0, seed=7):
        self.e = engine
        self.rng = random.Random(seed)
        self.start = to_ticks(start_price)
        self.mid = self.start
        self.next_id = 1
        self.live = []
        for k in range(1, 13):                       # initial depth on both sides
            self._limit(SIDE_BUY, self.mid - k, 100 + 20 * k)
            self._limit(SIDE_SELL, self.mid + k, 100 + 20 * k)

    def _limit(self, side, price, qty):
        oid, self.next_id = self.next_id, self.next_id + 1
        res = self.e.submit(oid, side, LIMIT, price, qty)
        if res["status"] in ("RESTING", "PARTIAL_RESTING"):
            self.live.append(oid)
        return self.e.last_trades()

    def step(self, n=1):
        """Run n random actions; returns the trades they produced."""
        trades, rng = [], self.rng
        for _ in range(n):
            self.mid = max(int(self.start * 0.7), min(int(self.start * 1.3),
                           self.mid + rng.choice((-1, 0, 0, 1)) + (1 if self.mid < self.start else -1) * (rng.random() < 0.05)))
            roll = rng.random()
            if roll < 0.55:                          # passive quote near the mid
                side = rng.choice((SIDE_BUY, SIDE_SELL))
                off = int(abs(rng.gauss(0, 5))) + 1
                price = self.mid - off if side == SIDE_BUY else self.mid + off
                trades += self._limit(side, price, rng.randint(1, 10) * 10)
            elif roll < 0.85 and self.live:          # cancel a random resting order
                k = rng.randrange(len(self.live))
                self.live[k], self.live[-1] = self.live[-1], self.live[k]
                self.e.cancel(self.live.pop())
            else:                                    # aggressive order that takes liquidity
                side = rng.choice((SIDE_BUY, SIDE_SELL))
                oid, self.next_id = self.next_id, self.next_id + 1
                px = self.mid + (6 if side == SIDE_BUY else -6)
                self.e.submit(oid, side, IOC, px, rng.randint(1, 6) * 10)
                trades += self.e.last_trades()
            if len(self.live) > 400:                 # keep the book bounded
                self.e.cancel(self.live.pop(0))
        return trades
