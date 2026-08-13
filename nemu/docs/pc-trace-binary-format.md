# NEMU PC trace binary file format specification

## Scope

This document specifies the **binary on-disk / on-stream format** produced by NEMU's PC trace pipeline.

It answers four questions:

1. what file format is written;
2. how dynamic PC information is encoded;
3. where the data is produced in NEMU;
4. how a reader restores the original dynamic PC sequence.

This is the normative format description for:

- plain binary trace files;
- run-compressed binary trace files;
- the same binary stream when wrapped by external `bzip2` compression.

---

## Producer / consumer map

### Producer side

- observer hook registration: `nemu/src/trace/observer.c`
- PC trace writer: `nemu/src/trace/pc_trace.c`
- observer call site in CPU execution loop: `nemu/src/cpu/cpu-exec.c`
- CLI wiring: `nemu/src/monitor/monitor.c`

### Consumer side

- Python decoder library: `nemu/tools/trace/pc_trace.py`
- trace dumper: `nemu/tools/trace/dump_pc_trace.py`
- trace equivalence checker: `nemu/tools/trace/compare_pc_traces.py`
- NPC-vs-NEMU comparer: `nemu/tools/trace/compare_npc_itrace.py`

---

## High-level semantics

The file represents a **dynamic instruction PC sequence**.

Conceptually, the decoded result is always:

```text
PC[0], PC[1], PC[2], ..., PC[N-1]
```

where each element is the architectural PC of one dynamically executed instruction.

Important properties:

- the trace is generated from the **single existing CPU execution loop**;
- the trace is an **observer**, not a second simulator;
- the trace records `pc`, not instruction bytes or disassembly text;
- compressed and uncompressed traces must decode to the **same PC sequence**.

---

## Where the PC data comes from

The creation path is:

1. `cpu_exec()` enters the normal execution loop in `nemu/src/cpu/cpu-exec.c`;
2. `exec_once(&s, cpu.pc)` executes one instruction using the normal ISA path;
3. after `exec_once()`, the loop calls:

   ```c
   exec_observer_on_instruction(s.pc, s.dnpc);
   ```

4. if PC tracing is enabled, `exec_observer_on_instruction()` dispatches to the PC trace observer in `nemu/src/trace/pc_trace.c`;
5. that observer accumulates PCs and writes the selected binary encoding.

Current behavior:

- the observer callback receives both `pc` and `next_pc`;
- the current PC trace format uses **only `pc`**;
- `next_pc` is available for future extensions but is not serialized in version 1.

---

## Endianness and integer encoding

All serialized integers in this format are:

- **little-endian**;
- fixed width;
- independent of host compiler struct layout.

The format must **not** be interpreted as a dumped C struct image.

---

## File layout overview

Every trace stream is:

```text
+-------------------+
| header (16 bytes) |
+-------------------+
| payload records   |
+-------------------+
```

The payload record encoding depends on the `encoding` field in the header.

---

## Header format

### Serialized layout

The header is exactly **16 bytes**:

| Offset | Size | Type     | Name            | Meaning |
|-------:|-----:|----------|-----------------|---------|
| 0      | 4    | bytes    | `magic`         | ASCII `PCTR` |
| 4      | 2    | uint16le | `version`       | format version, currently `1` |
| 6      | 2    | uint16le | `header_size`   | total header size in bytes, currently `16` |
| 8      | 1    | uint8    | `address_width` | PC width in bytes, currently `4` |
| 9      | 1    | uint8    | `encoding`      | payload encoding selector |
| 10     | 1    | uint8    | `endianness`    | `1` means little-endian |
| 11     | 1    | uint8    | `reserved0`     | reserved, currently `0` |
| 12     | 4    | uint32le | `reserved1`     | reserved, currently `0` |

### Current legal values

| Field | Value |
|---|---|
| `magic` | `PCTR` |
| `version` | `1` |
| `header_size` | `16` |
| `address_width` | `4` |
| `endianness` | `1` |

For **PCTR v1**, the RUN encoding also defines one additional semantic constant:

| Property | Value |
|---|---|
| sequential RUN stride | `4` bytes |

### Encoding field values

| Value | Name | Meaning |
|---:|---|---|
| `1` | raw | one 32-bit PC per record |
| `2` | run | sequential-run compressed record stream |

### Equivalent writer logic

The header is written in `nemu/src/trace/pc_trace.c:write_header()`.

Equivalent pseudocode:

```c
write_bytes("PCTR", 4);
write_u16_le(1);      // version
write_u16_le(16);     // header size
write_u8(4);          // address width
write_u8(encoding);   // raw or run
write_u8(1);          // little-endian flag
write_u8(0);          // reserved0
write_u32_le(0);      // reserved1
```

---

## Payload encoding 1: raw format

### Meaning

Raw format is the simplest correctness/reference format.

It stores the dynamic PC sequence directly:

