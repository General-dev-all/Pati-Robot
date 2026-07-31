// Konak testinde gorev YOK: kareleri test kendi cagiriyor.
#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
inline void vTaskDelay(TickType_t) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline BaseType_t xTaskDelayUntil(TickType_t*, TickType_t) { return pdTRUE; }
inline BaseType_t xTaskCreate(void (*)(void*), const char*, std::uint32_t,
                              void*, std::uint32_t, TaskHandle_t*)
{
    return pdPASS;
}
