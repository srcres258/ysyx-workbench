all: default

git_commit_sim: default
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!!

include ../Makefile

CCACHE ?= ccache
CC = $(CCACHE) gcc
CXX = $(CCACHE) g++

YSYXSOC_PATH ?= ../ysyxSoC

# Chisel RTL generation (npc/vsrc-chisel/ → npc/vsrc/generated/)
CHISEL_DIR = $(abspath ./vsrc-chisel)
CHISEL_GEN_OUT = $(CHISEL_DIR)/generated
NPC_GEN_DIR = $(abspath ./vsrc/generated)
CHISEL_ARGS ?=

VERILATOR = verilator
VERILATOR_CFLAGS += -MMD -O3 --x-assign fast \
					--x-initial fast --noassert \
					--trace-fst \
					-Wno-WIDTHEXPAND

BUILD_DIR = ./build
NPC_INC = $(abspath ./include/)
NPC_INC += $(abspath $(VERILATOR_HOME)/include/)
NPC_INC += $(abspath $(VERILATOR_HOME)/include/vltstd/)
NPC_INC += $(abspath $(BUILD_DIR))
OBJ_DIR = $(BUILD_DIR)/obj_dir
NPC_INC += $(abspath $(OBJ_DIR))
NPC_INC += $(abspath $(NVBOARD_HOME)/usr/include)

$(shell mkdir -p $(BUILD_DIR))

# Chisel RTL generation — build vsrc-chisel, then clear & copy to vsrc/generated/
ifeq ($(RUN_CONFIG_SIM_MODE),standalone)
CHISEL_TARGET = mill-run-standalone
else
CHISEL_TARGET = mill-run
endif

RUN_CONFIG_DPI ?= on
ifeq ($(RUN_CONFIG_DPI),off)
CHISEL_ARGS += disableDPI
endif

chisel-gen:
	@echo "[Chisel] Building RTL from vsrc-chisel (mode: $(RUN_CONFIG_SIM_MODE))..."
	@$(MAKE) -C $(CHISEL_DIR) $(CHISEL_TARGET) MILL_ARGS="$(CHISEL_ARGS)"
	@echo "[Chisel] Copying generated SystemVerilog to $(NPC_GEN_DIR)..."
	@rm -rf $(NPC_GEN_DIR)
	@cp -r $(CHISEL_GEN_OUT) $(NPC_GEN_DIR)
	@echo "[Chisel] Done. Generated $$(find $(NPC_GEN_DIR) -name '*.sv' | wc -l) .sv files."

chisel-gen-standalone:
	@echo "[Chisel] Building Standalone RTL from vsrc-chisel..."
	@$(MAKE) -C $(CHISEL_DIR) mill-run-standalone MILL_ARGS="$(CHISEL_ARGS)"
	@echo "[Chisel] Copying generated SystemVerilog to $(NPC_GEN_DIR)..."
	@rm -rf $(NPC_GEN_DIR)
	@cp -r $(CHISEL_GEN_OUT) $(NPC_GEN_DIR)
	@echo "[Chisel] Done. Generated $$(find $(NPC_GEN_DIR) -name '*.sv' | wc -l) .sv files."

chisel-clean:
	rm -rf $(CHISEL_GEN_OUT)
	rm -rf $(NPC_GEN_DIR)

# Verilated 生成的代码输出目录
VERILATOR_MDIR = $(OBJ_DIR)
NPC_INC += $(abspath $(VERILATOR_MDIR))

# Verilated 生成的静态库
VERILATED_LIB = $(VERILATOR_MDIR)/V$(TOPNAME)__ALL.a

# project sources
# Static (hand-written) RTL — always present, safe to discover at parse time
VSRCS_HANDWRITTEN = $(shell find vsrc -maxdepth 1 -name "*.v" -or -name "*.sv")
VSRCS_PERIP = $(shell find $(YSYXSOC_PATH)/perip -name "*.v")
VSRCS_SOC_TOP = $(YSYXSOC_PATH)/build/$(TOPNAME).v
# Simulation mode: "standalone" or "ysyxsoc" (default)
RUN_CONFIG_SIM_MODE ?= ysyxsoc
# Standalone mode: no ysyxSoC peripherals, top is directly from Chisel
ifeq ($(RUN_CONFIG_SIM_MODE),standalone)
VSRCS = $(VSRCS_HANDWRITTEN)
CXXFLAGS += -DNPC_STANDALONE
TOPNAME = ysyx_25070190
NXDC_FILES =
else
TOPNAME = ysyxSoCFull
NXDC_FILES = constr/ysyxSoCFull.nxdc
VSRCS = $(VSRCS_HANDWRITTEN) $(VSRCS_PERIP) $(VSRCS_SOC_TOP)
endif

