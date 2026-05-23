# tests/meson.build

gtest = dependency('gtest_main', required : get_option('enable_tests'))

itch_parser_test = executable(
  'itch_parser_test',
  'itch_parser_test.cpp',
  include_directories : itch_inc,
  dependencies        : [gtest, llob_dep, libdpdk],
)

test('itch_parser_test', itch_parser_test)

mold_udp64_test = executable(
  'mold_udp64_test',
  'mold_udp64_test.cpp',
  include_directories : itch_inc,
  dependencies        : [gtest, llob_dep, libdpdk],
)

test('mold_udp64_test', mold_udp64_test)