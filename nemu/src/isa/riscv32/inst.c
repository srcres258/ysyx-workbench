/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include "local-include/reg.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>

#include <utils.h>

#define R(i) gpr(i)
#define Mr vaddr_read
#define Mw vaddr_write

#define SEXT_REG(x) SEXT(x, sizeof(word_t) * 8)

enum {
  TYPE_R, TYPE_I, TYPE_S, TYPE_B, TYPE_U, TYPE_J,
  TYPE_N, // none
};

#define src1R() do { *src1 = R(rs1); *ssrc1 = (int) *src1; } while (0)
#define src2R() do { *src2 = R(rs2); *ssrc2 = (int) *src2; \
                     *src2m = ((word_t) *src2) & 0b11111; } while (0)
#define immI() do { *imm = SEXT(BITS(i, 31, 20), 12); \
                    *simm = (int) *imm; \
                    *immm = ((word_t) *imm) & 0b11111; } while (0)
#define immS() do { *imm = (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); \
                    *simm = (int) *imm; \
                    *immm = ((word_t) *imm) & 0b11111; } while (0)
#define immB() do { *imm = (SEXT(BITS(i, 31, 31), 1) << 12) | (BITS(i, 7, 7) << 11) | \
                      (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1); \
                    *simm = (int) *imm; \
                    *immm = ((word_t) *imm) & 0b11111; } while (0)
#define immU() do { *imm = SEXT(BITS(i, 31, 12), 20) << 12; \
                    *simm = (int) *imm; \
                    *immm = ((word_t) *imm) & 0b11111; } while (0)
#define immJ() do { *imm = (SEXT(BITS(i, 31, 31), 1) << 20) | (BITS(i, 19, 12) << 12) | \
                      (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1); \
                    *simm = (int) *imm; \
                    *immm = ((word_t) *imm) & 0b11111; } while (0)

static void decode_operand(
  Decode *s, int *rd,
  int *rs1p, int *rs2p,
  word_t *src1, word_t *src2,
  int *ssrc1, int *ssrc2,
  word_t *src2m,
  word_t *imm, int *simm,
  word_t *immm,
  int type
) {
  uint32_t i = s->isa.inst;
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  *rd     = BITS(i, 11, 7);
  *rs1p   = rs1;
  *rs2p   = rs2;
  switch (type) {
    case TYPE_R: src1R(); src2R();         break;
    case TYPE_I: src1R();          immI(); break;
    case TYPE_S: src1R(); src2R(); immS(); break;
    case TYPE_B: src1R(); src2R(); immB(); break;
    case TYPE_U:                   immU(); break;
    case TYPE_J:                   immJ(); break;
    case TYPE_N: break;
    default: panic("unsupported type = %d", type);
  }
}

static inline word_t inst_mul(word_t rs1, word_t rs2) {
  uint64_t rs1e, rs2e, r;

  rs1e = (uint64_t) rs1;
  rs2e = (uint64_t) rs2;

  r = rs1e * rs2e;

  return (word_t) (r & 0xFFFFFFFFLL);
}

static inline word_t inst_mulh(word_t rs1, word_t rs2) {
  int64_t srs1, srs2, sr;
  uint64_t r;

  srs1 = (int64_t) SEXT_REG(rs1);
  srs2 = (int64_t) SEXT_REG(rs2);

  sr = srs1 * srs2;
  r = (uint64_t) sr;

  return (word_t) (r >> 32);
}

static inline word_t inst_mulhsu(word_t rs1, word_t rs2) {
  int64_t srs1, sr;
  uint64_t rs2e, r;

  srs1 = (int64_t) SEXT_REG(rs1);
  rs2e = (uint64_t) rs2;

  sr = srs1 * rs2e;
  r = (uint64_t) sr;

  return (word_t) (r >> 32);
}

static inline word_t inst_mulhu(word_t rs1, word_t rs2) {
  uint64_t rs1e, rs2e, r;

  rs1e = (uint64_t) rs1;
  rs2e = (uint64_t) rs2;

  r = rs1e * rs2e;

  return (word_t) (r >> 32);
}

static inline word_t inst_div(word_t rs1, word_t rs2) {
  int srs1, srs2, sr;

  srs1 = (int) rs1;
  srs2 = (int) rs2;

  sr = srs1 / srs2;

  return (word_t) sr;
}

static inline word_t inst_divu(word_t rs1, word_t rs2) {
  return rs1 / rs2;
}

