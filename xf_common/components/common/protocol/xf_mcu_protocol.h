/**
 * @file    xf_mcu_protocol.h
 * @brief   独立于平台和传输方式的 MCU 协议编解码
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#ifndef __XF_MCU_PROTOCOL_H__
#define __XF_MCU_PROTOCOL_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief 协议容量与帧格式。 */
#define MCU_PROTOCOL_MAX_DP_NUM       256
#define MCU_PROTOCOL_PRI_DP_START     0x40
#define MCU_PROTOCOL_MAX_PRIVATE_CMDS (MCU_PROTOCOL_MAX_DP_NUM - MCU_PROTOCOL_PRI_DP_START)
#define MCU_PROTOCOL_CMD_NAME_SIZE    16
#define MCU_PROTOCOL_MAX_FIFO         2048
#define MCU_PROTOCOL_MIN_CMD_LEN      8
#define MCU_PROTOCOL_MAX_PAYLOAD      (MCU_PROTOCOL_MAX_FIFO - MCU_PROTOCOL_MIN_CMD_LEN)

/** @brief 去掉 0x55、0xAA 帧头后，各字段的偏移。 */
#define MCU_FUNCTION_OFFSET           0
#define MCU_DP_ID_OFFSET              1
#define MCU_LEN_OFFSET                2
#define MCU_DATA_OFFSET               4

/** @brief 公共命令编号；协议模块不执行命令对应的业务。 */
typedef enum {
    MCU_DP_REBOOT = 0x00, /**< 重启命令. */
} xf_mcu_common_dp_id_t;

/** @brief 命令描述。 */
typedef struct {
    char type[MCU_PROTOCOL_CMD_NAME_SIZE]; /**< 命令名称，必须以零结尾且非空. */
    uint8_t dp_id;                        /**< 私有命令编号范围为 0x40～0xFF. */
    uint16_t len;                         /**< 负载长度；0 表示可变长度，非零表示固定长度. */
} xf_mcu_cmd_t;

/** @brief 初始化一项命令描述。 */
#define XF_MCU_CMD(mcu_type, mcu_dp_id, cmd_len) \
    { .type = mcu_type, .dp_id = mcu_dp_id, .len = cmd_len }

/** @brief 独立命令表上下文，由初始化函数设置，使用期间不直接修改成员。 */
typedef struct {
    const xf_mcu_cmd_t *private_commands; /**< 借用的只读命令表. */
    int32_t command_count;                /**< 私有命令数量. */
} xf_mcu_protocol_t;

/**
 * @brief 绑定一个实例的私有命令表
 *
 * 不分配内存，不复制命令表；调用者需保证表在上下文使用期间有效且不变。
 * 初始化或更换命令表时，不与该上下文的编解码并发；只读编解码可使用独立缓冲并发调用。
 *
 * @param [out] protocol - 协议上下文，失败时不修改.
 * @param [in] commands - 私有命令表，数量为 0 时可为 NULL.
 * @param [in] count - 私有命令数量，范围 0～MCU_PROTOCOL_MAX_PRIVATE_CMDS.
 * @return 0 表示成功，-1 表示数量、命令编号、名称或长度无效.
 */
int32_t xf_mcu_protocol_init(xf_mcu_protocol_t *protocol, const xf_mcu_cmd_t *commands, int32_t count);

/**
 * @brief 从环形缓冲查找并复制第一条完整命令
 *
 * 只复制一条命令，不执行命令。不完整帧保留等待后续数据，坏帧丢弃帧头后重新同步。
 * buff 与 cmd 不得重叠；失败时不消费输入，调用者需处理错误，不能原样无限重试。
 *
 * @param [in] buff - 环形缓冲数组.
 * @param [in] buff_len - 数组容量，范围 1～MCU_PROTOCOL_MAX_FIFO.
 * @param [in] index - 有效数据起点，必须在数组内.
 * @param [in] valid_len - 有效字节数，不超过 buff_len.
 * @param [out] pass - 成功帧去掉两字节帧头后的长度；其余情况为 0.
 * @param [out] cmd - 命令输出数组，内容仅在 pass 大于 0 时有效.
 * @param [in] cmd_capacity - 命令输出数组容量.
 * @return 可消费字节数，0 表示等待更多数据，-1 表示参数无效或输出容量不足.
 */
int32_t xf_mcu_protocol_input_check(const uint8_t *buff, int32_t buff_len, int32_t index,
                                    int32_t valid_len, int32_t *pass, uint8_t *cmd, int32_t cmd_capacity);

/**
 * @brief 校验命令并复制负载
 *
 * input_cmd 为 input_check 输出的命令，不包含两字节帧头。
 * 输入、输出数组不得重叠；失败时不修改输出数组和输出参数。
 *
 * @param [in] protocol - 已初始化的协议上下文.
 * @param [in] input_cmd - 完整命令数据.
 * @param [in] input_len - 命令字节数.
 * @param [out] out_cmd - 负载数组，容量为 0 时可为 NULL.
 * @param [in] out_capacity - 负载数组容量.
 * @param [out] out_len - 实际负载字节数.
 * @param [out] cmd_id - 协议命令编号.
 * @return 0 表示成功，-1 表示帧无效、命令未知或容量不足.
 */
int32_t xf_mcu_protocol_parse(const xf_mcu_protocol_t *protocol, const uint8_t *input_cmd,
                              int32_t input_len, uint8_t *out_cmd, int32_t out_capacity,
                              int32_t *out_len, int32_t *cmd_id);

/**
 * @brief 按命令名称生成完整协议帧
 *
 * 输入、输出数组不得重叠；失败时不修改输出数组和输出长度。
 *
 * @param [in] protocol - 已初始化的协议上下文.
 * @param [in] type - 以零结尾的命令名称.
 * @param [in] input_cmd - 负载数组，长度为 0 时可为 NULL.
 * @param [in] input_len - 负载字节数.
 * @param [out] out_cmd - 完整帧输出数组.
 * @param [in] out_capacity - 输出数组容量，需容纳负载和 8 字节协议开销.
 * @param [out] out_len - 实际帧字节数.
 * @param [in] detect - true 生成查询帧，false 生成设置帧.
 * @return 0 表示成功，-1 表示参数、命令或容量无效.
 */
int32_t xf_mcu_protocol_pack(const xf_mcu_protocol_t *protocol, const char *type,
                             const uint8_t *input_cmd, int32_t input_len, uint8_t *out_cmd,
                             int32_t out_capacity, int32_t *out_len, bool detect);

#endif /* __XF_MCU_PROTOCOL_H__ */
