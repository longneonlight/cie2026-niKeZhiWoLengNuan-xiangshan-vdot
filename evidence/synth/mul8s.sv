// 单个 8x8 有符号乘法器（16 位积），用于 Yosys 等效门数量级的对照
module mul8s(input signed [7:0] a, input signed [7:0] b, output signed [15:0] p);
  assign p = a * b;
endmodule
