# ADR-004: ITCH 5.0 Message Type Selection

## Status

Accepted

## Context

The NASDAQ ITCH 5.0 protocol defines over 20 message types. Implementing all of
them in the first iteration would expand the parser surface area significantly,
delay integration testing of the end-to-end pipeline, and introduce more failure
modes before the core path is validated.

The `low-latency-order-book` project followed the same discipline: the SOA order
book was built ADD-only first, validated end-to-end, then extended. This approach
surfaces pipeline issues (queue backpressure, mbuf lifecycle, seqlock contention)
before the parser complexity grows.

### ITCH 5.0 message types relevant to an order book

| Type | Name | Purpose |
|---|---|---|
| `A` | Add Order (no MPID) | New resting order at price level |
| `F` | Add Order with MPID | New resting order with market participant ID |
| `E` | Order Executed | Full or partial fill at original price |
| `C` | Order Executed with Price | Fill at a different (negotiated) price |
| `X` | Order Cancel | Partial cancellation — reduce qty |
| `D` | Order Delete | Full removal of resting order |
| `U` | Order Replace | Delete + re-add at new price/qty |

All other message types (system events, stock directory, NOII, RPII, etc.) are
metadata — they do not modify order book state and are ignored on the hot path.

### Selection rationale

The `low-latency-order-book` subproject currently implements ADD only. The feed
handler parser must match the book's capability — implementing message types the
book cannot consume adds parser complexity with no observable end-to-end effect
and defers pipeline validation.

The correct sequence is: validate the full pipeline end-to-end with ADD, then
extend the order book and parser together.

## Decision

Implement **`A` (Add Order) only** in this iteration. This validates the complete
pipeline — pcap PMD → parser → `MarketDataEvent` → MPSC → SOA book add — with
the smallest possible parser surface area.

All other message types (`F`, `E`, `C`, `X`, `D`, `U`) are explicitly deferred
until the `low-latency-order-book` subproject is extended to support them. Parser
and book extensions are developed together — a message type is not implemented in
the parser until the corresponding book operation exists.

All unrecognised message types are silently skipped — the parser reads the
message length field and advances the buffer pointer without allocating or
processing. This is the correct default for ITCH 5.0 (length-prefixed framing)
and holds for the deferred types until they are implemented.

## Consequences

### Positive

- The full pipeline (NIC → parser → MPSC → book) is validated with the smallest
  possible message surface area. Issues in DPDK setup, mbuf lifecycle, or queue
  integration are caught before parser complexity grows.
- Same incremental discipline as `low-latency-order-book` — proven to work.
- Unrecognised message types are skipped correctly from day one: length-prefixed
  skip is safe and required for ITCH compliance regardless of which types are
  implemented.

### Negative

- The book accumulates resting orders with no cancellation, execution, or
  deletion. It is not a correct representation of market state. This is
  acceptable for development validation and must not be used for any live
  decision-making.
- All deferred types (`F`, `E`, `C`, `X`, `D`, `U`) require coordinated
  extension of both the parser and the order book subproject. Neither is updated
  in isolation.
