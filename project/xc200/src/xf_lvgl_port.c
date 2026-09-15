#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stdint.h"

#include "xf_lvgl_port.h"

static SemaphoreHandle_t lvgl_mux;                  // LVGL mutex

void xf_lvgl_port_init(void)
{
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
}

int xf_lvgl_port_lock(uint32_t timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void xf_lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}


