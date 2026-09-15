/**
 * @file    xf_uart_bsp.h
 * @brief   UART 适配接口，应用不依赖厂商 SDK
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_UART_BSP_H__
#define __XF_UART_BSP_H__

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "xf_error.h"

/** @brief 初始化时保持原有引脚映射。 */
#define XF_UART_PIN_NO_CHANGE (-1)

/** @brief 由适配实现拥有的串口句柄，释放后失效。 */
typedef struct xf_uart *xf_uart_handle_t;

/** @brief 普通 UART 配置，固定使用 8N1、无硬件流控。 */
typedef struct {
    int32_t port;             /**< 板级串口编号；ESP-IDF 实现对应普通 UART 编号，不包含 LP UART. */
    int32_t baud_rate;        /**< 实际波特率，例如 115200、921600. */
    int32_t tx_pin;           /**< 板级 TX 引脚编号，或 XF_UART_PIN_NO_CHANGE. */
    int32_t rx_pin;           /**< 板级 RX 引脚编号，或 XF_UART_PIN_NO_CHANGE. */
    int32_t rx_buffer_size;   /**< 接收缓冲容量，单位字节，需满足底层驱动的最小容量. */
    int32_t tx_buffer_size;   /**< 发送缓冲容量，单位字节；0 表示不用软件发送缓冲. */
    int32_t event_queue_size; /**< 接收通知队列长度；0 表示仅主动读取、不使用事件等待. */
} xf_uart_config_t;

/** @brief 串口接收通知；DATA 仅用于唤醒，不表示协议帧边界。 */
typedef enum {
    XF_UART_EVENT_DATA,          /**< 接收缓冲区有数据可读. */
    XF_UART_EVENT_RX_OVERFLOW,   /**< 硬件或软件接收缓冲溢出. */
    XF_UART_EVENT_PARITY_ERROR,  /**< 校验错误. */
    XF_UART_EVENT_FRAME_ERROR,   /**< 帧错误. */
    XF_UART_EVENT_BREAK,         /**< 检测到线路 BREAK. */
    XF_UART_EVENT_OTHER,         /**< 当前应用不处理的其他事件. */
} xf_uart_event_t;

/**
 * @brief 初始化串口，创建驱动缓冲和可选通知队列
 *
 * 仅在任务中调用；同一串口的初始化和释放由调用者串行执行。
 * 配置在返回后可释放，失败会回收本次创建的资源。
 *
 * @param [in] config - 板级串口配置.
 * @param [out] handle - 成功时写入句柄，失败时不修改.
 * @return XF_OK 表示成功，其他值表示参数、状态或底层错误.
 */
xf_err_t xf_uart_init(const xf_uart_config_t *config, xf_uart_handle_t *handle);

/**
 * @brief 复制接收缓冲中的数据到调用者数组
 *
 * 每个串口由一个接收任务读取；读取长度不代表帧边界。
 * 可能继续等待后续字节，事件处理时传 0 可立即返回当前数据。
 *
 * @param [in] handle - 有效串口句柄.
 * @param [out] data - 接收数组.
 * @param [in] length - 数组容量，单位字节，必须大于 0.
 * @param [in] ticks_to_wait - FreeRTOS 等待 tick 数.
 * @return 实际字节数，0 表示没有数据，-1 表示失败.
 */
int32_t xf_uart_read(xf_uart_handle_t handle, void *data, int32_t length, TickType_t ticks_to_wait);

/**
 * @brief 提交发送数据，必要时阻塞等待空间
 *
 * 仅在任务中调用。返回后可复用发送数组，但线路可能尚未发送完毕。
 *
 * @param [in] handle - 有效串口句柄.
 * @param [in] data - 发送数组.
 * @param [in] length - 数据字节数，必须大于 0.
 * @return 已接受的字节数，-1 表示失败.
 */
int32_t xf_uart_write(xf_uart_handle_t handle, const void *data, int32_t length);

/**
 * @brief 等待并取出一个串口通知，不创建额外任务
 *
 * 初始化时 event_queue_size 必须大于 0；由接收任务独占消费。
 *
 * @param [in] handle - 有效串口句柄.
 * @param [out] event - 成功时写入自有事件类型，失败时不修改.
 * @param [in] ticks_to_wait - FreeRTOS 等待 tick 数.
 * @return XF_OK 表示收到通知，XF_ERR_TIMEOUT 表示没有通知，其他值表示失败.
 */
xf_err_t xf_uart_wait_event(xf_uart_handle_t handle, xf_uart_event_t *event, TickType_t ticks_to_wait);

/**
 * @brief 查询当前接收缓冲中的字节数
 *
 * @param [in] handle - 有效串口句柄.
 * @param [out] length - 当前字节数，返回后可能继续增加.
 * @return XF_OK 表示成功，其他值表示失败.
 */
xf_err_t xf_uart_get_rx_length(xf_uart_handle_t handle, size_t *length);

/**
 * @brief 丢弃当前接收数据，由接收任务在错误恢复时调用
 *
 * @param [in] handle - 有效串口句柄.
 * @return XF_OK 表示成功，其他值表示失败.
 */
xf_err_t xf_uart_flush_input(xf_uart_handle_t handle);

/**
 * @brief 等待线路发送完成
 *
 * @param [in] handle - 有效串口句柄.
 * @param [in] ticks_to_wait - FreeRTOS 等待 tick 数.
 * @return XF_OK 表示发送完成，XF_ERR_TIMEOUT 表示超时，其他值表示失败.
 */
xf_err_t xf_uart_wait_tx_done(xf_uart_handle_t handle, TickType_t ticks_to_wait);

/**
 * @brief 释放串口及其缓冲和事件队列
 *
 * 调用前先停止该串口的收发和事件消费，必要时等待发送完成。
 * 成功后句柄失效，由调用者清空，不能继续使用或重复释放。
 *
 * @param [in] handle - 有效串口句柄.
 * @return XF_OK 表示成功，其他值表示失败，失败时句柄仍由调用者持有.
 */
xf_err_t xf_uart_deinit(xf_uart_handle_t handle);

#endif /* __XF_UART_BSP_H__ */
