#include <device/keyboard.hpp>

#ifdef NPC_STANDALONE

#include <SDL2/SDL.h>
#include <utils.hpp>

#define KEYDOWN_MASK 0x8000

#define NEMU_KEYS(f) \
    f(ESCAPE) f(F1) f(F2) f(F3) f(F4) f(F5) f(F6) f(F7) f(F8) f(F9) f(F10) \
    f(F11) f(F12) f(GRAVE) f(1) f(2) f(3) f(4) f(5) f(6) f(7) f(8) f(9) f(0) \
    f(MINUS) f(EQUALS) f(BACKSPACE) f(TAB) f(Q) f(W) f(E) f(R) f(T) f(Y) f(U) \
    f(I) f(O) f(P) f(LEFTBRACKET) f(RIGHTBRACKET) f(BACKSLASH) f(CAPSLOCK) \
    f(A) f(S) f(D) f(F) f(G) f(H) f(J) f(K) f(L) f(SEMICOLON) f(APOSTROPHE) \
    f(RETURN) f(LSHIFT) f(Z) f(X) f(C) f(V) f(B) f(N) f(M) f(COMMA) f(PERIOD) \
    f(SLASH) f(RSHIFT) f(LCTRL) f(APPLICATION) f(LALT) f(SPACE) f(RALT) \
    f(RCTRL) f(UP) f(DOWN) f(LEFT) f(RIGHT) f(INSERT) f(DELETE) f(HOME) f(END) \
    f(PAGEUP) f(PAGEDOWN)

#define NEMU_KEY_NAME(k) NEMU_KEY_##k,

enum {
    NEMU_KEY_NONE = 0,
    MAP(NEMU_KEYS, NEMU_KEY_NAME)
};

#define KEY_QUEUE_LEN 1024
static int key_queue[KEY_QUEUE_LEN];
static int key_f = 0;
static int key_r = 0;

static uint32_t keymap[256] = {};
static bool keymap_initialized = false;

#define SDL_KEYMAP(k) keymap[SDL_SCANCODE_##k] = NEMU_KEY_##k;

static void init_keymap() {
    MAP(NEMU_KEYS, SDL_KEYMAP)
    keymap_initialized = true;
}

static void key_enqueue(uint32_t am_scancode) {
    int next_r = (key_r + 1) % KEY_QUEUE_LEN;
    if (next_r == key_f)
        return;
    key_queue[key_r] = (int) am_scancode;
    key_r = next_r;
}

static uint32_t key_dequeue() {
    if (key_f == key_r)
        return NEMU_KEY_NONE;
    uint32_t key = (uint32_t) key_queue[key_f];
    key_f = (key_f + 1) % KEY_QUEUE_LEN;
    return key;
}

void keyboard_update() {
    if (!keymap_initialized) {
        init_keymap();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool is_keydown = (event.type == SDL_KEYDOWN);
            SDL_Scancode sc = event.key.keysym.scancode;
            uint32_t code = keymap[sc];
            if (code != NEMU_KEY_NONE) {
                key_enqueue(code | (is_keydown ? KEYDOWN_MASK : 0));
            }
            break;
        }
        case SDL_QUIT:
            break;
        default:
            break;
        }
    }
}

uint32_t keyboard_read(uint32_t addr) {
    (void) addr;
    return key_dequeue();
}

void keyboard_write(uint32_t addr, uint32_t data, uint8_t strb) {
    (void) addr;
    (void) data;
    (void) strb;
}

#else

void keyboard_update() {}

uint32_t keyboard_read(uint32_t addr) {
    (void) addr;
    return 0;
}

void keyboard_write(uint32_t addr, uint32_t data, uint8_t strb) {
    (void) addr;
    (void) data;
    (void) strb;
}

#endif
