#pragma once
// Lightweight text market-data protocol (one message per line, CSV fields):
//   N,<id>,<B|S>,<L|M|I|F>,<price_ticks>,<qty>   new order (Limit/Market/IOC/FOK)
//   C,<id>                                       cancel
//   M,<id>,<price_ticks>,<qty>                   modify
//   # comment lines and blank lines are ignored
// This is the same message shape as ITCH-style add/cancel/replace events, in
// readable form. A binary decoder can reuse apply() unchanged.
#include <charconv>
#include <istream>
#include <optional>
#include <string>
#include <string_view>

#include "OrderBook.hpp"

namespace lob {
namespace detail {

template <typename T>
inline bool parse_num(std::string_view s, T& out) {
    if (s.empty()) return false;
    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{} && res.ptr == s.data() + s.size();
}

inline std::string_view next_field(std::string_view& rest) {
    const auto pos = rest.find(',');
    std::string_view f = rest.substr(0, pos);
    rest = (pos == std::string_view::npos) ? std::string_view{} : rest.substr(pos + 1);
    return f;
}

} // namespace detail

inline std::optional<Command> parse_line(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.remove_suffix(1);
    if (line.empty() || line.front() == '#') return std::nullopt;

    std::string_view rest = line;
    const std::string_view kind = detail::next_field(rest);
    Command c;

    if (kind == "N") {
        auto id = detail::next_field(rest), side = detail::next_field(rest),
             type = detail::next_field(rest), price = detail::next_field(rest),
             qty = detail::next_field(rest);
        if (!rest.empty()) return std::nullopt;
        c.action = Action::New;
        if (!detail::parse_num(id, c.order.id) || !detail::parse_num(price, c.order.price) ||
            !detail::parse_num(qty, c.order.quantity)) return std::nullopt;
        if (side == "B") c.order.side = Side::Buy;
        else if (side == "S") c.order.side = Side::Sell;
        else return std::nullopt;
        if (type == "L") c.order.type = OrderType::Limit;
        else if (type == "M") c.order.type = OrderType::Market;
        else if (type == "I") c.order.type = OrderType::IOC;
        else if (type == "F") c.order.type = OrderType::FOK;
        else return std::nullopt;
        return c;
    }
    if (kind == "C") {
        auto id = detail::next_field(rest);
        if (!rest.empty()) return std::nullopt;
        c.action = Action::Cancel;
        if (!detail::parse_num(id, c.order.id)) return std::nullopt;
        return c;
    }
    if (kind == "M") {
        auto id = detail::next_field(rest), price = detail::next_field(rest),
             qty = detail::next_field(rest);
        if (!rest.empty()) return std::nullopt;
        c.action = Action::Modify;
        if (!detail::parse_num(id, c.order.id) || !detail::parse_num(price, c.order.price) ||
            !detail::parse_num(qty, c.order.quantity)) return std::nullopt;
        return c;
    }
    return std::nullopt;
}

struct ReplayStats {
    std::uint64_t lines = 0, applied = 0, malformed = 0, trades = 0;
};

inline ReplayStats replay(OrderBook& book, std::istream& in) {
    ReplayStats st;
    std::string line;
    while (std::getline(in, line)) {
        ++st.lines;
        if (line.empty() || line[0] == '#') continue;
        auto cmd = parse_line(line);
        if (!cmd) { ++st.malformed; continue; }
        book.apply(*cmd);
        ++st.applied;
        st.trades += book.last_trades().size();
    }
    return st;
}

} // namespace lob
