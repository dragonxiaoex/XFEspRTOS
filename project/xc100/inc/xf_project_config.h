/**
 * @file    xf_project_config.h
 * @brief   XC100 产品配置
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_PROJECT_CONFIG_H__
#define __XF_PROJECT_CONFIG_H__

/** @brief 产品使用的 ESP-IDF 芯片标识。 */
#define XF_USE_CHIP_ID                          "esp32p4"

/** @brief 产品软件版本。 */
#define XF_SOFTWARE_VERSION_MAJOR               1
#define XF_SOFTWARE_VERSION_MINOR               0
#define XF_SOFTWARE_VERSION_PATCH               0
#define XF_SOFTWARE_VERSION_STRING              "1.0.0"

/** @brief XF 日志开关：1 开启，0 关闭。 */
#define XF_LOG_ENABLE                           1

#endif /* __XF_PROJECT_CONFIG_H__ */
