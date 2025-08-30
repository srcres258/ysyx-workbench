module DPIAdapter (
    input logic         halt,
    input logic         inst_jal,
    input logic         inst_jalr,
    input logic         memWriteEnable,
    input logic         memReadEnable,
    input logic [2:0]   stage
);
    /**
     * 终止仿真
     */
    import "DPI-C" function void dpi_halt(
        input logic halt
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
    /**
     * 触发内存写使能，通知后台仿真环境将处理器提供的数据写入主存
     */
    import "DPI-C" function void dpi_onMemWriteEnable(
        input logic         memWriteEnable
    );
    /**
     * 触发内存读使能，通知后台仿真环境读取主存提供给处理器
     */
    import "DPI-C" function void dpi_onMemReadEnable(
        input logic         memReadEnable
    );
    /**
     * 触发处理器阶段更新，通知后台仿真环境同步处理器阶段
     */
    import "DPI-C" function void dpi_onStage(
        input logic [2:0]   stage
    );

    always_ff @( posedge halt ) begin : call_dpi_halt
        dpi_halt(halt);
    end
    always_ff @( posedge inst_jal ) begin : call_dpi_onInst_jal
        dpi_onInst_jal(inst_jal);
    end
    always_ff @( posedge inst_jalr ) begin : call_dpi_onInst_jalr
        dpi_onInst_jalr(inst_jalr);
    end
    always_ff @( posedge memWriteEnable ) begin : call_dpi_onMemWriteEnable
        dpi_onMemWriteEnable(memWriteEnable);
    end
    always_ff @( posedge memReadEnable ) begin : call_dpi_onMemReadEnable
        dpi_onMemReadEnable(memReadEnable);
    end
    always @( stage ) begin : call_dpi_onStage
        dpi_onStage(stage);
    end
endmodule
