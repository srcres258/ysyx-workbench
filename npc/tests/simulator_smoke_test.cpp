//
// npc/tests/simulator_smoke_test.cpp — Backend smoke tests for the NPC
// simulator stepping and reset API.  All assertions use the public read-only
// state queries; no tests call run().
//

#include "test_fixture.h"

#include <npc/state.hpp>

TEST_F(NpcSimulatorSmokeTest, StepClockAdvancesSimulationTime) {
    const auto before = sim_->execClockCount();
    sim_->stepClock();
    EXPECT_EQ(sim_->execClockCount(), before + 1);
}

TEST_F(NpcSimulatorSmokeTest, MultipleStepClockAccumulates) {
    const auto before = sim_->execClockCount();
    constexpr int kSteps = 3;
    for (int i = 0; i < kSteps; ++i) {
        sim_->stepClock();
    }
    EXPECT_EQ(sim_->execClockCount(), before + kSteps);
}

TEST_F(NpcSimulatorSmokeTest, ResetConsumesCorrectCycles) {
    const auto before = sim_->execClockCount();
    constexpr int kCycles = 7;
    sim_->reset(kCycles);
    EXPECT_GE(sim_->execClockCount(), before + kCycles);
}

TEST_F(NpcSimulatorSmokeTest, ResetLeavesStateRunning) {
    sim_->reset(5);
    const auto s = sim_->state();
    EXPECT_NE(s, npc::SimStatus::Aborted);
    EXPECT_NE(s, npc::SimStatus::Ended);
}

TEST_F(NpcSimulatorSmokeTest, ProcessorStateIsReadable) {
    const auto ps = sim_->processorState();
    EXPECT_NE(ps.pc, 0u); // reset vector should be nonzero in toy cpu
}
