project('low-latency-order-book', 'cpp')

# Thin Meson wrapper for low-latency-order-book (header-only interface).
# The subproject's CMakeLists.txt is intentionally ignored — see ADR-006.
#
# Populate include/llob/ by copying headers from the low-latency-order-book
# repo at integration time. Updates are applied deliberately, not automatically.
#
# Provides:
#   llob_dep — include path for <llob/mpsc_queue.hpp>, <llob/order_book.hpp>,
#              <llob/spsc_queue.hpp>, <llob/pinned_thread.hpp>, etc.

llob_inc = include_directories('include')
llob_dep = declare_dependency(include_directories : llob_inc)
