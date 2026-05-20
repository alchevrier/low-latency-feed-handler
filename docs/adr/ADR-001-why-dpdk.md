# ADR-001: Why DPDK — Kernel UDP Jitter, Ordering, and Compliance

## Status

Accepted

## Context

The feed handler receives NASDAQ ITCH 5.0 market data over UDP. The standard
approaches are a `recvmsg` loop on a UDP socket, or the modern equivalent —
io_uring (Linux 5.1+), which eliminates per-syscall overhead by batching
completions through a shared ring buffer. Both remain inside the kernel
networking stack and introduce jitter, packet loss, and ordering violations on
the hot path that cannot be eliminated without leaving it.

### Why p99.9 is the relevant metric

Tail latency is not a performance nicety — it is a risk exposure metric with
direct revenue consequences. A delayed or dropped market data event means the
feed handler's view of the order book lags the exchange. Decisions made on stale
data risk fills at incorrect prices. p99.9 represents the 1-in-1000 event; at
10,000 messages/second that is 10 events per second executing on stale state.

Beyond fill risk, a stale fill is not just a loss — it is a compliance event.
A fill executed on a price that no longer reflects current market state can be
challenged and voided by the exchange. Repeated occurrences trigger regulatory
scrutiny under best execution obligations (MiFID II in Europe, SEC Rule 611 in
the US). A voided trade carries operational cost, regulatory exposure, and
reputational damage with counterparties and the exchange. At p99.9 frequency that
is not an edge case — it is a structural drag on profitability and standing.

A feed handler that cannot process events in sequence and on time cannot provide
liquidity reliably. Market makers depend on accurate, timely book state to quote.
A stale or corrupted book forces a choice between quoting on wrong prices (fill
risk, compliance exposure) or widening spreads and pulling quotes (lost revenue).

### 1. IRQ delivery, softirq deferral, and ksoftirqd reordering

When a packet arrives, the NIC raises a hardware interrupt. The kernel's IRQ
balancer (`irqbalance`) decides which logical CPU services it. Even with
`/proc/irq/*/smp_affinity_list` locked to a specific core, interrupt processing
runs in hardirq context, preempting whatever thread is executing on that core.
The interrupt latency itself is bounded but not constant: it varies with CPU
frequency state (C-state residency), other pending interrupts, and BIOS SMI
activity.

The hardirq handler does minimal work and defers actual packet processing to a
softirq (`NET_RX_SOFTIRQ`), which runs after the hardirq completes. Softirqs
themselves are preemptible by hardirqs — a new NIC interrupt can fire mid-softirq
and interrupt the packet processing already in progress, adding another layer of
non-determinism. Under high packet rates the softirq budget (`netdev_budget`,
default 300 packets per poll) is exhausted and the kernel hands off to
`ksoftirqd` — a per-CPU kernel thread that is **schedulable and can sleep**.
Unlike a softirq which runs to completion in interrupt context, `ksoftirqd` is a
normal kernel thread: the scheduler can preempt it, block it on a lock, or
migrate it. It runs at `SCHED_NORMAL` with a default nice value of 0 — the same
priority as ordinary user processes — deliberately kept low to avoid starving
other devices and workloads sharing the same CPU. Under any competing load,
`ksoftirqd` yields. Packet processing can be suspended mid-batch and resumed
later, interleaved with other kernel work. Once `ksoftirqd` is involved, ordering
between packets in the same batch is no longer guaranteed relative to other
softirq processing on the same core.

For ITCH 5.0 this is a correctness and compliance issue, not just a latency
issue. ITCH messages carry a monotonically increasing sequence number — the
exchange guarantees ordering and the consumer is required to preserve it.
FINRA Rule 4370 and SEC Rule 17a-4 mandate that market data processing preserves
the sequence of events as received. If `ksoftirqd` reorders delivery under load,
a Delete (`D`) can arrive at the application before its corresponding Add (`A`)
has been processed — the book attempts to cancel an order that does not yet
exist. This is not just a corrupted book state: it means the system acted on a
sequence of events that never legally occurred. DPDK's polling loop processes
packets strictly in descriptor ring order, preserving the exchange-guaranteed
sequence and eliminating this reordering hazard entirely.

There is no clean mitigation for hardirq preemption within the kernel stack. The
IRQ-steering dilemma makes this unavoidable: steering NIC interrupts *to* your
core (via `smp_affinity_list`) means the hardirq preempts your thread directly;
steering them *away* incurs an IPI (inter-processor interrupt) to wake another
core, adding cross-core latency. Either way the interrupt cost is paid. DPDK
eliminates the interrupt entirely by switching the NIC to polling mode.

### 2. sk_buff allocation on every packet

The kernel allocates an `sk_buff` for every arriving packet on the hot path.
Since this happens in softirq context (where sleeping is forbidden), it uses
`kmalloc(size, GFP_ATOMIC)`. Unlike `GFP_KERNEL` which can sleep waiting for
memory reclaim, `GFP_ATOMIC` must succeed immediately or fail — there is no
retry. Under memory pressure it fails silently and the packet is dropped with no
notification to the application. For a feed handler this is worse than latency
jitter: a dropped ITCH message means a permanently stale order book with no
indication that state has diverged from the exchange.

Even when it succeeds, `GFP_ATOMIC` goes through the slab allocator, which:

- may trigger cache line eviction on the allocator's free-list
- is not lock-free under contention
- introduces non-deterministic latency that accumulates at p99+

There is no kernel-side mechanism to eliminate this allocation while remaining
in the kernel networking stack.

### 3. RCU grace periods accumulate jitter

