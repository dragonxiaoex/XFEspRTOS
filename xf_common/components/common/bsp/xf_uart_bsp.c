/**
 * @file    xf_uart_bsp.c
 * @brief   UART 适配层的 ESP-IDF 实现
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#include <stdlib.h>

#include "driver/uart.h"
#include "freertos/queue.h"
#include "soc/soc_caps.h"

#include "xf_uart_bsp.h"

/** @brief 串口实例，仅保存调用 SDK 所需的句柄。 */
struct xf_uart {
    uart_port_t port;
    QueueHandle_t event_queue;
};

/**
 * @brief 转换 SDK 状态，不向应用传播厂商错误码
 *
 * @param [in] error - SDK 状态码.
 * @return 自有状态码.
 */
static xf_err_t error_from_sdk(esp_err_t error)
{
    xf_err_t result;

    switch (error) {
    case ESP_OK: {
        result = XF_OK;
        break;
    }

    case ESP_ERR_INVALID_ARG: {
        result = XF_ERR_INVALID_ARG;
        break;
    }

    case ESP_ERR_INVALID_STATE: {
        result = XF_ERR_INVALID_STATE;
        break;
    }

    case ESP_ERR_NO_MEM: {
        result = XF_ERR_NO_MEM;
        break;
    }

    case ESP_ERR_TIMEOUT: {
        result = XF_ERR_TIMEOUT;
        break;
    }

    case ESP_ERR_NOT_SUPPORTED: {
        result = XF_ERR_NOT_SUPPORTED;
        break;
    }

    default: {
        result = XF_ERR_IO;
        break;
    }
    }

    return result;
}

xf_err_t xf_uart_init(const xf_uart_config_t *config, xf_uart_handle_t *handle)
{
    struct xf_uart *uart = NULL;
    esp_err_t ret = ESP_OK;

    if ((config == NULL) || (handle == NULL)) {
        return XF_ERR_INVALID_ARG;
    }

    if (((uint32_t)config->port >= SOC_UART_HP_NUM) || (config->event_queue_size < 0)) {
        return XF_ERR_INVALID_ARG;
    }

    if (uart_is_driver_installed(config->port)) {
        return XF_ERR_INVALID_STATE;
    }

    uart = calloc(1, sizeof(*uart));

    if (uart == NULL) {
        return XF_ERR_NO_MEM;
    }

    uart->port = config->port;

    ret = uart_driver_install(uart->port, config->rx_buffer_size, config->tx_buffer_size,
                              config->event_queue_size, config->event_queue_size > 0 ? &uart->event_queue : NULL, 0);

    if (ret != ESP_OK) {
        free(uart);
        return error_from_sdk(ret);
    }

    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_param_config(uart->port, &uart_config);

    if (ret != ESP_OK) {
        goto fail;
    }

    ret = uart_set_pin(uart->port, config->tx_pin, config->rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (ret != ESP_OK) {
        goto fail;
    }

    *handle = uart;

    return XF_OK;

fail:
    uart_driver_delete(uart->port);
    free(uart);

    return error_from_sdk(ret);
}

int32_t xf_uart_read(xf_uart_handle_t handle, void *data, int32_t length, TickType_t ticks_to_wait)
{
    if ((handle == NULL) || (data == NULL) || (length <= 0)) {
        return -1;
    }

    return uart_read_bytes(handle->port, data, length, ticks_to_wait);
}

int32_t xf_uart_write(xf_uart_handle_t handle, const void *data, int32_t length)
{
    if ((handle == NULL) || (data == NULL) || (length <= 0)) {
        return -1;
    }

    return uart_write_bytes(handle->port, data, length);
}

xf_err_t xf_uart_wait_event(xf_uart_handle_t handle, xf_uart_event_t *event, TickType_t ticks_to_wait)
{
    uart_event_t sdk_event;

    if ((handle == NULL) || (event == NULL)) {
        return XF_ERR_INVALID_ARG;
    }

    if (handle->event_queue == NULL) {
        return XF_ERR_INVALID_STATE;
    }

    if (xQueueReceive(handle->event_queue, &sdk_event, ticks_to_wait) != pdTRUE) {
        return XF_ERR_TIMEOUT;
    }

    switch (sdk_event.type) {
    case UART_DATA: {
        *event = XF_UART_EVENT_DATA;
        break;
    }

    case UART_FIFO_OVF:
    case UART_BUFFER_FULL: {
        *event = XF_UART_EVENT_RX_OVERFLOW;
        break;
    }

    case UART_PARITY_ERR: {
        *event = XF_UART_EVENT_PARITY_ERROR;
        break;
    }

    case UART_FRAME_ERR: {
        *event = XF_UART_EVENT_FRAME_ERROR;
        break;
    }

    case UART_BREAK: {
        *event = XF_UART_EVENT_BREAK;
        break;
    }

    default: {
        *event = XF_UART_EVENT_OTHER;
        break;
    }
    }

    return XF_OK;
}

xf_err_t xf_uart_get_rx_length(xf_uart_handle_t handle, size_t *length)
{
    if ((handle == NULL) || (length == NULL)) {
        return XF_ERR_INVALID_ARG;
    }

    return error_from_sdk(uart_get_buffered_data_len(handle->port, length));
}

xf_err_t xf_uart_flush_input(xf_uart_handle_t handle)
{
    if (handle == NULL) {
        return XF_ERR_INVALID_ARG;
    }

    return error_from_sdk(uart_flush_input(handle->port));
}

xf_err_t xf_uart_wait_tx_done(xf_uart_handle_t handle, TickType_t ticks_to_wait)
{
    if (handle == NULL) {
        return XF_ERR_INVALID_ARG;
    }

    return error_from_sdk(uart_wait_tx_done(handle->port, ticks_to_wait));
}

xf_err_t xf_uart_deinit(xf_uart_handle_t handle)
{
    esp_err_t ret = ESP_OK;

    if (handle == NULL) {
        return XF_ERR_INVALID_ARG;
    }

    ret = uart_driver_delete(handle->port);

    if (ret != ESP_OK) {
        return error_from_sdk(ret);
    }

    free(handle);

    return XF_OK;
}
