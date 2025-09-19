module DPIAdapter (
    input logic         halt,

    input logic [31:0]  gprs_0,
    input logic [31:0]  gprs_1,
    input logic [31:0]  gprs_2,
    input logic [31:0]  gprs_3,
    input logic [31:0]  gprs_4,
    input logic [31:0]  gprs_5,
    input logic [31:0]  gprs_6,
    input logic [31:0]  gprs_7,
    input logic [31:0]  gprs_8,
    input logic [31:0]  gprs_9,
    input logic [31:0]  gprs_10,
    input logic [31:0]  gprs_11,
    input logic [31:0]  gprs_12,
    input logic [31:0]  gprs_13,
    input logic [31:0]  gprs_14,
    input logic [31:0]  gprs_15,
    input logic [31:0]  gprs_16,
    input logic [31:0]  gprs_17,
    input logic [31:0]  gprs_18,
    input logic [31:0]  gprs_19,
    input logic [31:0]  gprs_20,
    input logic [31:0]  gprs_21,
    input logic [31:0]  gprs_22,
    input logic [31:0]  gprs_23,
    input logic [31:0]  gprs_24,
    input logic [31:0]  gprs_25,
    input logic [31:0]  gprs_26,
    input logic [31:0]  gprs_27,
    input logic [31:0]  gprs_28,
    input logic [31:0]  gprs_29,
    input logic [31:0]  gprs_30,
    input logic [31:0]  gprs_31,

    input logic [31:0]  csr_mstatus,
    input logic [31:0]  csr_mtvec,
    input logic [31:0]  csr_mepc,
    input logic [31:0]  csr_mcause,
    input logic [31:0]  csr_mtval,

    input logic         inst_jal,
    input logic         inst_jalr,

    input logic [4:0]   rs1,
    input logic [4:0]   rs2,
    input logic [4:0]   rd,
    input logic [31:0]  imm,
    input logic [31:0]  rs1Data,
    input logic [31:0]  rs2Data,

    input logic         memWriteEnable,
    input logic         memReadEnable,

    input logic         ecallEnable,

    input logic         executing,

    input logic         ifuInputValid,

    input logic         if_nextStage_valid,
    input logic         id_nextStage_valid,
    input logic         ex_nextStage_valid,
    input logic         ma_nextStage_valid,
    input logic         wb_nextStage_valid,

    input logic         upcu_pcOutput_valid
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
     * 触发环境调用，以记录 etrace 日志
     */
    import "DPI-C" function void dpi_onEcallEnable(
        input logic         ecallEnable
    );

    import "DPI-C" function void dpi_onPosEdge_ifuInputValid(
        input logic         ifuInputValid
    );

    import "DPI-C" function void dpi_onPosEdge_if_nextStage_valid(
        input logic         if_nextStage_valid
    );
    import "DPI-C" function void dpi_onPosEdge_id_nextStage_valid(
        input logic         id_nextStage_valid
    );
    import "DPI-C" function void dpi_onPosEdge_ex_nextStage_valid(
        input logic         ex_nextStage_valid
    );
    import "DPI-C" function void dpi_onPosEdge_ma_nextStage_valid(
        input logic         ma_nextStage_valid
    );
    import "DPI-C" function void dpi_onPosEdge_wb_nextStage_valid(
        input logic         wb_nextStage_valid
    );
    
    import "DPI-C" function void dpi_onPosEdge_upcu_pcOutput_valid(
        input logic         upcu_pcOutput_valid
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
    always_ff @( posedge ecallEnable ) begin : call_dpi_onEcallEnable
        dpi_onEcallEnable(ecallEnable);
    end

    always_ff @( posedge ifuInputValid ) begin : call_dpi_onPosEdge_ifuInputValid
        dpi_onPosEdge_ifuInputValid(ifuInputValid);
    end

    always_ff @( posedge if_nextStage_valid ) begin : call_dpi_onPosEdge_if_nextStage_valid
        dpi_onPosEdge_if_nextStage_valid(if_nextStage_valid);
    end
    always_ff @( posedge id_nextStage_valid ) begin : call_dpi_onPosEdge_id_nextStage_valid
        dpi_onPosEdge_id_nextStage_valid(id_nextStage_valid);
    end
    always_ff @( posedge ex_nextStage_valid ) begin : call_dpi_onPosEdge_ex_nextStage_valid
        dpi_onPosEdge_ex_nextStage_valid(ex_nextStage_valid);
    end
    always_ff @( posedge ma_nextStage_valid ) begin : call_dpi_onPosEdge_ma_nextStage_valid
        dpi_onPosEdge_ma_nextStage_valid(ma_nextStage_valid);
    end
    always_ff @( posedge wb_nextStage_valid ) begin : call_dpi_onPosEdge_wb_nextStage_valid
        dpi_onPosEdge_wb_nextStage_valid(wb_nextStage_valid);
    end

    always_ff @( posedge upcu_pcOutput_valid ) begin : call_dpi_onPosEdge_upcu_pcOutput_valid
        dpi_onPosEdge_upcu_pcOutput_valid(upcu_pcOutput_valid);
    end
endmodule
