#!/bin/bash
# 在 RTL 仿真器（Verilator 构建的 build/emu）上运行测试程序
# 用法: bash run-on-rtl.sh <image.bin> [--diff]
set -u

# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
REF=${REF:-$ND/build/riscv64-nemu-interpreter-so}
IMG=${1:?usage: run-on-rtl.sh <image.bin> [--diff]}

[ -x "$EMU" ] || { echo "emu not built: $EMU"; exit 1; }
[ -f "$IMG" ] || { echo "image not found: $IMG"; exit 1; }

echo "=== emu: $EMU ==="
if [ "${2:-}" = "--diff" ]; then
  [ -f "$REF" ] || { echo "ref model not found: $REF"; exit 1; }
  echo "=== diff model: $REF ==="
  timeout 300 "$EMU" -i "$IMG" --diff "$REF" 2>&1
else
  timeout 300 "$EMU" -i "$IMG" 2>&1
fi
echo "=== exit: $? ==="
