#!/usr/bin/env python3
"""在统一入口中选择产品、编译、烧录及打开串口。"""

import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass


TOOL_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOL_DIR.parent
BUILD_DIR = TOOL_DIR / "build"
CHIP_PATTERN = re.compile(
    r'^[ \t]*#[ \t]*define[ \t]+XF_USE_CHIP_ID[ \t]+"([a-z0-9]+)"'
    r'[ \t]*(?:/\*.*\*/|//.*)?$',
    re.MULTILINE,
)
HELP = """用法（在 xf_tool 下执行，也可从其他目录指定脚本路径）：
  python3 build.py MODEL=xc200                         编译
  python3 build.py MODEL=xc200 PORT=/dev/ttyUSB0        编译并烧录
  python3 build.py MODEL=xc200 LOG=/dev/ttyUSB0         编译、烧录并进入 IDF 串口监视器
  python3 build.py LOG=/dev/ttyUSB0                     仅打开串口，115200 波特率
  python3 build.py MODEL=xc200 menuconfig               编辑并回写产品私有 .conf
  python3 build.py menuconfig                           编辑并回写芯片公共 .conf
  python3 build.py CHIP=esp32s3 menuconfig              指定芯片编辑公共 .conf
  python3 build.py clean                               删除 xf_tool/build 下全部构建产物和生成配置

MODEL、PORT、LOG 的顺序不限；PORT 和 LOG 不能同时使用。
公共菜单在只有一种芯片时自动选择；多种芯片时交互选择，也可通过 CHIP 指定。
编译前自动格式化公共代码和所选项目，需要已安装 astyle；格式化失败时停止构建。
编译成功后打印分区表和静态资源占用，再执行请求的烧录、串口操作。
串口终端按 Ctrl+] 退出。clean 不需要 SDK 环境。
"""


class BuildError(Exception):
    """参数、项目或环境不满足操作要求。"""


@dataclass
class Options:
    model: str = ""
    port: str = ""
    log: str = ""
    action: str = "build"
    chip: str = ""


def parse_options(arguments):
    values = {}
    action = ""
    for argument in arguments:
        if "=" in argument:
            key, value = argument.split("=", 1)
            if key not in {"MODEL", "PORT", "LOG", "CHIP"}:
                raise BuildError(f"未知参数：{key}")
            if key in values:
                raise BuildError(f"参数重复：{key}")
            if not value:
                raise BuildError(f"{key} 不能为空。")
            values[key] = value
        elif argument in {"menuconfig", "clean"}:
            if action:
                raise BuildError("一次只能指定一个动作。")
            action = argument
        else:
            raise BuildError(f"未知动作：{argument}；使用 --help 查看用法。")

    if "PORT" in values and "LOG" in values:
        raise BuildError("PORT 和 LOG 不能同时使用。")
    if action == "clean" and values:
        raise BuildError("clean 清理全部构建产物，不接受其他参数。")
    if action == "menuconfig" and ("PORT" in values or "LOG" in values):
        raise BuildError("menuconfig 不接受 PORT 或 LOG 参数。")
    if "CHIP" in values and (action != "menuconfig" or "MODEL" in values):
        raise BuildError("CHIP 只用于不带 MODEL 的公共 menuconfig；产品芯片由头文件指定。")
    if "CHIP" in values and not re.fullmatch(r"[a-z0-9]+", values["CHIP"]):
        raise BuildError("CHIP 必须是小写的 ESP-IDF 芯片标识。")
    if not action and "MODEL" not in values and set(values) != {"LOG"}:
        raise BuildError("请指定 MODEL，或仅使用 LOG 打开串口。")
    if "MODEL" in values and not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", values["MODEL"]):
        raise BuildError("MODEL 必须是 project 下的产品目录名称。")

    return Options(values.get("MODEL", ""), values.get("PORT", ""), values.get("LOG", ""),
                   action or "build", values.get("CHIP", ""))


def validate_config(config):
    if not config.is_file():
        raise BuildError(f"缺少配置文件：{config}")
    if re.search(r"^[ \t]*CONFIG_IDF_TARGET[ \t]*=", config.read_text(encoding="utf-8"), re.MULTILINE):
        raise BuildError(f"请移除 {config} 中的 CONFIG_IDF_TARGET；产品使用 XF_USE_CHIP_ID，公共菜单使用 CHIP。")


