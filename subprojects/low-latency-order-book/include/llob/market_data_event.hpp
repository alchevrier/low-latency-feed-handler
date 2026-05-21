#pragma once
#include <cstdint>
#include <llob/types.hpp>

namespace llob
{

struct alignas(64) MarketDataEvent
{
    int64_t  price;      // fixed-point ticks
    int64_t  qty;
    uint32_t order_id;
    uint32_t symbol_id;
    Side      side;
    EventType event_type;
};

} // namespace llob
