#pragma once
#include <cstdint>
#include <llob/types.hpp>

namespace llob
{

struct alignas(64) Order
{
    int64_t  price;     // fixed-point ticks
    int64_t  qty;
    uint32_t symbol_id;
    Side     side;
};

} // namespace llob
