#include <itch/itch_parser.hpp>
#include <llob/order_book.hpp>
#include <llob/pinned_thread.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <rte_byteorder.h>

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

// Report P50, P99.9, P99.99, P100 into stdout (nanoseconds, RDTSC-measured).
static void report_percentiles(std::vector<double>& samples) {
    if (samples.empty()) return;
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();
    auto at = [&](double p) -> double {
        return samples[std::min(static_cast<size_t>(p * static_cast<double>(n)), n - 1)];
    };

    fprintf(stdout, "[bench] P50 reported at %.1f ns\n", at(0.500));
    fprintf(stdout, "[bench] P99.9 reported at %.1f ns\n", at(0.999));
    fprintf(stdout, "[bench] P99.99 reported at %.1f ns\n", at(0.9999));
    fprintf(stdout, "[bench] P100 reported at %.1f ns\n", samples.back());
}

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
            constexpr uint16_t kMaxLocate = 8714;
            size_t n_symbols = argc >= 3 ? (size_t)atoi(argv[2]) : kMaxLocate;
            static std::array<llob::OrderBook<50>, kMaxLocate> books{};

            constexpr size_t kMaxSamples = 5'000'000;

            if (argc < 2) {
                fprintf(stderr, "[bench] Missing filename as argument\n");
                return;
            } 

            int fd = open(argv[1], O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "[bench] Open file failed: %s filename could not be open\n", argv[1]);
                return;
            }

            struct stat st;
            if (fstat(fd, &st) != 0) {
                fprintf(stderr, "[bench] Failed to obtain file information for filename: %s\n", argv[1]);
                return;
            }

            size_t file_size = static_cast<size_t>(st.st_size);
            void* ptr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
            if (ptr == MAP_FAILED) {
                close(fd);
                fprintf(stderr, "[bench] Failed to memory map file for filename: %s\n", argv[1]);
                return;
            }

            const uint8_t* data = static_cast<const uint8_t*>(ptr);

            std::vector<double> samples;
            samples.reserve(kMaxSamples);

            size_t pos = 0;

            bool active[kMaxLocate] = {};
            size_t found = 0;

            while (pos + 2 <= file_size) {
                const uint16_t len = ntohs(*(uint16_t*)(data + pos));
                pos += 2;
                const uint8_t* msg = data + pos;
                pos += len;

                if (msg[0] == 'A') {
                    uint16_t locate = ntohs(*(uint16_t*)(msg + 1));
                    if (!active[locate]) {
                        active[locate] = true;
                        if (++found == n_symbols) break;
                    }
                }
            }

            pos = 0;
            size_t add_events = 0;

            while (pos + 2 <= file_size) {
                if (add_events == kMaxSamples) break;

                const uint16_t len = ntohs(*(uint16_t*)(data + pos));
                pos += 2;
                const uint8_t* msg = data + pos;
                pos += len;

                if (msg[0] == 'A') {
                    uint16_t locate = ntohs(*(uint16_t*)(msg + 1));
                    _mm_prefetch(&books[locate], _MM_HINT_T0); 
                    if (!active[locate]) continue;
                    uint64_t t0 = __rdtsc();
                    books[locate].add(itch::parse_add_order(msg, len));
                    uint64_t t1 = __rdtsc();
                    samples.push_back(static_cast<double>((t1 - t0) * kNsPerCycle));
                    add_events++;
                }
            }

            fprintf(stdout, "[bench] Seen %zu of ADD events\n", add_events);
            fprintf(stdout, "[bench] Collected %zu samples\n", samples.size());
            fprintf(stdout, "[bench] Symbol subset: %zu of %d total\n", n_symbols, kMaxLocate);
            report_percentiles(samples);
            munmap(ptr, file_size); 
            close(fd);
        }};
    }  // PinnedThread destructor joins — blocks until benchmarks complete
    return ret;
}
