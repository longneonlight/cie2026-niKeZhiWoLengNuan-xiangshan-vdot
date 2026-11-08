# vdot.vv 的完整 RTL 改动（5 个已有文件 + 1 个新增文件）

> **导出时间**：2026-11-02 02:20（**Build 8 = v2.3 语义 + `hasVdot` 面积开关**）
> **基线**：`xs-env` 固定的 XiangShan 提交 `16ae9ddcd`（`v3.2.2-alpha-587`，2025-04-03）
> **规模**：已有文件 **+70 −5 行**（`git diff --numstat` 5 个文件），新文件 **107 行**（去注释空行 34 行）
> **说明**：这是**实际参与构建并已通过验证**的改动，不是草稿；`xiangshan-vdot.diff` 与工作副本逐行一致
> （`bash scripts/check-numbers.sh` 会断言这一点）。

## 文件清单

| 文件 | 类型 | 作用 |
|---|---|---|
| `xiangshan-vdot.diff` | 统一 diff（**5 个文件 / +70 −5**） | 对 5 个既有文件的修改：参数开关、编码常量、译码表项、译码表过滤、`VIMacU` 输出接管 |
| `VDotUnit.scala` | **新增文件**（107 行） | 8×int8 乘法 + 三级平衡加法树（**无 `vl` 端口**、**无寄存器**），`io.sum: UInt(32.W)` |

`xiangshan-vdot.diff` 里涉及 5 个文件，逐条对应：

1. `xiangshan/Parameters.scala`（**+3**）—— `XSCoreParameters` 末尾加编译期开关：
   ```scala
   hasVdot: Boolean = true,
   ```
2. `backend/decode/Instructions.scala`（**+12**）—— 新增 `object VDot { def VDOT_VV = BitPat("b1111101??????????010?????1010111") }`，
   选槽理由与寄生风险写在注释里（见 `docs/02-vdot-design.md` §3）。
3. `backend/decode/VecDecoder.scala`（**+7**）—— 在 `vopmvv` 表加一行（**`vdRen = F`**）：
   ```scala
   // vdRen = F：VDOT_VV 的 BitPat 已把 vm 固定为 1，不需要读旧 vd 做掩码/尾部合并
   VDOT_VV -> OPMVV(F, FuType.vimac, VdotOpcode.vdot, F, T, F, UopSplitType.VEC_VVV)
   ```
4. `backend/decode/DecodeUnit.scala`（**+10 −1**）—— `isVdotEntry(pat)` 按 `value/mask` 认表项，
   `decode_table` 末尾 `.filter { case (pat, _) => hasVdot || !isVdotEntry(pat) }`。
   **这一处不能省**：只关 RTL 不摘译码表，`0xFA20A1D7` 会译成"合法的 vimac 指令"、`outIsVdot` 恒 0，
   于是静默退化成一条 `vmul` 并写错 `vd`——比报非法更糟。
5. `backend/fu/wrapper/VIMacU.scala`（**+38 −4**）—— `outIsVdot = hasVdot.B && VdotOpcode.isVdot(...)`；
   `if (hasVdot)` 分支里例化 `VDotUnit` 并按 `outIsVdot` 分流三条输出：
   `res.data` 给归约结果、`vxsat` 保持"本次不置位"、`illegalInstr` 用本单元自己的判据
   （`vsew ≠ e8 || vlmul ≠ m1`）；`else` 分支三条输出原样退回 `Mgu`。

## 怎么应用

```bash
cd <XiangShan 工作副本>
git apply /path/to/xiangshan-vdot.diff
cp VDotUnit.scala src/main/scala/xiangshan/backend/fu/wrapper/
# 然后照常构建
source ../env.sh && make emu JVM_XMX=16G -j8
```

> **只想量面积、不想让指令进核？** 把 `Parameters.scala` 里的 `hasVdot` 改成 `false` 再构建：
> `VDotUnit` 不例化、译码表摘项、`0xFA20A1D7` 变非法指令。两条路径都实测过
> （关断版证据：`evidence/final8/hasvdot-off.log`）。

## 设计要点（为什么只动这 5 处）

- **不新增 `FuType`、不新增 `FuConfig`**：直接复用 `VimacCfg`/`VIMacU`，
  所以发射、重命名、写回通路全部沿用现有实现；唯一新增的参数是编译期开关 `hasVdot`（默认 true）。
- **源操作数只要 2 个**：`vdRen = F` → 只读 `vs1`/`vs2`（`src(0)`/`src(1)`），
  **不产生对 `vd` 的假依赖**；但**物理 VRF 读端口数不变**（`VimacCfg.srcData` 固定声明 3 个向量源）。
- **输出绕过 `Mgu`**：归约类结果直接给 `res.data`，与 `VIPU`（实现 `vredsum`）的做法一致。
- **`vl` 不参与计算**：题面伪代码是 `for i in range(8)`，所以单元**没有 `vl` 端口**——
  多一个 `vl` 反而会引入"`vl<8` 时结果依赖 tail 未定义字节"的隐性语义（`docs/01-semantics.md` §2）。
- 代价是必须显式处理三条输出（数据/饱和/异常），这正是 `VIMacU.scala` 里那几行改动。

## 已知的编译期开关

`VIMacU.scala` 里有一个 `private val VdotDbg = false`：置 `true` 会打印
`[VDOTDBG] FU-IN/FU-OUT` 便于调试（elaboration 期常量，`false` 时零硬件开销）。
出货构建用的是 `false`（`strings build/emu | grep -c VDOTDBG` 应为 0，回归第 0 项就在查这个）。

## 验收状态（Build 8，2026-11-01 19:03 全量回归）

| 项 | 结果 |
|---|---|
| 定向功能 9 用例 + difftest | 9/9，mismatch 0 |
| 非法条件 4 条 | 4/4，3 条 `cause=2` |
| `vstart≠0` 行为 | 3/3 报非法（与原生 `vmul.vv` 一致） |
| 随机对拍 100 组 | 三方 `fails=0`，checksum `1bd2ef2a` |
| 性能 | [A] 11→1、[B] 7→3（**1.67×**）、[C] 17→1 |
| 等效门数（Yosys 0.9，generic） | **4,887**，`ltp` = 35（`evidence/synth/yosys-summary.txt`） |
| 关断路径（`hasVdot=false`） | `0xFA20A1D7` → `mcause=2`、`v3` 未被写、核仍可用（`evidence/final8/hasvdot-off.log`） |

## 已知限制

- 内部操作码 `0x067` 借用 `VimacType` 的 9 位命名空间里空闲的 `111` 槽；
  若将来 RVV 补出 `vwmaccus.vv`（`111110` + funct3 `110`），本设计**不冲突**（我们 funct3 是 `010`），
  但 `111110 + 010` 这个编码在标准里属 Reserved——沿用它的风险已在 `docs/02-vdot-design.md` §3 写明。
- 只有 generic gate 数与逻辑深度，**没有工艺面积/功耗/时序**（无 PDK、无 SDC）。
