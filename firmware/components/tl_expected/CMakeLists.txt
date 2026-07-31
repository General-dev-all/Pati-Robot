# Wrap the upstream TartanLlama/expected single-header library as an
# INTERFACE-only ESP-IDF component. Consumers can `REQUIRES tl_expected` and
# `#include <tl/expected.hpp>`.
idf_component_register(INCLUDE_DIRS "expected/include")
