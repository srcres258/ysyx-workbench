ifeq ($(USE_FLASH_XIP),1)
  AM_SRCS := riscv/ysyxsoc/start-flash-xip.S
else
  AM_SRCS := riscv/ysyxsoc/start.S \
             riscv/ysyxsoc/ssbl.c
endif
AM_SRCS += riscv/ysyxsoc/trm.c \
           riscv/ysyxsoc/ioe.c \
           riscv/ysyxsoc/uart.c \
           riscv/ysyxsoc/timer.c \
           riscv/ysyxsoc/input.c \
           riscv/ysyxsoc/gpu.c \
           riscv/ysyxsoc/cte.c \
           riscv/ysyxsoc/trap.S \
           riscv/ysyxsoc/vme.c \
           riscv/ysyxsoc/mpe.c

CFLAGS    += -fdata-sections -ffunction-sections
ifeq ($(USE_FLASH_XIP),1)
  LDSCRIPTS += $(AM_HOME)/scripts/platform/ysyxsoc/linker-flash-xip.ld
else ifeq ($(USE_SDRAM),1)
  LDSCRIPTS += $(AM_HOME)/scripts/platform/ysyxsoc/linker-fsbl-ssbl-sdram.ld
else ifeq ($(USE_PSRAM),1)
  LDSCRIPTS += $(AM_HOME)/scripts/platform/ysyxsoc/linker-fsbl-ssbl.ld
else
  LDSCRIPTS += $(AM_HOME)/scripts/platform/ysyxsoc/linker.ld
endif
LDFLAGS   += --defsym=_psram_start=0x80000000 --defsym=_sdram_start=0xa0000000 --defsym=_sram_start=0x0f000000 --defsym=_mrom_start=0x20000000 --defsym=_flash_start=0x30000000
LDFLAGS   += --gc-sections -e _start

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

ifeq ($(USE_FLASH_XIP),1)
  CFLAGS += -DFLASH_XIP_BOOT
endif

insert-arg: image
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"

image: image-dep
	$(DUMP_ELF)
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -S -R .bss -R .comment -R .riscv.attributes -O binary $(IMAGE).elf $(IMAGE).bin

CONFIG_SDB_ENABLED ?= false
CONFIG_ITRACE ?= on
CONFIG_MTRACE ?= on
CONFIG_FTRACE ?= on
CONFIG_DTRACE ?= on
CONFIG_ETRACE ?= on
CONFIG_DIFFTEST ?= off
CONFIG_DEVICE ?= on
CONFIG_NVBOARD ?= on
CONFIG_WAVE ?= on
CONFIG_DEBUG_OUTPUT ?= off
CONFIG_MROM ?= off
CONFIG_DIFFTEST_PORT ?= 12345
CONFIG_DIFFTEST_START_MODE ?= reset
CONFIG_DIFFTEST_START_PC ?= 0
CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH ?=
CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR ?= 0x80000000
CONFIG_DIFFTEST_MEM_MODE ?= auto
CONFIG_MROM_BIN_FILE_PATH ?= mrom.bin

# Flash XIP 模式下每次取指都触发 flash_read() DPI 调用, dtrace 会产生海量日志
ifeq ($(USE_FLASH_XIP),1)
  CONFIG_DTRACE ?= off
endif

TRACE_LOG_DIR = $(abspath ./build/trace-logs)

RUN_ARGS = RUN_SDB_ENABLED=$(CONFIG_SDB_ENABLED) \
	RUN_CONFIG_ITRACE=$(CONFIG_ITRACE) \
	RUN_CONFIG_MTRACE=$(CONFIG_MTRACE) \
	RUN_CONFIG_FTRACE=$(CONFIG_FTRACE) \
	RUN_CONFIG_DTRACE=$(CONFIG_DTRACE) \
	RUN_CONFIG_ETRACE=$(CONFIG_ETRACE) \
	RUN_CONFIG_DIFFTEST=$(CONFIG_DIFFTEST) \
	RUN_CONFIG_DEVICE=$(CONFIG_DEVICE) \
	RUN_CONFIG_NVBOARD=$(CONFIG_NVBOARD) \
	RUN_CONFIG_WAVE=$(CONFIG_WAVE) \
	RUN_CONFIG_DEBUG_OUTPUT=$(CONFIG_DEBUG_OUTPUT) \
	RUN_CONFIG_MROM=$(CONFIG_MROM) \
	RUN_CONFIG_DIFFTEST_PORT=$(CONFIG_DIFFTEST_PORT) \
	RUN_CONFIG_DIFFTEST_START_MODE=$(CONFIG_DIFFTEST_START_MODE) \
	RUN_CONFIG_DIFFTEST_START_PC=$(CONFIG_DIFFTEST_START_PC) \
	RUN_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH=$(CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH) \
	RUN_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR=$(CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR) \
	RUN_CONFIG_DIFFTEST_MEM_MODE=$(CONFIG_DIFFTEST_MEM_MODE) \
	RUN_CONFIG_ITRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/itrace.log) \
	RUN_CONFIG_MTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/mtrace.log) \
	RUN_CONFIG_FTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/ftrace.log) \
	RUN_CONFIG_DTRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/dtrace.log) \
	RUN_CONFIG_ETRACE_OUT_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/etrace.log) \
	RUN_CONFIG_FLASH_BIN_FILE_PATH=$(abspath $(IMAGE).bin) \
	RUN_CONFIG_FLASH_ELF_FILE_PATH=$(abspath $(IMAGE).elf) \
	RUN_CONFIG_MROM_BIN_FILE_PATH=$(abspath $(CONFIG_MROM_BIN_FILE_PATH)) \
	RUN_CONFIG_DIFFTEST_SO_FILE_PATH=$(abspath $(NEMU_HOME)/build/riscv32-nemu-interpreter-so) \
	RUN_CONFIG_WAVE_FILE_PATH=$(abspath $(TRACE_LOG_DIR)/sim.fst)

run: insert-arg
	echo "Beginning simulation for ysyxSoC..."
	/bin/sh -c "if [ ! -d $(TRACE_LOG_DIR) ]; then mkdir -p $(TRACE_LOG_DIR); fi"
	$(MAKE) -C $(NPC_HOME) run \
	IMG=$(abspath $(IMAGE).bin) \
	$(RUN_ARGS)

gdb: insert-arg
	echo "Beginning simulation for ysyxSoC..."
	/bin/sh -c "if [ ! -d $(TRACE_LOG_DIR) ]; then mkdir -p $(TRACE_LOG_DIR); fi"
	$(MAKE) -C $(NPC_HOME) gdb \
	IMG=$(abspath $(IMAGE).bin) \
	$(RUN_ARGS)

.PHONY: insert-arg