```text
PC0, PC1, PC2, PC3, ...
```

### Record structure

Each record is exactly one `uint32le` PC.

There is no per-record tag in raw mode.

### Serialized layout

After the 16-byte header:

| Record index | Size | Type     | Meaning |
|---:|---:|---|---|
| 0 | 4 | uint32le | `PC[0]` |
| 1 | 4 | uint32le | `PC[1]` |
| 2 | 4 | uint32le | `PC[2]` |
| ... | ... | ... | ... |

### Example

If the dynamic PC sequence is:

```text
0x30000000, 0x30000004, 0x30000008
```

then the payload bytes are conceptually:

```text
00 00 00 30
04 00 00 30
08 00 00 30
```

in little-endian 32-bit form.

### Decoding algorithm

1. read and validate the 16-byte header;
2. confirm `encoding == 1`;
3. repeatedly read 4 bytes until EOF;
4. each 4-byte chunk is one decoded PC.

### Python implementation

Implemented in `nemu/tools/trace/pc_trace.py:iter_pcs()`:

- reads 4-byte chunks;
- rejects truncated trailing chunks;
- yields one integer PC per chunk.

---

## Payload encoding 2: run format

### Motivation

Many PCs are sequential:

```text
0x80001000, 0x80001004, 0x80001008, ...
```

So instead of storing every PC separately, version 1 supports a simple run encoding.

### Record tags

Run encoding uses tagged records.

| Tag value | Name | Payload |
|---:|---|---|
| `0x01` | single-PC record | one `uint32le pc` |
| `0x02` | sequential-run record | `uint32le start_pc` + `uint32le count` |

### Record type A: single-PC record

Serialized form:

| Offset within record | Size | Type     | Meaning |
|---:|---:|---|---|
| 0 | 1 | uint8 | tag = `0x01` |
| 1 | 4 | uint32le | one PC |

Decoded result:

```text
[pc]
```

### Record type B: sequential-run record

Serialized form:

| Offset within record | Size | Type     | Meaning |
|---:|---:|---|---|
| 0 | 1 | uint8 | tag = `0x02` |
| 1 | 4 | uint32le | `start_pc` |
| 5 | 4 | uint32le | `count` |

Decoded result in **PCTR v1**:

```text
[start_pc,
 start_pc + 4,
 start_pc + 8,
 ...,
 start_pc + 4 * (count - 1)]
```

### Version-1 writer rule

`PCTR v1` accumulates **maximal sequential runs using the fixed v1 stride of 4 bytes**.

When a run is flushed:

- if encoding is `run` and run length is `1`, it writes a `0x01` single-PC record;
- if encoding is `run` and run length is `>1`, it writes a `0x02` run record.

### Example

Dynamic sequence:

```text
0x1000, 0x1004, 0x1008, 0x2000
```

can be encoded as:

```text
0x02 + start_pc=0x1000 + count=3
0x01 + pc=0x2000
```

### Decoding algorithm

1. read and validate the 16-byte header;
2. confirm `encoding == 2`;
3. read one tag byte;
4. if tag is `0x01`, read one `uint32le` and emit one PC;
5. if tag is `0x02`, read `start_pc` and `count`, emit `count` PCs spaced by the `PCTR v1` stride (`+4`);
6. if tag is anything else, fail the decode.

### Python implementation

Implemented in `nemu/tools/trace/pc_trace.py:iter_pcs()`.

---

## In-memory writer state (not serialized)

The writer keeps the following in-memory state in `PcTraceState` inside `nemu/src/trace/pc_trace.c`:

| Field | Meaning |
|---|---|
| `enabled` | whether tracing is active |
| `recording` | future gating knob; current master enable for event capture |
| `format` | raw or run |
| `compress` | none or bzip2 |
| `stream` | output stream |
| `compressor_pid` | child process id when using `bzip2` |
| `run_start_pc` | first PC of the currently accumulated sequential run |
| `run_count` | number of PCs in the currently accumulated run |

This is runtime state only.

It is **not** the file format.

---

## How the binary file is created

### Enable path

CLI options are parsed in `nemu/src/monitor/monitor.c`:

- `--pc-trace=PATH`
- `--pc-trace-format=raw|run`
- `--pc-trace-compress=none|bzip2`

Then `pc_trace_enable(path, format, compress)` is called.

### Stream creation

#### Uncompressed mode

If compression mode is `none`:

- open output with `fopen(path, "wb")`
- write the 16-byte header
- use a 1 MiB fully buffered stream

#### `bzip2` mode

If compression mode is `bzip2`:

1. create a pipe;
2. open the destination file path for write;
3. `fork()`;
4. child redirects:
   - pipe read end -> stdin
   - output file fd -> stdout
5. child executes:

   ```bash
   bzip2 -c
   ```

6. parent wraps the pipe write end as `FILE *` and writes the **same binary trace format** into it.

