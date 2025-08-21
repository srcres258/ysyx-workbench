#include <string.h>
#include <debug.h>
#include <utils/ringbuffer.h>

#define RingBuffer_commit_read(B, A) \
    (B)->start = ((B)->start + (A)) % (B)->length

#define RingBuffer_commit_write(B, A) \
    (B)->end = ((B)->end + (A)) % (B)->length

RingBuffer *RingBuffer_create(size_t length, bool *success) {
    RingBuffer *buffer;

    buffer = calloc(1, sizeof(RingBuffer));
    check(buffer, "RingBuffer_create: Failed to allocate memory for buffer.");
    buffer->length = length + 1;
    buffer->start = 0;
    buffer->end = 0;
    buffer->buffer = calloc(buffer->length, 1);

    *success = true;
    return buffer;

error:
    *success = false;
    return NULL;
}

void RingBuffer_destroy(RingBuffer *buffer) {
    if (buffer) {
        free(buffer->buffer);
        free(buffer);
    }
}

size_t RingBuffer_read(RingBuffer *buffer, char *target, size_t amount, bool *success) {
    void *result;

    check_debug(
        amount <= RingBuffer_available_data(buffer),
        "RingBuffer_read: Not enough in the buffer: has %lu, needs %lu",
        RingBuffer_available_data(buffer), amount
    );

    Log_debug("RingBuffer_read: before reading, buffer->start = %lu, buffer->end = %lu, amount = %lu", buffer->start, buffer->end, amount);
    result = memcpy(target, RingBuffer_starts_at(buffer), amount);
    check(result, "RingBuffer_read: Failed to write buffer into data.");

    RingBuffer_commit_read(buffer, amount);

    if (buffer->end == buffer->start) {
        buffer->start = 0;
        buffer->end = 0;
    }
    Log_debug("RingBuffer_read: after reading, buffer->start = %lu, buffer->end = %lu, amount = %lu", buffer->start, buffer->end, amount);

    *success = true;
    return amount;

error:
    *success = false;
    return -1;
}

size_t RingBuffer_write(RingBuffer *buffer, const char *data, size_t length, bool *success) {
    void *result;

    Log_debug("RingBuffer_write: RingBuffer_available_data = %lu, RingBuffer_available_space = %lu", RingBuffer_available_data(buffer), RingBuffer_available_space(buffer));
    if (!RingBuffer_available_data(buffer)) {
        buffer->start = 0;
        buffer->end = 0;
    }

    check(
        length <= RingBuffer_available_space(buffer),
        "RingBuffer_write: Not enough space: %lu request, %lu available",
        RingBuffer_available_data(buffer), length
    );

    Log_debug("RingBuffer_write: before writing, buffer->start = %lu, buffer->end = %lu, length = %lu", buffer->start, buffer->end, length);
    result = memcpy(RingBuffer_ends_at(buffer), data, length);
    check(result, "RingBuffer_write: Failed to write data into buffer.");

    RingBuffer_commit_write(buffer, length);
    Log_debug("RingBuffer_write: after writing, buffer->start = %lu, buffer->end = %lu, length = %lu", buffer->start, buffer->end, length);

    *success = true;
    return length;

error:
    *success = false;
    return -1;
}

bstring RingBuffer_gets(RingBuffer *buffer, size_t amount, bool *success) {
    bstring result;

    check(amount > 0, "RingBuffer_gets: Need more than 0 for gets, you gave: %lu", amount);
    check_debug(amount <= RingBuffer_available_data(buffer), "RingBuffer_gets: Not enough in the buffer.");

    Log_debug("RingBuffer_gets: before getting, buffer->start = %lu, buffer->end = %lu, amount = %lu", buffer->start, buffer->end, amount);
    result = blk2bstr(RingBuffer_starts_at(buffer), amount);
    check(result, "RingBuffer_gets: Failed to create gets result.");
    check(blength(result) == amount, "RingBuffer_gets: Wrong result length.");

    RingBuffer_commit_read(buffer, amount);
    check(RingBuffer_available_data(buffer) >= 0, "RingBuffer_gets: Error in read commit.");
    Log_debug("RingBuffer_gets: after getting, buffer->start = %lu, buffer->end = %lu, amount = %lu", buffer->start, buffer->end, amount);

    *success = true;
    return result;

error:
    *success = false;
    return NULL;
}

void RingBuffer_discard(RingBuffer *buffer, size_t amount) {
    RingBuffer_commit_write(buffer, amount);
}
