/**
 * @file    private_device.c
 * @brief   XC100 Hello World 示例
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xf_log.h"
#include "xf_private_device.h"

#define LOCAL_TAG               "xc100"

void xf_pri_device_start(void)
{
    for (;;) {
        LOG_I("Hello world! XC100 / ESP32-P4");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
