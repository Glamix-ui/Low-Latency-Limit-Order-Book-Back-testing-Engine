#pragma once
#include <algorithm>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Order.hpp"

namespace lob {

struct SubmitResult {
    OrderStatus status = OrderStatus::Rejected;
    Quantity    filled = 0;
    Quantity    remaining_on_book = 0;
};

struct LevelInfo {
    Price       price;
    Quantity    quantity;
    std::size_t orders;
};

struct BookStats {
    Quantity      volume           = 0; // total shares/contracts traded
    std::uint64_t trade_count      = 0;
    std::uint64_t orders_processed = 0;
    Price         last_price       = 0; // 0 until the first trade
};

class OrderBook {
public:
    // Pass an expected order count to pre-allocate the id index (avoids rehashing
    // in the hot path, which matters for benchmarks).
    explicit OrderBook(std::size_t expected_orders = 0) {
        if (expected_orders) index_.reserve(expected_orders);
    }

    // ---- Trading API ------------------------------------------------------

    SubmitResult submit(Order o) {
        trades_.clear();
        ++stats_.orders_processed;

        if (o.quantity == 0 ||
            (o.type != OrderType::Market && o.price <= 0) ||
            index_.count(o.id)) {
            return {OrderStatus::Rejected, 0, 0};
        }

        o.remaining = o.quantity;
        o.sequence  = ++next_seq_;

        // FOK: verify the full size is available at acceptable prices BEFORE
        // touching the book, so a failed FOK leaves no trace.
        if (o.type == OrderType::FOK && !can_fill_fully(o)) {
            return {OrderStatus::Cancelled, 0, 0};
        }

        if (o.side == Side::Buy) match(o, asks_);
        else                     match(o, bids_);

        const Quantity filled = o.quantity - o.remaining;
        if (o.remaining == 0) return {OrderStatus::Filled, filled, 0};

        if (o.type == OrderType::Limit) {
            rest(o);
            return {filled ? OrderStatus::PartiallyFilledResting : OrderStatus::Resting,
                    filled, o.remaining};
        }
        // Market / IOC leftovers are discarded.
        return {filled ? OrderStatus::PartiallyFilledCancelled : OrderStatus::Cancelled,
                filled, 0};
    }

    // O(1) id lookup + O(1) list erase (O(log n) only if the price level empties).
    bool cancel(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        const Locator loc = it->second;
        if (loc.side == Side::Buy) erase_from(bids_, loc);
        else                       erase_from(asks_, loc);
        index_.erase(it);
        return true;
    }

    // Reducing size at the same price keeps queue position (exchange convention).
    // Any price change or size increase = cancel + re-enter (loses time priority).
    SubmitResult modify(OrderId id, Price new_price, Quantity new_qty) {
        trades_.clear();
        auto it = index_.find(id);
        if (it == index_.end() || new_qty == 0 || new_price <= 0)
            return {OrderStatus::Rejected, 0, 0};

        Order& cur = *it->second.it;
        if (new_price == cur.price && new_qty <= cur.remaining) {
            const Quantity diff = cur.remaining - new_qty;
            cur.remaining = new_qty;
            cur.quantity  = std::min(cur.quantity, new_qty);
            adjust_total(it->second, diff);
            return {OrderStatus::Resting, 0, new_qty};
        }

        Order replacement = cur;
        replacement.price    = new_price;
        replacement.quantity = new_qty;
        replacement.type     = OrderType::Limit;
        cancel(id);
        return submit(replacement);
    }

    // Single entry point used by the market-data feed and the C API.
    SubmitResult apply(const Command& c) {
        switch (c.action) {
            case Action::New:    return submit(c.order);
            case Action::Cancel:
                trades_.clear();
                return cancel(c.order.id) ? SubmitResult{OrderStatus::Cancelled, 0, 0}
                                          : SubmitResult{OrderStatus::Rejected, 0, 0};
            case Action::Modify: return modify(c.order.id, c.order.price, c.order.quantity);
        }
        return {OrderStatus::Rejected, 0, 0};
    }

    // ---- Market data ------------------------------------------------------

