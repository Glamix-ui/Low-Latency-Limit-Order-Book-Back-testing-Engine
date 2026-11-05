HEAD
# Low-Latency Limit Order Book, Backtester & Institutional Web Terminal

C++ matching engine (price-time priority, LIMIT / MARKET / IOC / FOK / CANCEL / MODIFY),
exposed to Python via a C API, streamed to a browser terminal over WebSockets.

## Structure
```
lob-project/
├── CMakeLists.txt
├── engine/                      C++ core
│   ├── include/lob/
│   │   ├── Order.hpp            data models (Order, Trade, Command)
│   │   ├── OrderBook.hpp        matching engine
│   │   ├── FeedParser.hpp       text market-data feed (ITCH-style N/C/M messages)
│   │   └── engine_c_api.h       plain-C interface for ctypes
│   ├── src/engine_c_api.cpp
│   ├── tests/test_orderbook.cpp
│   └── benchmarks/              Step 2: Google Benchmark
├── server/                      Python
│   ├── engine_binding.py        ctypes wrapper
│   ├── smoke_test.py
│   ├── app.py                   Step 4: Flask + WebSocket gateway
│   ├── simulator.py             Step 4: market-maker bots so the book is always alive
│   ├── backtester.py            Step 4: moving-average crossover
│   └── analytics.py             Step 4: Sharpe, drawdown (NumPy)
├── web/                         Step 5: terminal UI
│   ├── index.html
│   └── static/{css,js}/         ladder, depth chart (Canvas), candles, order form, tape
├── data/sample_feed.txt
├── deploy/                      Step 6: Dockerfile, render.yaml
└── docs/
```

## Build (Windows, MSYS2 UCRT64 toolchain on PATH)
```
mkdir build
cd build
cmake .. -G Ninja
cmake --build .
.\bin\test_orderbook.exe
cd ..
python server\smoke_test.py
```

## Run the website locally
```
venv\Scripts\activate
python server\app.py
```
Open http://localhost:5000

## Run the benchmarks (optional, needs internet + git the first time)
```
cd build
cmake .. -G Ninja -DLOB_BUILD_BENCHMARKS=ON
cmake --build .
mkdir ..\docs
.\bin\bench_orderbook.exe --benchmark_format=json --benchmark_out=..\docs\benchmark.json
```
Restart the server and the Engine panel shows the numbers.

## Deploy
Push to GitHub, create a Render "Web Service" from the repo, and point it at `deploy/render.yaml` (Docker).
=======
# Low-Latency-Limit-Order-Book-Back-testing-Engine
A high-throughput, low-latency limit order book and backtesting engine engineered in C++ with Python-based analytics and strategy evaluation pipelines.
a8531e97b175c5062ead5dba1e05267764c21d57
