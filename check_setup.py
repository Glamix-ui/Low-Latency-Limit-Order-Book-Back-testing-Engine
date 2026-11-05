"""Run from the project root:  python check_setup.py
Checks that every file is in the right place and your tools are installed."""
import importlib, shutil, sys
from pathlib import Path

EXPECTED = """CMakeLists.txt README.md data/sample_feed.txt deploy/Dockerfile deploy/render.yaml
engine/benchmarks/bench_orderbook.cpp engine/include/lob/FeedParser.hpp engine/include/lob/Order.hpp
engine/include/lob/OrderBook.hpp engine/include/lob/engine_c_api.h engine/src/engine_c_api.cpp
engine/tests/test_orderbook.cpp server/analytics.py server/app.py server/backtester.py
server/engine_binding.py server/requirements.txt server/simulator.py server/smoke_test.py
web/index.html web/static/css/terminal.css web/static/js/app.js web/static/js/charts.js""".split()

root, problems = Path(__file__).resolve().parent, 0
print(f"Project folder: {root}\n")
for rel in EXPECTED:
    p = root / rel
    if not p.exists():
        extra = [x.name for x in p.parent.glob(p.name + ".*")] if p.parent.exists() else []
        hint = f"  <-- found {extra} (hidden extension? rename it)" if extra else ""
        print(f"MISSING  {rel}{hint}"); problems += 1
    elif p.stat().st_size == 0:
        print(f"EMPTY    {rel}  (file exists but has no code; paste it in and press Ctrl+S)"); problems += 1
if not problems:
    print("OK  all 24 files present and non-empty")

print()
if sys.version_info < (3, 10):
    print(f"PYTHON   need 3.10+, you have {sys.version.split()[0]}"); problems += 1
else:
    print(f"OK  Python {sys.version.split()[0]}")
for tool in ("g++", "cmake", "ninja"):
    if shutil.which(tool): print(f"OK  {tool} found")
    else: print(f"MISSING  {tool} not on PATH (see the Windows setup steps)"); problems += 1
for mod in ("flask", "flask_sock", "numpy", "matplotlib"):
    try: importlib.import_module(mod); print(f"OK  python package {mod}")
    except ImportError: print(f"MISSING  python package {mod}  ->  pip install -r server\\requirements.txt"); problems += 1
if not (root / ".venv").exists() and not (root / "venv").exists():
    print("NOTE  no venv folder found (fine if you installed packages another way)")
print("\nAll good!" if not problems else f"\n{problems} problem(s) found. Fix them and run again.")
