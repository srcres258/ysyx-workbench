module DPIAdapter (
    input logic         halt,
    input logic [31:0]  address
);
    import "DPI-C" function void dpi_halt(
        input logic halt
    );
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
