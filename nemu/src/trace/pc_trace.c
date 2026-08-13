#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <trace/pc_trace.h>

typedef struct {
  bool enabled;
  bool recording;
  PcTraceFormat format;
  PcTraceCompress compress;
  FILE *stream;
  pid_t compressor_pid;
  uint32_t run_start_pc;
  uint32_t run_count;
} PcTraceState;

static PcTraceState state = {0};

void pc_trace_disable(void);

static void reset_pending_run(void) {
  state.run_start_pc = 0;
  state.run_count = 0;
}

static void write_u8(FILE *stream, uint8_t value) {
  Assert(fputc(value, stream) != EOF, "failed to write pc trace byte");
}

static void write_u16_le(FILE *stream, uint16_t value) {
  write_u8(stream, (uint8_t)(value & 0xff));
  write_u8(stream, (uint8_t)((value >> 8) & 0xff));
}

static void write_u32_le(FILE *stream, uint32_t value) {
  write_u8(stream, (uint8_t)(value & 0xff));
  write_u8(stream, (uint8_t)((value >> 8) & 0xff));
  write_u8(stream, (uint8_t)((value >> 16) & 0xff));
  write_u8(stream, (uint8_t)((value >> 24) & 0xff));
}

static FILE *open_compressed_stream(const char *path, pid_t *pid_out) {
  int pipefd[2];
  int outfd;
  pid_t pid;

  Assert(pipe(pipefd) == 0, "failed to create pc trace pipe: %s", strerror(errno));
  outfd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  Assert(outfd >= 0, "failed to open pc trace output '%s': %s", path, strerror(errno));

  pid = fork();
  Assert(pid >= 0, "failed to fork pc trace compressor: %s", strerror(errno));
  if (pid == 0) {
    close(pipefd[1]);
    dup2(pipefd[0], STDIN_FILENO);
    dup2(outfd, STDOUT_FILENO);
    close(pipefd[0]);
    close(outfd);
    execlp("bzip2", "bzip2", "-c", (char *)NULL);
    _exit(127);
  }

  close(pipefd[0]);
  close(outfd);
  *pid_out = pid;
  return fdopen(pipefd[1], "wb");
}

static void write_header(FILE *stream, PcTraceFormat format) {
  Assert(fwrite(PCTR_MAGIC, 1, PCTR_MAGIC_SIZE, stream) == PCTR_MAGIC_SIZE,
      "failed to write pc trace header magic");
  write_u16_le(stream, PCTR_V1_VERSION);
  write_u16_le(stream, PCTR_V1_HEADER_SIZE);
  write_u8(stream, PCTR_V1_ADDRESS_WIDTH);
  write_u8(stream, (uint8_t)format);
  write_u8(stream, PCTR_ENDIANNESS_LITTLE);
  write_u8(stream, 0);
  write_u32_le(stream, 0);
}

static void flush_run(void) {
  if (!state.enabled || state.run_count == 0) {
    return;
  }

  if (state.format == PC_TRACE_FORMAT_RUN && state.run_count > 1) {
    write_u8(state.stream, PCTR_V1_TAG_RUN);
    write_u32_le(state.stream, state.run_start_pc);
    write_u32_le(state.stream, state.run_count);
  } else if (state.format == PC_TRACE_FORMAT_RUN) {
    write_u8(state.stream, PCTR_V1_TAG_SINGLE_PC);
    write_u32_le(state.stream, state.run_start_pc);
  } else {
    uint32_t i;
    for (i = 0; i < state.run_count; i++) {
      write_u32_le(state.stream, state.run_start_pc + i * PCTR_V1_RUN_STRIDE);
    }
  }

  reset_pending_run();
}

static void on_instruction(vaddr_t pc, vaddr_t next_pc) {
  uint32_t current_pc = (uint32_t)pc;
  (void)next_pc;

  if (!state.enabled || !state.recording) {
    return;
  }

  if (state.run_count == 0) {
    state.run_start_pc = current_pc;
    state.run_count = 1;
    return;
  }

  if (current_pc == state.run_start_pc + state.run_count * PCTR_V1_RUN_STRIDE && state.run_count != UINT32_MAX) {
    state.run_count++;
    return;
  }

  flush_run();
  state.run_start_pc = current_pc;
  state.run_count = 1;
}

static const ExecObserver observer = {
  .on_instruction = on_instruction,
};

bool pc_trace_parse_format(const char *name, PcTraceFormat *out) {
  if (strcmp(name, "raw") == 0) {
    *out = PC_TRACE_FORMAT_RAW;
    return true;
  }
  if (strcmp(name, "run") == 0) {
    *out = PC_TRACE_FORMAT_RUN;
    return true;
  }
  return false;
}

bool pc_trace_parse_compress(const char *name, PcTraceCompress *out) {
  if (strcmp(name, "none") == 0) {
    *out = PC_TRACE_COMPRESS_NONE;
    return true;
  }
  if (strcmp(name, "bzip2") == 0) {
    *out = PC_TRACE_COMPRESS_BZIP2;
    return true;
  }
  return false;
}

void pc_trace_enable(const char *path, PcTraceFormat format, PcTraceCompress compress) {
  Assert(path != NULL && path[0] != '\0', "pc trace path must not be empty");
  pc_trace_disable();

  state.enabled = true;
  state.recording = true;
  state.format = format;
  state.compress = compress;
  reset_pending_run();
  state.compressor_pid = -1;

  if (compress == PC_TRACE_COMPRESS_NONE) {
    state.stream = fopen(path, "wb");
  } else {
    state.stream = open_compressed_stream(path, &state.compressor_pid);
  }
  Assert(state.stream != NULL, "failed to open pc trace output '%s'", path);
  setvbuf(state.stream, NULL, _IOFBF, 1 << 20);
  write_header(state.stream, format);
  exec_observer_set(&observer);
}

void pc_trace_disable(void) {
  if (!state.enabled) {
    return;
  }

  flush_run();
  Assert(fflush(state.stream) == 0, "failed to flush pc trace stream");
  Assert(fclose(state.stream) == 0, "failed to close pc trace stream");
  if (state.compressor_pid > 0) {
    int status = 0;
    Assert(waitpid(state.compressor_pid, &status, 0) == state.compressor_pid,
        "failed to wait for pc trace compressor");
    Assert(WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "pc trace compressor exited abnormally");
  }

  exec_observer_clear();
  state = (PcTraceState){0};
}

bool pc_trace_is_enabled(void) {
  return state.enabled;
}

void pc_trace_set_recording(bool enabled) {
  if (!state.enabled || state.recording == enabled) {
    return;
  }
  if (!enabled) {
    flush_run();
  } else {
    reset_pending_run();
  }
  state.recording = enabled;
}

const ExecObserver *pc_trace_observer(void) {
  return state.enabled ? &observer : NULL;
}
