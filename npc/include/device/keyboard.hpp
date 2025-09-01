#ifndef __DEVICE__KEYBOARD_HPP__
#define __DEVICE__KEYBOARD_HPP__ 1

#include <common.hpp>

void device_keyboard_sendKey(uint8_t scancode, bool isKeyDown);

void device_keyboard_init();

#endif
