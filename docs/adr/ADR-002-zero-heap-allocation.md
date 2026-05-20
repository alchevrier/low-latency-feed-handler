# ADR-002: Zero Heap Allocation End-to-End

## Status

Accepted

## Context

ADR-001 establishes that the kernel networking stack introduces jitter through
`sk_buff` heap allocation on every received packet. DPDK eliminates that specific
allocation. But the zero-allocation requirement does not stop at the NIC boundary
— it must hold end-to-end through the parser, the MPSC queue, and into the SOA
order book.

### The existing in-process baseline

The `low-latency-order-book` project already validates this constraint in-process.
Valgrind massif measurement:

```
Peak heap: 14.6 MB — entirely Google Benchmark framework (result storage, statistics).
Zero heap allocation in the hot path — SOA arrays are stack/static,
seqlock counter embedded in the object.
```

The order book hot path (add, best_bid, seqlock handoff) allocates nothing. The
MPSC queue (N-SPSC composition) uses pre-allocated ring slots. MarketDataEvent
is stack-allocated and moved into a pre-owned queue slot.

The feed handler must maintain this guarantee from the NIC all the way into the
order book.

### Why heap allocation breaks latency guarantees

A `malloc`/`new` call on the hot path:

- invokes the allocator's free-list search (non-constant time)
- may call `brk`/`mmap` if no free block is available (system call, kernel
  scheduler involvement)
- introduces lock contention under concurrent allocation (glibc ptmalloc has
  per-arena locks)
- produces non-deterministic latency that accumulates at p99.9 and above

None of these properties are acceptable on a path that directly affects the
freshness of order book state.

### The full pipeline

```
NIC/pcap PMD
    │ rte_mbuf* (DPDK mempool — pre-allocated at startup)
    ▼
ITCH parser (zero-copy — reads directly from mbuf data pointer)
    │ MarketDataEvent (stack-allocated)
    ▼
MPSC queue (pre-allocated ring — move into slot)
    │
    ▼
Book writer thread → SOA order book (pre-sized arrays)
```

Each stage has a specific pre-allocation strategy:

| Stage | Mechanism | Allocated at |
|---|---|---|
| NIC buffer | `rte_mempool` (hugepage slab) | `rte_mempool_create()` at startup |
| ITCH parser output | `MarketDataEvent` on stack | call frame — no allocation |
| MPSC queue slots | N-SPSC ring arrays | queue construction |
| SOA order book arrays | `std::array` / fixed-size vectors | order book construction |

## Decision

Zero heap allocation on the hot path is a first-class design constraint for this
project. No `new`, `malloc`, `std::vector::push_back` (beyond capacity), or any
standard container that allocates on insert may appear on any code path between
`rte_eth_rx_burst()` and `order_book.add()`.

All capacity decisions are made at startup from configuration (maximum price
levels, queue depth, mbuf pool size). The system runs at fixed memory
footprint after initialization.

Pre-allocation equivalences:

- `rte_mempool` is to the NIC boundary what SOA pre-sizing is to the order book:
  a declaration of capacity made once at startup, never revisited at runtime.
- `MarketDataEvent` on the stack is the zero-cost handoff between parser and
  queue: moved into a pre-owned slot, no allocation on either side.

Violations of this constraint on the hot path are treated as bugs, not
trade-offs.

## Consequences

### Positive

- Allocator jitter is eliminated from the hot path. Latency distribution is
  determined by data structure access patterns and memory ordering, not by
  allocator non-determinism.
- Memory footprint is fixed and auditable at startup. Tools like Valgrind massif
  confirm zero hot-path allocation (as already demonstrated in
  `low-latency-order-book`).
- The constraint is self-documenting: any future contributor adding an allocation
  on the hot path must explicitly justify the violation.

### Negative

- Capacity must be sized correctly at startup. Under-sizing causes the pipeline
  to drop data (queue full) or fail at initialization (mempool too small).
  Over-sizing wastes locked hugepage memory.
- Startup time increases: mempool and queue pre-allocation is not free.
- Some standard library utilities (e.g. `std::string`, `std::vector` with
  dynamic growth, `std::function`) cannot be used on the hot path. This
  restricts the API surface of hot-path components.
- The constraint applies only to the hot path. Initialization, configuration
  parsing, and logging infrastructure may allocate freely.
