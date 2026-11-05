#ifndef LOB_ENGINE_C_API_H
#define LOB_ENGINE_C_API_H
// Plain-C interface so Python (ctypes) can drive the C++ engine.
// Enum values: side 0=Buy 1=Sell | type 0=Limit 1=Market 2=IOC 3=FOK
// status: 0=Filled 1=Resting 2=PartialResting 3=PartialCancelled 4=Cancelled 5=Rejected
// All struct fields are 64-bit so the layout is identical in C++ and ctypes.
#include <stdint.h>

#if defined(_WIN32)
  #define LOB_API __declspec(dllexport)
#else
  #define LOB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LobResult { uint64_t status, filled, remaining; } LobResult;
typedef struct LobLevel  { int64_t price; uint64_t quantity, orders; } LobLevel;
typedef struct LobTrade  { uint64_t taker_id, maker_id; int64_t price; uint64_t quantity, sequence, taker_side; } LobTrade;
typedef struct LobStats  { uint64_t volume, trade_count, orders_processed; int64_t last_price; } LobStats;

LOB_API void*     lob_create(uint64_t expected_orders);
LOB_API void      lob_destroy(void* book);
LOB_API LobResult lob_submit(void* book, uint64_t id, int side, int type, int64_t price, uint64_t qty);
LOB_API int       lob_cancel(void* book, uint64_t id);               /* 1 = cancelled, 0 = not found */
LOB_API LobResult lob_modify(void* book, uint64_t id, int64_t price, uint64_t qty);
LOB_API int       lob_best_bid(void* book, int64_t* out_price);      /* 1 = present */
LOB_API int       lob_best_ask(void* book, int64_t* out_price);
LOB_API uint64_t  lob_depth(void* book, int side, uint64_t max_levels, LobLevel* out);
LOB_API uint64_t  lob_last_trades(void* book, uint64_t max_trades, LobTrade* out);
LOB_API void      lob_stats(void* book, LobStats* out);
LOB_API int       lob_feed_line(void* book, const char* line);      /* status code, or -1 if malformed */

#ifdef __cplusplus
}
#endif
#endif
