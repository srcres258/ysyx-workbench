AM_SRCS := riscv/npc/start.S \
           riscv/npc/trm.c \
           riscv/npc/ioe.c \
           riscv/npc/timer.c \
           riscv/npc/input.c \
           riscv/npc/gpu.c \
           riscv/npc/cte.c \
           riscv/npc/trap.S \
           platform/dummy/vme.c \
           platform/dummy/mpe.c

CFLAGS    += -fdata-sections -ffunction-sections
LDSCRIPTS += $(AM_HOME)/scripts/linker.ld
LDFLAGS   += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
LDFLAGS   += --gc-sections -e _start

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

insert-arg: image
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"

image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $(IMAGE).elf $(IMAGE).bin

CONFIG_SDB_ENABLED ?= true
CONFIG_ITRACE ?= on
CONFIG_MTRACE ?= on
CONFIG_FTRACE ?= on
CONFIG_DTRACE ?= on
CONFIG_DIFFTEST ?= on
CONFIG_DEVICE ?= on
CONFIG_WAVE ?= on
CONFIG_DEBUG_OUTPUT ?= on
CONFIG_DIFFTEST_PORT ?= 12345

TRACE_LOG_DIR = $(abspath ./build/trace-logs)

RUN_ARGS = RUN_SDB_ENABLED=$(CONFIG_SDB_ENABLED) \
	RUN_CONFIG_ITRACE=$(CONFIG_ITRACE) \
	RUN_CONFIG_MTRACE=$(CONFIG_MTRACE) \
	RUN_CONFIG_FTRACE=$(CONFIG_FTRACE) \
	RUN_CONFIG_DTRACE=$(CONFIG_DTRACE) \
	RUN_CONFIG_DIFFTEST=$(CONFIG_DIFFTEST) \
	RUN_CONFIG_DEVICE=$(CONFIG_DEVICE) \
	RUN_CONFIG_WAVE=$(CONFIG_WAVE) \
	RUN_CONFIG_DEBUG_OUTPUT=$(CONFIG_DEBUG_OUTPUT) \
	RUN_CONFIG_DIFFTEST_PORT=$(CONFIG_DIFFTEST_PORT) \
	RUN_CONFIG_ITRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/itrace.log) \
	RUN_CONFIG_MTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/mtrace.log) \
	RUN_CONFIG_FTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/ftrace.log) \
	RUN_CONFIG_DTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/dtrace.log) \
	RUN_CONFIG_ELF_FILE_PATH=$(abspath $(IMAGE).elf) \
	RUN_CONFIG_DIFFTEST_SO_FILE_PATH=$(abspath $(NEMU_HOME)/build/riscv32-nemu-interpreter-so) \
	RUN_CONFIG_WAVE_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/sim.fst)

run: insert-arg
	echo "Beginning simulation..."
	/bin/sh -c "if [ ! -d $(TRACE_LOG_DIR) ]; then mkdir -p $(TRACE_LOG_DIR); fi"
	$(MAKE) -C $(NPC_HOME) run \
	IMG=$(abspath $(IMAGE).bin) \
	$(RUN_ARGS)

gdb: insert-arg
	echo "Beginning simulation..."
	/bin/sh -c "if [ ! -d $(TRACE_LOG_DIR) ]; then mkdir -p $(TRACE_LOG_DIR); fi"
	$(MAKE) -C $(NPC_HOME) gdb \
	IMG=$(abspath $(IMAGE).bin) \
	$(RUN_ARGS)

.PHONY: insert-arg
