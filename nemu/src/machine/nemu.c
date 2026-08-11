#include <isa.h>
#include <device/mmio.h>
#include <machine.h>
#include <memory/host.h>
#include <memory/paddr.h>

void init_mem(void);
void init_device(void);
void device_update(void);

extern uint64_t g_nr_guest_inst;

#define NEMU_DEVICE_BASE   0xa0000000u
#define NEMU_SERIAL_PORT   (NEMU_DEVICE_BASE + 0x000003f8u)
#define NEMU_RTC_ADDR      (NEMU_DEVICE_BASE + 0x00000048u)
#define NEMU_VGACTL_ADDR   (NEMU_DEVICE_BASE + 0x00000100u)
#define NEMU_FB_ADDR       (NEMU_DEVICE_BASE + 0x01000000u)
#define NEMU_FB_SIZE       0x00200000u

static uint8_t nemu_serial_regs[8] = {};
static uint8_t nemu_rtc_regs[8] = {};
static uint8_t nemu_vgactl_regs[8] = {};
static uint8_t nemu_fb_regs[NEMU_FB_SIZE] = {};

static const uint32_t default_image[] = {
  0x00000297,
  0x00028823,
  0x0102c503,
  0x00100073,
  0xdeadbeef,
};

static long load_file_to_reset_vector(const char *path) {
  FILE *fp = fopen(path, "rb");
  long size;
  uint8_t *buf;

  Assert(fp != NULL, "Can not open '%s'", path);
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  buf = (uint8_t *)malloc(size == 0 ? 1 : size);
  assert(buf != NULL);
  Assert(fread(buf, size, 1, fp) == 1, "Failed to read '%s'", path);
  fclose(fp);

  Log("The image is %s, size = %ld", path, size);
  paddr_load(RESET_VECTOR, buf, size);
  free(buf);
  return size;
}

static long load_blob_to_reset_vector(const void *buf, size_t size) {
  paddr_load(RESET_VECTOR, buf, size);
  return size;
}

static long load_default_image(void) {
  Log("No image is given. Use the default build-in image.");
  paddr_load(RESET_VECTOR, default_image, sizeof(default_image));
  return 4096;
}

static void init_nemu_machine(void) {
  init_mem();
  IFDEF(CONFIG_DEVICE, init_device());
  memset(nemu_serial_regs, 0, sizeof(nemu_serial_regs));
  memset(nemu_rtc_regs, 0, sizeof(nemu_rtc_regs));
  memset(nemu_vgactl_regs, 0, sizeof(nemu_vgactl_regs));
  nemu_serial_regs[5] = 0x60;
  *(uint16_t *)(void *)(nemu_vgactl_regs + 0) = 300;
  *(uint16_t *)(void *)(nemu_vgactl_regs + 2) = 400;
}

static void reset_nemu_machine(void) {
  isa_reset(RESET_VECTOR);
}

static vaddr_t nemu_reset_vector(void) {
  return RESET_VECTOR;
}

static void nemu_step(void) {
  IFDEF(CONFIG_DEVICE, device_update());
}

static bool nemu_mmio_read(paddr_t addr, int len, word_t *data) {
#ifdef CONFIG_DEVICE
  *data = mmio_read(addr, len);
  return true;
#else
  if (addr >= NEMU_SERIAL_PORT && addr + len <= NEMU_SERIAL_PORT + sizeof(nemu_serial_regs)) {
    uint32_t offset = addr - NEMU_SERIAL_PORT;
    if (offset == 0) {
      nemu_serial_regs[offset] = 0;
    } else if (offset == 5) {
      nemu_serial_regs[offset] = 0x60;
    }
    *data = host_read(nemu_serial_regs + offset, len);
    return true;
  }
  if (addr >= NEMU_RTC_ADDR && addr + len <= NEMU_RTC_ADDR + sizeof(nemu_rtc_regs)) {
    uint64_t now = g_nr_guest_inst;
    uint32_t lo = (uint32_t)(now & 0xffffffffu);
    uint32_t hi = (uint32_t)(now >> 32);
    uint32_t offset = addr - NEMU_RTC_ADDR;
    memcpy(nemu_rtc_regs, &lo, sizeof(lo));
    memcpy(nemu_rtc_regs + 4, &hi, sizeof(hi));
    *data = host_read(nemu_rtc_regs + offset, len);
    return true;
  }
  if (addr >= NEMU_VGACTL_ADDR && addr + len <= NEMU_VGACTL_ADDR + sizeof(nemu_vgactl_regs)) {
    uint32_t offset = addr - NEMU_VGACTL_ADDR;
    *data = host_read(nemu_vgactl_regs + offset, len);
    return true;
  }
  if (addr >= NEMU_FB_ADDR && addr + len <= NEMU_FB_ADDR + NEMU_FB_SIZE) {
    uint32_t offset = addr - NEMU_FB_ADDR;
    *data = host_read(nemu_fb_regs + offset, len);
    return true;
  }
  return false;
#endif
}

static bool nemu_mmio_write(paddr_t addr, int len, word_t data) {
#ifdef CONFIG_DEVICE
  mmio_write(addr, len, data);
  return true;
#else
  if (addr >= NEMU_SERIAL_PORT && addr + len <= NEMU_SERIAL_PORT + sizeof(nemu_serial_regs)) {
    uint32_t offset = addr - NEMU_SERIAL_PORT;
    host_write(nemu_serial_regs + offset, len, data);
    if (offset == 0) {
      fputc(nemu_serial_regs[offset], stdout);
      if (nemu_serial_regs[offset] == '\n' || nemu_serial_regs[offset] == '\r') {
        fflush(stdout);
      }
    }
    return true;
  }
  if (addr >= NEMU_RTC_ADDR && addr + len <= NEMU_RTC_ADDR + sizeof(nemu_rtc_regs)) {
    uint32_t offset = addr - NEMU_RTC_ADDR;
    host_write(nemu_rtc_regs + offset, len, data);
    return true;
  }
  if (addr >= NEMU_VGACTL_ADDR && addr + len <= NEMU_VGACTL_ADDR + sizeof(nemu_vgactl_regs)) {
    uint32_t offset = addr - NEMU_VGACTL_ADDR;
    host_write(nemu_vgactl_regs + offset, len, data);
    return true;
  }
  if (addr >= NEMU_FB_ADDR && addr + len <= NEMU_FB_ADDR + NEMU_FB_SIZE) {
    uint32_t offset = addr - NEMU_FB_ADDR;
    host_write(nemu_fb_regs + offset, len, data);
    return true;
  }
  return false;
#endif
}

const NemuMachine nemu_machine_profile = {
  .name = "nemu",
  .requires_image = false,
  .init = init_nemu_machine,
  .load_file = load_file_to_reset_vector,
  .load_blob = load_blob_to_reset_vector,
  .load_default_image = load_default_image,
  .reset = reset_nemu_machine,
  .reset_vector = nemu_reset_vector,
  .step = nemu_step,
  .mmio_read = nemu_mmio_read,
  .mmio_write = nemu_mmio_write,
};
