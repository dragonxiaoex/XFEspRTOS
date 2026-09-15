# 公共组件构建与项目隔离

## 按产品选择组件

顶层 `xf_tool/CMakeLists.txt` 在 `project()` 前指定构建起点：

```cmake
set(COMPONENTS xf_common "${MODEL}")
```

`xf_common` 提供 `app_main()` 启动入口，`${MODEL}` 对应 `project/xc100`、`project/xc200` 等产品组件。ESP-IDF 从这两个组件出发，递归加入 `REQUIRES` / `PRIV_REQUIRES` 声明的依赖以及 SDK 必需的基础组件。

`EXTRA_COMPONENT_DIRS` 仍提供组件搜索路径。放入 `xf_common/components` 的独立组件可以被发现，但只有进入当前产品依赖链后才会编译。

| 产品 | 产品声明的直接依赖 | LVGL 是否编译 |
| --- | --- | --- |
| xc100 | `common` | 否 |
| xc200 | `common`、`lvgl`、`esp_lcd`、`esp_driver_gpio`、`esp_psram`、`esp_timer` | 是 |

日常命令不变，已有构建目录会自动更新构建规则，无需先 `clean`：

```bash
python3 xf_tool/build.py MODEL=xc100
python3 xf_tool/build.py MODEL=xc200
```

以后接入一个独立组件时，在实际使用它的产品或组件的 `CMakeLists.txt` 中声明一次依赖即可；间接依赖由 ESP-IDF 自动展开。公共头文件需要的依赖放在 `REQUIRES`，仅实现文件使用的依赖放在 `PRIV_REQUIRES`。只有部分产品使用的组件，应由这些产品声明，不要加入所有产品都会依赖的 `common`。

依赖也可能用于提供启动功能，而不只是满足 `#include`。例如 XC200 的 PSRAM 需要 `esp_psram` 组件，即使不直接调用它的 API，也需要保留该依赖。

`MODEL=... menuconfig` 随产品依赖显示相关组件配置；单独 `menuconfig` 仍使用独立的芯片公共上下文，提供 SDK 和公共组件配置。新增组件后，应先声明依赖，再打开产品菜单配置它。

这是 ESP-IDF 原生的构建裁剪机制，详见 [ESP-IDF 构建系统文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#optional-project-variables)。配置阶段仍需发现组件和解析依赖，但不再为无关组件生成编译任务。旧构建目录中可能留有此前生成的静态库；判断是否参与本次构建，应查看当前 `compile_commands.json` 或 `project_description.json`。

## common 内自动收集源文件

`xf_common/components/common/CMakeLists.txt` 递归收集当前 `common` 目录及其子目录：

- `.c`、`.cpp`、`.S` 文件自动加入公共组件编译。
- `.h`、`.hpp` 所在目录自动加入公共头文件搜索路径；`common` 根目录也在搜索路径中。
- 新增、删除、移动这些文件后，下次正常构建自动重新配置，不需要修改文件清单或先执行 `clean`。

例如新增以下模块：

```text
xf_common/components/common/
└── storage/
    ├── xf_storage.c
    └── xf_storage.h
```

之后直接执行：

```bash
python3 xf_tool/build.py MODEL=xc100
```

`xf_storage.c` 会参与编译，产品及公共代码可通过 `#include "xf_storage.h"` 或 `#include "storage/xf_storage.h"` 使用接口。各模块的头文件名称应避免重复，以免使用短文件名时找到错误的头文件。

自动收集的范围仅为 `xf_common/components/common`，产品代码、启动入口和 LVGL 仍由各自的 CMake 管理。测试代码继续放在仓库的 `tests` 下；扫描范围内的源文件都会参与编译，不要将备用实现、示例或测试源文件放进去。以后加入同一接口的其他平台实现时，需要由构建规则选择对应实现，不能同时编译。

组件裁剪以组件为单位。当前 `common` 内的全部源文件仍会按前面的约定自动编译；以后若某个模块较大且只供部分产品使用，应将其独立为 `components` 下与 `common` 同级的组件，并维护自己的 `CMakeLists.txt`。

新增模块如果使用了新的 ESP-IDF 组件，仍需在 `common/CMakeLists.txt` 的 `REQUIRES` 或 `PRIV_REQUIRES` 中声明依赖。只有实现文件使用的依赖可放在 `PRIV_REQUIRES`；公共头文件需要的依赖放在 `REQUIRES`。

实现使用 CMake 的 `GLOB_RECURSE` 和 `CONFIGURE_DEPENDS`，已针对当前 ESP-IDF 使用的 Ninja 构建方式验证。ESP-IDF 提前扫描依赖时使用脚本模式，此阶段只收集文件，正式配置时才启用文件列表跟踪。机制说明见 [CMake 官方文档](https://cmake.org/cmake/help/latest/command/file.html#filesystem)。
