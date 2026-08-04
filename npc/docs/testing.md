# NPC Simulator Testing

## Overview

`npc-test` is a GoogleTest binary that exercises the NPC simulator library backend.
It links directly against `libnpc.a` and does **not** depend on `npc-runner` or
`gtest_main`.  All assertions use the public `npc::Simulator` API —
`initialize`, `reset`, `stepClock`, and read-only state queries (`execCount`,
`execClockCount`, `halted`, `state`, `processorState`).  Tests never call `run()`.

### What these tests cover

- Simulator lifecycle: creation, initialization, destruction, repeatability
- Configuration error handling (e.g., bad ftrace ELF)
- Clock-level stepping and reset behaviour
- Read-only state queries

### What these tests do NOT cover

- ISA conformance or instruction semantics (that belongs to
  `am-kernels/tests/cpu-tests` or difftest)
- Peripherals / devices / NVBoard / TUI (those are exercised by the runner)
- Multi-instance Verilator concurrency (the simulator uses a process-global
  singleton bridge)

## Prerequisites

```bash
# Enter the dev shell — provides gtest, Verilator, pkg-config, toolchain
nix develop
```

GoogleTest (v1.17.0) is provisioned via `pkgs.gtest` in `flake.nix`.
`pkg-config --cflags --libs gtest` resolves inside the shell and feeds the
Makefile's `GTEST_CXXFLAGS` / `GTEST_LDFLAGS` / `GTEST_LIBS` variables.

## Building and running

| Target | What it does |
|--------|-------------|
| `make -C npc test` | Build `npc-test` only (does not run) |
| `make -C npc test-run` | Build + run all tests |
| `make -C npc test-run TEST_ARGS='...'` | Build + run with forwarded gtest flags |
| `make -C npc clean` | Remove `build/` (including `npc-test` + `libnpc.a`) |

The `test-run` target sets `ASAN_OPTIONS=detect_leaks=0:exitcode=0` and propagates
GoogleTest exit status (non-zero on failure).

Examples:

```bash
# Build only
nix develop --command make -C npc test

# Build and run all tests
nix develop --command make -C npc test-run

# List all tests without executing them
nix develop --command make -C npc test-run TEST_ARGS='--gtest_list_tests'

# Run the lifecycle suite only
nix develop --command make -C npc test-run TEST_ARGS='--gtest_filter=NpcLifecycleTest.*'

# Run the stepping/reset smoke suite only
nix develop --command make -C npc test-run TEST_ARGS='--gtest_filter=NpcSimulatorSmokeTest.*'

# Run a specific test
nix develop --command make -C npc test-run TEST_ARGS='--gtest_filter=*CreateAndDestroy*'
```

You can also run the binary directly (after building).  The simulator links with
`-fsanitize=address`, so the same `ASAN_OPTIONS` the `test-run` target uses are
required:

```bash
nix develop --command env ASAN_OPTIONS=detect_leaks=0:exitcode=0 \
  npc/build/npc-test --gtest_list_tests

nix develop --command env ASAN_OPTIONS=detect_leaks=0:exitcode=0 \
  npc/build/npc-test --gtest_filter=NpcLifecycleTest.*
```

## Test suites

### NpcLifecycleTest (3 tests)

Tests for constructor/destructor lifecycle and error handling.

| Test | What it verifies |
|------|-----------------|
| `CreateAndDestroyContext` | Simulator constructs, initializes, and reports a clean baseline |
| `SequentialContextsDoNotLeakState` | Two create/destroy cycles see identical baselines (repeatability) |
| `InvalidConfigurationReturnsError` | `initialize()` returns `false` on a bad ftrace ELF without crashing |

### NpcSimulatorSmokeTest (5 tests)

Tests for the stepping and reset API.

| Test | What it verifies |
|------|-----------------|
| `StepClockAdvancesSimulationTime` | `stepClock()` increments `execClockCount()` by 1 |
| `MultipleStepClockAccumulates` | N `stepClock()` calls add exactly N to the clock counter |
| `ResetConsumesCorrectCycles` | `reset(N)` advances the clock by at least N cycles |
| `ResetLeavesStateRunning` | After `reset(5)`, state is not Aborted or Ended |
| `ProcessorStateIsReadable` | `processorState().pc` returns a non-zero reset vector |

## Adding a test

1. **Decide the suite**: Use `NpcLifecycleTest` for lifecycle/error-handling
   tests; use `NpcSimulatorSmokeTest` for stepping/state-query tests.
   Both are aliases of `NpcSimulatorTest` (declared in
   `npc/tests/test_fixture.h`).

2. **Pick a file**:
   - Lifecycle tests → `npc/tests/simulator_lifecycle_test.cpp`
   - Smoke tests → `npc/tests/simulator_smoke_test.cpp`
   - New category → new `tests/*.cpp` file (auto-discovered by the Makefile)

3. **Write the test**:

   ```cpp
   #include "test_fixture.h"
   #include <npc/state.hpp>

   TEST_F(NpcLifecycleTest, MyNewTest) {
       // sim_ is already initialized by SetUp() with MakeDefaultConfig()
       EXPECT_EQ(sim_->execCount(), 0u);
       // …
   }
   ```

   The fixture's `SetUp()` creates a Simulator with `MakeDefaultConfig()` (all
   traces/devices/UI off).  If your test needs to create additional Simulator
   instances, call `sim_.reset()` first — only one active Simulator is allowed
   per process.

4. **Build and run**:

   ```bash
   nix develop --command make -C npc test-run TEST_ARGS='--gtest_filter=*MyNewTest*'
   ```

## Architecture

```
npc/tests/main.cpp           ← custom gtest entry (Verilated::commandArgs + RUN_ALL_TESTS)
npc/tests/test_fixture.h     ← NpcSimulatorTest + suite aliases + MakeDefaultConfig()
npc/tests/test_support.cpp   ← MakeDefaultConfig() impl + nvboard_bind_all_pins stub
npc/tests/simulator_lifecycle_test.cpp
npc/tests/simulator_smoke_test.cpp
```

The test binary (`build/npc-test`) links:
- `libnpc.a` — simulator backend archive (csrc/*.cpp)
- `VysyxSoCFull__ALL.a` — Verilator-generated RTL
- `nvboard.a` — NVBoard archive (for symbol resolution, unused at runtime)
- `gtest` (via `pkg-config --libs gtest`) — **not** `gtest_main`

Neither `npc-runner` nor `auto_bind.cpp` (NVBoard pin bindings) are linked.
The `nvboard_bind_all_pins` symbol is satisfied by a no-op stub in
`test_support.cpp`.

## Limitations

- **One Simulator per process**: Multiple Simulator instances cannot coexist.
  Tests that create their own Simulators must `sim_.reset()` the fixture's
  instance first.

- **No `run()`**: The runner's `run()` method calls `shutdownResources()` at
  exit, making post-run state queries undefined.  Tests stay at the stepping
  API level.

- **No guest program**: `MakeDefaultConfig()` disables devices and flash, so
  no test payload is loaded.  `stepInstruction()` is untested for this reason.

- **ISA tests live elsewhere**: The primary regression suite is
  `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run` (35 tests).
  The gtest suite validates infrastructure correctness, not instruction
  semantics.
