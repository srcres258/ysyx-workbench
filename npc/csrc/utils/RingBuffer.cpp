#include <utils/RingBuffer.hpp>

RingBuffer::RingBuffer(size_t capacity) :
    m_buffer(capacity + 1), m_capacity(capacity + 1),
    m_start(0), m_end(0) {}

size_t RingBuffer::availableData() const {
    return m_end >= m_start ?
        m_end - m_start :
        m_capacity - (m_start - m_end);
}

size_t RingBuffer::availableSpace() const {
    // 总空间减去已用空间，再减1（环形缓冲区需留一个空位区分满/空）
    return m_capacity - availableData() - 1;
}

bool RingBuffer::empty() const {
    return m_start == m_end;
}

bool RingBuffer::full() const {
    return (m_end + 1) % m_capacity == m_start;
}

void RingBuffer::write(const std::string &data, bool overwrite) {
    if (data.size() > availableSpace() && !overwrite) {
        throw std::runtime_error("Not enough space in buffer");
    }
    for (const auto &c : data) {
        m_buffer[m_end] = c;
        m_end = (m_end + 1) % m_capacity;
    }
}

std::string RingBuffer::read(size_t amount) {
    if (amount > availableData()) {
        throw std::runtime_error("Not enough data in buffer");
    }
    std::string result;
    for (size_t i = 0; i < amount; i++) {
        result += m_buffer[m_start];
        m_start = (m_start + 1) % m_capacity;
    }
    if (empty()) {
        m_start = 0;
        m_end = 0;
    }
    return result;
}

void RingBuffer::discard(size_t amount, bool overwrite) {
    if (amount > availableSpace() && !overwrite) {
        throw std::runtime_error("Discard amount exceeds buffer space");
    }
    m_end = (m_end + amount) % m_capacity;
}
