#ifndef __PERF_HPP__
#define __PERF_HPP__ 1

#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>
#include <vector>
#include <ostream>

namespace perf {

// ── Index constants (position in the counter table) ──────────────────────
//
// APPEND-ONLY: add new constants at the TAIL only.
// NEVER rename, reorder, delete, or reassign existing values.
// Values 0–25 are frozen — they MUST match kCounterTable positions 0–25.
namespace Idx {
    constexpr size_t CORE_CYCLE                   =  0;
    constexpr size_t CORE_INSTRET                 =  1;
    constexpr size_t CORE_BUSY_CYCLE              =  2;
    constexpr size_t CORE_STALL_CYCLE             =  3;
    constexpr size_t INST_CLASS_ALU_COUNT         =  4;
    constexpr size_t INST_CLASS_LOAD_COUNT        =  5;
    constexpr size_t INST_CLASS_STORE_COUNT       =  6;
    constexpr size_t INST_CLASS_BRANCH_COUNT      =  7;
    constexpr size_t INST_CLASS_JAL_COUNT         =  8;
    constexpr size_t INST_CLASS_JALR_COUNT        =  9;
    constexpr size_t INST_CLASS_CSR_COUNT         = 10;
    constexpr size_t INST_CLASS_MULDIV_COUNT      = 11;
    constexpr size_t STATE_FETCH_CYCLE            = 12;
    constexpr size_t STATE_DECODE_CYCLE           = 13;
    constexpr size_t STATE_EXECUTE_CYCLE          = 14;
    constexpr size_t STATE_MEMORY_CYCLE           = 15;
    constexpr size_t STATE_WRITEBACK_CYCLE        = 16;
    constexpr size_t STALL_IFETCH_WAIT_RESP_CYCLE = 17;
    constexpr size_t STALL_MEM_WAIT_RESP_CYCLE    = 18;
    constexpr size_t STALL_MEM_REQ_BLOCKED_CYCLE  = 19;
    constexpr size_t STALL_STRUCT_SHARED_MEM_CYCLE= 20;
    constexpr size_t STALL_MULDIV_BUSY_CYCLE      = 21;
    constexpr size_t MEM_LOAD_REQ_COUNT           = 22;
    constexpr size_t MEM_STORE_REQ_COUNT          = 23;
    constexpr size_t MEM_MMIO_REQ_COUNT           = 24;
    constexpr size_t TRAP_EXCEPTION_COUNT         = 25;
    constexpr size_t REG_GPR_WRITE_COUNT          = 26;
    constexpr size_t REG_CSR_WRITE_COUNT          = 27;
    constexpr size_t REG_GPR_SRC_ALU_COUNT        = 28;
    constexpr size_t REG_GPR_SRC_DMEM_COUNT       = 29;
    constexpr size_t REG_GPR_SRC_IMM_COUNT        = 30;
    constexpr size_t REG_GPR_SRC_PC_NEXT_COUNT    = 31;
    constexpr size_t REG_GPR_SRC_BCU_COUNT        = 32;
    constexpr size_t REG_GPR_SRC_CSR_COUNT        = 33;
    constexpr size_t REG_CSR_MODE_RW_COUNT        = 34;
    constexpr size_t REG_CSR_MODE_RS_COUNT        = 35;
    constexpr size_t REG_CSR_MODE_RC_COUNT        = 36;
    constexpr size_t REG_CSR_MODE_IMM_COUNT       = 37;
    constexpr size_t GPR_READ_RS1_COUNT           = 38;
    constexpr size_t GPR_READ_RS2_COUNT           = 39;
    constexpr size_t GPR_READ_BOTH_COUNT          = 40;
    constexpr size_t GPR_READ_RS1_X0_COUNT        = 41;
    constexpr size_t GPR_READ_RS2_X0_COUNT        = 42;
    constexpr size_t GPR_READ_RS1_EQ_RS2_COUNT    = 43;
    constexpr size_t GPR_READ_RS2_UNUSED_COUNT    = 44;
    constexpr size_t GPR_READ_UPPER16_COUNT       = 45;
    constexpr size_t GPR_WRITE_SUPPRESSED_X0_COUNT= 46;
    constexpr size_t CSR_READ_PORT1_ENABLE_COUNT  = 47;
    constexpr size_t CSR_READ_PORT2_ENABLE_COUNT  = 48;
    constexpr size_t CSR_READ_PORT3_ENABLE_COUNT  = 49;
    constexpr size_t CSR_READ_CONCURRENT_2PORT    = 50;
    constexpr size_t CSR_READ_CONCURRENT_3PORT    = 51;
    constexpr size_t CSR_WRITE_NORMAL_COUNT       = 52;
    constexpr size_t CSR_WRITE_TRAP_COUNT         = 53;
    constexpr size_t CSR_WRITE_RETURN_COUNT       = 54;
    constexpr size_t CSR_ADDR_MSTATUS_COUNT       = 55;
    constexpr size_t CSR_ADDR_MTVEC_COUNT         = 56;
    constexpr size_t CSR_ADDR_MEPC_COUNT          = 57;
    constexpr size_t CSR_ADDR_MCAUSE_COUNT        = 58;
    constexpr size_t CSR_ADDR_MTVAL_COUNT         = 59;
    constexpr size_t CSR_ADDR_MVENDORID_COUNT     = 60;
    constexpr size_t CSR_ADDR_MARCHID_COUNT       = 61;
    constexpr size_t IFETCH_REQUEST_COUNT                       = 62;
    constexpr size_t IFETCH_LSU_REQ_FIRE_COUNT                  = 63;
    constexpr size_t IFETCH_AXI_AR_FIRE_COUNT                   = 64;
    constexpr size_t IFETCH_AXI_R_FIRE_COUNT                    = 65;
    constexpr size_t IFETCH_RESPONSE_FIRE_COUNT                 = 66;
    constexpr size_t IFETCH_CONSUMER_READY_AT_RESP_COUNT        = 67;
    constexpr size_t IFETCH_RESPONSE_CONSUMED_FIRST_CYCLE_COUNT = 68;
    constexpr size_t IFETCH_PHASE_ACCEPT_PC_CYCLE               = 69;
    constexpr size_t IFETCH_PHASE_PREPARE_REQUEST_CYCLE         = 70;
    constexpr size_t IFETCH_PHASE_REQUEST_BLOCKED_CYCLE         = 71;
    constexpr size_t IFETCH_PHASE_WAIT_RESPONSE_CYCLE           = 72;
    constexpr size_t IFETCH_PHASE_RESPONSE_BUFFERED_CYCLE       = 73;
    constexpr size_t IFETCH_PHASE_OUTPUT_BLOCKED_CYCLE          = 74;
    constexpr size_t LSU_LOAD_BYTE_COUNT                        = 75;
    constexpr size_t LSU_LOAD_HALF_COUNT                        = 76;
    constexpr size_t LSU_LOAD_WORD_COUNT                        = 77;
    constexpr size_t LSU_LOAD_ALIGNED_COUNT                     = 78;
    constexpr size_t LSU_LOAD_UNALIGNED_COUNT                   = 79;
    constexpr size_t LSU_STORE_BYTE_COUNT                       = 80;
    constexpr size_t LSU_STORE_HALF_COUNT                       = 81;
    constexpr size_t LSU_STORE_WORD_COUNT                       = 82;
    constexpr size_t LSU_STORE_ALIGNED_COUNT                    = 83;
    constexpr size_t LSU_STORE_UNALIGNED_COUNT                  = 84;
    constexpr size_t LSU_UNALIGNED_EXTRA_TRANSACTION_COUNT       = 85;
    constexpr size_t LSU_AXI_AR_FIRE_COUNT                      = 86;
    constexpr size_t LSU_AXI_AW_FIRE_COUNT                      = 87;
    constexpr size_t LSU_AXI_W_FIRE_COUNT                       = 88;
    constexpr size_t LSU_AXI_R_FIRE_COUNT                       = 89;
    constexpr size_t LSU_AXI_B_FIRE_COUNT                       = 90;
    constexpr size_t LSU_STORE_CONCURRENT_READY_OPPORTUNITY_CYCLE = 91;
    constexpr size_t LSU_STORE_AW_DONE_WAIT_W_CYCLE             = 92;
    constexpr size_t LSU_STORE_W_DONE_WAIT_AW_CYCLE             = 93;
    constexpr size_t ALU_OP_ADD_COUNT                           = 94;
    constexpr size_t ALU_OP_SUB_COUNT                           = 95;
    constexpr size_t ALU_OP_SLL_COUNT                           = 96;
    constexpr size_t ALU_OP_SRL_COUNT                           = 97;
    constexpr size_t ALU_OP_SRA_COUNT                           = 98;
    constexpr size_t ALU_OP_AND_COUNT                           = 99;
    constexpr size_t ALU_OP_OR_COUNT                            = 100;
    constexpr size_t ALU_OP_XOR_COUNT                           = 101;
    constexpr size_t EX_ADDER_COMPUTE_COUNT                     = 102;
    constexpr size_t EX_ADDER_AGEN_LS_COUNT                     = 103;
    constexpr size_t EX_ADDER_AGEN_BRANCH_COUNT                 = 104;
    constexpr size_t EX_ADDER_AGEN_AUIPC_COUNT                  = 105;
    constexpr size_t EX_CONC_ALU_ONLY_COUNT                     = 106;
    constexpr size_t EX_CONC_PC_ONLY_COUNT                      = 107;
    constexpr size_t EX_CONC_BOTH_COUNT                         = 108;
} // namespace Idx

// ── Single-source-of-truth counter definition ────────────────────────────

struct PerfCounterDef {
    const char *name;         // e.g. "core.cycle"
    const char *unit;         // e.g. "cycle", "count"
    const char *definition;   // human-readable description
    const char *rawSource;    // DPI signal that drives this counter
};

// ── Lightweight read-only view for TUI / snapshot consumption ────────────

struct PerfCounterView {
    std::string_view name;
    std::string_view unit;
    std::string_view definition;
    uint64_t          value;
};

// ── Accumulator — owns the counter storage ──────────────────────────────

class PerfCounters {
public:
    static constexpr size_t kNumCounters = 109;

