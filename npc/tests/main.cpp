//
// npc/tests/main.cpp — Custom GoogleTest entry point for the NPC simulator test suite.
//
// Responsibilities:
//   - Register Verilator command-line args (process-global, once before any context)
//   - Initialize GoogleTest (strips gtest flags from argv)
//   - Run all tests and propagate exit status
//
// This binary links libnpc.a directly and does NOT depend on npc-runner or gtest_main.
//

#include <gtest/gtest.h>
#include <verilated.h>

int main(int argc, char **argv) {
    // Process-wide Verilator arg registration — must be called exactly once
    // before the first VerilatedContext is created by any test fixture.
    Verilated::commandArgs(argc, argv);

    // Initialize GoogleTest (strips its own flags from argv, e.g. --gtest_filter)
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
