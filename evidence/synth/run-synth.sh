#!/usr/bin/env bash
# 复现结构量化：VDotUnit / 单个 8x8 有符号乘法器 / 单个 32 位加法器 的 Yosys 通用门数
#
# 用法（在 VM 上）：bash evidence/synth/run-synth.sh
# 依赖：yosys 0.9（Ubuntu 22.04 apt）、XiangShan 已 make verilog（build/rtl/VDotUnit.sv 存在）
#
# 说明：
#   - yosys 0.9 不支持 SystemVerilog 的 sized-cast（如 96'(...)，会报错），
#     所以先用 sed 做等价清理（只去掉显式宽度转换，位宽由赋值左侧决定）。
#   - 这里是 generic gate 计数，不是工艺面积；只能用于“量级与构成”的对照。
set -eu
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RTL=${RTL:-$HOME/xs-env/XiangShan/build/rtl}
OUT="$HERE/yosys-summary.txt"

if [ -f "$RTL/VDotUnit.sv" ]; then
  sed -E "s/[0-9]+'\(/\(/g" "$RTL/VDotUnit.sv" > "$HERE/VDotUnit_clean.sv"
else
  echo "WARN: $RTL/VDotUnit.sv 不存在，沿用已有 VDotUnit_clean.sv" >&2
fi

cells() {  # cells <top> <file>
  local top=$1 file=$2
  yosys -p "read_verilog -sv $HERE/$file; synth -top $top; stat" 2>&1 \
    | sed -n '/Number of cells/,/^$/p' | head -20
}

{
  echo "=== Yosys 通用门级综合（$(date -Iseconds)）==="
  echo "工具：$(yosys -V 2>&1 | head -1)"
  echo "流程：read_verilog -sv → synth -top X → stat"
  echo "说明：这是 generic gate 计数，不是工艺面积；只用于量级与构成的对照。"
  echo "VDotUnit 源文件来自 XiangShan/build/rtl/VDotUnit.sv（CIRCT 生成），"
  echo "为绕过 yosys 0.9 不支持 sized-cast 的问题，用 sed -E \"s/[0-9]+'\\(/\\(/g\" 做了等价清理。"
  echo
  echo "--- VDotUnit ---"
  cells VDotUnit VDotUnit_clean.sv
  echo "--- mul8s（单个 8x8 有符号乘法器，16 位积）---"
  cells mul8s mul8s.sv
  echo "--- add32（单个 32 位加法器）---"
  cells add32 add32.sv
  echo
  echo "--- 构成核对 ---"
  u=$(yosys -p "read_verilog -sv $HERE/VDotUnit_clean.sv; synth -top VDotUnit; stat" 2>&1 | sed -n 's/.*Number of cells: *//p' | head -1)
  m=$(yosys -p "read_verilog -sv $HERE/mul8s.sv; synth -top mul8s; stat" 2>&1 | sed -n 's/.*Number of cells: *//p' | head -1)
  a=$(yosys -p "read_verilog -sv $HERE/add32.sv; synth -top add32; stat" 2>&1 | sed -n 's/.*Number of cells: *//p' | head -1)
  echo "VDotUnit = $u 门；单个 8x8 乘法器 = $m 门；单个 32 位加法器 = $a 门"
  calc=$(awk -v m="$m" -v a="$a" 'BEGIN{printf "%d", 8*m+7*a}')
  echo "8×乘法 + 7×加法 = $calc 门"
  awk -v u="$u" -v c="$calc" 'BEGIN{printf "占比：%.1f%%\n", 100*c/u}'
} | tee "$OUT"

echo
echo "已写入：$OUT"
