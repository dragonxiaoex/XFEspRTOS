/**
 * @file    xf_packet_handle.c
 * @brief   XC200 串口协议接收与命令分发
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xf_packet_handle.h"
#include "xf_uart_bsp.h"
#include "xf_log.h"
#include "xf_task_def.h"
#include "xf_mcu_protocol.h"

#define LOCAL_TAG               "packet"
#define PACKET_UART_PORT        1
#define PACKET_RX_CHUNK_SIZE    512

/** @brief 由协议任务独占的接收状态。 */
typedef struct {
    TaskHandle_t packet_task;
    xf_uart_handle_t uart;
    xf_mcu_protocol_t protocol;
    uint8_t cmd_fifo[MCU_PROTOCOL_MAX_FIFO];
    int32_t fifo_read_index;
    int32_t fifo_rx_len;
} xf_packet_data_t;

static xf_packet_data_t packet_data = {0};
static const xf_mcu_cmd_t mcu_pri_cmd[] = {
    XF_MCU_CMD("eyeStatus", MCU_DP_EYE_STATUS, 0),
    XF_MCU_CMD("mode", MCU_DP_MODE, 0),
};

extern void eye_calib(uint8_t calib_area);
extern void eye_test_pos(uint8_t x, uint8_t y);

/**
 * @brief 切换显示模式
 *
 * @param [in] mode - 显示模式.
 * @param [in] area - 校准区域.
 */
static void packet_set_mode(uint8_t mode, uint8_t area)
{
    if (mode == MCU_DISPLAY_NORMAL_MODE) {
        LOG_I("change display mode to normal");

    } else if (mode == MCU_DISPLAY_CALIB_MODE) {
        LOG_I("change display mode to calib");
        eye_calib(area);

    } else {
        LOG_E("don't have %d mode!", mode);
    }
}

/**
 * @brief 解析并执行一条完整命令
 *
 * @param [in] cmd - 去掉帧头的命令数据.
 * @param [in] len - 命令字节数.
 */
static void packet_execute(const uint8_t *cmd, int32_t len)
{
    const xf_packet_data_t *packet = &packet_data;
    static uint8_t cmd_out[MCU_PROTOCOL_MAX_FIFO];
    int32_t out_len = 0;
    int32_t cmd_index = -1;

    if (xf_mcu_protocol_parse(&packet->protocol, cmd, len, cmd_out, sizeof(cmd_out),
                              &out_len, &cmd_index) < 0) {
        LOG_E("parse error");
        return;
    }

    if (((cmd_index == MCU_DP_EYE_STATUS) || (cmd_index == MCU_DP_MODE)) && (out_len < 2)) {
        LOG_E("cmd %" PRId32 " payload too short: %" PRId32, cmd_index, out_len);
        return;
    }

    switch (cmd_index) {
    case MCU_DP_REBOOT: {
        LOG_I("reboot------------");

        for (int32_t i = 0; i < out_len; i++) {
            printf("0x%02x ", cmd_out[i]);
        }

        break;
    }

    case MCU_DP_EYE_STATUS: {
        eye_test_pos(cmd_out[1], cmd_out[0] - 80);
        break;
    }

    case MCU_DP_MODE: {
        packet_set_mode(cmd_out[0], cmd_out[1]);
        break;
    }

    default: {
        LOG_I("unknown cmd: %" PRId32, cmd_index);
        break;
    }
    }
}

/**
 * @brief 将接收字节送入协议 FIFO，处理拆包和粘包
 *
 * @param [in] new_data - 本次读取的数据.
 * @param [in] new_data_len - 字节数，不超过协议 FIFO 剩余容量.
 */
static void packet_fifo_handle(const uint8_t *new_data, int32_t new_data_len)
{
    xf_packet_data_t *packet = &packet_data;
    static uint8_t check_out_buff[MCU_PROTOCOL_MAX_FIFO];
    int32_t write_index = 0;
    int32_t front_len = 0;
    int32_t dump_len = 0;
    int32_t pass = 0;

    write_index = (packet->fifo_read_index + packet->fifo_rx_len) % MCU_PROTOCOL_MAX_FIFO;
    front_len = MCU_PROTOCOL_MAX_FIFO - write_index;

    if (front_len > new_data_len) {
        front_len = new_data_len;
    }

    memcpy(&packet->cmd_fifo[write_index], new_data, front_len);
    memcpy(packet->cmd_fifo, &new_data[front_len], new_data_len - front_len);
    packet->fifo_rx_len += new_data_len;

    while (packet->fifo_rx_len >= MCU_PROTOCOL_MIN_CMD_LEN) {
        dump_len = xf_mcu_protocol_input_check(packet->cmd_fifo, MCU_PROTOCOL_MAX_FIFO,
                                               packet->fifo_read_index, packet->fifo_rx_len,
                                               &pass, check_out_buff, sizeof(check_out_buff));

        if (dump_len < 0) {
            packet->fifo_read_index = 0;
            packet->fifo_rx_len = 0;
            LOG_E("protocol input invalid, discard partial frame");
            return;
        }

        if (dump_len == 0) {
            break;
        }

        packet->fifo_read_index = (packet->fifo_read_index + dump_len) % MCU_PROTOCOL_MAX_FIFO;
        packet->fifo_rx_len -= dump_len;

        if (pass > 0) {
            packet_execute(check_out_buff, pass);
        }
    }
}

