#ifndef NPC_TESTS_TEST_FIXTURE_H
#define NPC_TESTS_TEST_FIXTURE_H

#include <gtest/gtest.h>
#include <memory>

#include <npc/config.hpp>
#include <npc/simulator.hpp>

/// Reusable test fixture that provides a clean Simulator context for each test.
///
/// Constructs a Simulator in SetUp() with a minimal, safe-for-testing
/// configuration and destroys it in TearDown().  All trace channels, devices,
/// difftest, UI features, and performance counters are disabled so tests start
/// from a deterministic baseline.
///
/// Usage:
/// @code
/// TEST_F(NpcSimulatorTest, MyTest) {
///     EXPECT_EQ(sim_->execCount(), 0);
///     EXPECT_EQ(sim_->execClockCount(), 0);
/// }
/// @endcode
class NpcSimulatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        sim_ = std::make_unique<npc::Simulator>();
        auto cfg = MakeDefaultConfig();
        ASSERT_TRUE(sim_->initialize(cfg))
            << "Simulator initialization failed — check Verilated::commandArgs() was called";
    }

    void TearDown() override {
        sim_.reset(); // Triggers ~Simulator() → shutdownResources()
    }

    /// Build a minimal, safe-for-testing configuration.
    ///
    /// All trace channels, devices, difftest, UI features, and performance
    /// counters are disabled.  Output file paths are redirected to
    /// /tmp/npc-test/ to avoid polluting the build directory or colliding
    /// with concurrent runs.
    static npc::SimulatorConfig MakeDefaultConfig();

    std::unique_ptr<npc::Simulator> sim_;
};

/// Suite alias — tests that exercise constructor / destructor / initialize
/// lifecycle use this name so they are discoverable as NpcLifecycleTest.*.
using NpcLifecycleTest = NpcSimulatorTest;

/// Suite alias — tests that exercise the step/reset API are discoverable as
/// NpcSimulatorSmokeTest.*.
using NpcSimulatorSmokeTest = NpcSimulatorTest;

#endif // NPC_TESTS_TEST_FIXTURE_H
