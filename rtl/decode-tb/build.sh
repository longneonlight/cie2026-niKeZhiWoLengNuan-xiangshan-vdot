#!/bin/bash
# 用法: bash build.sh /path/to/XiangShan/build/rtl
set -eu
RTL=${1:?usage: build.sh <rtl-dir>}
cd "$(dirname "$0")"
mkdir -p x

# 1) DecodeUnit
verilator --cc --exe tb_decode_unit.cpp --build --top-module DecodeUnit -Wno-fatal \
  --x-assign fast --x-initial 0 -CFLAGS "-O1" -I"$RTL" \
  "$RTL/DecodeUnit.sv" "$RTL/FPDecoder.sv" "$RTL/UopInfoGen.sv" "$RTL"/*LSNumOfUopTable*.sv \
  -o dec_tb

# 2) VecExceptionGen（带探针）
python3 patch_vec_exception.py "$RTL/VecExceptionGen.sv" x/VecExceptionGenX.sv
verilator --cc --exe tb_vec_exception.cpp --build --top-module VecExceptionGenX -Wno-fatal \
  --x-assign fast --x-initial 0 -CFLAGS "-O1" x/VecExceptionGenX.sv \
  -o vecx

echo
echo "OK. 示例："
echo "  ./obj_dir/dec_tb 0xFA20A1D7"
echo "  ./obj_dir/vecx   0xFA20A1D7"
