/**
 * @file    test_protocol.c
 * @brief   公共协议容量、帧边界和实例隔离回归测试
 *
 * @author  Rzon
 * @date    2026-09-15
 *
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xf_mcu_protocol.h"
#include "xf_utils.h"

/** @brief 检查失败路径没有写入输出数组。 */
static void check_untouched(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        assert(data[i] == 0xa5);
    }
}

/** @brief 检查命令表数量、重复项、非法描述与实例隔离。 */
static void test_commands(void)
{
    const xf_mcu_cmd_t first[] = {XF_MCU_CMD("first", 0x40, 0)};
    const xf_mcu_cmd_t second[] = {XF_MCU_CMD("second", 0x40, 2)};
    xf_mcu_protocol_t a;
    xf_mcu_protocol_t b;
    uint8_t output[16];
    int32_t length = -1;

    assert(xf_mcu_protocol_init(&a, first, 1) == 0);
    assert(xf_mcu_protocol_init(&b, second, 1) == 0);
    assert(xf_mcu_protocol_pack(&a, "first", NULL, 0, output, sizeof(output), &length, false) == 0);
    assert(xf_mcu_protocol_pack(&a, "second", NULL, 0, output, sizeof(output), &length, false) == -1);
    assert(xf_mcu_protocol_pack(&b, "first", NULL, 0, output, sizeof(output), &length, false) == -1);
    assert(xf_mcu_protocol_pack(&b, "second", NULL, 0, output, sizeof(output), &length, false) == -1);

    xf_mcu_cmd_t commands[MCU_PROTOCOL_MAX_PRIVATE_CMDS];
    for (int32_t i = 0; i < MCU_PROTOCOL_MAX_PRIVATE_CMDS; i++)
    {
        snprintf(commands[i].type, sizeof(commands[i].type), "cmd%d", (int)i);
        commands[i].dp_id = MCU_PROTOCOL_PRI_DP_START + i;
        commands[i].len = 0;
    }
    assert(xf_mcu_protocol_init(&b, commands, MCU_PROTOCOL_MAX_PRIVATE_CMDS) == 0);
    /* 193 项应在读取表内容前拒绝；实际数组只有 192 项。 */
    assert(xf_mcu_protocol_init(&a, commands, MCU_PROTOCOL_MAX_PRIVATE_CMDS + 1) == -1);
    assert(xf_mcu_protocol_init(&a, commands, -1) == -1);
    assert(xf_mcu_protocol_init(&a, NULL, 1) == -1);
    assert(xf_mcu_protocol_init(NULL, commands, 1) == -1);
    commands[1].dp_id = commands[0].dp_id;
    assert(xf_mcu_protocol_init(&a, commands, 2) == -1);
    commands[1].dp_id++;
    strcpy(commands[1].type, commands[0].type);
    assert(xf_mcu_protocol_init(&a, commands, 2) == -1);
    commands[0].dp_id = 0;
    assert(xf_mcu_protocol_init(&a, commands, 1) == -1);
    commands[0].dp_id = 0x40;
    strcpy(commands[0].type, "reboot");
    assert(xf_mcu_protocol_init(&a, commands, 1) == -1);
    memset(commands[0].type, 'x', sizeof(commands[0].type));
    assert(xf_mcu_protocol_init(&a, commands, 1) == -1);
    commands[0].type[0] = '\0';
    assert(xf_mcu_protocol_init(&a, commands, 1) == -1);
    strcpy(commands[0].type, "valid");
    commands[0].len = MCU_PROTOCOL_MAX_PAYLOAD + 1;
    assert(xf_mcu_protocol_init(&a, commands, 1) == -1);
    assert(a.private_commands == first && a.command_count == 1);
    assert(xf_mcu_protocol_init(&b, NULL, 0) == 0);
    assert(xf_mcu_protocol_pack(&b, "reboot", NULL, 0, output, sizeof(output), &length, false) == 0);
}

