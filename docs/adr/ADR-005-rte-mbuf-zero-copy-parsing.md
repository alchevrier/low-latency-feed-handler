# ADR-005: rte_mbuf Zero-Copy Parsing Boundary

## Status

Accepted

## Context

DPDK delivers received packets as `rte_mbuf` structures backed by a pre-allocated
hugepage mempool. Each `rte_mbuf` contains a pointer to packet data within the
mempool slab — it does not copy data. The application takes ownership of the
mbuf for the duration of its processing.

The key question is: **where does packet data become application data, and at
what cost?**

### The naive approach (copy)

```cpp
uint8_t buffer[64];
memcpy(buffer, rte_pktmbuf_mtod(mbuf, const uint8_t*), mbuf->data_len);
rte_pktmbuf_free(mbuf);
MarketDataEvent event = parse_itch(buffer, sizeof(buffer));
```

This copies packet data to a local buffer before parsing. It is safe but
introduces a memcpy on every packet — typically 50–200 bytes for ITCH messages.
More importantly it adds unnecessary cache pressure: the packet data is read
twice (once into buffer, once by the parser).

### The zero-copy approach

```cpp
const uint8_t* data = rte_pktmbuf_mtod(mbuf, const uint8_t*);
MarketDataEvent event = parse_itch(data, mbuf->data_len);
rte_pktmbuf_free(mbuf);
// event is on the stack — mbuf is back in the pool
```

The parser reads directly from the mbuf data pointer. `MarketDataEvent` is
constructed on the stack from the raw bytes — field-by-field reads with
appropriate byte-order conversion (ITCH 5.0 is big-endian). Once the event is
extracted, the mbuf is freed to the pool immediately.

This is valid because:

1. ITCH 5.0 messages are small (< 50 bytes) — always single-segment mbuf
   (`mbuf->next == NULL`). Multi-segment handling is not needed.
2. The parser does not retain any pointer into the mbuf after returning. The
   `MarketDataEvent` contains only value-typed fields — no raw pointers.
3. `rte_pktmbuf_free()` returns the mbuf to the pool; after this point the data
   pointer is invalid. The ordering (parse → free) guarantees the pointer is
   never used after free.

### mbuf lifetime contract

```
rte_eth_rx_burst()       → mbuf owned by application
rte_pktmbuf_mtod()       → typed pointer into mbuf data region (valid)
parse_itch()             → reads from pointer, constructs MarketDataEvent
rte_pktmbuf_free()       → mbuf returned to pool, data pointer invalid
push(std::move(event))   → event moved into pre-owned MPSC queue slot
```

The mbuf is live for the minimum possible duration. The pool can reuse the buffer
on the next `rte_eth_rx_burst()` call.

### Byte ordering

ITCH 5.0 encodes all multi-byte integers in network byte order (big-endian).
The parser converts each field at parse time using `__builtin_bswap16` /
`__builtin_bswap32` / `__builtin_bswap64` (compiles to `bswap` on x86 — single
instruction). The `MarketDataEvent` fields are stored in host byte order (little-
endian on x86). No byte-order conversion happens after the parsing boundary.

## Decision

Parse ITCH 5.0 messages in-place from the `rte_mbuf` data pointer using
`rte_pktmbuf_mtod()`. Extract all required fields into a stack-allocated
`MarketDataEvent` in host byte order. Free the mbuf to the pool immediately
after extraction. The parser must not retain any pointer into mbuf data after
returning.

Hot path pattern:

```cpp
uint16_t nb_rx = rte_eth_rx_burst(port, queue, mbufs, BURST_SIZE);
for (uint16_t i = 0; i < nb_rx; ++i) {
    const uint8_t* data = rte_pktmbuf_mtod(mbufs[i], const uint8_t*);
    MarketDataEvent event = parse_itch(data, mbufs[i]->data_len);
    rte_pktmbuf_free(mbufs[i]);
    mpsc_queue.push(std::move(event));
}
```

## Consequences

### Positive

- Zero memcpy on the hot path — packet data is read exactly once by the parser.
- mbuf lifetime is minimal — returned to pool as soon as the event is extracted,
  maximising pool availability for the next burst.
- `MarketDataEvent` contains no raw pointers — it is safe to move across the
  MPSC queue boundary without lifetime concerns.
- The parsing boundary is explicit and auditable: everything after
  `rte_pktmbuf_free()` is application domain, everything before it is DPDK
  domain.

### Negative

- Multi-segment mbufs are not handled. ITCH 5.0 messages are < 50 bytes and
  will always fit in a single segment, but this assumption must be enforced by an
  assertion (`assert(mbuf->next == nullptr)`) to catch unexpected configurations.
- The parser must be careful not to read beyond `data_len` — buffer overread
  would access invalid memory within the mempool slab. Length validation is
  required on every message.
- `rte_pktmbuf_mtod` applies `data_off` (headroom offset) internally. The
  returned pointer points to the start of packet payload, not the start of the
  mbuf structure. This is the correct pointer for parsing but must not be confused
  with the mbuf's internal layout.
