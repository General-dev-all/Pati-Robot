// Konak testi: rastgeleyi TEST SURUYOR.
//
// Belirlenimci olmasi sart — tarayici ile piksel piksel karsilastirma
// yapiliyor ve iki tarafta ayni sakkad/kirpma olmasi gerekiyor.
// Test 0 donduruyor: rastgele() 0 verir, yani kirpma tetiklenmez.
#pragma once
#include <cstdint>
extern std::uint32_t test_rastgele_deger;
inline std::uint32_t esp_random() { return test_rastgele_deger; }
