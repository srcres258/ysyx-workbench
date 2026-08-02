#ifndef NPC_SIMULATOR_HPP
#define NPC_SIMULATOR_HPP

#include <cstdint>
#include <memory>

#include "npc/config.hpp"
#include "npc/state.hpp"

namespace npc {

class Simulator {
public:
    Simulator();
    ~Simulator();

    // 不可拷贝、不可移动 — Simulator 独占内部生命期资源
    Simulator(const Simulator &)            = delete;
    Simulator &operator=(const Simulator &) = delete;
    Simulator(Simulator &&)                 = delete;
    Simulator &operator=(Simulator &&)      = delete;

    bool initialize(const SimulatorConfig &config,
                    int verilatorArgc = 0,
                    const char *verilatorArgv[] = nullptr);

    bool run(bool sdbEnabled = false);

    void reset(int cycles);

    void stepInstruction();

    void stepClock();

    bool halted() const;

    SimStatus state() const;

    ProcessorState processorState() const;

    std::uint64_t execCount() const;

    std::uint64_t execClockCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace npc

#endif // NPC_SIMULATOR_HPP
