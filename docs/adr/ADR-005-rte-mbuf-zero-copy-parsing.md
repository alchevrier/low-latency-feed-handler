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

### MoldUDP64 encapsulation

NASDAQ ITCH 5.0 messages are not delivered as raw UDP payloads. They are
encapsulated in **MoldUDP64** packets. Each UDP datagram contains one MoldUDP64
frame:

```
Session          [10 bytes] — ASCII session identifier
Sequence Number   [8 bytes] — big-endian uint64, sequence number of the first
                              message in this packet
Message Count     [2 bytes] — big-endian uint16, number of ITCH messages

For each message:
  Message Length  [2 bytes] — big-endian uint16
  Message Data    [variable] — ITCH 5.0 message bytes
```

The MoldUDP64 sequence number is the carrier of the exchange-guaranteed ordering
requirement cited in ADR-001 (FINRA Rule 4370). Each message in the packet is
assigned sequence number `seq_no + i` for `i` in `[0, msg_count)`. The ITCH
parser operates on the message payload only — the MoldUDP64 header is unwrapped
first, in-place from the same mbuf pointer.

This adds one layer of framing above ITCH but does not change the zero-copy
principle: both the MoldUDP64 header and the ITCH message payloads are read
directly from the mbuf without copying.

### The naive approach (copy)

```cpp
uint8_t buffer[256];
memcpy(buffer, rte_pktmbuf_mtod(mbuf, const uint8_t*), mbuf->data_len);
rte_pktmbuf_free(mbuf);
// unwrap MoldUDP64 header from buffer, then parse each ITCH message
```

This copies the entire MoldUDP64 frame to a local buffer before parsing. It is
safe but introduces a memcpy on every UDP datagram. The packet data is read
twice (once into buffer, once by the parser), adding unnecessary cache pressure.

### The zero-copy approach

The parser receives a pointer directly into the mbuf via `rte_pktmbuf_mtod()`.
It reads the MoldUDP64 header fields in-place, then advances a cursor through
each length-prefixed ITCH message, constructing a `MarketDataEvent` on the stack
for each one. The mbuf is freed once after the cursor has consumed all messages.
No data is copied at any point — the cursor reads bytes directly from the
hugepage mempool slab.

This is valid because:

1. A MoldUDP64 datagram fits in a single UDP packet — always single-segment mbuf
   (`mbuf->next == NULL`). Multi-segment handling is not needed.
2. The parser does not retain any pointer into the mbuf after returning. The
   `MarketDataEvent` contains only value-typed fields — no raw pointers.
3. `rte_pktmbuf_free()` is called only after the cursor has advanced past all
   messages — the pointer is never used after free.

### mbuf lifetime contract

```
rte_eth_rx_burst()           → mbuf owned by application
rte_pktmbuf_mtod()           → typed pointer into MoldUDP64 frame (valid)
parse MoldUDP64 header       → seq_no, msg_count extracted in-place
for each ITCH message:       → cursor advances through frame (no copy)
  parse_itch()               → MarketDataEvent constructed on stack
  push(std::move(event))     → moved into pre-owned MPSC queue slot
rte_pktmbuf_free()           → mbuf returned to pool, frame pointer invalid
```

The mbuf is live for the duration of the full datagram — freed once after all
messages are consumed. The pool can reuse the buffer on the next
`rte_eth_rx_burst()` call.

### Byte ordering

ITCH 5.0 encodes all multi-byte integers in network byte order (big-endian).
The parser converts each field at parse time using `__builtin_bswap16` /
`__builtin_bswap32` / `__builtin_bswap64` (compiles to `bswap` on x86 — single
instruction). The `MarketDataEvent` fields are stored in host byte order (little-
endian on x86). No byte-order conversion happens after the parsing boundary.

## Decision

Unwrap the MoldUDP64 header and parse each ITCH 5.0 message in-place from the
`rte_mbuf` data pointer using `rte_pktmbuf_mtod()`. Extract all required fields
into a stack-allocated `MarketDataEvent` in host byte order. Free the mbuf to
the pool after all messages in the datagram are consumed. Neither the MoldUDP64
unwrapper nor the ITCH parser may retain any pointer into mbuf data after the
mbuf is freed.

Hot path pattern:

```cpp
uint16_t nb_rx = rte_eth_rx_burst(port, queue, mbufs, BURST_SIZE);
for (uint16_t i = 0; i < nb_rx; ++i) {
    const uint8_t* frame   = rte_pktmbuf_mtod(mbufs[i], const uint8_t*);
    uint64_t seq_no        = bswap64(read<uint64_t>(frame + 10));
    uint16_t msg_count     = bswap16(read<uint16_t>(frame + 18));
    const uint8_t* cursor  = frame + 20;
    for (uint16_t j = 0; j < msg_count; ++j) {
        uint16_t msg_len       = bswap16(read<uint16_t>(cursor));
        MarketDataEvent event  = parse_itch(cursor + 2, msg_len, seq_no + j);
        mpsc_queue.push(std::move(event));
        cursor += 2 + msg_len;
    }
    rte_pktmbuf_free(mbufs[i]);
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
