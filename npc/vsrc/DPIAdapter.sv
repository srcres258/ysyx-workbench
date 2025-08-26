module DPIAdapter (
    input logic         halt,
    input logic [31:0]  address
);
    /**
     * 终止仿真
     */
    import "DPI-C" function void dpi_halt(
        input logic halt
    );
    /**
     * 更新访存地址，以同步获取地址对应的主存内容
     */
    import "DPI-C" function void dpi_onAddressUpdate(
        input logic [31:0]  address
    );

    always_ff @( posedge halt ) begin : call_dpi_halt
        dpi_halt(halt);
    end
    always @( address ) begin : on_address_update
        dpi_onAddressUpdate(address);
    end
endmodule
