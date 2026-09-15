# XFEspRTOS 统一构建与多芯片管理建议

更新日期：2026-09-15
文档状态：统一脚本、芯片宏选择和两层配置已实施；其余架构建议待评审。

本文保留最初的整体架构建议。当前已经按后续确认落地两层配置，维护文件采用
`xf_tool/config/esp32s3.conf` 和 `project/xc200/sdkconfig.conf`，生成配置位于构建目录。
统一脚本已支持 `python3 build.py MODEL=xc200` 及 `PORT`、`LOG`、`menuconfig`、`clean` 组合，
芯片由 `project/<MODEL>/inc/xf_project_config.h` 的 `XF_USE_CHIP_ID` 字符串宏指定。
实际命令和配置维护方式以 [统一构建与配置说明](configuration.md) 为准。
后续已补充菜单回写：`build.py MODEL=... menuconfig` 将已保存差异写回产品 `.conf`，
单独 `build.py menuconfig` 编辑芯片公共 `.conf`，多芯片可通过 `CHIP=...` 指定，
直接修改公共或私有 `.conf` 后，下次构建自动重新生成完整配置。
下文保留最初提议的命令形式，已由用户确认的 `MODEL=...` 接口替代；
`project.json` 未采用，profile 和进一步组件拆分仍为方案；
下文目录中的 `.defaults` 是最初的拟议命名，当前采用自定义 `.conf` 后缀。
SDK 指针与 LVGL 注册也已由用户修复，第二节的问题表保留审查时的历史记录。

## 1. 已明确的目标

- 当前只保留 `project/xc200`，其他旧项目已经由用户删除。
- 日常在 `xf_tool` 目录下使用同一个指令，通过不同参数选择项目构建。
- 后续增加多个产品，并支持不同型号的 ESP 芯片。
- 公共代码集中维护，产品之间通过组件复用。
- 保留 ESP-IDF 原生构建能力，统一入口负责参数解析、环境检查和构建组织。

**推荐保留 `xf_tool` 顶层工程，通过参数加载 `project/<项目名>` 产品组件。**

此前提出的“每个产品各自成为独立 ESP-IDF 顶层工程”不作为本方案的推荐方向。现有统一入口可以继续使用，重点是补齐项目描述、配置隔离、芯片约束和依赖管理。

## 2. 当前仓库的实际情况

### 2.1 构建关系

```text
xf_tool/CMakeLists.txt               唯一顶层 ESP-IDF 工程入口
    ├── MODEL 选择 project/xc200     产品组件
    ├── xf_common                   公共启动组件，提供 app_main()
    └── xf_common/components
        ├── common                  UART、协议和工具函数
        └── lvgl                    第三方图形库
```

环境已安装并完成初始化时，最初审查时的调用方式为：

```bash
cd xf_tool
idf.py -D MODEL=xc200 build
```

这里的 `MODEL` 是产品名称，不是 ESP 芯片型号。当前 `xc200` 由组件 CMake 注册，并不需要改为独立顶层工程。

### 2.2 已确认的问题

| 优先级 | 问题 | 证据与影响 | 建议 |
|---|---|---|---|
| 高 | SDK 记录与本地实际版本不一致 | 主仓库记录 `30aaf645`，对应 IDF v5.5.2；本地实际为 `27506a49`，描述为 `v6.1-704-g27506a49bc9` | 选定并验证 SDK 基线，同步记录确切提交 |
| 高 | LVGL 子模块配置不完整 | `xf_common/components/lvgl` 是 Git 子模块条目，但 `.gitmodules` 只配置了 ESP-IDF | 补齐仍在使用的 LVGL 来源、路径和提交 |
| 高 | 产品配置未随 MODEL 自动选择 | 已有构建记录读取 `xf_tool/sdkconfig`，不是 `project/xc200/sdkconfig` | 型号选择必须同时决定配置路径 |
| 高 | 相同产品存在不同配置副本 | 产品目录配置 CPU 为 240 MHz，`xf_tool/sdkconfig` 为 160 MHz | 先确认实际需要的配置，再建立唯一维护来源 |
| 中 | 缺少完整的构建隔离约定 | 默认复用 `xf_tool/build` 和 `xf_tool/sdkconfig`，没有项目与芯片的联合隔离规则 | 隔离缓存、生成配置和产物 |
| 中 | 文件名版本与内部版本不同 | 已有构建记录中项目名为 `xc200-v1.1.1`，内部版本为 `342234d-dirty` | 同一版本来源同时用于内部版本和发布命名 |
| 中 | 公共组件边界偏粗 | `common` 同时包含 UART、协议和工具，并依赖 `esp_driver_uart` | 按复用和平台依赖逐步拆分 |
| 中 | 功能开关存在两套来源 | 产品头文件定义 `XF_UART1_ENABLE`，UART 实现自行定义 `CONFIG_UART1_ENABLE 1` | 统一产品选项到明确的配置入口 |
| 低 | 构建说明和自动检查不足 | README 内容很少，未发现根仓库自己的构建 CI | 补充使用说明与活跃项目构建检查 |
| 低 | 开发环境配置写死本机路径 | `.vscode/settings.json` 的 CMake 源目录指向本机 `esp-idf` | 指向统一工程入口，使用工作区相对路径 |