# constraint file
SRC_AUTO_BIND = $(abspath $(BUILD_DIR)/auto_bind.cpp)
$(SRC_AUTO_BIND): $(NXDC_FILES)
	python3 $(NVBOARD_HOME)/scripts/auto_pin_bind.py $^ $@
auto_bind: $(SRC_AUTO_BIND)

CSRCS = $(shell find csrc -name "*.c" -or -name "*.cc" -or -name "*.cpp")
ifneq ($(RUN_CONFIG_SIM_MODE),standalone)
CSRCS += $(SRC_AUTO_BIND)
endif
# project static libraries
ifeq ($(RUN_CONFIG_SIM_MODE),standalone)
LIBS = $(VERILATED_LIB)
else
LIBS = $(VERILATED_LIB) $(NVBOARD_ARCHIVE)
endif

BIN = $(BUILD_DIR)/$(TOPNAME)

default: $(BIN)

# C/C++ 源文件对应的 .o 文件路径（放到构建目录下的对应子目录）
COBJS = $(patsubst csrc/%.cpp,$(OBJ_DIR)/%.o,$(CSRCS))
COBJS_D = $(filter %.o,$(COBJS))

# 让头文件变更也能触发对应 C++ 对象重编译，避免旧的 obj_dir/*.o
# 在类名/文件名重构后继续被链接进最终二进制。
-include $(wildcard $(COBJS_D:.o=.d))

# rules for NVBoard
# 把NVBoard的Makefile包括进来，构建程序包括NVBoard界 面
include $(NVBOARD_HOME)/scripts/nvboard.mk

# rules for verilator
INCFLAGS = -I. $(addprefix -I, $(NPC_INC))
CXXFLAGS += $(INCFLAGS) -g -std=c++26
LDFLAGS += -lz -lelf -fsanitize=address

# Standalone VGA: add SDL2 for VGA device simulation
ifeq ($(RUN_CONFIG_SIM_MODE),standalone)
CXXFLAGS += $(shell sdl2-config --cflags)
LDFLAGS  += $(shell sdl2-config --libs)
endif
VERILATOR_GEN_INC = $(shell [ -d $(NPC_GEN_DIR) ] && find $(NPC_GEN_DIR) -type d 2>/dev/null || true)
VERILATOR_INC_PATH ?= $(abspath ./vsrc) $(VERILATOR_GEN_INC) \
	$(abspath $(YSYXSOC_PATH)/perip/uart16550/rtl) $(abspath $(YSYXSOC_PATH)/perip/spi/rtl)
VERILATOR_INCFLAGS = -I. $(addprefix -I, $(VERILATOR_INC_PATH))
VERILATOR_CFLAGS += $(VERILATOR_INCFLAGS)

# 规则：将 csrc/.../xxx.cpp -> obj_dir/.../xxx.o
$(OBJ_DIR)/%.o: csrc/%.cpp | $(VERILATED_LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 规则：用 Verilator 生成仿真代码（不自动编译 C++ 源文件）
# Prerequisite chisel-gen ensures generated RTL exists before Verilator runs.
# Generated sources are discovered at recipe-time via shell $$(find ...).
$(VERILATED_LIB): chisel-gen $(VSRCS)
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_CFLAGS) \
		--top-module $(TOPNAME) \
		--Mdir $(OBJ_DIR) --cc $(VSRCS) $$(find $(NPC_GEN_DIR) \( -name "*.v" -or -name "*.sv" \) -not -path "*/verification/assert/*" 2>/dev/null) \
		--timescale "1ns/1ns" --no-timing
	$(MAKE) -C $(VERILATOR_MDIR) -f V$(TOPNAME).mk

# 链接所有 .o 文件与 Verilated 库生成最终仿真程序
# NOTE: 对象文件必须放在静态库之前，否则链接器无法解析库中的符号。
# --start-group/--end-group 处理可能存在的循环依赖。
$(BIN): $(LIBS) $(COBJS)
	$(CXX) $(CXXFLAGS) $(COBJS) -Wl,--start-group $(LIBS) -Wl,--end-group -o $@ \
		-L$(VERILATOR_MDIR) -lverilated -lV$(TOPNAME) \
		$(LDFLAGS)

