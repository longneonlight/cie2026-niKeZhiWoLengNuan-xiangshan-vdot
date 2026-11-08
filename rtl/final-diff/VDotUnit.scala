// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 CHEN JIONG (2026 CIE RISC-V Contest, XiangShan track)

package xiangshan.backend.fu.wrapper

import chisel3._
import chisel3.util._

/** `vdot.vv` 的内部操作码（复用 `VimacType` 的 9 位 `fuOpType` 布局）。
  *
  * 布局：
  * {{{
  *   [8:7] pad(2) | [6] vs2sign | [5] vs1sign | [4] vdsign | [3] format(1) | [2:0] opcode(3)
  * }}}
  *
  * `VimacOpcode` 的 3 位操作码 `000`~`110` 已被 `vmul`/`vmulh`/`vmacc`/`vnmsac`/`vmadd`/
  * `vnmsub`/`vsmul` 占用，**`111` 空闲**。这里取：
  *
  * {{{
  *   pad=00, vs2sign=1(S), vs1sign=1(S), vdsign=0, format=0(VVV), opcode=111
  *   ==> 0b00_110_0_111 = 0x067
  * }}}
  *
  * （注意不是 0x0E7：`vdsign` 位取 0，因为结果由本单元自己算，不经过 Vimac 的
  * 符号扩展通路。）
  *
  * 为什么 0x067 在既有数据通路里是**确定的**：`VimacOpcode` 的全部辅助函数都只看
  * opcode 的低 3 位（`yunsuan/encoding/Opcode/VimacOpcode.scala`），`111` 使
  * `highHalf`（===001）、`isMacc`（∈010/011/100/101）、`isSub`（∈011/101）、
  * `isFixP`（===110）、`overWriteMultiplicand`（∈100/101）一律返回 `false`，
  * 而 `VimacType.getFormat` 读 bit3 = 0 得 `VVV`（widen=false）。也就是说该操作码
  * 在既有 VIMac 数据通路里走的是"普通 vmul"那一支，不会进入任何未定义分支。
  * 逐函数核对见 `docs/16-vdot-interface.md` §1。
  *
  * 该值只用于本 FU 内部选择，**不是架构编码**，不要在别处复用。
  */
object VdotOpcode {
  /** 9 位内部操作码。 */
  def vdot: UInt = "b00_110_0_111".U(9.W)
  /** 唯一判据入口，避免在别处写裸魔数。 */
  def isVdot(fuOpType: UInt): Bool = fuOpType === vdot
}

/** `vdot.vv` 的乘加归约数据通路。
  *
  * 架构语义（**以题面伪代码为准**）：
  * {{{
  *   result = Σ_{i=0}^{7} sext8(vs1.byte[i]) * sext8(vs2.byte[i])   // 32 位回绕，不饱和
  *   vd[31:0]      = result
  *   vd[vlen-1:32] = 0
  * }}}
  *
  * **`vl` 不参与计算**：题面写的是 `for i in range(8)`，固定 8 个元素，与 RVV 的
  * "按 vl / 逐元素"语义不同。本单元因此**没有 vl 输入**——多一个 vl 反而会引入
  * "vl<8 时结果依赖 tail 未定义字节"这类隐性语义。见 `docs/01-semantics.md` §2。
  *
  * 微架构：
  *  - 8 个 8x8 有符号乘法器（Chisel 会把 SInt(8) 扩到 16 位再乘，见 `prods`；
  *    firtool 生成的就是 16x16 乘法器，综合价见 `evidence/synth/nelem-scaling.txt`）
  *  - 32 位平衡归约树（每层两两相加，结果模 2^32）
  *  - 纯组合逻辑，单元内无寄存器；FU 边界的 2 拍由 `VimacCfg.latency` 决定
  *
  * 关于为何不复用 `yunsuan` 的 `VIMac64b`：**这是取舍，不是做不到**。
  * 该阵列确实能凑出 8 个乘积（8 个模块各喂 32 位，`uopIdx=0` 一次出 8 个 16 位）。
  * 代价是要改 `VIMacU` 接线、从两个 64 位输出里重新对齐符号，并与 FU 自身的
  * `vmacc/vwmul` 共用同一乘法阵列——结构与 `uopIdx` 语义耦合，且无法单独对拍。
  * 自写 8 个乘法器换来的是一块可单独综合、单独对拍、单独量成本的黑盒。
  * 详见 `docs/17-mul-reuse.md` §4。
  */
class VDotUnit(vlen: Int = 128) extends Module {
  require(vlen >= 64, s"vdot 需要至少 8 个 int8：vlen=$vlen")
  require(vlen % 8 == 0, s"vlen 必须是 8 的整数倍：vlen=$vlen")

  val io = IO(new Bundle {
    val vs1 = Input(UInt(vlen.W))   // 乘数向量 a
    val vs2 = Input(UInt(vlen.W))   // 乘数向量 b
    val sum = Output(UInt(32.W))    // 32 位回绕累加结果
  })

  private val SEW = 8

  /** 题面语义：`for i in range(8)` —— 每条指令固定取 **8** 个 int8 相乘求和，
    * 与 `vl` 无关。若将来要做 16 元素版本，只改这个常量。
    */
  private val NELEM = 8

  /** 每个参与元素的完整符号扩展乘积，按 32 位对齐 */
  private val prods: Seq[UInt] = (0 until NELEM).map { i =>
    val a = io.vs1(SEW * i + SEW - 1, SEW * i).asSInt
    val b = io.vs2(SEW * i + SEW - 1, SEW * i).asSInt
    (a * b).pad(32).asUInt
  }

  /** 平衡归约树：每层两两相加，截断到 32 位（模 2^32） */
  private def reduceTree(xs: Seq[UInt]): UInt = {
    if (xs.length == 1) xs.head
    else {
      val next = xs.grouped(2).map {
        case Seq(a, b) => (a + b)(31, 0)
        case Seq(a)    => a
      }.toSeq
      reduceTree(next)
    }
  }

  io.sum := reduceTree(prods)
}
