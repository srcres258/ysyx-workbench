#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trace/observer.h>
#include <trace/pc_trace.h>
#include <trace/pctr_format.h>

FILE *log_fp = NULL;
bool log_enable(void) {
  return false;
}

void assert_fail_msg(void) {
}

typedef struct {
  uint32_t *pcs;
  size_t count;
} DecodedTrace;

static uint16_t read_u16_le(FILE *fp) {
  int b0 = fgetc(fp);
  int b1 = fgetc(fp);
  assert(b0 != EOF && b1 != EOF);
  return (uint16_t)b0 | ((uint16_t)b1 << 8);
}

static uint32_t read_u32_le(FILE *fp) {
  int b0 = fgetc(fp);
  int b1 = fgetc(fp);
  int b2 = fgetc(fp);
  int b3 = fgetc(fp);
  assert(b0 != EOF && b1 != EOF && b2 != EOF && b3 != EOF);
  return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
}

static void decoded_trace_push(DecodedTrace *trace, uint32_t pc) {
  uint32_t *new_pcs = realloc(trace->pcs, (trace->count + 1) * sizeof(*trace->pcs));
  assert(new_pcs != NULL);
  trace->pcs = new_pcs;
  trace->pcs[trace->count++] = pc;
}

static void decoded_trace_expect(const DecodedTrace *trace, const uint32_t *expected, size_t expected_count) {
  size_t i;

  assert(trace->count == expected_count);
  for (i = 0; i < expected_count; i++) {
    assert(trace->pcs[i] == expected[i]);
  }
}

static void decoded_trace_free(DecodedTrace *trace) {
  free(trace->pcs);
  trace->pcs = NULL;
  trace->count = 0;
}

static FILE *open_trace_stream(const char *path, bool compressed, bool *use_pclose) {
  char cmd[512];

  *use_pclose = false;
  if (!compressed) {
    return fopen(path, "rb");
  }

  snprintf(cmd, sizeof(cmd), "bzip2 -dc '%s'", path);
  *use_pclose = true;
  return popen(cmd, "r");
}

static DecodedTrace decode_trace_file(const char *path, bool compressed) {
  bool use_pclose;
  FILE *fp = open_trace_stream(path, compressed, &use_pclose);
  DecodedTrace trace = {0};
  char magic[PCTR_MAGIC_SIZE];
  uint16_t version;
  uint16_t header_size;
  int address_width;
  int encoding;
  int endianness;

  assert(fp != NULL);
  assert(fread(magic, 1, sizeof(magic), fp) == sizeof(magic));
  assert(memcmp(magic, PCTR_MAGIC, sizeof(magic)) == 0);
  version = read_u16_le(fp);
  header_size = read_u16_le(fp);
  address_width = fgetc(fp);
  encoding = fgetc(fp);
  endianness = fgetc(fp);
  assert(fgetc(fp) == 0);
  assert(read_u32_le(fp) == 0);

  assert(version == PCTR_V1_VERSION);
  assert(header_size == PCTR_V1_HEADER_SIZE);
  assert(address_width == PCTR_V1_ADDRESS_WIDTH);
  assert(endianness == PCTR_ENDIANNESS_LITTLE);

  if (encoding == PC_TRACE_FORMAT_RAW) {
    for (;;) {
      int first = fgetc(fp);
      uint32_t pc;
      int b1;
      int b2;
      int b3;

      if (first == EOF) {
        break;
      }
      b1 = fgetc(fp);
      b2 = fgetc(fp);
      b3 = fgetc(fp);
      assert(b1 != EOF && b2 != EOF && b3 != EOF);
      pc = (uint32_t)first | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
      decoded_trace_push(&trace, pc);
    }
  } else {
    for (;;) {
      int tag = fgetc(fp);
      if (tag == EOF) {
        break;
      }
      if (tag == PCTR_V1_TAG_SINGLE_PC) {
        decoded_trace_push(&trace, read_u32_le(fp));
      } else if (tag == PCTR_V1_TAG_RUN) {
        uint32_t start_pc = read_u32_le(fp);
        uint32_t count = read_u32_le(fp);
        uint32_t i;
        for (i = 0; i < count; i++) {
          decoded_trace_push(&trace, start_pc + i * PCTR_V1_RUN_STRIDE);
        }
      } else {
        assert(0 && "unexpected PCTR tag");
      }
    }
  }

  if (use_pclose) {
    assert(pclose(fp) == 0);
  } else {
    assert(fclose(fp) == 0);
  }
  return trace;
}

static void send_pc(vaddr_t pc) {
  exec_observer_on_instruction(pc, pc + PCTR_V1_RUN_STRIDE);
}

static void enable_run_trace(const char *path, PcTraceCompress compress) {
  pc_trace_enable(path, PC_TRACE_FORMAT_RUN, compress);
}

static void enable_raw_trace(const char *path) {
  pc_trace_enable(path, PC_TRACE_FORMAT_RAW, PC_TRACE_COMPRESS_NONE);
}

