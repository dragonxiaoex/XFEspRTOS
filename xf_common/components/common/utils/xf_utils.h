/**
 * @file    xf_utils.h
 * @brief   平台无关的校验工具
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_UTILS_H__
#define __XF_UTILS_H__

#include <stdint.h>

/**
 * @brief 计算 CRC16-CCITT，初值 0xFFFF，多项式 0x1021
 *
 * @param [in] data - 有效数据数组；长度为 0 时可为 NULL.
 * @param [in] len - 数据字节数.
 * @return CRC16 校验值.
 */
uint16_t xf_crc16_calc(const uint8_t *data, uint16_t len);

#endif /* __XF_UTILS_H__ */
