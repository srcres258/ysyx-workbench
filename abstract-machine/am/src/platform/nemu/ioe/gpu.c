#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

static AM_GPU_CONFIG_T cached_gpu_config;
static bool cached_gpu_config_available = false;

static void read_gpu_config(AM_GPU_CONFIG_T *cfg) {
  uintptr_t gpu_config_addr;
  uint16_t width, height;
  AM_GPU_CONFIG_T result;

  gpu_config_addr = (uintptr_t) VGACTL_ADDR;
  width = inw(gpu_config_addr + sizeof(uint16_t));
  height = inw(gpu_config_addr);

  result = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = width, .height = height,
    .vmemsz = 0
  };
  if (cfg) {
    *cfg = result;
  }
  cached_gpu_config = result;
  cached_gpu_config_available = true;
}

void __am_gpu_init() {
  int i, w, h;
  uint32_t *fb;

  read_gpu_config(NULL);
  w = cached_gpu_config.width;
  h = cached_gpu_config.height;
  fb = (uint32_t *) (uintptr_t) FB_ADDR;
  for (i = 0; i < w * h; i++) {
    fb[i] = i;
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  read_gpu_config(cfg);
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  size_t offset, cur_x, cur_y;
  uintptr_t cur_addr;
  uint32_t *cur_src_addr;

  if (!cached_gpu_config_available) {
    return;
  }

  offset = ctl->y * cached_gpu_config.width;
  cur_addr = FB_ADDR + offset * 4;
  cur_src_addr = (uint32_t *) ctl->pixels;
  for (cur_y = ctl->y; cur_y < ctl->y + ctl->h; cur_y++) {
    // 注意绘制区域不能超出屏幕高度，超出了就停止绘制
    if (cur_y >= cached_gpu_config.height) {
      break;
    }
    // 跳过之前的空白区域 (不需要更新图像显示的区域)
    cur_addr += ctl->x * sizeof(uint32_t);
    // 剩下的区域就是显示图像内容的
    for (cur_x = ctl->x; cur_x < ctl->x + ctl->w; cur_x++) {
      outl(cur_addr, *cur_src_addr);
      cur_addr += sizeof(uint32_t);
      cur_src_addr++;
    }
    if (ctl->x + ctl->w > cached_gpu_config.width) {
      // 如果图像有超出屏幕的部分，超出的部分无法绘制，需要手动跳过
      cur_src_addr += ctl->x + ctl->w - cached_gpu_config.width;
    } else {
      // 否则跳过本行的剩余部分区域 (不需要更新图像显示的区域)
      cur_addr += (cached_gpu_config.width - (ctl->x + ctl->w)) * sizeof(uint32_t);
    }
  }

  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