Therefore:

- compressed mode does **not** define a different semantic trace format;
- it wraps the exact same binary stream in external `bzip2` transport.

### Per-instruction accumulation logic

When one instruction commits:

1. current `pc` is observed;
2. if there is no active run, start a run at this `pc`;
3. if `pc == run_start_pc + run_count * 4`, extend the run;
4. otherwise flush the old run and start a new one.

### Flush points

The accumulated run is flushed when:

- a non-sequential PC breaks the run;
- trace recording transitions from enabled to disabled;
- tracing is disabled at process shutdown.

Recording boundaries are also encoding boundaries: a RUN that was in progress before
`pc_trace_set_recording(false)` must be finalized before the pause, and the first PC
recorded after `pc_trace_set_recording(true)` starts a new independent RUN.

---

## How to parse the file and reconstruct the PC trace

### Reconstruction contract

Parsing must always produce the same logical output type:

```text
Iterator[uint32_pc]
```

or an equivalent list/stream of decoded dynamic PCs.

### Plain file parsing

For plain files:

```python
from nemu.tools.trace.pc_trace import iter_pcs

for pc in iter_pcs("trace.pctrace"):
    ...
```

### Compressed file parsing

For `.bz2` files, the Python decoder automatically uses `bz2.open(...)` and then decodes the same binary payload.

```python
for pc in iter_pcs("trace.pctrace.bz2"):
    ...
```

### Reader-side validation checks

A correct reader should reject:

- bad magic;
- unsupported version;
- unsupported header size;
- unsupported address width;
- unsupported endianness flag;
- unsupported encoding id;
- truncated raw record;
- truncated `0x01` record;
- truncated `0x02` record;
- unknown run-record tag.

These checks are implemented in `nemu/tools/trace/pc_trace.py`.

---

## Reference Python data structure

The Python decoder does **not** create a complex object model for records.

Its current public semantic interface is intentionally simple:

- `iter_pcs(path)` -> iterator of decoded integer PCs
- `load_pcs(path)` -> full decoded list
- `count_pcs(path)` -> decoded event count

So the effective restored data structure is:

```python
list[int]
```

or a lazy iterator producing the same values.

---

## Worked examples

### Example 1: raw file

Decoded sequence:

```text
0x30000000, 0x30000004, 0x30000008
```

Serialized file:

```text
header(encoding=1)
00 00 00 30
04 00 00 30
08 00 00 30
```

### Example 2: run file

Decoded sequence:

```text
0x30000000, 0x30000004, 0x30000008, 0x0f000000
```

Possible serialized payload:

```text
header(encoding=2)
02 00 00 00 30 03 00 00 00
01 00 00 00 0f
```

Meaning:

- one sequential run starting at `0x30000000` with count `3`
- one single isolated PC `0x0f000000`

---

## Compression semantics

`--pc-trace-compress=bzip2` means:

```text
binary-trace-stream -> bzip2 -c -> .bz2 file
```

It does **not** mean:

- a different header;
- a different payload encoding;
- a different decode algorithm after decompression.

The semantic invariant is:

```text
decode(trace.raw)
==
decode(trace.run)
==
decode(trace.run.bz2)
```

provided they were generated from the same execution interval.

---

## Current limitations in version 1

- only 32-bit PCs are encoded;
- only `pc` is recorded, not instruction bytes or `next_pc`;
- `PCTR v1` run compression only recognizes strict `+4` sequential progressions;
- no per-record timestamps or instruction counters are serialized;
- no random-access index is stored;
- the format is designed for streaming decode, not seeking.

---

## Practical commands

Generate raw trace:

```bash
./nemu/build/riscv32-nemu-interpreter -b --machine=ysyxsoc \
  --pc-trace=/tmp/hello-raw.pctrace --pc-trace-format=raw \
  am-kernels/kernels/hello/build/hello-riscv32e-ysyxsoc.bin
```

Generate run trace with `bzip2`:

```bash
./nemu/build/riscv32-nemu-interpreter -b --machine=ysyxsoc \
  --pc-trace=/tmp/hello-run.pctrace.bz2 --pc-trace-format=run \
  --pc-trace-compress=bzip2 \
  am-kernels/kernels/hello/build/hello-riscv32e-ysyxsoc.bin
```

Dump decoded PCs:

```bash
python3 nemu/tools/trace/dump_pc_trace.py /tmp/hello-run.pctrace.bz2 --limit 32
```

Validate equivalence:

```bash
python3 nemu/tools/trace/compare_pc_traces.py \
  /tmp/hello-raw.pctrace \
  /tmp/hello-run.pctrace.bz2
```

---

## Relationship to other documentation

`nemu/docs/architecture-machines-pc-trace.md` explains the broader machine split and gives usage examples.

This file is the **format-focused specification** for the PC trace binary stream itself.
