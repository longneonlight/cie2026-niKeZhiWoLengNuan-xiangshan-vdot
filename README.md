# 面向边缘 AI 推理的 vdot.vv 自定义指令（香山昆明湖 V2）

2026CIE 全国 RISC-V 高水平创新及应用大赛 · 应用方向 · 参赛团队「你可知我冷暖」（陈炯、周振杨，武汉大学）

在香山昆明湖 V2 的向量执行单元里新增一条自定义指令 **`vdot.vv`**（编码 `0xFA20A1D7`）：一条指令完成 8 个 int8 的乘加与归约，结果写回 `vd[31:0]`，高 96 位清零，32 位回绕。同时把 NEMU 扩展成能执行这条指令的参考模型，用它和 RTL 做逐指令协同仿真。

---

## 三份文档

| 阶段 | 文档 | 内容 |
|---|---|---|
| 一 | [`docs/01-阶段一-向量加法执行过程.md`](docs/01-阶段一-向量加法执行过程.md) | 环境配置、`vadd.vv` 从译码到写回的完整流水、波形读数 |
| 二 | [`docs/02-阶段二-设计与实现.md`](docs/02-阶段二-设计与实现.md) | 编码选型、语义、微架构、RTL 改动、基础测试 |
| 三 | [`docs/03-阶段三-协同仿真与性能.md`](docs/03-阶段三-协同仿真与性能.md) | difftest 两侧对齐、flash 注入、功能验证、性能与结构量化、波形 |

每份文档都有同名的 `.pdf`。

---

## 关键结论

| 项 | 数值 | 出处 |
|---|---|---|
| 编码 | `vdot.vv vd, vs1, vs2` = `0xFA20A1D7`（`funct6=111110`、`vm=1`、`funct3=010`、`opcode=1010111`） | 文档二 §1 |
| 语义 | `vd[31:0] = Σ(int8 vs1[i] × int8 vs2[i])`，`i` 取 0..7，`vl` 不参与 | 文档二 §2 |
| 单次执行 | baseline 5 拍 vs vdot 3 拍 = **1.67×** | 文档三 §5 |
| 连续运算 | baseline 17 拍 vs vdot 1 拍 = **17.0×** | 文档三 §5 |
| FU 输入到输出 | **2 拍**（波形 t=8318 进、t=8320 出） | 文档三 §8 |
| 验证 | **112 次执行**（9 定向 + 3 非法 + 100 随机），随机三方 checksum 一致 | 文档三 §4 |
| 结构 | Yosys 通用门级 **4,887 等效门**，最长组合路径 35 级 | 文档三 §6 |
| 扩展性 | `NELEM` 参数化：8→16 元素门数 **2.02×**；VLEN 单独加宽不加门；16 元素形态已过 850 次比对 | 文档三 §7 |

---

## 目录

```plain
docs/       三份阶段文档（md + pdf）
tests/      测试程序：定向、非法、随机、性能、基线、flash 存根
scripts/    复现脚本：provision-2204.sh、repro-stage1/2/3.sh、run-all.sh
evidence/   回归日志、性能、综合、波形窗口（vcd.gz）与逐拍记录
rtl/        波形时序分析、译码表互斥检查
nemu/       NEMU 参考模型的两份补丁
ref/        点积的独立 C 参考实现
```

---

## 复现

环境：Ubuntu 22.04，Verilator 5.x，JDK 17，RISC-V 交叉工具链。搭建步骤见文档一 §2，或直接跑 `scripts/provision-2204.sh`。

```bash
# 工作区位置按自己的环境设；脚本也会自动读取 xs-env 的 setup.sh 导出的
# NOOP_HOME / NEMU_HOME，都取不到时退回 $HOME/XiangShan、$HOME/NEMU
export XIANGSHAN_DIR=${XIANGSHAN_DIR:-$HOME/XiangShan}
export NEMU_DIR=${NEMU_DIR:-$HOME/NEMU}

bash scripts/repro-stage1.sh          # 环境自检 → hello → vadd 双端比对 → lane 隔离
bash scripts/repro-stage2.sh          # 译码互斥 → 反汇编核对 → 基础测试
bash scripts/repro-stage3.sh          # 协同仿真 9 例 + 非法 4 条 + vstart 3 条
bash scripts/repro-stage3.sh full     # 再加 100 组随机三方对拍与性能
```

每个脚本都会把原始日志写进 `evidence/` 下对应目录。

---

## 三条限制

1. **没有工艺面积**。环境里没有 PDK 与标准单元库，4,887 是 Yosys 通用门级综合的等效门数，不是某个工艺下的面积。
2. **没有时序收敛证明**。没做 STA，不主张 Fmax；2 拍是 FU 的流水级数，波形实测。
3. **异常类用例只在 RTL 侧判定**。本套 difftest 在指令触发 trap 时两侧不同步（`ecall` 与全零指令做对照同样失配，见 `evidence/final8/trap-diff-control.txt`），因此 `vm=0` / `vsew=e16` / `vlmul=m2` 与 `vstart≠0` 都按 RTL 的 `mcause` 判定。