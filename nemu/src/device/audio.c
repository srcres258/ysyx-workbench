/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <common.h>
#include <device/map.h>
#include <SDL2/SDL.h>
#include <utils/ringbuffer.h>
#include <pthread.h>

enum {
  reg_freq,
  reg_channels,
  reg_samples,
  reg_sbuf_size,
  reg_init,
  reg_count,
  nr_reg
};

static uint8_t *sbuf = NULL;
static uint32_t *audio_base = NULL;

RingBuffer *sound_ringbuf = NULL;
// 注：由于 sound_ringbuf 全局变量涉及多线程访存，所以有必要加线程同步锁
pthread_mutex_t sound_ringbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
  int len_read, rest_len;
  bool success;

  fprintf(stderr, "audio_callback called, len = %d\n", len);
  fflush(stderr);
  pthread_mutex_lock(&sound_ringbuf_mutex);
  len_read = RingBuffer_available_data(sound_ringbuf);
  if (len_read > len) {
    len_read = len;
  }
  RingBuffer_read(sound_ringbuf, (char *) stream, len_read, &success);
  if (success) {
    if (len_read < len) {
      rest_len = len - len_read;
      SDL_memset(stream + len_read, 0, rest_len);
    }
  } else {
    SDL_memset(stream, 0, len);
  }
  pthread_mutex_unlock(&sound_ringbuf_mutex);
}

static void audio_io_handler(uint32_t offset, int len, bool is_write) {
  uint32_t freq, channels, samples, init;
  bool success;

  freq = audio_base[reg_freq];
  channels = audio_base[reg_channels];
  samples = audio_base[reg_samples];
  init = audio_base[reg_init];
  if (is_write && init) {
    SDL_AudioSpec spec = {
      .freq = freq,
      .format = AUDIO_S16SYS,
      .channels = channels,
      .silence = 0,
      .samples = samples,
      .padding = 0,
      .size = CONFIG_SB_SIZE,
      .callback = audio_callback,
      .userdata = audio_callback
    };
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_OpenAudio(&spec, NULL);
    SDL_PauseAudio(0);

    pthread_mutex_lock(&sound_ringbuf_mutex);
    if (sound_ringbuf) {
      RingBuffer_destroy(sound_ringbuf);
    }
    sound_ringbuf = RingBuffer_create(1024 * 1024 * 64, &success);
    Assert(success, "Failed to create sound ring buffer");
    pthread_mutex_unlock(&sound_ringbuf_mutex);

    audio_base[reg_init] = 0;
  } else {
    audio_base[reg_count] = 0;
    pthread_mutex_lock(&sound_ringbuf_mutex);
    if (sound_ringbuf) {
      audio_base[reg_count] = RingBuffer_available_data(sound_ringbuf);
    }
    pthread_mutex_unlock(&sound_ringbuf_mutex);
  }
}

static void sbuf_io_handler(uint32_t offset, int len, bool is_write) {
  uint8_t *buf;
  bool success;
  
  buf = (uint8_t *) sbuf;
  buf += offset;
  if (is_write && sound_ringbuf) {
    pthread_mutex_lock(&sound_ringbuf_mutex);
    if (RingBuffer_available_space(sound_ringbuf) < len) {
      Log("Warning: sound ring buffer is full, discarding data");
      fprintf(stderr, "Warning: sound ring buffer is full, discarding data\n");
      fflush(stderr);
      RingBuffer_discard(sound_ringbuf, RingBuffer_available_space(sound_ringbuf));
    }
    RingBuffer_write(sound_ringbuf, (const char *) buf, len, &success);
    Assert(success, "Failed to write to sound ring buffer");
    pthread_mutex_unlock(&sound_ringbuf_mutex);
  }
}

void init_audio() {
  uint32_t space_size = sizeof(uint32_t) * nr_reg;
  audio_base = (uint32_t *)new_space(space_size);
  uint32_t sbuf_size = CONFIG_SB_SIZE;
  audio_base[reg_sbuf_size] = sbuf_size;
  audio_base[reg_count] = 0;
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("audio", CONFIG_AUDIO_CTL_PORT, audio_base, space_size, audio_io_handler);
#else
  add_mmio_map("audio", CONFIG_AUDIO_CTL_MMIO, audio_base, space_size, audio_io_handler);
#endif

  sbuf = (uint8_t *)new_space(CONFIG_SB_SIZE);
  add_mmio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, CONFIG_SB_SIZE, sbuf_io_handler);
}
