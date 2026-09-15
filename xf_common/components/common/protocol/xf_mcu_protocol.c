/**
 * @file    xf_mcu_protocol.c
 * @brief   MCU 协议帧校验与编解码
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#include <string.h>

#include "xf_mcu_protocol.h"
#include "xf_utils.h"

#define SEND_FUNCTION   0x01
#define DETECT_FUNCTION 0x02
#define FRAME_HEADER_0  0x55
#define FRAME_HEADER_1  0xAA
#define FRAME_HEADER_SIZE 2
#define COMMAND_OVERHEAD (MCU_PROTOCOL_MIN_CMD_LEN - FRAME_HEADER_SIZE)

static const xf_mcu_cmd_t reboot_command = XF_MCU_CMD("reboot", MCU_DP_REBOOT, 0);

/**
 * @brief 按偏移读取环形缓冲中的一个字节
 *
 * @param [in] buff - 环形缓冲.
 * @param [in] capacity - 缓冲容量.
 * @param [in] index - 起始位置.
 * @param [in] offset - 相对起始位置的偏移.
 * @return 对应字节.
 */
static uint8_t ring_byte(const uint8_t *buff, int32_t capacity, int32_t index, int32_t offset)
{
    return buff[(index + offset) % capacity];
}

/**
 * @brief 校验去掉帧头后的命令长度、功能码和 CRC
 *
 * @param [in] cmd - 非空命令数组.
 * @param [in] length - 命令长度.
 * @return true 表示完整且校验通过.
 */
static bool command_is_valid(const uint8_t *cmd, int32_t length)
{
    if ((length < COMMAND_OVERHEAD) || (length > MCU_PROTOCOL_MAX_FIFO - FRAME_HEADER_SIZE)) {
        return false;
    }

    if ((cmd[MCU_FUNCTION_OFFSET] != SEND_FUNCTION) && (cmd[MCU_FUNCTION_OFFSET] != DETECT_FUNCTION)) {
        return false;
    }

    uint16_t payload_len = cmd[MCU_LEN_OFFSET] | ((uint16_t)cmd[MCU_LEN_OFFSET + 1] << 8);

    if (payload_len + COMMAND_OVERHEAD != length) {
        return false;
    }

    uint16_t crc = cmd[length - 2] | ((uint16_t)cmd[length - 1] << 8);

    return crc == xf_crc16_calc(&cmd[MCU_DP_ID_OFFSET], payload_len + 3);
}

/**
 * @brief 按编号查询已初始化上下文中的命令
 *
 * @param [in] protocol - 协议上下文.
 * @param [in] id - 命令编号.
 * @return 命令描述，未找到时为 NULL.
 */
static const xf_mcu_cmd_t *command_by_id(const xf_mcu_protocol_t *protocol, uint8_t id)
{
    if (id == reboot_command.dp_id) {
        return &reboot_command;
    }

    for (int32_t i = 0; i < protocol->command_count; i++) {
        if (protocol->private_commands[i].dp_id == id) {
            return &protocol->private_commands[i];
        }
    }

    return NULL;
}

/**
 * @brief 按名称查询已初始化上下文中的命令
 *
 * @param [in] protocol - 协议上下文.
 * @param [in] type - 命令名称.
 * @return 命令描述，未找到时为 NULL.
 */
static const xf_mcu_cmd_t *command_by_name(const xf_mcu_protocol_t *protocol, const char *type)
{
    if (strcmp(reboot_command.type, type) == 0) {
        return &reboot_command;
    }

    for (int32_t i = 0; i < protocol->command_count; i++) {
        if (strcmp(protocol->private_commands[i].type, type) == 0) {
            return &protocol->private_commands[i];
        }
    }

    return NULL;
}

int32_t xf_mcu_protocol_init(xf_mcu_protocol_t *protocol, const xf_mcu_cmd_t *commands, int32_t count)
{
    if ((protocol == NULL) || (count < 0) || (count > MCU_PROTOCOL_MAX_PRIVATE_CMDS)
            || ((count > 0) && (commands == NULL))) {
        return -1;
    }

    for (int32_t i = 0; i < count; i++) {
        const xf_mcu_cmd_t *command = &commands[i];

        if ((command->dp_id < MCU_PROTOCOL_PRI_DP_START) || (command->len > MCU_PROTOCOL_MAX_PAYLOAD)
                || (command->type[0] == '\0') || (memchr(command->type, '\0', sizeof(command->type)) == NULL)) {
            return -1;
        }

        if (strcmp(command->type, reboot_command.type) == 0) {
            return -1;
        }

        for (int32_t j = 0; j < i; j++) {
            if ((commands[j].dp_id == command->dp_id) || (strcmp(commands[j].type, command->type) == 0)) {
                return -1;
            }
        }
    }

    protocol->private_commands = commands;
    protocol->command_count = count;

    return 0;
}

