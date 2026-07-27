#pragma once

#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
  #include <expected>
  namespace mondoc {
    using std::expected;
    using std::unexpected;
  }
#else
  #include <tl/expected.hpp>
  namespace mondoc {
    using tl::expected;
    using tl::unexpected;
  }
#endif
