# 统一构建入口与分层配置

日常入口为 `xf_tool/build.py`，按 `MODEL` 选择产品，调用原生 `idf.py`。
每次编译前先执行一次 `format_code_linux.sh`，格式化公共代码和所选项目；需安装 AStyle，工具失败会停止构建。范围和规则见 [代码格式化说明](code-formatting.md)。
顶层工程仍为 `xf_tool/CMakeLists.txt`，产品芯片在 `xf_project_config.h` 中声明。
不带 `MODEL` 的公共配置菜单根据 `xf_tool/config/*.conf` 选择芯片，不依赖产品。
配置文件采用 `.conf` 后缀，通过 ESP-IDF 原生 `SDKCONFIG_DEFAULTS` 指定路径，SDK 无需修改。

## 1. 维护文件

```text
xf_tool/config/
├── esp32s3.conf                # ESP32-S3 公共系统设置
├── esp32s3.csv                 # ESP32-S3 共用分区表
├── esp32p4.conf                # ESP32-P4 公共系统设置与分区选择
└── esp32p4.csv                 # ESP32-P4 共用分区表

project/xc100/
├── CMakeLists.txt             # 产品组件注册
├── inc/xf_project_config.h    # ESP32-P4 芯片标识与软件版本
├── src/private_device.c       # Hello World 启动入口
└── sdkconfig.conf             # Function EV 开发板与控制台配置

project/xc200/
├── inc/xf_project_config.h     # 芯片标识、产品版本和应用宏
└── sdkconfig.conf              # XC200 的 SDK 配置差异
```

构建加载顺序为：

```text
ESP-IDF 默认配置 → 芯片公共 .conf → 产品 sdkconfig.conf
```

在 Kconfig 依赖和有效值约束满足时，产品文件中同名选项覆盖公共文件。
公共文件只保存确实需要统一的配置，不需要复制 SDK 的全部默认项。

当前划分：

| 文件 | 内容 |
|---|---|
| `xf_tool/config/esp32s3.conf` | 现有编译设置、C 运行库、FreeRTOS 和终端支持 |
| `project/xc200/sdkconfig.conf` | CPU、Flash、PSRAM、分区、LCD、LVGL，以及产品的文件系统、TLS 和配网设置 |
| `xf_tool/config/esp32p4.conf` | FreeRTOS、终端支持与 ESP32-P4 共用分区选择 |
| `project/xc100/sdkconfig.conf` | Function EV 的 16 MB Flash、P4 修订系列及控制台配置 |

本次迁移以此前实际使用的 `xf_tool/sdkconfig` 为基线，保留 160 MHz CPU、16 MB Flash 和现有 PSRAM、显示设置。
原 `project/xc200/sdkconfig` 是另一份旧配置，其中的 240 MHz CPU 等差异未被引入。
原工程目录中的完整 `sdkconfig` 和 `.old` 副本已经移除，之后维护 `.conf` 文件。

## 2. 项目与芯片选择

每个产品的 `inc/xf_project_config.h` 必须包含唯一的一行字符串宏，例如：

```c
#define XF_USE_CHIP_ID "esp32s3"
```

`build.py` 和顶层 CMake 都直接读取这一行。脚本选择构建目录，CMake 在初始化 ESP-IDF 之前选择目标芯片及其公共 `.conf`。
宏使用 ESP-IDF 的小写芯片标识，例如 `esp32s3`、`esp32c3`，不使用数字编号、宏别名或条件分支选择芯片。
无需另维护项目到芯片的映射表；公共和产品 `.conf` 均不得再声明 `CONFIG_IDF_TARGET`。

脚本以产品宏为准设置 `IDF_TARGET`，不受终端中旧的目标芯片环境变量影响。
直接使用原生 `idf.py` 时，显式传入或环境中的 `IDF_TARGET` 必须与产品宏一致。
当前 `xc200` 使用 ESP32-S3，`xc100` 使用 ESP32-P4；增加其他芯片的公共文件不代表现有产品自动支持该芯片。

各项目的 `inc/xf_project_config.h` 还定义 `XF_LOG_ENABLE`：`1` 开启 XF 日志，`0` 关闭。
公共日志通过产品提供的 `inc/xf_log_config.h` 读取开关，该适配头包含 `xf_project_config.h`，不另维护一份开关。
修改后重新编译，只影响对应产品的 `LOG_NR`、`LOG_N`、`LOG_I`、`LOG_E`。
SDK 日志和直接调用的 `printf()` 不受该宏控制。

