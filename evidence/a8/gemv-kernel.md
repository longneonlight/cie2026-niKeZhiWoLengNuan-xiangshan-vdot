# “GEMV 基线卡死”定位与 A9 端到端实测（2026-10-31）

> 结论先行：**不是 RTL bug，是我们测试程序用了非法的目的寄存器**。
> `vwmul` 是加宽指令，XiangShan 的 `VecExceptionGen` 要求 `vd` 偶数对齐（`RegNumNotAlign`）；
> `vwmul.vv v3, ...` 被判非法指令，而裸机程序没有装 trap handler（`mtvec` 默认 0），
> 于是 PC 跑飞——表现为 UART 停在半句话、看起来像“挂死”。
> 改成偶数目的寄存器（`v6`）后 GEMV 正常跑完，**vdot 版与基线版结果逐位一致**。

## 1. 现象与波形

- 复现：`tests/vdot/probe_vwredsum.c`（7 条向量序列 + 每轮打印）跑到 `iter 0` 后不再输出。
- 波形：`/tmp/p3.vcd`（604 MB，窗口 8000–10500）、`/tmp/p3b.vcd`（510 MB，窗口 5000–7500），
  由 `emu --dump-wave -b … -e …` 生成。
- 波形事实：整个窗口内 **`inner_vfExuBlock.exus*` 的 `io_in_valid` / `io_out_valid` 全为 0**，
  即向量单元根本没有收到这条指令；与此同时 core 不再提交任何指令。
- 另一次 `-C 7600` 的运行日志里出现 `at pc = 0xecf1bb7e92b8`——一个明显跑飞的 PC，
  说明处理器进了异常/跳飞路径，而不是“流水线卡住”。

## 2. 二分矩阵（每条都是一次 RTL 运行）

| 探针 | 目的寄存器 | 结果 |
|---|---|---|
| `probe_stage.c`（S/l/m/r/v 分段打印） | `vmul`→`v3` | 停在 `Sl`：`vle8` 之后第一条 `vwmul` 就没完成 |
| `probe_no_uart_between.c`（load 与乘之间无 UART） | `v3` | 停在 `S` |
| `probe_nop_gap.c`（load 与乘之间插 8 条 nop） | `v3` | 仍停在 `S` → 不是 load-to-use 距离问题 |
| `probe_no_vsetvl.c`（去掉多余的 vsetvli） | `v3` | 仍停在 `Sl` → 不是 vsetvli 问题 |
| `probe_dest_v4.c` | **`v4`（偶数）** | **`SmD`，跑通** |
| `probe_dest_v5.c` | `v5`（奇数） | 停在 `S` |

奇偶两组一对比，指向“加宽目的寄存器必须偶数对齐”。

## 3. 根因（源码）

`src/main/scala/xiangshan/backend/decode/VecExceptionGen.scala`：

- L19：`RegNumNotAlign(reg, emul)` —— `emul=101` 要求 `reg(0)==0`（2 对齐），`110` 要求 4 对齐，`111` 要求 8 对齐；
- L247–255：`vdEmulSel` 里含 `vdWideningInst`，加宽指令的 `vdEmul` 被抬成 `LMUL*2`；
- L257：`vdNotAlign` → `regNumIllegal` → `illegalInst`。

`vwmul`（DestEEW=SEW×2）正是加宽指令，所以 `vd=3` 一定非法；改为 `v6` 合法。
归约类（`vwredsum`）因为 `vdIsSingleElem` 走另一条 `vdEmulSel` 分支，不受此约束——
这也解释了为什么 `perf_latency.c` 里 `vwredsum.vs v3, v4, v0` 一直能跑。

与我们的 `vdot` 无关：`vdot` 在译码里 `vdWideningInst=false`，不受该规则约束，
所以 vdot 版本从头到尾都正常。

> 旁证：本项目早期尝试 `vwaddu.vv v4,v2,v1`（`vd=3`）同样被判非法，
> 当时归因为“加宽指令要求 vd 偶数对齐”，与本次定位一致（`19-decode-illegal-rootcause.md`）。

## 4. 修正与 A9 结果

GEMV 程序（`tests/baseline/gemv_kernel.c`）修正两处：

1. `vwmul.vv v3` → `vwmul.vv v6`（偶数目的寄存器）；
2. 初始化时 `vmv.v.i v0, 0`（`vwredsum` 的种子寄存器，之前是残留值，导致两版 checksum 不一致）。

64×64 int8 GEMV（`y[i] = Σ_j A[i][j]·x[j]`，REP=1）：

| 版本 | 总拍数 | 拍/行 | cyc/MAC | checksum |
|---|---|---|---|---|
| V1 `vdot` | **5,110** | 79 | 1.25 | `5d25e46e` |
| V2 `vwmul+vwredsum` | 5,926 | 92 | 1.45 | `5d25e46e` |
| 比值 | **1.16×** | | | 逐位一致 |

**这就是 A9 要的答案**：微基准 1.67× 到 64×64 GEMV 只剩 **1.16×**。
差异来源是可解释的——每 8 个 MAC 必须把 `vdot` 的 32 位结果搬回标量累加（没有累加形式），
而取数与循环开销两版相同。这也正好量化了 A15（“不读 vd → 进不了 GEMM 内层累加”）的代价。

## 5. 复现命令

```bash
# 探针
riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
  -ffreestanding -O2 -fno-pic -fno-pie -mcmodel=medany -T ~/hello/link.ld \
  -o p4.elf tests/vdot/probe_dest_v4.c
riscv64-linux-gnu-objcopy -O binary p4.elf p4.bin
~/bin/emu-build7-clean -i p4.bin -F tests/flash/stub.bin --no-diff -C 200000

# GEMV
cc ... -o gemv.elf tests/baseline/gemv_kernel.c && objcopy -O binary gemv.elf gemv.bin
~/bin/emu-build7-clean -i gemv.bin -F tests/flash/stub.bin --no-diff
```
