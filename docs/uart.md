# UART 模块

## 设计

`xf_uart_bsp.h` 提供自有适配接口，`xf_uart_bsp.c` 当前使用 ESP-IDF 普通 UART 驱动。SDK 负责中断、收发环形缓冲和事件队列，厂商类型只出现在实现中。

接收路径：UART → SDK 接收缓冲 → UART 适配接口 → 产品接收任务 → 协议解析。

- 适配层不创建额外任务，不维护 DMA 双缓冲，也不向应用传递中断缓冲区指针。
- 产品决定串口编号、波特率、GPIO、缓冲容量及收到数据后的操作。
- 固定使用 8N1、无硬件流控，支持芯片的普通 UART，暂不包含 LP UART。
- FreeRTOS 直接使用，等待参数单位为 tick，可用 `pdMS_TO_TICKS()` 转换。
- 日志继续使用现有 `printf` 控制台，不经过本模块。

## 接口

声明位于 [xf_uart_bsp.h](../xf_common/components/common/bsp/xf_uart_bsp.h)，实现位于 [xf_uart_bsp.c](../xf_common/components/common/bsp/xf_uart_bsp.c)。状态返回 `XF_OK` 或 [xf_error.h](../xf_common/components/common/utils/xf_error.h) 中的自有错误码。

| 接口 | 用途 |
| --- | --- |
| `xf_uart_init(&config, &uart)` | 创建串口实例、驱动缓冲及可选事件队列 |
| `xf_uart_read(uart, data, capacity, ticks)` | 复制接收数据；返回字节数，0 表示没有数据，-1 表示失败 |
| `xf_uart_write(uart, data, length)` | 提交发送数据；返回已接受字节数，-1 表示失败 |
| `xf_uart_wait_event(uart, &event, ticks)` | 等待一个自有事件，无事件时返回 `XF_ERR_TIMEOUT` |
| `xf_uart_get_rx_length(uart, &length)` | 查询当前接收缓冲字节数 |
| `xf_uart_flush_input(uart)` | 丢弃当前接收数据 |
| `xf_uart_wait_tx_done(uart, ticks)` | 等待最后一个字节发送完毕 |
| `xf_uart_deinit(uart)` | 释放实例、缓冲和事件队列 |

初始化使用板级串口编号，例如 ESP-IDF 的 UART1 对应整数 `1`；后续调用均使用不透明句柄。应用不持有 SDK 队列、不直接调用 `uart_*()` 函数。

## 产品接入

XC200 的完整接入位于 [xf_packet_handle.c](../project/xc200/src/xf_packet_handle.c)。串口配置集中在 `xf_packet_handle_init()`：

```c
const xf_uart_config_t uart_config = {
    .port = 1,
    .baud_rate = 921600,
    .tx_pin = 4,
    .rx_pin = 5,
    .rx_buffer_size = 4096,
    .tx_buffer_size = 1024,
    .event_queue_size = 20,
};
```

GPIO 按产品连线配置。`XF_UART_PIN_NO_CHANGE` 表示保持当前引脚映射，不能理解为自动挑选一个空闲 GPIO。

初始化先建立驱动，再创建产品协议任务。驱动配置失败会释放本次创建的资源；任务创建失败也会释放串口并返回错误，允许重新尝试。重复初始化返回 `XF_ERR_INVALID_STATE`。

**当前 `private_device.c` 中的 `xf_packet_handle_init()` 仍处于注释状态。** 若启用，可在产品启动流程中调用并检查错误：

```c
xf_err_t ret = xf_packet_handle_init();
if (ret != XF_OK)
{
    LOG_E("packet init failed: %d", ret);
    return;
}
```

XC100 仍运行 Hello World，没有额外启动业务串口。

## 接收与错误恢复

`XF_UART_EVENT_DATA` 只表示有数据可读，不表示完整协议帧。事件中不提供厂商的队列项或数据指针。

XC200 收到事件后查询当前接收长度，分批非阻塞读取到自己的 512 字节数组，再交给协议解析器。每次处理一个长度快照，持续输入时也能回到事件循环处理错误；读取量同时受协议 FIFO 剩余容量限制。

硬件 FIFO 溢出与 SDK 缓冲满统一映射为 `XF_UART_EVENT_RX_OVERFLOW`。遇到溢出、校验错误、帧错误或 BREAK，产品清空接收数据并丢弃未完成帧，等待重新同步。事件队列不重置，遗留的数据通知通过非阻塞读取自然消耗。

协议处理帧头跨读取、连续 `0x55`、拆包、粘包、CRC 和长度错误；最大帧为 2048 字节。眼睛位置与模式命令需要至少两个负载字节，长度不足不执行。协议上下文和容量参数详见 [公共框架分层说明](component-layering-review.md)。

主动轮询接收时可将 `event_queue_size` 设为 `0`，初始化仍传入 `&uart` 接收句柄，然后直接 `xf_uart_read()`。此模式调用事件等待返回 `XF_ERR_INVALID_STATE`。带超时的读取可能继续等待后续字节，不能把一次读取返回当成帧边界。

## 发送与生命周期

- 接口在任务上下文调用，每个串口由一个任务读取和消费事件。
- 发送可能阻塞等待驱动缓冲空间。返回后可复用发送数组；需要确认线路发送完毕时调用 `xf_uart_wait_tx_done()`。
- 一帧尽量通过一次发送提交；多次调用拼帧时由业务协调不同发送者。
- 同一串口初始化、释放需串行执行，不与收发并发。
- 释放前先停止该串口的接收任务和发送者，必要时等待发送完成，再调用 `xf_uart_deinit()`。
- 初始化失败不修改输出句柄。反初始化成功后句柄失效，调用者应置空，不能继续使用或再次释放。
- 驱动缓冲和内部事件队列由适配实现管理，产品不能自行删除或绕过适配层操作这些资源。

## 验证

```sh
python3 tests/common/run_tests.py
python3 xf_tool/build.py MODEL=xc100
python3 xf_tool/build.py MODEL=xc200
```

两个产品已构建通过。主机测试覆盖初始化各阶段失败、任务创建失败后的重试、多串口隔离、事件和错误码转换、拆包、粘包、FIFO 回绕、最大帧、错误 CRC 和串口错误后的恢复，并开启地址与未定义行为检查。主机替身不模拟实际中断时序，详见 [测试说明](../tests/common/README.md)。

尚未进行板上持续收发测试，921600 波特率下与 LCD 刷新、Flash 写入同时运行的表现需要实测。当前溢出策略是丢弃数据后重新同步。
