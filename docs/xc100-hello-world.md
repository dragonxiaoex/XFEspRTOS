# XC100 Hello World

本示例面向官方 ESP32-P4-Function-EV-Board，在控制台每秒打印一次：

```text
          xc100-0024 | Hello world! XC100 / ESP32-P4
```

## 实现

公共入口 `xf_common/main/entry.c` 的 `app_main()` 调用 `project/xc100/src/private_device.c` 中的 `xf_pri_device_start()`。
示例直接在主任务中用 `LOG_I()` 打印，通过 `vTaskDelay(pdMS_TO_TICKS(1000))` 阻塞约一秒，让其他任务运行。
日志前缀包含 `LOCAL_TAG` 和源码行号，实际行号随代码变化。
`project/xc100/inc/xf_project_config.h` 中的 `XF_LOG_ENABLE` 为 `1` 时开启 XF 日志，改为 `0` 后重新编译即可关闭。
开关同时作用于当前产品及其公共组件中的 `LOG_NR`、`LOG_N`、`LOG_I`、`LOG_E`，不影响 SDK 日志或直接调用的 `printf()`。
当前示例无需初始化显示屏、网络或 PSRAM。

## 开发板配置

`project/xc100/sdkconfig.conf` 保存开发板差异：

- Flash 为 16 MB，模式和频率沿用当前修订配置下 SDK 默认的 DIO 40 MHz。
- 选择 3.0 之前的 P4 芯片系列，最低修订版本为 0.0，以覆盖早期 0.x 和 1.x 芯片。
- UART0 使用 115200 波特率，并将 USB Serial/JTAG 作为第二路日志输出。
- PSRAM 保持 SDK 默认的关闭状态。

开发板丝印版本与芯片修订版本是两个不同概念；较新的 P4X 开发板使用 3.x 芯片系列，需调整私有配置后重新构建。
这些硬件区别及 16 MB Flash 容量见 [官方开发板指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)。

## 连接、编译和烧录

| 开发板 | 连接电脑的接口 | Linux 常见设备名 |
|---|---|---|
| [v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html) | USB-to-UART | `/dev/ttyUSB0` |
| [v1.5.2](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | USB Serial/JTAG | `/dev/ttyACM0` |

端口号以电脑实际枚举结果为准；烧录时连接表中的调试接口。

```bash
cd xf_tool

# 只编译
python3 build.py MODEL=xc100

# v1.4：编译、烧录并打开串口
python3 build.py MODEL=xc100 LOG=/dev/ttyUSB0

# v1.5.2：编译、烧录并打开串口
python3 build.py MODEL=xc100 LOG=/dev/ttyACM0
```

按 `Ctrl+]` 退出串口。自动进入下载模式失败时，按住 BOOT、按一下 RESET，再松开 BOOT，重新执行烧录命令。
修改板级配置使用 `python3 build.py MODEL=xc100 menuconfig`，保存退出后回写 xc100 私有配置。