以上来自源码、Git 元数据和已有构建记录核对，未重新执行全量编译。已有产物只能证明此前构建使用了哪些输入，不能视为当前代码重新编译通过。

旧项目已删除，因此“多个旧工程使用不同入口”的问题不再作为待整改项。删除后的 Git 条目应随用户的正常提交一起处理，本方案不恢复这些目录。

## 3. 统一命令的建议形式

以下为最初拟议接口，供了解设计思路。现有 `build.py` 使用文档开头说明的 `MODEL=...` 接口，以下选项未实施。

```bash
cd xf_tool

# 列出项目、默认芯片及已支持的芯片
python build.py --list

# 构建 xc200，默认目标从项目描述读取
python build.py -m xc200 build

# 显式指定芯片；必须属于该项目已支持的范围
python build.py -m xc200 -t esp32s3 build

# 使用同一组选项修改配置、烧录及查看日志
python build.py -m xc200 menuconfig
python build.py -m xc200 -p /dev/ttyUSB0 flash monitor

# 将配置变更导出为 defaults，供检查后纳入版本控制
python build.py -m xc200 savedefconfig
```

建议参数职责：

| 参数 | 含义 | 建议规则 |
|---|---|---|
| `-m / --model` | 产品或项目名称 | 必填，避免遗漏参数时误构建默认产品 |
| `-t / --target` | ESP-IDF 芯片标识 | 可省略，使用项目的默认芯片；传入时校验支持范围 |
| `--profile` | 同一产品的构建配置 | 首期只有 `default`；实际需要时再增加 `debug`、`release` 等 |
| `-p / --port` | 烧录、监视串口 | 传给 ESP-IDF，避免写入产品源码 |
| 动作 | `build`、`menuconfig`、`flash` 等 | 尽量沿用 `idf.py` 的含义，保留失败退出码 |

首次安装 SDK 工具链和初始化环境可以单独完成。日常构建只需统一指令；入口应检查所用 SDK、Python 和工具链是否匹配，不应每次重新安装环境。

最初建议将清理范围限定到当前选中的项目；现按用户确认的 `python3 build.py clean` 清理全部构建目录。
重置生成配置应有明确动作，普通构建保留 `menuconfig` 的修改。

## 4. 项目名称与芯片型号必须分开

建议概念如下：

```text
MODEL    = 产品，例如 xc200
TARGET   = 芯片，例如 esp32s3
PROFILE  = 构建配置，例如 default
BOARD    = 产品内部的硬件变体，有实际需求时再增加
```

“仓库支持多个 ESP 芯片”表示不同项目可以分别使用不同芯片，不表示每个产品都能在任意芯片上运行。

当前 `xc200` 的显示、PSRAM 和 UART/UHCI 使用方式需要按实际硬件适配。首期只应声明已经验证的 `esp32s3`，不能因为统一脚本增加了 `-t` 参数，就宣称 `xc200` 支持其他芯片。

建议每个产品维护一份简单的 `project.json`。例如：

```json
{
  "default_target": "esp32s3",
  "supported_targets": ["esp32s3"]
}
```

项目名称由目录名确定，避免在多处重复定义。芯片信息只维护一份，命令入口与 CMake 使用同一份描述。是否采用 JSON 可在实施前调整，但不应同时维护 Python 项目表、CMake 项目表和配置文件三份相同信息。

后续新增一个使用 ESP32-C3 的产品时，添加该产品目录、项目描述和组件代码即可。统一脚本不应再增加一个产品专用的判断分支。

## 5. 建议目录结构