static inline word_t inst_rem(word_t rs1, word_t rs2) {
  int srs1, srs2, sr;

  srs1 = (int) rs1;
  srs2 = (int) rs2;

  sr = srs1 % srs2;

  return (word_t) sr;
}

static inline word_t inst_remu(word_t rs1, word_t rs2) {
  return rs1 % rs2;
}

#ifdef CONFIG_FTRACE
static bool is_addr_func_sym_start(word_t addr) {
  size_t i;
  Symbol *sym;

  for (i = 0; i < nemu_state.ftrace_func_syms_size; i++) {
    sym = &nemu_state.ftrace_func_syms[i];
    if (addr == sym->addr) {
      return true;
    }
  }

  return false;
}

static void handle_ftrace_inst_jalr(Decode *s, int rd, int rs1, int src1, int imm) {
  word_t dest_addr;

  dest_addr = (word_t) (src1 + imm);

  if (is_addr_func_sym_start(dest_addr) || (rd == 1 && rs1 == 1)) {
    // 情况：1. 目的地址是函数起始地址 
    //    或2. rd 为 x1， rs1 为 x1
    // 推测：该 jalr 指令可能来源于 call 伪指令
    Log_info("Detected call from jalr at pc = " FMT_PADDR ", dest_addr = " FMT_PADDR, s->pc, dest_addr);
    if (nemu_ftrace_record_and_log(CALL_TYPE_CALL, s->pc, dest_addr)) {
      Log_info("Detect succeeded.");
    } else {
      Log_info("Detect failed.");
    }
    return;
  }

  if (rd == 0 && rs1 == 1) {
    // 情况：rd 为 x0， rs1 为 x1
    // 推测：该 jalr 指令可能来源于 ret 伪指令
    Log_info("Detected ret from jalr at pc = " FMT_PADDR ", dest_addr = " FMT_PADDR, s->pc, dest_addr);
    if (nemu_ftrace_record_and_log(CALL_TYPE_RET, s->pc, dest_addr)) {
      Log_info("Detect succeeded.");
    } else {
      Log_info("Detect failed.");
    }
    return;
  }

  if (rd == 1 && (rs1 == 6 || rs1 == 7)) {
    // 情况：rd 为 x1， rs1 为 x6 或 x7
    // 推测：该 jalr 指令可能来源于 tail 伪指令
    Log_info("Detected tail from jalr at pc = " FMT_PADDR ", dest_addr = " FMT_PADDR, s->pc, dest_addr);
    if (nemu_ftrace_record_and_log(CALL_TYPE_TAIL, s->pc, dest_addr)) {
      Log_info("Detect succeeded.");
    } else {
      Log_info("Detect failed.");
    }
    return;
  }
}

static void handle_ftrace_inst_jal(Decode *s, int rd, int imm) {
  word_t dest_addr;

  dest_addr = (word_t) ((int) s->pc + imm);

  if (rd == 1) {
    // 情况：rd 为 x1
    // 推测：该 jal 指令可能来源于 call 伪指令
    Log_info("Detected call from jal at pc = " FMT_PADDR ", dest_addr = " FMT_PADDR, s->pc, dest_addr);
    if (nemu_ftrace_record_and_log(CALL_TYPE_CALL, s->pc, dest_addr)) {
      Log_info("Detect succeeded.");
    } else {
      Log_info("Detect failed.");
    }
    return;
  }
}

static void handle_ftrace(Decode *s);
#endif

