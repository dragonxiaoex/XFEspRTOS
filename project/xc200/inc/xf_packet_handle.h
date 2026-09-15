/**
 * @file    xf_packet_handle.h
 * @brief   XC200 串口协议入口
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_PACKET_HANDLE_H__
#define __XF_PACKET_HANDLE_H__

#include "xf_error.h"

/** @brief 产品私有命令。 */
typedef enum {
    MCU_DP_EYE_STATUS = 0x40,    /**< 眼睛位置. */
    MCU_DP_MODE = 0x41,         /**< 显示模式. */
} xf_mcu_pri_dp_id_t;

/** @brief 显示工作模式。 */
typedef enum {
    MCU_DISPLAY_NORMAL_MODE = 0,    /**< 正常显示. */
    MCU_DISPLAY_CALIB_MODE,         /**< 校准模式. */
} xf_display_mode_t;

/**
 * @brief 初始化 UART1 并启动协议任务
 *
 * 在产品启动时调用一次；初始化失败会释放本次创建的串口资源，可重试。
 *
 * @return XF_OK 表示成功，XF_ERR_INVALID_STATE 表示已启动，其他值表示初始化错误.
 */
xf_err_t xf_packet_handle_init(void);

#endif /* __XF_PACKET_HANDLE_H__ */
