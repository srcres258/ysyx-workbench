module top(
  input clk, output [7:0] out
);
  sub_a u_sub_a (.clk(clk), .out(tmp));
  sub_b u_sub_b (.clk(clk), .in(tmp), .out(out));
endmodule

module sub_a(
  input clk, output [7:0] out
);
  DFF_X1 reg_a (.D(1'b0), .CK(clk), .Q(out[0]));
  BUF_X1 buf_a (.A(out[0]), .Z(out[1]));
endmodule

module sub_b(
  input clk, input [7:0] in, output [7:0] out
);
  DFF_X1 reg_b1 (.D(in[0]), .CK(clk), .Q(out[0]));
  DFF_X1 reg_b2 (.D(in[1]), .CK(clk), .Q(out[1]));
  NAND2_X1 nand_b (.A1(out[0]), .A2(out[1]), .ZN(out[2]));
endmodule