下面是目标结构示意，不要求首期一次性创建所有组件和配置层。

```text
XFEspRTOS/
├── esp-idf/                         # 固定提交的 SDK
├── project/
│   └── xc200/
│       ├── project.json             # 默认芯片和支持范围
│       ├── CMakeLists.txt           # 产品组件注册，保留当前角色
│       ├── Kconfig.projbuild        # 产品配置项
│       ├── sdkconfig.defaults      # 产品配置基线
│       ├── sdkconfig.defaults.esp32s3
│       ├── inc/
│       ├── src/
│       ├── images/
│       └── boards/                  # 有硬件变体时再拆出
├── xf_common/
│   ├── CMakeLists.txt               # 公共启动组件
│   ├── main/entry.c                 # 薄的 app_main()
│   └── components/
│       ├── common/                  # 过渡期保留，逐步缩小职责
│       ├── xf_uart/                 # 平台串口适配
│       ├── xf_protocol/             # 公共协议
│       ├── xf_lvgl_port/            # 可复用的 LVGL 接入
│       └── lvgl/                    # 首期保留单一第三方来源
├── xf_tool/
│   ├── build.py                     # 统一命令
│   ├── CMakeLists.txt               # 唯一顶层工程
│   ├── config/
│   │   └── sdkconfig.defaults      # 适用于各产品的公共默认项
│   └── build/
│       └── xc200/
│           └── esp32s3/
│               └── default/         # 该组合的构建目录和生成配置
└── docs/
    └── build-management-proposal.md
```

保留 `xf_common/main/entry.c` 作为公共启动入口是可行的。它负责公共初始化和调用产品启动函数；界面、业务任务和具体硬件初始化留在产品侧。

产品启动接口应有清楚的归属，构建依赖要表达公共启动组件对所选产品实现的需要。不要依赖“所有组件刚好都被加入链接”来维持正确性。若后续启用最小组件构建，也必须确保启动组件和产品组件可达。

## 6. 构建目录与配置隔离

### 6.1 以项目、芯片和配置组合作为隔离单位

建议生成路径：

```text
xf_tool/build/<model>/<target>/<profile>/
```

该目录包含这次构建的：

- CMake 缓存、Ninja 文件、中间目标和编译数据库。
- 生成的 `sdkconfig` 与 `sdkconfig.h`。
- ELF、BIN、MAP、分区表、bootloader 和烧录参数。
- 构建信息记录。

底层调用通过 `idf.py -B` 指定构建目录，通过 `SDKCONFIG` 指定对应的生成配置，并显式传递产品与芯片。两者必须同时设置，仅使用不同 `-B` 不能解决共享 `sdkconfig` 的问题。

切换项目或芯片时选择另一套目录，不在同一缓存上反复调用 `set-target`。后续切回原组合应能增量构建。

### 6.2 配置来源与覆盖顺序

建议把配置拆为“维护的输入”和“构建生成的结果”：

| 配置 | 角色 | 管理方式 |
|---|---|---|
| 公共 `sdkconfig.defaults` | 跨产品适用的默认项 | 纳入 Git，避免放入单个板子的引脚配置 |
| 产品 `sdkconfig.defaults` | 产品需要的配置 | 纳入 Git |
| 产品 `sdkconfig.defaults.<target>` | 对应芯片的差异项 | 有差异时添加，纳入 Git |
| 可选 profile defaults | 调试或发布差异 | 实际需要时添加，纳入 Git |
| 构建目录中的 `sdkconfig` | 解析后的完整配置 | 生成文件，按组合隔离 |
| `sdkconfig.old` | 工具生成的备份 | 不作为长期配置来源 |

通过 `SDKCONFIG_DEFAULTS` 明确列出输入，按公共、产品、可选 profile 的顺序应用。ESP-IDF 会在对应 defaults 文件之后处理其芯片后缀文件；不要在脚本中重复合并同一文件。

**defaults 只为未设置的配置提供初始值，不会自动覆盖已有 `sdkconfig` 中已经设置的值。**

因此需要约定：

1. 首次构建从维护的 defaults 生成配置。
2. `menuconfig` 修改当前组合的生成配置，普通增量构建保留修改。
3. `savedefconfig` 先导出待检查结果，再把变化归入公共、产品或芯片配置；不能直接用单个芯片的结果覆盖公共 defaults。
4. defaults 发生变化后，明确执行重新生成配置的动作，或提示当前生成配置可能仍保留旧值。
5. 干净构建和发布检查从维护的配置重新生成，避免依赖本地未保存的设置。