static int decode_exec(Decode *s) {
  s->dnpc = s->snpc;

#define INSTPAT_INST(s) ((s)->isa.inst)
#define INSTPAT_MATCH(s, name, type, ... /* execute body */ ) { \
  int rd = 0; int rs1 = 0; int rs2 = 0; \
  word_t src1 = 0, src2 = 0, imm = 0, src2m = 0, immm = 0; \
  int ssrc1 = 0, ssrc2 = 0, simm = 0; \
  decode_operand(s, &rd, \
    &rs1, &rs2, \
    &src1, &src2, \
    &ssrc1, &ssrc2, \
    &src2m, \
    &imm, &simm, &immm, \
    concat(TYPE_, type) \
  ); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();

  /* ----- RV32I 指令模块 ----- */

  // R-Type 指令
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add     , R, R(rd) = src1 + src2);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub     , R, R(rd) = src1 - src2);
  INSTPAT("0000000 ????? ????? 001 ????? 01100 11", sll     , R, R(rd) = src1 << src2m);
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt     , R, R(rd) = ssrc1 < ssrc2 ? 1 : 0);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu    , R, R(rd) = src1 < src2 ? 1 : 0);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor     , R, R(rd) = src1 ^ src2);
  INSTPAT("0000000 ????? ????? 101 ????? 01100 11", srl     , R, R(rd) = src1 >> src2m);
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra     , R, R(rd) = (word_t) (ssrc1 >> src2m));
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or      , R, R(rd) = src1 | src2);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and     , R, R(rd) = src1 & src2);

  // I-Type 指令
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr    , I, R(rd) = s->pc + 4, s->dnpc = src1 + imm);
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb      , I, R(rd) = (word_t) SEXT(Mr(src1 + imm, 1), 8));
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh      , I, R(rd) = (word_t) SEXT(Mr(src1 + imm, 2), 16));
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw      , I, R(rd) = Mr(src1 + imm, 4));
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu     , I, R(rd) = 0, R(rd) = Mr(src1 + imm, 1));
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu     , I, R(rd) = 0, R(rd) = Mr(src1 + imm, 2));
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi    , I, R(rd) = src1 + imm);
  INSTPAT("??????? ????? ????? 010 ????? 00100 11", slti    , I, R(rd) = ssrc1 < simm ? 1 : 0);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu   , I, R(rd) = src1 < imm ? 1 : 0);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori    , I, R(rd) = src1 ^ imm);
  INSTPAT("??????? ????? ????? 110 ????? 00100 11", ori     , I, R(rd) = src1 | imm);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", ori     , I, R(rd) = src1 & imm);
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", slli    , I, R(rd) = src1 << immm);
  INSTPAT("0000000 ????? ????? 101 ????? 00100 11", srli    , I, R(rd) = src1 >> immm);
  INSTPAT("0100000 ????? ????? 101 ????? 00100 11", srai    , I, R(rd) = (word_t) (ssrc1 >> immm));

  // S-Type 指令
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb      , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh      , S, Mw(src1 + imm, 2, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw      , S, Mw(src1 + imm, 4, src2));

  // B-Type 指令（S-Type的变种）
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq     , B, if (src1 == src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne     , B, if (src1 != src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt     , B, if (ssrc1 < ssrc2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge     , B, if (ssrc1 >= ssrc2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu    , B, if (src1 < src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu    , B, if (src1 >= src2) s->dnpc = s->pc + imm);

  // U-Type 指令
  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui     , U, R(rd) = imm);
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc   , U, R(rd) = s->pc + imm);

  // J-Type 指令（U-Type的变种）
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal     , J, R(rd) = s->pc + 4, s->dnpc = s->pc + imm);

  /* ----- RV32M 指令模块 ----- */

  // R-Type 指令
  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul     , R, R(rd) = inst_mul(src1, src2));
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh    , R, R(rd) = inst_mulh(src1, src2));
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu  , R, R(rd) = inst_mulhsu(src1, src2));
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu   , R, R(rd) = inst_mulhu(src1, src2));
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div     , R, R(rd) = inst_div(src1, src2));
  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu    , R, R(rd) = inst_divu(src1, src2));
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem     , R, R(rd) = inst_rem(src1, src2));
  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu    , R, R(rd) = inst_remu(src1, src2));

  /* ----- 其他指令 ----- */

  // 非法指令
  INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak  , N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv     , N, INV(s->pc));

  INSTPAT_END();

#ifdef CONFIG_FTRACE

  handle_ftrace(s);

#endif

  R(0) = 0; // reset $zero to 0

  return 0;
}

#ifdef CONFIG_FTRACE

static void handle_ftrace(Decode *s) {
  /*
  如若开启了 ftrace 功能，还要记录函数调用栈信息，
  （识别到函数的调用 call 以及从函数的返回 ret）

  这里比较烦人的一点是，RV指令集因为过于精简，把 call 和 ret 之类的函数调用指令
  全部编成 pseudoinstruction 了，实际经过汇编器都转为 jal 或者 jalr，如果我们
  在此分析的话要从 jal 和 jalr 反推 call 和 ret，从而识别出哪一条jal 或者 jalr
  隐含着调用函数或者从函数返回的含义。
  */

  INSTPAT_START();

  // 幸运的是 RV 指令集中 call、ret、tail（尾调用） 等伪指令最后都归结于
  // jalr 或者 jal 这两条实际指令完成，而三者在这两条指令的使用上各不相同，
  // 据此可以将三者辨析开来。
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr    , I, handle_ftrace_inst_jalr(s, rd, rs1, src1, imm));
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal     , J, handle_ftrace_inst_jal(s, rd, imm));

  INSTPAT_END();
}

#endif

int isa_exec_once(Decode *s) {
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
