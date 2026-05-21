# src/meson.build
#
# Feed handler executable — populated as the ITCH parser and DPDK pipeline
# are implemented. Build order (ADR-004):
#   1. ITCH Add Order parser (no DPDK, validate against pcap corpus)
#   2. pcap PMD pipeline (rte_eth_rx_burst → parser → MPSC → book)
#   3. Benchmarks

# feed_handler_sources = files(
#   'itch_parser.cpp',
#   'rx_pipeline.cpp',
# )

# executable(
#   'feed-handler',
#   feed_handler_sources,
#   include_directories : itch_inc,
#   dependencies        : [libdpdk, llob_dep],
# )