/** @brief 读取当前已缓冲的数据，不等待填满接收数组。 */
static void packet_read_data(void)
{
    xf_packet_data_t *packet = &packet_data;
    uint8_t data[PACKET_RX_CHUNK_SIZE];
    size_t pending = 0;

    if (xf_uart_get_rx_length(packet->uart, &pending) != XF_OK) {
        LOG_E("get uart buffered length failed");
        return;
    }

    /* 每次处理一个快照，持续输入时也能返回事件循环处理溢出等错误。 */
    while (pending > 0) {
        int32_t length = pending < sizeof(data) ? pending : sizeof(data);
        int32_t free_size = MCU_PROTOCOL_MAX_FIFO - packet->fifo_rx_len;

        if (length > free_size) {
            length = free_size;
        }

        int32_t received = xf_uart_read(packet->uart, data, length, 0);

        if (received <= 0) {
            break;
        }

        packet_fifo_handle(data, received);
        pending -= received;
    }
}

/**
 * @brief 处理驱动事件，串口错误后丢弃不完整的协议帧
 *
 * @param [in] event - UART 适配层事件.
 */
static void packet_handle_uart_event(xf_uart_event_t event)
{
    xf_packet_data_t *packet = &packet_data;

    switch (event) {
    case XF_UART_EVENT_DATA: {
        /* 事件只作唤醒用途，清空接收缓冲后遗留的旧事件也可安全处理。 */
        packet_read_data();
        break;
    }

    case XF_UART_EVENT_RX_OVERFLOW:
    case XF_UART_EVENT_PARITY_ERROR:
    case XF_UART_EVENT_FRAME_ERROR:
    case XF_UART_EVENT_BREAK: {
        xf_uart_flush_input(packet->uart);
        packet->fifo_read_index = 0;
        packet->fifo_rx_len = 0;
        LOG_E("uart rx error: %d, discard partial frame", event);
        break;
    }

    default: {
        break;
    }
    }
}

/**
 * @brief 等待并分发 UART 事件
 *
 * @param [in] args - FreeRTOS 任务参数，未使用.
 */
static void packet_handle_task(void *args)
{
    xf_packet_data_t *packet = &packet_data;
    xf_uart_event_t event;

    while (1) {
        if (xf_uart_wait_event(packet->uart, &event, portMAX_DELAY) == XF_OK) {
            packet_handle_uart_event(event);
        }
    }
}

xf_err_t xf_packet_handle_init(void)
{
    xf_packet_data_t *packet = &packet_data;
    const xf_uart_config_t uart_config = {
        .port = PACKET_UART_PORT,
        .baud_rate = 921600,
        .tx_pin = 4,
        .rx_pin = 5,
        .rx_buffer_size = 4096,
        .tx_buffer_size = 1024,
        .event_queue_size = 20,
    };
    xf_err_t ret = XF_OK;

    if (packet->packet_task != NULL) {
        return XF_ERR_INVALID_STATE;
    }

    if (xf_mcu_protocol_init(&packet->protocol, mcu_pri_cmd,
                             sizeof(mcu_pri_cmd) / sizeof(mcu_pri_cmd[0])) < 0) {
        return XF_ERR_INVALID_ARG;
    }

    ret = xf_uart_init(&uart_config, &packet->uart);

    if (ret != XF_OK) {
        return ret;
    }

    packet->fifo_read_index = 0;
    packet->fifo_rx_len = 0;

    if (xTaskCreate(packet_handle_task, XF_PACKET_TASK_NAME, XF_PACKET_TASK_STACKSIZE,
                    NULL, XF_PACKET_TASK_PRIO, &packet->packet_task) != pdPASS) {
        xf_uart_deinit(packet->uart);
        packet->uart = NULL;
        packet->packet_task = NULL;
        return XF_ERR_NO_MEM;
    }

    LOG_I("packet init succ");

    return XF_OK;
}
