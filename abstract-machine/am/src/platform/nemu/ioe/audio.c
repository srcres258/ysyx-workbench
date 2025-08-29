#include <am.h>
#include <nemu.h>

#define AUDIO_FREQ_ADDR      (AUDIO_ADDR + 0x00)
#define AUDIO_CHANNELS_ADDR  (AUDIO_ADDR + 0x04)
#define AUDIO_SAMPLES_ADDR   (AUDIO_ADDR + 0x08)
#define AUDIO_SBUF_SIZE_ADDR (AUDIO_ADDR + 0x0c)
#define AUDIO_INIT_ADDR      (AUDIO_ADDR + 0x10)
#define AUDIO_COUNT_ADDR     (AUDIO_ADDR + 0x14)

void __am_audio_init() {
}

void __am_audio_config(AM_AUDIO_CONFIG_T *cfg) {
#ifdef CONFIG_AUDIO
  uint32_t sbuf_size;

  sbuf_size = inl(AUDIO_SBUF_SIZE_ADDR);
  cfg->present = sbuf_size > 0;
  cfg->bufsize = sbuf_size;
#else
  cfg->present = false;
  cfg->bufsize = 0;
#endif
}

void __am_audio_ctrl(AM_AUDIO_CTRL_T *ctrl) {
#ifdef CONFIG_AUDIO
  outl(AUDIO_FREQ_ADDR, ctrl->freq);
  outl(AUDIO_CHANNELS_ADDR, ctrl->channels);
  outl(AUDIO_SAMPLES_ADDR, ctrl->samples);
  outl(AUDIO_INIT_ADDR, 1);
#endif
}

void __am_audio_status(AM_AUDIO_STATUS_T *stat) {
#ifdef CONFIG_AUDIO
  stat->count = inl(AUDIO_COUNT_ADDR);
#else
  stat->count = 0;
#endif
}

void __am_audio_play(AM_AUDIO_PLAY_T *ctl) {
#ifdef CONFIG_AUDIO
  uint8_t *srcp;
  uintptr_t dstp;

  for (
    srcp = (uint8_t *) ctl->buf.start, dstp = AUDIO_SBUF_ADDR;
    srcp < (uint8_t *) ctl->buf.end;
    srcp++, dstp += sizeof(uint8_t)
  ) {
    outb(dstp, *srcp);
  }
#endif
}
