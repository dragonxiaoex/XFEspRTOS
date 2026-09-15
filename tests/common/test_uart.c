/**
 * @file    test_uart.c
 * @brief   UART 适配与 XC200 收包流程的主机回归测试
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "host_sdk.h"
#include "xf_uart_bsp.h"
#include "xf_utils.h"
/* 直接测试产品的静态收包路径，避免为测试增加生产接口。 */
#include "../../project/xc200/src/xf_packet_handle.c"

typedef struct
{
    bool installed;
    QueueHandle_t queue;
    uart_config_t config;
    uint8_t rx[32768];
    size_t rx_len;
    uint8_t tx[32768];
    size_t tx_len;
} mock_port_t;
static mock_port_t ports[SOC_UART_HP_NUM];
static int fail_stage;
static bool fail_task;
static int native_calls;
static int calibration_count;
static uint8_t last_area;
static int eye_count;
static uart_event_t next_event;
static bool event_pending;
static esp_err_t tx_result = ESP_OK;
static esp_err_t delete_result = ESP_OK;

bool uart_is_driver_installed(uart_port_t port)
{
    assert(port >= 0 && port < SOC_UART_HP_NUM);
    native_calls++;

    return ports[port].installed;
}

esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue_size, QueueHandle_t *queue, int flags)
{
    native_calls++;
    assert(flags == 0);
    assert(!ports[port].installed);
    if (fail_stage == 1 || rx <= 128 || (tx != 0 && tx <= 128))
    {
        return ESP_FAIL;
    }
    ports[port].installed = true;
    if (queue_size > 0)
    {
        assert(queue);
        ports[port].queue = malloc(1);
        *queue = ports[port].queue;
    }
    else
    {
        assert(queue == NULL);
    }

    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t port)
{
    native_calls++;
    if (delete_result != ESP_OK)
    {
        return delete_result;
    }
    free(ports[port].queue);
    memset(&ports[port], 0, sizeof(ports[port]));

    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config)
{
    native_calls++;
    assert(ports[port].installed);
    if (fail_stage == 2)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ports[port].config = *config;

    return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts)
{
    native_calls++;
    assert(ports[port].installed && rts == -1 && cts == -1);
    assert(tx == 4 && rx == 5);

    return fail_stage == 3 ? ESP_ERR_INVALID_ARG : ESP_OK;
}

int uart_read_bytes(uart_port_t port, void *data, uint32_t length, uint32_t ticks)
{
    mock_port_t *p = &ports[port];
    native_calls++;
    if (!p->installed)
    {
        return -1;
    }
    assert(ticks == 0);
    size_t n = length < p->rx_len ? length : p->rx_len;
    memcpy(data, p->rx, n);
    p->rx_len -= n;
    memmove(p->rx, p->rx + n, p->rx_len);

    return n;
}

int uart_write_bytes(uart_port_t port, const void *data, size_t length)
{
    mock_port_t *p = &ports[port];
    native_calls++;
    if (!p->installed)
    {
        return -1;
    }
    assert(length <= sizeof(p->tx));
    memcpy(p->tx, data, length);
    p->tx_len = length;

    return length;
}

esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *length)
{
    assert(ports[port].installed);
    *length = ports[port].rx_len;

    return ESP_OK;
}

esp_err_t uart_flush_input(uart_port_t port)
{
    ports[port].rx_len = 0;

    return ESP_OK;
}

esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t ticks)
{
    assert(ports[port].installed);

    return tx_result;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *event, TickType_t ticks)
{
    assert(queue != NULL);
    if (!event_pending)
    {
        return 0;
    }
    memcpy(event, &next_event, sizeof(next_event));
    event_pending = false;

    return pdTRUE;
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack, void *args, int priority,
                       TaskHandle_t *handle)
{
    assert(ports[1].installed);
    assert(task && name && stack >= 4096 && priority > 0);
    if (fail_task)
    {
        return 0;
    }
    *handle = (void *)1;

    return pdPASS;
}

void eye_calib(uint8_t area)
{
    calibration_count++;
    last_area = area;
}

void eye_test_pos(uint8_t x, uint8_t y)
{
    eye_count++;
}

/** @brief 把外部字节流送入模拟驱动。 */
static void push(const uint8_t *data, size_t n)
{
    mock_port_t *p = &ports[1];
    assert(p->rx_len + n <= sizeof(p->rx));
    memcpy(p->rx + p->rx_len, data, n);
    p->rx_len += n;
}

