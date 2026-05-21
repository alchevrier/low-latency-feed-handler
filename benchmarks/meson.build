# benchmarks/meson.build
#
# TSC-based benchmarks — see ADR-007.
# pcap PMD benchmarks measure parser throughput (lower bound on latency).
# NIC-to-book latency requires hardware PMD — not measurable here.

gbench = dependency('benchmark', required : get_option('enable_benchmarks'))

# parser_bench = executable(
#   'parser_bench',
#   'parser_bench.cpp',
#   include_directories : itch_inc,
#   dependencies        : [gbench, llob_dep],
# )
# benchmark('parser_bench', parser_bench)
