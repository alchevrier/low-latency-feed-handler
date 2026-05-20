# low-latency-feed-handler

C++23 low-latency feed handler — DPDK pcap PMD, NASDAQ ITCH 5.0 parser, zero-copy mbuf pipeline, MPSC market data aggregation into SOA order book. Zero heap allocation end-to-end.

## Architecture Decision Records

| ADR | Title | Status |
|-----|-------|--------|
| [ADR-001](docs/adr/ADR-001-why-dpdk.md) | Why DPDK — Kernel UDP Jitter via IRQs, sk_buff, RCU | Accepted |
| [ADR-002](docs/adr/ADR-002-zero-heap-allocation.md) | Zero Heap Allocation End-to-End | Accepted |
| [ADR-003](docs/adr/ADR-003-pcap-pmd-development.md) | pcap PMD for Development — PMD-Agnostic Application Design | Accepted |
| [ADR-004](docs/adr/ADR-004-itch-message-type-selection.md) | ITCH 5.0 Message Type Selection | Accepted |
| [ADR-005](docs/adr/ADR-005-rte-mbuf-zero-copy-parsing.md) | rte_mbuf Zero-Copy Parsing Boundary | Accepted |
| [ADR-006](docs/adr/ADR-006-mpsc-order-book-subproject.md) | MPSC Integration from low-latency-order-book Subproject | Accepted |
| [ADR-007](docs/adr/ADR-007-benchmark-methodology.md) | Benchmark Methodology — TSC Timing, Tail Latency, Environment | Accepted |