## 3. 日常构建

先初始化子模块并安装仓库所固定 ESP-IDF 的工具链、Python 环境及 AStyle。
脚本自动加载仓库内 `esp-idf/export.sh`，不需要每次手动 `source`，也不会自动安装工具链。
当前脚本面向 Linux/macOS 的 Bash 环境，Windows 可在 WSL 中使用。

在仓库根目录进入 `xf_tool`：

```bash
cd xf_tool

python3 build.py MODEL=xc200
python3 build.py MODEL=xc100
python3 build.py MODEL=xc200 PORT=/dev/ttyUSB0
python3 build.py MODEL=xc200 LOG=/dev/ttyUSB0
python3 build.py LOG=/dev/ttyUSB0
python3 build.py MODEL=xc200 menuconfig
python3 build.py menuconfig
python3 build.py CHIP=esp32s3 menuconfig
python3 build.py clean
```

| 参数 | 行为 |
|---|---|
| `MODEL=xc200` | 编译 xc200 |
| `MODEL=xc200 PORT=端口` | 编译成功后烧录，完成后退出 |
| `MODEL=xc200 LOG=端口` | 编译、烧录成功后进入 IDF 串口监视器 |
| `LOG=端口` | 仅打开串口，不需要项目或已有固件 |
| `MODEL=xc200 menuconfig` | 打开配置界面，正常退出后将已保存配置回写到私有 `.conf` |
| `menuconfig` | 编辑并回写芯片公共 `.conf`；只有一种芯片时自动选中，多种时交互选择 |
| `CHIP=esp32s3 menuconfig` | 直接选择 ESP32-S3 公共配置，适合多芯片或非交互终端 |
| `clean` | 删除整个 `xf_tool/build`，包括所有产品产物及生成配置 |

参数顺序不限，`PORT` 与 `LOG` 互斥；`menuconfig` 不接受串口参数，`clean` 单独使用。
`CHIP` 只用于不带 `MODEL` 的公共菜单，不能覆盖产品头文件里的芯片选择。
存在多个芯片且没有交互终端时，必须显式提供 `CHIP`，脚本不会根据旧构建或环境变量猜测。
无参数执行会提示用法，不默认构建某个产品。`--help` 查看命令帮助。
`clean` 和 `--help` 不需要 SDK 环境。

`MODEL` 搭配 `LOG` 使用 IDF Monitor，可根据当前 ELF 解析异常地址，波特率沿用项目配置。
单独的 `LOG` 使用 SDK Python 环境中的 PySerial 终端，固定 115200 波特率，不依赖 ELF，也不解析异常地址。
两种串口方式都可按 `Ctrl+]` 退出。

也可使用 `./build.py ...`。在仓库根目录执行 `python3 xf_tool/build.py ...` 效果相同；脚本不依赖调用时的工作目录。
构建目录固定为 `xf_tool/build/<MODEL>/<芯片标识>`。一个构建目录只属于一个产品和芯片，复用其他产品的缓存会得到明确错误。
公共菜单使用独立目录 `xf_tool/build/.common/<芯片标识>`，不复用产品的配置或缓存。

生成目录示例：

```text
xf_tool/build/xc200/esp32s3/
├── sdkconfig                   # 当前组合的完整生成配置
├── config/
│   ├── sdkconfig.h             # 自动生成的 C 头文件
│   └── sdkconfig.cmake         # 自动生成的 CMake 配置
├── CMakeCache.txt
└── ...                         # ELF、BIN、MAP、bootloader 等
```

源码继续使用 `#include "sdkconfig.h"`，头文件路径由 ESP-IDF 添加。
不要向产品 `inc` 目录复制生成的 `sdkconfig.h`。

### 构建完成后的摘要

`build.py` 在编译成功后调用 `build_report.py`，自动追加两个表格，再执行 `PORT` / `LOG` 请求的后续操作。
编译失败时不读取旧产物打印摘要，也不继续烧录。单独打开串口、`menuconfig` 和 `clean` 不打印构建摘要。

分区表由当前 SDK 的 `gen_esp32part.py` 解码生成的 `partition-table.bin`，名称、偏移和容量随实际配置变化。
同时依据 `flasher_args.json` 补充 bootloader 和分区表保留区域；表内 Size 为分配容量。

