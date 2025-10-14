all: default

git_commit_sim: default
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!!

include ../Makefile

CCACHE ?= ccache
CC = $(CCACHE) gcc
CXX = $(CCACHE) g++

YSYXSOC_PATH ?= ../ysyxSoC

TOPNAME = ysyxSoCFull
NXDC_FILES = constr/ysyxSoCFull.nxdc
INC_PATH ?= $(abspath ./include/)
INC_PATH += $(abspath $(VERILATOR_HOME)/include/)
INC_PATH += $(abspath $(VERILATOR_HOME)/include/vltstd/)

VERILATOR = verilator
VERILATOR_CFLAGS += -MMD -O3 --x-assign fast \
					--x-initial fast --noassert \
					--trace-fst --debug

BUILD_DIR = ./build
INC_PATH += $(abspath $(BUILD_DIR))
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(BUILD_DIR)/$(TOPNAME)

default: $(BIN)

$(shell mkdir -p $(BUILD_DIR))

# constraint file
SRC_AUTO_BIND = $(abspath $(BUILD_DIR)/auto_bind.cpp)
$(SRC_AUTO_BIND): $(NXDC_FILES)
	python3 $(NVBOARD_HOME)/scripts/auto_pin_bind.py $^ $@
auto_bind: $(SRC_AUTO_BIND)

# Verilated 生成的代码输出目录
VERILATOR_MDIR = $(OBJ_DIR)
INC_PATH += $(abspath $(VERILATOR_MDIR))

# Verilated 生成的静态库
VERILATED_LIB = $(VERILATOR_MDIR)/V$(TOPNAME)__ALL.a

# project sources
VSRCS = $(shell find vsrc -name "*.v" -or -name "*.sv") $(shell find $(YSYXSOC_PATH)/perip -name "*.v")
VSRCS += $(YSYXSOC_PATH)/build/$(TOPNAME).v
CSRCS = $(shell find csrc -name "*.c" -or -name "*.cc" -or -name "*.cpp")
CSRCS += $(SRC_AUTO_BIND)
# project static libraries
LIBS = $(VERILATED_LIB) $(NVBOARD_ARCHIVE)

# C/C++ 源文件对应的 .o 文件路径（放到构建目录下的对应子目录）
COBJS = $(patsubst csrc/%.cpp,$(OBJ_DIR)/%.o,$(CSRCS))

# rules for NVBoard
# 把NVBoard的Makefile包括进来，构建程序包括NVBoard界面
include $(NVBOARD_HOME)/scripts/nvboard.mk

# rules for verilator
INCFLAGS = -I. $(addprefix -I, $(INC_PATH))
CXXFLAGS += $(INCFLAGS) -g -std=c++26
LDFLAGS += -lz -lelf -fsanitize=address
VERILATOR_INC_PATH ?= $(abspath ./vsrc) $(shell find $(abspath ./vsrc/generated) -type d) \
	$(abspath $(YSYXSOC_PATH)/perip/uart16550/rtl) $(abspath $(YSYXSOC_PATH)/perip/spi/rtl)
VERILATOR_INCFLAGS = -I. $(addprefix -I, $(VERILATOR_INC_PATH))
VERILATOR_CFLAGS += $(VERILATOR_INCFLAGS)

# 规则：将 csrc/.../xxx.cpp -> obj_dir/.../xxx.o
$(OBJ_DIR)/%.o: csrc/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 规则：用 Verilator 生成仿真代码（不自动编译 C++ 源文件）
$(VERILATED_LIB): $(VSRCS)
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_CFLAGS) \
		--top-module $(TOPNAME) \
		--Mdir $(OBJ_DIR) --cc $^ \
		--timescale "1ns/1ns" --no-timing
	make -C $(VERILATOR_MDIR) -f V$(TOPNAME).mk

