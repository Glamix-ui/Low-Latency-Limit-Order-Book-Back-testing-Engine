#include <iostream>
#include <sstream>
#include "lob/FeedParser.hpp"

using namespace lob;

static Order mk(OrderId id, Side s, OrderType t, Price p, Quantity q) {
    Order o; o.id = id; o.side = s; o.type = t; o.price = p; o.quantity = q; return o;
}

#define CHECK(c) do { if(!(c)) { std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; return 1; } } while(0)

int main() {
    // 1. Price-time priority
    {
        OrderBook b;
        b.submit(mk(1, Side::Sell, OrderType::Limit, 100, 5));
        b.submit(mk(2, Side::Sell, OrderType::Limit, 100, 5));
        b.submit(mk(3, Side::Sell, OrderType::Limit, 99, 5));
        auto r = b.submit(mk(4, Side::Buy, OrderType::Limit, 100, 12));
        CHECK(r.status == OrderStatus::Filled);
        auto& t = b.last_trades();
        CHECK(t.size() == 3);
        CHECK(t[0].maker_id == 3 && t[0].price == 99);
        CHECK(t[1].maker_id == 1);
        CHECK(t[2].maker_id == 2 && t[2].quantity == 2);
        CHECK(b.best_ask() == 100);
        CHECK(b.stats().volume == 12 && b.stats().trade_count == 3 && b.stats().last_price == 100);
    }
    // 2. Resting + partial fill
    {
        OrderBook b;
        b.submit(mk(1, Side::Sell, OrderType::Limit, 101, 3));
        auto r = b.submit(mk(2, Side::Buy, OrderType::Limit, 101, 10));
        CHECK(r.status == OrderStatus::PartiallyFilledResting && r.filled == 3 && r.remaining_on_book == 7);
        CHECK(b.best_bid() == 101 && !b.best_ask());
    }
    // 3. Market never rests
    {
        OrderBook b;
        b.submit(mk(1, Side::Sell, OrderType::Limit, 100, 4));
        auto r = b.submit(mk(2, Side::Buy, OrderType::Market, 0, 10));
        CHECK(r.status == OrderStatus::PartiallyFilledCancelled && r.filled == 4);
        CHECK(b.resting_orders() == 0);
    }
    // 4. IOC and FOK
    {
        OrderBook b;
        b.submit(mk(1, Side::Sell, OrderType::Limit, 100, 5));
        auto r = b.submit(mk(2, Side::Buy, OrderType::FOK, 100, 6));
        CHECK(r.status == OrderStatus::Cancelled && b.last_trades().empty());
        CHECK(b.ask_levels(1)[0].quantity == 5);
        r = b.submit(mk(3, Side::Buy, OrderType::FOK, 100, 5));
        CHECK(r.status == OrderStatus::Filled);
        b.submit(mk(4, Side::Sell, OrderType::Limit, 100, 2));
        r = b.submit(mk(5, Side::Buy, OrderType::IOC, 100, 9));
        CHECK(r.status == OrderStatus::PartiallyFilledCancelled && r.filled == 2);
        CHECK(b.resting_orders() == 0);
    }
    // 5. Cancel and modify
    {
        OrderBook b;
        b.submit(mk(1, Side::Buy, OrderType::Limit, 100, 10));
        b.submit(mk(2, Side::Buy, OrderType::Limit, 100, 10));
        CHECK(b.modify(1, 100, 4).status == OrderStatus::Resting);   // keeps priority
        b.submit(mk(3, Side::Sell, OrderType::Limit, 100, 5));
        CHECK(b.last_trades()[0].maker_id == 1 && b.last_trades()[0].quantity == 4);
        CHECK(b.cancel(2));
        CHECK(!b.cancel(2));
        CHECK(b.bid_levels(5).empty());
        // price change loses priority
        b.submit(mk(10, Side::Buy, OrderType::Limit, 100, 1));
        b.submit(mk(11, Side::Buy, OrderType::Limit, 100, 1));
        b.modify(10, 99, 1);
        b.modify(10, 100, 1);
        b.submit(mk(12, Side::Sell, OrderType::Limit, 100, 1));
        CHECK(b.last_trades()[0].maker_id == 11);
    }
    // 6. Rejections
    {
        OrderBook b;
        CHECK(b.submit(mk(1, Side::Buy, OrderType::Limit, 100, 0)).status == OrderStatus::Rejected);
        b.submit(mk(2, Side::Buy, OrderType::Limit, 100, 1));
        CHECK(b.submit(mk(2, Side::Buy, OrderType::Limit, 100, 1)).status == OrderStatus::Rejected);
        CHECK(b.modify(99, 100, 1).status == OrderStatus::Rejected);
    }
    // 7. Feed parser + replay
    {
        CHECK(parse_line("N,1,B,L,10000,5").has_value());
        CHECK(!parse_line("N,1,X,L,10000,5").has_value());
        CHECK(!parse_line("N,1,B,L,10000").has_value());
        CHECK(!parse_line("Z,1").has_value());
        std::istringstream feed(
            "# sample\n"
            "N,1,S,L,10100,10\n"
            "N,2,B,L,10000,10\n"
            "N,3,B,L,10100,4\n"      // trades 4 with order 1
            "M,2,10050,10\n"         // reprice bid
            "C,1\n"                  // cancel remaining ask
            "garbage line\n");
        OrderBook b;
        auto st = replay(b, feed);
        CHECK(st.applied == 5 && st.malformed == 1 && st.trades == 1);
        CHECK(b.best_bid() == 10050 && !b.best_ask());
    }
    std::cout << "All tests passed\n";
    return 0;
}
