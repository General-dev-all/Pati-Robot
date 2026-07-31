// Konak testi: saati TEST SURUYOR.
#pragma once
#include <cstdint>
extern std::int64_t test_saat_us;
inline std::int64_t esp_timer_get_time() { return test_saat_us; }
