// Minimal hot-path driver for Cachegrind and Massif.
// No RDTSC, no GBench — avoids RDTSC emulation issues under Valgrind.
//
// Cachegrind:
//   valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./build/benchmarks/cachegrind_driver
//   cg_annotate cachegrind.out.<pid> --auto=yes | less
//
// Massif (validate ADR-001 zero heap allocation in hot path):
//   valgrind --tool=massif ./build/benchmarks/cachegrind_driver
//   ms_print massif.out.<pid> | less

#include <itch/mold_udp64.hpp>
#include <itch/itch_parser.hpp>
#include <llob/order_book.hpp>

static constexpr uint8_t kTestDatagram[] = {
    // MoldUDP64 header (20 bytes)
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01,
    // Message block: Length=36 + ITCH Add Order
    0x00, 0x24,
    0x41,                                                // Type 'A'
    0x00, 0x01,                                          // StockLocate = 1
    0x00, 0x00,                                          // TrackingNumber = 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                 // Timestamp
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,    // OrderRefNum = 1
    0x42,                                                // Side 'B'
    0x00, 0x00, 0x00, 0x64,                             // Shares = 100
    0x41, 0x41, 0x50, 0x4C, 0x20, 0x20, 0x20, 0x20,    // Stock "AAPL    "
    0x00, 0x00, 0x3A, 0x98,                             // Price = 15000
};
static constexpr uint16_t kDatagramLen = sizeof(kTestDatagram);
static constexpr int kIterations = 100'000;

int main() {
    llob::OrderBook<50> book;

    for (int i = 0; i < kIterations; ++i) {
        itch::unwrap(kTestDatagram, kDatagramLen,
            [&book](const uint8_t* msg, uint16_t msg_len, uint64_t) noexcept {
                if (msg[0] == 'A')
                    book.add(itch::parse_add_order(msg, msg_len));
            });
    }

    // Prevent dead-store elimination of book without using volatile or GBench.
    __asm__ volatile("" :: "m"(book));
    return 0;
}