    std::optional<Price> best_bid() const {
        return bids_.empty() ? std::nullopt : std::optional<Price>(bids_.begin()->first);
    }
    std::optional<Price> best_ask() const {
        return asks_.empty() ? std::nullopt : std::optional<Price>(asks_.begin()->first);
    }
    std::optional<Price> spread() const {
        if (bids_.empty() || asks_.empty()) return std::nullopt;
        return asks_.begin()->first - bids_.begin()->first;
    }
    std::vector<LevelInfo> bid_levels(std::size_t depth) const { return levels(bids_, depth); }
    std::vector<LevelInfo> ask_levels(std::size_t depth) const { return levels(asks_, depth); }

    // Trades produced by the most recent submit()/modify()/apply() call.
    const std::vector<Trade>& last_trades() const { return trades_; }
    const BookStats& stats() const { return stats_; }
    std::size_t resting_orders() const { return index_.size(); }

private:
    struct Level {
        std::list<Order> orders;   // FIFO = time priority; list allows O(1) mid-queue cancel
        Quantity         total = 0;
    };
    using BidMap = std::map<Price, Level, std::greater<Price>>; // best = highest
    using AskMap = std::map<Price, Level, std::less<Price>>;    // best = lowest

    struct Locator {
        Side                       side;
        Price                      price;
        std::list<Order>::iterator it;
    };

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Locator> index_;
    std::vector<Trade> trades_;
    BookStats stats_;
    Sequence next_seq_  = 0;
    Sequence trade_seq_ = 0;

    static bool crosses(const Order& taker, Price level_price) {
        if (taker.type == OrderType::Market) return true;
        return taker.side == Side::Buy ? level_price <= taker.price
                                       : level_price >= taker.price;
    }

    template <typename Map>
    void match(Order& taker, Map& book) {
        while (taker.remaining > 0 && !book.empty()) {
            auto lvl = book.begin();
            if (!crosses(taker, lvl->first)) break;

            Level& level = lvl->second;
            while (taker.remaining > 0 && !level.orders.empty()) {
                Order& maker = level.orders.front();
                const Quantity qty = std::min(taker.remaining, maker.remaining);

                trades_.push_back(Trade{taker.id, maker.id, taker.side,
                                        maker.price, qty, ++trade_seq_});
                stats_.volume      += qty;
                stats_.trade_count += 1;
                stats_.last_price   = maker.price;

                taker.remaining -= qty;
                maker.remaining -= qty;
                level.total     -= qty;

                if (maker.remaining == 0) {
                    index_.erase(maker.id);
                    level.orders.pop_front();
                }
            }
            if (level.orders.empty()) book.erase(lvl);
        }
    }

    bool can_fill_fully(const Order& o) const {
        return o.side == Side::Buy ? available(asks_, o) >= o.quantity
                                   : available(bids_, o) >= o.quantity;
    }

    template <typename Map>
    Quantity available(const Map& book, const Order& taker) const {
        Quantity sum = 0;
        for (const auto& [price, level] : book) {
            if (!crosses(taker, price)) break;
            sum += level.total;
            if (sum >= taker.quantity) break;
        }
        return sum;
    }

    void rest(const Order& o) {
        if (o.side == Side::Buy) insert(bids_, o);
        else                     insert(asks_, o);
    }

    template <typename Map>
    void insert(Map& book, const Order& o) {
        Level& level = book[o.price];
        level.orders.push_back(o);
        level.total += o.remaining;
        index_[o.id] = Locator{o.side, o.price, std::prev(level.orders.end())};
    }

    template <typename Map>
    void erase_from(Map& book, const Locator& loc) {
        auto lvl = book.find(loc.price);
        lvl->second.total -= loc.it->remaining;
        lvl->second.orders.erase(loc.it);
        if (lvl->second.orders.empty()) book.erase(lvl);
    }

    void adjust_total(const Locator& loc, Quantity diff) {
        if (loc.side == Side::Buy) bids_.find(loc.price)->second.total -= diff;
        else                       asks_.find(loc.price)->second.total -= diff;
    }

    template <typename Map>
    static std::vector<LevelInfo> levels(const Map& book, std::size_t depth) {
        std::vector<LevelInfo> out;
        for (const auto& [price, level] : book) {
            if (out.size() == depth) break;
            out.push_back({price, level.total, level.orders.size()});
        }
        return out;
    }
};

} // namespace lob