迁移 `xc200` 时，应以确认过的可用固件配置为基线，逐项核对 160/240 MHz、Flash、PSRAM、分区和屏幕设置。不能仅凭文件所在目录认定其中一份一定正确。

## 7. CMake 调整建议

`xf_tool/CMakeLists.txt` 继续保留，但职责应集中为：

1. 校验产品目录与项目描述。
2. 确定所选芯片、配置输入及生成配置路径。
3. 加载固定的 ESP-IDF 工程工具。
4. 只引入所需的公共组件、产品组件和可选板级组件。
5. 设置 ESP-IDF 固件版本并创建工程。

具体需要注意：

- 当前 SDK 会在加载 `project.cmake` 时较早读取 `SDKCONFIG`；路径选择和相关目标设置必须在需要它们的初始化步骤之前完成，或从命令行传入。
- 自定义产品目录变量建议使用 `XF_PROJECT_DIR`，避免与 ESP-IDF 的工程目录概念 `PROJECT_DIR` 混淆。
- 缺少产品、非法芯片、缺少必要配置时给出明确错误，不静默回退到 `xc200`。
- 固件内部版本使用 `PROJECT_VER`。首期可以继续读取现有 `XF_SOFTWARE_VERSION_STRING`，不必同时引入另一份手工维护的版本号。
- 工程名建议保持产品身份稳定，发布产物名再包含版本、芯片和配置。
- 业务源码优先明确列举；资源文件需要自动收集时单独处理，避免递归收集备份源码。
- `REQUIRES` 表达公开接口依赖，`PRIV_REQUIRES` 表达组件内部依赖；公开与内部头文件目录也应区分。
- LVGL 库及其调用方必须采用同一份 `lv_conf.h`。后续可缩小当前全局宏的作用范围，但不能只给应用设置而让库使用另一份配置。

统一脚本负责整理参数，CMake 保留必要的一致性校验，使直接调用 `idf.py` 时也不会绕过项目和芯片约束。

## 8. 公共代码与多芯片适配

### 8.1 按实际依赖逐步拆分

| 层次 | 示例 | 依赖方向 |
|---|---|---|
| 公共基础逻辑 | 工具函数、校验算法、协议编解码 | 尽量不依赖具体硬件与产品 |
| 平台适配 | UART/UHCI、计时、LVGL 接入 | 依赖 ESP-IDF，封装平台差异 |
| 板级实现 | 引脚、屏幕参数、外设初始化 | 组合公共驱动，描述真实硬件 |
| 产品业务 | 页面、任务、产品协议处理、启动流程 | 使用公共能力和板级接口 |

当前 `common` 只有少量源文件，不必立即拆成很多组件。优先把 UART 等平台依赖与纯工具、协议分开，使一个不需要 UART 或屏幕的产品可以只引用所需部分。

公共代码应通过参数、明确配置或回调接收产品差异，尽量避免到处出现 `#if MODEL == ...`。公共接口头文件不要直接包含某个产品的私有头文件。

### 8.2 功能配置统一来源

UART 开关等可配置功能优先定义为项目或组件的 Kconfig 选项，由产品 defaults 选择。不要再由公共 `.c` 自行定义 `CONFIG_*` 开关。

引脚、屏幕参数等硬件信息可以由板级配置或初始化结构体提供。当前 UART 接口已经通过初始化参数传入引脚，这种做法可以保留。

### 8.3 多芯片支持的边界

- 构建系统为不同目标选择正确工具链和配置。
- 公共组件根据 SDK 的芯片能力配置选择可用实现。
- 对 UART DMA/UHCI、RGB LCD、PSRAM 等能力逐项核对，不假定不同 ESP 芯片都具有相同外设。
- 缺少能力时，只有存在实际需求才增加替代实现；否则在配置阶段明确拒绝该组合。
- 是否需要创建多个板级实现，由真实产品硬件决定，不提前复制整套业务代码。

首次加入第二种芯片时，选择一个真实产品或必要的适配验证工程，验证统一入口和公共组件。仅让参数解析接受一个新芯片名称，不算完成适配。

## 9. SDK 与第三方依赖管理

### 9.1 SDK 基线

首期建议所有活跃产品共用一个经过验证的 ESP-IDF 提交。支持多种芯片不要求同时维护多个 SDK 版本。

