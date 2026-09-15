# 代码格式化

统一构建入口在编译前调用一次 [format_code_linux.sh](../xf_tool/format_code_linux.sh)。脚本使用 AStyle，保持移植脚本原有的格式参数。

## 构建与手动使用

Ubuntu/Debian 可通过 `sudo apt install astyle` 安装工具，使用 `astyle --version` 检查。当前验证版本为 AStyle 3.1。

```sh
# 编译前自动格式化公共代码和 XC200
python3 xf_tool/build.py MODEL=xc200

# 只执行格式化，不编译
bash xf_tool/format_code_linux.sh xc200

# 格式化公共代码及全部产品
bash xf_tool/format_code_linux.sh
```

脚本以自身位置定位仓库，可以从其他目录调用，支持路径含空格。传入的产品参数是 `project` 下的目录名。

`MODEL=... PORT=...` 和 `MODEL=... LOG=...` 同样只在首次编译前执行一次格式化，后续烧录、串口阶段不再调用。`menuconfig`、`clean`、帮助和仅打开串口不调用格式化。直接使用原生 `idf.py` 不经过此钩子。

缺少 AStyle、参数错误、查找文件失败或 AStyle 返回非零时，构建入口会报告错误并停止，不继续编译或烧录。

## 文件范围

| 目录或文件 | 处理方式 |
| --- | --- |
| `xf_common/main` | 格式化 `.c`、`.h` |
| `xf_common/components/common` | 格式化 `.c`、`.h` |
| `project/<MODEL>` | 格式化 `.c`、`.h`；手动不传产品时处理全部产品 |
| 产品内的 `build`、`images`、`managed_components`、`.git` 目录 | 跳过 |
| `lv_conf.h` | 保留 LVGL 配置模板排版 |
| 符号链接 | 不跟随、不格式化链接目标 |
| ESP-IDF、LVGL 子模块、`ref`、主机测试代码 | 不在搜索范围中 |

## 默认格式

```text
--mode=c
--style=kr
--max-code-length=200
--indent=spaces=4
--pad-oper
--pad-comma
--pad-header
--align-pointer=name
--convert-tabs
--unpad-paren
--break-blocks=all
```

函数定义的左大括号单独一行；控制语句、结构体等的左大括号跟在当前行：

```c
typedef struct {
    int32_t value;
} Sample;

int32_t check_value(int32_t value)
{
    if (value < 0) {
        return -1;
    }

    return 0;
}
```

使用 4 空格缩进，指针靠变量名，如 `int *value`；补齐运算符、逗号和控制关键字周围的空格，移除括号内多余空格。`--break-blocks=all` 保留原脚本要求的逻辑块空行。

脚本另外使用 `--options=none --project=none`，避免其他 AStyle 配置影响结果；`--suffix=none` 不生成 `.orig`，`--formatted` 只列出发生变化的文件。已经符合格式的文件不会被重新写入，不因重复格式化触发重新编译。

## 本次修复与验证

移植脚本原先依赖工作目录，`find` 的通配符和收集到的路径未正确引用，可能漏选文件或拆开带空格的路径。现在使用脚本位置解析目录，通过 `find -exec ... {} +` 直接传递文件参数，并处理缺失工具、无源文件及失败状态。

测试命令：

```sh
python3 -m unittest discover -s xf_tool/tests -p test_format_code.py
python3 -m unittest discover -s xf_tool/tests -p test_build_cli.py
python3 tests/common/run_tests.py
```

测试覆盖跨目录调用、空格路径、产品选择、排除范围、符号链接、重复执行、外部配置隔离、缺少工具、格式化失败，以及构建/烧录/串口/菜单命令的调用顺序。XC100 / ESP32-P4 和 XC200 / ESP32-S3 的实际构建也已通过。
