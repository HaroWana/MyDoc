#pragma once

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
  #include <expected>
#else
  #include <tl/expected.hpp>
  namespace std {
    using tl::expected;
    using tl::unexpected;
  }
#endif
