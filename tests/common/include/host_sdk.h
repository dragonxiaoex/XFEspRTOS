#ifndef HOST_SDK_H
#define HOST_SDK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef int uart_port_t;
#define UART_NUM_1 1
#define UART_NUM_MAX 6
#define SOC_UART_HP_NUM 5
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 1
#define UART_PIN_NO_CHANGE -1
#include "freertos/FreeRTOS.h"
/* SDK 替身只供 UART 适配实现测试使用。 */
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef enum { UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL, UART_PARITY_ERR, UART_FRAME_ERR, UART_BREAK } uart_event_type_t;
typedef struct
{
    uart_event_type_t type;
    size_t size;
} uart_event_t;
typedef struct
{
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
bool uart_is_driver_installed(uart_port_t port);
esp_err_t uart_driver_install(uart_port_t port, int rx_size, int tx_size, int queue_size, QueueHandle_t *queue,
                              int flags);
esp_err_t uart_driver_delete(uart_port_t port);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
int uart_read_bytes(uart_port_t port, void *data, uint32_t length, uint32_t ticks);
int uart_write_bytes(uart_port_t port, const void *data, size_t length);
esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *length);
esp_err_t uart_flush_input(uart_port_t port);
esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *event, TickType_t ticks);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack, void *args, int priority,
                       TaskHandle_t *handle);
#endif
