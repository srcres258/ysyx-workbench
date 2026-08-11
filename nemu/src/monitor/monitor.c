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

#include <isa.h>
#include <machine.h>
#include <memory/paddr.h>
#include <trace/pc_trace.h>

void init_rand();
void init_log(const char *log_file);
void init_difftest(char *ref_so_file, long img_size, int port);
void init_sdb();
void init_disasm();

static void welcome() {
  Log("Trace: %s", MUXDEF(CONFIG_TRACE, ANSI_FMT("ON", ANSI_FG_GREEN), ANSI_FMT("OFF", ANSI_FG_RED)));
  IFDEF(CONFIG_TRACE, Log("If trace is enabled, a log file will be generated "
        "to record the trace. This may lead to a large log file. "
        "If it is not necessary, you can disable it in menuconfig"));
  Log("Build time: %s, %s", __TIME__, __DATE__);
  printf("Welcome to %s-NEMU!\n", ANSI_FMT(str(__GUEST_ISA__), ANSI_FG_YELLOW ANSI_BG_RED));
  printf("For help, type \"help\"\n");
}

#ifndef CONFIG_TARGET_AM
#include <getopt.h>
#include <fcntl.h>
#include <utils.h>
#include <unistd.h>

void sdb_set_batch_mode();

static char *log_file = NULL;
static char *diff_so_file = NULL;
static char *img_file = NULL;
static char *elf_file = NULL;
static const char *pc_trace_path = NULL;
static const char *pc_trace_format_name = "raw";
static const char *pc_trace_compress_name = "none";
static const char *selected_machine = "nemu";
static int difftest_port = 1234;

#ifdef CONFIG_FTRACE
static size_t load_elf(void) {
  int fd;
  Elf *elf;
  size_t size;

  // Before the first call to elf_begin() , a program must call elf_version() to coordinate versions.
  if (elf_version(EV_CURRENT) == EV_NONE) {
    Log_info("libelf version is missing! ELF file will not be loaded.");
    return 0;
  }

  if (!elf_file) {
    Log_info("No ELF file is given!");
    return 0;
  }
  fd = open(elf_file, O_RDONLY);
  if (fd < 0) {
    Log_info("Failed to read ELF file '%s'!", elf_file);
    return 0;
  }
  elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
  if (!elf) {
    Log_info("Failed to load ELF file '%s'!", elf_file);
    return 0;
  }
  // 确定文件类型是否是ELF文件
  if (elf_kind(elf) != ELF_K_ELF) {
    Log_info("'%s' is not an ELF executable file!", elf_file);
    elf_end(elf);
    return 0;
  }
  size = load_function_symbols_from_elf(nemu_state.ftrace_func_syms, elf);
  elf_end(elf);
  close(fd);
  return size;
}
#endif

static int parse_args(int argc, char *argv[]) {
  const struct option table[] = {
    {"batch"    , no_argument      , NULL, 'b'},
    {"log"      , required_argument, NULL, 'l'},
    {"diff"     , required_argument, NULL, 'd'},
    {"port"     , required_argument, NULL, 'p'},
    {"elf"      , required_argument, NULL, 'e'},
    {"machine"  , required_argument, NULL, 'm'},
    {"pc-trace" , required_argument, NULL, 'P'},
    {"pc-trace-format", required_argument, NULL, 'F'},
    {"pc-trace-compress", required_argument, NULL, 'C'},
    {"help"     , no_argument      , NULL, 'h'},
    {0          , 0                , NULL,  0 },
  };
  int o;
  while ( (o = getopt_long(argc, argv, "-bhl:d:p:e:m:P:F:C:", table, NULL)) != -1) {
    switch (o) {
      case 'b': sdb_set_batch_mode(); break;
      case 'p': sscanf(optarg, "%d", &difftest_port); break;
      case 'l': log_file = optarg; break;
      case 'd': diff_so_file = optarg; break;
      case 'e': elf_file = optarg; break;
      case 'm': selected_machine = optarg; break;
      case 'P': pc_trace_path = optarg; break;
      case 'F': pc_trace_format_name = optarg; break;
      case 'C': pc_trace_compress_name = optarg; break;
      case 1: img_file = optarg; return 0;
      default:
        printf("Usage: %s [OPTION...] IMAGE [args]\n\n", argv[0]);
        printf("\t-b,--batch              run with batch mode\n");
        printf("\t-l,--log=FILE           output log to FILE\n");
        printf("\t-d,--diff=REF_SO        run DiffTest with reference REF_SO\n");
        printf("\t-p,--port=PORT          run DiffTest with port PORT\n");
        printf("\t-e,--elf=FILE           specify the ELF file to load\n");
        printf("\t-m,--machine=NAME       select machine profile (nemu|ysyxsoc)\n");
        printf("\t-P,--pc-trace=FILE      write dynamic PC trace to FILE\n");
        printf("\t-F,--pc-trace-format=FMT  select trace format (raw|run)\n");
        printf("\t-C,--pc-trace-compress=MODE select compression (none|bzip2)\n");
        printf("\n");
        exit(0);
    }
  }
  return 0;
}

void init_monitor(int argc, char *argv[]) {
  /* Perform some global initialization. */

  /* Parse arguments. */
  parse_args(argc, argv);
  Assert(machine_select(selected_machine), "Unknown machine '%s'", selected_machine);

  /* Set random seed. */
  init_rand();

  /* Open the log file. */
  init_log(log_file);

  machine_init();

  /* Perform ISA dependent initialization. */
  init_isa();

  /* Load the image to memory. This belongs to the selected machine profile. */
  long img_size = machine_load_image(img_file);
  machine_reset();

  if (pc_trace_path != NULL) {
    PcTraceFormat pc_trace_format;
    PcTraceCompress pc_trace_compress;
    Assert(pc_trace_parse_format(pc_trace_format_name, &pc_trace_format),
        "Unsupported pc trace format '%s'", pc_trace_format_name);
    Assert(pc_trace_parse_compress(pc_trace_compress_name, &pc_trace_compress),
        "Unsupported pc trace compression mode '%s'", pc_trace_compress_name);
    pc_trace_enable(pc_trace_path, pc_trace_format, pc_trace_compress);
  }

#ifdef CONFIG_FTRACE
  /* Load function symbols from ELF file. */
  nemu_state.ftrace_func_syms_size = load_elf();
  Log_info("Loaded %lu function symbols from ELF file.", nemu_state.ftrace_func_syms_size);
#endif

  /* Initialize differential testing. */
  init_difftest(diff_so_file, img_size, difftest_port);

  /* Initialize the simple debugger. */
  init_sdb();

  IFDEF(CONFIG_ITRACE, init_disasm());

  /* Display welcome message. */
  welcome();
}
#else // CONFIG_TARGET_AM
static long load_img() {
  extern char bin_start, bin_end;
  size_t size = &bin_end - &bin_start;
  Log("img size = %ld", size);
  return machine_load_embedded_image(&bin_start, size);
}

void am_init_monitor() {
  init_rand();
  Assert(machine_select("nemu"), "failed to select default machine");
  machine_init();
  init_isa();
  load_img();
  machine_reset();
  welcome();
}
#endif
