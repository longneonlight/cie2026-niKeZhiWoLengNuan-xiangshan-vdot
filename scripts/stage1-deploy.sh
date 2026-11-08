#!/usr/bin/env bash
# 阶段一：环境部署与 hello 验证的可重放脚本
#
# 作用：把阶段一的关键步骤串成一条可重放命令，并把每一步的命令、耗时、退出码
#       与原始输出写进 evidence/stage1/stage1-deploy-<时间戳>.log。
#       报告 22-report-stage1.md §1、§4 引用的就是这个脚本与它的日志。
#
# 前提：环境已 provision（scripts/provision-2204.sh）、NEMU 与 RTL emu 已构建。
#       RTL 生成规模一项需要 build/rtl 存在（make verilog 过）。
#
# 用法：
#   bash scripts/stage1-deploy.sh
#   EMU=.../emu NEMU=.../nemu-standalone OUT=/tmp/out bash scripts/stage1-deploy.sh
#
# 退出码：0 = 全部检查通过；非 0 = 有一项失败（日志里能定位到具体命令）
set -uo pipefail

REPO=${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
OUT=${OUT:-$REPO/evidence/stage1}
# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
NEMU=${NEMU:-$ND/build/riscv64-nemu-interpreter}
CC=${CC:-riscv64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-riscv64-linux-gnu-objcopy}

STAMP=$(date +%Y%m%d-%H%M%S)
LOG="$OUT/stage1-deploy-$STAMP.log"
WORK=$(mktemp -d /tmp/stage1.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$OUT"

say() { printf '%s\n' "$*" | tee -a "$LOG"; }

run() {  # run <说明> <命令...>
  local desc=$1; shift
  say ""
  say "## $desc"
  say "\$ $*"
  local t0=$SECONDS rc
  "$@" 2>&1 | tee -a "$LOG"
  rc=${PIPESTATUS[0]}
  say "   -> exit=$rc  elapsed=$((SECONDS - t0))s"
  return $rc
}

fail=0

say "=== CIE-2026 vdot 阶段一：环境与 hello 验证（可重放） ==="
say "时间：$(date -Iseconds)"
say "仓库：$REPO"
say "日志：$LOG"

run "系统信息" bash -c 'uname -a; echo "cores: $(nproc)"; free -g | sed -n "1,2p"'
run "工具链" bash -c "$CC --version | head -1; verilator --version 2>/dev/null | head -1 || true"
run "关键产物是否存在" bash -c "ls -l '$EMU' '$NEMU'"

# 1) 编译 hello
run "编译 hello（裸机：-fno-pic/-fno-pie/-mcmodel=medany）" \
    "$CC" -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles -ffreestanding \
    -O2 -fno-pic -fno-pie -mcmodel=medany \
    -T "$REPO/tests/hello/link.ld" -o "$WORK/hello.elf" "$REPO/tests/hello/hello.c"
run "objcopy -> bin" "$OBJCOPY" -O binary "$WORK/hello.elf" "$WORK/hello.bin"

# 2) NEMU 上运行
run "NEMU 上运行 hello" bash -c "timeout 10 '$NEMU' -b '$WORK/hello.bin'"

# 3) RTL 上运行（程序结尾是 wfi 死循环，必须 timeout）
run "RTL 上运行 hello（timeout 120，退出码 124 属正常）" \
    bash -c "timeout 120 '$EMU' -i '$WORK/hello.bin' --no-diff; true"

# 4) RTL 生成规模
if [ -d "$XS/build/rtl" ]; then
  run "RTL 生成规模" bash -c "echo \"sv files: \$(find '$XS/build/rtl' -name '*.sv' | wc -l)\"; du -sh '$XS/build/rtl'"
else
  say ""
  say "## RTL 生成规模：跳过（$XS/build/rtl 不存在，先 make verilog）"
  fail=1
fi

# 5) 判定
say ""
say "## 判定"
hits=$(grep -c 'hello xiangshan' "$LOG" || true)
if [ "${hits:-0}" -ge 2 ]; then
  say "  [OK]   hello 在 NEMU 与 RTL 上各输出一次（共 $hits 次）"
else
  say "  [FAIL] hello 输出次数不足（$hits 次，期望 >= 2）"
  fail=1
fi

if [ "$fail" = "0" ]; then
  say ""
  say "OVERALL: PASS"
else
  say ""
  say "OVERALL: FAIL（见上面 [FAIL] 行）"
fi

say "日志已写入：$LOG"
exit "$fail"