内存统计使用当前 SDK 的 `idf_size.py --format json2 --show-unused` 分析 MAP / ELF，保留芯片对应的分类：

| 项目 | 占用与容量的含义 |
|---|---|
| `FLASH (分区名)` | 实际烧录的 APP BIN 字节数 / 该 APP 分区容量，按烧录偏移匹配 factory 或 OTA 分区 |
| `IRAM`、`DRAM`、`DIRAM`、RTC 等 | ESP-IDF 统计的静态占用 / 链接布局可用容量；`DIRAM` 是指令与数据共用的内部 RAM |
| `PSRAM` | 静态段占用；物理容量由运行时识别，Size 和 Usage 显示 `N/A` |

内存表按类型汇总，可能涉及多个地址窗口，因此不使用单一 `addr` 表示整类内存。
RAM 的堆分配、动态任务栈和显示缓冲区需要运行时统计；PSRAM 静态占用为 `0 B` 不代表程序运行时没有使用 PSRAM。
ESP32-S3 的独立 `IRAM` 与共享 `DIRAM` 分开列出，独立 IRAM 的占用率不能用于判断整个芯片内存是否已满。

当前 xc200 的摘要示例（数值会随代码与配置变化）：

```text
========================= Partitions Table =========================
xc200-v1.1.1 / esp32s3    Flash: 16MB
Name                           Offset           Size
--------------------------------------------------------------------
bootloader                 0x00000000            32K
partition_table            0x00008000             4K
nvs                        0x00009000            24K
phy_init                   0x0000f000             4K
factory                    0x00010000          1024K

========================= APP Memory Info ==========================
Name                             Size           Used     Usage
--------------------------------------------------------------------
FLASH (factory)            0x00100000       574256 B    54.77%
DIRAM                      0x00053700       151100 B    44.21%
IRAM                       0x00004000        16384 B   100.00%
RTC SLOW                   0x00002000           36 B     0.44%
RTC FAST                   0x00002000           24 B     0.29%
PSRAM                             N/A            0 B       N/A
```

## 4. 配置回写与自动重新生成

日常修改配置使用：

```bash
python3 build.py MODEL=xc200 menuconfig  # 当前产品差异
python3 build.py menuconfig             # 芯片公共设置
python3 build.py MODEL=xc200
```

在界面中保存并正常退出后，脚本调用 `config_sync.py`，按入口选择回写目标：

| 入口 | 菜单配置来源 | 回写目标 |
|---|---|---|
| `MODEL=xc200 menuconfig` | SDK 默认值＋芯片公共配置＋XC200 私有配置 | `project/xc200/sdkconfig.conf`，保存相对公共基线的必要差异 |
| `menuconfig` 或 `CHIP=esp32s3 menuconfig` | SDK 默认值＋ESP32-S3 公共配置 | `xf_tool/config/esp32s3.conf`，保存公共设置 |

公共菜单不加载任何产品代码、私有 `.conf` 或产品 `Kconfig.projbuild`；它包含 SDK 和 `xf_common/components` 的公共组件菜单。
例如 XC200 的 LCD 产品选项只在私有菜单里出现。现有公共文件中当前菜单没有定义的选项会原样保留。
公共文件中明确写出的配置即使等于 SDK 默认值也保留，避免丢失团队统一约定。
在私有菜单里修改公共选项，仍只产生当前产品的私有覆盖；在公共菜单里修改则影响所有使用该芯片公共文件的产品。
产品私有覆盖优先，公共菜单不会自动删除或修改它们；需要项目继承公共值时，移除相应私有项。
取消未保存的修改时，只同步磁盘上最后保存的状态；菜单执行失败或中断时不执行回写。

回写使用当前 SDK 的 Kconfig 引擎处理依赖、choice、关闭选项和条件默认值，并剔除可自动推导出的冗余项。
写入前从候选维护文件重新初始化 Kconfig，检查完整配置与保存结果一致（忽略 SDK 初始化版本元数据），通过后才原子替换目标文件。
原私有文件保存在 `build/<产品>/<芯片>/config_sync/sdkconfig.conf.before-sync`；原公共文件保存在 `build/.common/<芯片>/config_sync/<芯片>.conf.before-sync`。已有注释保留。
同步失败会返回非零退出码，完整 `sdkconfig` 保留在构建目录中；此时不要 `clean`。

