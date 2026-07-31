// Konak testi: gunluk cikisi sessiz. Test kendi ciktisini basiyor.
#pragma once
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)

// Hafiza son_gorulme damgasi icin. HER CAGRIDA ARTIYOR: Python tarafinda
// da damgalar artiyor ve budama/siralama ona bakiyor. Sabit donse
// siralama kararsiz olur ve karsilastirma anlamsizlasirdi.
#include <cstdint>
inline std::uint32_t esp_log_timestamp()
{
    static std::uint32_t t = 1000;
    t += 1000;
    return t;
}
