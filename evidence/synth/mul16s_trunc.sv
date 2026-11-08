// 同上，但积截断到 16 位（VDotUnit 实际只需要低 16 位参与归约）
module mul16s_trunc(input signed [7:0] a, input signed [7:0] b, output signed [15:0] p);
  wire signed [15:0] aa = {{8{a[7]}}, a};
  wire signed [15:0] bb = {{8{b[7]}}, b};
  assign p = (aa * bb);
endmodule
