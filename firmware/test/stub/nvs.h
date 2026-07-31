// Konak testi: NVS taklidi.
//
// Hafizanin METIN mantigi test ediliyor, kalici depolama degil. NVS
// cagrilari basarili donup hicbir sey yapmiyor; boylece hafiza_*
// fonksiyonlarinin gercek yolu (dedup, budama, prompt) calisiyor.
#pragma once
#include <cstddef>
#include "esp_err.h"

#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef unsigned int nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

inline esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* h)
{
    if (h) *h = 1;
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t)
{
    return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*)
{
    return ESP_ERR_NVS_NOT_FOUND;
}
