// 单个 32 位加法器，用于 Yosys 等效门数量级的对照
module add32(input [31:0] a, input [31:0] b, output [31:0] s);
  assign s = a + b;
endmodule
