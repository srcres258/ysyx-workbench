#ifndef __TRACE_PC_TRACE_H__
#define __TRACE_PC_TRACE_H__

#include <stdbool.h>
#include <trace/observer.h>
#include <trace/pctr_format.h>

typedef enum {
  PC_TRACE_FORMAT_RAW = 1,
  PC_TRACE_FORMAT_RUN = 2,
} PcTraceFormat;

typedef enum {
  PC_TRACE_COMPRESS_NONE = 0,
  PC_TRACE_COMPRESS_BZIP2 = 1,
} PcTraceCompress;

bool pc_trace_parse_format(const char *name, PcTraceFormat *out);
bool pc_trace_parse_compress(const char *name, PcTraceCompress *out);

void pc_trace_enable(const char *path, PcTraceFormat format, PcTraceCompress compress);
void pc_trace_disable(void);
bool pc_trace_is_enabled(void);
void pc_trace_set_recording(bool enabled);
const ExecObserver *pc_trace_observer(void);

#endif
