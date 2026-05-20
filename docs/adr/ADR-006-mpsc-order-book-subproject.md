# ADR-006: MPSC Integration from low-latency-order-book Subproject

## Status

Accepted

## Context

The feed handler requires an MPSC queue to aggregate market data events from
multiple feed threads (one per market data feed) into the single book writer
thread. An MPSC queue is also already implemented, tested, and benchmarked in
`low-latency-order-book` (ADR-005 in that repo).

The `low-latency-order-book` implementation uses N-SPSC composition: each
producer owns its own SPSC queue, and the consumer polls all queues in round-
robin with a starvation guard. This avoids the complexity of a true shared-head
MPSC while preserving the lock-free, zero-allocation properties required by
ADR-002.

### The options

**Option A — Duplicate the implementation**

Copy `include/llob/mpsc_queue.hpp` (and `spsc_queue.hpp`) into this repo. The
feed handler has no dependency on `low-latency-order-book`.

Drawbacks:
- Two copies of the same code diverge over time.
- Bug fixes or improvements in the original do not propagate automatically.
- The SOA order book (`order_book.hpp`) would also need to be duplicated — the
  book writer thread writes directly into the book using its API.

**Option B — Pull as a Meson subproject**

Reference `low-latency-order-book` as a Meson subproject (via
`subprojects/low-latency-order-book/`). The subproject exposes an `llob_dep`
Meson dependency that provides the include path for `include/llob/`. Application
code uses `#include <llob/mpsc_queue.hpp>` and `#include <llob/order_book.hpp>`
directly.

This is the standard Meson pattern for header-only or static-linked subprojects.
DPDK uses the same pattern for its own subproject dependencies.

**Option C — Package manager (Conan/vcpkg)**

`low-latency-order-book` is not published to any package registry. This option
requires either publishing it or maintaining a private registry — disproportionate
overhead for a portfolio project with a single dependency.

### Build system choice: Meson

The feed handler uses Meson as its build system (not CMake) because DPDK itself
uses Meson natively. Using CMake would require bridging CMake and Meson for DPDK
integration, or duplicating DPDK's build configuration. `dependency('libdpdk')`
via pkg-config is the idiomatic Meson approach.

`low-latency-order-book` uses CMake. The subproject integration requires a thin
`meson.build` in the subproject directory that exposes `llob_dep` as an
`include_directories` dependency — no CMake is invoked.

## Decision

Pull `low-latency-order-book` as a Meson subproject. The subproject provides
`include/llob/` headers (MPSC queue, SPSC queue, SOA order book, seqlock,
PinnedThread). The feed handler links against `llob_dep` declared in
`subprojects/low-latency-order-book/meson.build`.

Subproject structure:

```
subprojects/
└── low-latency-order-book/
    ├── meson.build        # declares llob_dep = declare_dependency(...)
    └── include/
        └── llob/
            ├── spsc_queue.hpp
            ├── mpsc_queue.hpp
            ├── order_book.hpp
            ├── pinned_thread.hpp
            └── ...
```

The `include/llob/` directory is copied from the `low-latency-order-book` repo
head at integration time. Updates are applied deliberately — not automatically —
to avoid unexpected API changes breaking the feed handler pipeline.

## Consequences

### Positive

- No code duplication. The MPSC queue, seqlock, and order book implementations
  are reused as-is, with their existing unit test coverage as the correctness
  baseline.
- The SOA order book is available to the book writer thread without
  re-implementation. The full pipeline (NIC → parser → MPSC → book) composes
  from validated components.
- Meson subproject integration is lightweight: a single `meson.build` file in
  the subproject directory, no build system bridging required.
- `PinnedThread` from the subproject is reused for the EAL lcore threads'
  pinning validation at startup.

### Negative

- Updates to `low-latency-order-book` must be manually synchronised into the
  subproject directory. There is no automatic tracking of upstream changes.
- The subproject `include/llob/` copy must be kept in sync with the
  `low-latency-order-book` repo. A divergence creates a maintenance gap.
- `low-latency-order-book` uses CMake for its own build. The subproject
  integration bypasses CMake entirely — the subproject's `CMakeLists.txt` is
  ignored. This means the subproject's own tests cannot be run from the feed
  handler build. They must be run separately from the `low-latency-order-book`
  repo.
