#include <benchmark/benchmark.h>
#include <itch/mold_udp64.hpp>
#include <itch/itch_parser.hpp>
#include <llob/order_book.hpp>
#include <llob/pinned_thread.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>

// MoldUDP64 frame: Session[10] + SeqNo BE[8]=1 + MsgCount BE[2]=1
//                  + Length BE[2]=36 + ITCH Add Order[36]
// Mirrors the wire format after kL2L3L4Len=42 bytes are stripped by rx_parse_loop.
// See ADR-004 for zero-copy mbuf boundary.
static constexpr uint8_t kTestDatagram[] = {
    // MoldUDP64 header (20 bytes)
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,  // Session "ABCDEFGHIJ"
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,               // SeqNo = 1
    0x00, 0x01,                                                     // MsgCount = 1
    // Message block
    0x00, 0x24,                                                     // Length = 36
    // ITCH Add Order (36 bytes, offset 22 from datagram start)
    0x41,                                                           // Type 'A'
    0x00, 0x01,                                                     // StockLocate = 1
    0x00, 0x00,                                                     // TrackingNumber = 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                            // Timestamp
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,               // OrderRefNum = 1
    0x42,                                                           // Side 'B'
    0x00, 0x00, 0x00, 0x64,                                        // Shares = 100
    0x41, 0x41, 0x50, 0x4C, 0x20, 0x20, 0x20, 0x20,               // Stock "AAPL    "
    0x00, 0x00, 0x3A, 0x98,                                        // Price = 15000
};
static constexpr uint16_t kDatagramLen = sizeof(kTestDatagram);

static constexpr size_t kMaxSamples = 1'000'000;

// Calibrate TSC → ns via CLOCK_MONOTONIC (runs once at binary startup, ~100ms).
static double calibrate_tsc_ns_per_cycle() {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t c0 = __rdtsc();
    do { clock_gettime(CLOCK_MONOTONIC, &t1); }
    while ((t1.tv_sec  - t0.tv_sec)  * 1'000'000'000LL +
           (t1.tv_nsec - t0.tv_nsec) < 100'000'000LL);
    uint64_t c1 = __rdtsc();
    double elapsed_ns = static_cast<double>(t1.tv_sec  - t0.tv_sec)  * 1e9
                      + static_cast<double>(t1.tv_nsec - t0.tv_nsec);
    return elapsed_ns / static_cast<double>(c1 - c0);
}

static const double kNsPerCycle = calibrate_tsc_ns_per_cycle();

static std::vector<double> make_samples(const benchmark::State& state) {
    std::vector<double> v;
    v.reserve(std::min(static_cast<size_t>(state.max_iterations), kMaxSamples));
    return v;
}

// Report P50, P99.9, P99.99, P100 into GBench counters (nanoseconds, RDTSC-measured).
static void report_percentiles(benchmark::State& state, std::vector<double>& samples) {
    if (samples.empty()) return;
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();
    auto at = [&](double p) -> double {
        return samples[std::min(static_cast<size_t>(p * static_cast<double>(n)), n - 1)];
    };
    state.counters["p50_ns"]    = at(0.500);
    state.counters["p99.9_ns"]  = at(0.999);
    state.counters["p99.99_ns"] = at(0.9999);
    state.counters["p100_ns"]   = samples.back();
}

// Raw ITCH Add Order bytes starting at type byte — mirrors what unwrap passes to on_message.
// Used in BM_ParseDirect to isolate parse_add_order from MoldUDP64 frame overhead.
static constexpr uint8_t kItchAddOrder[] = {
    0x41,                                                           // Type 'A'
    0x00, 0x01,                                                     // StockLocate = 1
    0x00, 0x00,                                                     // TrackingNumber = 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                            // Timestamp
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,               // OrderRefNum = 1
    0x42,                                                           // Side 'B'
    0x00, 0x00, 0x00, 0x64,                                        // Shares = 100
    0x41, 0x41, 0x50, 0x4C, 0x20, 0x20, 0x20, 0x20,               // Stock "AAPL    "
    0x00, 0x00, 0x3A, 0x98,                                        // Price = 15000
};

