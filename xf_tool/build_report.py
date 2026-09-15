#!/usr/bin/env python3
"""从 ESP-IDF 构建产物生成分区和静态资源占用摘要。"""

import csv
import io
import json
import os
from pathlib import Path
import subprocess
import sys


class ReportError(Exception):
    """构建产物或 SDK 统计结果不可用。"""


def tool_output(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ReportError(f"{Path(command[1]).name} 执行失败：{detail}")
    return result.stdout


def parse_size(value):
    value = value.strip().upper()
    if value.endswith(("K", "M")):
        return int(value[:-1], 0) * (1024 if value[-1] == "K" else 1024 * 1024)
    return int(value, 0)


def read_partitions(build_dir, flasher, sdk_dir):
    table = flasher["partition-table"]
    table_offset = int(table["offset"], 0)
    command = [
        sys.executable, str(sdk_dir / "components/partition_table/gen_esp32part.py"),
        "--offset", hex(table_offset),
    ]
    bootloader = flasher.get("bootloader")
    if bootloader:
        command.extend(["--primary-bootloader-offset", bootloader["offset"]])
    command.append(str(build_dir / table["file"]))
    csv_text = tool_output(command)
    partitions = []
    for row in csv.reader(io.StringIO(csv_text)):
        if not row or row[0].lstrip().startswith("#"):
            continue
        if len(row) < 5:
            raise ReportError("SDK 返回的分区表格式不完整。")
        name, kind, _, offset, size = (field.strip() for field in row[:5])
        partitions.append({"name": name, "type": kind, "offset": parse_size(offset), "size": parse_size(size)})
    if not partitions:
        raise ReportError("生成的分区表为空。")

    # 普通 CSV 不包含这两个保留区域；偏移来自烧录元数据。
    # ESP-IDF 的分区表固定保留一个 4 KiB Flash 扇区。
    reserved = [{"name": "partition_table", "type": "partition_table", "offset": table_offset, "size": 0x1000}]
    if bootloader:
        offset = int(bootloader["offset"], 0)
        reserved.append({"name": "bootloader", "type": "bootloader", "offset": offset, "size": table_offset - offset})
    for item in reserved:
        if item["size"] > 0 and not any(p["offset"] == item["offset"] for p in partitions):
            partitions.append(item)
    return sorted(partitions, key=lambda item: item["offset"])


def memory_rows(partitions, flasher, app_size, size_info, config):
    """保留官方内存分类，不重复累加 IRAM/DRAM 的共享地址窗口。"""
    app_offset = int(flasher["app"]["offset"], 0)
    app = next((p for p in partitions if p["offset"] == app_offset and p["type"] in ("app", "0", "0x00")), None)
    if app is None:
        raise ReportError(f"分区表中找不到烧录地址 {app_offset:#x} 对应的 APP 分区。")
    rows = [(f"FLASH ({app['name']})", app["size"], app_size)]
    external_seen = False
    for item in size_info["layout"]:
        name, total, used = item["name"], item["total"], item["used"]
        if name in ("Flash Code", "Flash Data"):
            continue  # 已使用实际 BIN 大小统计整个 APP，不重复统计其映射段。
        if name in ("External RAM", "SPI DRAM"):
            external_seen = True
            if config.get("SPIRAM") or used:
                # 链接器映射窗口不代表板载 PSRAM 的物理容量。
                rows.append(("PSRAM", None, used))
        elif total or used:
            rows.append((name, total or None, used))
    if config.get("SPIRAM") and not external_seen:
        rows.append(("PSRAM", None, None))
    return rows


def human_size(size):
    return f"{size // 1024}K" if size % 1024 == 0 else f"{size} B"


def render_report(project, flash_size, partitions, rows):
    width = max(24, *(len(p["name"]) for p in partitions), *(len(row[0]) for row in rows))
    line_width = max(68, width + 44)
    lines = ["", " Partitions Table ".center(line_width, "="),
             f"{project['project_name']} / {project['target']}    Flash: {flash_size}",
             f"{'Name':<{width}} {'Offset':>12} {'Size':>14}", "-" * line_width]
    for item in partitions:
        offset = f"0x{item['offset']:08x}"
        lines.append(f"{item['name']:<{width}} {offset:>12} {human_size(item['size']):>14}")
    lines.extend(["", " APP Memory Info ".center(line_width, "="),
                  f"{'Name':<{width}} {'Size':>12} {'Used':>14} {'Usage':>9}", "-" * line_width])
    for name, total, used in rows:
        total_text = f"0x{total:08x}" if total is not None else "N/A"
        used_text = f"{used} B" if used is not None else "N/A"
        usage = f"{used / total * 100:.2f}%" if total and used is not None else "N/A"
        lines.append(f"{name:<{width}} {total_text:>12} {used_text:>14} {usage:>9}")
    lines.extend(["-" * line_width,
                  "FLASH: 实际 APP BIN / 对应 APP 分区；分区表 Size 为分配容量。",
                  "RAM: 链接时静态占用 / 链接可用容量；动态分配需运行时统计。"])
    if any(name == "DIRAM" for name, _, _ in rows):
        lines.append("DIRAM: 指令和数据共用的内部 RAM，按 ESP-IDF 分类统计。")
    if any(name == "PSRAM" for name, _, _ in rows):
        lines.append("PSRAM: 仅统计静态段；物理容量运行时识别，动态缓冲区不在本表统计范围。")
    return "\n".join(lines) + "\n"


def build_report(build_dir, sdk_dir):
    project = json.loads((build_dir / "project_description.json").read_text())
    flasher = json.loads((build_dir / "flasher_args.json").read_text())
    config = json.loads((build_dir / "config/sdkconfig.json").read_text())
    partitions = read_partitions(build_dir, flasher, sdk_dir)
    app_size = (build_dir / flasher["app"]["file"]).stat().st_size
    map_file = (build_dir / project["app_elf"]).with_suffix(".map")
    size_info = json.loads(tool_output([
        sys.executable, str(sdk_dir / "tools/idf_size.py"), "--format", "json2", "--show-unused", str(map_file),
    ]))
    rows = memory_rows(partitions, flasher, app_size, size_info, config)
    return render_report(project, config.get("ESPTOOLPY_FLASHSIZE", "N/A"), partitions, rows)


def main():
    try:
        print(build_report(Path(sys.argv[1]), Path(os.environ["IDF_PATH"])), flush=True)
        return 0
    except (ReportError, OSError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f"构建摘要生成失败：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
