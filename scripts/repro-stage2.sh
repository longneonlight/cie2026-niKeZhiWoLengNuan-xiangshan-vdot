#!/usr/bin/env bash
# 阶段二复现：编码不撞车 → 反汇编核对 → 基础测试（功能 / 非法 / vstart）
#
# 对应文档：docs/33-stage2-explained.md（§1–§3 编码、§6 测试）
#
# 用法：
#   bash scripts/repro-stage2.sh
#   XIANGSHAN_DIR=... NEMU_DIR=... bash scripts/repro-stage2.sh
#
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
OUT=${OUT:-$REPO/evidence/stage2}
STAMP=$(date +%Y%m%d-%H%M%S)
LOG=$OUT/repro-$STAMP.log
WORK=$(mktemp -d /tmp/repro2.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$OUT"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
fail=0
bad() { say "  [FAIL] $*"; fail=1; }
ok()  { say "  [OK]   $*"; }

CFLAGS="-march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles -ffreestanding \
-O2 -fno-pic -fno-pie -mcmodel=medany -T $REPO/tests/hello/link.ld"

say "=== CIE-2026 vdot 阶段二复现 ==="
say "时间：$(date -Iseconds)"
say "日志：$LOG"

# ---------- 1. 译码表两两互斥 ----------
say ""
say "## 1. 译码表两两互斥（新指令不会改变既有指令）"
if command -v python3 >/dev/null 2>&1 && [ -f "$REPO/scripts/check-decode-disjoint.py" ]; then
  python3 "$REPO/scripts/check-decode-disjoint.py" --xs "$XS" 2>&1 | tee -a "$LOG" | tail -3
  [ "${PIPESTATUS[0]}" = "0" ] && ok "互斥核对通过" || bad "互斥核对失败"
else
  say "  [SKIP] 需要 python3 与脚本 scripts/check-decode-disjoint.py"
fi

# ---------- 2. 反汇编核对编码槽 ----------
say ""
say "## 2. 反汇编核对（funct3=010 + funct6=111110 + vm=1）"
cat > "$WORK/enc.S" <<'EOF'
.text
.globl _start
_start:
  .word 0xFA20A1D7     # vdot.vv  vd=v3, vs1=v1, vs2=v2
  .word 0xFA20E1D7     # 对照：funct3=110，属 vwmaccus.vx 槽
  .word 0xC220A1D7     # 对照：vwaddu.vv（选错槽时占的就是它）
EOF
$CC $CFLAGS -o "$WORK/enc.elf" "$WORK/enc.S" 2>>"$LOG"
$OBJDUMP -d "$WORK/enc.elf" | grep -A1 '<_start>' | tee -a "$LOG"
if $OBJDUMP -d "$WORK/enc.elf" | grep -q 'fa20a1d7'; then ok "0xFA20A1D7 在镜像里（反汇编器不认识，属预期）"; else bad "镜像里没有 0xFA20A1D7"; fi
if $OBJDUMP -d "$WORK/enc.elf" | grep -q 'vwaddu'; then ok "0xC220A1D7 反汇编为 vwaddu.vv（说明它已被标准指令占用）"; else say "  [INFO] 反汇编器未给出 vwaddu.vv 助记符"; fi

# ---------- 3. 基础测试 ----------
run_test() {  # run_test <标题> <源文件> <期望关键字>
  local name=$1 src=$2 pat=$3
  say ""
  say "## $name"
  $CC $CFLAGS -o "$WORK/$name.elf" "$src" 2>>"$LOG" || { bad "$name 编译失败"; return; }
  $OBJCOPY -O binary "$WORK/$name.elf" "$WORK/$name.bin"
  local out
  out=$(timeout 300 "$EMU" -i "$WORK/$name.bin" -F "$REPO/tests/flash/stub.bin" --no-diff 2>&1 | tee -a "$LOG")
  if printf '%s' "$out" | grep -qa "$pat"; then ok "$pat"; else bad "没看到 $pat"; fi
}

run_test vdot_basic   "$REPO/tests/vdot/vdot_basic_tests.c"    'RESULT: PASS'
run_test vdot_illegal "$REPO/tests/vdot/vdot_illegal_tests.c"  'RESULT: PASS'
run_test vdot_vstart  "$REPO/tests/vdot/vdot_vstart_test.c"    'RESULT: PASS'

say ""
if [ "$fail" = "0" ]; then say "OVERALL: PASS"; else say "OVERALL: FAIL"; fi
say "日志：$LOG"
exit "$fail"
