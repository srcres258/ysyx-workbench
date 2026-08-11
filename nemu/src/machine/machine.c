#include <machine.h>

extern const NemuMachine nemu_machine_profile;
extern const NemuMachine ysyxsoc_machine_profile;

static const NemuMachine *const machine_profiles[] = {
  &nemu_machine_profile,
  &ysyxsoc_machine_profile,
};

static const NemuMachine *current_machine = &nemu_machine_profile;

bool machine_select(const char *name) {
  size_t i;
  if (name == NULL) {
    current_machine = &nemu_machine_profile;
    return true;
  }
  for (i = 0; i < ARRLEN(machine_profiles); i++) {
    if (strcmp(machine_profiles[i]->name, name) == 0) {
      current_machine = machine_profiles[i];
      return true;
    }
  }
  return false;
}

const NemuMachine *machine_get(void) {
  return current_machine;
}

const char *machine_name(void) {
  return current_machine->name;
}

void machine_init(void) {
  current_machine->init();
}

long machine_load_image(const char *path) {
  if (path != NULL) {
    return current_machine->load_file(path);
  }
  if (current_machine->load_default_image != NULL) {
    return current_machine->load_default_image();
  }
  Assert(!current_machine->requires_image,
      "machine '%s' requires an explicit image", current_machine->name);
  return 0;
}

long machine_load_embedded_image(const void *buf, size_t size) {
  return current_machine->load_blob(buf, size);
}

void machine_reset(void) {
  current_machine->reset();
}

vaddr_t machine_reset_vector(void) {
  return current_machine->reset_vector();
}

void machine_step(void) {
  current_machine->step();
}

bool machine_mmio_read(paddr_t addr, int len, word_t *data) {
  return current_machine->mmio_read != NULL && current_machine->mmio_read(addr, len, data);
}

bool machine_mmio_write(paddr_t addr, int len, word_t data) {
  return current_machine->mmio_write != NULL && current_machine->mmio_write(addr, len, data);
}
