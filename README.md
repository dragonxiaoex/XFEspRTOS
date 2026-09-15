# XFEspRTOS

通过 `xf_tool/build.py` 统一选择产品、编译、烧录和打开串口，底层使用 ESP-IDF 的 `idf.py`。

先初始化子模块，安装仓库所固定 ESP-IDF 的工具链及 AStyle（Ubuntu/Debian：`sudo apt install astyle`）。脚本会自动加载 SDK 环境，日常在 `xf_tool` 下执行：

```bash
cd xf_tool

python3 build.py MODEL=xc200                         # 编译
python3 build.py MODEL=xc100                         # 编译 ESP32-P4 Hello World
python3 build.py MODEL=xc200 PORT=/dev/ttyUSB0        # 编译、烧录
python3 build.py MODEL=xc200 LOG=/dev/ttyUSB0         # 编译、烧录、进入 IDF 串口监视器
python3 build.py LOG=/dev/ttyUSB0                     # 仅打开串口，115200 波特率
python3 build.py MODEL=xc200 menuconfig               # 编辑并回写 XC200 私有配置
python3 build.py menuconfig                           # 编辑并回写芯片公共配置
python3 build.py CHIP=esp32s3 menuconfig              # 多芯片时也可直接指定
python3 build.py clean                               # 清理全部构建产物和生成配置
```

也可以直接执行 `./build.py`，或在仓库根目录执行 `python3 xf_tool/build.py ...`。参数顺序不限，`PORT` 和 `LOG` 不能同时使用。串口按 `Ctrl+]` 退出。

每次编译前调用一次 `format_code_linux.sh`，按现有 K&R 参数格式化公共代码和所选项目；格式化失败会停止构建。`menuconfig`、`clean`、仅串口操作不触发格式化，详见 [代码格式化说明](docs/code-formatting.md)。

每次编译成功后会打印 **Partitions Table** 和 **APP Memory Info**，再执行请求的烧录、串口操作。
Flash 占用按实际固件与 APP 分区容量计算；RAM 为静态链接占用，PSRAM 的动态分配需在运行时统计。

芯片在各产品的 `inc/xf_project_config.h` 中选择，当前 `xc200` 为：

```c
#define XF_USE_CHIP_ID "esp32s3"
```

`xc100` 的芯片宏为 `"esp32p4"`，公共入口调用 `xf_pri_device_start()`，每秒打印一次 Hello World。
脚本分别使用 `xf_tool/build/xc200/esp32s3` 和 `xf_tool/build/xc100/esp32p4`，其他产品、芯片使用各自目录。
配置按以下顺序加载，产品配置覆盖公共配置：

1. `xf_tool/config/esp32s3.conf`
2. `project/xc200/sdkconfig.conf`

生成的 `sdkconfig` 和 `config/sdkconfig.h` 位于所选构建目录中。`.conf` 不再声明 `CONFIG_IDF_TARGET`。

`MODEL=xc200 menuconfig` 加载公共配置和产品配置，保存并正常退出后，只将差异回写到 `project/xc200/sdkconfig.conf`。
单独 `menuconfig` 只加载 SDK 默认值和芯片公共配置，回写 `xf_tool/config/<芯片>.conf`，使用独立目录 `xf_tool/build/.common/<芯片>`。
目前有 ESP32-S3 和 ESP32-P4 两套公共配置，单独执行 `menuconfig` 会列出交互选择，也可用 `CHIP=esp32s3 menuconfig` 或 `CHIP=esp32p4 menuconfig` 指定。
公共菜单包含 SDK 和公共组件选项，产品自己的菜单选项只出现在私有菜单中。
公共配置修改后，各项目在下次构建时采用新公共值；项目私有文件里的同名选项仍然优先。
各芯片共用分区表位于 `xf_tool/config/esp32s3.csv` 和 `xf_tool/config/esp32p4.csv`。
xc100 按官方 ESP32-P4-Function-EV-Board 配置 16 MB Flash、3.0 之前的 P4 芯片系列，Hello World 暂不使用 PSRAM。
开发板接口、烧录命令见 [XC100 Hello World](docs/xc100-hello-world.md)。

直接修改公共或私有 `.conf` 后，下一次构建自动重新生成配置，无需先 `clean`：

```bash
python3 build.py MODEL=xc200
```

`clean` 删除整个 `xf_tool/build`；已经回写到 `.conf` 的配置会在重建时恢复。
自动回写适用于 `build.py ... menuconfig`；若回写报错，应先处理错误并保留构建目录中的 `sdkconfig`。

详细使用方法见 [分层配置说明](docs/configuration.md)，后续架构建议见 [构建管理方案](docs/build-management-proposal.md)。

公共 UART 接口和产品接入示例见 [UART 模块说明](docs/uart.md)。

构建按当前产品的组件依赖裁剪，XC100 不编译 LVGL，XC200 通过 `PRIV_REQUIRES lvgl` 使用它。`xf_common/components/common` 下的源文件和头文件目录自动收集；新增独立组件仍需声明依赖，详见 [公共组件构建与项目隔离](docs/common-build.md)。

公共组件的分层边界、参考总结和本次改造说明见 [公共框架分层说明](docs/component-layering-review.md)，主机回归测试见 [测试说明](tests/common/README.md)。
