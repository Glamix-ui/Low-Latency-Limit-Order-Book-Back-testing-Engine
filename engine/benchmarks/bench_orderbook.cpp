// Google Benchmark suite. Run:
//   .\bin\bench_orderbook.exe --benchmark_format=json --benchmark_out=..\docs\benchmark.json
// The web terminal reads docs/benchmark.json and shows the numbers on its Engine panel.
#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include "lob/OrderBook.hpp"

using namespace lob;

static Order mk(OrderId id, Side s, OrderType t, Price p, Quantity q) {
    Order o; o.id = id; o.side = s; o.type = t; o.price = p; o.quantity = q; return o;
}

// Cost of resting a passive limit order and cancelling it, on a book with ~2000 levels.
static void BM_AddCancel(benchmark::State& state) {
    OrderBook book(1 << 16);
    for (int i = 1; i <= 1000; ++i) {
        book.submit(mk(i, Side::Buy,  OrderType::Limit, 10000 - i, 100));
        book.submit(mk(100000 + i, Side::Sell, OrderType::Limit, 10001 + i, 100));
    }
    OrderId id = 1'000'000;
    for (auto _ : state) {
        auto r = book.submit(mk(++id, Side::Buy, OrderType::Limit, 9500, 10));
        benchmark::DoNotOptimize(r);
        book.cancel(id);
    }
    state.SetItemsProcessed(state.iterations() * 2);  // one add + one cancel
}
BENCHMARK(BM_AddCancel);

// Cost of a marketable order that executes against deep resting liquidity.
static void BM_MatchAggressive(benchmark::State& state) {
    OrderBook book(1 << 16);
    for (int i = 0; i < 50; ++i)  // huge quantities so liquidity never runs out
        book.submit(mk(i + 1, Side::Sell, OrderType::Limit, 10001 + i, 1ULL << 50));
    OrderId id = 1'000'000;
    for (auto _ : state) {
        auto r = book.submit(mk(++id, Side::Buy, OrderType::IOC, 10100, 1));
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MatchAggressive);

// Realistic mix: 60% passive limits near the mid, 30% cancels, 10% marketable orders.
static void BM_MixedWorkload(benchmark::State& state) {
    constexpr std::size_t N = 200000;
    std::mt19937_64 rng(42);
    std::vector<Command> ops;
    ops.reserve(N);
    std::vector<OrderId> live;
    OrderId id = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const int roll = static_cast<int>(rng() % 100);
        Command c;
        if (roll < 60 || live.empty()) {
            const bool buy = rng() & 1;
            const Price off = static_cast<Price>(rng() % 20) + 1;
            c.action = Action::New;
            c.order = mk(++id, buy ? Side::Buy : Side::Sell, OrderType::Limit,
                         buy ? 10000 - off : 10000 + off, 10 + rng() % 90);
            live.push_back(id);
        } else if (roll < 90) {
            const std::size_t k = rng() % live.size();
            c.action = Action::Cancel;
            c.order.id = live[k];
            live[k] = live.back(); live.pop_back();
        } else {
            c.action = Action::New;
            c.order = mk(++id, (rng() & 1) ? Side::Buy : Side::Sell, OrderType::IOC,
                         (rng() & 1) ? 10030 : 9970, 10 + rng() % 40);
        }
        ops.push_back(c);
    }
    OrderBook book(N);
    std::size_t i = 0;
    for (auto _ : state) {
        if (i == N) {
            state.PauseTiming();
            book = OrderBook(N);
            i = 0;
            state.ResumeTiming();
        }
        auto r = book.apply(ops[i++]);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MixedWorkload);

BENCHMARK_MAIN();