static void test_recording_boundary_case(void) {
  const char *path = "/tmp/opencode/pctr-boundary-run.pctrace";
  DecodedTrace trace;
  FILE *fp;
  uint32_t expected[] = {0x1000, 0x1004, 0x1008};

  enable_run_trace(path, PC_TRACE_COMPRESS_NONE);
  send_pc(0x1000);
  send_pc(0x1004);
  pc_trace_set_recording(false);
  send_pc(0x2000);
  send_pc(0x2004);
  pc_trace_set_recording(true);
  send_pc(0x1008);
  pc_trace_disable();

  trace = decode_trace_file(path, false);
  decoded_trace_expect(&trace, expected, ARRLEN(expected));
  decoded_trace_free(&trace);

  fp = fopen(path, "rb");
  assert(fp != NULL);
  assert(fseek(fp, PCTR_V1_HEADER_SIZE, SEEK_SET) == 0);
  assert(fgetc(fp) == PCTR_V1_TAG_RUN);
  assert(read_u32_le(fp) == 0x1000);
  assert(read_u32_le(fp) == 2);
  assert(fgetc(fp) == PCTR_V1_TAG_SINGLE_PC);
  assert(read_u32_le(fp) == 0x1008);
  assert(fgetc(fp) == EOF);
  assert(fclose(fp) == 0);
}

static void test_multiple_recording_windows(void) {
  const char *path = "/tmp/opencode/pctr-multi-window-run.pctrace";
  DecodedTrace trace;
  uint32_t expected[] = {0x3000, 0x3004, 0x4000};

  enable_run_trace(path, PC_TRACE_COMPRESS_NONE);
  pc_trace_set_recording(false);
  pc_trace_set_recording(true);
  send_pc(0x3000);
  send_pc(0x3004);
  pc_trace_set_recording(false);
  pc_trace_set_recording(false);
  pc_trace_set_recording(true);
  pc_trace_set_recording(true);
  send_pc(0x4000);
  pc_trace_set_recording(false);
  pc_trace_disable();

  trace = decode_trace_file(path, false);
  decoded_trace_expect(&trace, expected, ARRLEN(expected));
  decoded_trace_free(&trace);
}

static void test_disable_while_no_run_pending(void) {
  const char *path = "/tmp/opencode/pctr-empty-window-run.pctrace";
  DecodedTrace trace;

  enable_run_trace(path, PC_TRACE_COMPRESS_NONE);
  pc_trace_set_recording(false);
  pc_trace_disable();

  trace = decode_trace_file(path, false);
  decoded_trace_expect(&trace, NULL, 0);
  decoded_trace_free(&trace);
}

static void test_close_while_recording_disabled(void) {
  const char *path = "/tmp/opencode/pctr-close-disabled-run.pctrace";
  DecodedTrace trace;
  uint32_t expected[] = {0x5000};

  enable_run_trace(path, PC_TRACE_COMPRESS_NONE);
  send_pc(0x5000);
  pc_trace_set_recording(false);
  pc_trace_disable();

  trace = decode_trace_file(path, false);
  decoded_trace_expect(&trace, expected, ARRLEN(expected));
  decoded_trace_free(&trace);
}

static void test_equivalent_raw_run_and_bzip2(void) {
  const char *raw_path = "/tmp/opencode/pctr-seq-raw.pctrace";
  const char *run_path = "/tmp/opencode/pctr-seq-run.pctrace";
  const char *bz2_path = "/tmp/opencode/pctr-seq-run.pctrace.bz2";
  uint32_t expected[] = {0x1000, 0x1004, 0x1008, 0x2000, 0x2004};
  DecodedTrace raw_trace;
  DecodedTrace run_trace;
  DecodedTrace bz2_trace;
  FILE *fp;

  enable_raw_trace(raw_path);
  send_pc(0x1000);
  send_pc(0x1004);
  send_pc(0x1008);
  send_pc(0x2000);
  send_pc(0x2004);
  pc_trace_disable();

  enable_run_trace(run_path, PC_TRACE_COMPRESS_NONE);
  send_pc(0x1000);
  send_pc(0x1004);
  send_pc(0x1008);
  send_pc(0x2000);
  send_pc(0x2004);
  pc_trace_disable();

  enable_run_trace(bz2_path, PC_TRACE_COMPRESS_BZIP2);
  send_pc(0x1000);
  send_pc(0x1004);
  send_pc(0x1008);
  send_pc(0x2000);
  send_pc(0x2004);
  pc_trace_disable();

  raw_trace = decode_trace_file(raw_path, false);
  run_trace = decode_trace_file(run_path, false);
  bz2_trace = decode_trace_file(bz2_path, true);

  decoded_trace_expect(&raw_trace, expected, ARRLEN(expected));
  decoded_trace_expect(&run_trace, expected, ARRLEN(expected));
  decoded_trace_expect(&bz2_trace, expected, ARRLEN(expected));

  decoded_trace_expect(&run_trace, raw_trace.pcs, raw_trace.count);
  decoded_trace_expect(&bz2_trace, raw_trace.pcs, raw_trace.count);

  fp = fopen(run_path, "rb");
  assert(fp != NULL);
  assert(fseek(fp, PCTR_V1_HEADER_SIZE, SEEK_SET) == 0);
  assert(fgetc(fp) == PCTR_V1_TAG_RUN);
  assert(read_u32_le(fp) == 0x1000);
  assert(read_u32_le(fp) == 3);
  assert(fgetc(fp) == PCTR_V1_TAG_RUN);
  assert(read_u32_le(fp) == 0x2000);
  assert(read_u32_le(fp) == 2);
  assert(fgetc(fp) == EOF);
  assert(fclose(fp) == 0);

  decoded_trace_free(&raw_trace);
  decoded_trace_free(&run_trace);
  decoded_trace_free(&bz2_trace);
}

int main(void) {
  test_recording_boundary_case();
  test_multiple_recording_windows();
  test_disable_while_no_run_pending();
  test_close_while_recording_disabled();
  test_equivalent_raw_run_and_bzip2();
  puts("test_pc_trace_writer: ok");
  return 0;
}
