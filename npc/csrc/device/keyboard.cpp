#include <SDL2/SDL.h>
#include <macro-def.hpp>
#include <utils.hpp>
#include <device/map.hpp>
#include <device/keyboard.hpp>

#define KEYDOWN_MASK 0x8000

// Note that this is not the standard
#define NPC_KEYS(f) \
  f(ESCAPE) f(F1) f(F2) f(F3) f(F4) f(F5) f(F6) f(F7) f(F8) f(F9) f(F10) f(F11) f(F12) \
f(GRAVE) f(1) f(2) f(3) f(4) f(5) f(6) f(7) f(8) f(9) f(0) f(MINUS) f(EQUALS) f(BACKSPACE) \
f(TAB) f(Q) f(W) f(E) f(R) f(T) f(Y) f(U) f(I) f(O) f(P) f(LEFTBRACKET) f(RIGHTBRACKET) f(BACKSLASH) \
f(CAPSLOCK) f(A) f(S) f(D) f(F) f(G) f(H) f(J) f(K) f(L) f(SEMICOLON) f(APOSTROPHE) f(RETURN) \
f(LSHIFT) f(Z) f(X) f(C) f(V) f(B) f(N) f(M) f(COMMA) f(PERIOD) f(SLASH) f(RSHIFT) \
f(LCTRL) f(APPLICATION) f(LALT) f(SPACE) f(RALT) f(RCTRL) \
f(UP) f(DOWN) f(LEFT) f(RIGHT) f(INSERT) f(DELETE) f(HOME) f(END) f(PAGEUP) f(PAGEDOWN)

#define NPC_KEY_NAME(k) NPC_KEY_ ## k,

enum {
    NPC_KEY_NONE = 0,
    MAP(NPC_KEYS, NPC_KEY_NAME)
};

#define SDL_KEYMAP(k) keymap[SDL_SCANCODE_ ## k] = NPC_KEY_ ## k;
static uint32_t keymap[256] = {};

static void initKeymap() {
    MAP(NPC_KEYS, SDL_KEYMAP)
}

#define KEY_QUEUE_LEN 1024
static int keyQueue[KEY_QUEUE_LEN] = {};
static int key_f = 0, key_r = 0;

static void keyEnqueue(uint32_t am_scancode) {
    keyQueue[key_r] = am_scancode;
    key_r = (key_r + 1) % KEY_QUEUE_LEN;
    Assert(key_r != key_f, "key queue overflow!");
}

static uint32_t keyDequeue() {
    uint32_t key = NPC_KEY_NONE;
    if (key_f != key_r) {
        key = keyQueue[key_f];
        key_f = (key_f + 1) % KEY_QUEUE_LEN;
    }
    return key;
}

void device_keyboard_sendKey(uint8_t scancode, bool isKeyDown) {
    if (sim_state.state == SIM_RUNNING && keymap[scancode] != NPC_KEY_NONE) {
        uint32_t am_scancode = keymap[scancode] | (isKeyDown ? KEYDOWN_MASK : 0);
        keyEnqueue(am_scancode);
    }
}

static void *keyboard_base = nullptr;

static void keyboard_io_handler(uint32_t offset, int len, bool isWrite) {
    Assert(!isWrite);
    Assert(offset == 0);
    uint32_t *keyboardAddr = (uint32_t *) keyboard_base;
    keyboardAddr[0] = keyDequeue();
}

void device_keyboard_init() {
    keyboard_base = device_map_newSpace(4);
    uint32_t *keyboardAddr = (uint32_t *) keyboard_base;
    keyboardAddr[0] = NPC_KEY_NONE;
    device_map_addMMIOMap(
        "keyboard", KEYBOARD_MMIO_ADDR, keyboard_base,
        4, keyboard_io_handler
    );
    initKeymap();
}
