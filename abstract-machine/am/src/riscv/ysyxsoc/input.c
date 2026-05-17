#include <am.h>
#include <riscv/riscv.h>

// PS/2 keyboard controller APB address (offset 0 = scan code data register)
#define PS2_DATA_ADDR  0x10011000

// Read one byte from PS/2 controller; returns 0 if FIFO is empty
static inline uint8_t ps2_read_byte(void) {
  return *(volatile uint8_t *)(PS2_DATA_ADDR);
}

// PS/2 multi-byte sequence state
static bool ps2_extended = false;  // 0xE0 prefix seen
static bool ps2_released = false;  // 0xF0 prefix seen

// --- AT scan code → AM keycode lookup tables ---

#define IDX(key) AM_KEY_##key

// Normal keys (single-byte scan codes)
static const uint8_t sc_to_am[256] = {
  [0x76] = IDX(ESCAPE),    // Escape
  [0x05] = IDX(F1),        // F1
  [0x06] = IDX(F2),        // F2
  [0x04] = IDX(F3),        // F3
  [0x0C] = IDX(F4),        // F4
  [0x03] = IDX(F5),        // F5
  [0x0B] = IDX(F6),        // F6
  [0x83] = IDX(F7),        // F7
  [0x0A] = IDX(F8),        // F8
  [0x01] = IDX(F9),        // F9
  [0x09] = IDX(F10),       // F10
  [0x78] = IDX(F11),       // F11
  [0x07] = IDX(F12),       // F12
  [0x0E] = IDX(GRAVE),     // ` ~
  [0x16] = IDX(1),         // 1 !
  [0x1E] = IDX(2),         // 2 @
  [0x26] = IDX(3),         // 3 #
  [0x25] = IDX(4),         // 4 $
  [0x2E] = IDX(5),         // 5 %
  [0x36] = IDX(6),         // 6 ^
  [0x3D] = IDX(7),         // 7 &
  [0x3E] = IDX(8),         // 8 *
  [0x46] = IDX(9),         // 9 (
  [0x45] = IDX(0),         // 0 )
  [0x4E] = IDX(MINUS),     // - _
  [0x55] = IDX(EQUALS),    // = +
  [0x66] = IDX(BACKSPACE), // Backspace
  [0x0D] = IDX(TAB),       // Tab
  [0x15] = IDX(Q),         // Q
  [0x1D] = IDX(W),         // W
  [0x24] = IDX(E),         // E
  [0x2D] = IDX(R),         // R
  [0x2C] = IDX(T),         // T
  [0x35] = IDX(Y),         // Y
  [0x3C] = IDX(U),         // U
  [0x43] = IDX(I),         // I
  [0x44] = IDX(O),         // O
  [0x4D] = IDX(P),         // P
  [0x54] = IDX(LEFTBRACKET),   // [ {
  [0x5B] = IDX(RIGHTBRACKET),  // ] }
  [0x5D] = IDX(BACKSLASH),     // \ |
  [0x58] = IDX(CAPSLOCK),  // Caps Lock
  [0x1C] = IDX(A),         // A
  [0x1B] = IDX(S),         // S
  [0x23] = IDX(D),         // D
  [0x2B] = IDX(F),         // F
  [0x34] = IDX(G),         // G
  [0x33] = IDX(H),         // H
  [0x3B] = IDX(J),         // J
  [0x42] = IDX(K),         // K
  [0x4B] = IDX(L),         // L
  [0x4C] = IDX(SEMICOLON), // ; :
  [0x52] = IDX(APOSTROPHE),// ' "
  [0x5A] = IDX(RETURN),    // Enter
  [0x12] = IDX(LSHIFT),    // Left Shift
  [0x1A] = IDX(Z),         // Z
  [0x22] = IDX(X),         // X
  [0x21] = IDX(C),         // C
  [0x2A] = IDX(V),         // V
  [0x32] = IDX(B),         // B
  [0x31] = IDX(N),         // N
  [0x3A] = IDX(M),         // M
  [0x41] = IDX(COMMA),     // , <
  [0x49] = IDX(PERIOD),    // . >
  [0x4A] = IDX(SLASH),     // / ?
  [0x59] = IDX(RSHIFT),    // Right Shift
  [0x14] = IDX(LCTRL),     // Left Ctrl
  [0x11] = IDX(LALT),      // Left Alt
  [0x29] = IDX(SPACE),     // Space
};

// Extended keys (scan codes that appear after 0xE0 prefix)
static const uint8_t sc_ext_to_am[256] = {
  [0x75] = IDX(UP),        // Up arrow
  [0x72] = IDX(DOWN),      // Down arrow
  [0x6B] = IDX(LEFT),      // Left arrow
  [0x74] = IDX(RIGHT),     // Right arrow
  [0x70] = IDX(INSERT),    // Insert
  [0x71] = IDX(DELETE),    // Delete
  [0x6C] = IDX(HOME),      // Home
  [0x69] = IDX(END),       // End
  [0x7D] = IDX(PAGEUP),    // Page Up
  [0x7A] = IDX(PAGEDOWN),  // Page Down
  [0x11] = IDX(RALT),      // Right Alt (extended)
  [0x14] = IDX(RCTRL),     // Right Ctrl (extended)
  [0x2F] = IDX(APPLICATION), // Application / Menu key
  // LGUI (0xE0,0x1F) and RGUI (0xE0,0x27) have no AM counterpart → NONE
};

// Translate AT scan code to AM keycode
static int ps2_scancode_to_am_keycode(uint8_t sc, bool extended) {
  if (extended)
    return sc_ext_to_am[sc];  // unmapped extended keys → 0 = AM_KEY_NONE
  return sc_to_am[sc];        // unmapped normal keys → 0 = AM_KEY_NONE
}

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  kbd->keydown = false;
  kbd->keycode = AM_KEY_NONE;

  // Read byte from PS/2 controller FIFO
  uint8_t sc = ps2_read_byte();
  if (sc == 0) return;  // no data available

  // Handle special prefix bytes
  if (sc == 0xE0) {
    ps2_extended = true;
    return;
  }
  if (sc == 0xF0) {
    ps2_released = true;
    return;
  }

  // Translate scan code to AM keycode
  kbd->keycode = ps2_scancode_to_am_keycode(sc, ps2_extended);
  kbd->keydown = !ps2_released;

  // Reset state for next key event
  ps2_extended = false;
  ps2_released = false;
}
