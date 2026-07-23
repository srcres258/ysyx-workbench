module top(input clk, input rst, input [3:0] a, output [3:0] b);
  wire [3:0] a_d;
  child u0(.clk(clk), .rst(rst), .a(a), .b(a_d));
  child u1(.clk(clk), .rst(rst), .a(a_d), .b(b));
endmodule

module child(input clk, input rst, input [3:0] a, output reg [3:0] b);
  always @(posedge clk) begin
    if (rst) b <= 0; else b <= a + 1;
  end
endmodule
