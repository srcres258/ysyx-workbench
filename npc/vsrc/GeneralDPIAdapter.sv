module GeneralDPIAdapter (
    input         clock,
    input         reset,

    input  [31:0] core_pc /*verilator public*/,
    input         core_halt /*verilator public*/,
    input         core_executing /*verilator public*/,
    input         core_ifuInputValid /*verilator public*/,

    input         physicalRAM_read_readEnable /*verilator public*/,
    input  [31:0] physicalRAM_read_readAddress /*verilator public*/,
    output [31:0] physicalRAM_read_readData,
    input         physicalRAM_write_writeEnable /*verilator public*/,
    input  [31:0] physicalRAM_write_writeAddress /*verilator public*/,
    input  [3:0]  physicalRAM_write_writeDataStrobe /*verilator public*/,
    input  [31:0] physicalRAM_write_writeData /*verilator public*/,

    input         uart_read_readEnable /*verilator public*/,
    input  [31:0] uart_read_readAddress /*verilator public*/,
    output [31:0] uart_read_readData,
    input         uart_write_writeEnable /*verilator public*/,
    input  [31:0] uart_write_writeAddress /*verilator public*/,
    input  [3:0]  uart_write_writeDataStrobe /*verilator public*/,
    input  [31:0] uart_write_writeData /*verilator public*/,

    input         clint_read_readEnable /*verilator public*/,
    input  [31:0] clint_read_readAddress /*verilator public*/,
    output [31:0] clint_read_readData,
    input         clint_write_writeEnable /*verilator public*/,
    input  [31:0] clint_write_writeAddress /*verilator public*/,
    input  [3:0]  clint_write_writeDataStrobe /*verilator public*/,
    input  [31:0] clint_write_writeData /*verilator public*/,

    input  [31:0] gpr_gprs_0 /*verilator public*/,
    input  [31:0] gpr_gprs_1 /*verilator public*/,
    input  [31:0] gpr_gprs_2 /*verilator public*/,
    input  [31:0] gpr_gprs_3 /*verilator public*/,
    input  [31:0] gpr_gprs_4 /*verilator public*/,
    input  [31:0] gpr_gprs_5 /*verilator public*/,
    input  [31:0] gpr_gprs_6 /*verilator public*/,
    input  [31:0] gpr_gprs_7 /*verilator public*/,
    input  [31:0] gpr_gprs_8 /*verilator public*/,
    input  [31:0] gpr_gprs_9 /*verilator public*/,
    input  [31:0] gpr_gprs_10 /*verilator public*/,
    input  [31:0] gpr_gprs_11 /*verilator public*/,
    input  [31:0] gpr_gprs_12 /*verilator public*/,
    input  [31:0] gpr_gprs_13 /*verilator public*/,
    input  [31:0] gpr_gprs_14 /*verilator public*/,
    input  [31:0] gpr_gprs_15 /*verilator public*/,
    input  [31:0] gpr_gprs_16 /*verilator public*/,
    input  [31:0] gpr_gprs_17 /*verilator public*/,
    input  [31:0] gpr_gprs_18 /*verilator public*/,
    input  [31:0] gpr_gprs_19 /*verilator public*/,
    input  [31:0] gpr_gprs_20 /*verilator public*/,
    input  [31:0] gpr_gprs_21 /*verilator public*/,
    input  [31:0] gpr_gprs_22 /*verilator public*/,
    input  [31:0] gpr_gprs_23 /*verilator public*/,
    input  [31:0] gpr_gprs_24 /*verilator public*/,
    input  [31:0] gpr_gprs_25 /*verilator public*/,
    input  [31:0] gpr_gprs_26 /*verilator public*/,
    input  [31:0] gpr_gprs_27 /*verilator public*/,
    input  [31:0] gpr_gprs_28 /*verilator public*/,
    input  [31:0] gpr_gprs_29 /*verilator public*/,
    input  [31:0] gpr_gprs_30 /*verilator public*/,
    input  [31:0] gpr_gprs_31 /*verilator public*/,
    input  [31:0] csr_csr_mstatus /*verilator public*/,
    input  [31:0] csr_csr_mtvec /*verilator public*/,
    input  [31:0] csr_csr_mepc /*verilator public*/,
    input  [31:0] csr_csr_mcause /*verilator public*/,
    input  [31:0] csr_csr_mtval /*verilator public*/,

    input         ifu_if_nextStage_valid /*verilator public*/,
    input  [4:0]  idu_rs1 /*verilator public*/,
    input  [4:0]  idu_rs2 /*verilator public*/,
    input  [4:0]  idu_rd /*verilator public*/,
    input  [31:0] idu_imm /*verilator public*/,
    input  [31:0] idu_rs1Data /*verilator public*/,
    input  [31:0] idu_rs2Data /*verilator public*/,
    input  [31:0] idu_inst /*verilator public*/,
    input         idu_inst_jal /*verilator public*/,
    input         idu_inst_jalr /*verilator public*/,
    input         idu_id_nextStage_valid /*verilator public*/,
    input         exu_ecallEnable /*verilator public*/,
    input         exu_ex_nextStage_valid /*verilator public*/,
    input         mau_memWriteEnable /*verilator public*/,
    input         mau_memReadEnable /*verilator public*/,
    input         mau_ma_nextStage_valid /*verilator public*/,
    input         wbu_wb_nextStage_valid /*verilator public*/,
    input         upcu_upcu_pcOutput_valid /*verilator public*/
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

    logic [31:0] clint_readData;
    initial clint_readData = 32'h0;
    assign clint_read_readData = clint_readData;

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

    import "DPI-C" function bit [31:0] dpi_clint_onReadEnable(input logic clint_read_readEnable);
    import "DPI-C" function void       dpi_clint_onWriteEnable(input logic clint_write_writeEnable);

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

    always_ff @( posedge clint_read_readEnable ) begin : call_dpi_clint_onReadEnable
        clint_readData <= dpi_clint_onReadEnable(clint_read_readEnable);
    end
    always_ff @( posedge clint_write_writeEnable ) begin : call_dpi_clint_onWriteEnable
        dpi_clint_onWriteEnable(clint_write_writeEnable);
    end
endmodule
