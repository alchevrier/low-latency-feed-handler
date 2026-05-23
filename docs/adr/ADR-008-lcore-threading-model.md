# ADR-008: EAL Lcore Threading Model

## Status

Accepted

## Context

The feed handler pipeline requires at minimum two concurrent execution contexts:

- An **RX/parse lcore** that polls the NIC (or pcap PMD), unwraps MoldUDP64
  frames, parses ITCH messages, and pushes `MarketDataEvent` values into the
  MPSC queue.
- A **book writer lcore** that polls the MPSC queue and writes events into the
  SOA order book.

Two threading primitives are available in the codebase:

- `rte_eal_remote_launch()` — DPDK EAL lcore management. `rte_eal_init()`
  pins each lcore to its assigned CPU via `pthread_setaffinity_np` internally.
  Lcore identity is available via `rte_lcore_id()`.
- `PinnedThread` — the order book subproject's thin wrapper around
  `pthread_setaffinity_np`. Used by the order book for its book writer and
  matching threads.

### Inline parse vs raw mbuf handoff

An alternative design passes raw `rte_mbuf*` pointers from the RX lcore to a
separate parse lcore via a pointer queue. The parse lcore then unwraps and
parses before pushing to the MPSC.

This adds a queue, an extra lcore, and a pointer indirection on the hot path.
No other component in the current design consumes raw mbufs — the ITCH parser
is the only consumer of packet data. The handoff buys nothing and costs a
cache line transfer per mbuf.

Decision: unwrap MoldUDP64, parse ITCH, push `MarketDataEvent`, and free the
mbuf all on the RX lcore. The mbuf lifecycle is contained to a single lcore.

### Threading primitive: EAL vs PinnedThread

Using both primitives in the same application creates two sources of truth for
CPU pinning. `rte_eal_init()` already calls `pthread_setaffinity_np` for every
lcore in the coremask — `PinnedThread` would duplicate this for any thread it
manages, with no additional guarantee.

A mixed model (EAL for RX, PinnedThread for book writer) is confusing to
reason about: which primitive owns which core, and what happens if the
assignments conflict?

Decision: use `rte_eal_remote_launch()` for all feed handler threads.
`PinnedThread` is available via the `llob_dep` subproject but is not used
directly by the feed handler.

### Core assignment

The development machine is a single-socket AMD Ryzen (single NUMA node —
`libnuma` not required). Cores 0 and 1 carry unmoveable IRQ affinity and are
excluded from the coremask.

Coremask: `-l 2,4,6`

| Lcore | CPU | Role |
|-------|-----|------|
| 0 (EAL main) | 2 | EAL init, port setup, lcore launch |
| 1 | 4 | RX/parse — `rte_eth_rx_burst`, MoldUDP64 unwrap, ITCH parse, MPSC push, mbuf free |
| 2 | 6 | Book writer — MPSC poll, SOA order book write |

CPU 6 is reserved for a future matcher lcore. The coremask extends to `-l 2,4,6,8`
when the matcher is added.

Note: verify that CPU 3 and CPU 5 (HT siblings of CPU 2 and CPU 4) are idle
before benchmarking. HT sibling activity causes intra-core cache eviction that
`isolcpus` and `alignas(64)` do not protect against.

## Decision

- All feed handler threads are launched via `rte_eal_remote_launch()`.
- The RX lcore performs inline parsing: MoldUDP64 unwrap → ITCH parse →
  MPSC push → `rte_pktmbuf_free()`. No raw mbuf handoff.
- Coremask: `-l 2,4,6`. Cores 0 and 1 are excluded (IRQ affinity).
- `PinnedThread` is not used by the feed handler directly.

## Consequences

- Single threading primitive across the feed handler — no ambiguity about
  which component owns which core.
- mbuf lifecycle is fully contained to the RX lcore — no cross-lcore pointer
  passing for packet data.
- EAL main lcore (CPU 2) is idle after port setup and lcore launch. It blocks
  on `rte_eal_wait_lcore()`.
- Extending to a matcher lcore requires only adding a new `rte_eal_remote_launch`
  call and widening the coremask to include CPU 8.
- Full commitment to EAL means DPDK API changes directly affect the threading
  model. Breaking changes to `rte_eal_remote_launch`, lcore lifecycle, or
  affinity semantics require coordinated updates across the pipeline. Silent
  behavioral changes (e.g. affinity policy shifts between LTS releases) may not
  surface until benchmarks regress. Pin the DPDK version in the build
  environment and treat upgrades as requiring a full benchmark re-run.