/** @brief 通过真实适配接口唤醒产品收包路径。 */
static void notify_data(void)
{
    /* 故意使用过大的旧事件长度，消费端必须按缓冲区实际内容读取。 */
    next_event = (uart_event_t)
    {
        .type = UART_DATA, .size = 65535
    };
    event_pending = true;
    xf_uart_event_t event;
    assert(xf_uart_wait_event(packet_data.uart, &event, 0) == XF_OK);
    packet_handle_uart_event(event);
    assert(ports[1].rx_len == 0);
}

/** @brief 构造模式命令，独立于协议打包入口。 */
static size_t frame(uint8_t *data, size_t payload_size)
{
    memset(data, 0, payload_size + 8);
    data[0] = 0x55;
    data[1] = 0xaa;
    data[2] = 1;
    data[3] = MCU_DP_MODE;
    data[4] = payload_size & 255;
    data[5] = payload_size >> 8;
    if (payload_size > 0)
    {
        data[6] = MCU_DISPLAY_CALIB_MODE;
    }
    if (payload_size > 1)
    {
        data[7] = 42;
    }
    uint16_t crc = xf_crc16_calc(data + 3, payload_size + 3);
    data[payload_size + 6] = crc & 255;
    data[payload_size + 7] = crc >> 8;

    return payload_size + 8;
}

/** @brief 检查接口参数、失败回滚、事件转换与多串口隔离。 */
static void test_lifecycle(void)
{
    xf_uart_config_t config = {1, 921600, 4, 5, 4096, 1024, 20};
    xf_uart_handle_t handle = NULL;
    uint8_t data[] = {1, 2, 3};
    xf_uart_event_t event = XF_UART_EVENT_OTHER;
    assert(xf_uart_init(NULL, &handle) == XF_ERR_INVALID_ARG);
    config.port = -1;
    assert(xf_uart_init(&config, &handle) == XF_ERR_INVALID_ARG);
    config.port = SOC_UART_HP_NUM;
    assert(xf_uart_init(&config, &handle) == XF_ERR_INVALID_ARG);
    assert(xf_uart_read(NULL, data, sizeof(data), 0) == -1);
    assert(xf_uart_write(NULL, data, sizeof(data)) == -1);
    assert(xf_uart_deinit(NULL) == XF_ERR_INVALID_ARG);
    config.port = 1;
    assert(xf_uart_init(&config, NULL) == XF_ERR_INVALID_ARG);
    config.event_queue_size = -1;
    assert(xf_uart_init(&config, &handle) == XF_ERR_INVALID_ARG);
    assert(native_calls == 0 && handle == NULL);
    config.event_queue_size = 20;
    for (fail_stage = 1; fail_stage <= 3; fail_stage++)
    {
        assert(xf_uart_init(&config, &handle) != XF_OK);
        assert(!ports[1].installed && ports[1].queue == NULL && handle == NULL);
    }
    fail_stage = 0;
    assert(xf_uart_init(&config, &handle) == XF_OK);
    xf_uart_handle_t original = handle;
    config.baud_rate = 115200;
    assert(xf_uart_init(&config, &handle) == XF_ERR_INVALID_STATE);
    assert(handle == original && ports[1].config.baud_rate == 921600);
    config.port = 2;
    assert(xf_uart_init(&config, &handle) == XF_OK && handle != original);
    assert(xf_uart_write(original, data, sizeof(data)) == sizeof(data));
    data[0] = 9;
    assert(ports[1].tx[0] == 1 && ports[2].tx_len == 0);
    const xf_uart_event_t expected[] =
    {
        XF_UART_EVENT_DATA, XF_UART_EVENT_RX_OVERFLOW, XF_UART_EVENT_RX_OVERFLOW,
        XF_UART_EVENT_PARITY_ERROR, XF_UART_EVENT_FRAME_ERROR, XF_UART_EVENT_BREAK,
        XF_UART_EVENT_OTHER,
    };
    for (int i = 0; i < 7; i++)
    {
        next_event.type = i;
        event_pending = true;
        assert(xf_uart_wait_event(original, &event, 0) == XF_OK && event == expected[i]);
    }
    assert(xf_uart_wait_event(original, &event, 0) == XF_ERR_TIMEOUT);
    assert(event == XF_UART_EVENT_OTHER);
    const esp_err_t errors[] = {ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_NO_MEM,
                                ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_FAIL
                               };
    const xf_err_t mapped[] = {XF_OK, XF_ERR_TIMEOUT, XF_ERR_NO_MEM,
                               XF_ERR_NOT_SUPPORTED, XF_ERR_INVALID_ARG, XF_ERR_INVALID_STATE, XF_ERR_IO
                              };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++)
    {
        tx_result = errors[i];
        assert(xf_uart_wait_tx_done(original, 0) == mapped[i]);
    }
    delete_result = ESP_FAIL;
    assert(xf_uart_deinit(original) == XF_ERR_IO && ports[1].installed);
    assert(xf_uart_write(original, data, sizeof(data)) == sizeof(data));
    delete_result = ESP_OK;
    assert(xf_uart_deinit(original) == XF_OK && ports[2].installed);
    assert(xf_uart_deinit(handle) == XF_OK);
    config.event_queue_size = 0;
    config.tx_buffer_size = 0;
    assert(xf_uart_init(&config, &handle) == XF_OK);
    assert(xf_uart_wait_event(handle, &event, 0) == XF_ERR_INVALID_STATE);
    assert(xf_uart_deinit(handle) == XF_OK);
}

