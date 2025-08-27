module DPIAdapter (
    input logic         halt,
    input logic [31:0]  address,
    input logic [3:0]   lsType,
    input logic         inst_jal,
    input logic         inst_jalr
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
    /**
     * 更新访存类型，以记录 mtrace 日志
     */
    import "DPI-C" function void dpi_onLSTypeUpdate(
        input logic [3:0]   lsType
    );
    /**
     * 触发指令 jal ，以记录 ftrace 日志
     */
    import "DPI-C" function void dpi_onInst_jal(
        input logic         trig
    );
    /**
     * 触发指令 jalr ，以记录 ftrace 日志
     */
    import "DPI-C" function void dpi_onInst_jalr(
        input logic         trig
    );

    always_ff @( posedge halt ) begin : call_dpi_halt
        dpi_halt(halt);
    end
    always @( address ) begin : call_dpi_onAddressUpdate
        dpi_onAddressUpdate(address);
    end
    always @( lsType ) begin : call_dpi_onLSTypeUpdate
        dpi_onLSTypeUpdate(lsType);
    end
    always_ff @( posedge inst_jal ) begin : call_dpi_onInst_jal
        dpi_onInst_jal(inst_jal);
    end
    always_ff @( posedge inst_jalr ) begin : call_dpi_onInst_jalr
        dpi_onInst_jalr(inst_jalr);
    end
endmodule
