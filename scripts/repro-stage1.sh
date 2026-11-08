#!/usr/bin/env bash
# 阶段一复现：环境自检 → hello → vadd.vv → lane 隔离 → 机器码
#
# 对应文档：docs/08-stage1-vadd.md（§2 环境配置、§12 证据与复现）
#
# 用法：
#   bash scripts/repro-stage1.sh
#   XIANGSHAN_DIR=/opt/XiangShan NEMU_DIR=/opt/NEMU bash scripts/repro-stage1.sh
#
# 前提：RTL emu（$XIANGSHAN_DIR/build/emu）与 NEMU 解释器已构建；
#       环境怎么搭见文档 §2，或直接跑 scripts/provision-2204.sh。
#
# 产物：evidence/stage1/repro-<时间戳>.log
# 退出码：0 = 全部通过；1 = 有一项失败
set -uo pipefail

REPO=${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
NEMU=${NEMU:-$ND/build/riscv64-nemu-interpreter}
CC=${CC:-riscv64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-riscv64-linux-gnu-objcopy}
OBJDUMP=${OBJDUMP:-riscv64-linux-gnu-objdump}
OUT=${OUT:-$REPO/evidence/stage1}
STAMP=$(date +%Y%m%d-%H%M%S)
LOG=$OUT/repro-$STAMP.log
WORK=$(mktemp -d /tmp/repro1.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$OUT"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
fail=0
bad() { say "  [FAIL] $*"; fail=1; }
ok()  { say "  [OK]   $*"; }

CFLAGS="-march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles -ffreestanding \
-O2 -fno-pic -fno-pie -mcmodel=medany -T $REPO/tests/hello/link.ld"

say "=== CIE-2026 vdot 阶段一复现 ==="
say "时间：$(date -Iseconds)"
say "仓库：$REPO"
say "日志：$LOG"

# ---------- 1. 环境自检 ----------
say ""
say "## 1. 环境自检"
for t in "$CC" "$OBJCOPY" "$OBJDUMP"; do
  if command -v "$t" >/dev/null 2>&1; then ok "工具链 $t"; else bad "缺少 $t（apt install gcc-riscv64-linux-gnu）"; fi
done
for f in "$EMU" "$NEMU"; do
  if [ -x "$f" ]; then ok "产物 $f"; else bad "缺少 $f（先构建香山 emu / NEMU）"; fi
done
if command -v verilator >/dev/null 2>&1; then
  say "  [INFO] $(verilator --version 2>/dev/null | head -1)"
else
  say "  [INFO] 没有 verilator（只在重建 RTL 时需要）"
fi
[ "$fail" = "0" ] || { say ""; say "OVERALL: FAIL（环境不全，后续步骤无意义）"; exit 1; }

run() {  # run <标题> <命令...>
  say ""
  say "## $1"; shift
  say "\$ $*"
  "$@" 2>&1 | tee -a "$LOG"
  local rc=${PIPESTATUS[0]}
  say "   -> exit=$rc"
  return $rc
}

# ---------- 2. hello：两端跑通 ----------
say ""
say "## 2. hello（两端跑通）"
$CC $CFLAGS -o "$WORK/hello.elf" "$REPO/tests/hello/hello.c" 2>>"$LOG" \
  && $OBJCOPY -O binary "$WORK/hello.elf" "$WORK/hello.bin" || bad "hello 编译失败"
h_nemu=$(timeout 30 "$NEMU" -b "$WORK/hello.bin" 2>&1 | tee -a "$LOG" | grep -c 'hello xiangshan')
h_rtl=$(timeout 180 "$EMU" -i "$WORK/hello.bin" -F "$REPO/tests/flash/stub.bin" --no-diff 2>&1 | tee -a "$LOG" | grep -c 'hello xiangshan')
[ "$h_nemu" -ge 1 ] && ok "NEMU 打印 hello" || bad "NEMU 没有打印 hello"
[ "$h_rtl"  -ge 1 ] && ok "RTL 打印 hello"  || bad "RTL 没有打印 hello"

# ---------- 3. vadd.vv：两端结果一致 ----------
say ""
say "## 3. vadd.vv（两端结果一致）"
$CC $CFLAGS -o "$WORK/vadd.elf" "$REPO/tests/hello/vadd.c" 2>>"$LOG" \
  && $OBJCOPY -O binary "$WORK/vadd.elf" "$WORK/vadd.bin" || bad "vadd 编译失败"
v_nemu=$(timeout 30 "$NEMU" -b "$WORK/vadd.bin" 2>&1 | tee -a "$LOG" | grep -aE 'vadd.*=' | head -1)
v_rtl=$(timeout 180 "$EMU" -i "$WORK/vadd.bin" -F "$REPO/tests/flash/stub.bin" --no-diff 2>&1 | tee -a "$LOG" | grep -aE 'vadd.*=' | head -1)
say "  NEMU: ${v_nemu:-<无输出>}"
say "  RTL : ${v_rtl:-<无输出>}"
if [ -n "$v_nemu" ] && [ "$v_nemu" = "$v_rtl" ]; then ok "两端输出一致"; else bad "两端输出不一致或缺失"; fi

# ---------- 4. lane 隔离 ----------
say ""
say "## 4. lane 隔离（字节之间不进位）"
$CC $CFLAGS -o "$WORK/iso.elf" "$REPO/tests/hello/vadd_lane_isolation.c" 2>>"$LOG" \
  && $OBJCOPY -O binary "$WORK/iso.elf" "$WORK/iso.bin" || bad "lane 隔离程序编译失败"
iso=$(timeout 180 "$EMU" -i "$WORK/iso.bin" -F "$REPO/tests/flash/stub.bin" --no-diff 2>&1 | tee -a "$LOG" | grep -acE 'PASS')
[ "${iso:-0}" -ge 1 ] && ok "lane 隔离用例通过（$iso 条 PASS）" || bad "lane 隔离用例未通过"

# ---------- 5. 机器码 ----------
say ""
say "## 5. 机器码"
run "反汇编里的 vadd.vv" bash -c "$OBJDUMP -d '$WORK/vadd.elf' | grep -E 'vadd|vsetvli' | head -6" || true

say ""
if [ "$fail" = "0" ]; then say "OVERALL: PASS"; else say "OVERALL: FAIL"; fi
say "日志：$LOG"
exit "$fail"
