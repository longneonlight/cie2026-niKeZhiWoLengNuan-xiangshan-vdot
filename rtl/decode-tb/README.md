# decode-tb —— 给"生成的 RTL"做单模块 Verilator 测试台

用途：不重跑 ~85 分钟的完整 emu 构建，直接对 `XiangShan/build/rtl/` 下**已生成**的
Verilog 模块做实验，每轮 30~40 秒。定位 `vdot` 被判 illegal 就是靠它
（见 `docs/19-decode-illegal-rootcause.md`）。

## 两个测试台

1. `tb_decode_unit.cpp` —— 驱动 `DecodeUnit`，打印一条指令的
   `fuType / fuOpType / selImm / uopSplitType / exc_2`。
   用于：确认自定义编码有没有被表项匹配、有没有遮蔽别的指令。
2. `tb_vec_exception.cpp` —— 驱动 `VecExceptionGen`（需先用
   `patch_vec_exception.py` 生成带探针的 `VecExceptionGenX`），打印
   `instIllegal/villIllegal/eewIllegal/emulIllegal/regNumIllegal/regOverlapIllegal/vstartIllegal`
   以及 `vs1Emul/vs2Emul/vdEmul`。
   用于：定位"为什么这条向量指令被判非法"。

## 用法

```bash
RTL=$HOME/xs-env/XiangShan/build/rtl
bash build.sh $RTL          # 生成 obj_dir/dec_tb 与 obj_dir/vecx
./obj_dir/dec_tb 0xFA20A1D7
./obj_dir/vecx   0xFA20A1D7
```

## 已知注意点

- `DecodeUnit.sv` 依赖 `FPDecoder.sv`、`UopInfoGen.sv`、`*LSNumOfUopTable*.sv`，缺一不可。
- 只写 `.word` 的 .s 文件不会有 RISC-V 属性，`objdump` 会把整段当数据（打印 `.word`）；
  要验证"某个编码到底是谁"，文件里必须先放一条真实向量指令。
- `--no-diff` 的 emu 里 `-C` 不生效（`CONFIG_NO_DIFFTEST` 分支），跑批要用 `timeout`。