# 链接所有 .o 文件与 Verilated 库生成最终仿真程序
$(BIN): $(LIBS) $(COBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ \
		-L$(VERILATOR_MDIR) -lverilated -lV$(TOPNAME) \
		$(LDFLAGS)

RUN_SDB_ENABLED ?= false
RUN_CONFIG_ITRACE ?= off
RUN_CONFIG_MTRACE ?= off
RUN_CONFIG_FTRACE ?= off
RUN_CONFIG_DTRACE ?= off
RUN_CONFIG_ETRACE ?= off
RUN_CONFIG_DIFFTEST ?= off
RUN_CONFIG_DEVICE ?= off
RUN_CONFIG_WAVE ?= off
RUN_CONFIG_DEBUG_OUTPUT ?= off
RUN_CONFIG_DIFFTEST_PORT ?= 12345
RUN_CONFIG_ITRACE_OUT_FILE_PATH ?= build/itrace.log
RUN_CONFIG_MTRACE_OUT_FILE_PATH ?= build/mtrace.log
RUN_CONFIG_FTRACE_OUT_FILE_PATH ?= build/ftrace.log
RUN_CONFIG_DTRACE_OUT_FILE_PATH ?= build/dtrace.log
RUN_CONFIG_ETRACE_OUT_FILE_PATH ?= build/dtrace.log
RUN_CONFIG_MROM_BIN_FILE_PATH ?= build/mrom.bin
RUN_CONFIG_MROM_ELF_FILE_PATH ?= build/mrom.elf
RUN_CONFIG_DIFFTEST_SO_FILE_PATH ?= build/riscv32-nemu-interpreter-so
RUN_CONFIG_WAVE_FILE_PATH ?= build/sim.fst

RUN_ARGS = NPC_SDB_ENABLED=$(RUN_SDB_ENABLED) \
	NPC_CONFIG_ITRACE=$(RUN_CONFIG_ITRACE) \
	NPC_CONFIG_MTRACE=$(RUN_CONFIG_MTRACE) \
	NPC_CONFIG_FTRACE=$(RUN_CONFIG_FTRACE) \
	NPC_CONFIG_DTRACE=$(RUN_CONFIG_DTRACE) \
	NPC_CONFIG_ETRACE=$(RUN_CONFIG_ETRACE) \
	NPC_CONFIG_DIFFTEST=$(RUN_CONFIG_DIFFTEST) \
	NPC_CONFIG_DEVICE=$(RUN_CONFIG_DEVICE) \
	NPC_CONFIG_WAVE=$(RUN_CONFIG_WAVE) \
	NPC_CONFIG_DEBUG_OUTPUT=$(RUN_CONFIG_DEBUG_OUTPUT) \
	NPC_CONFIG_DIFFTEST_PORT=$(RUN_CONFIG_DIFFTEST_PORT) \
	NPC_CONFIG_ITRACE_OUT_FILE_PATH=$(RUN_CONFIG_ITRACE_OUT_FILE_PATH) \
	NPC_CONFIG_MTRACE_OUT_FILE_PATH=$(RUN_CONFIG_MTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_FTRACE_OUT_FILE_PATH=$(RUN_CONFIG_FTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_DTRACE_OUT_FILE_PATH=$(RUN_CONFIG_DTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_ETRACE_OUT_FILE_PATH=$(RUN_CONFIG_ETRACE_OUT_FILE_PATH) \
	NPC_CONFIG_MROM_BIN_FILE_PATH=$(RUN_CONFIG_MROM_BIN_FILE_PATH) \
	NPC_CONFIG_MROM_ELF_FILE_PATH=$(RUN_CONFIG_MROM_ELF_FILE_PATH) \
	NPC_CONFIG_DIFFTEST_SO_FILE_PATH=$(RUN_CONFIG_DIFFTEST_SO_FILE_PATH) \
	NPC_CONFIG_WAVE_FILE_PATH=$(RUN_CONFIG_WAVE_FILE_PATH)

run: $(BIN) git_commit_sim
	$(RUN_ARGS) $(BIN)

GDB_ARGS = -ex "set debuginfod enabled on" \
	-ex "set env NPC_SDB_ENABLED $(RUN_SDB_ENABLED)" \
	-ex "set env NPC_CONFIG_ITRACE $(RUN_CONFIG_ITRACE)" \
	-ex "set env NPC_CONFIG_MTRACE $(RUN_CONFIG_MTRACE)" \
	-ex "set env NPC_CONFIG_FTRACE $(RUN_CONFIG_FTRACE)" \
	-ex "set env NPC_CONFIG_DTRACE $(RUN_CONFIG_DTRACE)" \
	-ex "set env NPC_CONFIG_ETRACE $(RUN_CONFIG_ETRACE)" \
	-ex "set env NPC_CONFIG_DIFFTEST $(RUN_CONFIG_DIFFTEST)" \
	-ex "set env NPC_CONFIG_DEVICE $(RUN_CONFIG_DEVICE)" \
	-ex "set env NPC_CONFIG_WAVE $(RUN_CONFIG_WAVE)" \
	-ex "set env NPC_CONFIG_DEBUG_OUTPUT $(RUN_CONFIG_DEBUG_OUTPUT)" \
	-ex "set env NPC_CONFIG_DIFFTEST_PORT $(RUN_CONFIG_DIFFTEST_PORT)" \
	-ex "set env NPC_CONFIG_ITRACE_OUT_FILE_PATH $(RUN_CONFIG_ITRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MTRACE_OUT_FILE_PATH $(RUN_CONFIG_MTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_FTRACE_OUT_FILE_PATH $(RUN_CONFIG_FTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DTRACE_OUT_FILE_PATH $(RUN_CONFIG_DTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_ETRACE_OUT_FILE_PATH $(RUN_CONFIG_ETRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MROM_BIN_FILE_PATH $(RUN_CONFIG_MROM_BIN_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MROM_ELF_FILE_PATH $(RUN_CONFIG_MROM_ELF_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DIFFTEST_SO_FILE_PATH $(RUN_CONFIG_DIFFTEST_SO_FILE_PATH)" \
	-ex "set env NPC_CONFIG_WAVE_FILE_PATH $(RUN_CONFIG_WAVE_FILE_PATH)"

gdb: $(BIN) git_commit_sim
	gdb $(GDB_ARGS) $(BIN)

clean:
	rm -rf $(BUILD_DIR)

# 生成 C++ 头文件，以便编写基于 Verilator 的 C++ Testbench
gen_header:
	$(VERILATOR) $(VERILATOR_CFLAGS) --top-module $(TOPNAME) --Mdir $(OBJ_DIR) --cc $(VSRCS) \
		--timescale "1ns/1ns" --no-timing

.PHONY: default all clean run git_commit_sim auto_bind gen_header
