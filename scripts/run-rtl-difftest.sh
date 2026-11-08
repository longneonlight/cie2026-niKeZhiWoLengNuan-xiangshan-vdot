#!/bin/bash
# 带 difftest 运行香山 RTL 仿真器
# 关键：必须用 -F 指定 flash 存根，否则参考模型的 flash 内容会错位（见 docs/13-difftest-debug.md）
set -u

REPO=${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
REF=${REF:-$ND/build/riscv64-nemu-interpreter-so}
STUB=${STUB:-$REPO/tests/flash/stub.bin}
IMG=${1:?usage: run-rtl-difftest.sh <image.bin> [timeout_sec]}

mkdir -p "$(dirname "$STUB")"
[ -f "$STUB" ] || printf '\x9b\x02\x10\x00\x93\x92\xf2\x01\x67\x80\x02\x00' > "$STUB"

[ -x "$EMU" ] || { echo "emu not built: $EMU"; exit 1; }
[ -f "$REF" ] || { echo "ref model missing: $REF"; exit 1; }
[ -f "$IMG" ] || { echo "image not found: $IMG"; exit 1; }

echo "emu : $EMU"
echo "ref : $REF"
echo "flash: $STUB"
echo "image: $IMG"
echo "----------------------------------------"
timeout "${2:-120}" "$EMU" -i "$IMG" -F "$STUB" --diff "$REF" 2>&1
