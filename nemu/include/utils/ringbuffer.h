#ifndef __UTILS_RINGBUFFER_H__
#define __UTILS_RINGBUFFER_H__

#include <stdint.h>
#include <stdbool.h>
#include <lib/bstrlib.h>

typedef struct {
    char *buffer;
    size_t length;
    size_t start;
    size_t end;
} RingBuffer;

RingBuffer *RingBuffer_create(size_t length, bool *success);

void RingBuffer_destroy(RingBuffer *buffer);

size_t RingBuffer_read(RingBuffer *buffer, char *target, size_t amount, bool *success);

size_t RingBuffer_write(RingBuffer *buffer, const char *data, size_t length, bool *success);

bstring RingBuffer_gets(RingBuffer *buffer, size_t amount, bool *success);

void RingBuffer_discard(RingBuffer *buffer, size_t amount);

#define RingBuffer_available_data(B) (((B)->end + 1) % (B)->length - (B)->start - 1)

#define RingBuffer_available_space(B) ((B)->length - (B)->end - 1)

#define RingBuffer_full(B) (RingBuffer_available_data((B)) - (B)->length == 0)

#define RingBuffer_empty(B) (RingBuffer_available_data((B)) == 0)

#define RingBuffer_puts(B, D, S) RingBuffer_write((B), bdata((D)), blength((D)), S)

#define RingBuffer_gets_all(B, S) RingBuffer_gets((B), RingBuffer_available_data((B)), S)

#define RingBuffer_starts_at(B) ((B)->buffer + (B)->start)

#define RingBuffer_ends_at(B) ((B)->buffer + (B)->end)

#endif /* __UTILS_RINGBUFFER_H__ */