def common_chip(requested):
    targets = sorted(path.stem for path in (TOOL_DIR / "config").glob("*.conf")
                     if path.is_file() and re.fullmatch(r"[a-z0-9]+", path.stem))
    if not targets:
        raise BuildError("xf_tool/config 下没有芯片公共 .conf 文件。")
    if requested:
        if requested not in targets:
            raise BuildError(f"没有 {requested} 的公共配置；可选芯片：{', '.join(targets)}")
        target = requested
    elif len(targets) == 1:
        target = targets[0]
    else:
        if not sys.stdin.isatty():
            raise BuildError(f"存在多个芯片：{', '.join(targets)}；请使用 CHIP=<芯片> menuconfig。")
        print("选择要编辑的芯片公共配置：", flush=True)
        for index, name in enumerate(targets, 1):
            print(f"  {index}. {name}", flush=True)
        while True:
            try:
                answer = input("芯片编号或名称（q 退出）：").strip()
            except EOFError:
                raise BuildError("未选择芯片。") from None
            if answer.lower() == "q":
                raise KeyboardInterrupt
            if answer in targets:
                target = answer
                break
            if answer.isdecimal() and 1 <= int(answer) <= len(targets):
                target = targets[int(answer) - 1]
                break
            print("请输入列表中的编号或芯片名称。", flush=True)
    validate_config(TOOL_DIR / "config" / f"{target}.conf")
    return target


def project_chip(model):
    project_dir = ROOT_DIR / "project" / model
    header = project_dir / "inc" / "xf_project_config.h"
    project_config = project_dir / "sdkconfig.conf"
    for path in (project_dir / "CMakeLists.txt", header, project_config):
        if not path.is_file():
            raise BuildError(f"产品 {model} 缺少文件：{path}")

    targets = CHIP_PATTERN.findall(header.read_text(encoding="utf-8"))
    if len(targets) != 1:
        raise BuildError(f'{header} 必须包含唯一的 #define XF_USE_CHIP_ID "芯片标识"。')
    target = targets[0]
    common_config = TOOL_DIR / "config" / f"{target}.conf"
    if not common_config.is_file():
        raise BuildError(f"缺少芯片公共配置：{common_config}")
    for config in (common_config, project_config):
        validate_config(config)

    return target


