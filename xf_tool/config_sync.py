#!/usr/bin/env python3
"""将菜单保存的 sdkconfig 回写为芯片公共配置或产品差异配置。"""

import argparse
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import re
import tempfile


class ConfigSyncError(Exception):
    """配置无法等价还原，保持维护文件不变。"""


ASSIGNMENT = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=.*$")
UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def render_config(original, assignments, scope):
    """保留已有注释，更新差异项，移除不再需要的覆盖项。"""
    pending = dict(assignments)
    lines = []
    for line in original.splitlines():
        match = ASSIGNMENT.fullmatch(line.strip()) or UNSET.fullmatch(line.strip())
        if match:
            key = match[1]
            if key in pending:
                lines.append(pending.pop(key))
        else:
            lines.append(line)
    if pending:
        comment = "# menuconfig 保存的公共配置。" if scope == "common" else "# menuconfig 保存的产品差异。"
        lines.extend(["", comment, *pending.values()])
    return "\n".join(lines).rstrip() + "\n"


def calculate_config(build_dir, common, private, work_dir):
    # 使用 SDK 自己的 Kconfig 引擎处理依赖、choice、默认值和旧选项改名。
    import esp_kconfiglib.core as kconfiglib
    from esp_kconfiglib.deprecated import load_rename_files_from_env
    from kconfgen.core import get_json_values

    context = json.loads((build_dir / "config.env").read_text(encoding="utf-8"))
    os.environ.update(context)
    os.environ.setdefault("IDF_INIT_VERSION", context["IDF_VERSION"])
    os.environ.setdefault("IDF_MINIMAL_BUILD", "n")
    os.environ["KCONFIG_REPORT_VERBOSITY"] = "quiet"
    os.environ["KCONFIG_DEFAULTS_POLICY"] = "sdkconfig"
    sdk_dir = Path(context["IDF_PATH"])
    config = None

    common_text = common.read_text(encoding="utf-8")
    destination = private if private is not None else common
    original = destination.read_text(encoding="utf-8")
    saved_text = (build_dir / "sdkconfig").read_text(encoding="utf-8")
    baseline_file, saved_file = work_dir / "common.conf", work_dir / "saved.config"
    baseline_file.write_text(common_text if private is not None else "", encoding="utf-8")
    saved_file.write_text(saved_text, encoding="utf-8")

    def values():
        result = get_json_values(config)
        # SDK 初始化版本是生成文件元数据，不属于产品配置。
        result.pop("IDF_INIT_VERSION", None)
        return result

    def reset(override=None):
        nonlocal config
        # SDK 会记住 sdkconfig 中的默认值标记。每次使用新实例，才能真实验证冷启动还原。
        config = kconfiglib.Kconfig(
            str(sdk_dir / "Kconfig"),
            parser_version=int(os.environ.get("KCONFIG_PARSER_VERSION", "1")),
            print_report=False,
        )
        load_rename_files_from_env(config, sdkconfig_rename=str(sdk_dir / "sdkconfig.rename"), list_separator="semicolon")
        config.load_config(str(baseline_file), replace=True, print_report=False)
        if override is not None:
            config.load_config(str(override), replace=False, print_report=False)
        return values()

    desired = reset(saved_file)
    # 条件默认值可能随产品功能变化；例如启用 PSRAM 后看门狗默认超时会改变。
    # 即使最终值等于公共基线，也可能需要保留 SDK 最小配置中的显式覆盖。
    minimum_file = work_dir / "sdk-minimum.conf"
    config.write_min_config(str(minimum_file), normalize_unset=True)
    minimum_keys = {match[1] for line in minimum_file.read_text(encoding="utf-8").splitlines()
                    if (match := ASSIGNMENT.fullmatch(line))}
    assignments = {}
    for symbol in config.unique_defined_syms:
        if symbol.name == "IDF_TARGET" or not any(node.prompt for node in symbol.nodes):
            continue
        # config_string 可能包含 SDK 的 "# default:" 元数据，只保留真正的配置行。
        line = next((item.strip() for item in symbol.config_string.splitlines()
                     if ASSIGNMENT.fullmatch(item.strip()) or UNSET.fullmatch(item.strip())), "")
        if not line:
            continue
        unset = UNSET.fullmatch(line)
        if unset:
            line = f"{unset[1]}=n"
        assignments[f"CONFIG_{symbol.name}"] = line

    baseline = reset()
    # 公共配置中明确写出的约定即使等于 SDK 默认值也保留，避免菜单退出时失去这些约定。
    existing = {}
    if private is None:
        for line in original.splitlines():
            match = ASSIGNMENT.fullmatch(line.strip()) or UNSET.fullmatch(line.strip())
            if match:
                existing[match[1]] = line.strip()
    candidates = {key: line for key, line in assignments.items()
                  if key in existing or key in minimum_keys or desired.get(key[7:]) != baseline.get(key[7:])}
    candidate_file = work_dir / "candidate.conf"

    differences = []

    def matches(items):
        candidate_file.write_text("\n".join(items.values()) + "\n", encoding="utf-8")
        actual = reset(candidate_file)
        differences[:] = sorted(key for key in actual.keys() | desired.keys() if actual.get(key) != desired.get(key))
        return not differences

    if not matches(candidates):
        raise ConfigSyncError(f"保存的配置无法完整还原，差异项：{', '.join(differences[:12])}；未回写配置文件。")

    # 去掉由依赖和默认值自动推导出的项，避免把整份 sdkconfig 搬进私有文件。
    for key in list(candidates):
        if key in existing:
            continue
        smaller = {name: line for name, line in candidates.items() if name != key}
        if matches(smaller):
            candidates = smaller

    # 当前公共上下文没有加载的自定义选项原样保留，不因不可见而丢失。
    candidates.update({key: line for key, line in existing.items() if key not in assignments})
    result = render_config(original, candidates, "private" if private is not None else "common")
    candidate_file.write_text(result, encoding="utf-8")
    if reset(candidate_file) != desired:
        raise ConfigSyncError("整理后的配置未通过完整配置一致性检查；未回写。")
    if common.read_text(encoding="utf-8") != common_text or destination.read_text(encoding="utf-8") != original:
        raise ConfigSyncError("同步期间公共或私有文件发生变化，请重新执行 menuconfig。")
    if (build_dir / "sdkconfig").read_text(encoding="utf-8") != saved_text:
        raise ConfigSyncError("同步期间 sdkconfig 发生变化，请重新执行 menuconfig。")
    return original, result, len(candidates)


