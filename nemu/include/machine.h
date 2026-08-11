#ifndef __MACHINE_H__
#define __MACHINE_H__

#include <common.h>

typedef struct NemuMachine {
  const char *name;
  bool requires_image;
  void (*init)(void);
  long (*load_file)(const char *path);
  long (*load_blob)(const void *buf, size_t size);
  long (*load_default_image)(void);
  void (*reset)(void);
  vaddr_t (*reset_vector)(void);
  void (*step)(void);
  bool (*mmio_read)(paddr_t addr, int len, word_t *data);
  bool (*mmio_write)(paddr_t addr, int len, word_t data);
} NemuMachine;

bool machine_select(const char *name);
const NemuMachine *machine_get(void);
const char *machine_name(void);

void machine_init(void);
long machine_load_image(const char *path);
long machine_load_embedded_image(const void *buf, size_t size);
void machine_reset(void);
vaddr_t machine_reset_vector(void);
void machine_step(void);
bool machine_mmio_read(paddr_t addr, int len, word_t *data);
bool machine_mmio_write(paddr_t addr, int len, word_t data);

#endif
