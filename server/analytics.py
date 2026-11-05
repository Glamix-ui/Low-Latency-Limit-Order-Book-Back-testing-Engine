"""Performance and risk metrics (NumPy)."""
import numpy as np


def sharpe_ratio(returns, periods_per_year=252, risk_free=0.0):
    """Annualised Sharpe ratio of per-period returns."""
    r = np.asarray(returns, dtype=float) - risk_free / periods_per_year
    sd = r.std(ddof=1) if len(r) > 1 else 0.0
    return 0.0 if sd == 0 else float(np.sqrt(periods_per_year) * r.mean() / sd)


def max_drawdown(equity):
    """Largest peak-to-trough fall as a negative fraction (e.g. -0.12 = -12%)."""
    eq = np.asarray(equity, dtype=float)
    peak = np.maximum.accumulate(eq)
    return float(((eq - peak) / peak).min())


def summarize(equity, trades=0, periods_per_year=252):
    eq = np.asarray(equity, dtype=float)
    returns = np.diff(eq) / eq[:-1]
    return {
        "total_return": float(eq[-1] / eq[0] - 1),
        "sharpe": sharpe_ratio(returns, periods_per_year),
        "max_drawdown": max_drawdown(eq),
        "volatility": float(returns.std(ddof=1) * np.sqrt(periods_per_year)) if len(returns) > 1 else 0.0,
        "trades": int(trades),
    }