def sync_config(build_dir, common, private=None):
    destination = private if private is not None else common
    label = "产品差异" if private is not None else "芯片公共"
    messages = io.StringIO()
    with tempfile.TemporaryDirectory(prefix="xf-config-sync-") as directory:
        with redirect_stdout(messages), redirect_stderr(messages):
            original, result, count = calculate_config(build_dir, common, private, Path(directory))
    if original == result:
        print(f"{label}配置已同步，无需回写：{destination}", flush=True)
        return

    # 验证通过后才保存，保留上一次维护文件，并在同一目录原子替换。
    backup = build_dir / "config_sync" / f"{destination.name}.before-sync"
    backup.parent.mkdir(parents=True, exist_ok=True)
    backup.write_text(original, encoding="utf-8")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(result)
        temporary.chmod(destination.stat().st_mode & 0o777)
        os.replace(temporary, destination)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    print(f"已回写 {count} 项{label}配置：{destination}\n原文件备份：{backup}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--common", required=True, type=Path)
    parser.add_argument("--private", type=Path, help="提供时回写产品差异；省略时回写 --common 文件")
    args = parser.parse_args()
    try:
        sync_config(args.build_dir, args.common, args.private)
        return 0
    except Exception as error:
        # SDK 的解析异常也必须报错；保持已保存的 sdkconfig 和原私有文件可恢复。
        print(f"配置回写失败：{error}\n已保存的配置仍位于 {args.build_dir / 'sdkconfig'}，请勿 clean。", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
