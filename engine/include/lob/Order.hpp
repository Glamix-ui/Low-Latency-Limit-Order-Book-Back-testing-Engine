#pragma once
#include <cstdint>

namespace lob {

// Prices are integer "ticks" (1 tick = $0.01). Never use double as a map key
// in a matching engine: 0.1 + 0.2 != 0.3. The Python layer converts to/from ticks.
using OrderId  = std::uint64_t;
using Price    = std::int64_t;
using Quantity = std::uint64_t;
using Sequence = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

enum class OrderType : std::uint8_t {
    Limit  = 0,  // match what crosses, rest the remainder on the book
    Market = 1,  // match at any price, never rests
    IOC    = 2,  // Immediate-or-Cancel: match what crosses at the limit, drop the rest
    FOK    = 3   // Fill-or-Kill: fill entirely at the limit, or do nothing at all
};

enum class OrderStatus : std::uint8_t {
    Filled                   = 0, // fully executed
    Resting                  = 1, // nothing executed, now on the book
    PartiallyFilledResting   = 2, // partly executed, remainder on the book
    PartiallyFilledCancelled = 3, // partly executed, remainder dropped (IOC/Market)
    Cancelled                = 4, // nothing executed and dropped, or a successful CANCEL
    Rejected                 = 5  // invalid (zero qty, bad price, duplicate id, unknown id)
};

// Things the exchange can be asked to do. CANCEL and MODIFY act on an existing
// order, so they are actions rather than order types.
enum class Action : std::uint8_t { New = 0, Cancel = 1, Modify = 2 };

struct Order {
    OrderId   id        = 0;
    Side      side      = Side::Buy;
    OrderType type      = OrderType::Limit;
    Price     price     = 0;   // ignored for Market
    Quantity  quantity  = 0;   // original size
    Quantity  remaining = 0;   // unfilled size (set by the book)
    Sequence  sequence  = 0;   // arrival order, assigned by the book (time priority)
};

struct Trade {
    OrderId  taker_id   = 0;
    OrderId  maker_id   = 0;
    Side     taker_side = Side::Buy;
    Price    price      = 0;   // always the resting (maker) order's price
    Quantity quantity   = 0;
    Sequence sequence   = 0;   // global trade counter
};

// One instruction for the book (from the UI, a strategy, or a market-data feed).
//   New:    uses the whole order
//   Cancel: uses order.id
//   Modify: uses order.id, order.price, order.quantity
struct Command {
    Action action = Action::New;
    Order  order;
};

} // namespace lob
