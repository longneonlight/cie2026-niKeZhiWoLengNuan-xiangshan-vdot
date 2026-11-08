# 证据索引（每个结论 → 对应文件）

> 目的：报告里每一句带数字的话，都能在这里找到落地文件。
> 路径约定：下表里的路径都相对于本仓库根目录。

---

## 0. 2026-11-01 审计修复（v2.3 / Build 7 重建）新增证据

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| 全量回归 OVERALL PASS | `evidence/final8/00-summary.txt` | 6 项全 PASS；random-100 三方 checksum `1bd2ef2a` |
| 9 定向（含 `vl=4`→204、`vl=0`→204） | `evidence/final8/basic-diff.log` | 9 行 PASS + `RESULT: PASS`，mismatch=0 |
| 3 非法条件 | `evidence/final8/illegal.log` | 4 条（1 合法 + 3 非法 `cause=2`） |
| vstart 行为 | `evidence/final8/vstart.log` | `vstart=0` 正常；`4/16` → `cause=2` |
| 100 组随机（三方） | `evidence/final8/random-rtl.log`、`evidence/final8/random-diff.log` + `random-nemu.log` | 三份 `fails=0`，checksum 同为 `1bd2ef2a` |
| 两组性能 | `evidence/final8/perf.log` | 单次执行 `[B]` 5→3、连续运算 `[C]` 17→1 |
| **`hasVdot=false` 关断路径**（2026-11-02） | `evidence/final8/hasvdot-off.log` + `hasvdot-off.build-info.txt` | `0xFA20A1D7` → `mcause=2`；`v3` 未被写；`vadd.vv` 仍正确；vm=0 同样 `cause=2` |
| 单元门数（v2.3） | `evidence/synth/yosys-summary.txt` | **4,887 门 / ltp 35**；结构 = 8 乘 + 7 加 |

---

## 1. 功能与正确性

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| 9 个定向用例全过、difftest 零失配 | `evidence/final8/basic-diff.log` | 每行 `got=/want=`，末尾 `RESULT: PASS`；无 `different at` |
| 3 条非法条件均 `mcause=2` | `evidence/final8/illegal.log` | `e8/m1/vm=1 PASS` + `vm=0/vsew=e16/vlmul=m2` 三条 `cause=00000002` |
| 100 组随机输入三方一致 | `evidence/final8/random-rtl.log`、`random-diff.log`、`random-nemu.log` | 三份都 `fails=0 checksum=1bd2ef2a`；diff 版无 mismatch |
| `vstart≠0` → illegal | `evidence/final8/vstart.log` | `[1] vstart=4 trapped=1 cause=2`、`[2] vstart=16` 同 |
| 尾部 `vd[127:32]=0` | 上述 basic/random 日志 | 每个用例都检查 out[4..15]==0 |

## 2. 性能

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| [A] 11→1 拍、[B] 5→3（1.67×）、[C] 17→1 | `evidence/final8/perf.log` | `[A] latency baseline=11 vdot=1`、`[B] throughput baseline=5 vdot=3`、`[C] b2b 17→1`；`vdot result = 000000cc` |
| 定版无调试打印 | `evidence/final8/00-summary.txt` | `VDOTDBG 字符串（应为 0）：0` |

## 3. 结构量化（替代综合）

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| `VDotUnit` = **4,887 通用门 / ltp 35** | `evidence/synth/yosys-summary.txt` | Yosys 0.9 `synth`+`stat` 输出（v2.3 的 CIRCT RTL）；**不做百分比构成归因**（单价相加 4,472 ≠ 实测 4,887，见文件内说明） |
| 综合脚本可复现 | `evidence/synth/run-synth.sh` + `VDotUnit_clean.sv` + `mul8s.sv` + `add32.sv` | 一条命令重跑三个模块，脚本内先 `sed -E "s/[0-9]+'\(/\(/g"` 去掉 SV sized-cast |
| `vdot` 的 FU 输入→输出 = 2 拍 | `evidence/wave/vdot-fu-timing.txt` | trace 重建 + `vdot_wave.c`；t=8318→8320，`res_data=0x8`、`pdest=51` |

## 3.5 官方依据（题面 / 规范原文）

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| 题面语义：`range(8)`、写 `vd[0]`、其余清零 | `evidence/official/official-vdot-semantics.png` | 赛题页面配图原图（URL、抓取日期、SHA-256 见同目录 README） |
| RVV 0.7.1 的 `vdot.vv` 是**逐元素**（EDIV 子元素）归约 | `evidence/official/rvv-0.7.1-vdot.adoc` | `riscv-v-spec/0.7.1`（`0.7.1-20190610-Workshop-Release`）原文摘录；与本题"整寄存器→标量"不同 |