def idf_environment():
    """在子进程中加载仓库 SDK 环境，不依赖调用者预先 source。"""
    sdk_dir = ROOT_DIR / "esp-idf"
    if not (sdk_dir / "export.sh").is_file():
        raise BuildError("缺少 esp-idf/export.sh，请先初始化仓库的 ESP-IDF 子模块并安装工具链。")

    environment = os.environ.copy()
    environment.pop("IDF_TARGET", None)
    # 路径作为独立参数传入 shell；只执行 SDK 自己的环境初始化脚本。
    export_command = (
        'source "$1/export.sh" >&2 && '
        '"$2" -c \'import json, os; print(json.dumps(dict(os.environ)))\''
    )
    result = subprocess.run(
        ["bash", "-c", export_command, "xf-build-env", str(sdk_dir), sys.executable],
        cwd=TOOL_DIR,
        env=environment,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise BuildError(f"ESP-IDF 环境初始化失败，请确认已安装对应工具链和 Python 环境。\n{result.stderr.strip()}")
    try:
        environment = json.loads(result.stdout)
        python_dir = Path(environment["IDF_PYTHON_ENV_PATH"])
        python = python_dir / "bin" / "python"
        active_sdk = Path(environment["IDF_PATH"]).resolve()
    except (ValueError, KeyError, TypeError) as error:
        raise BuildError("ESP-IDF 未返回有效环境，请检查 SDK 安装。") from error
    if active_sdk != sdk_dir.resolve() or not python.is_file():
        raise BuildError("激活的 SDK 或 Python 环境与当前仓库不匹配。")

    return str(python), environment


def clean_build():
    # 固定清理脚本旁的 build 目录，拒绝通过根符号链接访问外部文件。
    if BUILD_DIR.is_symlink():
        raise BuildError(f"构建目录是符号链接，未执行清理：{BUILD_DIR}")
    if not BUILD_DIR.exists():
        print("没有需要清理的构建产物。")
        return
    if not BUILD_DIR.is_dir():
        raise BuildError(f"构建路径不是目录：{BUILD_DIR}")
    shutil.rmtree(BUILD_DIR)
    print(f"已删除构建产物和生成配置：{BUILD_DIR}")


def run(options):
    if options.action == "clean":
        clean_build()
        return 0

    public_menu = options.action == "menuconfig" and not options.model
    target = ""
    if public_menu:
        target = common_chip(options.chip)
    elif options.model:
        target = project_chip(options.model)
    python, environment = idf_environment()
    if not options.model and not public_menu:
        # 独立终端没有构建或烧录功能，不需要产品、ELF 或 CMake 缓存。
        command = [
            python, "-m", "serial.tools.miniterm",
            "--raw", "--eol", "LF", "--rts", "0", "--dtr", "0",
            "--", options.log, "115200",
        ]
        print(f"打开串口 {options.log}，115200 波特率；按 Ctrl+] 退出。", flush=True)
    else:
        build_dir = BUILD_DIR / (options.model or ".common") / target
        environment["IDF_TARGET"] = target
        command = [
            python, str(ROOT_DIR / "esp-idf" / "tools" / "idf.py"),
            "-B", str(build_dir), "-D", f"IDF_TARGET={target}",
        ]
        if public_menu:
            command.extend(["-D", "XF_COMMON_CONFIG_ONLY=ON"])
            print(f"公共配置：{TOOL_DIR / 'config' / (target + '.conf')}\n构建目录：{build_dir}", flush=True)
        else:
            command.extend(["-D", f"MODEL={options.model}"])
            print(f"项目：{options.model}  芯片：{target}\n构建目录：{build_dir}", flush=True)
        if options.port or options.log:
            command.extend(["-p", options.port or options.log])
        if options.action == "menuconfig":
            print(shlex.join(command + ["menuconfig"]), flush=True)
            result = subprocess.run(command + ["menuconfig"], cwd=TOOL_DIR, env=environment)
            if result.returncode:
                return result.returncode if result.returncode > 0 else 128 - result.returncode
            sync_command = [
                python, str(TOOL_DIR / "config_sync.py"), "--build-dir", str(build_dir),
                "--common", str(TOOL_DIR / "config" / f"{target}.conf"),
            ]
            if not public_menu:
                sync_command.extend(["--private", str(ROOT_DIR / "project" / options.model / "sdkconfig.conf")])
            result = subprocess.run(sync_command, cwd=TOOL_DIR, env=environment)
            return result.returncode if result.returncode >= 0 else 128 - result.returncode
        else:
            print("格式化公共代码和所选项目……", flush=True)
            result = subprocess.run(
                ["bash", str(TOOL_DIR / "format_code_linux.sh"), options.model],
                cwd=TOOL_DIR, env=environment,
            )
            if result.returncode:
                print("错误：代码格式化失败，已停止构建。", file=sys.stderr, flush=True)
                return result.returncode if result.returncode > 0 else 128 - result.returncode
            # 先等待编译完成，摘要在烧录和串口监视器之前打印。
            print(shlex.join(command + ["build"]), flush=True)
            result = subprocess.run(command + ["build"], cwd=TOOL_DIR, env=environment)
            if result.returncode:
                return result.returncode if result.returncode > 0 else 128 - result.returncode
            report = subprocess.run(
                [python, str(TOOL_DIR / "build_report.py"), str(build_dir)], cwd=TOOL_DIR, env=environment,
            )
            if report.returncode:
                print("提示：固件编译成功，但资源统计未完成。", file=sys.stderr, flush=True)
            if not options.port and not options.log:
                return 0
            command.append("flash")
            if options.log:
                command.append("monitor")
    print(shlex.join(command), flush=True)

    # 交接给原生命令，保留终端交互、信号处理和退出码。
    os.chdir(TOOL_DIR)
    os.execve(command[0], command, environment)


def main(arguments=None):
    arguments = sys.argv[1:] if arguments is None else arguments
    if arguments in (["-h"], ["--help"]):
        print(HELP)
        return 0
    try:
        return run(parse_options(arguments))
    except (BuildError, OSError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\n已中断。", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