/** @brief 检查输出容量不足、截断帧、非法长度和最大负载。 */
static void test_codec(void)
{
    const xf_mcu_cmd_t commands[] = {XF_MCU_CMD("data", 0x40, 0)};
    const uint8_t payload[] = {1, 2};
    xf_mcu_protocol_t protocol;
    uint8_t frame[MCU_PROTOCOL_MAX_FIFO];
    uint8_t output[MCU_PROTOCOL_MAX_PAYLOAD];
    int32_t length = -1;
    int32_t parsed = -1;
    int32_t id = -1;

    assert(xf_crc16_calc((const uint8_t *)"123456789", 9) == 0x29b1);
    assert(xf_crc16_calc(NULL, 0) == 0xffff);
    assert(xf_mcu_protocol_init(&protocol, commands, 1) == 0);
    memset(frame, 0xa5, sizeof(frame));
    for (int32_t capacity = -1; capacity < 10; capacity++)
    {
        assert(xf_mcu_protocol_pack(&protocol, "data", payload, 2, frame, capacity, &length, false) == -1);
        assert(length == -1);
        check_untouched(frame, sizeof(frame));
    }
    assert(xf_mcu_protocol_pack(&protocol, "data", payload, -1, frame, sizeof(frame), &length, false) == -1);
    assert(xf_mcu_protocol_pack(&protocol, "data", payload, INT32_MAX,
                                frame, sizeof(frame), &length, false) == -1);
    assert(xf_mcu_protocol_pack(&protocol, "data", NULL, 2, frame, sizeof(frame), &length, false) == -1);
    assert(xf_mcu_protocol_pack(&protocol, "data", payload, 2, frame, sizeof(frame), &length, false) == 0);
    assert(length == 10);
    const uint8_t prefix[] = {0x55, 0xaa, 1, 0x40, 2, 0, 1, 2};
    assert(memcmp(frame, prefix, sizeof(prefix)) == 0);
    memset(output, 0xa5, sizeof(output));
    for (int32_t capacity = -1; capacity < 2; capacity++)
    {
        assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2,
                                     output, capacity, &parsed, &id) == -1);
        assert(parsed == -1 && id == -1);
        check_untouched(output, sizeof(output));
    }
    for (int32_t truncated = -1; truncated < length - 2; truncated++)
    {
        assert(xf_mcu_protocol_parse(&protocol, frame + 2, truncated,
                                     output, sizeof(output), &parsed, &id) == -1);
        check_untouched(output, sizeof(output));
    }
    frame[length - 1] ^= 1;
    assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2,
                                 output, sizeof(output), &parsed, &id) == -1);
    frame[length - 1] ^= 1;
    frame[2] = 3;
    assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2,
                                 output, sizeof(output), &parsed, &id) == -1);
    frame[2] = 1;
    assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2, output, 2, &parsed, &id) == 0);
    assert(parsed == 2 && id == 0x40 && memcmp(output, payload, 2) == 0);

    /* 遍历所有合法负载长度，同时覆盖设置帧和查询帧。 */
    uint8_t input[MCU_PROTOCOL_MAX_PAYLOAD];
    memset(input, 0x37, sizeof(input));
    for (int32_t size = 0; size <= MCU_PROTOCOL_MAX_PAYLOAD; size++)
    {
        assert(xf_mcu_protocol_pack(&protocol, "data", input, size,
                                    frame, sizeof(frame), &length, size % 2) == 0);
        assert(length == size + MCU_PROTOCOL_MIN_CMD_LEN);
        assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2,
                                     output, sizeof(output), &parsed, &id) == 0);
        assert(parsed == size && memcmp(input, output, size) == 0);
    }
    assert(xf_mcu_protocol_pack(&protocol, "reboot", NULL, 0, frame, sizeof(frame), &length, false) == 0);
    assert(xf_mcu_protocol_parse(&protocol, frame + 2, length - 2, NULL, 0, &parsed, &id) == 0);
    assert(parsed == 0 && id == MCU_DP_REBOOT);
}

/** @brief 检查环形缓冲回绕、只复制一帧以及失败时的容量保护。 */
static void test_ring(void)
{
    const xf_mcu_cmd_t commands[] = {XF_MCU_CMD("data", 0x40, 0)};
    const uint8_t payload[] = {1, 2};
    xf_mcu_protocol_t protocol;
    uint8_t frame[10];
    uint8_t ring[32];
    uint8_t output[16];
    int32_t length;
    int32_t pass = -1;

    assert(xf_mcu_protocol_init(&protocol, commands, 1) == 0);
    assert(xf_mcu_protocol_pack(&protocol, "data", payload, 2, frame, sizeof(frame), &length, false) == 0);
    for (int32_t start = 0; start < (int32_t)sizeof(ring); start++)
    {
        memset(ring, 0, sizeof(ring));
        for (int32_t i = 0; i < length * 2; i++)
        {
            ring[(start + i) % sizeof(ring)] = frame[i % length];
        }
        memset(output, 0xa5, sizeof(output));
        assert(xf_mcu_protocol_input_check(ring, sizeof(ring), start, 20, &pass, output, 7) == -1);
        assert(pass == 0);
        check_untouched(output, sizeof(output));
        assert(xf_mcu_protocol_input_check(ring, sizeof(ring), start, 20, &pass, output, 8) == 10);
        assert(pass == 8 && memcmp(output, frame + 2, 8) == 0);
        check_untouched(output + 8, sizeof(output) - 8);
        for (int32_t size = 0; size < length; size++)
        {
            assert(xf_mcu_protocol_input_check(ring, sizeof(ring), start, size,
                                               &pass, output, sizeof(output)) == 0);
            assert(pass == 0);
        }
    }
    assert(xf_mcu_protocol_input_check(ring, 0, 0, 0, &pass, output, sizeof(output)) == -1);
    assert(xf_mcu_protocol_input_check(ring, sizeof(ring), -1, 1, &pass, output, sizeof(output)) == -1);
    assert(xf_mcu_protocol_input_check(ring, sizeof(ring), 32, 1, &pass, output, sizeof(output)) == -1);
    assert(xf_mcu_protocol_input_check(ring, sizeof(ring), 0, 33, &pass, output, sizeof(output)) == -1);
    assert(xf_mcu_protocol_input_check(ring, sizeof(ring), 0, -1, &pass, output, sizeof(output)) == -1);
}

/** @brief 运行不依赖 SDK、产品配置或 FreeRTOS 的协议测试。 */
int main(void)
{
    test_commands();
    test_codec();
    test_ring();
    puts("PASS: protocol bounds, command tables, independent instances, ring buffer, CRC");

    return 0;
}