SDK 升级作为独立变更：更新版本记录后，对所有已支持的产品和芯片组合重新构建。只有真实产品存在不可兼容的 SDK 要求时，再增加 SDK 版本维度。

仓库内固定了 SDK，不代表 shell 中的 `IDF_PATH` 一定正确。统一入口应识别并检查实际使用路径与版本，避免误用另一套本机 SDK。

### 9.2 LVGL 等第三方组件

首期建议修复并保留现有 LVGL 子模块，减少同时改变构建框架和依赖来源的变量。

后续可选择：

| 方式 | 适用情况 | 要求 |
|---|---|---|
| Git 子模块 | 希望直接控制源码，或维护必要的上游修改 | 完整 `.gitmodules`、固定提交、清楚的本地修改策略 |
| ESP Component Manager | 使用已发布组件，期望自动解析依赖 | 明确依赖约束、锁定解析结果、管理生成目录 |

同一份第三方组件只保留一个明确来源。采用组件管理器时，不应同时保留同名本地组件却不说明覆盖关系。

### 9.3 依赖状态也需要考虑隔离

将来使用组件管理器时，不能只隔离 `build` 和 `sdkconfig`：

- 不同产品、目标芯片或配置可能解析出不同的依赖集合。
- `dependencies.lock` 必须对应明确的依赖组合，不能让多个组合反复覆盖同一份锁文件。
- 当前 SDK 提供 `DEPENDENCIES_LOCK` 构建属性，可指定锁文件路径；锁文件由解析器生成，不手工改内容。
- `managed_components` 默认位于工程目录。仅改锁文件路径或 `-B` 不会自动隔离下载目录。
- 在统一顶层工程下，首期不承诺并行运行不同组合的组件解析。若引入差异化托管依赖，应保证切换时重配，并串行保护所有会读写共享下载目录的构建操作。
- 真正需要并行构建差异化依赖时，再设计隔离工作区或独立依赖目录，并验证所用组件管理器版本的支持方式。

当前锁文件主要记录 IDF，本身不能替代对 LVGL 子模块提交的管理。依赖解析和并行能力可以分阶段完善，不必成为首期统一命令的前置大工程。

## 10. 固件版本、产物与追踪

建议每次构建记录：

```text
项目 / 芯片 / 配置
固件版本
主仓库 Git 提交及是否存在未提交修改
ESP-IDF 提交
LVGL 等关键依赖的版本或提交
配置标识或摘要
```

首期继续使用现有产品版本头文件作为版本来源，设置 `PROJECT_VER`，同时在产物信息中保留 Git 提交用于追踪。

发布文件名可采用：

```text
xc200_esp32s3_default_v1.1.1.bin
```

发布目录应说明这是应用 BIN 还是合并 BIN。完整烧录包还应包含所需的 bootloader、分区表、数据镜像和烧录参数，地址从 ESP-IDF 生成结果获取，不在打包脚本中另写一套。

## 11. 成熟仓库的参考方式

