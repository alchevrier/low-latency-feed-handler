#pragma once
#include <cstdint>
#include <llob/types.hpp>

namespace llob
{

struct alignas(64) MarketDataEvent
{
    int64_t  price;      // fixed-point ticks
    int64_t  qty;
    uint64_t order_id;
    uint16_t stock_locate; // NASDAQ-assigned per-session instrument identifier (from Stock Locate field).
                           // No Stock Directory (R) lookup needed on the hot path — locate code is the key.
    Side      side;
    EventType event_type;
};

} // namespace llob
