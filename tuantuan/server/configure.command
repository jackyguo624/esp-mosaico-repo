#!/bin/bash
# macOS local QR helper; all credentials stay in the ignored config/data files.
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
cd "$script_dir"
if [ ! -x .venv/bin/python ]; then
    printf '请先运行 start.command 完成电脑端安装。\n'
    read -r -p '按回车退出…' _answer
    exit 1
fi
if ! .venv/bin/python -c 'import qrcode' >/dev/null 2>&1; then
    .venv/bin/python -m pip install -r requirements-qr.txt
fi
printf '团团 · 本机配置二维码\n1. Wi-Fi / 电脑连接设置\n2. 模型 API Key\n'
read -r -p '请选择 [1]：' selection
kind=connection
if [ "$selection" = '2' ]; then kind=api-key; fi
.venv/bin/python make_qr.py --kind "$kind"
open "$script_dir/data/qr/$kind.svg"
printf '在团团上点“设置 → 扫码导入”，识别后检查并保存。\n'
read -r -p '按回车退出…' _answer
