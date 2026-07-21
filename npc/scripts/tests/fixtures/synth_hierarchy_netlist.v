// Hierarchical mapped netlist fixture for register/clock-gating inventory tests
module sub_b (
  input  clk,
  input  d_in,
  output q_out
);
  wire gated_clk;
  CLKGATE_X1 g1 (.CK(clk), .E(1'b1), .GCK(gated_clk));
  DFF_X1 r1 (.CK(gated_clk), .D(d_in), .Q(q_out));
endmodule

module sub_a (
  input  clk,
  input  [1:0] d_in,
  output [1:0] q_out
);
  SDFF_X1 r1 (.CK(clk), .D(d_in[0]), .Q(q_out[0]), .SE(1'b0), .SI(1'b0));
  DFF_X2 r2 (.CK(clk), .D(d_in[1]), .Q(q_out[1]));
endmodule

module top (
  input  clk,
  input  [2:0] d_in,
  output [2:0] q_out
);
  wire n1;
  CLKGATE_X1 cg1 (.CK(clk), .E(1'b1), .GCK(n1));
  DFF_X1 r1 (.CK(n1), .D(d_in[0]), .Q(q_out[0]));
  sub_a a1 (.clk(n1), .d_in(d_in[2:1]), .q_out(q_out[2:1]));
endmodule