    void clear();

    void accumulateCore(uint64_t running, uint64_t commitFire,
                        uint64_t busy, uint64_t stall);
    void accumulateInstClass(uint64_t alu, uint64_t load, uint64_t store,
                             uint64_t branch, uint64_t jal, uint64_t jalr,
                             uint64_t csr, uint64_t muldiv);
    void accumulateState(uint64_t fetch, uint64_t decode, uint64_t execute,
                         uint64_t memory, uint64_t writeback);
    void accumulateStall(uint64_t ifetchWaitResp, uint64_t memWaitResp,
                         uint64_t memReqBlocked, uint64_t structSharedMem,
                         uint64_t muldivBusy);
    void accumulateMem(uint64_t loadReqFire, uint64_t storeReqFire,
                       uint64_t mmioReqFire);
    void accumulateTrap(uint64_t exceptionFire);
    void accumulateReg(uint64_t gprWbFire, uint64_t csrWbFire,
                       uint64_t gprSrcAlu, uint64_t gprSrcDmem,
                       uint64_t gprSrcImm, uint64_t gprSrcPcNext,
                       uint64_t gprSrcBcu, uint64_t gprSrcCsr,
                       uint64_t csrModeRw, uint64_t csrModeRs,
                       uint64_t csrModeRc, uint64_t csrModeImm);
    void accumulateGprCsr(uint64_t gprReadRs1, uint64_t gprReadRs2,
                          uint64_t gprReadBoth, uint64_t gprReadRs1X0,
                          uint64_t gprReadRs2X0, uint64_t gprReadRs1EqRs2,
                          uint64_t gprReadRs2Unused, uint64_t gprReadUpper16,
                          uint64_t gprWriteSuppressedX0,
                          uint64_t csrReadPort1, uint64_t csrReadPort2,
                          uint64_t csrReadPort3, uint64_t csrReadConcurrent2,
                          uint64_t csrReadConcurrent3,
                          uint64_t csrWriteNormal, uint64_t csrWriteTrap,
                          uint64_t csrWriteReturn,
                          uint64_t csrAddrMstatus, uint64_t csrAddrMtvec,
                          uint64_t csrAddrMepc, uint64_t csrAddrMcause,
                           uint64_t csrAddrMtval, uint64_t csrAddrMvendorid,
                           uint64_t csrAddrMarchid);
    void accumulateIfetchLsu(uint64_t ifetchReq, uint64_t ifetchLsuReqFire,
                              uint64_t ifetchAxiArFire, uint64_t ifetchAxiRFire,
                              uint64_t ifetchRespFire, uint64_t ifetchConsumerReadyAtResp,
                              uint64_t ifetchRespConsumedFirstCycle,
                              uint64_t ifetchPhaseAcceptPc, uint64_t ifetchPhasePrepareReq,
                              uint64_t ifetchPhaseReqBlocked, uint64_t ifetchPhaseWaitResp,
                              uint64_t ifetchPhaseRespBuffered, uint64_t ifetchPhaseOutputBlocked,
                              uint64_t lsuLoadByte, uint64_t lsuLoadHalf,
                              uint64_t lsuLoadWord, uint64_t lsuLoadAligned,
                              uint64_t lsuLoadUnaligned, uint64_t lsuStoreByte,
                              uint64_t lsuStoreHalf, uint64_t lsuStoreWord,
                              uint64_t lsuStoreAligned, uint64_t lsuStoreUnaligned,
                              uint64_t lsuUnalignedExtraTrans,
                              uint64_t lsuAxiArFire, uint64_t lsuAxiAwFire,
                              uint64_t lsuAxiWFire, uint64_t lsuAxiRFire,
                              uint64_t lsuAxiBFire,
                               uint64_t lsuConcurrentReadyOpp, uint64_t lsuAwDoneWaitW,
                               uint64_t lsuWDoneWaitAw);
    void accumulateEx(uint64_t aluOpAdd, uint64_t aluOpSub,
                      uint64_t aluOpSll, uint64_t aluOpSrl,
                      uint64_t aluOpSra, uint64_t aluOpAnd,
                      uint64_t aluOpOr,  uint64_t aluOpXor,
                      uint64_t adderCompute, uint64_t adderAgenLs,
                      uint64_t adderAgenBranch, uint64_t adderAgenAuipc,
                      uint64_t concAluOnly, uint64_t concPcOnly,
                      uint64_t concBoth);