直接编辑公共或私有 `.conf` 也会生效：CMake 记录两份输入文件的摘要，发现变化后自动从两份文件重新生成 `sdkconfig`。
旧完整配置备份为 `sdkconfig.before-conf-change`。输入未变化时继续使用当前完整配置。
因此修改 `.conf` 后直接执行 `python3 build.py MODEL=xc200` 即可，无需先清理整个构建目录。

`clean` 删除所有产品的构建目录；已经回写的配置会从 `.conf` 恢复。
这个回写流程适用于统一脚本，直接运行原生 `idf.py menuconfig` 不会调用 `config_sync.py`。

当前共用 CSV 为 `xf_tool/config/esp32s3.csv`，沿用现有 nvs、phy_init、factory 布局。
对应选择保存在产品的私有配置中：

```ini
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/esp32s3.csv"
```

需要让其他 ESP32-S3 产品默认采用这张表时，可在公共菜单中选择自定义分区表并设置路径，再移除产品中不需要的重复覆盖；也可以直接将这两项移到 `esp32s3.conf`。
公共配置调整后，私有文件中同名选项仍然优先；需要继承公共值时移除对应私有覆盖。

需要其他原生 IDF 动作时，可以在 `xf_tool` 下手动加载环境后调用 `idf.py`。
例如，只重置 xc200 的构建目录，保留其他产品：

```bash
source ../esp-idf/export.sh
idf.py -B build/xc200/esp32s3 -D MODEL=xc200 fullclean build
```

原生 SDK 仍支持在相同环境中导出相对 SDK 默认值的非默认项：

```bash
idf.py -B build/xc200/esp32s3 -D MODEL=xc200 save-defconfig
```

原生导出命令仍会在 `xf_tool` 下生成 `sdkconfig.defaults`，这是工具输出文件，不是本工程加载的配置输入。
它包含当前完整配置相对 SDK 默认值的差异，不是自动算好的“产品相对公共配置”的差异。
统一脚本的回写会自动处理产品差异，不需要日常执行此导出命令。

## 5. 新增项目或芯片

新增同芯片产品时：

1. 添加 `project/<产品>/CMakeLists.txt` 和产品代码，保持产品组件结构。
2. 在 `inc/xf_project_config.h` 中声明 `XF_USE_CHIP_ID`、`XF_LOG_ENABLE`，并维护产品版本宏；参照 xc100 添加 `inc/xf_log_config.h`，将该项目配置接入公共日志。
3. 添加该产品的 `sdkconfig.conf`，只写入相对芯片公共配置的差异；没有差异时可为空。
4. 在 `xf_tool` 下执行 `python3 build.py MODEL=产品目录名`，脚本自动选择芯片和独立构建目录。

新增其他芯片产品时，还需要添加 `xf_tool/config/<芯片>.conf` 并适配硬件能力。
现有 `xc100` 可作为最小项目参考：复用公共 `app_main()`，在 `xf_pri_device_start()` 中每秒打印 Hello World。
ESP32-P4 公共分区表包含 24 KB NVS 和 1 MB factory APP 分区，公共 `.conf` 已选择这张表。
xc100 按官方 Function EV 开发板选择 16 MB Flash 和 3.0 之前的 P4 芯片系列，当前修订配置下 Flash 默认使用 DIO 40 MHz，PSRAM 保持关闭。
控制台同时输出到 UART0 和 USB Serial/JTAG；连接与烧录步骤见 [XC100 Hello World](xc100-hello-world.md)。
使用 `MODEL=xc100 menuconfig` 修改产品配置，P4X 开发板需另选对应芯片修订系列。
当前没有创建 `esp32c3.conf`；UART、LCD 等能力仍需随具体产品功能验证，当前公共 UART 使用普通驱动。

当前已完成统一脚本、芯片宏选择、两层配置、菜单回写和输入变化后的自动重新生成。构建 profile、公共组件拆分及多芯片并行依赖管理仍待后续需要时实施。

## 6. 官方机制

[ESP-IDF 配置加载说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html#custom-sdkconfig-defaults)规定，
`SDKCONFIG_DEFAULTS` 可以指定多个任意名称的文件，后面的文件覆盖前面的默认值。
文件名不要求使用 `.defaults` 后缀。
