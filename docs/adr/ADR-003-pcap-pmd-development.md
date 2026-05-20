# ADR-003: pcap PMD for Development — PMD-Agnostic Application Design

## Status

Accepted

## Context

Production deployment of a DPDK application requires a DPDK-compatible NIC
(Intel X710, Mellanox ConnectX, or equivalent). The development machine is a
consumer desktop (Intel i5-12400) with no such hardware. Writing application
code that is tightly coupled to a specific PMD would block development without
hardware and make the codebase harder to test reproducibly.

DPDK solves this with the **pcap Poll Mode Driver** (`net_pcap`): a software PMD
that reads packet data from a `.pcap` file and presents it to the application
through the standard `rte_eth_rx_burst()` API — identical to a hardware PMD.

NASDAQ publishes historical ITCH 5.0 data as compressed pcap files
(https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/), providing a real, deterministic
input corpus for development and benchmarking.

### PMD-agnostic design

The key insight is that `rte_eth_rx_burst()` is the only point of contact between
the application and the PMD. The application never calls PMD-specific APIs. The
PMD is selected entirely through EAL initialization arguments:

```bash
# Development — pcap PMD
./feed-handler --vdev net_pcap0,rx_pcap=itch.pcap -l 2,4,6

# Production — hardware PMD (Intel X710 bound to vfio-pci)
./feed-handler -l 2,4,6 -a 0000:01:00.0
```

The binary is identical. Only the EAL arguments differ.

### Limitations of the pcap PMD

The pcap PMD is not representative of production latency. It reads from a file
via the OS kernel's file I/O path — it is not a kernel-bypass path. Packet data
is copied from a file buffer into the mbuf; a hardware PMD DMAs directly into
the pre-allocated pool, bypassing the CPU on the copy. The pcap PMD also ignores
original inter-packet timestamps — packets are delivered as fast as
`rte_eth_rx_burst()` is called, with no burst or gap characteristics from the
original capture.

pcap PMD latency figures are an **indicative lower bound** for parser processing
time. They characterise the order of magnitude of p99.9 and p99.99 under
constant load, but live NIC-to-book figures will differ due to DMA transfer
cost, real inter-packet timing, and hardware interrupt elimination overhead.
Treat pcap latency results as a correctness proxy and a floor estimate, not a
production characterisation.

This distinction must be documented and enforced: pcap PMD benchmarks are
labelled as **parser/pipeline benchmarks**, not **NIC latency benchmarks**.

### Allocation validation

The pcap PMD is the primary environment for validating the zero-allocation
constraint (ADR-002). Valgrind massif run against the pcap PMD pipeline confirms
that no heap allocation occurs on the hot path between `rte_eth_rx_burst()` and
`order_book.add()`. This is a key use case of the pcap PMD — not a limitation.
A clean massif result in the pcap environment is a necessary (though not
sufficient) condition before any hardware PMD deployment.

## Decision

Use the DPDK pcap PMD (`--vdev net_pcap0,rx_pcap=itch.pcap`) for all
development and CI. Application code must make zero assumptions about which PMD
is active — the only PMD-facing API call is `rte_eth_rx_burst()` and the mbuf
lifecycle (see ADR-005).

Production deployment with a hardware PMD requires no code changes — only EAL
arguments and NIC binding (`dpdk-devbind --bind vfio-pci <PCI addr>`).

Benchmark reports must state which PMD was used and what the benchmark
measures (parser throughput vs NIC latency).

## Consequences

### Positive

- Development proceeds without DPDK-compatible hardware.
- Historical NASDAQ pcap files provide a deterministic, real-world input corpus
  — tests are reproducible across machines and time.
- CI can run on any Linux machine with DPDK installed using the pcap PMD and
  `--vdev`.
- PMD-agnostic application design is a correctness property: it enforces that no
  PMD-specific API leaks into the application layer.
- Valgrind massif runs cleanly against the pcap PMD pipeline, providing the
  primary mechanism for validating the zero-allocation constraint (ADR-002)
  before hardware deployment.

### Negative

- pcap PMD throughput is limited by kernel file I/O — it cannot reproduce the
  burst characteristics of a live NIC at line rate.
- End-to-end NIC-to-book latency cannot be measured in development. A hardware
  benchmark environment is required for production validation.
- pcap files must be obtained separately (NASDAQ historical data download) and
  are large (~tens of GB compressed). They are not committed to the repository.
- The `--vdev` argument syntax differs between pcap and hardware PMDs, which
  adds a configuration step when moving to production.