    // Direct indexed access (for dump / derived metric computation)
    uint64_t get(size_t idx) const { return m_values[idx]; }

    // Build a full snapshot view for TUI consumption
    std::vector<PerfCounterView> view() const;

private:
    std::array<uint64_t, kNumCounters> m_values{};
};

// ── PerfMonitor — lifecycle + polling entry point ──────────────────────

class PerfMonitor {
public:
    static constexpr size_t kNumCounters = PerfCounters::kNumCounters;

    /// Clear accumulators before a reset; enter reset-gate state.
    void onResetBegin();

    /// Exit reset-gate state; counters are already zeroed.
    void onResetEnd();

    /// Poll RTL perf signals once per stable clock cycle.
    ///
    /// Template parameter DPIModule must expose the 26 public `perf_*`
    /// members generated by Verilator from the `GeneralDPIAdapter`.
    template <typename DPIModule>
    void sampleCycle(const DPIModule *dpi) {
        if (m_resetting) return;
        accumulateCore(
            dpi->perf_core_running,
            dpi->perf_core_commitFire,
            dpi->perf_core_busy,
            dpi->perf_core_stall);
        accumulateInstClass(
            dpi->perf_inst_alu,
            dpi->perf_inst_load,
            dpi->perf_inst_store,
            dpi->perf_inst_branch,
            dpi->perf_inst_jal,
            dpi->perf_inst_jalr,
            dpi->perf_inst_csr,
            dpi->perf_inst_muldiv);
        accumulateState(
            dpi->perf_state_fetch_cycle,
            dpi->perf_state_decode_cycle,
            dpi->perf_state_execute_cycle,
            dpi->perf_state_memory_cycle,
            dpi->perf_state_writeback_cycle);
        accumulateStall(
            dpi->perf_stall_ifetch_wait_resp,
            dpi->perf_stall_mem_wait_resp,
            dpi->perf_stall_mem_req_blocked,
            dpi->perf_stall_structural_shared_mem,
            dpi->perf_stall_muldiv_busy);
        accumulateMem(
            dpi->perf_mem_load_req_fire,
            dpi->perf_mem_store_req_fire,
            dpi->perf_mem_mmio_req_fire);
        accumulateTrap(dpi->perf_trap_exception_fire);
        accumulateReg(
            dpi->perf_reg_gpr_wb_fire,
            dpi->perf_reg_csr_wb_fire,
            dpi->perf_reg_gpr_src_alu,
            dpi->perf_reg_gpr_src_dmem,
            dpi->perf_reg_gpr_src_imm,
            dpi->perf_reg_gpr_src_pc_next,
            dpi->perf_reg_gpr_src_bcu,
            dpi->perf_reg_gpr_src_csr,
            dpi->perf_reg_csr_mode_rw,
            dpi->perf_reg_csr_mode_rs,
            dpi->perf_reg_csr_mode_rc,
            dpi->perf_reg_csr_mode_imm);
        accumulateGprCsr(
            dpi->perf_gpr_gpr_read_rs1,
            dpi->perf_gpr_gpr_read_rs2,
            dpi->perf_gpr_gpr_read_both,
            dpi->perf_gpr_gpr_read_rs1_x0,
            dpi->perf_gpr_gpr_read_rs2_x0,
            dpi->perf_gpr_gpr_read_rs1_eq_rs2,
            dpi->perf_gpr_gpr_read_rs2_unused,
            dpi->perf_gpr_gpr_read_upper16,
            dpi->perf_gpr_gpr_write_suppressed_x0,
            dpi->perf_csr_csr_read_port1_enable,
            dpi->perf_csr_csr_read_port2_enable,
            dpi->perf_csr_csr_read_port3_enable,
            dpi->perf_csr_csr_read_concurrent_2port,
            dpi->perf_csr_csr_read_concurrent_3port,
            dpi->perf_csr_csr_write_normal,
            dpi->perf_csr_csr_write_trap,
            dpi->perf_csr_csr_write_return,
            dpi->perf_csr_csr_addr_mstatus,
            dpi->perf_csr_csr_addr_mtvec,
            dpi->perf_csr_csr_addr_mepc,
            dpi->perf_csr_csr_addr_mcause,
            dpi->perf_csr_csr_addr_mtval,
            dpi->perf_csr_csr_addr_mvendorid,
            dpi->perf_csr_csr_addr_marchid);
        accumulateIfetchLsu(
            dpi->perf_ifetch_request_fire,
            dpi->perf_ifetch_lsu_req_fire,
            dpi->perf_ifetch_axi_ar_fire,
            dpi->perf_ifetch_axi_r_fire,
            dpi->perf_ifetch_response_fire,
            dpi->perf_ifetch_consumer_ready_at_response,
            dpi->perf_ifetch_response_consumed_first_cycle,
            dpi->perf_ifetch_phase_accept_pc,
            dpi->perf_ifetch_phase_prepare_request,
            dpi->perf_ifetch_phase_request_blocked,
            dpi->perf_ifetch_phase_wait_response,
            dpi->perf_ifetch_phase_response_buffered,
            dpi->perf_ifetch_phase_output_blocked,
            dpi->perf_lsu_load_byte_fire,
            dpi->perf_lsu_load_half_fire,
            dpi->perf_lsu_load_word_fire,
            dpi->perf_lsu_load_aligned_fire,
            dpi->perf_lsu_load_unaligned_fire,
            dpi->perf_lsu_store_byte_fire,
            dpi->perf_lsu_store_half_fire,
            dpi->perf_lsu_store_word_fire,
            dpi->perf_lsu_store_aligned_fire,
            dpi->perf_lsu_store_unaligned_fire,
            dpi->perf_lsu_unaligned_extra_transaction,
            dpi->perf_lsu_axi_ar_fire,
            dpi->perf_lsu_axi_aw_fire,
            dpi->perf_lsu_axi_w_fire,
            dpi->perf_lsu_axi_r_fire,
            dpi->perf_lsu_axi_b_fire,
            dpi->perf_lsu_concurrent_ready_opportunity,
            dpi->perf_lsu_aw_done_wait_w,
            dpi->perf_lsu_w_done_wait_aw);
        accumulateEx(
            dpi->perf_ex_alu_op_add,
            dpi->perf_ex_alu_op_sub,
            dpi->perf_ex_alu_op_sll,
            dpi->perf_ex_alu_op_srl,
            dpi->perf_ex_alu_op_sra,
            dpi->perf_ex_alu_op_and,
            dpi->perf_ex_alu_op_or,
            dpi->perf_ex_alu_op_xor,
            dpi->perf_ex_adder_compute,
            dpi->perf_ex_adder_agen_ls,
            dpi->perf_ex_adder_agen_branch,
            dpi->perf_ex_adder_agen_auipc,
            dpi->perf_ex_concurrency_alu_only,
            dpi->perf_ex_concurrency_pc_only,
            dpi->perf_ex_concurrency_both);
    }

