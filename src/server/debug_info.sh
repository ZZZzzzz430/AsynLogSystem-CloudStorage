#!/usr/bin/env bash
# 从任何目录启动；配置文件中的相对路径要求工作目录为 src/server。
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
for tool in make g++ gdb; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "缺少工具：$tool，请先安装。" >&2
        exit 1
    fi
done
make gdb_test
exec gdb -q -x debug_info.gdb ./gdb_test
