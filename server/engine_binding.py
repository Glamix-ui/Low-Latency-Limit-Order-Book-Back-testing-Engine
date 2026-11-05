"""ctypes bridge to the C++ matching engine (build/bin/lob_engine.dll or .so)."""
import ctypes
import os
import sys
from pathlib import Path

TICK = 100  # 1 tick = $0.01  ->  price 100.25 == 10025 ticks

SIDE_BUY, SIDE_SELL = 0, 1
LIMIT, MARKET, IOC, FOK = 0, 1, 2, 3
STATUS = ["FILLED", "RESTING", "PARTIAL_RESTING", "PARTIAL_CANCELLED", "CANCELLED", "REJECTED"]


def to_ticks(price: float) -> int:
    return int(round(price * TICK))


def to_price(ticks: int) -> float:
    return ticks / TICK


class _Result(ctypes.Structure):
    _fields_ = [("status", ctypes.c_uint64), ("filled", ctypes.c_uint64), ("remaining", ctypes.c_uint64)]


class _Level(ctypes.Structure):
    _fields_ = [("price", ctypes.c_int64), ("quantity", ctypes.c_uint64), ("orders", ctypes.c_uint64)]


class _Trade(ctypes.Structure):
    _fields_ = [("taker_id", ctypes.c_uint64), ("maker_id", ctypes.c_uint64), ("price", ctypes.c_int64),
                ("quantity", ctypes.c_uint64), ("sequence", ctypes.c_uint64), ("taker_side", ctypes.c_uint64)]


class _Stats(ctypes.Structure):
    _fields_ = [("volume", ctypes.c_uint64), ("trade_count", ctypes.c_uint64),
                ("orders_processed", ctypes.c_uint64), ("last_price", ctypes.c_int64)]


def _find_library() -> str:
    env = os.environ.get("LOB_ENGINE_PATH")
    if env:
        return env
    root = Path(__file__).resolve().parent.parent
    if sys.platform == "win32":
        name = "lob_engine.dll"
    elif sys.platform == "darwin":
        name = "lob_engine.dylib"
    else:
        name = "lob_engine.so"
    for folder in (root / "build" / "bin", root / "build", root / "server"):
        candidate = folder / name
        if candidate.exists():
            return str(candidate)
    raise FileNotFoundError(f"Could not find {name}. Build the C++ engine first (see README).")


class Engine:
    """One order book. Prices in/out are ticks unless a method says otherwise."""

    def __init__(self, expected_orders: int = 1_000_000, lib_path: str | None = None):
        self._lib = ctypes.CDLL(lib_path or _find_library())
        L = self._lib
        L.lob_create.restype = ctypes.c_void_p
        L.lob_create.argtypes = [ctypes.c_uint64]
        L.lob_destroy.argtypes = [ctypes.c_void_p]
        L.lob_submit.restype = _Result
        L.lob_submit.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int64, ctypes.c_uint64]
        L.lob_cancel.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        L.lob_modify.restype = _Result
        L.lob_modify.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int64, ctypes.c_uint64]
        L.lob_best_bid.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int64)]
        L.lob_best_ask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int64)]
        L.lob_depth.restype = ctypes.c_uint64
        L.lob_depth.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64, ctypes.POINTER(_Level)]
        L.lob_last_trades.restype = ctypes.c_uint64
        L.lob_last_trades.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(_Trade)]
        L.lob_stats.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Stats)]
        L.lob_feed_line.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._h = L.lob_create(expected_orders)

    def close(self):
        if self._h:
            self._lib.lob_destroy(self._h)
            self._h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    # ---- trading ----
    def submit(self, order_id: int, side: int, order_type: int, price_ticks: int, qty: int) -> dict:
        r = self._lib.lob_submit(self._h, order_id, side, order_type, price_ticks, qty)
        return {"status": STATUS[r.status], "filled": r.filled, "remaining": r.remaining}

    def cancel(self, order_id: int) -> bool:
        return bool(self._lib.lob_cancel(self._h, order_id))

    def modify(self, order_id: int, price_ticks: int, qty: int) -> dict:
        r = self._lib.lob_modify(self._h, order_id, price_ticks, qty)
        return {"status": STATUS[r.status], "filled": r.filled, "remaining": r.remaining}

    def feed_line(self, line: str) -> int:
        """Apply one text-protocol line (see FeedParser.hpp). Returns status code, -1 if malformed."""
        return self._lib.lob_feed_line(self._h, line.encode())

    # ---- market data ----
    def best_bid(self):
        v = ctypes.c_int64()
        return v.value if self._lib.lob_best_bid(self._h, ctypes.byref(v)) else None

    def best_ask(self):
        v = ctypes.c_int64()
        return v.value if self._lib.lob_best_ask(self._h, ctypes.byref(v)) else None

    def depth(self, side: int, levels: int = 15) -> list[dict]:
        buf = (_Level * levels)()
        n = self._lib.lob_depth(self._h, side, levels, buf)
        return [{"price": buf[i].price, "qty": buf[i].quantity, "orders": buf[i].orders} for i in range(n)]

    def last_trades(self, max_trades: int = 1024) -> list[dict]:
        buf = (_Trade * max_trades)()
        n = self._lib.lob_last_trades(self._h, max_trades, buf)
        return [{"taker": buf[i].taker_id, "maker": buf[i].maker_id, "price": buf[i].price,
                 "qty": buf[i].quantity, "seq": buf[i].sequence,
                 "taker_side": "BUY" if buf[i].taker_side == 0 else "SELL"} for i in range(n)]

    def stats(self) -> dict:
        s = _Stats()
        self._lib.lob_stats(self._h, ctypes.byref(s))
        return {"volume": s.volume, "trades": s.trade_count, "orders": s.orders_processed,
                "last_price": s.last_price or None}