The kernel uses RCU (Read-Copy-Update) extensively in the networking path
(routing tables, neighbour cache, socket lookup). Even read-side RCU critical
sections can be delayed when grace period callbacks accumulate on the local CPU,
particularly under interrupt load. This shows up as periodic latency spikes at
p99.9 and above.

### Existing evidence from the order book

The `low-latency-order-book` benchmark (kernel-side only, no NIC) already shows
that timer interrupts and RCU callbacks produce a p99.9 of 7.94 ns vs a 2.57 ns
median on a pinned, IRQ-steered core — a 3× tail-to-median ratio with the kernel
doing nothing except background housekeeping. Adding NIC interrupt delivery and
`sk_buff` allocation makes the tail worse.

### Considered alternative: io_uring

io_uring (Linux 5.1+) is the modern Linux async I/O interface and is widely
cited as the replacement for `recvmsg`-based loops. It eliminates the per-syscall
overhead by batching submission and completion events through shared ring buffers
between userspace and kernel — no syscall per packet in steady state.

However, io_uring does not address the jitter sources above, nor the
correctness and compliance issues introduced by `ksoftirqd`:

| Source | recvmsg loop | io_uring | DPDK PMD |
|---|---|---|---|
| IRQ delivery | hardirq on every packet | hardirq on every packet | no interrupt — polling loop |
| `sk_buff` allocation | per packet, `GFP_ATOMIC` | per packet, `GFP_ATOMIC` | none — `rte_mbuf` from mempool |
| RCU | routing, socket lookup, neighbour cache | routing, socket lookup, neighbour cache | not present in userspace PMD path |
| `ksoftirqd` reordering | under load, ITCH sequence broken | under load, ITCH sequence broken | descriptor ring order preserved |
| Compliance | ordering not guaranteed under load | ordering not guaranteed under load | exchange-guaranteed sequence preserved |

io_uring reduces **syscall cost** — it does not exit the kernel networking stack.
The NIC still raises interrupts, the kernel still allocates `sk_buff` per packet
in the driver and socket layers, and RCU is still present in the routing and
socket lookup path. `IORING_SETUP_SQPOLL` adds a kernel-side submission polling
thread but the RX path remains interrupt-driven.

io_uring is the right choice when the bottleneck is syscall overhead (file I/O,
high-connection-count servers). It is not the right choice when the bottleneck is
the kernel networking stack itself. DPDK exits the stack entirely.

## Decision

Use DPDK to bypass the kernel networking stack entirely on the RX path.

DPDK's Poll Mode Driver (PMD) operates in userspace busy-poll: the application
thread calls `rte_eth_rx_burst()` in a tight loop, which reads packets directly
from the NIC's RX descriptor ring without raising any interrupt. Packet buffers
(`rte_mbuf`) come from a pre-allocated hugepage mempool (`rte_mempool`) —
equivalent to the SOA pre-sizing principle already established in
`low-latency-order-book`. There are no `sk_buff` allocations on the hot path, no RCU read-side latency
in the NIC→application data path, and no `ksoftirqd` involvement — packets are
processed strictly in descriptor ring order, preserving the exchange-guaranteed
ITCH sequence number end-to-end.

All jitter sources are eliminated, and ordering and compliance are
guaranteed by construction:

| Source | Kernel UDP / io_uring | DPDK PMD |
|---|---|---|
| IRQ delivery | hardirq preempts any core | no interrupt — polling loop |
| Packet buffer | `sk_buff` / `GFP_ATOMIC` per packet | `rte_mbuf` from pre-allocated mempool |
| RCU | grace period callbacks on RX core | not present in userspace PMD path |
| `ksoftirqd` reordering | ITCH sequence broken under load | descriptor ring order preserved |
| Compliance | ordering not guaranteed under load | exchange-guaranteed sequence preserved |

## Consequences

### Positive

- RX path latency is bounded by the poll loop period, not by interrupt delivery
  or allocator jitter.
- `rte_mempool` (hugepage-backed, pre-allocated at startup) gives the same
  zero-allocation guarantee on the NIC boundary as SOA pre-sizing gives on the
  in-process side — the end-to-end pipeline is allocation-free in steady state.
- PMD-agnostic application code: the same binary runs against the pcap PMD in
  development and a hardware PMD (Intel X710, Mellanox ConnectX) in production
  — only the EAL `--vdev` argument changes. See ADR-003.
- Busy-polling on a dedicated, pinned core eliminates scheduler migration and
  preemption on the RX thread.
- **ITCH sequence ordering preserved**: packets are processed strictly in
  descriptor ring order — no `ksoftirqd` reordering under load. The
  exchange-guaranteed monotonically increasing sequence number is honoured
  end-to-end, which is a prerequisite for a correct order book.
- **Compliance**: DPDK's polling loop satisfies the FINRA Rule 4370 and
  SEC Rule 17a-4 ordering requirements (established in section 1) by
  construction. The kernel networking stack does not under load — making DPDK
  not just the faster choice but the only compliant one for a production feed
  handler.

### Negative

- Production requires a DPDK-compatible NIC. Consumer NICs (Realtek, Intel
  i225) are not supported by high-performance PMDs.
- Hugepages must be reserved at system boot (`hugepages=N` kernel parameter).
  This is a fixed OS-level configuration requirement.
- The RX thread is spinning at 100% CPU permanently — the core is fully
  dedicated and unavailable for other work.
- DPDK initialization (`rte_eal_init`) adds startup latency and requires root
  or `CAP_NET_ADMIN` privileges (or `vfio-pci` configuration for non-root).
- Debugging is harder: standard `tcpdump` and `wireshark` cannot see traffic
  once DPDK has bound the NIC. Use the pcap PMD in development for
  reproducibility (ADR-003).