## 3.6 扩展性：`NELEM` / `VLEN` 参数化（A10，2026-11-01）

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| NELEM 8→16 = **4,887→9,894 门（2.02×）**，ltp 35→40 | `evidence/synth/nelem-scaling.txt` | 7 个配置扫描 + 单价 + 解读；`run-nelem.sh` 一条命令重跑 |
| 参数化模型与真实 CIRCT RTL 校准：**4,887 / 35**，与真实 RTL **完全一致（差 0 门）** | 同上 | 锚点段 `VDotUnit.sv（CIRCT, NELEM=8/VLEN=128）= 4887 门，ltp 35` |
| VLEN 128→256（NELEM 不变）**逻辑完全不变** | 同上 | 两行都是 `9,894 / 40`（NELEM=8 时 4,887 / 35） |
| 参数化后语义仍然位精确 | 同上 | `NELEM=8 ... 650 checks, 0 fails`；`NELEM=16 ... 850 checks, 0 fails` |
| 生成出的乘法器是 16×16（Yosys 下多 **1,052 门 = 21.5%**，**流程相关**，Chisel 层改不动） | 同上 | `TRUNC16=1` 4,887 vs `TRUNC16=0` 3,835；单价 517 vs 391 |
| 若加累加形式 `vdot.acc`：单元内 **+375 门** | 同上 | 4,887 → 5,262；不含 VRF 读口与 `vdRen` 条件 |
| GEMV 1.16× **不是**被基线里那条重复 `vsetvli` 抬高的 | `evidence/a8/gemv-fairness-2026-11-01.md` | 同一次运行三版：V1 5,136 / V2a 5,919 / V2b 5,947；checksum 全 `5d25e46e` |
| GEMV 复现性 | 同上 | 与 10-31 的 5,110/5,926 差 0.5%；`perf_latency` 对照 `[A] 11/1`、`[B] 7/3`、`[C] 17/1` 逐字一致 |
| **调度优化：GEMV 公平比值 1.81×**（两端都批处理） | `evidence/a8/gemv-scheduling-2026-11-01.md` | K=64：vdot 2,775 vs 基线 5,024（REP=3）；K=32：724 vs 1,290 = 1.78×；checksum 全 `5d25e46e` |
| 每块成本分解（K=64） | 同上 | vdot 8.10→5.42 拍/块，基线 12.88→9.81；批处理把 8 条 vsetvli/行压到 2 条 |
| 模型与激励源码 | `evidence/synth/vdot-nelem.v`、`tb_vdot_nelem.cpp`、`run-nelem.sh` | 与 `VDotUnit.scala` 逐项对应；Verilator 位精确自检 |

## 4. 阶段一（波形）

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| `vadd.vv` 在 t=8568 进、t=8569 出（FU 内部 1 拍） | `evidence/stage1/vadd-wave-cycle.txt`；`evidence/wave/vadd-8568-window.vcd.gz` | 含 `fuOpType=0x40`、`src0=src1=0x0101…01`、输出 `0x0202…02`、`pdest=51`、`vecWen=1` |
| `vdot` 的 FU 输入→输出 = 2 拍 | `evidence/wave/vdot-fu-timing.txt` | t=8318 进（fuOp=0x067）、t=8320 出（`res_data=0x8`、`pdest=51`）；与 `VimacCfg.latency=2` 一致 |
| UART 首字符 t=8653 | 同上 | 执行点定位 |
| 波形扫描方法 | `rtl/wave_vimac_cycle.py` | 逐拍打印 `VIMacU` 的 `in_valid`/`out_valid`/`res_data` |

## 5. 回归（一键复跑）

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| 全量回归结果（**2026-11-02 03:12 复跑**） | `evidence/regress/00-summary.txt` | 6 项全 PASS，`OVERALL: PASS`；与 11-01 19:03 那次逐项相同（9/9、4/4、3/3、`1bd2ef2a`、`0xCC`、11→1 / 7→3 / 17→1） |
| 阶段一复跑 | `evidence/stage1/stage1-deploy-20261031-103742.log` | hello 在 NEMU 与 RTL 各输出一次，`OVERALL: PASS` |
| 回归脚本 | `scripts/run-all.sh` | 轮询结果后杀进程，避免死等 timeout |

## 6. 代码与差异

| 结论 | 证据文件 | 关键内容 |
|---|---|---|
| RTL 改动 **5 文件 / +70 −5 行**（另新增 1 文件 107 行） | `rtl/final-diff/xiangshan-vdot.diff` | `Parameters.scala`(`hasVdot` 开关)、`DecodeUnit.scala`(摘表项)、`Instructions.scala`(VDot BitPat)、`VecDecoder.scala`(表项)、`VIMacU.scala`(接入) |
| 单元本体 | `rtl/final-diff/VDotUnit.scala` | 8×8 乘法 + 三级平衡截断树；**无 `vl` 端口**、无寄存器（107 行 / 去注释 34 行） |
| 译码/异常 testbench | `rtl/decode-tb/` | `tb_decode_unit.cpp`、`tb_vec_exception.cpp` |
