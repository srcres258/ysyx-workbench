#ifndef __UTILS__RINGBUFFER_HPP__
#define __UTILS__RINGBUFFER_HPP__ 1

#include <vector>
#include <stdexcept>
#include <string>

/**
 * @brief 环形缓冲区。
 */
class RingBuffer {
public:
    /**
     * @brief 构造一个新的环形缓冲区。
     * 
     * @param capacity 该环形缓冲区的容量
     */
    explicit RingBuffer(size_t capacity);

    /**
     * @brief 获取环形缓冲区中已经存在的数据大小。
     * 
     * @return size_t 环形缓冲区中已经存在的数据大小
     */
    size_t availableData() const;

    /**
     * @brief 获取环形缓冲区中可用空间的大小。
     * 
     * @return size_t 环形缓冲区中可用空间的大小
     */
    size_t availableSpace() const;

    /**
     * @brief 判断环形缓冲区是否为空。
     * 
     * @return true 环形缓冲区为空
     * @return false 环形缓冲区不为空
     */
    bool empty() const;

    /**
     * @brief 判断环形缓冲区是否已满。
     * 
     * @return true 环形缓冲区已满
     * @return false 环形缓冲区未满
     */
    bool full() const;

    /**
     * @brief 向环形缓冲区写入数据。
     * 
     * @param data 要写入的数据
     * @param overwrite 若丢弃量大于可用空间，是否覆盖旧数据
     */
    void write(const std::string &data, bool overwrite = false);

    /**
     * @brief 从环形缓冲区中读取数据。
     * 
     * @param amount 要读取的数据量
     * @return std::string 读取的数据
     */
    std::string read(size_t amount);

    /**
     * @brief 从环形缓冲区中丢弃数据。
     * 
     * @param amount 要丢弃的数据量
     * @param overwrite 若丢弃量大于可用空间，是否覆盖旧数据
     */
    void discard(size_t amount, bool overwrite = false);

private:
    std::vector<char> m_buffer;
    size_t m_capacity;
    size_t m_start;
    size_t m_end;
};

#endif /* __UTILS__RINGBUFFER_HPP__ */
