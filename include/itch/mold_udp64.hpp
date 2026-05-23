#pragma once
#include <cassert>
#include <cstdint>
#include <rte_byteorder.h>
#include <itch/utils.hpp>

namespace itch {
    template<typename F>
    void unwrap(const uint8_t* data, uint16_t datagram_len, F&& on_message) {
        static constexpr uint16_t kHeaderLen = 20;
        assert(datagram_len >= kHeaderLen && "Data message is too short");

        uint64_t seq_no = rte_be_to_cpu_64(read<uint64_t>(data + 10));
        uint16_t msg_count = rte_be_to_cpu_16(read<uint16_t>(data + 18));
        const uint8_t* cursor = data + 20;
        for (uint16_t j = 0; j < msg_count; ++j) {
            assert(cursor + 2 <= data + datagram_len && "Cursor exceeding datagram_len");
            uint16_t msg_len = rte_be_to_cpu_16(read<uint16_t>(cursor));
            assert(cursor + 2 + msg_len <= data + datagram_len && "Message exceeds datagram");
            on_message(cursor + 2, msg_len, seq_no + j);
            cursor += 2 + msg_len;
        }
    }
} // namespace itch