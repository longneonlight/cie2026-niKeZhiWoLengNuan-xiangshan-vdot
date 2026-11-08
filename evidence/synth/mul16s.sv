// 单个 16x16 有符号乘法器（32 位积）——复刻 firtool 对 SInt(8)*SInt(8) 的写法：
// 两个操作数各自符号扩展到 16 位再相乘（HW 方言要求二元运算操作数等宽）。
// 对照用：解释 VDotUnit 里乘法器的单价，不代表可直接替换。
module mul16s(input signed [7:0] a, input signed [7:0] b, output signed [31:0] p);
  wire signed [15:0] aa = {{8{a[7]}}, a};
  wire signed [15:0] bb = {{8{b[7]}}, b};
  assign p = aa * bb;
endmodule
