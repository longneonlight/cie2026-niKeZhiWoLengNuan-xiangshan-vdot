// 三种 8→1 归约结构的对比（仅组合部分；用于 Yosys 门数与最长路径对比）
// 输入 p0..p7：8 个 32 位（符号扩展后的乘积）；输出 s：32 位
//
// A: 3 级平衡二叉（当前 VDotUnit 的结构）
// B: 4 输入四叉树（每 4 个先各自两级求和，再合并）——深度与 A 相同，但加法器级数不同
// C: 3:2 CSA 压缩（Wallace 风格）后再加一次
module red_bin(input [255:0] p, output [31:0] s);
  wire [31:0] p0 = p[31:0],   p1 = p[63:32],  p2 = p[95:64],   p3 = p[127:96];
  wire [31:0] p4 = p[159:128],p5 = p[191:160],p6 = p[223:192], p7 = p[255:224];
  wire [31:0] a = p0 + p1, b = p2 + p3, c = p4 + p5, d = p6 + p7;
  wire [31:0] e = a + b, f = c + d;
  assign s = e + f;
endmodule

module red_quad(input [255:0] p, output [31:0] s);
  wire [31:0] p0 = p[31:0],   p1 = p[63:32],  p2 = p[95:64],   p3 = p[127:96];
  wire [31:0] p4 = p[159:128],p5 = p[191:160],p6 = p[223:192], p7 = p[255:224];
  // 每个 4 输入节点用两级加法器（等价于一个 4 输入加法器）
  wire [31:0] q0 = (p0 + p1) + (p2 + p3);
  wire [31:0] q1 = (p4 + p5) + (p6 + p7);
  assign s = q0 + q1;
endmodule

module csa32(input [31:0] a, input [31:0] b, input [31:0] c,
             output [31:0] sum, output [31:0] carry);
  assign sum   = a ^ b ^ c;
  assign carry = ((a & b) | (a & c) | (b & c)) << 1;
endmodule

module red_csa(input [255:0] p, output [31:0] s);
  wire [31:0] p0 = p[31:0],   p1 = p[63:32],  p2 = p[95:64],   p3 = p[127:96];
  wire [31:0] p4 = p[159:128],p5 = p[191:160],p6 = p[223:192], p7 = p[255:224];
  wire [31:0] s0, c0, s1, c1, s2, c2, s3, c3, s4, c4, s5, c5;
  csa32 u0(p0, p1, p2, s0, c0);
  csa32 u1(p3, p4, p5, s1, c1);
  csa32 u2(s0, c0, s1, s2, c2);
  csa32 u3(c1, p6, p7, s3, c3);
  csa32 u4(s2, c2, s3, s4, c4);
  csa32 u5(s4, c4, c3, s5, c5);
  assign s = s5 + c5;
endmodule
