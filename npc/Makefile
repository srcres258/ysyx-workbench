all: default

SIM_ARGS = NPC_BIN_PATH=$(IMG) \
	NPC_SDB_ENABLED=true \
	NPC_CONFIG_ITRACE=on \
	NPC_CONFIG_MTRACE=on \
	NPC_CONFIG_FTRACE=on \
	NPC_CONFIG_DTRACE=on \
	NPC_CONFIG_DIFFTEST=on \
	NPC_CONFIG_DEVICE=on \
	NPC_CONFIG_WAVE=on \
	NPC_CONFIG_DEBUG_OUTPUT=on \
	NPC_CONFIG_DIFFTEST_PORT=12345 \
	NPC_CONFIG_ITRACE_OUT_FILE_PATH=build/itrace.log \
	NPC_CONFIG_MTRACE_OUT_FILE_PATH=build/mtrace.log \
	NPC_CONFIG_FTRACE_OUT_FILE_PATH=build/ftrace.log \
	NPC_CONFIG_DTRACE_OUT_FILE_PATH=build/dtrace.log \
	NPC_CONFIG_ELF_FILE_PATH=build/program.elf \
	NPC_CONFIG_DIFFTEST_SO_FILE_PATH=$(abspath $(NEMU_HOME)/build/riscv32-nemu-interpreter-so) \
	NPC_CONFIG_WAVE_FILE_PATH=build/sim.fst

sim: default
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!!
	$(SIM_ARGS) build/$(TOPNAME)

include ../Makefile

TOPNAME = ProcessorCore
NXDC_FILES = constr/ProcessorCore.nxdc
INC_PATH ?= $(abspath ./include/)

VERILATOR = verilator
VERILATOR_CFLAGS += -MMD --build -cc \
					-O3 --x-assign fast \
					--x-initial fast --noassert \
					--trace-fst --debug \
					-CFLAGS -g \
					-CFLAGS -std=c++26 \
					-LDFLAGS -lelf \
					-LDFLAGS -fsanitize=address

BUILD_DIR = ./build
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(BUILD_DIR)/$(TOPNAME)

default: $(BIN)

$(shell mkdir -p $(BUILD_DIR))

# constraint file
SRC_AUTO_BIND = $(abspath $(BUILD_DIR)/auto_bind.cpp)
$(SRC_AUTO_BIND): $(NXDC_FILES)
	python3 $(NVBOARD_HOME)/scripts/auto_pin_bind.py $^ $@
auto_bind: $(SRC_AUTO_BIND)

# project source
VSRCS = $(shell find $(abspath ./vsrc) -name "*.v" -or -name "*.sv")
CSRCS = $(shell find $(abspath ./csrc) -name "*.c" -or -name "*.cc" -or -name "*.cpp")
CSRCS += $(SRC_AUTO_BIND)

# rules for NVBoard
# 把NVBoard的Makefile包括进来，构建程序包括NVBoard界面
include $(NVBOARD_HOME)/scripts/nvboard.mk

# rules for verilator
INCFLAGS = $(addprefix -I, $(INC_PATH))
CXXFLAGS += $(INCFLAGS)

IMG ?= build/program.bin

$(BIN): $(VSRCS) $(CSRCS) $(NVBOARD_ARCHIVE)
	@rm -rf $(OBJ_DIR)
	OBJCACHE=ccache $(VERILATOR) $(VERILATOR_CFLAGS) \
		--top-module $(TOPNAME) $^ \
		$(addprefix -CFLAGS , $(CXXFLAGS)) $(addprefix -LDFLAGS , $(LDFLAGS)) \
		--Mdir $(OBJ_DIR) --exe -o $(abspath $(BIN)) \
		-I$(abspath ./vsrc) -I$(abspath ./vsrc/generated)

RUN_SDB_ENABLED ?= false
RUN_CONFIG_ITRACE ?= off
RUN_CONFIG_MTRACE ?= off
RUN_CONFIG_FTRACE ?= off
RUN_CONFIG_DTRACE ?= off
RUN_CONFIG_DIFFTEST ?= off
RUN_CONFIG_DEVICE ?= off
RUN_CONFIG_WAVE ?= off
RUN_CONFIG_DEBUG_OUTPUT ?= off
RUN_CONFIG_DIFFTEST_PORT ?= 12345
RUN_CONFIG_ITRACE_OUT_FILE_PATH ?= build/itrace.log
RUN_CONFIG_MTRACE_OUT_FILE_PATH ?= build/mtrace.log
RUN_CONFIG_FTRACE_OUT_FILE_PATH ?= build/ftrace.log
RUN_CONFIG_DTRACE_OUT_FILE_PATH ?= build/dtrace.log
RUN_CONFIG_ELF_FILE_PATH ?= build/program.elf
RUN_CONFIG_DIFFTEST_SO_FILE_PATH ?= build/riscv32-nemu-interpreter-so
RUN_CONFIG_WAVE_FILE_PATH ?= build/sim.fst