// Full frame hot path: MoldUDP64 unwrap + ITCH parse_add_order.
// This is the compute floor — subtract BM_ParseDirect to isolate unwrap overhead.
static void BM_UnwrapParse(benchmark::State& state) {
    auto samples = make_samples(state);
    for (auto _ : state) {
        llob::MarketDataEvent event{};
        uint64_t t0 = __rdtsc();
        itch::unwrap(kTestDatagram, kDatagramLen,
            [&event](const uint8_t* msg, uint16_t msg_len, uint64_t) noexcept {
                if (msg[0] == 'A')
                    event = itch::parse_add_order(msg, msg_len);
            });
        uint64_t t1 = __rdtsc();
        benchmark::DoNotOptimize(event);
        if (samples.size() < kMaxSamples)
            samples.push_back(static_cast<double>(t1 - t0) * kNsPerCycle);
    }
    report_percentiles(state, samples);
}
BENCHMARK(BM_UnwrapParse);

// Raw parser throughput with MoldUDP64 unwrap overhead removed.
// Difference between BM_UnwrapParse and BM_ParseDirect = unwrap cost per datagram.
static void BM_ParseDirect(benchmark::State& state) {
    auto samples = make_samples(state);
    for (auto _ : state) {
        uint64_t t0 = __rdtsc();
        auto event = itch::parse_add_order(kItchAddOrder, sizeof(kItchAddOrder));
        uint64_t t1 = __rdtsc();
        benchmark::DoNotOptimize(event);
        if (samples.size() < kMaxSamples)
            samples.push_back(static_cast<double>(t1 - t0) * kNsPerCycle);
    }
    report_percentiles(state, samples);
}
BENCHMARK(BM_ParseDirect);

// Full hot path: MoldUDP64 unwrap + ITCH parse_add_order + order_book.add.
// OrderBook<50> is shared across iterations — after 50 bids at the same price it saturates,
// making each subsequent add a lower_bound probe + memmove(0) + write: a realistic insert cost.
static void BM_Pipeline(benchmark::State& state) {
    auto samples = make_samples(state);
    llob::OrderBook<50> book;
    for (auto _ : state) {
        uint64_t t0 = __rdtsc();
        itch::unwrap(kTestDatagram, kDatagramLen,
            [&book](const uint8_t* msg, uint16_t msg_len, uint64_t) noexcept {
                if (msg[0] == 'A')
                    book.add(itch::parse_add_order(msg, msg_len));
            });
        uint64_t t1 = __rdtsc();
        benchmark::DoNotOptimize(book);
        if (samples.size() < kMaxSamples)
            samples.push_back(static_cast<double>(t1 - t0) * kNsPerCycle);
    }
    report_percentiles(state, samples);
}
BENCHMARK(BM_Pipeline);

// Move all moveable IRQs off `cpu` so the benchmark thread is not interrupted.
// Requires root — if permission is denied, prints a warning and continues anyway.
static void move_irqs_off_cpu(int cpu) {
    const int total = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    std::string affinity;
    if (cpu > 0)
        affinity += "0-" + std::to_string(cpu - 1);
    if (cpu < total - 1) {
        if (!affinity.empty()) affinity += ',';
        affinity += std::to_string(cpu + 1) + '-' + std::to_string(total - 1);
    }
    if (affinity.empty()) return;

    int moved = 0, failed = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/irq")) {
        std::ofstream f{entry.path() / "smp_affinity_list"};
        if (f) { f << affinity; ++moved; }
        else    ++failed;
    }
    if (failed > 0)
        fprintf(stderr, "[bench] IRQ affinity: %d moved, %d failed (run with sudo for full isolation)\n",
                moved, failed);
    else
        fprintf(stderr, "[bench] IRQ affinity: %d IRQs moved off CPU %d\n", moved, cpu);
}

int main(int argc, char** argv) {
    constexpr int kBenchCpu = 4;
    move_irqs_off_cpu(kBenchCpu);

    int ret = 0;
    {
        llob::PinnedThread runner{kBenchCpu, [&]() {
            ::benchmark::Initialize(&argc, argv);
            if (::benchmark::ReportUnrecognizedArguments(argc, argv)) { ret = 1; return; }
            ::benchmark::RunSpecifiedBenchmarks();
            ::benchmark::Shutdown();
        }};
    }  // PinnedThread destructor joins — blocks until benchmarks complete
    return ret;
}
