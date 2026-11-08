#!/bin/bash
# 一键回归：把 vdot 的全部验证重跑一遍并汇总
#
# 用法（在 VM 上）：
#   bash scripts/run-all.sh            # 全量
#   bash scripts/run-all.sh quick      # 跳过 random(100) 与 perf
#
# 产物：~/evidence/regress/
#   00-summary.txt      汇总表（每项 PASS/FAIL + checksum）
#   <test>.log          各项原始日志
#
# 退出码：0 = 全部通过；1 = 有失败
#
# 说明：emu 在程序跑完后不会自己退出（`_start` 结尾是 wfi 死循环），
# 所以这里不用固定 timeout 硬等，而是**轮询日志里的结果关键字，出现即杀进程**，
# 既快又不会因为 timeout 太短而误判。
set -u

# 仓库根：默认取脚本所在目录的上一级，不依赖任何绝对路径
REPO=${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
# 香山与 NEMU 工作区：用环境变量指定，默认放在 $HOME 下
# 工作区解析顺序：显式环境变量 → xs-env 的 setup.sh 导出 → 默认位置
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
EMU=${EMU:-$XS/build/emu}
REF=${REF:-$ND/build/riscv64-nemu-interpreter-so}
NEMU=${NEMU:-$ND/build/riscv64-nemu-interpreter}
STUB=${STUB:-$REPO/tests/flash/stub.bin}
WORK=${WORK:-$REPO/tests/hello}
OUT=${OUT:-$REPO/evidence/regress}
MODE=${1:-full}

CC="riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
    -ffreestanding -O2 -fno-pic -fno-pie -mcmodel=medany -T $WORK/link.ld"

rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK" || exit 1

fail=0
declare -a NAMES=() RESULTS=() DETAILS=()

record () {  # name, result, detail
  NAMES+=("$1"); RESULTS+=("$2"); DETAILS+=("$3")
  [ "$2" = "PASS" ] || fail=1
}

build () {  # src, out
  $CC -o "$2.elf" "$1" 2>/dev/null || { record "$2" "FAIL" "compile error"; return 1; }
  riscv64-linux-gnu-objcopy -O binary "$2.elf" "$2.bin"
}

# run_watch <log> <max_wait_s> <pattern> <cmd...>
#   启动 cmd，轮询 log 直到出现 pattern（或超时），然后杀掉进程
run_watch () {
  local log=$1 wait_s=$2 pat=$3; shift 3
  : > "$log"
  "$@" > "$log" 2>&1 &
  local pid=$!
  local t=0
  while [ "$t" -lt "$wait_s" ]; do
    if grep -qaE "$pat" "$log" 2>/dev/null; then
      sleep 4                      # 让最后几行落盘（UART 慢，必须等）
      kill "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
      return 0
    fi
    sleep 2; t=$((t+2))
  done
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
  return 1
}

echo "=== vdot regression $(date -Iseconds) mode=$MODE ==="
echo "emu: $(ls -la "$EMU" | awk '{print $5, $6, $7, $8}')"

# ---------- 0) 定版检查 ----------
dbg=$(strings "$EMU" | grep -ac VDOTDBG || true)
if [ "$dbg" = "0" ]; then record "build-clean(VDOTDBG=0)" PASS "0"; else record "build-clean(VDOTDBG=0)" FAIL "found $dbg"; fi

# ---------- 1) 定向功能（difftest） ----------
if build "$REPO/tests/vdot/vdot_basic_tests.c" vdot_basic; then
  run_watch "$OUT/basic-diff.log" 600 'RESULT:' "$EMU" -i vdot_basic.bin -F "$STUB" --diff "$REF" -C 300000
  m=$(grep -ac 'different at' "$OUT/basic-diff.log" || true)
  if grep -qa 'RESULT: PASS' "$OUT/basic-diff.log" && [ "$m" = "0" ]; then
    record "basic-9case(difftest)" PASS "9/9 mismatch=0"
  else
    record "basic-9case(difftest)" FAIL "$(grep -a 'RESULT:' "$OUT/basic-diff.log" | head -1) mismatch=$m"
  fi
fi

# ---------- 2) 非法条件（--no-diff） ----------
if build "$REPO/tests/vdot/vdot_illegal_tests.c" vdot_illegal; then
  run_watch "$OUT/illegal.log" 600 'RESULT:' "$EMU" -i vdot_illegal.bin -F "$STUB" --no-diff
  if grep -qa 'RESULT: PASS' "$OUT/illegal.log"; then
    record "illegal-4cond(no-diff)" PASS "4/4"
  else
    record "illegal-4cond(no-diff)" FAIL "see illegal.log"
  fi
fi

# ---------- 3) vstart 行为（--no-diff） ----------
if build "$REPO/tests/vdot/vdot_vstart_test.c" vdot_vstart; then
  run_watch "$OUT/vstart.log" 600 'RESULT:' "$EMU" -i vdot_vstart.bin -F "$STUB" --no-diff
  if grep -qa 'RESULT: PASS' "$OUT/vstart.log"; then
    record "vstart(no-diff)" PASS "3/3"
  else
    record "vstart(no-diff)" FAIL "see vstart.log"
  fi
fi

# ---------- 4) 随机对拍（三方） ----------
if [ "$MODE" != "quick" ]; then
  if build "$REPO/tests/vdot/vdot_random_tests.c" vdot_random; then
    run_watch "$OUT/random-rtl.log" 900 'RESULT:' "$EMU" -i vdot_random.bin -F "$STUB" --no-diff
    run_watch "$OUT/random-diff.log" 900 'RESULT:' "$EMU" -i vdot_random.bin -F "$STUB" --diff "$REF" -C 500000
    timeout 120 "$NEMU" -b vdot_random.bin > "$OUT/random-nemu.log" 2>&1 || true

    ck_rtl=$(grep -a 'fails=' "$OUT/random-rtl.log" | head -1 | tr -d ' \r')
    ck_diff=$(grep -a 'fails=' "$OUT/random-diff.log" | head -1 | tr -d ' \r')
    ck_nemu=$(grep -a 'fails=' "$OUT/random-nemu.log" | head -1 | tr -d ' \r')
    m=$(grep -ac 'different at' "$OUT/random-diff.log" || true)

    if echo "$ck_rtl"  | grep -q 'fails=0' && \
       echo "$ck_diff" | grep -q 'fails=0' && \
       echo "$ck_nemu" | grep -q 'fails=0' && [ "$m" = "0" ] && \
       [ "${ck_rtl##*checksum=}"  = "${ck_nemu##*checksum=}" ] && \
       [ "${ck_diff##*checksum=}" = "${ck_nemu##*checksum=}" ]; then
      record "random-100(3-way)" PASS "checksum=${ck_nemu##*checksum=}"
    else
      record "random-100(3-way)" FAIL "rtl[$ck_rtl] diff[$ck_diff] nemu[$ck_nemu] mm=$m"
    fi
  fi

  # ---------- 5) 性能 ----------
  if build "$REPO/tests/baseline/perf_latency.c" perf; then
    run_watch "$OUT/perf.log" 1200 'RESULT: DONE' "$EMU" -i perf.bin -F "$STUB" --no-diff
    if grep -qa 'RESULT: DONE' "$OUT/perf.log"; then
      record "perf-3metric(no-diff)" PASS "$(grep -a 'result = ' "$OUT/perf.log" | head -1 | tr -d ' ')"
    else
      record "perf-3metric(no-diff)" FAIL "see perf.log"
    fi
  fi
fi

# ---------- 汇总 ----------
{
  echo "=== vdot regression summary $(date -Iseconds) ==="
  echo
  printf '%-26s %-6s %s\n' "TEST" "RESULT" "DETAIL"
  printf '%-26s %-6s %s\n' "--------------------------" "------" "------------------------------"
  for i in "${!NAMES[@]}"; do
    printf '%-26s %-6s %s\n' "${NAMES[$i]}" "${RESULTS[$i]}" "${DETAILS[$i]}"
  done
  echo
  if [ "$fail" = "0" ]; then echo "OVERALL: PASS"; else echo "OVERALL: FAIL"; fi
} | tee "$OUT/00-summary.txt"

# 性能三口径单独摘一行，方便直接引用（仅 full 模式）
if [ "$MODE" != "quick" ] && [ -f "$OUT/perf.log" ]; then
  {
    echo
    echo "--- perf 三口径 ---"
    grep -aE '\[A\]|\[B\]|\[C\]|vdot result' "$OUT/perf.log" | head -6
  } | tee -a "$OUT/00-summary.txt"
fi

exit "$fail"
