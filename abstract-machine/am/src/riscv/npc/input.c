#include <am.h>
#include <riscv/riscv.h>

#define KEYBOARD_MMIO_ADDR  0xa0000060

#define KEYDOWN_MASK 0x8000

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
    uint32_t keycode;

    keycode = inl(KEYBOARD_MMIO_ADDR);

    kbd->keydown = keycode & KEYDOWN_MASK;
    kbd->keycode = keycode & ~KEYDOWN_MASK;
}
