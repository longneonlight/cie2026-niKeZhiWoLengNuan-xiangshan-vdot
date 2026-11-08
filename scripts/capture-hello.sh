#!/usr/bin/env bash
# 采集阶段一的 hello 证据：编译裸机程序，在 NEMU 与香山 RTL 上各跑一遍，
# 结果写到 evidence/stage1/ 下。
#
# 用法： bash scripts/capture-hello.sh
# 可覆盖的环境变量：
#   XIANGSHAN_DIR  香山工作区（默认 $HOME/XiangShan）
#   NEMU_DIR       NEMU 工作区（默认 $HOME/NEMU）
#   EMU            仿真器可执行文件（默认 $XIANGSHAN_DIR/build/emu）
#   NEMU           NEMU 解释器（默认 $NEMU_DIR/build/riscv64-nemu-interpreter）
set -u

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
NEMU=${NEMU:-$ND/build/riscv64-nemu-interpreter}
CC=${CC:-riscv64-linux-gnu-gcc}
OC=${OC:-riscv64-linux-gnu-objcopy}
OUT=$REPO/evidence/stage1
W=${WORK:-/tmp/hello-build}
mkdir -p "$OUT" "$W"

echo "==> 编译 hello"
$CC -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles -ffreestanding -O2 \
    -fno-pic -fno-pie -mcmodel=medany \
    -T "$REPO/tests/hello/link.ld" "$REPO/tests/hello/hello.c" -o "$W/hello.elf" || exit 1
$OC -O binary "$W/hello.elf" "$W/hello.bin" || exit 1
ls -l "$W/hello.elf" "$W/hello.bin"

echo "==> NEMU 上运行"
timeout 30 "$NEMU" -b "$W/hello.bin" > "$W/nemu.out" 2>&1
grep -a 'hello xiangshan' "$W/nemu.out" || tail -5 "$W/nemu.out"

echo "==> RTL 上运行"
timeout 120 "$EMU" -i "$W/hello.bin" --no-diff > "$W/rtl.out" 2>&1
grep -a 'hello xiangshan' "$W/rtl.out" || tail -5 "$W/rtl.out"

{
  echo "=== hello 在 NEMU 上 ==="
  echo "$(date -Iseconds)"
  echo
  echo "\$ $NEMU -b hello.bin"
  grep -a 'hello xiangshan' "$W/nemu.out" || tail -5 "$W/nemu.out"
} > "$OUT/hello-nemu.log"

{
  echo "=== hello 在香山 RTL 上 ==="
  echo "$(date -Iseconds)"
  echo
  echo "\$ $EMU -i hello.bin --no-diff"
  grep -a 'hello xiangshan' "$W/rtl.out" || tail -5 "$W/rtl.out"
} > "$OUT/hello-rtl.log"

echo "==> 完成，日志：$OUT/hello-nemu.log、$OUT/hello-rtl.log"
