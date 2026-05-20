# ADR-007: Benchmark Methodology — TSC Timing, Tail Latency, Environment

## Status

Accepted

## Context

### Why p99.9 and above are the meaningful metrics

Benchmark medians are easy to optimise and easy to misrepresent. In a feed
handler context, tail latency is a market risk metric: a p99.9 latency event at
10,000 messages/second means 10 events per second where the feed handler's view
of the book lags the exchange. Decisions made on stale state risk fills at
incorrect prices.

Median latency is a correctness indicator. p99.9 is the risk indicator. Both
must be reported.

### Why standard benchmark frameworks are insufficient for tail measurement

Google Benchmark uses `clock_gettime(CLOCK_MONOTONIC)` internally for its
iteration timer. This is a VDSO call on most Linux kernels — fast in the common
case. However:

- On cores configured with `nohz_full` (tickless), `clock_gettime` may stall
  waiting for timekeeping synchronisation from a non-isolated core. This was
  observed directly during `low-latency-order-book` benchmarking: enabling
  `isolcpus` + `nohz_full` caused GBench to hang or report wildly incorrect
  timings because the tickless core's clock was not being updated.
- GBench's statistical output (`mean`, `stddev`, `median`) aggregates across
  iterations but does not expose the raw per-iteration distribution. Percentiles
  above p95 are not directly accessible.
- GBench's iteration batching and result aggregation add overhead that is
  non-trivial relative to sub-10 ns measurements.

### TSC-based timing

The CPU timestamp counter (`rdtsc` / `rdtscp`) is the correct tool for sub-
microsecond latency measurement on x86:

- It reads a hardware counter directly — no system call, no VDSO, no kernel
  timekeeping involvement.
- On modern x86 CPUs with **invariant TSC** (`CPUID.80000007H:EDX[8]`), the TSC
  increments at a fixed frequency regardless of CPU power state (C-states,
  frequency scaling). This must be verified before trusting TSC measurements.
- `rdtscp` additionally serialises the instruction stream (reads `TSC` after all
  prior instructions complete) and returns the logical CPU ID — used to detect
  unexpected core migration during measurement.
- TSC counts are converted to nanoseconds using the TSC frequency:
  `ns = tsc_delta * 1e9 / tsc_hz`. TSC frequency is read from
  `/sys/devices/system/cpu/cpu0/tsc_freq_khz` or calibrated against
  `clock_gettime` at startup.

### Environment requirements

Learned from `low-latency-order-book` benchmarking:

| Requirement | Rationale |
|---|---|
| `performance` CPU governor | Prevents frequency scaling from inflating cold measurements |
| Threads pinned via `pthread_setaffinity_np` | Prevents core migration mid-measurement |
| HT sibling idle on benchmark cores | HT siblings share L1/L2 — a busy sibling evicts hot cache lines regardless of `alignas` |
| Migratable IRQs steered away | `echo 0-3 > /proc/irq/*/smp_affinity_list` before benchmarking |
| **No `isolcpus` or `nohz_full`** | Breaks `clock_gettime` VDSO on tickless cores — GBench hangs or reports incorrect timing. Use TSC instead, which is unaffected. |
| Warmup iterations before measurement | Cold-cache latency is a separate metric from steady-state latency |

### Benchmark scope

Different pipeline stages require different benchmark types:

| Benchmark | Measures | PMD |
|---|---|---|
| Parser throughput | Messages parsed per second from raw bytes | n/a (no PMD) |
| Pipeline latency | mbuf → MarketDataEvent → MPSC push, TSC-timestamped | pcap PMD |
| Book write latency | `order_book.add()` under seqlock contention | n/a (existing from llob) |
| End-to-end NIC latency | NIC arrival → book update, hardware timestamping | hardware PMD only |

Parser throughput and pipeline latency benchmarks run in CI against the pcap
PMD. End-to-end NIC latency requires hardware and is not part of CI.

## Decision

Use TSC (`rdtscp`) for all per-event latency measurements. Record the full
latency distribution (min, p50, p90, p99, p99.9, p99.99, p100 / max) over at
least 100,000 measurements to get stable tail estimates. Do not rely on GBench
percentile output for tail metrics — record raw TSC deltas into a fixed-size
array and compute percentiles post-hoc via `std::nth_element`.

Environment checklist (must be documented in every benchmark report):

```
[ ] CPU governor: performance
[ ] Benchmark threads pinned to CPUs with idle HT siblings
[ ] Migratable IRQs steered away from benchmark cores
[ ] isolcpus / nohz_full: NOT set (breaks GBench clock; TSC unaffected)
[ ] Invariant TSC confirmed: grep "constant_tsc" /proc/cpuinfo
[ ] Warmup: N_WARMUP iterations discarded before recording
[ ] PMD used: pcap (parser/pipeline) or hardware (NIC latency)
```

Benchmark reports state which pipeline stage is measured and which PMD is active.
"NIC-to-book latency" is only reported when a hardware PMD is in use.

## Consequences

### Positive

- TSC timing is immune to the `nohz_full` / `clock_gettime` stall observed in
  the order book benchmarks — the counter increments regardless of kernel
  timekeeping state.
- Full distribution reporting (p99.9, p99.99, max) gives a credible tail
  latency picture. Medians alone can hide pathological tail behaviour.
- The environment checklist makes benchmark conditions reproducible and
  comparable across runs and machines.
- Separating parser/pipeline benchmarks (pcap PMD) from NIC latency benchmarks
  (hardware) prevents misleading claims about end-to-end latency measured through
  kernel file I/O.

### Negative

- Invariant TSC must be verified (`constant_tsc` in `/proc/cpuinfo`). On VMs or
  older hardware without invariant TSC, TSC measurements are unreliable. The
  benchmark must assert this at startup.
- TSC frequency calibration adds startup overhead and a small measurement
  uncertainty (< 1 ppm on modern CPUs, negligible for ns-scale measurements).
- Recording 100,000+ raw TSC deltas requires a pre-allocated array of `uint64_t`
  — approximately 800 KB for 100,000 samples. This must be pre-allocated in
  keeping with ADR-002.
- `std::nth_element` for percentile computation is O(n) but not in-place-stable.
  It modifies the sample array — a copy is needed if the raw distribution must
  be preserved.
- End-to-end NIC latency benchmarks require hardware not available in development.
  This is an accepted gap — documented in ADR-003.
