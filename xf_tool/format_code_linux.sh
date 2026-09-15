#!/usr/bin/env bash
# 格式化公共 C 代码及产品代码；可传入一个产品目录名，省略时处理全部产品。
set -euo pipefail

if (( $# > 1 )); then
    printf '用法：%s [产品目录名]\n' "$0" >&2
    exit 2
fi

tool_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "$tool_dir/.." && pwd)
project_dir="$root_dir/project"

if (( $# == 1 )); then
    if [[ ! $1 =~ ^[A-Za-z0-9][A-Za-z0-9_-]*$ ]]; then
        printf '错误：产品参数必须是 project 下的目录名。\n' >&2
        exit 2
    fi
    project_dir="$project_dir/$1"
fi

if [[ ! -d $project_dir || -L $project_dir ]]; then
    printf '错误：产品路径必须是现有目录，不能是符号链接：%s\n' "$project_dir" >&2
    exit 2
fi

if ! command -v astyle >/dev/null 2>&1; then
    printf '错误：未找到 astyle，请先安装 Artistic Style（Ubuntu/Debian：sudo apt install astyle）。\n' >&2
    exit 127
fi

# 固定采用此处的参数，避免用户目录或其他项目的 AStyle 配置影响结果。
format_options=(
    --options=none
    --project=none
    --suffix=none
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
    --formatted
)

# find 默认不跟随符号链接；-exec ... {} + 安全传递带空格的路径，并在工具失败时返回非零。
# SDK、LVGL 子模块和 ref 不在搜索根目录中；产品内跳过生成资源和 LVGL 配置模板。
find "$root_dir/xf_common/main" "$root_dir/xf_common/components/common" "$project_dir" \
    -type d \( -name build -o -name images -o -name managed_components -o -name .git \) -prune -o \
    -type f \( -name '*.c' -o -name '*.h' \) ! -name lv_conf.h \
    -exec astyle "${format_options[@]}" {} +