| 参考项目 | 值得借鉴的做法 | 本仓库的对应调整 |
|---|---|---|
| [ESP-IoT-Solution](https://github.com/espressif/esp-iot-solution) | 公共能力组件化，组件声明版本和依赖 | 改善 `xf_common` 的边界和依赖声明；本仓库仍保留自己的统一入口 |
| [ESP-BSP](https://github.com/espressif/esp-bsp) | 板级初始化和应用逻辑分离，不同板子提供统一能力接口 | 产品硬件变化放入板级实现，公共驱动保持复用 |
| [小智 ESP32](https://github.com/78/xiaozhi-esp32) | 板型描述关联芯片与构建选项，由统一脚本和 CI 组织变体构建 | 用项目描述驱动统一命令，减少项目专用脚本分支 |

本方案重点借鉴职责划分和构建组织，不要求照搬其他仓库的目录、语言或规模。

参考资料：

- [ESP-IDF 构建系统：配置、组件与版本规则](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [组件清单 idf_component.yml](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html)
- [组件锁文件说明](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/dependencies_lock.html)
- [小智 ESP32 自定义板型说明](https://github.com/78/xiaozhi-esp32/blob/main/docs/custom-board_zh.md)
- [小智 ESP32 构建 CI](https://github.com/78/xiaozhi-esp32/blob/main/.github/workflows/build.yml)
- [ESP-IoT-Solution button 组件清单](https://github.com/espressif/esp-iot-solution/blob/master/components/button/idf_component.yml)

外部链接指向维护中的文档和分支；实施时以仓库最终固定的 SDK、组件及工具版本为准。

## 12. 建议分阶段实施

| 阶段 | 工作内容 | 完成标准 |
|---|---|---|
| 1：建立可复现基线 | 确定 SDK 提交，补齐 LVGL 子模块记录，确认 xc200 有效配置，统一内部版本 | 从干净工作区能构建 xc200，输入版本和配置明确 |
| 2：统一命令与隔离 | 增加项目描述和 `build.py`，校验项目/芯片，隔离构建目录与生成配置，整理 defaults | 在 `xf_tool` 下一条指令完成构建；配置、烧录、监视使用同一选择规则 |
| 3：改善公共组件 | 拆开必要的平台依赖，统一功能开关，理清启动接口和产品依赖 | 公共组件不依赖具体产品私有头文件，不需要的硬件能力可不引入 |
| 4：接入第二种芯片 | 根据实际项目添加目标配置和必要的平台实现 | 至少两个真实支持组合可切换构建，彼此状态独立 |
| 5：自动检查与发布 | 增加构建矩阵、干净构建检查和产物信息 | 公共代码修改后自动构建所有活跃组合，产物可追踪 |

首轮建议落实阶段 1、2；公共组件拆分按实际阻碍跟进。首轮不要求引入多个 SDK、复杂插件框架、完整板级抽象体系或并行构建调度。

## 13. 验收检查项

- [ ] 干净克隆可以获取完整 SDK 和 LVGL 依赖。
- [ ] 统一入口检测到的 SDK 与项目固定版本一致。
- [ ] `--list` 能列出 `xc200` 及其真实支持芯片。
- [ ] 缺少项目参数、未知项目或不支持的芯片得到明确错误。
- [ ] `xc200 + esp32s3` 可从干净配置完成构建。
- [ ] 生成配置路径位于选中组合的构建目录，不再使用共享 `xf_tool/sdkconfig`。
- [ ] `menuconfig` 的修改不会被普通增量构建无声覆盖。
- [ ] defaults 变更有明确的重生成流程，并能用于干净构建。
- [ ] 固件内部版本与维护的产品版本一致。
- [ ] `flash` 和 `monitor` 使用对应构建组合与串口。
- [ ] 清理当前组合不影响其他组合的配置和产物。
- [ ] 第二个真实项目/芯片接入后，可以来回切换并恢复各自增量构建。
- [ ] 如采用托管依赖，其锁文件、下载目录和并发行为已经验证。

编译通过用于验证构建组织和接口兼容性。屏幕、UART/DMA、PSRAM 等运行行为仍需在对应硬件上验证。

## 14. 后续评审时可确定的事项

| 事项 | 本文建议 |
|---|---|
| 统一入口形式 | `xf_tool/build.py`，Python 负责参数与调用 |
| 产品参数名称 | 保留 `MODEL` 概念，CLI 使用 `-m / --model` |
| 项目描述格式 | 每个项目一份简短 `project.json` |
| 目标芯片选择 | 默认读取项目描述，允许显式传入已支持芯片 |
| 首期配置种类 | 仅 `default`，有需求再加入其他 profile |
| SDK 策略 | 固定一套经验证的 SDK 提交，多产品共同使用 |
| 第三方管理 | 首期修复并保留 LVGL 子模块，后续评估组件管理器 |
| 公共启动入口 | 保留 `xf_common/main/entry.c`，明确依赖和产品启动契约 |
| 第一轮实施范围 | 可复现基线、统一命令、配置与产物隔离、版本一致性 |

## 15. 本地核对入口

- [统一顶层 CMake](../xf_tool/CMakeLists.txt)
- [xc200 产品组件](../project/xc200/CMakeLists.txt)
- [公共启动组件](../xf_common/CMakeLists.txt)
- [当前 common 组件](../xf_common/components/common/CMakeLists.txt)
- [产品版本与功能配置](../project/xc200/inc/xf_project_config.h)
- [公共 UART 实现](../xf_common/components/common/bsp/xf_uart_bsp.c)
- [Git 子模块配置](../.gitmodules)
- [VS Code 设置](../.vscode/settings.json)

实施迁移并完成验证后，这些文件的位置和内容可能变化；届时应同步更新本文件及实际构建使用说明。
