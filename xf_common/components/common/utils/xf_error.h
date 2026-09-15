/**
 * @file    xf_error.h
 * @brief   公共适配接口状态码
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_ERROR_H__
#define __XF_ERROR_H__

/** @brief 与厂商 SDK 无关的操作状态；收发接口另行返回字节数或 -1。 */
typedef enum {
    XF_OK = 0,                /**< 操作成功. */
    XF_ERR_INVALID_ARG,       /**< 参数无效. */
    XF_ERR_INVALID_STATE,     /**< 当前状态不允许操作. */
    XF_ERR_NO_MEM,            /**< 内存不足. */
    XF_ERR_TIMEOUT,           /**< 等待超时. */
    XF_ERR_NOT_SUPPORTED,     /**< 平台不支持. */
    XF_ERR_IO,                /**< 底层操作失败. */
} xf_err_t;

#endif /* __XF_ERROR_H__ */
