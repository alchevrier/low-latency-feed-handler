#pragma once
#include <cassert>
#include <cstdint>
#include <rte_byteorder.h>
#include <llob/market_data_event.hpp>
#include <itch/utils.hpp>

namespace itch {

llob::MarketDataEvent parse_add_order(const uint8_t* cursor, const uint16_t msg_len) {
    static constexpr uint16_t kAddOrderLen = 36;
    assert(msg_len >= kAddOrderLen && "Add Order message too short");

    const uint16_t stock_locate = rte_be_to_cpu_16(read<uint16_t>(cursor + 1));
    const uint64_t order_id = rte_be_to_cpu_64(read<uint64_t>(cursor + 11));
    const int64_t qty = static_cast<int64_t>(rte_be_to_cpu_32(read<uint32_t>(cursor + 20)));
    const int64_t price = static_cast<int64_t>(rte_be_to_cpu_32(read<uint32_t>(cursor + 32)));
    const llob::Side side = read<uint8_t>(cursor + 19) == 'B' ? llob::Side::Bid : llob::Side::Ask;

    return llob::MarketDataEvent{price, qty, order_id, stock_locate, side, llob::EventType::Add};
}

} // namespace itch

