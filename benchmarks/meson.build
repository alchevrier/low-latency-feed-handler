# benchmarks/meson.build
#
# TSC-based benchmarks — see ADR-007.
# pcap PMD benchmarks measure parser throughput (lower bound on latency).
# NIC-to-book latency requires hardware PMD — not measurable here.

gbench = dependency('benchmark', required : get_option('enable_benchmarks'))

# BM_UnwrapParse — MoldUDP64 unwrap + ITCH parse (compute floor, no DPDK runtime)
# BM_ParseDirect — raw parse_add_order only (subtract from BM_UnwrapParse for unwrap cost)
# BM_Pipeline    — full hot path: unwrap + parse + order_book.add
# libdpdk is a header-only dependency here (rte_byteorder.h) — no EAL init required.
executable(
  'pipeline_bench',
  'pipeline_bench.cpp',
  include_directories : itch_inc,
  dependencies        : [gbench, llob_dep, libdpdk],
)

# RDTSC-free hot-path driver for Cachegrind and Massif.
# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./build/benchmarks/cachegrind_driver
# valgrind --tool=massif ./build/benchmarks/cachegrind_driver
executable(
  'cachegrind_driver',
  'cachegrind_driver.cpp',
  include_directories : itch_inc,
  dependencies        : [llob_dep, libdpdk],
)
