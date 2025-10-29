# Google Test-based integration tests for DeSmuME (POSIX)

# Only build tests when explicitly enabled
if not get_option('build_tests')
  subdir_done()
endif

# Resolve GoogleTest; prefer system, fallback to subproject if configured by wrap
# We require gtest main so we can declare TEST() suites easily
# Meson will automatically add pthreads on Linux via dependency('threads') if needed

dep_gtest = dependency('gtest', main: true, required: true)

# Reuse the same include directories and deps as the main library
# The parent meson.build defines `includes`, `dependencies`, and `libdesmume`.

# Test sources

test_sources = files(
  'integration/test_context.cpp',
  'integration/test_cpu_context.cpp',
  'integration/test_mmu_context.cpp',
  'integration/test_multithread.cpp',
)

# Test executable links against the static library to exercise real code paths

test_exe = executable('desmume_tests',
  test_sources,
  dependencies: [dep_gtest] + dependencies,
  include_directories: includes,
  link_with: libdesmume,
  cpp_args: ['-DUNIT_TESTING=1'],
)

# Register tests
# Use one invocation to run all gtest suites; verbosity is controlled by Meson flags

test('integration_tests', test_exe)