RUN_ARGS = NPC_BIN_PATH=$(IMG) \
	NPC_SDB_ENABLED=$(RUN_SDB_ENABLED) \
	NPC_CONFIG_ITRACE=$(RUN_CONFIG_ITRACE) \
	NPC_CONFIG_MTRACE=$(RUN_CONFIG_MTRACE) \
	NPC_CONFIG_FTRACE=$(RUN_CONFIG_FTRACE) \
	NPC_CONFIG_DTRACE=$(RUN_CONFIG_DTRACE) \
	NPC_CONFIG_DIFFTEST=$(RUN_CONFIG_DIFFTEST) \
	NPC_CONFIG_DEVICE=$(RUN_CONFIG_DEVICE) \
	NPC_CONFIG_WAVE=$(RUN_CONFIG_WAVE) \
	NPC_CONFIG_DEBUG_OUTPUT=$(RUN_CONFIG_DEBUG_OUTPUT) \
	NPC_CONFIG_DIFFTEST_PORT=$(RUN_CONFIG_DIFFTEST_PORT) \
	NPC_CONFIG_ITRACE_OUT_FILE_PATH=$(RUN_CONFIG_ITRACE_OUT_FILE_PATH) \
	NPC_CONFIG_MTRACE_OUT_FILE_PATH=$(RUN_CONFIG_MTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_FTRACE_OUT_FILE_PATH=$(RUN_CONFIG_FTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_DTRACE_OUT_FILE_PATH=$(RUN_CONFIG_DTRACE_OUT_FILE_PATH) \
	NPC_CONFIG_ELF_FILE_PATH=$(RUN_CONFIG_ELF_FILE_PATH) \
	NPC_CONFIG_DIFFTEST_SO_FILE_PATH=$(RUN_CONFIG_DIFFTEST_SO_FILE_PATH) \
	NPC_CONFIG_WAVE_FILE_PATH=$(RUN_CONFIG_WAVE_FILE_PATH)

run: $(BIN)
	$(RUN_ARGS) $(BIN)

GDB_ARGS = -ex "set debuginfod enabled on" \
	-ex "set env NPC_BIN_PATH $(IMG)" \
	-ex "set env NPC_SDB_ENABLED $(RUN_SDB_ENABLED)" \
	-ex "set env NPC_CONFIG_ITRACE $(RUN_CONFIG_ITRACE)" \
	-ex "set env NPC_CONFIG_MTRACE $(RUN_CONFIG_MTRACE)" \
	-ex "set env NPC_CONFIG_FTRACE $(RUN_CONFIG_FTRACE)" \
	-ex "set env NPC_CONFIG_DTRACE $(RUN_CONFIG_DTRACE)" \
	-ex "set env NPC_CONFIG_DIFFTEST $(RUN_CONFIG_DIFFTEST)" \
	-ex "set env NPC_CONFIG_DEVICE $(RUN_CONFIG_DEVICE)" \
	-ex "set env NPC_CONFIG_WAVE $(RUN_CONFIG_WAVE)" \
	-ex "set env NPC_CONFIG_DEBUG_OUTPUT $(RUN_CONFIG_DEBUG_OUTPUT)" \
	-ex "set env NPC_CONFIG_DIFFTEST_PORT $(RUN_CONFIG_DIFFTEST_PORT)" \
	-ex "set env NPC_CONFIG_ITRACE_OUT_FILE_PATH $(RUN_CONFIG_ITRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_MTRACE_OUT_FILE_PATH $(RUN_CONFIG_MTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_FTRACE_OUT_FILE_PATH $(RUN_CONFIG_FTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DTRACE_OUT_FILE_PATH $(RUN_CONFIG_DTRACE_OUT_FILE_PATH)" \
	-ex "set env NPC_CONFIG_ELF_FILE_PATH $(RUN_CONFIG_ELF_FILE_PATH)" \
	-ex "set env NPC_CONFIG_DIFFTEST_SO_FILE_PATH $(RUN_CONFIG_DIFFTEST_SO_FILE_PATH)" \
	-ex "set env NPC_CONFIG_WAVE_FILE_PATH $(RUN_CONFIG_WAVE_FILE_PATH)"

gdb: $(BIN)
	gdb $(GDB_ARGS) $(BIN)

clean:
	rm -rf $(BUILD_DIR)

# 生成 C++ 头文件，以便编写基于 Verilator 的 C++ Testbench
gen_header:
	$(VERILATOR) --top-module $(TOPNAME) --Mdir $(OBJ_DIR) --cc $(VSRCS) \
		-I$(abspath ./vsrc) -I$(abspath ./vsrc/generated)

.PHONY: default all clean run sim auto_bind gen_header
