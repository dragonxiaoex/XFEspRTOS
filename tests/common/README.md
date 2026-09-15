# 公共框架主机测试

在仓库根目录执行：

```sh
python3 tests/common/run_tests.py
```

需要 Python 3 和支持 AddressSanitizer / UndefinedBehaviorSanitizer 的 Linux C 编译器，默认使用 `cc`，可通过 `CC` 指定。编译产物写入临时目录，运行结束自动清理，不修改平台 CMake 或固件配置。

| 测试 | 实际使用的生产代码 | 主要覆盖 |
| --- | --- | --- |
| `test_protocol.c` | 公共协议和 CRC | 容量不足不写出、截断帧、功能码和 CRC、全部合法负载长度、192/193 项命令表边界、重复/非法命令、实例隔离、环形缓冲回绕和单帧复制 |
| `test_uart.c` | UART 适配实现、XC200 收包、公共协议和 CRC | 初始化失败回滚、重复初始化、删除失败、错误映射、事件转换、轮询模式、多串口隔离、任务创建失败、拆包粘包、短命令、最大帧、错误后恢复、固定种子 500 组变长分片输入 |
| 脚本中的公开头与日志检查 | UART/协议/日志公开头 | 仅提供 FreeRTOS 类型和日志配置，不提供 SDK/产品头也能编译；日志关闭时不计算参数 |

`include/` 只为主机测试提供最小 SDK / FreeRTOS 替身，不属于产品构建。收包测试直接包含产品 `.c`，用于验证其静态接收路径，不为测试增加生产 API。

使用 `-Wall -Wextra -Werror`、AddressSanitizer、UndefinedBehaviorSanitizer；模拟外部 SDK / FreeRTOS 固定签名时保留未使用参数。受限主机不支持 LeakSanitizer，因此脚本关闭泄漏检测，资源回滚另外检查模拟驱动状态。

这些测试不运行真正的 FreeRTOS 任务、不模拟硬件中断或验证串口时序。两个芯片的固件构建及板上测试仍分别验证平台兼容性和实际运行行为。
