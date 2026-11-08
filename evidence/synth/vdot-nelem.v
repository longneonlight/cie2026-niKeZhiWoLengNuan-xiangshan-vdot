// vdot 归约数据通路的 NELEM / VLEN 参数化模型（A10 用）
//
// 与 **v2.3** 的 `VDotUnit.scala` 逐项对应，只把常量提成参数：
//   - NELEM 个 8x8 有符号乘法器，乘积符号扩展到 32 位            (VDotUnit.scala:93-99)
//   - 无 vl 门控：题面 `for i in range(8)` 不引用 vl               (v2.3，2026-11-01)
//   - 平衡二叉归约树，每层两两相加并截断到 32 位（模 2^32）        (VDotUnit.scala:101-110)
//
// 参数 TRUNC16 用来复刻两种乘法器写法：
//   TRUNC16=1  与 CIRCT/firtool 生成的 VDotUnit.sv 完全一致：
//              8 位操作数先符号扩展到 16 位再相乘（16x16 乘法器）
//   TRUNC16=0  语义等价的 8x8 乘法器（手写风格）
// 两者功能完全相同（低 16 位积一致），只是综合后门数不同 —— 见 nelem-scaling.txt。
//
// 注意：`vl` 端口**保留但逻辑上不使用**，只为复用旧的位精确 testbench
// （`tb_vdot_nelem.cpp` 仍驱动它，用来证明"结果与 vl 无关"）。
// 真实的 `VDotUnit.scala` v2.3 已经删掉了这个端口。
//
// 这不是新的 ISA 语义：题面语义固定为 `for i in range(8)`。
// 这里回答的是红队 A10 的问题——"NELEM / VLEN 变大，门数和路径怎么变"。
//
// 用法（yosys 0.9）：
//   yosys -p "read_verilog -sv vdot-nelem.v; chparam -set NELEM 16 -set VLEN 256 vdot_nelem; \
//             hierarchy -top vdot_nelem; synth -top vdot_nelem; stat; ltp"
module vdot_nelem #(
  parameter integer VLEN    = 128,  // 向量寄存器位宽（香山默认 128）
  parameter integer NELEM   = 8,    // 每条指令参与归约的元素数（题面为 8）
  parameter integer TRUNC16 = 1,    // 1 = 复刻 CIRCT 写法；0 = 等价 8x8
  parameter integer ACC     = 0     // 1 = 假设语义改成"读旧 vd 累加"（A15 的成本估算）
)(
  input  wire [VLEN-1:0] vs1,       // 乘数向量 a
  input  wire [VLEN-1:0] vs2,       // 乘数向量 b
  input  wire [7:0]      vl,        // **逻辑上不使用**（保留只为复用旧 testbench）
  input  wire [31:0]     vd_old,    // 仅在 ACC=1 时有效（对应 vdot.acc 变体）
  output wire [31:0]     sum        // 32 位回绕累加结果
);

  // ---------------------------------------------------------------- 乘法
  localparam integer NP = NELEM;
  wire [(NP*32)-1:0] p;

  genvar i;
  generate
    for (i = 0; i < NELEM; i = i + 1) begin : g_mul
      wire signed [7:0] a = vs1[8*i +: 8];
      wire signed [7:0] b = vs2[8*i +: 8];
      wire [15:0] m;
      if (TRUNC16) begin : g_m16
        // firtool 的写法：两个 16 位（符号扩展后）操作数相乘，结果截到 16 位
        assign m = ({{8{a[7]}}, a} * {{8{b[7]}}, b});
      end else begin : g_m8
        assign m = a * b;
      end
      // v2.3：无 vl 门控，符号扩展到 32 位后直接进树
      assign p[32*i +: 32] = {{16{m[15]}}, m};
    end
  endgenerate

  // ---------------------------------------------------------------- 平衡归约树
  // 每层宽度 ceil(n/2)；奇数个时最后一个直接上移（与 reduceTree 的 grouped(2) 一致）
  localparam integer N1 = (NP + 1) / 2;
  localparam integer N2 = (N1 + 1) / 2;
  localparam integer N3 = (N2 + 1) / 2;
  localparam integer N4 = (N3 + 1) / 2;
  localparam integer N5 = (N4 + 1) / 2;   // NP <= 32 时 N5 = 1

  wire [(N1*32)-1:0] l1;
  wire [(N2*32)-1:0] l2;
  wire [(N3*32)-1:0] l3;
  wire [(N4*32)-1:0] l4;
  wire [(N5*32)-1:0] l5;

  generate
    for (i = 0; i < N1; i = i + 1) begin : g_l1
      if (2*i + 1 < NP)
        assign l1[32*i +: 32] = p[64*i +: 32] + p[64*i+32 +: 32];
      else
        assign l1[32*i +: 32] = p[64*i +: 32];
    end
    for (i = 0; i < N2; i = i + 1) begin : g_l2
      if (2*i + 1 < N1)
        assign l2[32*i +: 32] = l1[64*i +: 32] + l1[64*i+32 +: 32];
      else
        assign l2[32*i +: 32] = l1[64*i +: 32];
    end
    for (i = 0; i < N3; i = i + 1) begin : g_l3
      if (2*i + 1 < N2)
        assign l3[32*i +: 32] = l2[64*i +: 32] + l2[64*i+32 +: 32];
      else
        assign l3[32*i +: 32] = l2[64*i +: 32];
    end
    for (i = 0; i < N4; i = i + 1) begin : g_l4
      if (2*i + 1 < N3)
        assign l4[32*i +: 32] = l3[64*i +: 32] + l3[64*i+32 +: 32];
      else
        assign l4[32*i +: 32] = l3[64*i +: 32];
    end
    for (i = 0; i < N5; i = i + 1) begin : g_l5
      if (2*i + 1 < N4)
        assign l5[32*i +: 32] = l4[64*i +: 32] + l4[64*i+32 +: 32];
      else
        assign l5[32*i +: 32] = l4[64*i +: 32];
    end
  endgenerate

  // ACC=1 时把旧 vd 加进来（elaboration 期决定，ACC=0 不产生任何逻辑）
  generate
    if (ACC) begin : g_acc
      assign sum = l5[31:0] + vd_old;
    end else begin : g_noacc
      assign sum = l5[31:0];
    end
  endgenerate

endmodule
