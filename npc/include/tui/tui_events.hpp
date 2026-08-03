#ifndef __TUI_EVENTS_HPP__
#define __TUI_EVENTS_HPP__ 1

#include <cstdint>
#include <cstddef>
#include <deque>
#include <common.hpp>

namespace tui {

/**
 * @brief Categories of simulation events captured in the backend-owned feed.
 *
 * These are produced by the simulation engine (sim.cpp, dpi.cpp, dut.cpp,
 * sdb.cpp) and consumed by the TUI frontend for status-bar, log-panel,
 * and overlay display.  Every event carries a timestamp (execCount),
 * the originating PC, and an optional value alongside a human-readable
 * description.
 *
 * The feed is backend-only: no terminal, renderer, or UI logic.
 */
enum class EventType : uint8_t {
    INFO = 0,              ///< Generic informational event
    WATCHPOINT,            ///< Watchpoint triggered (sdb.cpp)
    HALT,                  ///< sim_halt asserted by DPI (dpi.cpp)
    TRAP_GOOD,             ///< Good trap — halt_ret == 0 (sim.cpp)
    TRAP_BAD,              ///< Bad trap — halt_ret != 0 (sim.cpp)
    ABORT,                 ///< Simulation aborted (sim.cpp)
    DIFFTEST_ACTIVATE,     ///< DiffTest comparison activated (sim.cpp)
    DIFFTEST_MISMATCH,     ///< DiffTest register mismatch (dut.cpp)
    DIFFTEST_WARNING,      ///< DiffTest warning, e.g. startPC never reached (sim.cpp)
    PAUSE,                 ///< User/automated pause request
    RESUME,                ///< Simulation resumed from pause
    RESET,                 ///< Simulation reset triggered
    CONFIG,                ///< Configuration-related event (main.cpp)
};

/**
 * @brief A single timestamped event for the TUI event feed.
 */
struct Event {
    uint64_t timestamp;       ///< execCount at which the event occurred
    EventType type;
    addr_t pc;                ///< Relevant PC (0 if N/A)
    word_t value;             ///< Optional value (0 if N/A)
    char description[256];    ///< Human-readable description (null-terminated)

    Event() : timestamp(0), type(EventType::INFO), pc(0), value(0) {
        description[0] = '\0';
    }
};

/**
 * @brief Backend-owned event feed.
 *
 * Simulation code pushes events here as they occur.  The TUI frontend
 * (future task) reads them for display.  No rendering logic — pure data.
 *
 * @note Single-threaded: assumes the simulation loop is the sole
 *       producer and the TUI loop is the sole consumer, running in
 *       the same thread.
 */
class EventFeed {
public:
    static constexpr size_t kDefaultMaxEvents = 1024;

    explicit EventFeed(size_t maxEvents = kDefaultMaxEvents);

    /// Push a pre-constructed event.
    void push(const Event &event);

    /// Push an event with inline construction.
    void push(
        uint64_t timestamp, EventType type, addr_t pc, word_t value,
        const char *desc
    );

    /// Get all events whose timestamp >= @p since.  Appends to @p out.
    size_t getEventsSince(uint64_t since, std::deque<Event> &out) const;

    /// Fill @p out with the N most recent events.  Returns the actual count.
    size_t getRecentEvents(Event *out, size_t maxCount) const;

    /// Discard all events.
    void clear();

    /// Current event count.
    size_t size() const { return events_.size(); }

    /// True if no events have been recorded.
    bool empty() const { return events_.empty(); }

private:
    std::deque<Event> events_;
    size_t maxEvents_;
};

/** Global event feed — owns events from start of simulation. */
extern EventFeed g_eventFeed;

/** Initialise the global feed (called once in simulate()). */
void initEventFeed();

} // namespace tui

#endif /* __TUI_EVENTS_HPP__ */