RUN_SDB_ENABLED ?= false
RUN_CONFIG_MROM ?= off
RUN_CONFIG_TUI ?= off
RUN_CONFIG_TUI_CONFIG_FILE_PATH ?= build/npc-tui.toml
RUN_CONFIG_TUI_GENERATE_CONFIG ?= off
RUN_CONFIG_TUI_GENERATE_FULL_CONFIG ?= off
RUN_CONFIG_TUI_FORCE_OVERWRITE_CONFIG ?= off
RUN_CONFIG_TUI_PRINT_CONFIG_SCHEMA ?= off
RUN_CONFIG_TUI_PRINT_DEFAULT_CONFIG ?= off
RUN_CONFIG_ITRACE ?= off
RUN_CONFIG_MTRACE ?= off
RUN_CONFIG_FTRACE ?= off
RUN_CONFIG_DTRACE ?= off
RUN_CONFIG_ETRACE ?= off
RUN_CONFIG_DIFFTEST ?= off
RUN_CONFIG_DEVICE ?= off
RUN_CONFIG_WAVE ?= off
RUN_CONFIG_NVBOARD ?= on
RUN_CONFIG_DEBUG_OUTPUT ?= off
RUN_CONFIG_PERF ?= on
RUN_CONFIG_DIFFTEST_PORT ?= 12345
RUN_CONFIG_ITRACE_OUT_FILE_PATH ?= build/itrace.log
RUN_CONFIG_MTRACE_OUT_FILE_PATH ?= build/mtrace.log
RUN_CONFIG_FTRACE_OUT_FILE_PATH ?= build/ftrace.log
RUN_CONFIG_DTRACE_OUT_FILE_PATH ?= build/dtrace.log
RUN_CONFIG_ETRACE_OUT_FILE_PATH ?= build/etrace.log
RUN_CONFIG_FLASH_BIN_FILE_PATH ?= build/flash.bin
RUN_CONFIG_FLASH_ELF_FILE_PATH ?= build/flash.elf
RUN_CONFIG_MROM_BIN_FILE_PATH ?= build/mrom.bin
RUN_CONFIG_MROM_ELF_FILE_PATH ?= build/mrom.elf
RUN_CONFIG_DIFFTEST_SO_FILE_PATH ?= build/riscv32-nemu-interpreter-so
RUN_CONFIG_WAVE_FILE_PATH ?= build/sim.fst
RUN_CONFIG_DIFFTEST_START_MODE ?= reset
RUN_CONFIG_DIFFTEST_START_PC ?= 0
RUN_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH ?=
RUN_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR ?= 0x80000000
RUN_CONFIG_DIFFTEST_MEM_MODE ?= auto

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
	NPC_CONFIG_NVBOARD=$(RUN_CONFIG_NVBOARD) \
	NPC_CONFIG_MROM=$(RUN_CONFIG_MROM) \
	NPC_CONFIG_PERF=$(RUN_CONFIG_PERF) \
	NPC_CONFIG_TUI=$(RUN_CONFIG_TUI) \
	NPC_CONFIG_TUI_CONFIG_FILE_PATH=$(RUN_CONFIG_TUI_CONFIG_FILE_PATH) \
	NPC_CONFIG_TUI_GENERATE_CONFIG=$(RUN_CONFIG_TUI_GENERATE_CONFIG) \
	NPC_CONFIG_TUI_GENERATE_FULL_CONFIG=$(RUN_CONFIG_TUI_GENERATE_FULL_CONFIG) \
	NPC_CONFIG_TUI_FORCE_OVERWRITE_CONFIG=$(RUN_CONFIG_TUI_FORCE_OVERWRITE_CONFIG) \
	NPC_CONFIG_TUI_PRINT_CONFIG_SCHEMA=$(RUN_CONFIG_TUI_PRINT_CONFIG_SCHEMA) \
	NPC_CONFIG_TUI_PRINT_DEFAULT_CONFIG=$(RUN_CONFIG_TUI_PRINT_DEFAULT_CONFIG) \
	NPC_CONFIG_DIFFTEST_PORT=$(RUN_CONFIG_DIFFTEST_PORT) \
	NPC_CONFIG_ITRACE_OUT_FILE_PATH=$(RUN_CONFIG_ITRACE_OUT_FILE_PATH) \
	NPC_CONFIG_MTRACE_OUT_FILE_PATH=$(RUN_CONFIG_MTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_FTRACE_OUT_FILE_PATH=$(RUN_CONFIG_FTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_DTRACE_OUT_FILE_PATH=$(RUN_CONFIG_DTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_ETRACE_OUT_FILE_PATH=$(RUN_CONFIG_ETRACE_OUT_FILE_PATH) \
	NPC_CONFIG_FLASH_BIN_FILE_PATH=$(RUN_CONFIG_FLASH_BIN_FILE_PATH) \
	NPC_CONFIG_FLASH_ELF_FILE_PATH=$(RUN_CONFIG_FLASH_ELF_FILE_PATH) \
	NPC_CONFIG_MROM_BIN_FILE_PATH=$(RUN_CONFIG_MROM_BIN_FILE_PATH) \
	NPC_CONFIG_MROM_ELF_FILE_PATH=$(RUN_CONFIG_MROM_ELF_FILE_PATH) \
	NPC_CONFIG_DIFFTEST_SO_FILE_PATH=$(RUN_CONFIG_DIFFTEST_SO_FILE_PATH) \
	NPC_CONFIG_WAVE_FILE_PATH=$(RUN_CONFIG_WAVE_FILE_PATH) \
	NPC_CONFIG_DIFFTEST_START_MODE=$(RUN_CONFIG_DIFFTEST_START_MODE) \
	NPC_CONFIG_DIFFTEST_START_PC=$(RUN_CONFIG_DIFFTEST_START_PC) \
	NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH=$(RUN_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH) \
	NPC_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR=$(RUN_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR) \
	NPC_CONFIG_DIFFTEST_MEM_MODE=$(RUN_CONFIG_DIFFTEST_MEM_MODE)

run: $(BIN) git_commit_sim
	ASAN_OPTIONS=detect_leaks=0:exitcode=0 IMG=$(IMG) $(RUN_ARGS) $(BIN)

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
	-ex "set env NPC_CONFIG_NVBOARD $(RUN_CONFIG_NVBOARD)" \
	-ex "set env NPC_CONFIG_PERF $(RUN_CONFIG_PERF)" \
	-ex "set env NPC_CONFIG_TUI $(RUN_CONFIG_TUI)" \
	-ex "set env NPC_CONFIG_TUI_CONFIG_FILE_PATH $(RUN_CONFIG_TUI_CONFIG_FILE_PATH)" \
	-ex "set env NPC_CONFIG_TUI_GENERATE_CONFIG $(RUN_CONFIG_TUI_GENERATE_CONFIG)" \
	-ex "set env NPC_CONFIG_TUI_GENERATE_FULL_CONFIG $(RUN_CONFIG_TUI_GENERATE_FULL_CONFIG)" \
	-ex "set env NPC_CONFIG_TUI_FORCE_OVERWRITE_CONFIG $(RUN_CONFIG_TUI_FORCE_OVERWRITE_CONFIG)" \
	-ex "set env NPC_CONFIG_TUI_PRINT_CONFIG_SCHEMA $(RUN_CONFIG_TUI_PRINT_CONFIG_SCHEMA)" \
	-ex "set env NPC_CONFIG_TUI_PRINT_DEFAULT_CONFIG $(RUN_CONFIG_TUI_PRINT_DEFAULT_CONFIG)" \
	-ex "set env NPC_CONFIG_DIFFTEST_PORT $(RUN_CONFIG_DIFFTEST_PORT)" \
	-ex "set env NPC_CONFIG_ITRACE_OUT_FILE_PATH $(RUN_CONFIG_ITRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MTRACE_OUT_FILE_PATH $(RUN_CONFIG_MTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_FTRACE_OUT_FILE_PATH $(RUN_CONFIG_FTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DTRACE_OUT_FILE_PATH $(RUN_CONFIG_DTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_ETRACE_OUT_FILE_PATH $(RUN_CONFIG_ETRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_FLASH_BIN_FILE_PATH $(RUN_CONFIG_FLASH_BIN_FILE_PATH)" \
	-ex "set env NPC_CONFIG_FLASH_ELF_FILE_PATH $(RUN_CONFIG_FLASH_ELF_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MROM_BIN_FILE_PATH $(RUN_CONFIG_MROM_BIN_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MROM_ELF_FILE_PATH $(RUN_CONFIG_MROM_ELF_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DIFFTEST_SO_FILE_PATH $(RUN_CONFIG_DIFFTEST_SO_FILE_PATH)" \
	-ex "set env NPC_CONFIG_WAVE_FILE_PATH $(RUN_CONFIG_WAVE_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DIFFTEST_START_MODE $(RUN_CONFIG_DIFFTEST_START_MODE)" \
	-ex "set env NPC_CONFIG_DIFFTEST_START_PC $(RUN_CONFIG_DIFFTEST_START_PC)" \
	-ex "set env NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH $(RUN_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR $(RUN_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR)" \
	-ex "set env NPC_CONFIG_DIFFTEST_MEM_MODE $(RUN_CONFIG_DIFFTEST_MEM_MODE)"

gdb: $(BIN) git_commit_sim
	gdb $(GDB_ARGS) $(BIN)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(NPC_GEN_DIR)
	rm -rf $(CHISEL_GEN_OUT)

# 生成 C++ 头文件，以便编写基于 Verilator 的 C++ Testbench
gen_header: chisel-gen
	$(VERILATOR) $(VERILATOR_CFLAGS) --top-module $(TOPNAME) --Mdir $(OBJ_DIR) --cc $(VSRCS) \
		$$(find $(NPC_GEN_DIR) \( -name "*.v" -or -name "*.sv" \) -not -path "*/verification/assert/*" 2>/dev/null) \
		--timescale "1ns/1ns" --no-timing

.PHONY: default all clean run git_commit_sim auto_bind gen_header chisel-gen chisel-clean