/** @brief 验证真实产品接收流程的拆包、粘包、边界与错误恢复。 */
static void test_packet(void)
{
    uint8_t data[MCU_PROTOCOL_MAX_FIFO];
    uint8_t noise[9] = {0};
    size_t n = frame(data, 2);
    fail_task = true;
    assert(xf_packet_handle_init() == XF_ERR_NO_MEM);
    assert(!ports[1].installed && packet_data.uart == NULL && packet_data.packet_task == NULL);
    fail_task = false;
    assert(xf_packet_handle_init() == XF_OK);
    assert(xf_packet_handle_init() == XF_ERR_INVALID_STATE);
    for (size_t split = 1; split < n; split++)
    {
        int count = calibration_count;
        push(data, split);
        notify_data();
        assert(calibration_count == count);
        push(data + split, n - split);
        notify_data();
        assert(calibration_count == count + 1 && last_area == 42);
    }
    int count = calibration_count;
    for (int i = 0; i < 300; i++)
    {
        push(data, n);
    }
    notify_data();
    assert(calibration_count == count + 300);
    count = calibration_count;
    push(noise, 7);
    push(data, 1);
    notify_data();
    push(data + 1, n - 1);
    notify_data();
    assert(calibration_count == count + 1);
    count = calibration_count;
    push(data, 1);
    push(data, n);
    notify_data();
    assert(calibration_count == count + 1);
    count = calibration_count;
    data[n - 1] ^= 1;
    push(data, n);
    frame(data, 2);
    push(data, n);
    notify_data();
    assert(calibration_count == count + 1);
    count = calibration_count;
    const uint8_t impossible[] = {0x55, 0xaa, 1, MCU_DP_MODE, 0xff, 0xff, 0, 0};
    push(impossible, sizeof(impossible));
    push(data, n);
    notify_data();
    assert(calibration_count == count + 1);
    for (int type = UART_FIFO_OVF; type <= UART_BREAK; type++)
    {
        count = calibration_count;
        push(data, 8);
        notify_data();
        push(data + 8, n - 8);
        next_event.type = type;
        event_pending = true;
        xf_uart_event_t event;
        assert(xf_uart_wait_event(packet_data.uart, &event, 0) == XF_OK);
        packet_handle_uart_event(event);
        assert(packet_data.fifo_rx_len == 0 && ports[1].rx_len == 0);
        notify_data();
        push(data, n);
        notify_data();
        assert(calibration_count == count + 1);
    }
    count = calibration_count;
    n = frame(data, 0);
    push(data, n);
    notify_data();
    n = frame(data, 1);
    push(data, n);
    notify_data();
    assert(calibration_count == count);
    n = frame(data, MCU_PROTOCOL_MAX_FIFO - MCU_PROTOCOL_MIN_CMD_LEN);
    push(noise, sizeof(noise));
    push(data, n);
    push(data, n);
    notify_data();
    assert(calibration_count == count + 2);
    /* 固定种子的不同长度帧、拆包、FIFO 回绕。 */
    srand(17);
    for (int i = 0; i < 500; i++)
    {
        count = calibration_count;
        n = frame(data, 2 + rand() % 2039);
        push(noise, sizeof(noise));
        size_t offset = 0;
        while (offset < n)
        {
            size_t chunk = 1 + rand() % 900;
            if (chunk > n - offset)
            {
                chunk = n - offset;
            }
            push(data + offset, chunk);
            notify_data();
            offset += chunk;
        }
        assert(calibration_count == count + 1);
        assert(packet_data.fifo_rx_len == 0);
    }
    assert(xf_uart_deinit(packet_data.uart) == XF_OK);
}

/** @brief 运行 UART 和产品接收流程回归测试。 */
int main(void)
{
    test_lifecycle();
    test_packet();
    puts("PASS: lifecycle, multi-port isolation, stream fragmentation/coalescing, bounds, error recovery");

    return 0;
}
