// StandaloneMemDPI: DPI-C based memory for standalone NPC simulation.
// Provides a simple AXI4-lite-like slave that reads/writes through DPI-C
// to a C++ byte array. The C++ side loads the program binary before simulation.

module StandaloneMemDPI(
    input         clock,
    input         reset,

    // AXI4 read address channel
    input         axi_ar_valid,
    output        axi_ar_ready,
    input  [31:0] axi_ar_bits_addr,
    input  [3:0]  axi_ar_bits_id,
    input  [7:0]  axi_ar_bits_len,
    input  [2:0]  axi_ar_bits_size,
    input  [1:0]  axi_ar_bits_burst,

    // AXI4 read data channel
    output        axi_r_valid,
    input         axi_r_ready,
    output [1:0]  axi_r_bits_resp,
    output [31:0] axi_r_bits_data,
    output        axi_r_bits_last,
    output [3:0]  axi_r_bits_id,

    // AXI4 write address channel
    input         axi_aw_valid,
    output        axi_aw_ready,
    input  [31:0] axi_aw_bits_addr,
    input  [3:0]  axi_aw_bits_id,
    input  [7:0]  axi_aw_bits_len,
    input  [2:0]  axi_aw_bits_size,
    input  [1:0]  axi_aw_bits_burst,

    // AXI4 write data channel
    input         axi_w_valid,
    output        axi_w_ready,
    input  [31:0] axi_w_bits_data,
    input  [3:0]  axi_w_bits_strb,
    input         axi_w_bits_last,

    // AXI4 write response channel
    output        axi_b_valid,
    input         axi_b_ready,
    output [1:0]  axi_b_bits_resp,
    output [3:0]  axi_b_bits_id
);

    import "DPI-C" function int dpi_pmem_read(input int addr);
    import "DPI-C" function void dpi_pmem_write(input int addr, input int data, input byte strb);
    import "DPI-C" function void dpi_set_pmem_word(input int word_addr, input int data);

    assign axi_ar_ready = 1'b1;
    assign axi_aw_ready = 1'b1;
    assign axi_w_ready  = 1'b1;

    // Read channel
    reg [31:0] rdata_reg;
    reg        rvalid_reg;
    assign axi_r_bits_data = rdata_reg;
    assign axi_r_bits_resp = 2'b00;
    assign axi_r_bits_last = 1'b1;
    assign axi_r_bits_id   = 4'b0;
    assign axi_r_valid     = rvalid_reg;

    always @(posedge clock) begin
        if (reset) begin
            rvalid_reg <= 1'b0;
            rdata_reg  <= 32'b0;
        end else begin
            if (axi_ar_valid && axi_ar_ready) begin
                rdata_reg  <= dpi_pmem_read(axi_ar_bits_addr);
                rvalid_reg <= 1'b1;
            end
            if (axi_r_valid && axi_r_ready) begin
                rvalid_reg <= 1'b0;
            end
        end
    end

    // Write channel — supports both same-cycle and sequential AW/W
    reg         aw_captured;
    reg [31:0]  aw_addr_reg;
    reg         w_captured;
    reg [31:0]  w_data_reg;
    reg [3:0]   w_strb_reg;
    reg         bvalid_reg;

    assign axi_b_bits_resp = 2'b00;
    assign axi_b_bits_id   = 4'b0;
    assign axi_b_valid     = bvalid_reg;

    always @(posedge clock) begin
        if (reset) begin
            aw_captured <= 1'b0;
            w_captured  <= 1'b0;
            bvalid_reg  <= 1'b0;
        end else begin
            // Latch write address
            if (axi_aw_valid && axi_aw_ready) begin
                aw_addr_reg <= axi_aw_bits_addr;
                aw_captured <= 1'b1;
            end
            // Latch write data
            if (axi_w_valid && axi_w_ready) begin
                w_data_reg <= axi_w_bits_data;
                w_strb_reg <= axi_w_bits_strb;
                w_captured <= 1'b1;
            end
            // Execute write when both captured (sequential case)
            if (aw_captured && w_captured && !bvalid_reg) begin
                dpi_pmem_write(aw_addr_reg, w_data_reg, w_strb_reg);
                bvalid_reg  <= 1'b1;
                aw_captured <= 1'b0;
                w_captured  <= 1'b0;
            end
            // Also handle same-cycle arrival
            if (axi_aw_valid && axi_aw_ready && axi_w_valid && axi_w_ready) begin
                dpi_pmem_write(axi_aw_bits_addr, axi_w_bits_data, axi_w_bits_strb);
                bvalid_reg  <= 1'b1;
                aw_captured <= 1'b0;
                w_captured  <= 1'b0;
            end
            // B handshake
            if (axi_b_valid && axi_b_ready) begin
                bvalid_reg <= 1'b0;
            end
        end
    end

endmodule
