module GeneralDPIAdapter (
    input  [31:0] core_pc,
    input         core_halt,
    input         core_executing,
    input         core_ifuInputValid,

    input         physicalRAM_read_readEnable,
    input  [31:0] physicalRAM_read_readAddress,
    output [31:0] physicalRAM_read_readData,
    input         physicalRAM_write_writeEnable,
    input  [31:0] physicalRAM_write_writeAddress,
    input  [3:0]  physicalRAM_write_writeDataStrobe,
    input  [31:0] physicalRAM_write_writeData,

    input         uart_read_readEnable,
    input  [31:0] uart_read_readAddress,
    output [31:0] uart_read_readData,
    input         uart_write_writeEnable,
    input  [31:0] uart_write_writeAddress,
    input  [3:0]  uart_write_writeDataStrobe,
    input  [31:0] uart_write_writeData,

    input  [31:0] gpr_gprs_0,
    input  [31:0] gpr_gprs_1,
    input  [31:0] gpr_gprs_2,
    input  [31:0] gpr_gprs_3,
    input  [31:0] gpr_gprs_4,
    input  [31:0] gpr_gprs_5,
    input  [31:0] gpr_gprs_6,
    input  [31:0] gpr_gprs_7,
    input  [31:0] gpr_gprs_8,
    input  [31:0] gpr_gprs_9,
    input  [31:0] gpr_gprs_10,
    input  [31:0] gpr_gprs_11,
    input  [31:0] gpr_gprs_12,
    input  [31:0] gpr_gprs_13,
    input  [31:0] gpr_gprs_14,
    input  [31:0] gpr_gprs_15,
    input  [31:0] gpr_gprs_16,
    input  [31:0] gpr_gprs_17,
    input  [31:0] gpr_gprs_18,
    input  [31:0] gpr_gprs_19,
    input  [31:0] gpr_gprs_20,
    input  [31:0] gpr_gprs_21,
    input  [31:0] gpr_gprs_22,
    input  [31:0] gpr_gprs_23,
    input  [31:0] gpr_gprs_24,
    input  [31:0] gpr_gprs_25,
    input  [31:0] gpr_gprs_26,
    input  [31:0] gpr_gprs_27,
    input  [31:0] gpr_gprs_28,
    input  [31:0] gpr_gprs_29,
    input  [31:0] gpr_gprs_30,
    input  [31:0] gpr_gprs_31,
    input  [31:0] csr_csr_mstatus,
    input  [31:0] csr_csr_mtvec,
    input  [31:0] csr_csr_mepc,
    input  [31:0] csr_csr_mcause,
    input  [31:0] csr_csr_mtval,

    input         ifu_if_nextStage_valid,
    input  [4:0]  idu_rs1,
    input  [4:0]  idu_rs2,
    input  [4:0]  idu_rd,
    input  [31:0] idu_imm,
    input  [31:0] idu_rs1Data,
    input  [31:0] idu_rs2Data,
    input  [31:0] idu_inst,
    input         idu_inst_jal,
    input         idu_inst_jalr,
    input         idu_id_nextStage_valid,
    input         exu_ecallEnable,
    input         exu_ex_nextStage_valid,
    input         mau_memWriteEnable,
    input         mau_memReadEnable,
    input         mau_ma_nextStage_valid,
    input         wbu_wb_nextStage_valid,
    input         upcu_upcu_pcOutput_valid
);
    logic halt = core_halt;
    logic inst_jal = idu_inst_jal;
    logic inst_jalr = idu_inst_jalr;
    logic memWriteEnable = physicalRAM_write_writeEnable;
    logic memReadEnable = physicalRAM_read_readEnable;
    logic ecallEnable = exu_ecallEnable;
    logic ifuInputValid = core_ifuInputValid;
    logic if_nextStage_valid = ifu_if_nextStage_valid;
    logic id_nextStage_valid = idu_id_nextStage_valid;
    logic ex_nextStage_valid = exu_ex_nextStage_valid;
    logic ma_nextStage_valid = mau_ma_nextStage_valid;
    logic wb_nextStage_valid = wbu_wb_nextStage_valid;
    logic upcu_pcOutput_valid = upcu_upcu_pcOutput_valid;

    logic [31:0] readData;
    initial readData = 32'h0;
    assign physicalRAM_read_readData = readData;

    logic [31:0] uart_readData;
    initial uart_readData = 32'h0;
    assign uart_read_readData = uart_readData;

    /**
     * 终止仿真
     */
    import "DPI-C" function void       dpi_halt(input logic halt);
    /**
     * 触发指令 jal ，以记录 ftrace 日志
     */
    import "DPI-C" function void       dpi_onInst_jal(input logic trig);
    /**
     * 触发指令 jalr ，以记录 ftrace 日志
     */
    import "DPI-C" function void       dpi_onInst_jalr(input logic trig);
    /**
     * 触发内存写使能，通知后台仿真环境将处理器提供的数据写入主存
     */
    import "DPI-C" function void       dpi_onMemWriteEnable(input logic memWriteEnable);
    /**
     * 触发内存读使能，通知后台仿真环境读取主存提供给处理器
     */
    import "DPI-C" function bit [31:0] dpi_onMemReadEnable(input logic memReadEnable);
    /**
     * 触发环境调用，以记录 etrace 日志
     */
    import "DPI-C" function void       dpi_onEcallEnable(input logic ecallEnable);

    import "DPI-C" function void       dpi_onPosEdge_ifuInputValid(input logic ifuInputValid);

    import "DPI-C" function void       dpi_onPosEdge_if_nextStage_valid(input logic if_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_id_nextStage_valid(input logic id_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_ex_nextStage_valid(input logic ex_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_ma_nextStage_valid(input logic ma_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_wb_nextStage_valid(input logic wb_nextStage_valid);
    
    import "DPI-C" function void       dpi_onPosEdge_upcu_pcOutput_valid(input logic upcu_pcOutput_valid);

    import "DPI-C" function bit [31:0] dpi_uart_onReadEnable(input logic uart_read_readEnable);
    import "DPI-C" function void       dpi_uart_onWriteEnable(input logic uart_write_writeEnable);

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
        readData <= dpi_onMemReadEnable(memReadEnable);
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

    always_ff @( posedge uart_read_readEnable ) begin : call_dpi_uart_onReadEnable
        uart_readData <= dpi_uart_onReadEnable(uart_read_readEnable);
    end
    always_ff @( posedge uart_write_writeEnable ) begin : call_dpi_uart_onWriteEnable
        dpi_uart_onWriteEnable(uart_write_writeEnable);
    end
endmodule
