//
// npc/tests/simulator_lifecycle_test.cpp — Lifecycle smoke tests for the NPC
// simulator library.  Uses the NpcSimulatorTest fixture (Task 4) which
// constructs and initializes a simulator in SetUp() and destroys it in
// TearDown().  No tests use run() — all assertions are against the public
// stepping/state API only.
//

#include "test_fixture.h"

#include <npc/state.hpp>

//
// Smoke: the fixture can create a Simulator with the default config without
// crashing, and the post-initialization state reflects a clean (Task 3) reset.
//
TEST_F(NpcLifecycleTest, CreateAndDestroyContext) {
    // sim_ is already constructed + initialized by SetUp().
    // TearDown() will destroy it.  Verify the baseline state.
    EXPECT_EQ(sim_->execCount(), 0u);
    EXPECT_GE(sim_->execClockCount(), 1u);
    EXPECT_FALSE(sim_->halted());
    EXPECT_EQ(sim_->state(), npc::SimStatus::Running);
}

TEST_F(NpcLifecycleTest, SequentialContextsDoNotLeakState) {
    // The fixture already has one active Simulator — destroy it first so we
    // can create fresh instances.  The singleton bridge allows only one
    // active Simulator at a time.
    sim_.reset();

    // Cycle 1
    {
        npc::Simulator sim1;
        auto cfg1 = NpcSimulatorTest::MakeDefaultConfig();
        ASSERT_TRUE(sim1.initialize(cfg1));
        EXPECT_EQ(sim1.execCount(), 0u);
        EXPECT_GE(sim1.execClockCount(), 1u);
        EXPECT_EQ(sim1.state(), npc::SimStatus::Running);
        EXPECT_FALSE(sim1.halted());
    }

    // Cycle 2 — must see the same clean baseline (Task 3 repeatability)
    {
        npc::Simulator sim2;
        auto cfg2 = NpcSimulatorTest::MakeDefaultConfig();
        ASSERT_TRUE(sim2.initialize(cfg2));
        EXPECT_EQ(sim2.execCount(), 0u);
        EXPECT_GE(sim2.execClockCount(), 1u);
        EXPECT_EQ(sim2.state(), npc::SimStatus::Running);
        EXPECT_FALSE(sim2.halted());
    }
}

TEST_F(NpcLifecycleTest, InvalidConfigurationReturnsError) {
    // Destroy the fixture's sim so we can create a fresh one with a bad config.
    sim_.reset();

    auto badCfg = NpcSimulatorTest::MakeDefaultConfig();
    badCfg.ftraceEnabled = true;
    badCfg.flashElfFilePath = "/tmp/npc-test/nonexistent_npc_test.elf";

    npc::Simulator badSim;
    EXPECT_FALSE(badSim.initialize(badCfg))
        << "initialize() should return false when ftrace ELF does not exist";
}
