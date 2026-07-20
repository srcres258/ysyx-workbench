module ysyx_25070190(
  input clock,
  input reset,
  input [7:0] io_data_in,
  output [7:0] io_data_out
);

  DFF_X1 cpu_reg1 (.D(n1), .CK(clock), .Q(n2));
  DFF_X1 cpu_reg2 (.D(n3), .CK(clock), .Q(n4));
  NAND2_X1 cpu_nand1 (.A1(n2), .A2(n4), .ZN(n5));
  INV_X1 cpu_inv1 (.A(n5), .ZN(io_data_out[0]));

endmodule
