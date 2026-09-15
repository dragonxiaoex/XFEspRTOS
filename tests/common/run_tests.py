#!/usr/bin/env python3
"""用主机编译器验证公共协议、UART 适配与产品收包，不依赖 ESP-IDF 工具链。"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
COMMON = ROOT / "xf_common/components/common"


def main():
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c11", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             "-fno-pie", "-no-pie"]
    includes = [f"-I{COMMON / name}" for name in ("utils", "protocol", "bsp")]
    protocol = [str(COMMON / name) for name in ("protocol/xf_mcu_protocol.c", "utils/xf_utils.c")]
    environment = os.environ.copy()
    # 受限主机环境不支持 LeakSanitizer；保留地址与未定义行为检查。
    environment["ASAN_OPTIONS"] = "detect_leaks=0"

    with tempfile.TemporaryDirectory(prefix="xf-common-tests-") as directory:
        build = Path(directory)

        def run(name, sources, extra=()):
            binary = build / name
            subprocess.run(compiler + flags + includes + list(extra) + list(map(str, sources))
                           + ["-o", str(binary)], check=True)
            subprocess.run([str(binary)], env=environment, check=True)

        run("protocol", [HERE / "test_protocol.c", *protocol])
        run("uart", [HERE / "test_uart.c", COMMON / "bsp/xf_uart_bsp.c", *protocol],
            [f"-I{HERE / 'include'}", f"-I{ROOT / 'project/xc200/inc'}"])

        # 此目录只有 FreeRTOS 类型和日志配置，不提供任何 SDK 或产品头文件。
        portable = build / "portable"
        (portable / "freertos").mkdir(parents=True)
        shutil.copyfile(HERE / "include/freertos/FreeRTOS.h", portable / "freertos/FreeRTOS.h")
        shutil.copyfile(HERE / "include/xf_log_config.h", portable / "xf_log_config.h")
        probe = build / "public_headers.c"
        probe.write_text('''#include <assert.h>
#include "xf_uart_bsp.h"
#include "xf_mcu_protocol.h"
#include "xf_log.h"
#define LOCAL_TAG "test"
int main(void)
{
    int calls = 0;
    LOG_NR("%d", ++calls);
    LOG_N("%d", ++calls);
    LOG_I("%d", ++calls);
    LOG_E("%d", ++calls);
    assert(calls == (XF_LOG_ENABLE ? 4 : 0));

    return 0;
}
''')
        for enabled in (0, 1):
            run(f"headers_log_{enabled}", [probe], [f"-I{portable}", f"-DXF_LOG_ENABLE={enabled}"])
        print("PASS: public headers without SDK/product headers, log on/off")


if __name__ == "__main__":
    main()
