#include <cstring>
#include <tui/tui_events.hpp>

namespace tui {

EventFeed g_eventFeed(1024);

EventFeed::EventFeed(size_t maxEvents)
    : maxEvents_(maxEvents > 0 ? maxEvents : 1) {}

void EventFeed::push(const Event &event) {
    events_.push_back(event);
    while (events_.size() > maxEvents_) {
        events_.pop_front();
    }
}

void EventFeed::push(
    uint64_t timestamp,
    EventType type,
    addr_t pc,
    word_t value,
    const char *desc
) {
    Event event;
    event.timestamp = timestamp;
    event.type = type;
    event.pc = pc;
    event.value = value;
    if (desc) {
        std::strncpy(event.description, desc, sizeof(event.description) - 1);
        event.description[sizeof(event.description) - 1] = '\0';
    }
    events_.push_back(event);
    while (events_.size() > maxEvents_) {
        events_.pop_front();
    }
}

size_t EventFeed::getEventsSince(uint64_t since, std::deque<Event> &out) const {
    size_t count = 0;
    for (const auto &e : events_) {
        if (e.timestamp >= since) {
            out.push_back(e);
            count++;
        }
    }
    return count;
}

size_t EventFeed::getRecentEvents(Event *out, size_t maxCount) const {
    if (events_.empty() || maxCount == 0) return 0;
    size_t count = 0;
    auto it = events_.rbegin();
    for (; it != events_.rend() && count < maxCount; ++it, ++count) {
        out[count] = *it;
    }
    return count;
}

void EventFeed::clear() {
    events_.clear();
}

void initEventFeed() {
    g_eventFeed.clear();
}

} // namespace tui
