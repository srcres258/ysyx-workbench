module GeneralDPIAdapter (
    input         clock,
    input         reset,

    input  [31:0] core_pc /*verilator public*/,
    input         core_halt /*verilator public*/,
    input         core_executing /*verilator public*/,
    input         core_ifuInputValid /*verilator public*/,

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
    input  [31:0] csr_csr_mvendorid /*verilator public*/,
    input  [31:0] csr_csr_marchid /*verilator public*/,

    input         ifu_if_nextStage_valid /*verilator public*/,
    input  [31:0] ifu_instData /*verilator public*/,
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
    input         exu_epcRecoverEnable /*verilator public*/,
    input  [31:0] exu_exPc /*verilator public*/,
    input         exu_ex_nextStage_valid /*verilator public*/,
    input         memu_memWriteEnable /*verilator public*/,
    input         memu_memReadEnable /*verilator public*/,
    input  [31:0] memu_memAddr /*verilator public*/,
    input  [31:0] memu_memData /*verilator public*/,
    input  [3:0]  memu_memStrobe /*verilator public*/,
    input  [1:0]  memu_memResp /*verilator public*/,
    input  [3:0]  memu_memLsType /*verilator public*/,
    input  [31:0] memu_memPc /*verilator public*/,
    input         memu_mem_nextStage_valid /*verilator public*/,
    input         wbu_wb_nextStage_valid /*verilator public*/
);
    logic halt = core_halt;
    logic inst_jal = idu_inst_jal;
    logic inst_jalr = idu_inst_jalr;
    logic ecallEnable = exu_ecallEnable;
    logic epcRecoverEnable = exu_epcRecoverEnable;
    logic [31:0] exPc = exu_exPc;
    logic ifuInputValid = core_ifuInputValid;
    logic if_nextStage_valid = ifu_if_nextStage_valid;
    logic id_nextStage_valid = idu_id_nextStage_valid;
    logic ex_nextStage_valid = exu_ex_nextStage_valid;
    logic mem_nextStage_valid = memu_mem_nextStage_valid;
    logic wb_nextStage_valid = wbu_wb_nextStage_valid;

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
     * 触发环境调用，以记录 etrace 日志
     */
    import "DPI-C" function void       dpi_onEcallEnable(input logic [31:0] pc, input logic ecallEnable);
    /**
     * 触发异常返回，以记录 etrace 日志
     */
    import "DPI-C" function void       dpi_onEpcRecoverEnable(input logic [31:0] pc, input logic epcRecoverEnable);

    /**
     * 触发访存，以记录 mtrace 日志
     */
    import "DPI-C" function void       dpi_onMemAccess(
        input logic [31:0] memPc,
        input logic memWriteEnable,
        input logic memReadEnable,
        input logic [31:0] memAddr,
        input logic [31:0] memData,
        input logic [3:0] memStrobe,
        input logic [1:0] memResp,
        input logic [3:0] memLsType
    );

    import "DPI-C" function void       dpi_onPosEdge_ifuInputValid(input logic ifuInputValid);

    import "DPI-C" function void       dpi_onPosEdge_if_nextStage_valid(input logic if_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_id_nextStage_valid(input logic id_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_ex_nextStage_valid(input logic ex_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_mem_nextStage_valid(input logic mem_nextStage_valid);
    import "DPI-C" function void       dpi_onPosEdge_wb_nextStage_valid(input logic wb_nextStage_valid);

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
    always_ff @( posedge ecallEnable ) begin : call_dpi_onEcallEnable
        dpi_onEcallEnable(exPc, ecallEnable);
    end
    always_ff @( posedge epcRecoverEnable ) begin : call_dpi_onEpcRecoverEnable
        dpi_onEpcRecoverEnable(exPc, epcRecoverEnable);
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
    always_ff @( posedge mem_nextStage_valid ) begin : call_dpi_onPosEdge_mem_nextStage_valid
        dpi_onPosEdge_mem_nextStage_valid(mem_nextStage_valid);
        dpi_onMemAccess(
            memu_memPc,
            memu_memWriteEnable,
            memu_memReadEnable,
            memu_memAddr,
            memu_memData,
            memu_memStrobe,
            memu_memResp,
            memu_memLsType
        );
    end
    always_ff @( posedge wb_nextStage_valid ) begin : call_dpi_onPosEdge_wb_nextStage_valid
        dpi_onPosEdge_wb_nextStage_valid(wb_nextStage_valid);
    end

    always_ff @( posedge clint_read_readEnable ) begin : call_dpi_clint_onReadEnable
        clint_readData <= dpi_clint_onReadEnable(clint_read_readEnable);
    end
    always_ff @( posedge clint_write_writeEnable ) begin : call_dpi_clint_onWriteEnable
        dpi_clint_onWriteEnable(clint_write_writeEnable);
    end
endmodule