int32_t xf_mcu_protocol_input_check(const uint8_t *buff, int32_t buff_len, int32_t index,
                                    int32_t valid_len, int32_t *pass, uint8_t *cmd, int32_t cmd_capacity)
{
    int32_t header_offset = 0;

    if (pass == NULL) {
        return -1;
    }

    *pass = 0;

    if ((buff == NULL) || (cmd == NULL) || (buff_len <= 0) || (buff_len > MCU_PROTOCOL_MAX_FIFO)
            || (index < 0) || (index >= buff_len) || (valid_len < 0) || (valid_len > buff_len)
            || (cmd_capacity < 0)) {
        return -1;
    }

    if (valid_len < FRAME_HEADER_SIZE) {
        return 0;
    }

    while (header_offset + 1 < valid_len) {
        if ((ring_byte(buff, buff_len, index, header_offset) == FRAME_HEADER_0)
                && (ring_byte(buff, buff_len, index, header_offset + 1) == FRAME_HEADER_1)) {
            break;
        }

        header_offset++;
    }

    if (header_offset + 1 >= valid_len) {
        /* 末尾的 0x55 可能是下一帧的开头。 */
        return valid_len - (ring_byte(buff, buff_len, index, valid_len - 1) == FRAME_HEADER_0 ? 1 : 0);
    }

    if (valid_len - header_offset < MCU_PROTOCOL_MIN_CMD_LEN) {
        return header_offset;
    }

    uint16_t payload_len = ring_byte(buff, buff_len, index, header_offset + 4)
                           | ((uint16_t)ring_byte(buff, buff_len, index, header_offset + 5) << 8);
    int32_t frame_len = payload_len + MCU_PROTOCOL_MIN_CMD_LEN;

    if (frame_len > buff_len) {
        return header_offset + FRAME_HEADER_SIZE;
    }

    if (valid_len - header_offset < frame_len) {
        return header_offset;
    }

    int32_t command_len = frame_len - FRAME_HEADER_SIZE;

    if (cmd_capacity < command_len) {
        return -1;
    }

    int32_t start = (index + header_offset + FRAME_HEADER_SIZE) % buff_len;
    int32_t front_len = buff_len - start;

    if (front_len > command_len) {
        front_len = command_len;
    }

    memcpy(cmd, &buff[start], front_len);
    memcpy(&cmd[front_len], buff, command_len - front_len);

    if (!command_is_valid(cmd, command_len)) {
        return header_offset + FRAME_HEADER_SIZE;
    }

    *pass = command_len;

    return header_offset + frame_len;
}

int32_t xf_mcu_protocol_parse(const xf_mcu_protocol_t *protocol, const uint8_t *input_cmd,
                              int32_t input_len, uint8_t *out_cmd, int32_t out_capacity,
                              int32_t *out_len, int32_t *cmd_id)
{
    if ((protocol == NULL) || (input_cmd == NULL) || (out_len == NULL) || (cmd_id == NULL)
            || (out_capacity < 0) || ((out_capacity > 0) && (out_cmd == NULL))) {
        return -1;
    }

    if (!command_is_valid(input_cmd, input_len)) {
        return -1;
    }

    int32_t payload_len = input_len - COMMAND_OVERHEAD;
    const xf_mcu_cmd_t *command = command_by_id(protocol, input_cmd[MCU_DP_ID_OFFSET]);

    if ((command == NULL) || (payload_len > out_capacity)) {
        return -1;
    }

    if ((command->len > 0) && (payload_len != command->len)) {
        return -1;
    }

    if (payload_len > 0) {
        memcpy(out_cmd, &input_cmd[MCU_DATA_OFFSET], payload_len);
    }

    *out_len = payload_len;
    *cmd_id = command->dp_id;

    return 0;
}

int32_t xf_mcu_protocol_pack(const xf_mcu_protocol_t *protocol, const char *type,
                             const uint8_t *input_cmd, int32_t input_len, uint8_t *out_cmd,
                             int32_t out_capacity, int32_t *out_len, bool detect)
{
    if ((protocol == NULL) || (type == NULL) || (out_cmd == NULL) || (out_len == NULL)
            || (input_len < 0) || (input_len > MCU_PROTOCOL_MAX_PAYLOAD)
            || ((input_len > 0) && (input_cmd == NULL))) {
        return -1;
    }

    if (out_capacity < input_len + MCU_PROTOCOL_MIN_CMD_LEN) {
        return -1;
    }

    const xf_mcu_cmd_t *command = command_by_name(protocol, type);

    if (command == NULL) {
        return -1;
    }

    if ((command->len > 0) && (input_len != command->len)) {
        return -1;
    }

    out_cmd[0] = FRAME_HEADER_0;
    out_cmd[1] = FRAME_HEADER_1;
    out_cmd[2] = detect ? DETECT_FUNCTION : SEND_FUNCTION;
    out_cmd[3] = command->dp_id;
    out_cmd[4] = (uint16_t)input_len & 0xff;
    out_cmd[5] = (uint16_t)input_len >> 8;

    if (input_len > 0) {
        memcpy(&out_cmd[6], input_cmd, input_len);
    }

    uint16_t crc = xf_crc16_calc(&out_cmd[3], input_len + 3);
    out_cmd[input_len + 6] = crc & 0xff;
    out_cmd[input_len + 7] = crc >> 8;
    *out_len = input_len + MCU_PROTOCOL_MIN_CMD_LEN;

    return 0;
}
