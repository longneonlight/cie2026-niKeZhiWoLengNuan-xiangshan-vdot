# GEMV 基准公平性复核（2026-11-01）

> 目的：核对我提出的两个怀疑——
> ① `gemv_base()` 内层有一条重复的 `vsetvli`，是否抬高了基线、从而夸大了 vdot 的 1.16×？
> ② 反汇编数出来的"12 条 vs 10 条"是否真的决定周期（即该循环是否发射受限）？
>
> **结论：① 不成立（复核后 1.16× 不变）；② 不成立（该循环不是单纯发射受限）。**

---

## 1. 方法与对照

- 用同一个 `~/bin/emu-build7-clean`（Build 7，`--no-diff`，`-F tests/flash/stub.bin`）。
- 新增 `tests/baseline/gemv_kernel_fair.c`：**同一次运行**里跑三个版本，同一份数据，逐元素比对：

| 变量 | 内容 |
|---|---|
| V1 | `vdot` 内核（与提交版相同） |
| V2a | 基线**原版**（`e8` / `e16` / `e16` 三条 `vsetvli`） |
| V2b | 基线**公平版**（删掉第三条重复的 `vsetvli`） |

- 对照实验（验证环境可信）：复跑 `tests/baseline/perf_latency.c`，结果与 10-29 的
  `evidence/final7/perf.log` **逐字一致**：`[A] 11→1`、`[B] 7→3`、`[C] 17→1`。

---

## 2. 反汇编事实

```asm
; vdot 内层（0x1120，8 条 + 2 条指针/分支 = 10 条）
vsetvli t1,a1,e8      vle8.v v1,(s0)   vle8.v v2,(s1)   .word 0xfa20a1d7
vsetvli t1,zero,e32   vmv.x.s a4,v3    addiw a5,a5,-1  bnez

; 基线内层（0x138，10 条 + 2 条 = 12 条）
vsetvli t1,a0,e8  vle8  vle8  vwmul.vv v6,v1,v2
vsetvli t1,a0,e16 vwredsum.vs v4,v6,v0
vsetvli t1,a0,e16           <-- 与上一条逐字相同
vmv.x.s a1,v4  addi a5  addw a3  addi a4  bne
```

指令数 12 : 10，但周期比是 11.58 : 9.98（1.16），不是 1.20——本来就不是严格的线性关系。

---

## 3. 实测（同一次运行，`mcycle` 夹住整段循环）

| N（行数=列数） | V1 vdot | V2a 基线原版 | V2b 基线公平版 | V1/V2a | V1/V2b |
|---|---|---|---|---|---|
| 16 | 584 | 512 | 494 | **0.87**（vdot 慢） | 0.84 |
| 32 | 1479 | 1748 | 1788 | **1.18**（vdot 快） | 1.20 |
| **64** | **5136** | **5919** | **5947** | **1.15** | **1.15** |

三版的 checksum 全部相同（N=16 `28376d70`、N=32 `b1363901`、N=64 **`5d25e46e`**，与
`evidence/a8/gemv-kernel.md` 记录的 `5d25e46e` 一致）。

### 3.1 关于①：冗余 vsetvli 的影响

- N=16：删掉它省 **18 拍 / 512**（+3.5%）；
- N=32：删掉它**多花 40 拍** / 1748（−2.3%）；
- N=64：删掉它**多花 28 拍** / 5919（−0.5%）。

**符号不一致 ⇒ 这是噪声/指令排布效应，不是系统性成本。**
1.16× 不是被这条指令抬高的。它在 `gemv_base()` 里确实冗余（应删，属于代码整洁问题），
但**不影响测量结论**。

### 3.2 关于②：是否发射受限

若该循环是发射受限（~1 IPC），删一条指令应稳定省 ~1 拍/迭代（N=64 时约 512 拍）。
实测只有 ±18~40 拍。说明基线内层**主要卡在 `vwredsum → vmv.x.s` 的延迟链**，
多一条 `vsetvli` 被延迟掩盖掉了。

### 3.3 N=16 为什么反过来

N=16 时每个内核只跑 16×2 = 32 次内层迭代，冷启动（I-cache/预测器）成本摊不薄，
两版都被抬高，且 vdot 的行开销更大，于是出现 0.87×。
**N=16 的结论不可用**，只作趋势参考；K≥32 时复现 1.15–1.18×。

---

## 4. 复现

```bash
cd ~/cie2026-vdot-repo
riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles -ffreestanding \
  -O2 -fno-pic -fno-pie -mcmodel=medany -DN=64 -T ~/hello/link.ld \
  -o /tmp/gf64.elf tests/baseline/gemv_kernel_fair.c
riscv64-linux-gnu-objcopy -O binary /tmp/gf64.elf /tmp/gf64.bin
~/bin/emu-build7-clean -i /tmp/gf64.bin -F tests/flash/stub.bin --no-diff -C 150000
# 注意：emu 约 230 周期/秒，N=64 一次约 11 分钟；输出在进程退出时才 flush，
# 所以 -C 要给到"程序跑完"之上（N=64 建议 120000）。
```

---

## 5. 对我此前说法的处置

| 说法 | 处置 |
|---|---|
| "基线有一条冗余 vsetvli" | ✅ **事实**（反汇编 0x1150 与 0x1148 逐字相同） |
| "所以 1.16× 被高估，真实可能 1.05–1.10" | ❌ **撤回**：实测删掉后无变化（甚至略慢），属噪声 |
| "1.16× ≈ 12:10 的指令数比，说明发射受限" | ❌ **撤回**：删一条指令不省拍，说明是延迟受限而非发射受限 |
| "5,110 / 5,926" | ✅ **复现**：本次 5,136 / 5,919（差 0.5%），checksum 相同 |