    /// Return a read-only snapshot of all 26 counters.
    std::vector<PerfCounterView> view() const { return m_counters.view(); }

    /// Raw counter access (for derived metric computation).
    const PerfCounters &counters() const { return m_counters; }
    uint64_t get(size_t idx) const { return m_counters.get(idx); }

    /// Get the counter definition at a given index.
    static const PerfCounterDef &counterDef(size_t idx);

    /// Enable strict closure checks in dumpSummary().
    void setStrict(bool v) { m_strict = v; }

    /// Print a summary of all counters to an output stream.
    /// When strict mode is enabled, runs closure checks on counter relationships.
    void dumpSummary(std::ostream &os) const;

    /// Write cycles, instret, ipc, and every perf counter as JSON.
    /// Returns true on success; fails closed on any I/O or formatting error.
    bool dumpJson(const std::string &path) const;

private:
    void accumulateCore(uint64_t running, uint64_t commitFire,
                        uint64_t busy, uint64_t stall) {
        m_counters.accumulateCore(running, commitFire, busy, stall);
    }
    void accumulateInstClass(uint64_t alu, uint64_t load, uint64_t store,
                             uint64_t branch, uint64_t jal, uint64_t jalr,
                             uint64_t csr, uint64_t muldiv) {
        m_counters.accumulateInstClass(alu, load, store, branch, jal, jalr, csr, muldiv);
    }
    void accumulateState(uint64_t fetch, uint64_t decode, uint64_t execute,
                         uint64_t memory, uint64_t writeback) {
        m_counters.accumulateState(fetch, decode, execute, memory, writeback);
    }
    void accumulateStall(uint64_t ifetchWaitResp, uint64_t memWaitResp,
                         uint64_t memReqBlocked, uint64_t structSharedMem,
                         uint64_t muldivBusy) {
        m_counters.accumulateStall(ifetchWaitResp, memWaitResp, memReqBlocked,
                                   structSharedMem, muldivBusy);
    }
    void accumulateMem(uint64_t loadReqFire, uint64_t storeReqFire,
                       uint64_t mmioReqFire) {
        m_counters.accumulateMem(loadReqFire, storeReqFire, mmioReqFire);
    }
    void accumulateTrap(uint64_t exceptionFire) {
        m_counters.accumulateTrap(exceptionFire);
    }
    void accumulateReg(uint64_t gprWbFire, uint64_t csrWbFire,
                        uint64_t gprSrcAlu, uint64_t gprSrcDmem,
                        uint64_t gprSrcImm, uint64_t gprSrcPcNext,
                        uint64_t gprSrcBcu, uint64_t gprSrcCsr,
                        uint64_t csrModeRw, uint64_t csrModeRs,
                        uint64_t csrModeRc, uint64_t csrModeImm) {
        m_counters.accumulateReg(gprWbFire, csrWbFire,
                                 gprSrcAlu, gprSrcDmem,
                                 gprSrcImm, gprSrcPcNext,
                                 gprSrcBcu, gprSrcCsr,
                                 csrModeRw, csrModeRs, csrModeRc, csrModeImm);
    }
    void accumulateGprCsr(uint64_t gprReadRs1, uint64_t gprReadRs2,
                          uint64_t gprReadBoth, uint64_t gprReadRs1X0,
                          uint64_t gprReadRs2X0, uint64_t gprReadRs1EqRs2,
                          uint64_t gprReadRs2Unused, uint64_t gprReadUpper16,
                          uint64_t gprWriteSuppressedX0,
                          uint64_t csrReadPort1, uint64_t csrReadPort2,
                          uint64_t csrReadPort3, uint64_t csrReadConcurrent2,
                          uint64_t csrReadConcurrent3,
                          uint64_t csrWriteNormal, uint64_t csrWriteTrap,
                          uint64_t csrWriteReturn,
                          uint64_t csrAddrMstatus, uint64_t csrAddrMtvec,
                          uint64_t csrAddrMepc, uint64_t csrAddrMcause,
                          uint64_t csrAddrMtval, uint64_t csrAddrMvendorid,
                          uint64_t csrAddrMarchid) {
        m_counters.accumulateGprCsr(gprReadRs1, gprReadRs2,
                                    gprReadBoth, gprReadRs1X0,
                                    gprReadRs2X0, gprReadRs1EqRs2,
                                    gprReadRs2Unused, gprReadUpper16,
                                    gprWriteSuppressedX0,
                                    csrReadPort1, csrReadPort2,
                                    csrReadPort3, csrReadConcurrent2,
                                    csrReadConcurrent3,
                                    csrWriteNormal, csrWriteTrap,
                                    csrWriteReturn,
                                    csrAddrMstatus, csrAddrMtvec,
                                    csrAddrMepc, csrAddrMcause,
                                    csrAddrMtval, csrAddrMvendorid,
                                    csrAddrMarchid);
    }
    void accumulateIfetchLsu(uint64_t ifetchReq, uint64_t ifetchLsuReqFire,
                              uint64_t ifetchAxiArFire, uint64_t ifetchAxiRFire,
                              uint64_t ifetchRespFire, uint64_t ifetchConsumerReadyAtResp,
                              uint64_t ifetchRespConsumedFirstCycle,
                              uint64_t ifetchPhaseAcceptPc, uint64_t ifetchPhasePrepareReq,
                              uint64_t ifetchPhaseReqBlocked, uint64_t ifetchPhaseWaitResp,
                              uint64_t ifetchPhaseRespBuffered, uint64_t ifetchPhaseOutputBlocked,
                              uint64_t lsuLoadByte, uint64_t lsuLoadHalf,
                              uint64_t lsuLoadWord, uint64_t lsuLoadAligned,
                              uint64_t lsuLoadUnaligned, uint64_t lsuStoreByte,
                              uint64_t lsuStoreHalf, uint64_t lsuStoreWord,
                              uint64_t lsuStoreAligned, uint64_t lsuStoreUnaligned,
                              uint64_t lsuUnalignedExtraTrans,
                              uint64_t lsuAxiArFire, uint64_t lsuAxiAwFire,
                              uint64_t lsuAxiWFire, uint64_t lsuAxiRFire,
                              uint64_t lsuAxiBFire,
                              uint64_t lsuConcurrentReadyOpp, uint64_t lsuAwDoneWaitW,
                              uint64_t lsuWDoneWaitAw) {
        m_counters.accumulateIfetchLsu(ifetchReq, ifetchLsuReqFire,
                                       ifetchAxiArFire, ifetchAxiRFire,
                                       ifetchRespFire, ifetchConsumerReadyAtResp,
                                       ifetchRespConsumedFirstCycle,
                                       ifetchPhaseAcceptPc, ifetchPhasePrepareReq,
                                       ifetchPhaseReqBlocked, ifetchPhaseWaitResp,
                                       ifetchPhaseRespBuffered, ifetchPhaseOutputBlocked,
                                       lsuLoadByte, lsuLoadHalf,
                                       lsuLoadWord, lsuLoadAligned,
                                       lsuLoadUnaligned, lsuStoreByte,
                                       lsuStoreHalf, lsuStoreWord,
                                       lsuStoreAligned, lsuStoreUnaligned,
                                       lsuUnalignedExtraTrans,
                                       lsuAxiArFire, lsuAxiAwFire,
                                       lsuAxiWFire, lsuAxiRFire,
                                       lsuAxiBFire,
                                       lsuConcurrentReadyOpp, lsuAwDoneWaitW,
                                       lsuWDoneWaitAw);
    }
    void accumulateEx(uint64_t aluOpAdd, uint64_t aluOpSub,
                      uint64_t aluOpSll, uint64_t aluOpSrl,
                      uint64_t aluOpSra, uint64_t aluOpAnd,
                      uint64_t aluOpOr,  uint64_t aluOpXor,
                      uint64_t adderCompute, uint64_t adderAgenLs,
                      uint64_t adderAgenBranch, uint64_t adderAgenAuipc,
                      uint64_t concAluOnly, uint64_t concPcOnly,
                      uint64_t concBoth) {
        m_counters.accumulateEx(aluOpAdd, aluOpSub,
                                aluOpSll, aluOpSrl,
                                aluOpSra, aluOpAnd,
                                aluOpOr,  aluOpXor,
                                adderCompute, adderAgenLs,
                                adderAgenBranch, adderAgenAuipc,
                                concAluOnly, concPcOnly,
                                concBoth);
    }

    PerfCounters m_counters;
    bool         m_resetting{false};
    bool         m_strict{false};
};

/// Global singleton — instantiated at static-init time, lives for the
/// entire simulation process.
extern PerfMonitor g_perfMonitor;

} // namespace perf

#endif /* __PERF_HPP__ */
