#!/usr/bin/env bash
# 阶段三复现：协同仿真（difftest）→ 随机对拍 → 性能两组数字
#
# 对应文档：docs/34-stage3-explained.md（§4 功能验证、§5 性能、§8 复现）
#
# 用法：
#   bash scripts/repro-stage3.sh          # 快速：定向 + 非法 + vstart（几分钟）
#   bash scripts/repro-stage3.sh full     # 全量：再加 100 组随机 + 性能
#
# 退出码：0 = 全部通过；1 = 有一项失败
set -uo pipefail

MODE=${1:-quick}
REPO=${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
XS=${XIANGSHAN_DIR:-${NOOP_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/XiangShan}}}
XS=${XS:-$HOME/XiangShan}
ND=${NEMU_DIR:-${NEMU_HOME:-${XS_PROJECT_ROOT:+$XS_PROJECT_ROOT/NEMU}}}
ND=${ND:-$HOME/NEMU}
export XIANGSHAN_DIR=$XS
export NEMU_DIR=$ND
OUT=${OUT:-$REPO/evidence/regress}

mkdir -p "$OUT"
echo "=== CIE-2026 vdot 阶段三复现（mode=$MODE） ==="
echo "时间：$(date -Iseconds)"

# run-all.sh 已把 9 定向 / 4 非法 / 3 vstart / 100 随机 / 性能 串成一条链，
# 这里只做口径检查与结果汇总，避免两套脚本各写一遍。
bash "$REPO/scripts/run-all.sh" "$MODE"
rc=$?

echo ""
echo "=== 汇总 ==="
if [ -f "$OUT/00-summary.txt" ]; then cat "$OUT/00-summary.txt"; else ls -la "$OUT"; fi

if [ "$MODE" = "full" ] && [ -f "$OUT/perf.log" ]; then
  echo ""
  echo "=== 性能两组 ==="
  grep -aE '单次执行|连续运算|cyc' "$OUT/perf.log" | head -10
fi

exit "$rc"
