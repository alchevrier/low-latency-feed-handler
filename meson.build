project(
  'low-latency-feed-handler',
  'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++23',
    'warning_level=3',
    'optimization=3',
    'b_ndebug=if-release',
  ],
)

cpp = meson.get_compiler('cpp')

add_project_arguments(
  cpp.get_supported_arguments([
    '-march=native',
    '-Wpedantic',
  ]),
  language : 'cpp',
)

# DPDK — system dependency via pkg-config (see ADR-001)
# Development: pcap PMD, --vdev net_pcap0,rx_pcap=itch.pcap (see ADR-003)
# Production:  hardware PMD, -a <PCI addr> — binary is PMD-agnostic
libdpdk = dependency('libdpdk')

# low-latency-order-book — Meson subproject (see ADR-006)
# Provides: MPSC queue, SPSC queue, SOA order book, seqlock, PinnedThread
llob_proj = subproject('low-latency-order-book')
llob_dep  = llob_proj.get_variable('llob_dep')

# ITCH parser headers
itch_inc = include_directories('include')

subdir('src')

if get_option('enable_tests')
  subdir('tests')
endif

if get_option('enable_benchmarks')
  subdir('benchmarks')
endif
