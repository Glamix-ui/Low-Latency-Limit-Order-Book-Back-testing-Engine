"""Run after building the engine:  python server/smoke_test.py"""
from engine_binding import *

e = Engine(expected_orders=1000)
print("1) rest a sell and a buy:")
print("  ", e.submit(1, SIDE_SELL, LIMIT, to_ticks(100.10), 50))
print("  ", e.submit(2, SIDE_BUY, LIMIT, to_ticks(99.90), 40))
print("   best bid/ask:", to_price(e.best_bid()), to_price(e.best_ask()))

print("2) aggressive buy crosses the spread:")
print("  ", e.submit(3, SIDE_BUY, LIMIT, to_ticks(100.10), 20))
print("   trades:", e.last_trades())

print("3) FOK that cannot fully fill leaves the book untouched:")
print("  ", e.submit(4, SIDE_BUY, FOK, to_ticks(100.10), 500))

print("4) depth:", e.depth(SIDE_BUY, 5), e.depth(SIDE_SELL, 5))
print("5) cancel:", e.cancel(2), "| cancel again:", e.cancel(2))

print("6) text feed:")
for line in open("data/sample_feed.txt"):
    if line.strip() and not line.startswith("#"):
        e.feed_line(line)
print("   stats:", e.stats())
print("OK")
