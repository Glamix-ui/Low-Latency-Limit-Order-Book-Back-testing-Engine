#include "lob/engine_c_api.h"
#include "lob/FeedParser.hpp"

#include <algorithm>
#include <string_view>

using namespace lob;

namespace {
inline OrderBook* B(void* h) { return static_cast<OrderBook*>(h); }
inline LobResult to_c(const SubmitResult& r) {
    return LobResult{static_cast<uint64_t>(r.status), r.filled, r.remaining_on_book};
}
}

extern "C" {

void* lob_create(uint64_t expected_orders) { return new OrderBook(static_cast<std::size_t>(expected_orders)); }
void  lob_destroy(void* book) { delete B(book); }

LobResult lob_submit(void* book, uint64_t id, int side, int type, int64_t price, uint64_t qty) {
    Order o;
    o.id = id;
    o.side = side == 0 ? Side::Buy : Side::Sell;
    o.type = static_cast<OrderType>(std::clamp(type, 0, 3));
    o.price = price;
    o.quantity = qty;
    return to_c(B(book)->submit(o));
}

int lob_cancel(void* book, uint64_t id) { return B(book)->cancel(id) ? 1 : 0; }

LobResult lob_modify(void* book, uint64_t id, int64_t price, uint64_t qty) {
    return to_c(B(book)->modify(id, price, qty));
}

int lob_best_bid(void* book, int64_t* out) {
    auto p = B(book)->best_bid();
    if (!p) return 0;
    *out = *p; return 1;
}
int lob_best_ask(void* book, int64_t* out) {
    auto p = B(book)->best_ask();
    if (!p) return 0;
    *out = *p; return 1;
}

uint64_t lob_depth(void* book, int side, uint64_t max_levels, LobLevel* out) {
    auto lv = side == 0 ? B(book)->bid_levels(max_levels) : B(book)->ask_levels(max_levels);
    for (std::size_t i = 0; i < lv.size(); ++i)
        out[i] = LobLevel{lv[i].price, lv[i].quantity, lv[i].orders};
    return lv.size();
}

uint64_t lob_last_trades(void* book, uint64_t max_trades, LobTrade* out) {
    const auto& t = B(book)->last_trades();
    const std::size_t n = std::min<std::size_t>(t.size(), max_trades);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = LobTrade{t[i].taker_id, t[i].maker_id, t[i].price, t[i].quantity,
                          t[i].sequence, static_cast<uint64_t>(t[i].taker_side)};
    return n;
}

void lob_stats(void* book, LobStats* out) {
    const auto& s = B(book)->stats();
    *out = LobStats{s.volume, s.trade_count, s.orders_processed, s.last_price};
}

int lob_feed_line(void* book, const char* line) {
    auto cmd = parse_line(std::string_view(line));
    if (!cmd) return -1;
    return static_cast<int>(B(book)->apply(*cmd).status);
}

} // extern "C"
